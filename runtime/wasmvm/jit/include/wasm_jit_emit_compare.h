#ifndef _WASM_JIT_EMIT_COMPARE_H_
#define _WASM_JIT_EMIT_COMPARE_H_

#include "wasm_jit_compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

    bool
    wasm_jit_compile_op_i32_compare(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                    IntCond cond);

    bool
    wasm_jit_compile_op_i64_compare(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                    IntCond cond);

    bool
    wasm_jit_compile_op_f32_compare(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                    FloatCond cond);

    bool
    wasm_jit_compile_op_f64_compare(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                    FloatCond cond);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* end of _WASM_JIT_EMIT_COMPARE_H_ */
