#!/usr/bin/env python3
"""Independent v2 schedule/ELF checks, including spill paths without AMX hardware.

The AMX schedule interpreter is an oracle for the emitted fixed-shape schedule,
not a claim of hardware execution. Scalar/auto/available AVX2 execute real ELF.
"""
import hashlib
import json
import os
from pathlib import Path
import random
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MODEL = Path(os.environ.get('NIYAH_MODEL', str(ROOT.parent/'niyah-assembly-model'))).resolve()
DRIVER = r'''
#include "tensor_v2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static TensorValue values[64];static TensorOp ops[128];
static unsigned char guarded[262208];
int main(int argc,char **argv){
 unsigned h[5];CodegenResult r;
 if(fread(h,4,5,stdin)!=5||h[0]>64||h[1]>128)return 65;
 if(fread(values,1024,h[0],stdin)!=h[0]||fread(ops,16,h[1],stdin)!=h[1])return 65;
 unsigned cap=argc>2?(unsigned)strtoul(argv[2],0,10):262144;
 if(cap>262144)return 65;memset(guarded,0xA5,sizeof(guarded));unsigned n=0;
 if(argc>1&&!strcmp(argv[1],"asm"))
  n=tensor_v2_export_asm(values,h[0],ops,h[1],h[2],h[3],h[4],guarded+32,cap,&r);
 else if(tensor_v2_codegen(values,h[0],ops,h[1],h[2],h[3],h[4],guarded+32,cap,&r))n=r.size;
 for(unsigned i=0;i<32;i++)if(guarded[i]!=0xA5)return 99;
 for(unsigned i=32+n;i<sizeof(guarded);i++)if(guarded[i]!=0xA5)return 99;
 if(!n){fprintf(stderr,"%s\n",r.error?r.error:"failure");return r.exit_code?r.exit_code:98;}
 return fwrite(guarded+32,1,n,stdout)==n?0:74;
}
'''


def run(args, data=None, expected=0):
    env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0'
    p = subprocess.run(list(map(str,args)),input=data,capture_output=True,timeout=60,env=env)
    assert p.returncode == expected, (args,p.returncode,p.stderr[:2000])
    return p.stdout


def ir(ops,mask):
    types=[];starts=[];ends=[];ni=ne=0
    for at,(kind,d,a,b) in enumerate(ops):
        if kind==4:
            ends[d]=at+1;ne+=1
        else:
            assert d==len(types)
            if kind<3:ni+=1
            else:
                ends[a]=at+1
                if kind in (3,6):ends[b]=at+1
            types.append(kind if kind<3 else 3);starts.append(at);ends.append(at+1)
    out=struct.pack('<5I',len(types),len(ops),ni,ne,mask)
    for typ,start,end in zip(types,starts,ends):
        out+=struct.pack('<3I',typ,start,end)+bytes(1012)
    return out+b''.join(struct.pack('<4I',*o) for o in ops),ni,ne


