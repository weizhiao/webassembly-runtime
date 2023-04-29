#include "wasm_jit_emit_exception.h"
#include "wasm_exception.h"

static const char *exception_msgs[] = {
    "unreachable",                             /* EXCE_UNREACHABLE */
    "allocate memory failed",                  /* EXCE_OUT_OF_MEMORY */
    "out of bounds memory access",             /* EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS */
    "integer overflow",                        /* EXCE_INTEGER_OVERFLOW */
    "integer divide by zero",                  /* EXCE_INTEGER_DIVIDE_BY_ZERO */
    "invalid conversion to integer",           /* EXCE_INVALID_CONVERSION_TO_INTEGER */
    "indirect call type mismatch",             /* EXCE_INVALID_FUNCTION_TYPE_INDEX */
    "invalid function index",                  /* EXCE_INVALID_FUNCTION_INDEX */
    "undefined element",                       /* EXCE_UNDEFINED_ELEMENT */
    "uninitialized element",                   /* EXCE_UNINITIALIZED_ELEMENT */
    "failed to call unlinked import function", /* EXCE_CALL_UNLINKED_IMPORT_FUNC */
    "native stack overflow",                   /* EXCE_NATIVE_STACK_OVERFLOW */
    "unaligned atomic",                        /* EXCE_UNALIGNED_ATOMIC */
    "wasm auxiliary stack overflow",           /* EXCE_AUX_STACK_OVERFLOW */
    "wasm auxiliary stack underflow",          /* EXCE_AUX_STACK_UNDERFLOW */
    "out of bounds table access",              /* EXCE_OUT_OF_BOUNDS_TABLE_ACCESS */
    "wasm operand stack overflow",             /* EXCE_OPERAND_STACK_OVERFLOW */
    "failed to compile fast jit function",     /* EXCE_FAILED_TO_COMPILE_FAST_JIT_FUNC */
    "",                                        /* EXCE_ALREADY_THROWN */
};

void wasm_set_exception_with_id(WASMModule *module_inst, uint32 id)
{
    if (id < EXCE_NUM)
        wasm_set_exception(module_inst, exception_msgs[id]);
    else
        wasm_set_exception(module_inst, "unknown exception");
}

void jit_set_exception_with_id(WASMModule *module_inst, uint32 id)
{
    if (id != EXCE_ALREADY_THROWN)
        wasm_set_exception_with_id(module_inst, id);
#ifdef OS_ENABLE_HW_BOUND_CHECK
    wasm_runtime_access_exce_check_guard_page();
#endif
}

bool wasm_jit_emit_exception(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                             int32 exception_id, bool is_cond_br, LLVMValueRef cond_br_if,
                             LLVMBasicBlockRef cond_br_else_block)
{
    LLVMBasicBlockRef block_curr = LLVMGetInsertBlock(comp_ctx->builder);
    LLVMValueRef exce_id = I32_CONST((uint32)exception_id), func_const, func;
    LLVMTypeRef param_types[2], ret_type, func_type, func_ptr_type;
    LLVMValueRef param_values[2];

    CHECK_LLVM_CONST(exce_id);

    /* Create got_exception block if needed */
    if (!func_ctx->got_exception_block)
    {
        if (!(func_ctx->got_exception_block = LLVMAppendBasicBlockInContext(
                  comp_ctx->context, func_ctx->func, "got_exception")))
        {
            wasm_jit_set_last_error("add LLVM basic block failed.");
            return false;
        }

        LLVMPositionBuilderAtEnd(comp_ctx->builder,
                                 func_ctx->got_exception_block);

        /* Create exection id phi */
        if (!(func_ctx->exception_id_phi = LLVMBuildPhi(
                  comp_ctx->builder, I32_TYPE, "exception_id_phi")))
        {
            wasm_jit_set_last_error("llvm build phi failed.");
            return false;
        }

        /* Call wasm_jit_set_exception_with_id() to throw exception */
        param_types[0] = INT8_PTR_TYPE;
        param_types[1] = I32_TYPE;
        ret_type = VOID_TYPE;

        /* Create function type */
        if (!(func_type = LLVMFunctionType(ret_type, param_types, 2, false)))
        {
            wasm_jit_set_last_error("create LLVM function type failed.");
            return false;
        }

        /* Create function type */
        if (!(func_ptr_type = LLVMPointerType(func_type, 0)))
        {
            wasm_jit_set_last_error("create LLVM function type failed.");
            return false;
        }
        /* Create LLVM function with const function pointer */
        if (!(func_const =
                  I64_CONST((uint64)(uintptr_t)jit_set_exception_with_id)) ||
            !(func = LLVMConstIntToPtr(func_const, func_ptr_type)))
        {
            wasm_jit_set_last_error("create LLVM value failed.");
            return false;
        }

        /* Call the wasm_jit_set_exception_with_id() function */
        param_values[0] = func_ctx->wasm_module;
        param_values[1] = func_ctx->exception_id_phi;
        if (!LLVMBuildCall2(comp_ctx->builder, func_type, func, param_values, 2,
                            ""))
        {
            wasm_jit_set_last_error("llvm build call failed.");
            return false;
        }

        /* Create return IR */
        WASMType *wasm_jit_func_type = func_ctx->wasm_func->func_type;
        if (!wasm_jit_build_zero_function_ret(comp_ctx, func_ctx, wasm_jit_func_type))
        {
            return false;
        }

        /* Resume the builder position */
        LLVMPositionBuilderAtEnd(comp_ctx->builder, block_curr);
    }

    /* Add phi incoming value to got_exception block */
    LLVMAddIncoming(func_ctx->exception_id_phi, &exce_id, &block_curr, 1);

    if (!is_cond_br)
    {
        /* not condition br, create br IR */
        if (!LLVMBuildBr(comp_ctx->builder, func_ctx->got_exception_block))
        {
            wasm_jit_set_last_error("llvm build br failed.");
            return false;
        }
    }
    else
    {
        /* Create condition br */
        if (!LLVMBuildCondBr(comp_ctx->builder, cond_br_if,
                             func_ctx->got_exception_block,
                             cond_br_else_block))
        {
            wasm_jit_set_last_error("llvm build cond br failed.");
            return false;
        }
        /* Start to translate the else block */
        LLVMPositionBuilderAtEnd(comp_ctx->builder, cond_br_else_block);
    }

    return true;
fail:
    return false;
}
