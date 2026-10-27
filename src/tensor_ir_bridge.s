.file "tensor_ir_bridge.s"
.intel_syntax noprefix
.equ SOURCE_CAP,1048576
.equ IR_CAP,1116192
.equ ASM_CAP,262144
.section .rodata
mode_ir: .asciz "--ir"
mode_iv: .asciz "--intervals"
mode_asm: .asciz "--asm"
kw_a: .asciz "input_a"
kw_b: .asciz "input_b"
kw_g: .asciz "gemm"
kw_e: .asciz "emit"
kw_r: .asciz "reserve"
sig_ignore: .quad 1,0,0,0
asm_head: .asciz ".intel_syntax noprefix\n.section .rodata\n.global nx_plan,nx_colors\nnx_plan:\n .long "
s_long: .asciz " .long "
s_byte: .asciz " .byte "
s_comma: .asciz ","
s_nl: .asciz "\n"
s_colors: .asciz "nx_colors:\n"
s_bss: .asciz ".section .bss\n.balign 64\n.global nx_inputs,nx_slots,nx_outputs,nx_packbuf\nnx_inputs: .skip "
s_slots: .asciz "\nnx_slots: .skip 8192\nnx_outputs: .skip "
s_pack: .asciz "\nnx_packbuf: .skip 1024\n.section .text\n.global nx_vector_program,nx_amx_program\nnx_vector_program:\n"
s_src_input: .asciz " lea rsi,[rip+nx_inputs+"
s_dst_slot: .asciz "]\n lea rdi,[rip+nx_slots+"
s_copy: .asciz "]\n mov ecx,128\n rep movsq\n"
s_arg_a: .asciz " lea rdi,[rip+nx_slots+"
s_arg_b: .asciz "]\n lea rsi,[rip+nx_slots+"
s_arg_c: .asciz "]\n lea rdx,[rip+nx_slots+"
s_call: .asciz "]\n call r15\n"
s_src_slot: .asciz " lea rsi,[rip+nx_slots+"
s_dst_output: .asciz "]\n lea rdi,[rip+nx_outputs+"
s_amx: .asciz " ret\nnx_amx_program:\n ldtilecfg [rip+nx_tilecfg]\n"
s_amx_input: .asciz " lea rdi,[rip+nx_inputs+"
s_amx_load: .asciz "]\n mov eax,64\n tileloadd tmm"
s_amx_address: .asciz ",[rdi+rax*1]\n"
s_pack_call: .asciz "]\n lea rsi,[rip+nx_packbuf]\n call nx_pack_b\n lea rdi,[rip+nx_packbuf"
s_zero: .asciz " tilezero tmm"
s_mma: .asciz "\n tdpbssd tmm"
s_tmm: .asciz ",tmm"
s_amx_out: .asciz " lea rdi,[rip+nx_outputs+"
s_store: .asciz "]\n mov eax,64\n tilestored [rdi+rax*1],tmm"
s_end: .asciz " tilerelease\n ret\n.section .note.GNU-stack,\"\",@progbits\n"
.section .bss
.balign 16
source: .skip SOURCE_CAP
ir_in: .skip IR_CAP
ir_out: .skip IR_CAP
ops: .skip 2048
asm_out: .skip ASM_CAP
lexer: .skip 32
token: .skip 40
morph_result: .skip 8200
intervals: .skip 528
allocation: .skip 72
extra: .skip 1
number: .skip 24
mode: .skip 4
source_len: .skip 4
ir_in_len: .skip 4
ir_len: .skip 4
out_len: .skip 4
nv: .skip 4
no: .skip 4
ni: .skip 4
n_emit: .skip 4
reserved: .skip 4
reserve_seen: .skip 4
op_d: .skip 4
op_a: .skip 4
op_b: .skip 4
input_index: .skip 4
output_index: .skip 4
.section .text
.global _start
.extern lex_init,lex_next,morph_analyze
_start:
 cld
 mov r15,rsp
 and rsp,-16
 mov eax,13
 mov edi,13
 lea rsi,[rip+sig_ignore]
 xor edx,edx
 mov r10d,8
 syscall
 test rax,rax
 js badoutput
 cmp qword ptr [r15],3
 jb badarg
 cmp qword ptr [r15],4
 ja badarg
 mov rdi,[r15+16]
 lea rsi,[rip+mode_ir]
 call str_eq
 test eax,eax
 jnz .mode_ir
 mov rdi,[r15+16]
 lea rsi,[rip+mode_iv]
 call str_eq
 test eax,eax
 jnz .mode_iv
 mov rdi,[r15+16]
 lea rsi,[rip+mode_asm]
 call str_eq
 test eax,eax
 jz badarg
 cmp qword ptr [r15],4
 jne badarg
 mov dword ptr [rip+mode],2
 jmp .read_ir
