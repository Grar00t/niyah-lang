#!/usr/bin/env python3
"""Build-time only: seal native instruction/relocation templates; no runtime tools."""
import pathlib,struct,subprocess,hashlib,json,ast,re,sys
ROOT=pathlib.Path(sys.argv[1]).resolve(); MODEL=pathlib.Path(sys.argv[2]).resolve(); OUT=ROOT/'build/tensor_core';OUT.mkdir(parents=True,exist_ok=True)
def run(cmd,inp=None):return subprocess.run([str(x) for x in cmd],cwd=OUT,input=inp,capture_output=True,check=True).stdout
def align(x,n):return (x+n-1)//n*n
class Elf:
 def __init__(self,p):
  self.data=pathlib.Path(p).read_bytes();self.h=struct.unpack_from('<16sHHIQQQIHHHHHH',self.data);assert self.h[0][:6]==b'\x7fELF\x02\x01'
  self.ss=[struct.unpack_from('<IIQQQQIIQQ',self.data,self.h[6]+i*64) for i in range(self.h[12])]
  sh=self.ss[self.h[13]];ns=self.data[sh[4]:sh[4]+sh[5]]
  self.names=[ns[s[0]:].split(b'\0')[0].decode() for s in self.ss];self.si={n:i for i,n in enumerate(self.names)}
  self.sy=[]
  if '.symtab' in self.si:
   s=self.ss[self.si['.symtab']];st=self.ss[s[6]];strings=self.data[st[4]:st[4]+st[5]]
   for o in range(s[4],s[4]+s[5],24):
    q=struct.unpack_from('<IBBHQQ',self.data,o);self.sy.append((strings[q[0]:].split(b'\0')[0].decode(),q))
 def sec(self,n):
  if n not in self.si:return b''
  s=self.ss[self.si[n]];return self.data[s[4]:s[4]+s[5]] if s[1]!=8 else b''
 def size(self,n):return self.ss[self.si[n]][5] if n in self.si else 0
 def al(self,n):return max(1,self.ss[self.si[n]][8]) if n in self.si else 1
 def relocs(self,n):
  for s in self.ss:
   if s[1]==4 and self.names[s[7]]==n:
    for o in range(s[4],s[4]+s[5],24):
     a,b,c=struct.unpack_from('<QQq',self.data,o);yield a,b&0xffffffff,b>>32,c
# These tools run while building niyahc, never while niyahc compiles a source.
side=ROOT/'niyahc-tile';alloc=MODEL/'build/tile_allocator'
assert hashlib.sha256(side.read_bytes()).hexdigest()=='b6924af76ea75dd3f23ef80ff926d67f74959185c346ec178c8df03ad543afe8'
assert hashlib.sha256(alloc.read_bytes()).hexdigest()=='59447d2ededb37fb66985be7cbb2bb3c4e7b8326443141b61880ece2158e0866'
(OUT/'reference.niyah').write_bytes(b'reserve 1;\ninput_a a;\ninput_b b;\ngemm c = a * b;\nemit c;\n')
nir=run([side,'--ir','reference.niyah']);(OUT/'reference.nir').write_bytes(nir)
iv=run([side,'--intervals','reference.nir']);a=run([alloc],iv);(OUT/'reference.colors').write_bytes(a)
s=run([side,'--asm','reference.nir','reference.colors']);(OUT/'reference.s').write_bytes(s)
for name,src in [('reference',OUT/'reference.s'),('tensor_runtime',MODEL/'src/tensor_runtime.s'),('tensor_dispatch',MODEL/'src/tensor_dispatch.s')]:run(['as','--64','-o',name+'.o',src])
run(['ld','-static','--build-id=none','-z','noexecstack','-o','reference.elf','reference.o','tensor_runtime.o','tensor_dispatch.o'])
ref=Elf(OUT/'reference.elf')
# The historical identity applies only to its matching runtime/dispatch contract.
# Corrected native sources establish a new, measured whole-ELF reference.
historical_sources=('14b675c767073d88a048d61d8ef5c8911ddc00c941bd8ce049c67cd3edd94e78',
                    '48e924edf1d2835a033b34375285837f536af23d0cceabbf304a3d34a42069b6')
