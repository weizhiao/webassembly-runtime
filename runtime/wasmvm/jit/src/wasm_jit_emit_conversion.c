#include "wasm_jit_emit_conversion.h"
#include "wasm_jit_emit_exception.h"
#include "wasm_jit_emit_numberic.h"

static bool
trunc_float_to_int(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                   LLVMValueRef operand, LLVMTypeRef src_type,
                   LLVMTypeRef dest_type, LLVMValueRef min_value,
                   LLVMValueRef max_value, char *name, bool sign)
{
    LLVMBasicBlockRef check_nan_succ, check_overflow_succ;
    LLVMValueRef is_less, is_greater, res;

    LLVMOPFCmp(res, LLVMRealUNO, operand, operand, "fcmp_is_nan");

    if (!(check_nan_succ = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func_ctx->func, "check_nan_succ")))
    {
        wasm_jit_set_last_error("llvm add basic block failed.");
        goto fail;
    }

    LLVMMoveBasicBlockAfter(check_nan_succ,
                            LLVMGetInsertBlock(comp_ctx->builder));

    if (!(wasm_jit_emit_exception(comp_ctx, func_ctx,
                                  EXCE_INVALID_CONVERSION_TO_INTEGER, true, res,
                                  check_nan_succ)))
        goto fail;

    LLVMOPFCmp(is_less, LLVMRealOLE, operand, min_value, "fcmp_min_value");
    LLVMOPFCmp(is_greater, LLVMRealOGE, operand, max_value, "fcmp_max_value");
    LLVMOPOr(res, is_less, is_greater, "is_overflow");

    if (!(check_overflow_succ = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func_ctx->func, "check_overflow_succ")))
    {
        wasm_jit_set_last_error("llvm add basic block failed.");
        goto fail;
    }

    LLVMMoveBasicBlockAfter(check_overflow_succ,
                            LLVMGetInsertBlock(comp_ctx->builder));

    if (!(wasm_jit_emit_exception(comp_ctx, func_ctx, EXCE_INTEGER_OVERFLOW, true,
                                  res, check_overflow_succ)))
        goto fail;

    if (sign)
        LLVMOPFPToSI(operand, dest_type, name);
    else
        LLVMOPFPToUI(operand, dest_type, name);

    PUSH(operand);
    return true;
fail:
    return false;
}

#define ADD_BASIC_BLOCK(block, name)                                           \
    do                                                                         \
    {                                                                          \
        if (!(block = LLVMAppendBasicBlockInContext(comp_ctx->context,         \
                                                    func_ctx->func, name)))    \
        {                                                                      \
            wasm_jit_set_last_error("llvm add basic block failed.");           \
            goto fail;                                                         \
        }                                                                      \
                                                                               \
        LLVMMoveBasicBlockAfter(block, LLVMGetInsertBlock(comp_ctx->builder)); \
    } while (0)

