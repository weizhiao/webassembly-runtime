#ifndef _WASM_JIT_EMIT_NUMBERIC_H_
#define _WASM_JIT_EMIT_NUMBERIC_H_

#include "wasm_jit_compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

    bool
    wasm_jit_compile_op_i32_clz(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

    bool
    wasm_jit_compile_op_i32_ctz(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

    bool
    wasm_jit_compile_op_i32_popcnt(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

    bool
    wasm_jit_compile_op_i64_clz(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

    bool
    wasm_jit_compile_op_i64_ctz(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

    bool
    wasm_jit_compile_op_i64_popcnt(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

    bool
    wasm_jit_compile_op_i32_arithmetic(JITCompContext *comp_ctx,
                                       JITFuncContext *func_ctx, IntArithmetic arith_op,
                                       uint8 **p_frame_ip);

    bool
    wasm_jit_compile_op_i64_arithmetic(JITCompContext *comp_ctx,
                                       JITFuncContext *func_ctx, IntArithmetic arith_op,
                                       uint8 **p_frame_ip);

    bool
    wasm_jit_compile_op_i32_bitwise(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                    IntBitwise bitwise_op);

    bool
    wasm_jit_compile_op_i64_bitwise(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                    IntBitwise bitwise_op);

    bool
    wasm_jit_compile_op_i32_shift(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  IntShift shift_op);

    bool
    wasm_jit_compile_op_i64_shift(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  IntShift shift_op);

    bool
    wasm_jit_compile_op_f32_math(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                 FloatMath math_op);

    bool
    wasm_jit_compile_op_f64_math(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                 FloatMath math_op);

    bool
    wasm_jit_compile_op_f32_arithmetic(JITCompContext *comp_ctx,
                                       JITFuncContext *func_ctx,
                                       FloatArithmetic arith_op);

    bool
    wasm_jit_compile_op_f64_arithmetic(JITCompContext *comp_ctx,
                                       JITFuncContext *func_ctx,
                                       FloatArithmetic arith_op);

    bool
    wasm_jit_compile_op_f32_copysign(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

    bool
    wasm_jit_compile_op_f64_copysign(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* end of _WASM_JIT_EMIT_NUMBERIC_H_ */