current_sources=tuple(hashlib.sha256((MODEL/'src'/name).read_bytes()).hexdigest()
                      for name in ('tensor_runtime.s','tensor_dispatch.s'))
if current_sources==historical_sources:
 assert hashlib.sha256(ref.data).hexdigest()=='460d3d202fd42d389ff03e7c6305c6c7296480752ff81bead47550187db3c0f4'
mods=[Elf(OUT/(n+'.o')) for n in ['tensor_runtime','tensor_dispatch']]
# Anchor IDs: text,ro,bss,vector,amx,runtime-text,dispatch-text,
# runtime-ro,dispatch-ro,runtime-bss,dispatch-bss,inputs,slots,outputs,
# packbuf,plan,colors,bss-start,bss-end.
mb=[{'.text':5,'.rodata':7,'.bss':9},{'.text':6,'.rodata':8,'.bss':10}]
gen={'nx_vector_program':(3,0),'nx_amx_program':(4,0),'nx_inputs':(11,0),'nx_slots':(12,0),'nx_outputs':(13,0),'nx_packbuf':(14,0),'nx_plan':(15,0),'nx_colors':(16,0),'__bss_start':(17,0),'_edata':(17,0),'_end':(18,0)}
known=dict(gen)
for m,bases in zip(mods,mb):
 for n,q in m.sy:
  if n and q[3] and q[3]<len(m.ss) and m.names[q[3]] in bases:
   assert n not in known,n;known[n]=(bases[m.names[q[3]]],q[4])
fix=[]
for i,(n,q) in enumerate(ref.sy):
 if q[3] in (1,2,3):
  if not n:raise ValueError('unexpected section symbol')
  b,o=known[n];fix.append((i*24+8,b,o))
rel=[]
for k,(m,bases) in enumerate(zip(mods,mb)):
 for off,typ,si,add in m.relocs('.text'):
  assert typ in (2,4),(k,typ)
  n,q=m.sy[si]
  if q[3] and q[3]<len(m.ss):b,o=bases[m.names[q[3]]],q[4]
  else:b,o=known[n]
  rel.append((bases['.text'],off,b,o+add))
# GAS determines each instruction encoding, including every valid TMM triple.
chunks=[]
def chunk(name,body):chunks.append((name,body));return len(chunks)-1
VLOAD=chunk('vload','lea rsi,[rip+pa]\nlea rdi,[rip+pb]\nmov ecx,128\nrep movsq')
VGEMM=chunk('vgemm','lea rdi,[rip+pa]\nlea rsi,[rip+pb]\nlea rdx,[rip+pc]\ncall r15')
VEMIT=chunk('vemit','lea rsi,[rip+pa]\nlea rdi,[rip+pb]\nmov ecx,128\nrep movsq')
RET=chunk('ret','ret');AMXSTART=chunk('amxstart','ldtilecfg [rip+pa]');AMXEND=chunk('amxend','tilerelease\nret')
LA=[];LB=[];ST=[];G=[]
for i in range(8):LA.append(chunk('la'+str(i),f'lea rdi,[rip+pa]\nmov eax,64\ntileloadd tmm{i},[rdi+rax*1]'))
for i in range(8):LB.append(chunk('lb'+str(i),f'lea rdi,[rip+pa]\nlea rsi,[rip+pb]\ncall pc\nlea rdi,[rip+pb]\nmov eax,64\ntileloadd tmm{i},[rdi+rax*1]'))
for d in range(8):
 for a in range(8):
  for b in range(8):G.append(chunk(f'g{d}{a}{b}',f'tilezero tmm{d}\ntdpbssd tmm{d},tmm{a},tmm{b}') if len({d,a,b})==3 else 0xffffffff)
