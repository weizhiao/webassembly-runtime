
/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _wasm_jit_EMIT_TABLE_H_
#define _wasm_jit_EMIT_TABLE_H_

#include "wasm_jit_compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

    bool
    wasm_jit_compile_op_elem_drop(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 tbl_seg_idx);

    bool
    wasm_jit_compile_op_table_get(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 tbl_idx);

    bool
    wasm_jit_compile_op_table_set(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 tbl_idx);

    bool
    wasm_jit_compile_op_table_init(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                   uint32 tbl_idx, uint32 tbl_seg_idx);

    bool
    wasm_jit_compile_op_table_copy(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                   uint32 src_tbl_idx, uint32 dst_tbl_idx);

    bool
    wasm_jit_compile_op_table_size(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                   uint32 tbl_idx);

    bool
    wasm_jit_compile_op_table_grow(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                   uint32 tbl_idx);

    bool
    wasm_jit_compile_op_table_fill(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                   uint32 tbl_idx);

    uint64
    get_tbl_inst_offset(uint32 tbl_idx);

    LLVMValueRef
    wasm_jit_compile_get_tbl_inst(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 tbl_idx);

#ifdef __cplusplus
} /* end of extern "C" */
#endif
#endif