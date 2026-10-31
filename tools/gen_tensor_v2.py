#!/usr/bin/env python3
"""Seal the syscall-only v2 ELF and absolute-relocation instruction fragments.

GNU as/ld and Python run only while building niyahc. Generated programs require
neither a linker nor libc, and the legacy v1 generator/runtime are untouched.
"""
import hashlib
import json
import pathlib
import struct
import subprocess
import sys


class Elf:
    def __init__(self, path):
        self.data = pathlib.Path(path).read_bytes()
        self.h = struct.unpack_from('<16sHHIQQQIHHHHHH', self.data)
        assert self.h[0][:6] == b'\x7fELF\x02\x01'
        assert self.h[2] == 62
        self.sections = [struct.unpack_from('<IIQQQQIIQQ', self.data,
                         self.h[6] + i * 64) for i in range(self.h[12])]
        names = self.sections[self.h[13]]
        raw = self.data[names[4]:names[4] + names[5]]
        self.names = [raw[s[0]:].split(b'\0', 1)[0].decode()
                      for s in self.sections]
        self.index = {n: i for i, n in enumerate(self.names)}
        self.symbols = []
        self.symbol = {}
        if '.symtab' in self.index:
            sec = self.sections[self.index['.symtab']]
            strings = self.sections[sec[6]]
            raw = self.data[strings[4]:strings[4] + strings[5]]
            for off in range(sec[4], sec[4] + sec[5], 24):
                sym = struct.unpack_from('<IBBHQQ', self.data, off)
                name = raw[sym[0]:].split(b'\0', 1)[0].decode()
                self.symbols.append((name, sym))
                if name:
                    self.symbol[name] = sym

    def sec(self, name):
        s = self.sections[self.index[name]]
        return self.data[s[4]:s[4] + s[5]] if s[1] != 8 else b''

    def va(self, name):
        return self.symbol[name][4]

    def offset(self, name):
        sym = self.symbol[name]
        sec = self.sections[sym[3]]
        assert sec[1] != 8
        return sec[4] + sym[4] - sec[3]

    def relocs(self, name):
        for s in self.sections:
            if s[1] == 4 and self.names[s[7]] == name:
                for off in range(s[4], s[4] + s[5], 24):
                    addr, info, addend = struct.unpack_from('<QQq', self.data, off)
                    yield addr, info & 0xffffffff, info >> 32, addend

    def validate_static(self):
        assert self.h[1] == 2
        for i in range(self.h[10]):
            ph = struct.unpack_from('<IIQQQQQQ', self.data, self.h[5] + i * 56)
            assert ph[0] not in (2, 3), 'dynamic/interpreter segment'
            assert ph[1] & 3 != 3, 'writable executable segment'
        assert not any(name and s[3] == 0 and s[1] >> 4 != 2
                       for name, s in self.symbols), 'undefined symbol'