.mode_iv:
 cmp qword ptr [r15],3
 jne badarg
 mov dword ptr [rip+mode],1
 jmp .read_ir
.mode_ir:
 cmp qword ptr [r15],3
 jne badarg
 mov rdi,[r15+24]
 lea rsi,[rip+source]
 mov edx,SOURCE_CAP
 call read_file
 mov [rip+source_len],eax
 call parse_source
 lea rsi,[rip+ir_out]
 mov edx,[rip+ir_len]
 jmp write_exit
.read_ir:
 mov rdi,[r15+24]
 lea rsi,[rip+ir_in]
 mov edx,IR_CAP
 call read_file
 mov [rip+ir_in_len],eax
 cmp eax,32
 jb badinput
 movabs rax,0x313030304752494e
 cmp [rip+ir_in],rax
 jne badinput
 mov eax,[rip+ir_in+8]
 test eax,eax
 jz badinput
 cmp eax,64
 ja badinput
 shl eax,10
 mov ecx,[rip+ir_in+12]
 test ecx,ecx
 jz badinput
 cmp ecx,128
 ja badinput
 shl ecx,4
 add eax,ecx
 add eax,32
 mov edx,[rip+ir_in+28]
 cmp edx,SOURCE_CAP
 ja badinput
 mov [rip+source_len],edx
 lea ecx,[rax+rdx]
 cmp ecx,[rip+ir_in_len]
 jne badinput
 lea rsi,[rip+ir_in]
 add rsi,rax
 lea rdi,[rip+source]
 mov ecx,edx
 rep movsb
 call parse_source
 mov ecx,[rip+ir_len]
 cmp ecx,[rip+ir_in_len]
 jne badinput
 lea rsi,[rip+ir_out]
 lea rdi,[rip+ir_in]
 repe cmpsb
 jne badinput
 cmp dword ptr [rip+mode],1
 jne .read_assignment
 lea rdi,[rip+intervals]
 movabs rax,0x313030454c49544e
 mov [rdi],rax
 mov eax,[rip+nv]
 mov [rdi+8],al
 mov eax,[rip+reserved]
 mov [rdi+9],al
 xor r8d,r8d
 lea rsi,[rip+ir_out+32]
.iv_loop:
 mov rax,[rsi+4]
 mov [rdi+r8*8+16],rax
 add rsi,1024
 inc r8d
 cmp r8d,[rip+nv]
 jb .iv_loop
 lea rsi,[rip+intervals]
 mov edx,[rip+nv]
 lea edx,[rdx*8+16]
 jmp write_exit
.read_assignment:
 mov rdi,[r15+32]
 lea rsi,[rip+allocation]
 mov edx,72
 call read_file
 mov ecx,[rip+nv]
 add ecx,8
 cmp eax,ecx
 jne badinput
 cmp dword ptr [rip+allocation],0x314d4d54
 jne badinput
 movzx eax,byte ptr [rip+allocation+4]
 cmp eax,[rip+nv]
 jne badinput
 cmp word ptr [rip+allocation+5],0
 jne badinput
 cmp byte ptr [rip+allocation+7],0
 jne badinput
 call verify_colors
 call emit_assembly
 lea rsi,[rip+asm_out]
 mov edx,[rip+out_len]
 jmp write_exit

str_eq:
 xor eax,eax
.se_loop:
 mov cl,[rdi+rax]
 cmp cl,[rsi+rax]
 jne .se_no
 inc eax
 test cl,cl
 jnz .se_loop
 mov eax,1
 ret
.se_no:
 xor eax,eax
 ret
# RDI=path, RSI=buffer, EDX=capacity. Returns byte count. Never overwrites a file.
read_file:
 push r12
 push r13
 push r14
 mov r12,rsi
 mov r13d,edx
 mov eax,2
 mov esi,0x20000
 xor edx,edx
 syscall
 test rax,rax
 js badfile
 mov r14,rax
 xor r8d,r8d
