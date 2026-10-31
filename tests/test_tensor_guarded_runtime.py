#!/usr/bin/env python3
"""Hosted guarded-memory oracle for exact existing native runtime routine bytes."""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
ROUTINES = ("nx_i8_scalar", "nx_i8_avx2", "nx_i8_avx512", "nx_pack_b")
THUNKS = """.intel_syntax noprefix
.text
.macro THUNK name,target
.global \\name
.type \\name,@function
\\name:
 push rbp
 push rbx
 push r12
 push r13
 push r14
 push r15
 sub rsp,8
 call \\target
 add rsp,8
 pop r15
 pop r14
 pop r13
 pop r12
 pop rbx
 pop rbp
 ret
.size \\name,.-\\name
.endm
THUNK guarded_scalar,nx_i8_scalar
THUNK guarded_avx2,nx_i8_avx2
THUNK guarded_avx512,nx_i8_avx512
THUNK guarded_pack,nx_pack_b
.section .note.GNU-stack,"",@progbits
"""

def sha(data):
    return hashlib.sha256(data).hexdigest()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runtime", type=pathlib.Path,
                    default=ROOT.parent / "backend/src/tensor_runtime.s")
    ap.add_argument("--evidence", type=pathlib.Path,
                    default=ROOT / "build/tensor_core/guarded_runtime_receipt.json")
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = ap.parse_args()
    out = args.evidence.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    work = out.parent / "guarded_runtime_build"
    work.mkdir(exist_ok=True)
    records = []
    receipt = {"schema": "niyah-guarded-native-runtime-v1", "status": "FAIL",
               "exit_code": 1, "commands": records, "routine_byte_parity": [],
               "scope": "Hosted tests only; extracted existing native routine bytes, guarded mmap and independent int64 C oracle. No AMX execution."}
    started = time.perf_counter()

    def run(argv):
        q = subprocess.run([str(x) for x in argv], capture_output=True, timeout=60)
        records.append({"argv": [str(x) for x in argv], "expected_exit": 0,
                        "exit": q.returncode, "stdout_bytes": len(q.stdout),
                        "stdout_sha256": sha(q.stdout),
                        "stderr": q.stderr.decode(errors="replace")[:2000]})
        if q.returncode:
            raise RuntimeError((argv, q.returncode, q.stderr.decode(errors="replace")[:2000]))
        return q.stdout

    def symbols(obj):
        found = {}
        for line in run(["nm", "-n", "--defined-only", obj]).decode().splitlines():
            fields = line.split()
            if len(fields) == 3 and fields[2] in ROUTINES:
                found[fields[2]] = int(fields[0], 16)
        if set(found) != set(ROUTINES):
            raise AssertionError(("missing runtime routine symbols", found))
        return found

    try:
        runtime = args.runtime.resolve()
        source = runtime.read_bytes()
        text = source.decode("utf-8")
        for name in ROUTINES:
            if text.splitlines().count(name + ":") != 1:
                raise AssertionError(("ambiguous runtime label", name))
        begin = text.index("nx_i8_scalar:\n")
        end = text.index('.section .note.GNU-stack', begin)
        body = text[begin:end]
        original = work / "original-runtime.s"
        extracted = work / "routines.s"
        thunks = work / "thunks.s"
        original.write_bytes(source)
        extracted.write_text('.intel_syntax noprefix\n.text\n.global ' +
                             ",".join(ROUTINES) + "\n" + body +
                             '.section .note.GNU-stack,"",@progbits\n')
        thunks.write_text(THUNKS)
        original_obj = work / "original.o"
        extracted_obj = work / "routines.o"
        thunk_obj = work / "thunks.o"
        run(["as", "--64", "-o", original_obj, original])
        run(["as", "--64", "-o", extracted_obj, extracted])
        run(["as", "--64", "-o", thunk_obj, thunks])
        original_text = work / "original.text"
        extracted_text = work / "routines.text"
        run(["objcopy", "-O", "binary", "--only-section=.text", original_obj, original_text])
        run(["objcopy", "-O", "binary", "--only-section=.text", extracted_obj, extracted_text])
        old_bytes, new_bytes = original_text.read_bytes(), extracted_text.read_bytes()
        old_symbols, new_symbols = symbols(original_obj), symbols(extracted_obj)
        for i, name in enumerate(ROUTINES):
            next_name = ROUTINES[i + 1] if i + 1 < len(ROUTINES) else None
            old_end = old_symbols[next_name] if next_name else len(old_bytes)
            new_end = new_symbols[next_name] if next_name else len(new_bytes)
            original_body = old_bytes[old_symbols[name]:old_end]
            extracted_body = new_bytes[new_symbols[name]:new_end]
            if not original_body or original_body != extracted_body:
                raise AssertionError(("native routine byte mismatch", name))
            receipt["routine_byte_parity"].append(
                {"symbol": name, "bytes": len(original_body),
                 "original_sha256": sha(original_body),
                 "extracted_sha256": sha(extracted_body), "equal": True})
        harness = ROOT / "tests/tensor_guarded_runtime.c"
        exe = work / "guarded-runtime"
        flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                 "-fno-tree-vectorize", "-fno-tree-slp-vectorize",
                 "-mno-avx", "-mno-avx2", "-mno-avx512f", "-fno-pie", "-no-pie",
                 "-Wl,--build-id=none,-z,noexecstack"]
        run([args.cc, *flags, "-o", exe, harness, extracted_obj, thunk_obj])
        result = json.loads(run([exe]))
        if result.get("status") != "PASS":
            raise AssertionError(("native harness did not report completion", result))
        receipt.update({"status": "PASS", "exit_code": 0, "counts": result,
                        "runtime_source": {"path": str(runtime), "bytes": len(source),
                                           "sha256": sha(source)},
                        "harness_source_sha256": sha(harness.read_bytes()),
                        "harness_binary": {"path": str(exe), "bytes": exe.stat().st_size,
                                           "sha256": sha(exe.read_bytes())},
                        "build_test_dependencies": ["C compiler", "GNU as", "nm", "objcopy",
                                                    "hosted libc/CPU detection; none added to production"]})
    except Exception as exc:
        receipt["error"] = repr(exc)
    finally:
        receipt["seconds"] = time.perf_counter() - started
        receipt["executed_command_count"] = len(records)
        out.write_text(json.dumps(receipt, indent=2) + "\n")
        print(json.dumps({k: v for k, v in receipt.items() if k != "commands"}, indent=2))
    return receipt["exit_code"]

if __name__ == "__main__":
    sys.exit(main())
