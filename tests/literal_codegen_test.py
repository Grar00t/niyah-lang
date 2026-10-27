#!/usr/bin/env python3
"""Check literal-RHS lowering in executable instructions, then observe full i64."""
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

def command(args):
    p = subprocess.run(args, capture_output=True, timeout=15)
    if p.returncode or p.stderr:
        raise AssertionError(f"{args[0]}: exit={p.returncode}: {p.stderr.decode(errors='replace')}")
    return p.stdout

def main():
    compiler = str(Path(os.environ.get("NIYAHC", "./niyahc")).resolve())
    probe = str(Path(os.environ.get("NATIVE_RESULT_PROBE", "./tests/native_result_probe")).resolve())
    # Negative source literals are unary nodes and deliberately use the general
    # path. Full-width constants above INT32_MAX must never be sign-extended.
    cases = [
        ("signed32_max", "9 + 2147483647", 2147483656, 0, 2147483647),
        ("outside_signed32", "9 + 2147483648", 2147483657, 0, 2147483648),
        ("high_bits", "9 + 4294967297", 4294967306, 0, 4294967297),
        ("maximum", "0 + 9223372036854775807", 9223372036854775807, 0, 9223372036854775807),
        ("subtract", "9 - 2147483648", -2147483639, 0, 2147483648),
        ("negative_general", "9 + -2147483648", -2147483639, 1, None),
        ("outside_negative_general", "9 + -2147483649", -2147483640, 1, None),
        ("mixed", "(9 + 2147483648) - (7 * 3)", 2147483636, 1, 2147483648),
    ]
    with tempfile.TemporaryDirectory(prefix="niyah-literal-test-") as folder:
        folder = Path(folder)
        for name, expression, wanted, pairs, literal in cases:
            source, elf, code = [folder / (name + suffix) for suffix in (".nya", ".elf", ".code")]
            source.write_text(f"دالة رئيسية() -> صحيح {{ أرجع {expression}؛ }}\n")
            command([compiler, "-o", str(elf), str(source)])
            data = elf.read_bytes()
            ph = struct.unpack_from("<Q", data, 32)[0]
            vaddr = struct.unpack_from("<Q", data, ph + 16)[0]
            entry = struct.unpack_from("<Q", data, 24)[0] - vaddr
            rx_end = struct.unpack_from("<Q", data, ph + 32)[0]
            # Trap stubs follow function bodies and interleave instructions with
            # message bytes. Exclude them rather than disassembling text as code.
            trap = data.index(bytes.fromhex("b801000000bf02000000488d35"), entry, rx_end)
            instructions = data[entry:trap]
            code.write_bytes(instructions)
            dis = command(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64",
                           "-M", "intel", "--no-show-raw-insn", str(code)]).decode()
            pushes = len(re.findall(r"\bpush\s+rax\b", dis))
            pops = len(re.findall(r"\bpop\s+rax\b", dis))
            assert (pushes, pops) == (pairs, pairs), (name, pushes, pops, pairs)
            if literal is not None:
                encoded = (bytes.fromhex("48c7c1") + struct.pack("<i", literal)
                           if -(1 << 31) <= literal < (1 << 31)
                           else bytes.fromhex("48b9") + struct.pack("<Q", literal))
                assert encoded in instructions, (name, "wrong immediate encoding")
            observed = command([probe, str(elf)])
            assert observed == f"RESULT={wanted}\n".encode(), (name, observed, wanted)
    print(f"LITERAL_CODEGEN_PASS: {len(cases)} encoding/stack/full-i64 cases")

if __name__ == "__main__":
    main()