def generate(root, model):
    out = root / 'build/tensor_core'
    out.mkdir(parents=True, exist_ok=True)

    def run(args):
        return subprocess.run([str(a) for a in args], cwd=out,
                              capture_output=True, check=True).stdout

    run(['as', '--64', '-o', 'v2_runtime.o', root / 'src/tensor_v2_runtime.s'])
    run(['as', '--64', '-o', 'v2_dispatch.o', model / 'src/tensor_dispatch.s'])
    run(['ld', '-static', '--build-id=none', '-z', 'noexecstack',
         '-o', 'v2_template.elf', 'v2_runtime.o', 'v2_dispatch.o'])
    template = Elf(out / 'v2_template.elf')
    template.validate_static()
    assert len(template.sec('.text')) >= 65536
    config_at = template.offset('nx_tilecfg')
    config = template.data[config_at:config_at + 64]
    assert len(config) == 64 and config[:2] == b'\x01\x00'
    assert config[2:16] == bytes(14) and config[32:48] == bytes(16)
    assert config[56:64] == bytes(8)
    assert struct.unpack_from('<8H', config, 16) == (64,) * 8
    assert config[48:56] == bytes([16] * 8)

    chunks = []

    def chunk(name, body):
        chunks.append((name, body))
        return len(chunks) - 1

    ids = {}
    ids['PROLOGUE'] = chunk('prologue', 'push r12\npush r13\nmov r13,rsp\n'
                           'sub rsp,OFFSET FLAT:pa\nand rsp,-64\nmov r12,rsp')
    ids['EPILOGUE'] = chunk('epilogue', 'mov rsp,r13\npop r13\npop r12\nret')
    ids['COPY_IN'] = chunk('copy_in', 'movabs rsi,OFFSET FLAT:pa\n'
                          'lea rdi,[r12+pb]\nmov ecx,128\nrep movsq')
    ids['COPY_OUT'] = chunk('copy_out', 'lea rsi,[r12+pa]\n'
                           'movabs rdi,OFFSET FLAT:pb\nmov ecx,128\nrep movsq')
    ids['PACK_IN'] = chunk('pack_in', 'movabs rdi,OFFSET FLAT:pa\n'
                          'lea rsi,[r12+pb]\n'
                          f'movabs rax,{template.va("nx_pack_b")}\ncall rax')
    ids['ARGS3'] = chunk('args3', 'lea rdi,[r12+pa]\nlea rsi,[r12+pb]\n'
                        'lea rdx,[r12+pc]')
    ids['CALL'] = chunk('call', 'movabs rax,OFFSET FLAT:pa\ncall rax')
    ids['CALL_R15'] = chunk('call_r15', 'call r15')
    ids['LDTILECFG'] = chunk('ldtilecfg', 'movabs rax,OFFSET FLAT:pa\n'
                            'ldtilecfg [rax]')
    ids['TILERELEASE'] = chunk('tilerelease', 'tilerelease')
    loads = [chunk('load' + str(i), f'lea rdi,[r12+pa]\nmov eax,64\n'
                   f'tileloadd tmm{i},[rdi+rax*1]') for i in range(8)]
    stores = [chunk('store' + str(i), f'lea rdi,[r12+pa]\nmov eax,64\n'
                    f'tilestored [rdi+rax*1],tmm{i}') for i in range(8)]
    gemms = []
    for dst in range(8):
        for a in range(8):
            for b in range(8):
                gemms.append(chunk(f'gemm{dst}{a}{b}', f'tilezero tmm{dst}\n'
                             f'tdpbssd tmm{dst},tmm{a},tmm{b}')
                             if len({dst, a, b}) == 3 else 0xffffffff)
    source = '.intel_syntax noprefix\n.text\n'
    source += ''.join(f'zt2_{name}:\n{body}\nze2_{name}:\n'
                      for name, body in chunks)
    source += '.section .note.GNU-stack,"",@progbits\n'
    (out / 'v2_instructions.s').write_text(source)
    run(['as', '--64', '-o', 'v2_instructions.o', 'v2_instructions.s'])
    instructions = Elf(out / 'v2_instructions.o')
    desc = []
    patches = []
    relocations = list(instructions.relocs('.text'))
    for name, _ in chunks:
        lo, hi = instructions.va('zt2_' + name), instructions.va('ze2_' + name)
        first = len(patches)
        for off, kind, symbol, addend in relocations:
            if lo <= off < hi:
                assert kind in (1, 10, 11), ('non-absolute relocation', kind)
                param = instructions.symbols[symbol][0]
                assert param in ('pa', 'pb', 'pc'), param
                width = 8 if kind == 1 else 4
                assert off + width <= hi
                patches.append((off - lo, ('pa', 'pb', 'pc').index(param),
                                width, addend))
        desc.append((lo, hi - lo, first, len(patches) - first))

    constants = {
        'ELF_SIZE': len(template.data),
        'VECTOR_OFFSET': template.offset('nx_vector_program'),
        'VECTOR_VA': template.va('nx_vector_program'),
        'VECTOR_CAPACITY': 32768,
        'AMX_OFFSET': template.offset('nx_amx_program'),
        'AMX_VA': template.va('nx_amx_program'),
        'AMX_CAPACITY': 32768,
        'PLAN_OFFSET': template.offset('nx_plan'),
        'PLAN_VA': template.va('nx_plan'),
        'PLAN_SIZE': 16,
        'INPUTS_VA': template.va('nx_inputs'),
        'INPUT_CAPACITY': 65536,
        'OUTPUTS_VA': template.va('nx_outputs'),
        'OUTPUT_CAPACITY': 131072,
        'TILECFG_VA': template.va('nx_tilecfg'),
        'PACK_B_VA': template.va('nx_pack_b'),
        'ADD_VA': template.va('nx_i32_add'),
        'RELU_VA': template.va('nx_i32_relu'),
        'TRANSPOSE_VA': template.va('nx_i32_transpose'),
        'GEMM_SCALAR_VA': template.va('nx_i8_scalar'),
        'GEMM_AVX2_VA': template.va('nx_i8_avx2'),
    }
    for name in ('VECTOR', 'AMX'):
        at = constants[name + '_OFFSET']
        assert template.data[at:at + 32768] == bytes([0xcc]) * 32768
    lines = ['/* Generated at build time; absolute patch parameters, no runtime tools. */',
             '#ifndef NIYAH_TENSOR_V2_TEMPLATES_H',
             '#define NIYAH_TENSOR_V2_TEMPLATES_H',
             'typedef struct { unsigned off,len,first,count; } T2Chunk;',
             'typedef struct { unsigned off,param,width; long addend; } T2Patch;']
    lines += [f'#define T2_{name} {value}u' for name, value in constants.items()]
    lines += [f'#define T2_{name} {value}u' for name, value in ids.items()]

    def byte_array(name, data):
        lines.append('static const unsigned char ' + name + '[] = {')
        lines.extend(','.join(str(x) for x in data[i:i + 24]) + ','
                     for i in range(0, len(data), 24))
        lines.append('};')

    byte_array('t2_elf', template.data)
    byte_array('t2_chunks', instructions.sec('.text'))
    for name, typ, rows in [('t2_chunk', 'T2Chunk', desc),
                            ('t2_patch', 'T2Patch', patches)]:
        lines.append('static const ' + typ + ' ' + name + '[] = {')
        lines.extend('{' + ','.join(str(v) for v in row) + '},' for row in rows)
        lines.append('};')
    for name, values in [('t2_load', loads), ('t2_store', stores), ('t2_gemm', gemms)]:
        lines.append('static const unsigned ' + name + '[] = {' +
                     ','.join(str(v) + 'u' for v in values) + '};')
    lines.append('#endif')
    header = ('\n'.join(lines) + '\n').encode()
    (out / 'tensor_v2_templates.h').write_bytes(header)
    manifest = {
        'schema': 'niyah-tensor-v2-native-template-1',
        'template_sha256': hashlib.sha256(template.data).hexdigest(),
        'header_sha256': hashlib.sha256(header).hexdigest(),
        'runtime_source_sha256': hashlib.sha256(
            (root / 'src/tensor_v2_runtime.s').read_bytes()).hexdigest(),
        'unchanged_dispatch_source_sha256': hashlib.sha256(
            (model / 'src/tensor_dispatch.s').read_bytes()).hexdigest(),
        'static_no_interp_no_dynamic_no_wx': True,
        'constants': constants,
        'chunk_ids': ids,
        'chunk_count': len(chunks),
        'patch_count': len(patches),
        'chunks': desc,
        'patches': patches,
        'loads': loads,
        'stores': stores,
        'gemms': gemms,
    }
    (out / 'v2_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({k: v for k, v in manifest.items()
                      if k not in ('chunks', 'patches', 'loads', 'stores', 'gemms')}))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print('usage: gen_tensor_v2.py LANG_ROOT MODEL_ROOT', file=sys.stderr)
        raise SystemExit(64)
    generate(pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve())
