#ifndef _WASM_JIT_EMIT_NUMBERIC_H_
#define _WASM_JIT_EMIT_NUMBERIC_H_

#include "wasm_jit_compiler.h"

bool wasm_jit_compile_op_i32_clz(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

bool wasm_jit_compile_op_i32_ctz(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

bool wasm_jit_compile_op_i32_popcnt(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

bool wasm_jit_compile_op_i64_clz(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

bool wasm_jit_compile_op_i64_ctz(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

bool wasm_jit_compile_op_i64_popcnt(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

bool wasm_jit_compile_op_i32_arithmetic(JITCompContext *comp_ctx,
                                        JITFuncContext *func_ctx, IntArithmetic arith_op,
                                        uint8 **p_frame_ip);

LLVMValueRef
compile_op_float_min_max(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                         bool is_f32, LLVMValueRef left, LLVMValueRef right,
                         bool is_min);

LLVMValueRef
call_llvm_float_math_intrinsic(JITCompContext *comp_ctx,
                               JITFuncContext *func_ctx, bool is_f32,
                               const char *intrinsic, ...);

bool wasm_jit_compile_op_i64_arithmetic(JITCompContext *comp_ctx,
                                        JITFuncContext *func_ctx, IntArithmetic arith_op,
                                        uint8 **p_frame_ip);

#if WASM_ENABLE_FPU != 0
LLVMValueRef
call_llvm_float_experimental_constrained_intrinsic(JITCompContext *comp_ctx,
                                                   JITFuncContext *func_ctx,
                                                   bool is_f32,
                                                   const char *intrinsic, ...);
LLVMValueRef
call_llvm_libm_experimental_constrained_intrinsic(JITCompContext *comp_ctx,
                                                  JITFuncContext *func_ctx,
                                                  bool is_f32,
                                                  const char *intrinsic, ...);
#endif

#endif