for i in range(8):ST.append(chunk('st'+str(i),f'lea rdi,[rip+pa]\nmov eax,64\ntilestored [rdi+rax*1],tmm{i}'))
src='.intel_syntax noprefix\n.text\n'+''.join(f'zt_{n}:\n{b}\nze_{n}:\n' for n,b in chunks)+'\n.section .note.GNU-stack,"",@progbits\n'
(OUT/'instructions.s').write_text(src);run(['as','--64','-o','instructions.o','instructions.s']);ce=Elf(OUT/'instructions.o');symbols={n:q[4] for n,q in ce.sy};desc=[];patch=[]
for n,_ in chunks:
 lo,hi=symbols['zt_'+n],symbols['ze_'+n];p0=len(patch)
 for o,typ,si,add in ce.relocs('.text'):
  if lo<=o<hi:
   assert typ in (2,4);nm=ce.sy[si][0];assert nm in ('pa','pb','pc');patch.append((o-lo,('pa','pb','pc').index(nm),add))
 desc.append((lo,hi-lo,p0,len(patch)-p0))
lines=['/* Generated at build time from pinned native Assembly. */','#ifndef NIYAH_TENSOR_TEMPLATE_H','#define NIYAH_TENSOR_TEMPLATE_H']
def arr(name,bs):
 lines.append('static const unsigned char '+name+'[] = {'+','.join(str(x) for x in bs)+'};')
def rows(name,ctype,vs):lines.append('static const '+ctype+' '+name+'[] = {'+','.join('{'+','.join(str(x) for x in v)+'}' for v in vs)+'};')
for i,m in enumerate(mods):
 for sec in ['.text','.rodata']:arr('t_module'+str(i)+sec.replace('.','_'),m.sec(sec) or b'\0')
 lines.append(f'enum {{ T_M{i}_TEXT={m.size(".text")},T_M{i}_RO={m.size(".rodata")},T_M{i}_BSS={m.size(".bss")},T_M{i}_TA={m.al(".text")},T_M{i}_RA={m.al(".rodata")},T_M{i}_BA={m.al(".bss")} }};')
arr('t_header',ref.data[:64+56*ref.h[10]]);arr('t_sections',ref.data[ref.h[6]:]);arr('t_symbols',ref.sec('.symtab'));arr('t_strings',ref.sec('.strtab'));arr('t_section_names',ref.sec('.shstrtab'));arr('t_chunks',ce.sec('.text'))
lines+=['typedef struct { unsigned a,b; long c; } TFix;','typedef struct { unsigned a,b,c; long d; } TRel;','typedef struct { unsigned off,len,first,count; } TChunk;']
rows('t_symfix','TFix',fix);rows('t_reloc','TRel',rel);rows('t_patch','TFix',patch);rows('t_chunk','TChunk',desc)
for name,vals in [('t_load_a',LA),('t_load_b',LB),('t_store',ST),('t_gemm',G)]:lines.append('static const unsigned '+name+'[] = {'+','.join(str(x)+'u' for x in vals)+'};')
lines.append(f'enum {{ T_VLOAD={VLOAD},T_VGEMM={VGEMM},T_VEMIT={VEMIT},T_RET={RET},T_AMXSTART={AMXSTART},T_AMXEND={AMXEND} }};')
for name in ['nx_tilecfg','nx_pack_b','_start']:
 b,o=known[name];lines.append(f'enum {{ T_{name.upper()}_BASE={b},T_{name.upper()}_OFF={o} }};')
for ln in (ROOT/'src/tensor_ir_bridge.s').read_text().splitlines():
 m=re.match(r'^(asm_head|s_\w+): \.asciz (.+)$',ln)
 if m:lines.append('static const char '+m[1]+'[] = '+json.dumps(ast.literal_eval(m[2]))+';')
