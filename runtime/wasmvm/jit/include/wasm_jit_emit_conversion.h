#ifndef _WASM_JIT_EMIT_CONVERSION_H_
#define _WASM_JIT_EMIT_CONVERSION_H_

#include "wasm_jit_compiler.h"

bool wasm_jit_compile_op_i32_trunc_f32(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                       bool sign, bool saturating);

bool wasm_jit_compile_op_i32_trunc_f64(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                       bool sign, bool saturating);

bool wasm_jit_compile_op_i64_trunc_f32(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                       bool sign, bool saturating);

bool wasm_jit_compile_op_i64_trunc_f64(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                       bool sign, bool saturating);

#endif /* end of _WASM_JIT_EMIT_CONVERSION_H_ */
