#ifndef _WASM_JIT_EMIT_CONVERSION_H_
#define _WASM_JIT_EMIT_CONVERSION_H_

#include "wasm_jit_compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

    bool
    wasm_jit_compile_op_i32_trunc_f32(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                      bool sign, bool saturating);

    bool
    wasm_jit_compile_op_i32_trunc_f64(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                      bool sign, bool saturating);

    bool
    wasm_jit_compile_op_i64_trunc_f32(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                      bool sign, bool saturating);

    bool
    wasm_jit_compile_op_i64_trunc_f64(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                      bool sign, bool saturating);

    bool
    wasm_jit_compile_op_f32_convert_i32(JITCompContext *comp_ctx,
                                        JITFuncContext *func_ctx, bool sign);

    bool
    wasm_jit_compile_op_f32_convert_i64(JITCompContext *comp_ctx,
                                        JITFuncContext *func_ctx, bool sign);

    bool
    wasm_jit_compile_op_f32_demote_f64(JITCompContext *comp_ctx,
                                       JITFuncContext *func_ctx);

    bool
    wasm_jit_compile_op_f64_convert_i32(JITCompContext *comp_ctx,
                                        JITFuncContext *func_ctx, bool sign);

    bool
    wasm_jit_compile_op_f64_convert_i64(JITCompContext *comp_ctx,
                                        JITFuncContext *func_ctx, bool sign);

    bool
    wasm_jit_compile_op_f64_promote_f32(JITCompContext *comp_ctx,
                                        JITFuncContext *func_ctx);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* end of _WASM_JIT_EMIT_CONVERSION_H_ */