.rf_loop:
 mov edx,r13d
 sub edx,r8d
 jz .rf_extra
 lea rsi,[r12+r8]
 xor eax,eax
 mov rdi,r14
 syscall
 cmp rax,-4
 je .rf_loop
 test rax,rax
 js badfile
 jz .rf_done
 add r8d,eax
 jmp .rf_loop
.rf_extra:
 xor eax,eax
 mov rdi,r14
 lea rsi,[rip+extra]
 mov edx,1
 syscall
 cmp rax,-4
 je .rf_extra
 test rax,rax
 js badfile
 jnz badinput
.rf_done:
 mov eax,3
 mov rdi,r14
 syscall
 test rax,rax
 js badfile
 mov eax,r8d
 pop r14
 pop r13
 pop r12
 ret
next_token:
 push rbp
 mov rbp,rsp
 and rsp,-16
 lea rdi,[rip+token]
 lea rsi,[rip+lexer]
 call lex_next
 mov rsp,rbp
 pop rbp
 cmp dword ptr [rip+token],1
 je badinput
 ret
init_lexer:
 push rbp
 mov rbp,rsp
 and rsp,-16
 lea rdi,[rip+lexer]
 lea rsi,[rip+source]
 mov edx,[rip+source_len]
 call lex_init
 mov rsp,rbp
 pop rbp
 ret
call_morph:
 push rbp
 mov rbp,rsp
 and rsp,-16
 call morph_analyze
 mov rsp,rbp
 pop rbp
 ret
# Current token equals zero-terminated RSI (does not inspect bytes beyond token).
token_eq:
 mov ecx,[rip+token+24]
 mov rdi,[rip+token+16]
 xor eax,eax
.te_loop:
 cmp eax,ecx
 je .te_end
 mov dl,[rsi+rax]
 test dl,dl
 jz .te_no
 cmp dl,[rdi+rax]
 jne .te_no
 inc eax
 jmp .te_loop
.te_end:
 cmp byte ptr [rsi+rax],0
 jne .te_no
 mov eax,1
 ret
.te_no:
 xor eax,eax
 ret
# Exact surface-byte symbol identity, never root-based aliasing.
find_value:
 xor r8d,r8d
 lea r10,[rip+ir_out+32]
.fv_loop:
 cmp r8d,[rip+nv]
 jae .fv_absent
 mov ecx,[rip+token+24]
 cmp ecx,[r10+12]
 jne .fv_next
 lea rdi,[r10+32]
 mov rsi,[rip+token+16]
 repe cmpsb
 je .fv_found
.fv_next:
 add r10,1024
 inc r8d
 jmp .fv_loop
.fv_absent:
 mov eax,-1
 ret
.fv_found:
 mov eax,r8d
 ret
new_value:
 push rbx
 push r13
 push r14
 cmp dword ptr [rip+token],2
 jne badinput
 mov eax,[rip+token+24]
 test eax,eax
 jz badinput
 cmp eax,128
 ja badinput
 cmp dword ptr [rip+nv],64
 jae badinput
 call find_value
 cmp eax,-1
 jne badinput
 mov ebx,[rip+nv]
 shl ebx,10
 lea r13,[rip+ir_out+32]
 add r13,rbx
 mov [r13],r12d
 mov eax,[rip+no]
 mov [r13+4],eax
 inc eax
 mov [r13+8],eax
 mov eax,[rip+token+24]
 mov [r13+12],eax
 mov rax,[rip+token+4]
 mov [r13+16],rax
 lea rdi,[r13+32]
 mov rsi,[rip+token+16]
 mov ecx,[rip+token+24]
 rep movsb
 mov rdi,[rip+token+16]
 mov esi,[rip+token+24]
 lea rdx,[rip+morph_result]
 call call_morph
 test eax,eax
 jnz badinput
 cmp dword ptr [rip+morph_result+8196],0
 jne badinput
 mov r14d,[rip+morph_result+8192]
 cmp r14d,64
 ja badinput
 mov [r13+24],r14d
 lea rsi,[rip+morph_result]
 xor ecx,ecx
 xor edx,edx
