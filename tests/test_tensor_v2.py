#!/usr/bin/env python3
"""Core parser -> typed IR -> native ELF acceptance gate for tensor v2."""
import collections
import hashlib
import json
import os
import pathlib
import random
import re
import shutil
import struct
import subprocess
import tempfile
from test_tensor_v2_schedule import oracle, ir, amx_schedule

ROOT=pathlib.Path(__file__).resolve().parents[1]
CC=ROOT/"niyahc"
counts=collections.Counter()
fingerprints=[]
def run(args,data=None,code=0,env=None):
    p=subprocess.run(list(map(str,args)),input=data,capture_output=True,timeout=60,env=env)
    assert p.returncode==code,(args,code,p.returncode,p.stderr[:2000])
    return p.stdout
def native(ops,mask=0):
    lines=["دالة رئيسية() -> صحيح {",f"tensor_reserve({mask});"]
    for k,d,a,b in ops:
        if k==4:lines.append(f"tensor_emit(v{d});");continue
        typ={1:"i8x16x64",2:"i8x64x16"}.get(k,"i32x16x16")
        call={1:"tensor_input_a()",2:"tensor_input_b()",3:f"tensor_matmul(v{a},v{b})",
              6:f"tensor_add(v{a},v{b})",7:f"tensor_transpose(v{a})",8:f"tensor_relu(v{a})"}[k]
        lines.append(f"متغير v{d}: {typ} = {call};")
    return "\n".join(lines+["أرجع 0;","}"])
def compatibility(ops,mask=0):
    lines=[f"reserve {mask};"]
    for k,d,a,b in ops:
        lines.append({1:f"input_a v{d};",2:f"input_b v{d};",3:f"matmul v{d} = v{a} * v{b};",
                      4:f"emit v{d};",6:f"add v{d} = v{a} + v{b};",7:f"transpose v{d} = v{a};",
                      8:f"relu v{d} = v{a};"}[k])
    return "\n".join(lines)
def elf_contract(path):
    h=run(["readelf","-lW",path]);d=run(["readelf","-d",path]);u=run(["nm","-u",path])
    assert b"INTERP" not in h and b"DYNAMIC" not in h and b"NEEDED" not in d and not u
    for line in h.splitlines():
        if line.strip().startswith(b"LOAD"):assert b"RWE" not in line and b"R W E" not in line
    counts["static_ELF_checks"]+=1