def signed(x):return x-256 if x>=128 else x
def words(b):return struct.unpack('<256i',b)
def packed_words(xs):return struct.pack('<256I',*(x&0xffffffff for x in xs))
def matmul(a,b,packed=False):
    return packed_words(sum(signed(a[i*64+k])*signed(b[(k//4)*64+4*j+k%4] if packed else b[k*16+j])
                            for k in range(64)) for i in range(16) for j in range(16))
def pointwise(kind,a,b=b''):
    x=words(a)
    if kind==6:return packed_words(u+v for u,v in zip(x,words(b)))
    if kind==7:return packed_words(x[j*16+i] for i in range(16) for j in range(16))
    return packed_words(max(0,u) for u in x)
def oracle(ops,data):
    vals=[];out=bytearray();pos=0
    for k,d,a,b in ops:
        if k<3:vals.append(data[pos:pos+1024]);pos+=1024
        elif k==3:vals.append(matmul(vals[a],vals[b]))
        elif k==4:out+=vals[d]
        else:vals.append(pointwise(k,vals[a],vals[b] if k==6 else b''))
    return bytes(out)


def amx_schedule(assembly,data,nv,ne):
    mem={'nx_inputs':bytearray(data),'r12':bytearray(nv*1024),'nx_outputs':bytearray(ne*1024)}
    address={};tiles=[None]*8;configured=False;stores=loads=0
    def read(reg):
        base,offset=address[reg];return bytes(mem[base][offset:offset+1024])
    def write(reg,block):
        base,offset=address[reg];assert len(block)==1024 and offset%64==0
        assert offset+1024<=len(mem[base]);mem[base][offset:offset+1024]=block
    body=assembly.decode().split('nx_amx_program:\n',1)[1]
    for raw in body.splitlines():
        line=raw.strip()
        m=re.fullmatch(r'lea (rdi|rsi|rdx),\[(?:rip\+)?(nx_inputs|nx_outputs|r12)\+(\d+)\]',line)
        if m:address[m[1]]=(m[2],int(m[3]));continue
        if line=='rep movsq':write('rdi',read('rsi'));continue
        if line=='ldtilecfg [rax]':assert not configured;configured=True;tiles=[bytes(1024)]*8;continue
        if line=='tilerelease':configured=False;continue
        m=re.fullmatch(r'tileloadd tmm(\d),\[rdi\+rax\*1\]',line)
        if m:assert configured;tiles[int(m[1])]=read('rdi');loads+=1;continue
        m=re.fullmatch(r'tilestored \[rdi\+rax\*1\],tmm(\d)',line)
        if m:assert configured;write('rdi',tiles[int(m[1])]);stores+=1;continue
        m=re.fullmatch(r'tilezero tmm(\d)',line)
        if m:assert configured;tiles[int(m[1])]=bytes(1024);continue
        m=re.fullmatch(r'tdpbssd tmm(\d),tmm(\d),tmm(\d)',line)
        if m:
            d,a,b=map(int,m.groups());assert configured and len({d,a,b})==3
            tiles[d]=pointwise(6,tiles[d],matmul(tiles[a],tiles[b],True));continue
        if line=='call nx_pack_b':
            b=read('rdi');write('rsi',bytes(b[(4*g+l)*16+j] for g in range(16) for j in range(16) for l in range(4)));continue
        if line.startswith('call nx_i32_'):
            kind={'call nx_i32_add':6,'call nx_i32_transpose':7,'call nx_i32_relu':8}[line]
            write('rdx',pointwise(kind,read('rdi'),read('rsi') if kind==6 else b''));continue
        assert (line.startswith(('push ','pop ','mov ','sub ','and ','.section ')) or
                line in ('ret','lea rax,[rip+nx_tilecfg]')),line
    assert not configured
    return bytes(mem['nx_outputs']),loads,stores


def main():
    counts={'native_results':0,'amx_schedule_results':0,'output_canaries':0,'invalid_IR':0,'assembly_link_parity':0}
    native_amx=False
    with tempfile.TemporaryDirectory(prefix='niyah-v2-schedule-') as tmp:
        d=Path(tmp);(d/'driver.c').write_text(DRIVER)
        run(['cc','-U_FORTIFY_SOURCE','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
             '-no-pie','-I'+str(ROOT/'src'),'-I'+str(ROOT/'build/tensor_core'),'-o',d/'driver',
             d/'driver.c',ROOT/'src/tensor_v2.c'])
        rt=d/'runtime.o';dp=d/'dispatch.o'
        run(['as','--64','-o',rt,ROOT/'src/tensor_v2_runtime.s'])
        run(['as','--64','-o',dp,MODEL/'src/tensor_dispatch.s'])
        mixed=[(1,0,0,0),(2,1,0,0),(3,2,0,1),(3,3,0,1),(6,4,2,3),(7,5,4,0),(8,6,5,0)]+[(4,i,0,0) for i in range(2,7)]
        pressure=[(1,0,0,0),(2,1,0,0)]+[(3,i,0,1) for i in range(2,14)]+[(4,i,0,0) for i in range(2,14)]
        manyinputs=[(2,0,0,0)]+[(1,i,0,0) for i in range(1,9)]
        manyinputs+=sum(([(3,9+i,i+1,0),(4,9+i,0,0)] for i in range(8)),[])
        rng=random.Random(261028)
        for index,(ops,mask) in enumerate([(mixed,0),(mixed,248),(pressure,0),(pressure,248),(manyinputs,7)]):
            wire,ni,ne=ir(ops,mask);nv=struct.unpack_from('<I',wire)[0]
            elf=run([d/'driver'],wire);assembly=run([d/'driver','asm'],wire)
            path=d/f'program{index}';path.write_bytes(elf);path.chmod(0o755)
            assert run([d/'driver'],wire)==elf
            features=struct.unpack('<8Q',run([path,'features']))
            modes=['scalar','auto']+(['avx2'] if features[1]&1 else [])+(['amx'] if features[6]==3 else [])
            for data in [bytes(rng.randrange(256) for _ in range(ni*1024)),b'\x80'*(ni*1024)]:
                expected=oracle(ops,data)
                for mode in modes:
                    assert run([path,mode],data)==expected;counts['native_results']+=1
                    if mode=='amx':native_amx=True
                actual,loads,stores=amx_schedule(assembly,data,nv,ne)
                assert actual==expected,('AMX schedule mismatch',index,mask)
                assert loads>=ni and stores>=sum(o[0]==3 for o in ops)
                counts['amx_schedule_results']+=1
            if index==0:
                source=d/'program.s';source.write_bytes(assembly);obj=d/'program.o';linked=d/'linked'
                run(['as','--64','-o',obj,source]);run(['ld','-static','--build-id=none','-z','noexecstack','-o',linked,obj,rt,dp])
                assert run([linked,'scalar'],data)==expected;counts['assembly_link_parity']+=1
            for mode,cap in [('elf',len(elf)-1),('asm',len(assembly)-1)]:
                assert run([d/'driver',mode,cap],wire,74)==b'';counts['output_canaries']+=1
            broken=bytearray(wire);struct.pack_into('<I',broken,20+8,0)
            assert run([d/'driver'],broken,65)==b'';counts['invalid_IR']+=1
        wire,_,_=ir(mixed,254);assert run([d/'driver'],wire,77)==b''
    receipt={'status':'PASS','counts':counts,'amx_native_executed':native_amx,'scope':'native scalar/auto/available AVX2/AMX plus independent AMX schedule simulation'}
    (ROOT/'build/tensor_core/v2_schedule_receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt))


if __name__=='__main__':main()