.nv_roots:
 cmp ecx,r14d
 jae .nv_done
 cmp dword ptr [rsi+120],3
 jne .nv_next
 imul eax,edx,12
 lea rdi,[r13+rax+160]
 mov rax,[rsi+108]
 mov [rdi],rax
 mov eax,[rsi+116]
 mov [rdi+8],eax
 inc edx
.nv_next:
 add rsi,128
 inc ecx
 jmp .nv_roots
.nv_done:
 mov [r13+28],edx
 mov eax,[rip+nv]
 inc dword ptr [rip+nv]
 pop r14
 pop r13
 pop rbx
 ret
get_use:
 cmp dword ptr [rip+token],2
 jne badinput
 call find_value
 cmp eax,-1
 je badinput
 mov edx,eax
 shl edx,10
 lea rdi,[rip+ir_out+32]
 mov ecx,[rip+no]
 inc ecx
 mov [rdi+rdx+8],ecx
 ret
record_op:
 mov ecx,[rip+no]
 cmp ecx,128
 jae badinput
 shl ecx,4
 lea rdi,[rip+ops]
 add rdi,rcx
 mov [rdi],r12d
 mov eax,[rip+op_d]
 mov [rdi+4],eax
 mov eax,[rip+op_a]
 mov [rdi+8],eax
 mov eax,[rip+op_b]
 mov [rdi+12],eax
 inc dword ptr [rip+no]
 ret
parse_source:
 call init_lexer
.ps_next:
 call next_token
 cmp dword ptr [rip+token],0
 je .ps_done
 cmp dword ptr [rip+token],2
 jne badinput
 cmp dword ptr [rip+no],128
 jae badinput
 mov dword ptr [rip+op_a],0
 mov dword ptr [rip+op_b],0
 lea rsi,[rip+kw_r]
 call token_eq
 test eax,eax
 jnz .ps_reserve
 mov r12d,1
 lea rsi,[rip+kw_a]
 call token_eq
 test eax,eax
 jnz .ps_input
 mov r12d,2
 lea rsi,[rip+kw_b]
 call token_eq
 test eax,eax
 jnz .ps_input
 mov r12d,3
 lea rsi,[rip+kw_g]
 call token_eq
 test eax,eax
 jnz .ps_gemm
 mov r12d,4
 lea rsi,[rip+kw_e]
 call token_eq
 test eax,eax
 jz badinput
 call next_token
 call get_use
 cmp dword ptr [rdi+rdx],3
 jne badinput
 mov [rip+op_d],eax
 inc dword ptr [rip+n_emit]
 jmp .ps_semi
.ps_reserve:
 cmp dword ptr [rip+reserve_seen],0
 jne badinput
 cmp dword ptr [rip+no],0
 jne badinput
 call next_token
 cmp dword ptr [rip+token],3
 jne badinput
 mov rax,[rip+token+32]
 cmp rax,255
 ja badinput
 mov [rip+reserved],eax
 mov dword ptr [rip+reserve_seen],1
 call next_token
 cmp dword ptr [rip+token],20
 jne badinput
 jmp .ps_next
.ps_input:
 call next_token
 call new_value
 mov [rip+op_d],eax
 inc dword ptr [rip+ni]
 jmp .ps_semi
.ps_gemm:
 call next_token
 call new_value
 mov [rip+op_d],eax
 call next_token
 cmp dword ptr [rip+token],34
 jne badinput
 call next_token
 call get_use
 cmp dword ptr [rdi+rdx],1
 jne badinput
 mov [rip+op_a],eax
 call next_token
 cmp dword ptr [rip+token],26
 jne badinput
 call next_token
 call get_use
 cmp dword ptr [rdi+rdx],2
 jne badinput
 mov [rip+op_b],eax
.ps_semi:
 call next_token
 cmp dword ptr [rip+token],20
 jne badinput
 call record_op
 jmp .ps_next
