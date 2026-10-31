#!/usr/bin/env python3
"""Build and exercise the primary compiler's Arabic fixed-shape tensor example.

Python, make and the C/GNU build tools are build/demo dependencies only.
No command here is run by the emitted executable or by niyahc while compiling.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "examples" / "tensor_intrinsics.niyah"
MODE_NAMES = ("scalar", "avx2", "avx512", "amx")


class DemoFailure(RuntimeError):
    pass


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def command(argv: list[str], env: dict[str, str], data: bytes | None = None,
            timeout: int = 30) -> subprocess.CompletedProcess[bytes]:
    try:
        return subprocess.run(
            argv, cwd=ROOT, env=env, input=data, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, timeout=timeout, check=False,
        )
    except OSError as exc:
        raise DemoFailure(f"cannot run {argv[0]}: {exc}") from exc
    except subprocess.TimeoutExpired as exc:
        raise DemoFailure(f"command timed out: {' '.join(argv)}") from exc


def require_success(result: subprocess.CompletedProcess[bytes], label: str) -> None:
    if result.returncode != 0:
        details = result.stderr.decode("utf-8", errors="replace").strip()
        raise DemoFailure(f"{label}: exit {result.returncode}; {details}")


def reference(a: list[int], b: list[int]) -> tuple[int, ...]:
    if len(a) != 16 * 64 or len(b) != 64 * 16:
        raise DemoFailure("internal fixture shape error")
    # B is logical row-major here; this oracle does not reuse backend packing.
    values = tuple(
        sum(a[i * 64 + k] * b[k * 16 + j] for k in range(64))
        for i in range(16) for j in range(16)
    )
    if not all(-(1 << 31) <= value < (1 << 31) for value in values):
        raise DemoFailure("reference result is outside INT32")
    return values


def fixtures() -> list[tuple[str, bytes, tuple[int, ...]]]:
    uniform_a, uniform_b = [1] * 1024, [2] * 1024
    uniform = reference(uniform_a, uniform_b)
    if uniform != (128,) * 256:
        raise DemoFailure("uniform reference must contain exactly 256 values of 128")
    patterned_a = [
        ((i * 17 + k * 29 + (i * k) % 31) & 255) - 128
        for i in range(16) for k in range(64)
    ]
    patterned_b = [
        ((k * 43 + j * 13 + (k + 1) * (j + 3)) & 255) - 128
        for k in range(64) for j in range(16)
    ]
    result = []
    for name, a, b, expected in (
        ("uniform_A1_B2", uniform_a, uniform_b, uniform),
        ("signed_nonuniform", patterned_a, patterned_b, reference(patterned_a, patterned_b)),
    ):
        if not all(-128 <= value <= 127 for value in a + b):
            raise DemoFailure(f"{name}: fixture is outside signed INT8")
        result.append((name, bytes(value & 255 for value in a + b), expected))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--model", type=Path, default=ROOT.parent / "niyah-assembly-model",
        help="backend source repository (default: sibling niyah-assembly-model)",
    )
    args = parser.parse_args()
    model = args.model.expanduser().resolve()
    for path in (SOURCE, ROOT / "Makefile", model / "Makefile",
                 model / "src" / "tile_allocator.s",
                 model / "src" / "tensor_runtime.s",
                 model / "src" / "tensor_dispatch.s"):
        if not path.is_file():
            raise DemoFailure(f"required source file is missing: {path}")

    env = os.environ.copy()
    env.update(LC_ALL="C", LANG="C")
    build_argv = ["make", "--no-print-directory", "niyahc", f"NIYAH_MODEL={model}"]
    built = command(build_argv, env, timeout=300)
    if built.stdout:
        sys.stderr.write(built.stdout.decode("utf-8", errors="replace"))
    if built.stderr:
        sys.stderr.write(built.stderr.decode("utf-8", errors="replace"))
    require_success(built, "build primary niyahc")

    compiler = ROOT / "niyahc"
    # A new destination is essential: the compiler never overwrites outputs.
    # Keep this small directory and its ELF available for inspection.
    work = Path(tempfile.mkdtemp(prefix="tensor-demo-", dir=ROOT / "build"))
    executable = work / "tensor_intrinsics.elf"
    compiled = command(
        [str(compiler), "--tensor", "-o", str(executable), str(SOURCE)], env,
    )
    require_success(compiled, "compile Arabic source through primary niyahc")
    if compiled.stdout:
        raise DemoFailure("compiler unexpectedly wrote bytes to stdout")

    feature_run = command([str(executable), "features"], env, data=b"")
    require_success(feature_run, "read generated-process feature snapshot")
    if len(feature_run.stdout) != 64 or feature_run.stdout[:8] != b"NXISA001":
        raise DemoFailure("invalid generated-process feature snapshot")
    fields = struct.unpack("<8s7Q", feature_run.stdout)
    usable, selected, permission = fields[1], fields[6], fields[7]
    if usable & ~7 or selected >= len(MODE_NAMES) or permission not in (0, 1):
        raise DemoFailure("feature snapshot contains invalid fields")

    cases = []
    skipped = []
    values_checked = 0
    fixture_data = fixtures()
    for mode in ("scalar", "auto", "avx2"):
        for name, raw_input, expected in fixture_data:
            result = command([str(executable), mode], env, data=raw_input)
            if mode == "avx2" and not (usable & 1):
                if result.returncode != 77 or result.stdout:
                    raise DemoFailure("unavailable AVX2 must return 77 with empty stdout")
                skipped.append({
                    "mode": mode, "state": "NOT_RUN", "exit_code": 77,
                    "reason": "explicit backend unavailable; no AVX2 arithmetic claimed",
                })
                break
            require_success(result, f"{name}/{mode}")
            if len(result.stdout) != 256 * 4:
                raise DemoFailure(
                    f"{name}/{mode}: expected 1024 output bytes, got {len(result.stdout)}"
                )
            actual = struct.unpack("<256i", result.stdout)
            if actual != expected:
                at = next(i for i, pair in enumerate(zip(actual, expected))
                          if pair[0] != pair[1])
                row, column = divmod(at, 16)
                raise DemoFailure(
                    f"{name}/{mode}: C[{row},{column}]={actual[at]}, "
                    f"expected {expected[at]}"
                )
            expected_bytes = struct.pack("<256i", *expected)
            if result.stdout != expected_bytes:
                raise DemoFailure(f"{name}/{mode}: complete output byte mismatch")
            values_checked += len(actual)
            cases.append({
                "fixture": name, "mode": mode, "exit_code": result.returncode,
                "input_bytes": len(raw_input), "input_sha256": digest(raw_input),
                "output_bytes": len(result.stdout), "output_sha256": digest(result.stdout),
                "reference_sha256": digest(expected_bytes), "values_checked": len(actual),
            })

    compiler_bytes, elf_bytes, source_bytes = (
        compiler.read_bytes(), executable.read_bytes(), SOURCE.read_bytes()
    )
    report = {
        "status": "PASS", "exit_code": 0,
        "compiler": {"path": str(compiler), "bytes": len(compiler_bytes),
                     "sha256": digest(compiler_bytes)},
        "source": {"path": str(SOURCE), "bytes": len(source_bytes),
                   "sha256": digest(source_bytes)},
        "executable": {"path": str(executable), "bytes": len(elf_bytes),
                       "sha256": digest(elf_bytes)},
        "build": {"argv": build_argv, "cwd": str(ROOT), "exit_code": built.returncode},
        "compile_exit_code": compiled.returncode,
        "feature_probe": {
            "exit_code": feature_run.returncode, "usable_bits": usable,
            "automatic_selection_in_probe": MODE_NAMES[selected],
            "amx_permission_in_probe": permission,
            "snapshot_sha256": digest(feature_run.stdout),
        },
        "arithmetic_cases_passed": len(cases), "values_checked": values_checked,
        "cases": cases, "unavailable_modes": skipped,
        "scope": "Two fixed fixtures against Python integer sums; not the complete acceptance suite",
        "hardware_scope": (
            "Only completed modes above are arithmetic observations. "
            "No explicit AMX or AVX512 arithmetic test is claimed by this demo."
        ),
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (DemoFailure, OSError) as exc:
        print(f"TENSOR_DEMO_FAILED: {exc}", file=sys.stderr)
        raise SystemExit(1)