static bool
trunc_sat_float_to_int(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                       LLVMValueRef operand, LLVMTypeRef src_type,
                       LLVMTypeRef dest_type, LLVMValueRef min_value,
                       LLVMValueRef max_value, char *name, bool sign)
{
    LLVMBasicBlockRef check_nan_succ, check_less_succ, check_greater_succ;
    LLVMBasicBlockRef is_nan_block, is_less_block, is_greater_block, res_block;
    LLVMValueRef is_less, is_greater, res, phi;
    LLVMValueRef zero = (dest_type == I32_TYPE) ? I32_ZERO : I64_ZERO;
    LLVMValueRef vmin, vmax;

    LLVMOPFCmp(res, LLVMRealUNO, operand, operand, "fcmp_is_nan");

    ADD_BASIC_BLOCK(check_nan_succ, "check_nan_succ");
    ADD_BASIC_BLOCK(is_nan_block, "is_nan_block");
    ADD_BASIC_BLOCK(check_less_succ, "check_less_succ");
    ADD_BASIC_BLOCK(is_less_block, "is_less_block");
    ADD_BASIC_BLOCK(check_greater_succ, "check_greater_succ");
    ADD_BASIC_BLOCK(is_greater_block, "is_greater_block");
    ADD_BASIC_BLOCK(res_block, "res_block");

    if (!LLVMBuildCondBr(comp_ctx->builder, res, is_nan_block,
                         check_nan_succ))
    {
        wasm_jit_set_last_error("llvm build cond br failed.");
        goto fail;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, is_nan_block);
    if (!LLVMBuildBr(comp_ctx->builder, res_block))
    {
        wasm_jit_set_last_error("llvm build br failed.");
        goto fail;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, check_nan_succ);
    LLVMOPFCmp(is_less, LLVMRealOLE, operand, min_value, "fcmp_min_value");
    if (!LLVMBuildCondBr(comp_ctx->builder, is_less, is_less_block,
                         check_less_succ))
    {
        wasm_jit_set_last_error("llvm build cond br failed.");
        goto fail;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, is_less_block);
    if (!LLVMBuildBr(comp_ctx->builder, res_block))
    {
        wasm_jit_set_last_error("llvm build br failed.");
        goto fail;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, check_less_succ);
    LLVMOPFCmp(is_greater, LLVMRealOGE, operand, max_value, "fcmp_max_value");
    if (!LLVMBuildCondBr(comp_ctx->builder, is_greater, is_greater_block,
                         check_greater_succ))
    {
        wasm_jit_set_last_error("llvm build cond br failed.");
        goto fail;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, is_greater_block);
    if (!LLVMBuildBr(comp_ctx->builder, res_block))
    {
        wasm_jit_set_last_error("llvm build br failed.");
        goto fail;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, check_greater_succ);

    char intrinsic[128];

    snprintf(intrinsic, sizeof(intrinsic), "i%d_trunc_f%d_%c",
             LLVMGetIntTypeWidth(dest_type),
             LLVMGetTypeKind(src_type) == LLVMFloatTypeKind ? 32 : 64,
             sign ? 's' : 'u');

    if (sign)
        LLVMOPFPToSI(operand, dest_type, name);
    else
        LLVMOPFPToUI(operand, dest_type, name);

    if (!LLVMBuildBr(comp_ctx->builder, res_block))
    {
        wasm_jit_set_last_error("llvm build br failed.");
        goto fail;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, res_block);
    if (!(phi = LLVMBuildPhi(comp_ctx->builder, dest_type,
                             "trunc_sat_result_phi")))
    {
        wasm_jit_set_last_error("llvm build phi failed.");
        return false;
    }

    if (dest_type == I32_TYPE)
    {
        if (sign)
        {
            vmin = I32_CONST(INT32_MIN);
            vmax = I32_CONST(INT32_MAX);
        }
        else
        {
            vmin = I32_CONST(0);
            vmax = I32_CONST(UINT32_MAX);
        }
    }
    else if (dest_type == I64_TYPE)
    {
        if (sign)
        {
            vmin = I64_CONST(INT64_MIN);
            vmax = I64_CONST(INT64_MAX);
        }
        else
        {
            vmin = I64_CONST(0);
            vmax = I64_CONST(UINT64_MAX);
        }
    }
    LLVMAddIncoming(phi, &zero, &is_nan_block, 1);
    LLVMAddIncoming(phi, &vmin, &is_less_block, 1);
    LLVMAddIncoming(phi, &vmax, &is_greater_block, 1);
    LLVMAddIncoming(phi, &operand, &check_greater_succ, 1);

    PUSH(phi);
    return true;
fail:
    return false;
}

