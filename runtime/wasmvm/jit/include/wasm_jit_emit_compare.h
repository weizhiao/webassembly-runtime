/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _wasm_jit_EMIT_COMPARE_H_
#define _wasm_jit_EMIT_COMPARE_H_

#include "wasm_jit_compiler.h"

#ifdef __cplusplus
extern "C" {
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

#endif /* end of _wasm_jit_EMIT_COMPARE_H_ */