.ps_done:
 cmp dword ptr [rip+n_emit],0
 je badinput
 lea rdi,[rip+ir_out]
 movabs rax,0x313030304752494e
 mov [rdi],rax
 mov eax,[rip+nv]
 mov [rdi+8],eax
 mov eax,[rip+no]
 mov [rdi+12],eax
 mov eax,[rip+ni]
 mov [rdi+16],eax
 mov eax,[rip+n_emit]
 mov [rdi+20],eax
 mov eax,[rip+reserved]
 mov [rdi+24],eax
 mov eax,[rip+source_len]
 mov [rdi+28],eax
 mov eax,[rip+nv]
 shl eax,10
 add rdi,rax
 add rdi,32
 mov ecx,[rip+no]
 shl ecx,4
 lea rsi,[rip+ops]
 rep movsb
 lea rsi,[rip+source]
 mov ecx,[rip+source_len]
 rep movsb
 lea rax,[rip+ir_out]
 sub rdi,rax
 mov [rip+ir_len],edi
 ret
verify_colors:
 lea rsi,[rip+allocation+8]
 lea rdi,[rip+ir_out+32]
 xor r8d,r8d
.vc_outer:
 movzx eax,byte ptr [rsi+r8]
 cmp eax,8
 jae badinput
 bt dword ptr [rip+reserved],eax
 jc badinput
 mov r10d,r8d
 shl r10d,10
 xor r9d,r9d
.vc_inner:
 cmp r9d,r8d
 jae .vc_next
 cmp al,[rsi+r9]
 jne .vc_inner_next
 mov r11d,r9d
 shl r11d,10
 mov edx,[rdi+r10+4]
 cmp edx,[rdi+r11+8]
 jb badinput
.vc_inner_next:
 inc r9d
 jmp .vc_inner
.vc_next:
 inc r8d
 cmp r8d,[rip+nv]
 jb .vc_outer
 ret
# Output-building functions do not flush before whole compilation succeeds.
emit_s:
 mov ecx,[rip+out_len]
 lea rdi,[rip+asm_out]
.es_loop:
 lodsb
 test al,al
 jz .es_done
 cmp ecx,ASM_CAP
 jae badoutput
 mov [rdi+rcx],al
 inc ecx
 jmp .es_loop
.es_done:
 mov [rip+out_len],ecx
 ret
emit_n:
 lea rsi,[rip+number+23]
 mov byte ptr [rsi],0
 mov ecx,10
.en_loop:
 xor edx,edx
 div ecx
 add dl,48
 dec rsi
 mov [rsi],dl
 test eax,eax
 jnz .en_loop
 jmp emit_s
color_number:
 lea rdx,[rip+allocation+8]
 movzx eax,byte ptr [rdx+rax]
 jmp emit_n
slot_number:
 lea rdx,[rip+allocation+8]
 movzx eax,byte ptr [rdx+rax]
 shl eax,10
 jmp emit_n
emit_assembly:
 lea rsi,[rip+asm_head]
 call emit_s
 mov eax,[rip+nv]
 call emit_n
 lea rsi,[rip+s_comma]
 call emit_s
 mov eax,[rip+no]
 call emit_n
 lea rsi,[rip+s_comma]
 call emit_s
 mov eax,[rip+ni]
 call emit_n
 lea rsi,[rip+s_comma]
 call emit_s
 mov eax,[rip+n_emit]
 call emit_n
 lea rsi,[rip+s_nl]
 call emit_s
 xor r12d,r12d
.ea_ops:
 lea rsi,[rip+s_long]
 call emit_s
 xor r13d,r13d
.ea_fields:
 mov eax,r12d
 shl eax,4
 add eax,r13d
 lea rdx,[rip+ops]
 mov eax,[rdx+rax]
 call emit_n
 add r13d,4
 cmp r13d,16
 je .ea_op_end
 lea rsi,[rip+s_comma]
 call emit_s
 jmp .ea_fields
.ea_op_end:
 lea rsi,[rip+s_nl]
 call emit_s
 inc r12d
 cmp r12d,[rip+no]
 jb .ea_ops
 lea rsi,[rip+s_colors]
 call emit_s
 xor r12d,r12d
.ea_colors:
 lea rsi,[rip+s_byte]
 call emit_s
 mov eax,r12d
 call color_number
 lea rsi,[rip+s_nl]
 call emit_s
 inc r12d
 cmp r12d,[rip+nv]
 jb .ea_colors
 lea rsi,[rip+s_bss]
 call emit_s
 mov eax,[rip+ni]
 shl eax,10
 call emit_n
 lea rsi,[rip+s_slots]
 call emit_s
 mov eax,[rip+n_emit]
 shl eax,10
 call emit_n
 lea rsi,[rip+s_pack]
 call emit_s
 xor r12d,r12d
