.file "tensor_v2_runtime.s"
.intel_syntax noprefix
# Syscall-only v2 tensor runtime. The v1 runtime remains byte-for-byte unchanged.
# Scalar is an explicit oracle. Automatic execution requires AMX INT8 or AVX2.
.section .rodata
v2_auto: .asciz "auto"
v2_scalar: .asciz "scalar"
v2_avx2: .asciz "avx2"
v2_amx: .asciz "amx"
v2_features: .asciz "features"
.balign 8
v2_sigpipe_ignore: .quad 1,0,0,0
.balign 64
.global nx_tilecfg
nx_tilecfg:
 .byte 1,0
 .zero 14
 .rept 8
 .word 64
 .endr
 .zero 16
 .rept 8
 .byte 16
 .endr
 .zero 8
.balign 16
.weak nx_plan
nx_plan: .long 0,0,0,0
.section .bss
.balign 64
.weak nx_inputs,nx_outputs
nx_inputs: .skip 65536
nx_outputs: .skip 131072
v2_extra: .skip 1
.balign 4
v2_requested: .skip 4
v2_features_only: .skip 4
.section .text
.global _start,nx_i8_scalar,nx_i8_avx2,nx_pack_b
.global nx_i32_add,nx_i32_relu,nx_i32_transpose,nx_v2_amx_enable
.extern nx_cpu,nx_detect_int8,nx_amx_permission
_start:
 cld
 mov r14,rsp
 and rsp,-16
 mov eax,13
 mov edi,13
 lea rsi,[rip+v2_sigpipe_ignore]
 xor edx,edx
 mov r10d,8
 syscall
 test rax,rax
 js .v2_output_error
 call nx_detect_int8
 mov dword ptr [rip+v2_requested],-1
 cmp qword ptr [r14],1
 je .v2_choose
 cmp qword ptr [r14],2
 jne .v2_usage_error
 mov rdi,[r14+16]
 lea rsi,[rip+v2_auto]
 call .v2_equal
 test eax,eax
 jnz .v2_choose
 lea rsi,[rip+v2_features]
 call .v2_equal
 test eax,eax
 jnz .v2_feature_mode
 mov dword ptr [rip+v2_requested],0
 lea rsi,[rip+v2_scalar]
 call .v2_equal
 test eax,eax
 jnz .v2_choose
 mov dword ptr [rip+v2_requested],1
 lea rsi,[rip+v2_avx2]
 call .v2_equal
 test eax,eax
 jnz .v2_choose
 mov dword ptr [rip+v2_requested],3
 lea rsi,[rip+v2_amx]
 call .v2_equal
 test eax,eax
 jz .v2_usage_error
 jmp .v2_choose
.v2_feature_mode:
 mov dword ptr [rip+v2_requested],-1
 mov dword ptr [rip+v2_features_only],1
.v2_choose:
 mov eax,[rip+v2_requested]
 test eax,eax
 jz .v2_selected
 cmp eax,1
 je .v2_force_avx2
 cmp eax,3
 je .v2_force_amx
 call nx_v2_amx_enable
 test eax,eax
 jz .v2_select_amx
 test qword ptr [rip+nx_cpu+8],1
 jnz .v2_select_avx2
 # Feature inspection remains available on unsupported hardware.
 cmp dword ptr [rip+v2_features_only],0
 je .v2_unsupported
 mov eax,77
 jmp .v2_selected
.v2_force_avx2:
 test qword ptr [rip+nx_cpu+8],1
 jz .v2_unsupported
.v2_select_avx2:
 mov eax,1
 jmp .v2_selected
.v2_force_amx:
 call nx_v2_amx_enable
 test eax,eax
 jnz .v2_unsupported
.v2_select_amx:
 mov eax,3
.v2_selected:
 mov [rip+nx_cpu+48],rax
 cmp dword ptr [rip+v2_features_only],0
 jne .v2_write_features
 lea r15,[rip+nx_i8_scalar]
 cmp eax,1
 jne .v2_validate_plan
 lea r15,[rip+nx_i8_avx2]
.v2_validate_plan:
 mov eax,[rip+nx_plan]
 dec eax
 cmp eax,63
 ja .v2_input_error
 mov eax,[rip+nx_plan+4]
 dec eax
 cmp eax,127
 ja .v2_input_error
 mov eax,[rip+nx_plan+8]
 cmp eax,64
 ja .v2_input_error
 cmp eax,[rip+nx_plan]
 ja .v2_input_error
 mov eax,[rip+nx_plan+12]
 dec eax
 cmp eax,127
 ja .v2_input_error
 lea rsi,[rip+nx_inputs]
 mov edx,[rip+nx_plan+8]
 shl edx,10
 call .v2_read_exact
