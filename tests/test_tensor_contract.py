#!/usr/bin/env python3
"""Acceptance boundaries for the primary fixed-shape tensor compiler."""
import collections, hashlib, json, os, pathlib, random, struct, subprocess, tempfile, time
ROOT=pathlib.Path(__file__).resolve().parents[1]
CC=ROOT/"niyahc"
PROBE=ROOT/"build/tensor_core/syscall_fault_probe"
counts=collections.Counter()
records=[]
def run(args,expected=0,data=None,**kwargs):
    q=subprocess.run(list(map(str,args)),input=data,capture_output=True,timeout=90,**kwargs)
    records.append({"argv":list(map(str,args)),"expected":expected,"exit":q.returncode,
                    "stdout_bytes":len(q.stdout),"stdout_sha256":hashlib.sha256(q.stdout).hexdigest(),
                    "stderr":q.stderr.decode(errors="replace")[:2000]})
    assert q.returncode==expected,(args,expected,q.returncode,q.stderr[:2000])
    return q.stdout
def main():
    rng=random.Random(20261028)
    with tempfile.TemporaryDirectory(prefix="niyah-core-contract-") as temp:
        d=pathlib.Path(temp)
        native=(ROOT/"examples/tensor_intrinsics.niyah").read_bytes()
        src=d/"arabic.niyah";src.write_bytes(native)
        exe=d/"arabic.elf";run([CC,"--tensor","-o",exe,src])
        nir=run([CC,"--emit-tensor-ir",src])
        assert nir[:8]==b"NIRG0001"
        assert struct.unpack_from("<5I",nir,8)==(3,4,2,1,1)
        intervals=[struct.unpack_from("<2I",nir,32+1024*i+4) for i in range(3)]
        assert intervals==[(0,3),(1,3),(2,4)]
        counts["source_IR_interval_trace"]+=1
        features=struct.unpack("<8Q",run([exe,"features"]))
        assert features[0]==int.from_bytes(b"NXISA001","little")
        modes=["scalar","auto"]+(["avx2"] if features[1]&1 else [])
        for name,bit in [("avx2",1),("avx512",2),("amx",4)]:
            if not features[1]&bit:
                assert run([exe,name],77)==b""
                counts["explicit_unavailable_77"]+=1
        assert run([exe,"unknown"],64)==b""
        def raw(vals):return bytes(x&255 for x in vals)
        fixtures=[("ones_twos",[1]*1024,[2]*1024),
                  ("negative_extremes",[-128]*1024,[-128]*1024),
                  ("mixed_extremes",[-128]*1024,[127]*1024),
                  ("positive_extremes",[127]*1024,[127]*1024),
                  ("zero",[0]*1024,[127]*1024)]
        fixtures.append(("pattern",[(i*17+k*29+3)%256-128 for i in range(16) for k in range(64)],
                         [(k*11+j*7+5)%256-128 for k in range(64) for j in range(16)]))
        for k in [0,1,3,4,63]:
            a=[0]*1024;b=[0]*1024;a[k]=1
            for j in range(16):b[k*16+j]=j-8
            fixtures.append(("impulse_"+str(k),a,b))
        for n in range(4):
            fixtures.append(("seeded_"+str(n),[rng.randrange(-128,128) for _ in range(1024)],
                             [rng.randrange(-128,128) for _ in range(1024)]))
        first_data=None
        for name,a,b in fixtures:
            expected=[sum(a[i*64+k]*b[k*16+j] for k in range(64)) for i in range(16) for j in range(16)]
            expect=struct.pack("<256i",*expected);data=raw(a)+raw(b)
            if name=="ones_twos":
                assert expected==[128]*256;first_data=data
            for mode in modes:
                assert run([exe,mode],data=data)==expect,(name,mode)
                counts["native_matrix_comparisons"]+=1
                counts["int32_elements_compared"]+=256
        for label,data in [("empty",b""),("truncated",first_data[:-1]),("trailing",first_data+b"x")]:
            assert run([exe,"scalar"],65,data)==b""
            counts["malformed_runtime_input_65"]+=1
        fd=os.open(d,os.O_RDONLY)
        try:
            q=subprocess.run([str(exe),"scalar"],stdin=fd,capture_output=True)
            assert q.returncode==66 and not q.stdout
            counts["raw_read_error_66"]+=1
        finally:os.close(fd)
        def closed_stdin():os.close(0)
        q=subprocess.run([str(exe),"scalar"],preexec_fn=closed_stdin,capture_output=True)
        assert q.returncode==66 and not q.stdout;counts["raw_read_error_66"]+=1
        for mode in modes:
            with open("/dev/full","wb") as full:
                q=subprocess.run([str(exe),mode],input=first_data,stdout=full,stderr=subprocess.PIPE)
                assert q.returncode==74;counts["raw_write_error_74"]+=1
            rd,wr=os.pipe();os.close(rd)
            try:
                q=subprocess.run([str(exe),mode],input=first_data,stdout=wr,stderr=subprocess.PIPE)
                assert q.returncode==74;counts["raw_write_error_74"]+=1
            finally:os.close(wr)
        for index,(a,b) in enumerate([("أ","ا"),("كِتاب","كتاب"),("ﻛﺘﺎﺏ","كتاب"),("والكتاب","سيكتب")]):
            good=d/f"distinct{index}.niyah"
            good.write_text(f"input_a {a}; input_b {b}; gemm c={a}*{b}; emit c;",encoding="utf-8")
            out=d/f"distinct{index}.elf";run([CC,"--tensor","-o",out,good])
            assert run([out,"scalar"],data=first_data)==struct.pack("<256i",*([128]*256))
            counts["distinct_surface_identifiers"]+=1
            bad=d/f"alias{index}.niyah"
            bad.write_text(f"input_a {a}; input_b b; gemm c={b}*b; emit c;",encoding="utf-8")
            absent=d/f"alias{index}.elf"
            assert run([CC,"--tensor","-o",absent,bad],65)==b"" and not absent.exists()
            counts["no_normalized_or_root_alias"]+=1
        pressure="reserve 248; input_a a; input_b b; gemm c=a*b; gemm d=a*b; emit c; emit d;"
        bad_inputs=[
            ("spills",pressure,77),
            ("invalid_before_pressure","reserve 255; input_a a; input_b b; gemm c=b*a; emit c;",65),
            ("add","input_a a; input_b b; gemm c=a*b; add d=c+c; emit d;",65),
            ("transpose","input_a a; input_b b; gemm c=a*b; transpose d=c; emit d;",65),
            ("relu","input_a a; input_b b; gemm c=a*b; relu d=c; emit d;",65),
        ]
        for name,text,code in bad_inputs:
            f=d/(name+".niyah");f.write_text(text)
            for mode in ["--emit-tensor-ir","--emit-tensor-asm","--tensor"]:
                out=d/(name+"-"+mode[2:]+".out")
                assert run([CC,mode,"-o",out,f],code)==b"" and not out.exists()
                counts["unsupported_or_pressure_rejections"]+=1
        for index,raw_source in enumerate([b"input_a \xff; input_b b; gemm c=a*b; emit c;",
                                          b"input_a \xc0\xaf; input_b b; gemm c=a*b; emit c;",
                                          native+b"\xed\xa0\x80",native+b"\xf4\x90\x80\x80",
                                          native.replace(b"i8x16x64",b"i8x16xN",1)]):
            f=d/f"malformed{index}.niyah";f.write_bytes(raw_source);out=d/f"malformed{index}.elf"
            assert run([CC,"--tensor","-o",out,f],65)==b"" and not out.exists()
            counts["UTF8_or_dynamic_shape_rejections"]+=1
        for args,code in [([],1),(["--unknown"],1),([d/"missing"],1),
                          (["--tensor"],64),(["--tensor","--unknown"],64),
                          (["--tensor",d/"missing"],66),(["--emit-tensor-ir",d/"missing"],66)]:
            assert run([CC,*args],code)==b"";counts["CLI_ABI_checks"]+=1
        scalar=d/"scalar.niyah"
        scalar.write_text("دالة رئيسية() -> صحيح { أرجع 7; }",encoding="utf-8")
        scalar_out=d/"scalar.elf";run([CC,"-o",scalar_out,scalar])
        assert run([scalar_out],7)==b""
        assert run([CC,"--tensor","-o",d/"not-tensor.elf",scalar],65)==b""
        counts["scalar_and_explicit_tensor_boundary"]+=1
        for index,name in enumerate(["tensor_input_a","tensor_gemm","tensor_emit"]):
            f=d/f"scalar-name{index}.niyah"
            f.write_text("\u062f\u0627\u0644\u0629 \u0631\u0626\u064a\u0633\u064a\u0629() -> \u0635\u062d\u064a\u062d { \u0645\u062a\u063a\u064a\u0631 "+name+": \u0635\u062d\u064a\u062d = 7; \u0623\u0631\u062c\u0639 "+name+"; }",encoding="utf-8")
            target=d/f"scalar-name{index}.elf";run([CC,"-o",target,f])
            assert run([target],7)==b""
            counts["scalar_intrinsic_spelling_variable_compatibility"]+=1
        # The observer injects EINTR/short reads/short writes at actual syscall stops.
        for args,data,expected in [
            ([CC,"--tensor","-o",d/"fault-compiled.elf",src],None,b""),
            ([exe,"scalar"],first_data,struct.pack("<256i",*([128]*256))),
            ([CC,"--emit-tensor-ir",src],None,nir),
        ]:
            q=subprocess.run([str(PROBE),*map(str,args)],input=data,capture_output=True,timeout=90)
            assert q.returncode==0,(args,q.returncode,q.stderr)
            assert q.stdout==expected
            receipt=json.loads(q.stderr.decode().splitlines()[-1])
            assert receipt["execs"]==1 and receipt["forbidden"]==0 and receipt["target_exit"]==0
            records.append({"probe":receipt,"argv":list(map(str,args))})
            counts["ptrace_raw_EINTR_short_IO_no_subprocess"]+=1
        assert (d/"fault-compiled.elf").read_bytes()==exe.read_bytes()
        counts["fault_injected_compiler_whole_ELF_parity"]+=1
        receipt={"status":"PASS","counts":dict(counts),"features":list(features),"records":records,
                 "amx_hardware_arithmetic":"NOT_RUN","avx512_hardware_arithmetic":"NOT_RUN"}
        out=ROOT/"build/tensor_core/core_contract_receipt.json"
        out.write_text(json.dumps(receipt,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
        print(json.dumps({k:v for k,v in receipt.items() if k!="records"},ensure_ascii=False,indent=2))
if __name__=="__main__":main()
