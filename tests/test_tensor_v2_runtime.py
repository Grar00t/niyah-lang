#!/usr/bin/env python3
"""Independent arithmetic, ABI and syscall-contract tests for the v2 runtime."""
import argparse
import hashlib
import importlib.util
import json
import os
import pathlib
import random
import struct
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--model', type=pathlib.Path,
                    default=pathlib.Path(os.environ.get('NIYAH_MODEL', ROOT.parent / 'model')))
    ap.add_argument('--evidence', type=pathlib.Path,
                    default=ROOT / 'build/tensor_core/v2_runtime_receipt.json')
    args = ap.parse_args()
    subprocess.run(['python3', str(ROOT / 'tools/gen_tensor_v2.py'), str(ROOT),
                    str(args.model.resolve())], capture_output=True, check=True)
    out = ROOT / 'build/tensor_core'
    manifest = json.loads((out / 'v2_manifest.json').read_text())
    template = (out / 'v2_template.elf').read_bytes()
    spec = importlib.util.spec_from_file_location('tensor_v2_generator', ROOT / 'tools/gen_tensor_v2.py')
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    fragment_bytes = generator.Elf(out / 'v2_instructions.o').sec('.text')
    constants = manifest['constants']
    ids = manifest['chunk_ids']
    cases_checked = 0
    output_values_checked = 0
    exits = []

    def fragment(kind, *params):
        idx = ids[kind] if isinstance(kind, str) else kind
        at, length, first, count = manifest['chunks'][idx]
        data = bytearray(fragment_bytes[at:at + length])
        for off, param, width, addend in manifest['patches'][first:first + count]:
            value = params[param] + addend
            assert 0 <= value < (1 << (width * 8))
            data[off:off + width] = value.to_bytes(width, 'little')
        return bytes(data)

    def program(path, vector, amx=None, nv=3, no=4, ni=2, ne=1):
        data = bytearray(template)
        for prefix, code in [('VECTOR', vector), ('AMX', amx if amx is not None else vector)]:
            assert len(code) <= constants[prefix + '_CAPACITY']
            at = constants[prefix + '_OFFSET']
            data[at:at + len(code)] = code
        struct.pack_into('<4I', data, constants['PLAN_OFFSET'], nv, no, ni, ne)
        path.write_bytes(data)
        path.chmod(0o700)
        return path

    def run(path, mode, payload=None, expected=0):
        q = subprocess.run([str(path), mode], input=payload, capture_output=True, timeout=5)
        assert q.returncode == expected, (path.name, mode, q.returncode, expected, q.stderr)
        assert not q.stderr
        if expected:
            assert not q.stdout
        return q.stdout

    def oracle_mm(a, b):
        aa = [x if x < 128 else x - 256 for x in a]
        bb = [x if x < 128 else x - 256 for x in b]
        return struct.pack('<256i', *(sum(aa[i * 64 + k] * bb[k * 16 + j]
                                          for k in range(64))
                                     for i in range(16) for j in range(16)))

    def inputs(frame=3072):
        return (fragment('PROLOGUE', frame) +
                fragment('COPY_IN', constants['INPUTS_VA'], 0) +
                fragment('COPY_IN', constants['INPUTS_VA'] + 1024, 1024))

    end = fragment('COPY_OUT', 2048, constants['OUTPUTS_VA']) + fragment('EPILOGUE')
    vector_mm = inputs() + fragment('ARGS3', 0, 1024, 2048) + fragment('CALL_R15') + end
    amx_mm = (fragment('PROLOGUE', 3072) +
              fragment('COPY_IN', constants['INPUTS_VA'], 0) +
              fragment('PACK_IN', constants['INPUTS_VA'] + 1024, 1024) +
              fragment('LDTILECFG', constants['TILECFG_VA']) +
              fragment(manifest['loads'][0], 0) + fragment(manifest['loads'][1], 1024) +
              fragment(manifest['gemms'][(2 * 8 + 0) * 8 + 1]) +
              fragment(manifest['stores'][2], 2048) + fragment('TILERELEASE') + end)
    rnd = random.Random(2026102801)
    with tempfile.TemporaryDirectory(prefix='niyah-v2-runtime-') as temp:
        td = pathlib.Path(temp)
        mm = program(td / 'matmul', vector_mm, amx_mm)
        fields = struct.unpack('<8Q', run(mm, 'features'))
        assert fields[0] == int.from_bytes(b'NXISA001', 'little')
        modes = ['scalar']
        if fields[1] & 1:
            modes.append('avx2')
        if fields[6] != 77:
            modes.append('auto')
        if fields[6] == 3:
            modes.append('amx')
        boundaries = (-128, -127, -1, 0, 1, 126, 127)
        fixtures = [(bytes([a & 255]) * 1024, bytes([b & 255]) * 1024)
                    for a in boundaries for b in boundaries]
        fixtures += [(bytes(range(256)) * 4, bytes(reversed(range(256))) * 4)]
        fixtures += [(rnd.randbytes(1024), rnd.randbytes(1024)) for _ in range(32)]
        for a, b in fixtures:
            want = oracle_mm(a, b)
            for mode in modes:
                assert run(mm, mode, a + b) == want, ('matmul', mode)
                cases_checked += 1
                output_values_checked += 256

        # Scalar i32 helpers preserve the runtime's r15 GEMM pointer and stack
        # homes. ADD deliberately wraps; ReLU compares signed i32 values.
        helpers = {}
        for name in ('ADD', 'RELU', 'TRANSPOSE'):
            code = inputs() + fragment('ARGS3', 0, 1024, 2048)
            code += fragment('CALL', constants[name + '_VA']) + end
            helpers[name] = program(td / name.lower(), code)
        ints = (-2147483648, -2147483647, -1, 0, 1, 2147483646, 2147483647)
        for iteration in range(40):
            a = [ints[i % len(ints)] for i in range(256)] if not iteration else [rnd.randrange(-2**31, 2**31) for _ in range(256)]
            b = [ints[(i + 3) % len(ints)] for i in range(256)] if not iteration else [rnd.randrange(-2**31, 2**31) for _ in range(256)]
            payload = struct.pack('<512i', *(a + b))
            wants = {
                'ADD': struct.pack('<256I', *((x + y) & 0xffffffff for x, y in zip(a, b))),
                'RELU': struct.pack('<256i', *(max(x, 0) for x in a)),
                'TRANSPOSE': struct.pack('<256i', *(a[j * 16 + i] for i in range(16) for j in range(16))),
            }
            for name, exe in helpers.items():
                for mode in modes:
                    assert run(exe, mode, payload) == wants[name], (name, mode)
                    cases_checked += 1
                    output_values_checked += 256

        # Pack helper exercised as ordinary x86 code, independently of AMX.
        pack_code = fragment('PROLOGUE', 2048) + fragment('PACK_IN', constants['INPUTS_VA'], 1024)
        pack_code += fragment('COPY_OUT', 1024, constants['OUTPUTS_VA']) + fragment('EPILOGUE')
        pack = program(td / 'pack', pack_code, nv=2, no=2, ni=1)
        for raw in [bytes(range(256)) * 4] + [rnd.randbytes(1024) for _ in range(31)]:
            want = bytes(raw[(4 * g + lane) * 16 + j]
                         for g in range(16) for j in range(16) for lane in range(4))
            assert run(pack, 'scalar', raw) == want
            cases_checked += 1

        # A helper call followed by GEMM detects accidental r15 corruption.
        chain = inputs() + fragment('ARGS3', 0, 1024, 2048)
        chain += fragment('CALL', constants['ADD_VA'])
        chain += fragment('CALL', constants['RELU_VA'])
        chain += fragment('CALL', constants['TRANSPOSE_VA'])
        chain += fragment('ARGS3', 0, 1024, 2048) + fragment('CALL_R15') + end
        preserve = program(td / 'preserve', chain)
        a, b = fixtures[-1]
        for mode in ('scalar', 'avx2') if fields[1] & 1 else ('scalar',):
            assert run(preserve, mode, a + b) == oracle_mm(a, b)

        for mode, payload, code in [('scalar', b'', 65), ('scalar', bytes(2047), 65),
                                    ('scalar', bytes(2049), 65), ('invalid', b'', 64)]:
            run(mm, mode, payload, code)
            exits.append({'case': mode + ':' + str(len(payload)), 'exit': code})
        if fields[6] != 3:
            run(mm, 'amx', b'', 77)
            exits.append({'case': 'unusable_amx', 'exit': 77})
        q = subprocess.run([str(mm), 'scalar', 'extra'], capture_output=True, timeout=5)
        assert q.returncode == 64 and not q.stdout
        q = subprocess.run([str(mm), 'scalar'], preexec_fn=lambda: os.close(0),
                           capture_output=True, timeout=5)
        assert q.returncode == 66 and not q.stdout
        exits.append({'case': 'closed_stdin', 'exit': 66})
        read_fd, write_fd = os.pipe()
        os.close(read_fd)
        try:
            q = subprocess.run([str(mm), 'scalar'], input=bytes(2048), stdout=write_fd,
                               stderr=subprocess.PIPE, timeout=5)
        finally:
            os.close(write_fd)
        assert q.returncode == 74
        exits.append({'case': 'closed_stdout_pipe', 'exit': 74})
        with open('/dev/full', 'wb') as full:
            q = subprocess.run([str(mm), 'scalar'], input=bytes(2048), stdout=full,
                               stderr=subprocess.PIPE, timeout=5)
        assert q.returncode == 74
        exits.append({'case': 'dev_full', 'exit': 74})

        # Separate no-capability fixture: synthetic flags never reach an AVX or
        # AMX instruction. Production dispatcher/source/template stay unchanged.
        fake = td / 'no_caps.s'
        fake.write_text('.intel_syntax noprefix\n.bss\n.global nx_cpu\n'
                        'nx_cpu:.skip 64\n.text\n.global nx_detect_int8,nx_amx_permission\n'
                        'nx_detect_int8: xor eax,eax\nret\n'
                        'nx_amx_permission: mov eax,77\nret\n'
                        '.section .note.GNU-stack,"",@progbits\n')
        subprocess.run(['as', '--64', '-o', str(td / 'no_caps.o'), str(fake)], check=True)
        subprocess.run(['ld', '-static', '--build-id=none', '-z', 'noexecstack',
                        '-o', str(td / 'no_caps'), str(out / 'v2_runtime.o'),
                        str(td / 'no_caps.o')], check=True)
        for mode in ('auto', 'avx2', 'amx'):
            run(td / 'no_caps', mode, b'', 77)
        exits.append({'case': 'no_caps_auto_avx2_amx', 'exit': 77})

    result = {
        'status': 'PASS',
        'template_sha256': hashlib.sha256(template).hexdigest(),
        'native_matmul_fixtures': len(fixtures),
        'modes': modes,
        'arithmetic_and_pack_cases': cases_checked,
        'i32_output_values_checked': output_values_checked,
        'i32_helper_fixtures': 40,
        'pack_fixtures': 32,
        'helper_preserves_generated_frame_and_gemm_pointer': True,
        'native_amx_gemm_executed': 'amx' in modes,
        'features': list(fields),
        'exit_checks': exits,
        'unchanged_dispatch_source_sha256': manifest['unchanged_dispatch_source_sha256'],
        'scope': 'native arithmetic and syscall contracts; AMX validated only when native_amx_gemm_executed is true',
    }
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