.v2_read_extra:
 xor eax,eax
 xor edi,edi
 lea rsi,[rip+v2_extra]
 mov edx,1
 syscall
 cmp rax,-4
 je .v2_read_extra
 test rax,rax
 js .v2_read_error
 jnz .v2_input_error
 cmp qword ptr [rip+nx_cpu+48],3
 jne .v2_vector
 call nx_amx_program
 jmp .v2_output
.v2_vector:
 call nx_vector_program
.v2_output:
 lea rsi,[rip+nx_outputs]
 mov edx,[rip+nx_plan+12]
 shl edx,10
 jmp .v2_write_all
.v2_write_features:
 lea rsi,[rip+nx_cpu]
 mov edx,64
 jmp .v2_write_all
.v2_equal:
 xor eax,eax
.v2_equal_loop:
 mov cl,[rdi+rax]
 cmp cl,[rsi+rax]
 jne .v2_not_equal
 inc eax
 test cl,cl
 jnz .v2_equal_loop
 mov eax,1
 ret
.v2_not_equal:
 xor eax,eax
 ret
.v2_read_exact:
 test edx,edx
 jz .v2_read_done
.v2_read_loop:
 xor eax,eax
 xor edi,edi
 syscall
 cmp rax,-4
 je .v2_read_loop
 test rax,rax
 js .v2_read_error
 jz .v2_input_error
 add rsi,rax
 sub rdx,rax
 jnz .v2_read_loop
.v2_read_done:
 ret
.v2_write_all:
 test edx,edx
 jz .v2_success
.v2_write_loop:
 mov eax,1
 mov edi,1
 syscall
 cmp rax,-4
 je .v2_write_loop
 test rax,rax
 jle .v2_output_error
 add rsi,rax
 sub rdx,rax
 jnz .v2_write_loop
.v2_success:
 xor edi,edi
 jmp .v2_exit
.v2_usage_error:
 mov edi,64
 jmp .v2_exit
.v2_input_error:
 mov edi,65
 jmp .v2_exit
.v2_read_error:
 mov edi,66
 jmp .v2_exit
.v2_output_error:
 mov edi,74
 jmp .v2_exit
.v2_unsupported:
 mov edi,77
.v2_exit:
 mov eax,60
 syscall
 ud2

# Require OS permission and the palette/TMUL limits used by the generated code.
# No tile instruction executes here. Every failure is an ordinary fallback gate.
nx_v2_amx_enable:
 push rbx
 call nx_amx_permission
 test eax,eax
 jnz .v2_amx_no
 xor eax,eax
 cpuid
 cmp eax,0x1e
 jb .v2_amx_no
 mov eax,0x1d
 xor ecx,ecx
 cpuid
 cmp eax,1
 jb .v2_amx_no
 mov eax,0x1d
 mov ecx,1
 cpuid
 mov r8d,eax
 and eax,0xffff
 cmp eax,8192
 jb .v2_amx_no
 shr r8d,16
 cmp r8d,1024
 jb .v2_amx_no
 mov eax,ebx
 and eax,0xffff
 cmp eax,64
 jb .v2_amx_no
 shr ebx,16
 cmp ebx,8
 jb .v2_amx_no
 and ecx,0xffff
 cmp ecx,16
 jb .v2_amx_no
 mov eax,0x1e
 xor ecx,ecx
 cpuid
 mov eax,ebx
 and eax,255
 cmp eax,16
 jb .v2_amx_no
 shr ebx,8
 and ebx,0xffff
 cmp ebx,64
 jb .v2_amx_no
 xor eax,eax
 pop rbx
 ret
.v2_amx_no:
 mov eax,77
 pop rbx
 ret

# Signed i8 A[16,64] * B[64,16] -> i32 C[16,16], all row-major.
# RDI=A, RSI=B, RDX=C. Helpers preserve r12, r13, r15 and AMX tile state.
nx_i8_scalar:
 push rbx
 push r12
 xor r8d,r8d
.v2_scalar_i:
 xor r9d,r9d
.v2_scalar_j:
 lea r10,[rdi+r8]
 lea r11,[rsi+r9]
 xor eax,eax
 xor ecx,ecx
.v2_scalar_k:
 movsx ebx,byte ptr [r10+rcx]
 movsx r12d,byte ptr [r11]
 imul ebx,r12d
 add eax,ebx
 add r11,16
 inc ecx
 cmp ecx,64
 jb .v2_scalar_k
 lea r10,[rdx+r8]
 mov [r10+r9*4],eax
 inc r9d
 cmp r9d,16
 jb .v2_scalar_j
 add r8,64
 cmp r8,1024
 jb .v2_scalar_i
 pop r12
 pop rbx
 ret