bool wasm_jit_compile_op_i32_trunc_f32(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                       bool sign, bool saturating)
{
    LLVMValueRef value;
    LLVMValueRef min_value, max_value;

    POP_F32(value);

    if (sign)
    {
        min_value = F32_CONST(-2147483904.0f);
        max_value = F32_CONST(2147483648.0f);
    }
    else
    {
        min_value = F32_CONST(-1.0f);
        max_value = F32_CONST(4294967296.0f);
    }

    CHECK_LLVM_CONST(min_value);
    CHECK_LLVM_CONST(max_value);

    if (!saturating)
        return trunc_float_to_int(
            comp_ctx, func_ctx, value, F32_TYPE, I32_TYPE, min_value, max_value,
            sign ? "i32_trunc_f32_s" : "i32_trunc_f32_u", sign);
    else
        return trunc_sat_float_to_int(
            comp_ctx, func_ctx, value, F32_TYPE, I32_TYPE, min_value, max_value,
            sign ? "i32_trunc_sat_f32_s" : "i32_trunc_sat_f32_u", sign);
fail:
    return false;
}

bool wasm_jit_compile_op_i32_trunc_f64(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                       bool sign, bool saturating)
{
    LLVMValueRef value;
    LLVMValueRef min_value, max_value;

    POP_F64(value);

    if (sign)
    {
        min_value = F64_CONST(-2147483649.0);
        max_value = F64_CONST(2147483648.0);
    }
    else
    {
        min_value = F64_CONST(-1.0);
        max_value = F64_CONST(4294967296.0);
    }

    CHECK_LLVM_CONST(min_value);
    CHECK_LLVM_CONST(max_value);

    if (!saturating)
        return trunc_float_to_int(
            comp_ctx, func_ctx, value, F64_TYPE, I32_TYPE, min_value, max_value,
            sign ? "i32_trunc_f64_s" : "i32_trunc_f64_u", sign);
    else
        return trunc_sat_float_to_int(
            comp_ctx, func_ctx, value, F64_TYPE, I32_TYPE, min_value, max_value,
            sign ? "i32_trunc_sat_f64_s" : "i32_trunc_sat_f64_u", sign);
fail:
    return false;
}

bool wasm_jit_compile_op_i64_trunc_f32(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                       bool sign, bool saturating)
{
    LLVMValueRef value;
    LLVMValueRef min_value, max_value;

    POP_F32(value);

    if (sign)
    {
        min_value = F32_CONST(-9223373136366403584.0f);
        max_value = F32_CONST(9223372036854775808.0f);
    }
    else
    {
        min_value = F32_CONST(-1.0f);
        max_value = F32_CONST(18446744073709551616.0f);
    }

    if (!saturating)
        return trunc_float_to_int(
            comp_ctx, func_ctx, value, F32_TYPE, I64_TYPE, min_value, max_value,
            sign ? "i64_trunc_f32_s" : "i64_trunc_f32_u", sign);
    else
        return trunc_sat_float_to_int(
            comp_ctx, func_ctx, value, F32_TYPE, I64_TYPE, min_value, max_value,
            sign ? "i64_trunc_sat_f32_s" : "i64_trunc_sat_f32_u", sign);
}

bool wasm_jit_compile_op_i64_trunc_f64(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                       bool sign, bool saturating)
{
    LLVMValueRef value;
    LLVMValueRef min_value, max_value;

    POP_F64(value);

    if (sign)
    {
        min_value = F64_CONST(-9223372036854777856.0);
        max_value = F64_CONST(9223372036854775808.0);
    }
    else
    {
        min_value = F64_CONST(-1.0);
        max_value = F64_CONST(18446744073709551616.0);
    }

    if (!saturating)
        return trunc_float_to_int(
            comp_ctx, func_ctx, value, F64_TYPE, I64_TYPE, min_value, max_value,
            sign ? "i64_trunc_f64_s" : "i64_trunc_f64_u", sign);
    else
        return trunc_sat_float_to_int(
            comp_ctx, func_ctx, value, F64_TYPE, I64_TYPE, min_value, max_value,
            sign ? "i64_trunc_sat_f64_s" : "i64_trunc_sat_f64_u", sign);

fail:
    return false;
}