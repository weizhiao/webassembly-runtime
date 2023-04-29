#ifndef _wasm_jit_EMIT_FUNCTION_H_
#define _wasm_jit_EMIT_FUNCTION_H_

#include "wasm_jit_compiler.h"

bool wasm_jit_compile_op_call(WASMModule *wasm_module, JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                              uint32 func_idx);

bool wasm_jit_compile_op_call_indirect(WASMModule *wasm_module, JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                       uint32 type_idx, uint32 tbl_idx);

bool wasm_jit_compile_op_ref_null(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

bool wasm_jit_compile_op_ref_is_null(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

bool wasm_jit_compile_op_ref_func(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 func_idx);

#endif /* end of _wasm_jit_EMIT_FUNCTION_H_ */
