#include "wasm_jit_emit_numberic.h"
#include "wasm_jit_emit_exception.h"
#include "wasm_jit_emit_control.h"

#include <stdarg.h>

#define LLVM_BUILD_ICMP(op, left, right, res, name)                         \
    do                                                                      \
    {                                                                       \
        if (!(res =                                                         \
                  LLVMBuildICmp(comp_ctx->builder, op, left, right, name))) \
        {                                                                   \
            wasm_jit_set_last_error("llvm build " name " fail.");           \
            return false;                                                   \
        }                                                                   \
    } while (0)

#define LLVM_BUILD_OP_OR_INTRINSIC(Op, left, right, res, intrinsic, name, \
                                   err_ret)                               \
    do                                                                    \
    {                                                                     \
                                                                          \
        LLVM_OP_TEMPLATE(Op, left, right, res, name);                     \
                                                                          \
    } while (0)

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

#define CHECK_INT_OVERFLOW(type)                                       \
    do                                                                 \
    {                                                                  \
        LLVMValueRef cmp_min_int, cmp_neg_one;                         \
        LLVM_BUILD_ICMP(LLVMIntEQ, left, type##_MIN, cmp_min_int,      \
                        "cmp_min_int");                                \
        LLVM_BUILD_ICMP(LLVMIntEQ, right, type##_NEG_ONE, cmp_neg_one, \
                        "cmp_neg_one");                                \
        LLVMOPAnd(cmp_min_int, cmp_neg_one, overflow, "overflow");     \
    } while (0)

#define PUSH_INT(v) PUSH(v)

#define POP_INT(v) POP(v)

#define DEF_INT_UNARY_OP(op, err)             \
    do                                        \
    {                                         \
        LLVMValueRef res, operand;            \
        POP_INT(operand);                     \
        if (!(res = op))                      \
        {                                     \
            if (err)                          \
                wasm_jit_set_last_error(err); \
            return false;                     \
        }                                     \
        PUSH_INT(res);                        \
    } while (0)

#if WASM_ENABLE_FPU != 0
LLVMValueRef
call_llvm_float_experimental_constrained_intrinsic(JITCompContext *comp_ctx,
                                                   JITFuncContext *func_ctx,
                                                   bool is_f32,
                                                   const char *intrinsic, ...)
{
    va_list param_value_list;
    LLVMValueRef ret;
    LLVMTypeRef param_types[4], ret_type = is_f32 ? F32_TYPE : F64_TYPE;
    int param_count = 4;

    param_types[0] = param_types[1] = ret_type;
    param_types[2] = param_types[3] = MD_TYPE;

    va_start(param_value_list, intrinsic);

    ret = wasm_jit_call_llvm_intrinsic_v(comp_ctx, func_ctx, intrinsic, ret_type,
                                         param_types, param_count, param_value_list);

    va_end(param_value_list);

    return ret;
}

LLVMValueRef
call_llvm_libm_experimental_constrained_intrinsic(JITCompContext *comp_ctx,
                                                  JITFuncContext *func_ctx,
                                                  bool is_f32,
                                                  const char *intrinsic, ...)
{
    va_list param_value_list;
    LLVMValueRef ret;
    LLVMTypeRef param_types[3], ret_type = is_f32 ? F32_TYPE : F64_TYPE;

    param_types[0] = ret_type;
    param_types[1] = param_types[2] = MD_TYPE;

    va_start(param_value_list, intrinsic);

    ret = wasm_jit_call_llvm_intrinsic_v(comp_ctx, func_ctx, intrinsic, ret_type,
                                         param_types, 3, param_value_list);

    va_end(param_value_list);

    return ret;
}

#endif

