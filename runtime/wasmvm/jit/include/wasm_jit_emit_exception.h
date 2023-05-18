#ifndef _WASM_JIT_EMIT_EXCEPTION_H_
#define _WASM_JIT_EMIT_EXCEPTION_H_

#include "wasm_jit_compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

    bool
    wasm_jit_emit_exception(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                            int32 exception_id, bool is_cond_br, LLVMValueRef cond_br_if,
                            LLVMBasicBlockRef cond_br_else_block);

#ifdef __cplusplus
}
#endif

#endif
