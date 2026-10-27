.file "compiler_start.s"
.intel_syntax noprefix
.text
.global _start
_start:
 xor ebp,ebp
 mov rdi,[rsp]
 lea rsi,[rsp+8]
 and rsp,-16
 call main
 mov edi,eax
 mov eax,60
 syscall
 ud2
.section .note.GNU-stack,"",@progbits
