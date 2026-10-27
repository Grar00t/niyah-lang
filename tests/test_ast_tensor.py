#!/usr/bin/env python3
"""Core AST differential gate. Python is test orchestration, never compiler runtime."""
import collections,hashlib,json,os,pathlib,random,shutil,struct,subprocess,tempfile,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
MODEL=pathlib.Path(os.environ.get('NIYAH_MODEL',str(ROOT.parent/'niyah-assembly-model'))).resolve()
OUT=pathlib.Path(os.environ.get('NIYAH_AST_RECEIPT',str(ROOT/'build/tensor_core/ast_test_receipt.json')))
CC=ROOT/'niyahc';SIDE=ROOT/'niyahc-tile';ALLOC=MODEL/'build/tile_allocator'
counts=collections.Counter();records=[];started=time.perf_counter();identities={}
def sha(b):return hashlib.sha256(b).hexdigest()
def run(argv,expected=0,inp=None,env=None):
 t=time.perf_counter();q=subprocess.run([str(x) for x in argv],input=inp,capture_output=True,timeout=60,env=env)
 records.append({'argv':[str(x) for x in argv],'expected_exit':expected,'observed_exit':q.returncode,'seconds':time.perf_counter()-t,'stdout_bytes':len(q.stdout),'stdout_sha256':sha(q.stdout),'stderr':q.stderr.decode(errors='replace')[:500]})
 assert q.returncode==expected,(argv,expected,q.returncode,q.stderr[:1000]);return q.stdout
def elf(p):
 h=run(['readelf','-lW',p]);d=run(['readelf','-d',p]);u=run(['nm','-u',p]);assert b'INTERP' not in h and b'NEEDED' not in d and not u
 for ln in h.splitlines():
  if ln.strip().startswith(b'LOAD'):assert b'RWE' not in ln and b'R W E' not in ln
 counts['static_ELF_dependency_checks']+=1