lines.append('#endif');(OUT/'niyah_legacy_tensor_templates.h').write_text('\n'.join(lines)+'\n')
# Preserve the proven interval-coloring body; expose a bounded SysV callable ABI.
original=(MODEL/'src/tile_allocator.s').read_text();body=original.split('nx_color_begin:\n',1)[1].split('nx_color_end:',1)[0]
wrapper='''.file "tensor_allocate.s"
.intel_syntax noprefix
.bss
.balign 16
intervals: .skip 512
live_end: .skip 32
output: .skip 72
.text
.global tensor_allocate
# EDI input ptr, ESI exact byte size, RDX destination, ECX capacity.
tensor_allocate:
 push rbx
 push r12
 push r13
 push r14
 push r15
 mov rbx,rdx
 test rdi,rdi
 jz .Linvalid
 test rbx,rbx
 jz .Linvalid
 cmp esi,16
 jb .Linvalid
 movabs rax,0x313030454c49544e
 cmp [rdi],rax
 jne .Linvalid
 movzx r12d,byte ptr [rdi+8]
 test r12d,r12d
 jz .Linvalid
 cmp r12d,64
 ja .Linvalid
 lea eax,[r12*8+16]
 cmp esi,eax
 jne .Linvalid
 lea eax,[r12+8]
 cmp ecx,eax
 jb .Linvalid
 movzx r13d,byte ptr [rdi+9]
 cmp word ptr [rdi+10],0
 jne .Linvalid
 cmp dword ptr [rdi+12],0
 jne .Linvalid
 lea rsi,[rdi+16]
 lea rdi,[rip+intervals]
 lea ecx,[r12*8]
 cld
 rep movsb
 lea rdi,[rip+live_end]
 xor eax,eax
 mov ecx,8
 rep stosd
 lea rdi,[rip+output]
 mov ecx,72
 rep stosb
 mov dword ptr [rip+output],0x314d4d54
 mov byte ptr [rip+output+4],r12b
 lea rdi,[rip+intervals]
 xor r8d,r8d
 xor r9d,r9d
.Lvalidate:
 mov eax,[rdi+r8*8]
 cmp eax,[rdi+r8*8+4]
 jae .Linvalid
 cmp eax,r9d
 jb .Linvalid
 mov r9d,eax
 inc r8d
 cmp r8d,r12d
 jb .Lvalidate
'''+body+'''
 lea rsi,[rip+output]
 mov rdi,rbx
 lea ecx,[r12+8]
 rep movsb
 xor eax,eax
 jmp .Lreturn
.Lpressure:
 mov eax,77
 jmp .Lreturn
.Linvalid:
 mov eax,65
.Lreturn:
 pop r15
 pop r14
 pop r13
 pop r12
 pop rbx
 ret
.section .note.GNU-stack,"",@progbits
'''
(OUT/'tensor_allocate.s').write_text(wrapper)
identity={'reference_elf_sha256':hashlib.sha256(ref.data).hexdigest(),'reference_elf_bytes':len(ref.data),'allocator_critical_source_sha256':hashlib.sha256(body.encode()).hexdigest(),'runtime_source_sha256':hashlib.sha256((MODEL/'src/tensor_runtime.s').read_bytes()).hexdigest(),'dispatch_source_sha256':hashlib.sha256((MODEL/'src/tensor_dispatch.s').read_bytes()).hexdigest(),'chunks':len(chunks),'relocations':len(rel),'symbol_fixes':len(fix),'header_sha256':hashlib.sha256((OUT/'niyah_legacy_tensor_templates.h').read_bytes()).hexdigest()}
identity['historical_source_contract_match']=current_sources==historical_sources
identity['reference_scope']='complete ELF linked from the recorded current runtime and dispatch sources'
(OUT/'template_manifest.json').write_text(json.dumps(identity,indent=2)+'\n');print(json.dumps(identity))
