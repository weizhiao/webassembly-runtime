#ifndef _WASM_JIT_EMIT_CONTROL_H_
#define _WASM_JIT_EMIT_CONTROL_H_

#include "wasm_jit_compiler.h"

bool wasm_compile_op_block(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                           WASMBlock *wasm_block, uint32 label_type,
                           uint32 param_count, uint8 *param_types,
                           uint32 result_count, uint8 *result_types);

bool wasm_jit_compile_op_else(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

bool wasm_jit_compile_op_end(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

bool wasm_jit_compile_op_br(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                            uint32 br_depth, uint8 **frame_ip);

bool wasm_jit_compile_op_br_if(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                               uint32 br_depth, uint8 **frame_ip);

bool wasm_jit_compile_op_br_table(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 *br_depths, uint32 br_count, uint8 **frame_ip);

bool wasm_jit_compile_op_return(JITCompContext *comp_ctx, JITFuncContext *func_ctx, uint8 **frame_ip);

bool wasm_jit_compile_op_unreachable(JITCompContext *comp_ctx, JITFuncContext *func_ctx, uint8 **frame_ip);

#endif /* end of _WASM_JIT_EMIT_CONTROL_H_ */