LLVMValueRef
compile_op_float_min_max(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                         bool is_f32, LLVMValueRef left, LLVMValueRef right,
                         bool is_min)
{
    LLVMTypeRef param_types[2], ret_type = is_f32 ? F32_TYPE : F64_TYPE,
                                int_type = is_f32 ? I32_TYPE : I64_TYPE;
    LLVMValueRef cmp, is_eq, is_nan, ret, left_int, right_int, tmp,
        nan = LLVMConstRealOfString(ret_type, "NaN");
    char *intrinsic = is_min ? (is_f32 ? "llvm.minnum.f32" : "llvm.minnum.f64")
                             : (is_f32 ? "llvm.maxnum.f32" : "llvm.maxnum.f64");

    param_types[0] = param_types[1] = ret_type;

    if (!(is_nan = LLVMBuildFCmp(comp_ctx->builder, LLVMRealUNO, left,
                                 right, "is_nan")) ||
        !(is_eq = LLVMBuildFCmp(comp_ctx->builder, LLVMRealOEQ, left,
                                right, "is_eq")))
    {
        wasm_jit_set_last_error("llvm build fcmp fail.");
        return NULL;
    }

    if (!(left_int =
              LLVMBuildBitCast(comp_ctx->builder, left, int_type, "left_int")) ||
        !(right_int = LLVMBuildBitCast(comp_ctx->builder, right, int_type,
                                       "right_int")))
    {
        wasm_jit_set_last_error("llvm build bitcast fail.");
        return NULL;
    }

    if (is_min)
        LLVM_BUILD_OP_OR_INTRINSIC(Or, left_int, right_int, tmp,
                                   is_f32 ? "i32.or" : "i64.or", "tmp_int",
                                   false);
    else
        LLVM_BUILD_OP_OR_INTRINSIC(And, left_int, right_int, tmp,
                                   is_f32 ? "i32.and" : "i64.and", "tmp_int",
                                   false);

    if (!(tmp = LLVMBuildBitCast(comp_ctx->builder, tmp, ret_type, "tmp")))
    {
        wasm_jit_set_last_error("llvm build bitcast fail.");
        return NULL;
    }

    if (!(cmp = wasm_jit_call_llvm_intrinsic(comp_ctx, func_ctx, intrinsic, ret_type,
                                             param_types, 2, left, right)))
        return NULL;

    if (!(cmp = LLVMBuildSelect(comp_ctx->builder, is_eq, tmp, cmp, "cmp")))
    {
        wasm_jit_set_last_error("llvm build select fail.");
        return NULL;
    }

    if (!(ret = LLVMBuildSelect(comp_ctx->builder, is_nan, nan, cmp,
                                is_min ? "min" : "max")))
    {
        wasm_jit_set_last_error("llvm build select fail.");
        return NULL;
    }

    return ret;
fail:
    return NULL;
}

typedef enum BitCountType
{
    CLZ32 = 0,
    CLZ64,
    CTZ32,
    CTZ64,
    POP_CNT32,
    POP_CNT64
} BitCountType;

static char *bit_cnt_llvm_intrinsic[] = {
    "llvm.ctlz.i32",
    "llvm.ctlz.i64",
    "llvm.cttz.i32",
    "llvm.cttz.i64",
    "llvm.ctpop.i32",
    "llvm.ctpop.i64",
};

static bool
wasm_jit_compile_int_bit_count(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                               BitCountType type, bool is_i32)
{
    LLVMValueRef zero_undef;
    LLVMTypeRef ret_type, param_types[2];

    param_types[0] = ret_type = is_i32 ? I32_TYPE : I64_TYPE;
    param_types[1] = LLVMInt1TypeInContext(comp_ctx->context);

    zero_undef = LLVMConstInt(param_types[1], false, true);

    if (type < POP_CNT32)
        DEF_INT_UNARY_OP(wasm_jit_call_llvm_intrinsic(
                             comp_ctx, func_ctx, bit_cnt_llvm_intrinsic[type],
                             ret_type, param_types, 2, operand, zero_undef),
                         NULL);
    else
        DEF_INT_UNARY_OP(wasm_jit_call_llvm_intrinsic(
                             comp_ctx, func_ctx, bit_cnt_llvm_intrinsic[type],
                             ret_type, param_types, 1, operand),
                         NULL);

    return true;
}