def main():
 assert sha(ALLOC.read_bytes())=='59447d2ededb37fb66985be7cbb2bb3c4e7b8326443141b61880ece2158e0866'
 assert sha(SIDE.read_bytes())=='b6924af76ea75dd3f23ef80ff926d67f74959185c346ec178c8df03ad543afe8'
 elf(CC);rng=random.Random(20261027)
 with tempfile.TemporaryDirectory(prefix='niyah-ast-gate-') as tmp:
  D=pathlib.Path(tmp)
  rt=D/'runtime.o';dp=D/'dispatch.o'
  run(['as','--64','-o',rt,MODEL/'src/tensor_runtime.s']);run(['as','--64','-o',dp,MODEL/'src/tensor_dispatch.s'])
  ref=D/'c-reference'
  run(['cc','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-tree-loop-distribute-patterns','-fno-tree-vectorize','-nostdlib','-static','-no-pie','-Wl,--build-id=none,-z,noexecstack','-o',ref,ROOT/'src/compiler_start.s',ROOT/'src/compiler_native.c',ROOT/'tests/tensor_c_reference.c'])
  elf(ref);identities['c_reference']={'bytes':ref.stat().st_size,'sha256':sha(ref.read_bytes())}
  api=D/'api-sanitized'
  run(['cc','-std=c11','-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined','-no-pie','-I'+str(ROOT/'src'),'-I'+str(ROOT/'build/tensor_core'),'-o',api,ROOT/'tests/tensor_api_test.c',ROOT/'src/tensor_core.c',ROOT/'src/parser.c',ROOT/'src/lexer.c',ROOT/'src/morph.c',ROOT/'build/tensor_core/tensor_allocate.o'])
  observed=run([api]);assert b'AST_API_PASS cases=26' in observed,observed;counts['ASan_UBSan_API_cases']=26
  serial=0
  def parity(source,execute=False):
   nonlocal serial
   serial+=1;base=D/f'p{serial}';src=base.with_suffix('.nya');src.write_bytes(source.encode() if isinstance(source,str) else source)
   nir=run([SIDE,'--ir',src]);nfile=base.with_suffix('.nir');nfile.write_bytes(nir)
   iv=run([SIDE,'--intervals',nfile]);n=iv[8];mask=iv[9];intervals=[struct.unpack_from('<II',iv,16+8*i) for i in range(n)]
   pressure=max(sum(a<=s<b for a,b in intervals) for s,_ in intervals)>8-mask.bit_count()
   assigned=run([ALLOC],77 if pressure else 0,iv)
   exe=base.with_suffix('.elf')
   if pressure:
    run([CC,'-o',exe,src],77);assert not exe.exists();counts['pressure_rejections']+=1;return None
   afile=base.with_suffix('.colors');afile.write_bytes(assigned)
   assert run([CC,'--emit-tensor-ir',src])==nir;counts['canonical_IR_parity']+=1
   expected_asm=run([SIDE,'--asm',nfile,afile]);actual_asm=run([CC,'--emit-tensor-asm',src]);assert actual_asm==expected_asm;counts['assembly_byte_parity']+=1
   asm=base.with_suffix('.s');asm.write_bytes(expected_asm);obj=base.with_suffix('.o');reference=base.with_suffix('.reference')
   run(['as','--64','-o',obj,asm]);run(['ld','-static','--build-id=none','-z','noexecstack','-o',reference,obj,rt,dp]);run([CC,'-o',exe,src]);assert exe.read_bytes()==reference.read_bytes(),('ELF mismatch',serial,len(exe.read_bytes()),len(reference.read_bytes()))
   counts['whole_ELF_byte_parity']+=1
   nv,no,ni,ne,_,_=struct.unpack_from('<6I',nir,8);ops=nir[32+nv*1024:32+nv*1024+no*16]
   if execute:
    features=struct.unpack('<8Q',run([exe,'features']));paths=['scalar','auto']+(['avx2'] if features[1]&1 else [])
    cases=[bytes(rng.randrange(256) for _ in range(ni*1024)),b'\x80'*(ni*1024),bytes(ni*1024)]
    for data in cases:
     expected=run([ref],inp=struct.pack('<4I',nv,no,ni,ne)+ops+data);assert len(expected)==ne*1024
     for mode in paths:
      got=run([exe,mode],inp=data);assert got==expected,(serial,mode);counts['C_reference_native_comparisons']+=1;counts['INT32_elements_compared']+=len(got)//4
    for mode,bit in [('amx',4),('avx512',2)]:
     if not features[1]&bit:assert run([exe,mode],77,inp=b'')==b'';counts['unsupported_ISA_rejections']+=1
   return exe,nir,assigned
  demo='reserve 1;\ninput_a والكتاب;\ninput_b سيكتب;\ngemm نتيجة = والكتاب * سيكتب;\nemit نتيجة;\n'
  demo_result=parity(demo,True);demo_exe,demo_nir,assign=demo_result;elf(demo_exe)
  assert sha(demo_exe.read_bytes())=='460d3d202fd42d389ff03e7c6305c6c7296480752ff81bead47550187db3c0f4';assert len(demo_exe.read_bytes())==11888
  assert assign==b'TMM1\x03\0\0\0\x01\x02\x03';counts['predicted_demo_identity']=1
  for i in range(2):
   v=demo_nir[32+i*1024:32+(i+1)*1024];assert struct.unpack_from('<3I',v,160)==(0x643,0x62a,0x628)
  counts['distinct_same_root_metadata']=2
  for mask in range(256):parity(f'reserve {mask}; input_a a; input_b b; gemm c = a * b; emit c;',mask in (0,1,128,248))
  programs=[]
  for groups in [2,4,8,15,21]:
   lines=['reserve 248;']
   for i in range(groups):lines+= [f'input_a a{i};',f'input_b b{i};',f'gemm c{i} = a{i} * b{i};',f'emit c{i};']
   programs.append('\n'.join(lines))
  maximum=programs[-1]+'\ninput_a unused;\n'+'\n'.join('emit c20;' for _ in range(43));programs.append(maximum)
  for p in programs:parity(p,True)
  native='''دالة رئيسية() -> صحيح {
 tensor_reserve(1);
 متغير والكتاب: i8x16x64 = tensor_input_a();
 متغير سيكتب: i8x64x16 = tensor_input_b();
 ثابت نتيجة: i32x16x16 = tensor_gemm(والكتاب,سيكتب);
 tensor_emit(نتيجة);
 أرجع 0;
}'''
  for i,src in enumerate([native,native.replace('ثابت نتيجة','متغير نتيجة'),native.replace('tensor_reserve(1);','tensor_reserve(١);')]):
   f=D/f'intrinsic{i}.nya';f.write_text(src);x=D/f'intrinsic{i}.elf';run([CC,'-o',x,f]);assert x.read_bytes()==demo_exe.read_bytes();counts['intrinsic_syntax_binary_parity']+=1
  bad=[
   'input_a a; input_b b; gemm c = b * a; emit c;',
   'input_a a; input_b b; gemm c = a * missing; emit c;',
   'input_a a; input_b a; gemm c = a * a; emit c;',
   'input_a a; emit a;', 'reserve 256; input_a a;', 'reserve 1; reserve 2; input_a a;',
   'input_a a; reserve 1;', 'input_a a;', 'input_a a; input_b b; gemm c = c * b; emit c;',
   native.replace('i8x16x64 =','i8x64x16 =',1),native.replace('tensor_input_a()','tensor_input_a(1)'),
   native.replace('tensor_gemm(والكتاب,سيكتب)','tensor_gemm(سيكتب,والكتاب)'),
   native.replace('أرجع 0;','أرجع 1;'),native.replace('أرجع 0;',''),
   native.replace('tensor_emit(نتيجة);','إذا (1) { tensor_emit(نتيجة); }'),
   native.replace('tensor_emit(نتيجة);','tensor_emit(نتيجة) + 1;'),
   native.replace('tensor_gemm(والكتاب,سيكتب)','tensor_gemm(والكتاب,سيكتب)+1'),
   native.replace('tensor_emit(نتيجة);','tensor_emit(والكتاب);'),
   native+'\nمتغير z: صحيح = 1;',native.replace('tensor_reserve(1);','tensor_reserve(255);'),
  ]
  for i,s in enumerate(bad):
   f=D/f'bad{i}.nya';f.write_text(s);x=D/f'bad{i}.elf';run([CC,'-o',x,f],77 if i==len(bad)-1 else 65);assert not x.exists();counts['invalid_AST_or_semantics']=counts['invalid_AST_or_semantics']+1
  # Oversized source and closed output endpoints must not produce a false success.
  f=D/'too-big.nya';f.write_bytes(b' '*(1048576+1));assert run([CC,'--emit-tensor-ir',f],66)==b'';counts['source_size_boundary']+=1
  for option in ['--emit-tensor-asm','--emit-tensor-ir']:
   f=D/'io.nya';f.write_text(demo)
   with open('/dev/full','wb') as full:q=subprocess.run([str(CC),option,str(f)],stdout=full,stderr=subprocess.PIPE);assert q.returncode==74;counts['output_failure_74']+=1
   rd,wr=os.pipe();os.close(rd)
   try:q=subprocess.run([str(CC),option,str(f)],stdout=wr,stderr=subprocess.PIPE);assert q.returncode==74;counts['output_failure_74']+=1
   finally:os.close(wr)
  f=D/'collision.nya';f.write_text(demo);x=D/'existing';x.write_bytes(b'PRESERVE');run([CC,'-o',x,f],74);assert x.read_bytes()==b'PRESERVE';counts['no_overwrite']=1
  sym=D/'symlink';sym.symlink_to(x);run([CC,'-o',sym,f],74);assert x.read_bytes()==b'PRESERVE';counts['no_symlink_overwrite']=1
  # A lone compiler with an empty PATH and no adjacent helper binaries still builds.
  isolated=D/'isolated';isolated.mkdir();alone=isolated/'niyahc';shutil.copy2(CC,alone)
  x=isolated/'generated.elf';env=os.environ.copy();env['PATH']='/nonexistent';run([alone,'-o',x,f],env=env);assert x.read_bytes()==demo_exe.read_bytes();counts['isolated_compiler_no_helpers']=1
  tracer=shutil.which('strace')
  if tracer:
   log=D/'syscalls.log';x=isolated/'traced.elf';run([tracer,'-f','-qq','-e','trace=execve,open,openat,clone,fork,vfork','-o',log,alone,'-o',x,f],env=env)
   trace=log.read_text();assert trace.count('execve(')==1;assert not any(k+'(' in trace for k in ['clone','fork','vfork']);counts['no_child_process_syscall_trace']=1
   (OUT.parent/'compiler_syscalls.log').write_text(trace)
  baseline=os.environ.get('NIYAH_SCALAR_BASELINE',str(ROOT/'niyahc.baseline'))
  if pathlib.Path(baseline).is_file():
   for i in range(64):
    v=rng.randrange(-1000000,1000000);src=f'دالة رئيسية() -> صحيح {{ متغير x: صحيح = {v}; أرجع (x * 13 + 7) / 3; }}';f=D/f'scalar{i}.nya';f.write_text(src);old=D/f'old{i}';new=D/f'new{i}'
    run([baseline,'-o',old,f]);run([CC,'-o',new,f]);assert old.read_bytes()==new.read_bytes();counts['legacy_scalar_binary_parity']+=1
  identities['compiler']={'bytes':CC.stat().st_size,'sha256':sha(CC.read_bytes())};identities['demo_elf']={'bytes':len(demo_exe.read_bytes()),'sha256':sha(demo_exe.read_bytes())}
  identities['allocator_unchanged']=sha(ALLOC.read_bytes());identities['sidecar_unchanged']=sha(SIDE.read_bytes())
  saved=OUT.parent/'ast_demo.elf';saved.write_bytes(demo_exe.read_bytes());saved.chmod(0o755)
  (OUT.parent/'ast_demo.nir').write_bytes(demo_nir);(OUT.parent/'ast_demo.assignment').write_bytes(assign)
  (OUT.parent/'ast_demo.niyah').write_text(demo);(OUT.parent/'ast_intrinsics.niyah').write_text(native)
  (OUT.parent/'ast_demo.s').write_bytes(run([CC,'--emit-tensor-asm',OUT.parent/'ast_demo.niyah']))

if __name__=='__main__':
 OUT.parent.mkdir(parents=True,exist_ok=True)
 try:
  main();status='PASS';code=0
 except Exception as e:
  status='FAIL';code=1;print(type(e).__name__,str(e),flush=True);raise
 finally:
  receipt={'schema':'niyah-core-ast-tensor-v1','status':status,'exit_code':code,'counts':dict(counts),'identities':identities,'seconds':time.perf_counter()-started,'commands':records,'amx_hardware_executed':False,'avx512_hardware_executed':False,'scope':'fixed-shape straight-line intrinsic AST; no general CFG, no spills; no libc in shipped compiler or generated ELF'}
  OUT.write_text(json.dumps(receipt,indent=2,ensure_ascii=False)+'\n');print(json.dumps({k:v for k,v in receipt.items() if k!='commands'},indent=2,ensure_ascii=False))
