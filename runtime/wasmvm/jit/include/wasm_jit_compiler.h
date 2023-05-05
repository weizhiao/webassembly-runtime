#ifndef _WASM_JIT_COMPILER_H
#define _WASM_JIT_COMPILER_H

#include "wasm_jit.h"
#include "wasm_jit_llvm.h"
#include "wasm_llvm_opcode.h"

typedef enum IntArithmetic
{
    INT_ADD = 0,
    INT_SUB,
    INT_MUL,
    INT_DIV_S,
    INT_DIV_U,
    INT_REM_S,
    INT_REM_U
} IntArithmetic;

#define DROP() func_ctx->value_stack--

#define POP(llvm_value)                             \
    do                                              \
    {                                               \
        JITValue *wasm_jit_value;                   \
        wasm_jit_value = func_ctx->value_stack - 1; \
        llvm_value = wasm_jit_value->value;         \
        func_ctx->value_stack--;                    \
    } while (0)

#define POP_I32(v) POP(v)
#define POP_I64(v) POP(v)
#define POP_F32(v) POP(v)
#define POP_F64(v) POP(v)
#define POP_V128(v) POP(v)
#define POP_FUNCREF(v) POP(v)
#define POP_EXTERNREF(v) POP(v)

#define POP_COND(llvm_value)                                                  \
    do                                                                        \
    {                                                                         \
        JITValue *wasm_jit_value;                                             \
        wasm_jit_value = func_ctx->value_stack - 1;                           \
        if (!(llvm_value =                                                    \
                  LLVMBuildICmp(comp_ctx->builder, LLVMIntNE,                 \
                                wasm_jit_value->value, I32_ZERO, "i1_cond"))) \
        {                                                                     \
            wasm_jit_set_last_error("llvm build trunc failed.");              \
            goto fail;                                                        \
        }                                                                     \
        func_ctx->value_stack--;                                              \
    } while (0)

#define PUSH(llvm_value)                                    \
    do                                                      \
    {                                                       \
        JITValue *wasm_jit_value = func_ctx->value_stack++; \
        wasm_jit_value->value = llvm_value;                 \
    } while (0)

#define PUSH_I32(v) PUSH(v)
#define PUSH_I64(v) PUSH(v)
#define PUSH_F32(v) PUSH(v)
#define PUSH_F64(v) PUSH(v)
#define PUSH_V128(v) PUSH(v)
#define PUSH_COND(v)                                      \
    do                                                    \
    {                                                     \
        LLVMValueRef _llvm_value;                         \
        if (!(_llvm_value =                               \
                  LLVMBuildZExt(comp_ctx->builder, v,     \
                                I32_TYPE, "i1toi32")))    \
        {                                                 \
            wasm_jit_set_last_error("invalid WASM stack " \
                                    "data type.");        \
            goto fail;                                    \
        }                                                 \
        PUSH(_llvm_value);                                \
    } while (0)
#define PUSH_FUNCREF(v) PUSH(v)
#define PUSH_EXTERNREF(v) PUSH(v)

#define TO_LLVM_TYPE(wasm_type) \
    wasm_type_to_llvm_type(&comp_ctx->basic_types, wasm_type)

#define I32_TYPE comp_ctx->basic_types.int32_type
#define I64_TYPE comp_ctx->basic_types.int64_type
#define F32_TYPE comp_ctx->basic_types.float32_type
#define F64_TYPE comp_ctx->basic_types.float64_type
#define VOID_TYPE comp_ctx->basic_types.void_type
#define INT1_TYPE comp_ctx->basic_types.int1_type
#define INT8_TYPE comp_ctx->basic_types.int8_type
#define INT16_TYPE comp_ctx->basic_types.int16_type
#define MD_TYPE comp_ctx->basic_types.meta_data_type
#define INT8_TYPE_PTR comp_ctx->basic_types.int8_ptr_type
#define INT8_PPTR_TYPE comp_ctx->basic_types.int8_pptr_type
#define INT16_TYPE_PTR comp_ctx->basic_types.int16_ptr_type
#define I32_TYPE_PTR comp_ctx->basic_types.int32_ptr_type
#define I64_TYPE_PTR comp_ctx->basic_types.int64_ptr_type
#define F32_TYPE_PTR comp_ctx->basic_types.float32_ptr_type
#define F64_TYPE_PTR comp_ctx->basic_types.float64_ptr_type