static bool
compile_rems(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
             LLVMValueRef left, LLVMValueRef right, LLVMValueRef overflow_cond,
             bool is_i32)
{
    LLVMValueRef phi, no_overflow_value, zero = is_i32 ? I32_ZERO : I64_ZERO;
    LLVMBasicBlockRef block_curr, no_overflow_block, rems_end_block;
    LLVMTypeRef param_types[2];

    param_types[1] = param_types[0] = is_i32 ? I32_TYPE : I64_TYPE;

    block_curr = LLVMGetInsertBlock(comp_ctx->builder);

    ADD_BASIC_BLOCK(rems_end_block, "rems_end");
    ADD_BASIC_BLOCK(no_overflow_block, "rems_no_overflow");

    if (!LLVMBuildCondBr(comp_ctx->builder, overflow_cond, rems_end_block,
                         no_overflow_block))
    {
        wasm_jit_set_last_error("llvm build cond br failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, no_overflow_block);

    LLVM_BUILD_OP_OR_INTRINSIC(SRem, left, right, no_overflow_value,
                               is_i32 ? "i32.rem_s" : "i64.rem_s", "rem_s",
                               false);

    if (!LLVMBuildBr(comp_ctx->builder, rems_end_block))
    {
        wasm_jit_set_last_error("llvm build br failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, rems_end_block);

    if (!(phi = LLVMBuildPhi(comp_ctx->builder, is_i32 ? I32_TYPE : I64_TYPE,
                             "rems_result_phi")))
    {
        wasm_jit_set_last_error("llvm build phi failed.");
        return false;
    }

    LLVMAddIncoming(phi, &no_overflow_value, &no_overflow_block, 1);
    LLVMAddIncoming(phi, &zero, &block_curr, 1);

    PUSH(phi);
    return true;

fail:
    return false;
}

static bool
compile_int_div(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                IntArithmetic arith_op, bool is_i32)
{
    LLVMValueRef left, right, cmp_div_zero, overflow, res;
    LLVMBasicBlockRef check_div_zero_succ, check_overflow_succ;
    LLVMTypeRef param_types[2];

    param_types[1] = param_types[0] = is_i32 ? I32_TYPE : I64_TYPE;

    POP_INT(right);
    POP_INT(left);

    LLVM_BUILD_ICMP(LLVMIntEQ, right, is_i32 ? I32_ZERO : I64_ZERO,
                    cmp_div_zero, "cmp_div_zero");
    ADD_BASIC_BLOCK(check_div_zero_succ, "check_div_zero_success");

    if (!(wasm_jit_emit_exception(comp_ctx, func_ctx,
                                  EXCE_INTEGER_DIVIDE_BY_ZERO, true,
                                  cmp_div_zero, check_div_zero_succ)))
        goto fail;

    switch (arith_op)
    {
    case INT_DIV_S:
        if (is_i32)
            CHECK_INT_OVERFLOW(I32);
        else
            CHECK_INT_OVERFLOW(I64);

        ADD_BASIC_BLOCK(check_overflow_succ, "check_overflow_success");

        if (!(wasm_jit_emit_exception(comp_ctx, func_ctx,
                                      EXCE_INTEGER_OVERFLOW, true, overflow,
                                      check_overflow_succ)))
            goto fail;

        LLVM_BUILD_OP_OR_INTRINSIC(SDiv, left, right, res,
                                   is_i32 ? "i32.div_s" : "i64.div_s",
                                   "div_s", false);
        PUSH_INT(res);
        return true;
    case INT_DIV_U:

        LLVMOPUDiv(left, right, res, "div_u");

        PUSH_INT(res);
        return true;
    case INT_REM_S:
        if (is_i32)
            CHECK_INT_OVERFLOW(I32);
        else
            CHECK_INT_OVERFLOW(I64);
        return compile_rems(comp_ctx, func_ctx, left, right, overflow,
                            is_i32);
    case INT_REM_U:
        LLVM_BUILD_OP_OR_INTRINSIC(URem, left, right, res,
                                   is_i32 ? "i32.rem_u" : "i64.rem_u",
                                   "rem_u", false);
        PUSH_INT(res);
        return true;
    default:;
        return false;
    }

fail:
    return false;
}

static bool
compile_op_int_arithmetic(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                          IntArithmetic arith_op, bool is_i32,
                          uint8 **p_frame_ip)
{
    return compile_int_div(comp_ctx, func_ctx, arith_op, is_i32);
}

LLVMValueRef
call_llvm_float_math_intrinsic(JITCompContext *comp_ctx,
                               JITFuncContext *func_ctx, bool is_f32,
                               const char *intrinsic, ...)
{
    va_list param_value_list;
    LLVMValueRef ret;
    LLVMTypeRef param_type, ret_type = is_f32 ? F32_TYPE : F64_TYPE;

    param_type = ret_type;

    va_start(param_value_list, intrinsic);

    ret = wasm_jit_call_llvm_intrinsic_v(comp_ctx, func_ctx, intrinsic, ret_type,
                                         &param_type, 1, param_value_list);

    va_end(param_value_list);

    return ret;
}

bool wasm_jit_compile_op_i32_clz(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    return wasm_jit_compile_int_bit_count(comp_ctx, func_ctx, CLZ32, true);
}

bool wasm_jit_compile_op_i32_ctz(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    return wasm_jit_compile_int_bit_count(comp_ctx, func_ctx, CTZ32, true);
}

bool wasm_jit_compile_op_i32_popcnt(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    return wasm_jit_compile_int_bit_count(comp_ctx, func_ctx, POP_CNT32, true);
}

bool wasm_jit_compile_op_i64_clz(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    return wasm_jit_compile_int_bit_count(comp_ctx, func_ctx, CLZ64, false);
}

bool wasm_jit_compile_op_i64_ctz(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    return wasm_jit_compile_int_bit_count(comp_ctx, func_ctx, CTZ64, false);
}

bool wasm_jit_compile_op_i64_popcnt(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    return wasm_jit_compile_int_bit_count(comp_ctx, func_ctx, POP_CNT64, false);
}

bool wasm_jit_compile_op_i32_arithmetic(JITCompContext *comp_ctx,
                                        JITFuncContext *func_ctx, IntArithmetic arith_op,
                                        uint8 **p_frame_ip)
{
    return compile_op_int_arithmetic(comp_ctx, func_ctx, arith_op, true,
                                     p_frame_ip);
}

bool wasm_jit_compile_op_i64_arithmetic(JITCompContext *comp_ctx,
                                        JITFuncContext *func_ctx, IntArithmetic arith_op,
                                        uint8 **p_frame_ip)
{
    return compile_op_int_arithmetic(comp_ctx, func_ctx, arith_op, false,
                                     p_frame_ip);
}