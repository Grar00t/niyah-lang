#ifndef NIYAH_TENSOR_CORE_H
#define NIYAH_TENSOR_CORE_H
#include "codegen.h"
/* Single-translation-unit compiler context. Reentrant callers need separate contexts. */
typedef struct {
    u32 type,start,end,name_len,line,col,candidates,root_count;
    u8 name[128]; u32 roots[64][3]; u8 padding[96];
} TensorValue;
typedef struct { u32 kind,d,a,b; } TensorOp;
int tensor_prepare(const Parser *p,u32 root,CodegenResult *r);
u32 tensor_export_ir(u8 *out,u32 capacity);
u32 tensor_export_asm(u8 *out,u32 capacity);
int tensor_codegen(const Parser *p,u32 root,u8 *out,u32 capacity,CodegenResult *r);
int tensor_allocate(const u8 *input,u32 bytes,u8 *output,u32 capacity);
#endif
