#ifndef NIYAH_TENSOR_V2_H
#define NIYAH_TENSOR_V2_H
#include "tensor_core.h"

/* Fixed-shape, straight-line tensor backend with explicit tile spills.
 * Like tensor_core, this module has one bounded, non-reentrant context. */
int tensor_v2_codegen(const TensorValue *values,u32 nv,const TensorOp *ops,
                      u32 no,u32 ni,u32 ne,u32 mask,
                      u8 *out,u32 capacity,CodegenResult *result);
u32 tensor_v2_export_asm(const TensorValue *values,u32 nv,const TensorOp *ops,
                         u32 no,u32 ni,u32 ne,u32 mask,
                         u8 *out,u32 capacity,CodegenResult *result);
#endif
