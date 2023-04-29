/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _wasm_jit_EMIT_EXCEPTION_H_
#define _wasm_jit_EMIT_EXCEPTION_H_

#include "wasm_jit_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

bool
wasm_jit_emit_exception(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                   int32 exception_id, bool is_cond_br, LLVMValueRef cond_br_if,
                   LLVMBasicBlockRef cond_br_else_block);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* end of _wasm_jit_EMIT_EXCEPTION_H_ */