# Exact AVX2 decomposition: signed a = (a_bits & 127) - (a_bits & 128).
# VPMADDUBSW pair sums remain in [-32512,32258] and [-32768,32512].
# VPMADDWD with ones widens before subtraction; VPADDD joins the K halves.
# Each raw B column is gathered once and reused for all 16 A rows.
nx_i8_avx2:
 sub rsp,64
 mov eax,0x7f7f7f7f
 vmovd xmm6,eax
 vpbroadcastd ymm6,xmm6
 mov eax,0x80808080
 vmovd xmm7,eax
 vpbroadcastd ymm7,xmm7
 mov eax,0x00010001
 vmovd xmm8,eax
 vpbroadcastd ymm8,xmm8
 xor r9d,r9d
.v2_avx2_column:
 lea r11,[rsi+r9]
 xor ecx,ecx
.v2_avx2_gather:
 mov al,[r11]
 mov [rsp+rcx],al
 add r11,16
 inc ecx
 cmp ecx,64
 jb .v2_avx2_gather
 vmovdqu ymm4,[rsp]
 vmovdqu ymm5,[rsp+32]
 xor r8d,r8d
.v2_avx2_row:
 vmovdqu ymm0,[rdi+r8]
 vpand ymm1,ymm0,ymm6
 vpand ymm2,ymm0,ymm7
 vpmaddubsw ymm1,ymm1,ymm4
 vpmaddubsw ymm2,ymm2,ymm4
 vpmaddwd ymm1,ymm1,ymm8
 vpmaddwd ymm2,ymm2,ymm8
 vpsubd ymm3,ymm1,ymm2
 vmovdqu ymm0,[rdi+r8+32]
 vpand ymm1,ymm0,ymm6
 vpand ymm2,ymm0,ymm7
 vpmaddubsw ymm1,ymm1,ymm5
 vpmaddubsw ymm2,ymm2,ymm5
 vpmaddwd ymm1,ymm1,ymm8
 vpmaddwd ymm2,ymm2,ymm8
 vpsubd ymm0,ymm1,ymm2
 vpaddd ymm0,ymm0,ymm3
 vextracti128 xmm1,ymm0,1
 vpaddd xmm0,xmm0,xmm1
 vphaddd xmm0,xmm0,xmm0
 vphaddd xmm0,xmm0,xmm0
 vmovd eax,xmm0
 lea r11,[rdx+r8]
 mov [r11+r9*4],eax
 add r8,64
 cmp r8,1024
 jb .v2_avx2_row
 inc r9d
 cmp r9d,16
 jb .v2_avx2_column
 add rsp,64
 vzeroupper
 ret

# RDI raw B, RSI packed B: packed[g,4*j+l] = B[4*g+l,j].
nx_pack_b:
 xor r8d,r8d
.v2_pack_g:
 xor r9d,r9d
.v2_pack_j:
 xor r10d,r10d
.v2_pack_l:
 mov eax,r8d
 shl eax,2
 add eax,r10d
 shl eax,4
 add eax,r9d
 mov cl,[rdi+rax]
 mov eax,r8d
 shl eax,6
 lea eax,[rax+r9*4]
 add eax,r10d
 mov [rsi+rax],cl
 inc r10d
 cmp r10d,4
 jb .v2_pack_l
 inc r9d
 cmp r9d,16
 jb .v2_pack_j
 inc r8d
 cmp r8d,16
 jb .v2_pack_g
 ret

# RDI=A, RSI=B, RDX=C; elementwise 32-bit wrapping addition.
nx_i32_add:
 xor ecx,ecx
.v2_add_loop:
 mov eax,[rdi+rcx]
 add eax,[rsi+rcx]
 mov [rdx+rcx],eax
 add ecx,4
 cmp ecx,1024
 jb .v2_add_loop
 ret

# RDI=input, RDX=output; signed i32 ReLU including INT32_MIN.
nx_i32_relu:
 xor ecx,ecx
 xor r8d,r8d
.v2_relu_loop:
 mov eax,[rdi+rcx]
 test eax,eax
 cmovs eax,r8d
 mov [rdx+rcx],eax
 add ecx,4
 cmp ecx,1024
 jb .v2_relu_loop
 ret

# RDI=input, RDX=output, distinct row-major i32[16,16] buffers.
nx_i32_transpose:
 xor r8d,r8d
.v2_transpose_i:
 xor r9d,r9d
.v2_transpose_j:
 mov eax,r8d
 shl eax,6
 lea r10,[rdi+rax]
 mov ecx,[r10+r9*4]
 mov eax,r9d
 shl eax,6
 lea r10,[rdx+rax]
 mov [r10+r8*4],ecx
 inc r9d
 cmp r9d,16
 jb .v2_transpose_j
 inc r8d
 cmp r8d,16
 jb .v2_transpose_i
 ret

# Fixed executable patch regions. Strong symbols from a textual .s export may
# override these defaults when linking a verification-only external build.
.balign 16
.weak nx_vector_program,nx_amx_program
nx_vector_program:
 .fill 32768,1,0xcc
nx_amx_program:
 .fill 32768,1,0xcc
.section .note.GNU-stack,"",@progbits