.ea_vector:
 mov eax,r12d
 shl eax,4
 lea r14,[rip+ops]
 add r14,rax
 cmp dword ptr [r14],3
 je .ea_vec_gemm
 ja .ea_vec_emit
 lea rsi,[rip+s_src_input]
 call emit_s
 mov eax,[rip+input_index]
 shl eax,10
 call emit_n
 inc dword ptr [rip+input_index]
 lea rsi,[rip+s_dst_slot]
 call emit_s
 mov eax,[r14+4]
 call slot_number
 lea rsi,[rip+s_copy]
 call emit_s
 jmp .ea_vec_next
.ea_vec_gemm:
 lea rsi,[rip+s_arg_a]
 call emit_s
 mov eax,[r14+8]
 call slot_number
 lea rsi,[rip+s_arg_b]
 call emit_s
 mov eax,[r14+12]
 call slot_number
 lea rsi,[rip+s_arg_c]
 call emit_s
 mov eax,[r14+4]
 call slot_number
 lea rsi,[rip+s_call]
 call emit_s
 jmp .ea_vec_next
.ea_vec_emit:
 lea rsi,[rip+s_src_slot]
 call emit_s
 mov eax,[r14+4]
 call slot_number
 lea rsi,[rip+s_dst_output]
 call emit_s
 mov eax,[rip+output_index]
 shl eax,10
 call emit_n
 inc dword ptr [rip+output_index]
 lea rsi,[rip+s_copy]
 call emit_s
.ea_vec_next:
 inc r12d
 cmp r12d,[rip+no]
 jb .ea_vector
 lea rsi,[rip+s_amx]
 call emit_s
 mov dword ptr [rip+input_index],0
 mov dword ptr [rip+output_index],0
 xor r12d,r12d
.ea_tile:
 mov eax,r12d
 shl eax,4
 lea r14,[rip+ops]
 add r14,rax
 cmp dword ptr [r14],3
 je .ea_tile_gemm
 ja .ea_tile_emit
 lea rsi,[rip+s_amx_input]
 call emit_s
 mov eax,[rip+input_index]
 shl eax,10
 call emit_n
 inc dword ptr [rip+input_index]
 cmp dword ptr [r14],2
 jne .ea_tile_load
 lea rsi,[rip+s_pack_call]
 call emit_s
.ea_tile_load:
 lea rsi,[rip+s_amx_load]
 call emit_s
 mov eax,[r14+4]
 call color_number
 lea rsi,[rip+s_amx_address]
 call emit_s
 jmp .ea_tile_next
.ea_tile_gemm:
 lea rsi,[rip+s_zero]
 call emit_s
 mov eax,[r14+4]
 call color_number
 lea rsi,[rip+s_mma]
 call emit_s
 mov eax,[r14+4]
 call color_number
 lea rsi,[rip+s_tmm]
 call emit_s
 mov eax,[r14+8]
 call color_number
 lea rsi,[rip+s_tmm]
 call emit_s
 mov eax,[r14+12]
 call color_number
 lea rsi,[rip+s_nl]
 call emit_s
 jmp .ea_tile_next
.ea_tile_emit:
 lea rsi,[rip+s_amx_out]
 call emit_s
 mov eax,[rip+output_index]
 shl eax,10
 call emit_n
 inc dword ptr [rip+output_index]
 lea rsi,[rip+s_store]
 call emit_s
 mov eax,[r14+4]
 call color_number
 lea rsi,[rip+s_nl]
 call emit_s
.ea_tile_next:
 inc r12d
 cmp r12d,[rip+no]
 jb .ea_tile
 lea rsi,[rip+s_end]
 jmp emit_s
write_exit:
 test edx,edx
 jz success
.we_loop:
 mov eax,1
 mov edi,1
 syscall
 cmp rax,-4
 je .we_loop
 test rax,rax
 jle badoutput
 add rsi,rax
 sub rdx,rax
 jnz .we_loop
success:
 xor edi,edi
 jmp exit_code
badarg:
 mov edi,64
 jmp exit_code
badinput:
 mov edi,65
 jmp exit_code
badfile:
 mov edi,66
 jmp exit_code
badoutput:
 mov edi,74
exit_code:
 mov eax,60
 syscall
 ud2
.section .note.GNU-stack,"",@progbits
