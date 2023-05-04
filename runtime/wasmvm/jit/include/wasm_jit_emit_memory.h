#ifndef _WASM_JIT_EMIT_MEMORY_H_
#define _WASM_JIT_EMIT_MEMORY_H_

#include "wasm_jit_compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

    bool
    wasm_jit_compile_op_i32_load(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                 uint32 align, uint32 offset, uint32 bytes, bool sign,
                                 bool atomic);

    bool
    wasm_jit_compile_op_i64_load(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                 uint32 align, uint32 offset, uint32 bytes, bool sign,
                                 bool atomic);

    bool
    wasm_jit_compile_op_f32_load(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                 uint32 align, uint32 offset);

    bool
    wasm_jit_compile_op_f64_load(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                 uint32 align, uint32 offset);

    bool
    wasm_jit_compile_op_i32_store(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 align, uint32 offset, uint32 bytes,
                                  bool atomic);

    bool
    wasm_jit_compile_op_i64_store(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 align, uint32 offset, uint32 bytes,
                                  bool atomic);

    bool
    wasm_jit_compile_op_f32_store(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 align, uint32 offset);

    bool
    wasm_jit_compile_op_f64_store(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 align, uint32 offset);

    LLVMValueRef
    wasm_jit_check_memory_overflow(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                   uint32 offset, uint32 bytes);

    bool
    wasm_jit_compile_op_memory_size(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

    bool
    wasm_jit_compile_op_memory_grow(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

    bool
    wasm_jit_compile_op_memory_init(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                    uint32 seg_index);

    bool
    wasm_jit_compile_op_data_drop(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 seg_index);

    bool
    wasm_jit_compile_op_memory_copy(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

    bool
    wasm_jit_compile_op_memory_fill(JITCompContext *comp_ctx, JITFuncContext *func_ctx);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif
