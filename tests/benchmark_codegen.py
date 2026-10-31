#!/usr/bin/env python3
"""Compare two niyahc builds without timing tracing or sanitized programs.

All outputs are retained in a new evidence directory. --timings is explicit:
run it only after concurrent build/test work has stopped. No network is used.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import re
import shutil
import statistics
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
FLAGS = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]
SOURCES = ["src/compile_cli.c", "src/codegen.c", "src/check.c", "src/parser.c", "src/lexer.c"]
HEADERS = ["src/codegen.h", "src/check.h", "src/parser.h", "src/lexer.h"]
TRAP_MESSAGES = [
    b"runtime: signed arithmetic overflow\n", b"runtime: division by zero\n",
    b"runtime: shift count outside 0..63\n", b"runtime: execution budget exhausted\n",
    b"runtime: call depth exceeded\n", b"runtime: uninitialized variable\n",
]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def run(command: list[str], *, cwd: Path | None = None, timeout: int = 30) -> tuple[subprocess.CompletedProcess, int]:
    start = time.perf_counter_ns()
    result = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    return result, time.perf_counter_ns() - start


def require_clean(result: subprocess.CompletedProcess, code: int = 0) -> None:
    if result.returncode != code or result.stdout or result.stderr:
        raise RuntimeError(f"command {result.args!r}: rc={result.returncode}, "
                           f"stdout={result.stdout!r}, stderr={result.stderr!r}")


def source_manifest(root: Path) -> dict:
    return {name: sha(root / name) for name in SOURCES + HEADERS}


def workloads() -> list[dict]:
    cases = []
    for name, expression, expected in [
        ("literal_add", "١٧ + ٣", 20), ("literal_subtract", "١٧ - ٣", 14),
        ("literal_multiply", "١٧ * ٣", 51), ("literal_bitwise", "١٧ | ٣", 19),
        ("literal_shift", "١٧ << ٢", 68), ("literal_divide_control", "١٧ / ٣", 5),
    ]:
        cases.append({"name": name, "source": f"دالة رئيسية() -> صحيح {{ أرجع {expression}؛ }}\n",
                      "expected": expected, "target_expression": True, "time_execution": False})
    expression = "س"
    for _ in range(16):
        expression = f"(({expression} * ٣ + ٧) & 65535)"
    iterations = 120000
    value = 1
    for _ in range(iterations * 16):
        value = (value * 3 + 7) & 65535
    cases.append({"name": "arithmetic", "source":
        f"دالة رئيسية() -> صحيح {{ متغير س: صحيح = ١؛ متغير ع: صحيح = ٠؛ "
        f"طالما (ع < {iterations}) {{ س = {expression}؛ ع = ع + ١؛ }} أرجع س؛ }}\n",
        "expected": value, "target_expression": False, "time_execution": True})
    iterations = 200000
    cases.append({"name": "loop_sum", "source":
        f"دالة رئيسية() -> صحيح {{ متغير س: صحيح = ٠؛ متغير ع: صحيح = ٠؛ "
        f"طالما (ع < {iterations}) {{ س = س + ع؛ ع = ع + ١؛ }} أرجع س؛ }}\n",
        "expected": iterations * (iterations - 1) // 2, "target_expression": False, "time_execution": True})
    iterations = 120000
    value = 1
    for _ in range(iterations):
        value = (value * 3 + 7) & 65535
    cases.append({"name": "calls", "source":
        "دالة عدل(س: صحيح) -> صحيح { أرجع (س * ٣ + ٧) & 65535؛ } "
        f"دالة رئيسية() -> صحيح {{ متغير س: صحيح = ١؛ متغير ع: صحيح = ٠؛ "
        f"طالما (ع < {iterations}) {{ س = عدل(س)؛ ع = ع + ١؛ }} أرجع س؛ }}\n",
        "expected": value, "target_expression": False, "time_execution": True})
    return cases


def disassemble(data: bytes, address: int, destination: Path) -> dict:
    raw_path = destination.with_suffix(".bin")
    asm_path = destination.with_suffix(".asm")
    raw_path.write_bytes(data)
    command = ["objdump", "-D", "-b", "binary", "-m", "i386:x86-64", "-M", "intel",
               "--no-show-raw-insn", "--disassemble-zeroes", f"--adjust-vma={address}", str(raw_path)]
    result, _ = run(command)
    if result.returncode or result.stderr:
        raise RuntimeError(f"objdump failed: {result.stderr!r}")
    asm_path.write_bytes(result.stdout)
    instructions = re.findall(rb"^[ \t]*[0-9a-f]+:[ \t]+([^ \t\r\n]+)(?:[ \t]+[^\r\n]*)?$", result.stdout, re.MULTILINE)
    if not instructions or b"(bad)" in instructions:
        raise RuntimeError(f"invalid instruction region: {raw_path}")
    return {"address_start": hex(address), "address_end": hex(address + len(data)),
            "instruction_bytes": len(data), "instruction_count": len(instructions),
            "push_count": sum(i.startswith(b"push") for i in instructions),
            "pop_count": sum(i.startswith(b"pop") for i in instructions),
            "raw_file": raw_path.name, "disassembly_file": asm_path.name}


def inspect_elf(path: Path, destination: Path, target_expression: bool) -> dict:
    data = path.read_bytes()
    if data[:7] != b"\x7fELF\x02\x01\x01":
        raise RuntimeError(f"expected little-endian ELF64: {path}")
    entry, phoff = struct.unpack_from("<QQ", data, 24)
    phsize, phcount = struct.unpack_from("<HH", data, 54)
    segments = [struct.unpack_from("<IIQQQQQQ", data, phoff + i * phsize) for i in range(phcount)]
    executable = [p for p in segments if p[0] == 1 and p[1] & 1]
    if len(executable) != 1:
        raise RuntimeError("expected exactly one executable PT_LOAD")
    segment = executable[0]
    offset, virtual, filesz = segment[2], segment[3], segment[5]
    start, end = offset + entry - virtual, offset + filesz
    if not (offset <= start < end <= len(data)):
        raise RuntimeError("invalid executable bounds")
    code_end = end
    traps = []
    # Decode the known emitted trap layout from the end, validating every byte.
    # No objdump pass is allowed to interpret message strings or ELF padding.
    for index in reversed(range(len(TRAP_MESSAGES))):
        message = TRAP_MESSAGES[index]
        code = (b"\xb8" + struct.pack("<I", 1) + b"\xbf" + struct.pack("<I", 2) +
                b"\x48\x8d\x35" + struct.pack("<i", 21) + b"\xba" + struct.pack("<I", len(message)) +
                b"\x0f\x05\xbf" + struct.pack("<I", 120 + index) + b"\xb8" + struct.pack("<I", 60) +
                b"\x0f\x05\x0f\x0b")
        first = end - len(code) - len(message)
        if first < start or data[first:end] != code + message:
            raise RuntimeError("trap layout changed; refusing to guess code/data boundaries")
        traps.append((index, first, code, len(message)))
        end = first
    program = disassemble(data[start:end], virtual + start - offset, destination / "program")
    trap_regions = []
    for index, first, code, message_size in reversed(traps):
        region = disassemble(code, virtual + first - offset, destination / f"trap-{120 + index}")
        region["excluded_message_bytes"] = message_size
        trap_regions.append(region)
    total = {key: program[key] + sum(t[key] for t in trap_regions)
             for key in ["instruction_bytes", "instruction_count", "push_count", "pop_count"]}
    measured = {"elf_file_bytes": len(data), "rx_segment_file_bytes": filesz,
                "excluded_header_bytes": start - offset,
                "excluded_trap_message_bytes": sum(len(m) for m in TRAP_MESSAGES),
                "excluded_post_rx_bytes": len(data) - code_end,
                "program": program, "trap_instruction_regions": trap_regions, "all_instructions": total}
    if total["instruction_bytes"] + measured["excluded_header_bytes"] + measured["excluded_trap_message_bytes"] != filesz:
        raise RuntimeError("instruction/data accounting mismatch")
    if target_expression:
        tick = re.compile(re.escape(b"\x4d\x85\xed\x0f\x84") + b".{4}" + re.escape(b"\x49\xff\xcd"), re.DOTALL)
        ticks = list(tick.finditer(data, start, end))
        if len(ticks) != 1:
            raise RuntimeError("sample needs exactly one return-statement tick")
        expr_start = ticks[0].end()
        expr_end = data.find(b"\x49\xff\xcc\xc9\xc3", expr_start, end)
        if expr_end <= expr_start:
            raise RuntimeError("sample return epilogue not found")
        measured["target_expression"] = disassemble(data[expr_start:expr_end], virtual + expr_start - offset,
                                                      destination / "target-expression")
    return measured


def percentile(values: list[float], quantile: float) -> float:
    ordered = sorted(values)
    position = (len(ordered) - 1) * quantile
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def summary(values: list[int]) -> dict:
    median = statistics.median(values)
    return {"samples_ns": values, "median_ns": median,
            "mad_ns": statistics.median(abs(v - median) for v in values),
            "p10_ns": percentile(values, .1), "p25_ns": percentile(values, .25),
            "p75_ns": percentile(values, .75), "p90_ns": percentile(values, .9),
            "min_ns": min(values), "max_ns": max(values)}


def compare_times(samples: dict[str, list[int]], seed: int) -> dict:
    result = {label: summary(values) for label, values in samples.items()}
    ratios = [a / b for a, b in zip(samples["baseline"], samples["current"])]
    rng = random.Random(seed)
    bootstrap = [statistics.median(rng.choices(ratios, k=len(ratios))) for _ in range(2000)]
    low, high = percentile(bootstrap, .025), percentile(bootstrap, .975)
    a, b = result["baseline"], result["current"]
    overlapping_iqr = max(a["p25_ns"], b["p25_ns"]) <= min(a["p75_ns"], b["p75_ns"])
    classification = "indistinguishable_or_noise"
    if not overlapping_iqr and low > 1:
        classification = "current_faster_in_this_sample"
    elif not overlapping_iqr and high < 1:
        classification = "current_slower_in_this_sample"
    result.update({"paired_speed_ratio_median": statistics.median(ratios),
                   "paired_ratio_bootstrap_95pct_interval": [low, high],
                   "interquartile_ranges_overlap": overlapping_iqr, "classification": classification})
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--current", type=Path, default=ROOT / "niyahc")
    parser.add_argument("--probe", type=Path, default=ROOT / "tests/native_result_probe")
    parser.add_argument("--output", type=Path, required=True, help="new evidence directory; must not exist")
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--timings", action="store_true", help="enable paired timing after heavy tests stop")
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--repetitions", type=int, default=21)
    parser.add_argument("--cpu", type=int, help="default: lowest CPU in the process affinity mask")
    args = parser.parse_args()
    if args.warmup < 1 or args.repetitions < 7:
        parser.error("warmup must be >=1 and repetitions >=7")
    compilers = {"baseline": args.baseline.resolve(), "current": args.current.resolve()}
    probe = args.probe.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    cc = shutil.which(args.cc)
    if not cc or not shutil.which("objdump"):
        raise RuntimeError("host C compiler and objdump are required")
    version, _ = run([cc, "--version"])
    objdump_version, _ = run(["objdump", "--version"])
    affinity = sorted(os.sched_getaffinity(0))
    selected_cpu = min(affinity) if args.cpu is None else args.cpu
    if selected_cpu not in affinity:
        raise RuntimeError("selected CPU is outside current affinity")
    os.sched_setaffinity(0, {selected_cpu})
    environment = {"uname": platform.uname()._asdict(), "python": sys.version,
                   "cpuinfo": Path("/proc/cpuinfo").read_text(),
                   "os_release": Path("/etc/os-release").read_text(),
                   "compiler": cc, "compiler_version": version.stdout.decode(), "host_flags": FLAGS,
                   "objdump_version": objdump_version.stdout.decode(),
                   "benchmark_script_sha256": sha(Path(__file__)), "load_average_start": os.getloadavg(),
                   "original_affinity": affinity, "measurement_cpu": selected_cpu,
                   "clock": "time.perf_counter_ns", "warmup": args.warmup, "repetitions": args.repetitions,
                   "timings_enabled": args.timings, "probe_sha256": sha(probe),
                   "method": "fresh output per compile, paired alternating order, subprocess wall time; tracing only for correctness",
                   "instruction_metrics": "static encoded instruction bytes/counts and static push/pop counts, not hardware counters",
                   "limits": "Runtime timings include process startup. Compile timings include read/emission/fsync/publication. "
                             "Bootstrap intervals describe these samples, not general performance guarantees."}
    save_json(output / "environment.json", environment)
    manifests, binaries = {}, {}
    for label, compiler in compilers.items():
        root = compiler.parent
        manifests[label] = source_manifest(root)
        binaries[label] = sha(compiler)
        rebuilt = output / f"{label}-host-rebuilt"
        command = [cc, *FLAGS, "-o", str(rebuilt), *SOURCES]
        result, _ = run(command, cwd=root)
        (output / f"{label}-host-build.stdout").write_bytes(result.stdout)
        (output / f"{label}-host-build.stderr").write_bytes(result.stderr)
        require_clean(result)
        save_json(output / f"{label}-host-build.json", {"command": command, "cwd": str(root),
                  "source_sha256": manifests[label], "provided_binary": str(compiler),
                  "provided_sha256": binaries[label], "rebuilt_sha256": sha(rebuilt)})
        if sha(rebuilt) != binaries[label]:
            raise RuntimeError(f"{label}: supplied compiler does not match identical unsanitized host build")
    cases = workloads()
    results = {}
    for case in cases:
        case_dir = output / case["name"]
        case_dir.mkdir()
        source = case_dir / "program.niy"
        source.write_text(case["source"], encoding="utf-8")
        record = {"expected_i64": case["expected"], "source_sha256": sha(source), "variants": {}}
        for label, compiler in compilers.items():
            variant = case_dir / label
            variant.mkdir()
            executable = variant / "program.elf"
            result, _ = run([str(compiler), "-o", str(executable), str(source)])
            require_clean(result)
            observed, _ = run([str(probe), str(executable)])
            (variant / "correctness.stdout").write_bytes(observed.stdout)
            (variant / "correctness.stderr").write_bytes(observed.stderr)
            if observed.returncode or observed.stdout != f"RESULT={case['expected']}\n".encode() or observed.stderr:
                raise RuntimeError(f"full-i64 correctness failed for {case['name']}/{label}")
            raw, _ = run([str(executable)])
            require_clean(raw, case["expected"] & 255)
            measured = inspect_elf(executable, variant, case["target_expression"])
            measured.update({"sha256": sha(executable), "correctness": "full-i64-and-exit-status-pass"})
            record["variants"][label] = measured
        before, after = record["variants"]["baseline"], record["variants"]["current"]
        record["program_delta_current_minus_baseline"] = {
            key: after["program"][key] - before["program"][key]
            for key in ["instruction_bytes", "instruction_count", "push_count", "pop_count"]}
        if case["target_expression"]:
            record["target_expression_delta_current_minus_baseline"] = {
                key: after["target_expression"][key] - before["target_expression"][key]
                for key in ["instruction_bytes", "instruction_count", "push_count", "pop_count"]}
        if args.timings:
            for operation in ["compile"] + (["execute"] if case["time_execution"] else []):
                samples = {"baseline": [], "current": []}
                for repetition in range(-args.warmup, args.repetitions):
                    labels = ["baseline", "current"] if repetition % 2 == 0 else ["current", "baseline"]
                    for label in labels:
                        variant = case_dir / label
                        if operation == "compile":
                            phase = f"warmup{-repetition}" if repetition < 0 else f"sample{repetition}"
                            target = variant / f"compile-{phase}.elf"
                            command = [str(compilers[label]), "-o", str(target), str(source)]
                            expected = 0
                        else:
                            command = [str(variant / "program.elf")]
                            expected = case["expected"] & 255
                        completed, elapsed = run(command)
                        require_clean(completed, expected)
                        if repetition >= 0:
                            samples[label].append(elapsed)
                record[f"{operation}_wall_time"] = compare_times(samples, seed=1729)
        results[case["name"]] = record
        save_json(case_dir / "measurements.json", record)
        save_json(output / "results.json", {"environment": "environment.json", "cases": results})
    for label, compiler in compilers.items():
        if source_manifest(compiler.parent) != manifests[label] or sha(compiler) != binaries[label]:
            raise RuntimeError(f"{label} sources/compiler changed during measurement")
    if sha(probe) != environment["probe_sha256"]:
        raise RuntimeError("correctness probe changed during measurement")
    save_json(output / "completion.json", {"status": "complete", "case_count": len(cases),
              "timings_enabled": args.timings, "source_and_binary_stability": "verified",
              "load_average_end": os.getloadavg()})
    print(json.dumps({"output": str(output), "cases": len(cases), "timings": args.timings}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"benchmark failed: {error}", file=sys.stderr)
        raise SystemExit(1)