#define I32_CONST(v) LLVMConstInt(I32_TYPE, v, true)
#define I64_CONST(v) LLVMConstInt(I64_TYPE, v, true)
#define F32_CONST(v) LLVMConstReal(F32_TYPE, v)
#define F64_CONST(v) LLVMConstReal(F64_TYPE, v)
#define I8_CONST(v) LLVMConstInt(INT8_TYPE, v, true)

#define LLVM_CONST(name) (comp_ctx->llvm_consts.name)
#define I8_ZERO LLVM_CONST(i8_zero)
#define I32_ZERO LLVM_CONST(i32_zero)
#define I64_ZERO LLVM_CONST(i64_zero)
#define F32_ZERO LLVM_CONST(f32_zero)
#define F64_ZERO LLVM_CONST(f64_zero)
#define I32_ONE LLVM_CONST(i32_one)
#define I32_TWO LLVM_CONST(i32_two)
#define I32_THREE LLVM_CONST(i32_three)
#define I32_FOUR LLVM_CONST(i32_four)
#define I32_FIVE LLVM_CONST(i32_five)
#define I32_SIX LLVM_CONST(i32_six)
#define I32_SEVEN LLVM_CONST(i32_seven)
#define I32_EIGHT LLVM_CONST(i32_eight)
#define I32_NINE LLVM_CONST(i32_nine)
#define I32_NEG_ONE LLVM_CONST(i32_neg_one)
#define I64_NEG_ONE LLVM_CONST(i64_neg_one)
#define I32_MIN LLVM_CONST(i32_min)
#define I64_MIN LLVM_CONST(i64_min)
#define I32_31 LLVM_CONST(i32_31)
#define I32_32 LLVM_CONST(i32_32)
#define I64_63 LLVM_CONST(i64_63)
#define I64_64 LLVM_CONST(i64_64)
#define REF_NULL I32_NEG_ONE

#define CHECK_LLVM_CONST(v)                                       \
    do                                                            \
    {                                                             \
        if (!v)                                                   \
        {                                                         \
            wasm_jit_set_last_error("create llvm const failed."); \
            goto fail;                                            \
        }                                                         \
    } while (0)

#define GET_WASM_JIT_FUNCTION(name, argc)                                                                       \
    do                                                                                                          \
    {                                                                                                           \
        if (!(func_type =                                                                                       \
                  LLVMFunctionType(ret_type, param_types, argc, false)))                                        \
        {                                                                                                       \
            wasm_jit_set_last_error("llvm add function type failed.");                                          \
            goto fail;                                                                                          \
        }                                                                                                       \
                                                                                                                \
        /* JIT mode, call the function directly */                                                              \
        if (!(func_ptr_type = LLVMPointerType(func_type, 0)))                                                   \
        {                                                                                                       \
            wasm_jit_set_last_error("llvm add pointer type failed.");                                           \
            goto fail;                                                                                          \
        }                                                                                                       \
        if (!(value = I64_CONST((uint64)(uintptr_t)name)) || !(func = LLVMConstIntToPtr(value, func_ptr_type))) \
        {                                                                                                       \
            wasm_jit_set_last_error("create LLVM value failed.");                                               \
            goto fail;                                                                                          \
        }                                                                                                       \
    } while (0)

bool wasm_jit_emit_llvm_file(JITCompContext *comp_ctx, const char *file_name);

bool wasm_jit_emit_WASM_JIT_file(JITCompContext *comp_ctx, WASMModule *wasm_module,
                                 const char *file_name);

#endif /* end of _WASM_JIT_COMPILER_H_ */
