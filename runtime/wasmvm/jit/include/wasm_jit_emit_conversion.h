/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _wasm_jit_EMIT_CONVERSION_H_
#define _wasm_jit_EMIT_CONVERSION_H_

#include "wasm_jit_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

bool
wasm_jit_compile_op_i32_wrap_i64(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

bool
wasm_jit_compile_op_i32_trunc_f32(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                             bool sign, bool saturating);

bool
wasm_jit_compile_op_i32_trunc_f64(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                             bool sign, bool saturating);

bool
wasm_jit_compile_op_i64_extend_i32(JITCompContext *comp_ctx,
                              JITFuncContext *func_ctx, bool sign);

bool
wasm_jit_compile_op_i64_extend_i64(JITCompContext *comp_ctx,
                              JITFuncContext *func_ctx, int8 bitwidth);

bool
wasm_jit_compile_op_i32_extend_i32(JITCompContext *comp_ctx,
                              JITFuncContext *func_ctx, int8 bitwidth);

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

bool
wasm_jit_compile_op_i64_reinterpret_f64(JITCompContext *comp_ctx,
                                   JITFuncContext *func_ctx);

bool
wasm_jit_compile_op_i32_reinterpret_f32(JITCompContext *comp_ctx,
                                   JITFuncContext *func_ctx);

bool
wasm_jit_compile_op_f64_reinterpret_i64(JITCompContext *comp_ctx,
                                   JITFuncContext *func_ctx);

bool
wasm_jit_compile_op_f32_reinterpret_i32(JITCompContext *comp_ctx,
                                   JITFuncContext *func_ctx);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* end of _wasm_jit_EMIT_CONVERSION_H_ */