def main():
    rng=random.Random(20261028)
    mixed=[(1,0,0,0),(2,1,0,0),(3,2,0,1),(3,3,0,1),(6,4,2,3),(7,5,4,0),(8,6,5,0)]+[(4,i,0,0) for i in range(2,7)]
    pressure=[(1,0,0,0),(2,1,0,0)]+[(3,i,0,1) for i in range(2,14)]+[(4,i,0,0) for i in range(2,14)]
    many_inputs=[(2,0,0,0)]+[(1,i,0,0) for i in range(1,9)]
    many_inputs+=sum(([(3,9+i,i+1,0),(4,9+i,0,0)] for i in range(8)),[])
    wrap=[(1,0,0,0),(2,1,0,0),(3,2,0,1)]
    wrap += [(6,i,i-1,i-1) for i in range(3,14)]
    wrap += [(8,14,13,0),(7,15,13,0),(4,13,0,0),(4,14,0,0),(4,15,0,0)]
    max_values=[(1,0,0,0),(2,1,0,0),(3,2,0,1)]+[(8,i,i-1,0) for i in range(3,64)]+[(4,63,0,0)]
    max_ops=[(1,0,0,0),(2,1,0,0),(3,2,0,1),(7,3,2,0)]+[(4,3,0,0)]*124
    with tempfile.TemporaryDirectory(prefix="niyah-v2-core-") as tmp:
        d=pathlib.Path(tmp);last=None
        for index,(ops,mask) in enumerate([(mixed,0),(mixed,248),(pressure,0),(pressure,248),
                                            (many_inputs,7),(wrap,0),(max_values,248),(max_ops,0)]):
            source=d/f"p{index}.niyah";source.write_text(native(ops,mask));exe=d/f"p{index}.elf"
            run([CC,"-o",exe,source]);last=(source,exe)
            blob=exe.read_bytes();elf_contract(exe)
            second=d/f"p{index}-again.elf";run([CC,"-o",second,source]);assert second.read_bytes()==blob
            counts["deterministic_ELF"]+=1
            alternate=d/f"p{index}-compat.niyah";alternate.write_text(compatibility(ops,mask))
            other=d/f"p{index}-compat.elf";run([CC,"-o",other,alternate]);assert other.read_bytes()==blob
            counts["intrinsic_compatibility_byte_parity"]+=1
            nir=run([CC,"--emit-tensor-ir",source]);assert nir[:8]==b"NIRG0002"
            nv,no,ni,ne,reserved,source_bytes=struct.unpack_from("<6I",nir,8)
            assert no==len(ops) and reserved==mask and source_bytes==source.stat().st_size
            expected_ir,_,_=ir(ops,mask)
            for i in range(nv):
                assert nir[32+1024*i:32+1024*i+12]==expected_ir[20+1024*i:20+1024*i+12]
            assert nir[32+nv*1024:32+nv*1024+no*16]==b"".join(struct.pack("<4I",*o) for o in ops)
            counts["typed_IR_lifetime_checks"]+=nv
            asm=run([CC,"--emit-tensor-asm",source]);assert b"and rsp,-64" in asm
            if index in (2,3,4):
                m=re.search(rb"graph spills: (\d+); physical evictions: (\d+)",asm)
                assert m and int(m[1])>0 and int(m[2])>0
                assert b"tileloadd" in asm and b"tilestored" in asm
                counts["pressure_programs_spilled"]+=1
            features=struct.unpack("<8Q",run([exe,"features"]));assert features[0]==int.from_bytes(b"NXISA001","little")
            modes=["scalar"]+(["auto","avx2"] if features[1]&1 else [])
            if features[6]==3:modes.append("amx")
            cases=[bytes(rng.randrange(256) for _ in range(ni*1024)),b"\x80"*(ni*1024),
                   bytes((j*73+index)%256 for j in range(ni*1024))]
            for case,data in enumerate(cases):
                expected=oracle(ops,data)
                for mode in modes:
                    assert run([exe,mode],data)==expected,(index,case,mode)
                    if mode=="amx":counts["forced_amx_execution"]+=1
                    counts["native_output_comparisons"]+=1;counts["i32_elements_compared"]+=len(expected)//4
                if case==0:
                    simulated,_,_=amx_schedule(asm,data,nv,ne);assert simulated==expected
                    counts["AMX_schedule_simulations"]+=1
                fingerprints.append({"program":index,"case":case,"input_sha256":hashlib.sha256(data).hexdigest(),
                                     "output_sha256":hashlib.sha256(expected).hexdigest(),"bytes":len(expected)})
            if features[6]!=3:assert run([exe,"amx"],code=77)==b"";counts["unsupported_AMX_rejections"]+=1
            if index==0:
                dis=run(["objdump","-d","-M","intel",exe])
                for name in (b"vpmaddubsw",b"vpmaddwd",b"vpaddd",b"tileloadd",b"tilestored"):
                    assert name in dis,name
                counts["required_instructions_disassembled"]+=5
                def closed_stdin():os.close(0)
                p=subprocess.run([str(exe),"scalar"],capture_output=True,preexec_fn=closed_stdin)
                assert p.returncode==66 and not p.stdout;counts["read_error_66"]+=1
                rd,wr=os.pipe();os.close(rd)
                try:
                    p=subprocess.run([str(exe),"scalar"],input=cases[0],stdout=wr,stderr=subprocess.PIPE)
                    assert p.returncode==74;counts["write_error_74"]+=1
                finally:os.close(wr)
                with open("/dev/full","wb") as full:
                    p=subprocess.run([str(exe),"scalar"],input=cases[0],stdout=full,stderr=subprocess.PIPE)
                    assert p.returncode==74;counts["write_error_74"]+=1
                for data in (b"",cases[0][:-1],cases[0]+b"x"):
                    assert run([exe,"scalar"],data,65)==b"";counts["invalid_runtime_input_65"]+=1
                assert run([exe,"bogus"],code=64)==b"";counts["usage_64"]+=1
        # Normalized lexical equivalence retains source spelling and distinct roots.
        normalized=d/"normalized.niyah"
        normalized.write_text(native(mixed).replace("v0","أَلف",1).replace("v0","الف"))
        normexe=d/"normalized.elf";run([CC,"-o",normexe,normalized]);counts["normalized_reference"]+=1
        for i,spelling in enumerate(["ﻛﺘﺎﺏ","كِتَاب","كتاب\u08d3"]):
            normalized.write_text(native(mixed).replace("v0",spelling,1).replace("v0","كتاب"))
            normexe=d/f"normalized-{i}.elf";run([CC,"-o",normexe,normalized])
            data=b"\x80"*2048;assert run([normexe,"avx2"],data)==oracle(mixed,data)
            counts["normalized_reference"]+=1
        matmul_only=[(1,0,0,0),(2,1,0,0),(3,2,0,1),(4,2,0,0)]
        current=d/"matmul-only.niyah";current.write_text(native(matmul_only))
        assert run([CC,"--emit-tensor-ir",current])[:8]==b"NIRG0002"
        current_exe=d/"matmul-only.elf";run([CC,"-o",current_exe,current])
        assert run([current_exe,"avx2"],b"\x80"*2048)==oracle(matmul_only,b"\x80"*2048)
        counts["MATMUL_selects_current_backend"]+=1
        source,exe=last
        for args,code in [([],64),(["--unknown"],64),([d/"missing"],66),(["--emit-tensor-ir",d/"missing"],66)]:
            assert run([CC,*args],code=code)==b"";counts["compiler_exit_contract"]+=1
        bad=["input_a a; input_b b; gemm c = a*b; add d = c + a; emit d;",
             "input_a a; input_b b; gemm c = a*b; relu c = c; emit c;",
             "input_a a; input_b b; gemm c = a*b; transpose d = future; emit d;",
             native(mixed).replace("tensor_add(v2,v3)","tensor_add(v2)"),
             "input_a ألف; input_b الف; gemm c=ألف*الف; emit c;"]
        for i,text in enumerate(bad):
            f=d/f"bad{i}.niyah";f.write_text(text);dest=d/f"bad{i}.elf"
            assert run([CC,"-o",dest,f],code=65)==b"" and not dest.exists();counts["invalid_AST_65"]+=1
        impossible=d/"no-registers.niyah";impossible.write_text(native(mixed,254))
        assert run([CC,"-o",d/"no-registers.elf",impossible],code=77)==b"";counts["resource_77"]+=1
        huge=d/"too-large.niyah";huge.write_bytes(b" "*(1048576+1))
        assert run([CC,huge],code=77)==b"";counts["resource_77"]+=1
        protected=d/"protected";protected.write_bytes(b"PRESERVE")
        run([CC,"-o",protected,source],code=74);assert protected.read_bytes()==b"PRESERVE"
        counts["no_overwrite"]+=1
        isolated=d/"isolated";isolated.mkdir();alone=isolated/"niyahc";shutil.copy2(CC,alone)
        env=os.environ.copy();env["PATH"]="/nonexistent";out=isolated/"a.elf"
        run([alone,"-o",out,source],env=env);assert out.read_bytes()==exe.read_bytes()
        counts["standalone_compiler"]+=1
    receipt={"status":"PASS","counts":dict(counts),"functional_fingerprints":fingerprints,
             "amx_hardware_executed":bool(counts.get("forced_amx_execution",0)),
             "scope":"Fixed INT8 16x64 * 64x16; INT32 16x16 ADD, TRANSPOSE, RELU; <=64 SSA values, <=128 operations"}
    out=ROOT/"build/tensor_core/v2_core_receipt.json";out.write_text(json.dumps(receipt,indent=2)+"\n")
    print(json.dumps({k:v for k,v in receipt.items() if k!="functional_fingerprints"},indent=2))
if __name__=="__main__":main()
