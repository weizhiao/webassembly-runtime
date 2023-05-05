#ifndef _WASM_LLVM_OPCODE_H
#define _WASM_LLVM_OPCODE_H

#include "llvm-c/Core.h"
#include "wasm_jit_llvm.h"

#define LLVMBuildGEP(res, value_type, base_addr, value_offset, value_name) \
    do                                                                     \
    {                                                                      \
        res = LLVMBuildInBoundsGEP2(                                       \
            builder, value_type, base_addr,                                \
            &value_offset, 1, value_name);                                 \
    } while (0)

#define LLVMOPLoad(res, maddr, data_type)                         \
    do                                                            \
    {                                                             \
        res = LLVMBuildLoad2(comp_ctx->builder, data_type, maddr, \
                             "data");                             \
        LLVMSetAlignment(res, 1);                                 \
    } while (0)

#define LLVMOPStore(res, llvm_value, maddr)                         \
    do                                                              \
    {                                                               \
        res = LLVMBuildStore(comp_ctx->builder, llvm_value, maddr); \
        LLVMSetAlignment(res, 1);                                   \
    } while (0)

// 转换类指令
#define LLVM_OP_CAST_TEMPLATE(Op, value, dst_type, name)          \
    do                                                            \
    {                                                             \
        value = LLVMBuild##Op(comp_ctx->builder, value, dst_type, \
                              name);                              \
    } while (0)

#define LLVM_OP_INTCAST_TEMPLATE(value, dst_type, is_sign, name)                      \
    do                                                                                \
    {                                                                                 \
        value = LLVMBuildIntCast2(comp_ctx->builder, value, dst_type, is_sign, name); \
    } while (0)

// 符号扩展
#define LLVMOPSExt(value, dst_type) LLVM_OP_CAST_TEMPLATE(SExt, value, dst_type, "data_s_ext")
#define LLVMOPZExt(value, dst_type) LLVM_OP_CAST_TEMPLATE(ZExt, value, dst_type, "data_z_ext")
#define LLVMOPFPExt(value, dst_type) LLVM_OP_CAST_TEMPLATE(FPExt, value, dst_type, "data_f_ext")

// 浮点数与整数之间的转换
#define LLVMOPFPToSI(value, dst_type, name) LLVM_OP_CAST_TEMPLATE(FPToSI, value, dst_type, name)
#define LLVMOPFPToUI(value, dst_type, name) LLVM_OP_CAST_TEMPLATE(FPToUI, value, dst_type, name)
#define LLVMOPSIToFP(value, dst_type, name) LLVM_OP_CAST_TEMPLATE(SIToFP, value, dst_type, name)
#define LLVMOPUIToFP(value, dst_type, name) LLVM_OP_CAST_TEMPLATE(UIToFP, value, dst_type, name)

// 截断
#define LLVMOPBitCast(ptr, ptr_type) LLVM_OP_CAST_TEMPLATE(BitCast, ptr, ptr_type, "bit_cast")
#define LLVMOPTrunc(value, dst_type) LLVM_OP_CAST_TEMPLATE(Trunc, value, dst_type, "intval_trunc")
#define LLVMOPSIntCast(value, dst_type) LLVM_OP_INTCAST_TEMPLATE(value, dst_type, true, "int_cast")
#define LLVMOPFPTrunc(value, dst_type) LLVM_OP_CAST_TEMPLATE(FPTrunc, value, dst_type, "floatval_trunc")

// 比较指令
#define LLVMOPFCmp(res, Op, left, right, name)                         \
    do                                                                 \
    {                                                                  \
        res = LLVMBuildFCmp(comp_ctx->builder, Op, left, right, name); \
    } while (0)

#define LLVMOPICmp(res, Op, left, right, name)                         \
    do                                                                 \
    {                                                                  \
        res = LLVMBuildICmp(comp_ctx->builder, Op, left, right, name); \
    } while (0)

// 算术类指令
#define LLVM_OP_TEMPLATE(Op, left, right, res, name)               \
    do                                                             \
    {                                                              \
        res = LLVMBuild##Op(comp_ctx->builder, left, right, name); \
    } while (0)

#define LLVMOPAdd(left, right, res, name) LLVM_OP_TEMPLATE(Add, left, right, res, name)
#define LLVMOPSub(left, right, res, name) LLVM_OP_TEMPLATE(Sub, left, right, res, name)
#define LLVMOPMul(left, right, res, name) LLVM_OP_TEMPLATE(Mul, left, right, res, name)

#define LLVMOPOr(left, right, res, name) LLVM_OP_TEMPLATE(Or, left, right, res, name)
#define LLVMOPAnd(left, right, res, name) LLVM_OP_TEMPLATE(And, left, right, res, name)
#define LLVMOPXor(left, right, res, name) LLVM_OP_TEMPLATE(Xor, left, right, res, name)

#define LLVMOPShl(left, right, res, name) LLVM_OP_TEMPLATE(Shl, left, right, res, name)
#define LLVMOPUDiv(left, right, res, name) LLVM_OP_TEMPLATE(UDiv, left, right, res, name)
#define LLVMOPAShr(left, right, res, name) LLVM_OP_TEMPLATE(AShr, left, right, res, name)
#define LLVMOPLShr(left, right, res, name) LLVM_OP_TEMPLATE(LShr, left, right, res, name)

#define LLVMOPURem(left, right, res, name) LLVM_OP_TEMPLATE(URem, left, right, res, name)

#define LLVMOPF32Min(left, right, res, name) res = compile_op_float_min_max(comp_ctx, func_ctx, true, left, right, true)
#define LLVMOPF32Max(left, right, res, name) res = compile_op_float_min_max(comp_ctx, func_ctx, true, left, right, false)
#define LLVMOPF64Min(left, right, res, name) res = compile_op_float_min_max(comp_ctx, func_ctx, false, left, right, true)
#define LLVMOPF64Max(left, right, res, name) res = compile_op_float_min_max(comp_ctx, func_ctx, false, left, right, false)

#define LLVMOPF32CopySign(left, right, res, name)                                    \
    do                                                                               \
    {                                                                                \
        LLVMTypeRef _ret_type, _param_types[2];                                      \
        _param_types[0] = _param_types[1] = _ret_type = F32_TYPE;                    \
        res = wasm_jit_call_llvm_intrinsic(comp_ctx, func_ctx, "llvm.copysign.f32",  \
                                           _ret_type, _param_types, 2, left, right); \
    } while (0)

#define LLVMOPF64CopySign(left, right, res, name)                                    \
    do                                                                               \
    {                                                                                \
        LLVMTypeRef _ret_type, _param_types[2];                                      \
        _param_types[0] = _param_types[1] = _ret_type = F64_TYPE;                    \
        res = wasm_jit_call_llvm_intrinsic(comp_ctx, func_ctx, "llvm.copysign.f64",  \
                                           _ret_type, _param_types, 2, left, right); \
    } while (0)

#if WASM_ENABLE_FPU == 0
#define LLVMOPF32Add(left, right, res, name) LLVM_OP_TEMPLATE(FAdd, left, right, res, name)
#define LLVMOPF32Sub(left, right, res, name) LLVM_OP_TEMPLATE(FSub, left, right, res, name)
#define LLVMOPF32Mul(left, right, res, name) LLVM_OP_TEMPLATE(FMul, left, right, res, name)
#define LLVMOPF32Div(left, right, res, name) LLVM_OP_TEMPLATE(FDiv, left, right, res, name)
#define LLVMOPF64Add(left, right, res, name) LLVM_OP_TEMPLATE(FAdd, left, right, res, name)
#define LLVMOPF64Sub(left, right, res, name) LLVM_OP_TEMPLATE(FSub, left, right, res, name)
#define LLVMOPF64Mul(left, right, res, name) LLVM_OP_TEMPLATE(FMul, left, right, res, name)
#define LLVMOPF64Div(left, right, res, name) LLVM_OP_TEMPLATE(FDiv, left, right, res, name)

#else

#define LLVM_OP_FNUMBERIC_TEMPLATE(left, right, res, name, op_name, is_32) \
    do                                                                     \
    {                                                                      \
        res = call_llvm_float_experimental_constrained_intrinsic(          \
            comp_ctx, func_ctx, is_32,                                     \
            op_name,                                                       \
            left, right, comp_ctx->fp_rounding_mode,                       \
            comp_ctx->fp_exception_behavior);                              \
    } while (0)

#define LLVMOPF32Add(left, right, res, name) LLVM_OP_FNUMBERIC_TEMPLATE(left, right, res, name, "llvm.experimental.constrained.fadd.f32", true)
#define LLVMOPF32Sub(left, right, res, name) LLVM_OP_FNUMBERIC_TEMPLATE(left, right, res, name, "llvm.experimental.constrained.fsub.f32", true)
#define LLVMOPF32Mul(left, right, res, name) LLVM_OP_FNUMBERIC_TEMPLATE(left, right, res, name, "llvm.experimental.constrained.fmul.f32", true)
#define LLVMOPF32Div(left, right, res, name) LLVM_OP_FNUMBERIC_TEMPLATE(left, right, res, name, "llvm.experimental.constrained.fdiv.f32", true)
#define LLVMOPF64Add(left, right, res, name) LLVM_OP_FNUMBERIC_TEMPLATE(left, right, res, name, "llvm.experimental.constrained.fadd.f64", false)
#define LLVMOPF64Sub(left, right, res, name) LLVM_OP_FNUMBERIC_TEMPLATE(left, right, res, name, "llvm.experimental.constrained.fsub.f64", false)
#define LLVMOPF64Mul(left, right, res, name) LLVM_OP_FNUMBERIC_TEMPLATE(left, right, res, name, "llvm.experimental.constrained.fmul.f64", false)
#define LLVMOPF64Div(left, right, res, name) LLVM_OP_FNUMBERIC_TEMPLATE(left, right, res, name, "llvm.experimental.constrained.fdiv.f64", false)
#endif

#define SHIFT_COUNT_MASK(right, is_i32)                    \
    do                                                     \
    {                                                      \
        LLVMValueRef shift_count_mask, bits_minus_one;     \
        bits_minus_one = is_i32 ? I32_31 : I64_63;         \
        LLVMOPAnd(right, bits_minus_one, shift_count_mask, \
                  "shift_count_mask");                     \
        right = shift_count_mask;                          \
    } while (0)

#define LLVMOPRotl32(left, right, res, name)                          \
    do                                                                \
    {                                                                 \
        LLVMValueRef bits_minus_shift_count, tmp_l, tmp_r;            \
        SHIFT_COUNT_MASK(right, true);                                \
        if ((int32)LLVMConstIntGetSExtValue(right) == 0)              \
        {                                                             \
            res = left;                                               \
        }                                                             \
        else                                                          \
        {                                                             \
            LLVMOPSub(I32_32, right, bits_minus_shift_count,          \
                      "bits_minus_shift_count");                      \
            LLVMOPShl(left, right, tmp_l, "tmp_l");                   \
            LLVMOPLShr(left, bits_minus_shift_count, tmp_r, "tmp_r"); \
            LLVMOPOr(tmp_l, tmp_r, res, name);                        \
        }                                                             \
    } while (0)

#define LLVMOPRotr32(left, right, res, name)                         \
    do                                                               \
    {                                                                \
        LLVMValueRef bits_minus_shift_count, tmp_l, tmp_r;           \
        SHIFT_COUNT_MASK(right, true);                               \
        if ((int32)LLVMConstIntGetSExtValue(right) == 0)             \
        {                                                            \
            res = left;                                              \
        }                                                            \
        else                                                         \
        {                                                            \
            LLVMOPSub(I32_32, right, bits_minus_shift_count,         \
                      "bits_minus_shift_count");                     \
            LLVMOPLShr(left, right, tmp_l, "tmp_l");                 \
            LLVMOPShl(left, bits_minus_shift_count, tmp_r, "tmp_r"); \
            LLVMOPOr(tmp_l, tmp_r, res, name);                       \
        }                                                            \
    } while (0)

#define LLVMOPRotl64(left, right, res, name)                          \
    do                                                                \
    {                                                                 \
        LLVMValueRef bits_minus_shift_count, tmp_l, tmp_r;            \
        SHIFT_COUNT_MASK(right, false);                               \
        if ((int64)LLVMConstIntGetSExtValue(right) == 0)              \
        {                                                             \
            res = left;                                               \
        }                                                             \
        else                                                          \
        {                                                             \
            LLVMOPSub(I64_64, right, bits_minus_shift_count,          \
                      "bits_minus_shift_count");                      \
            LLVMOPShl(left, right, tmp_l, "tmp_l");                   \
            LLVMOPLShr(left, bits_minus_shift_count, tmp_r, "tmp_r"); \
            LLVMOPOr(tmp_l, tmp_r, res, name);                        \
        }                                                             \
    } while (0)

#define LLVMOPRotr64(left, right, res, name)                         \
    do                                                               \
    {                                                                \
        LLVMValueRef bits_minus_shift_count, tmp_l, tmp_r;           \
        SHIFT_COUNT_MASK(right, false);                              \
        if ((int64)LLVMConstIntGetSExtValue(right) == 0)             \
        {                                                            \
            res = left;                                              \
        }                                                            \
        else                                                         \
        {                                                            \
            LLVMOPSub(I64_64, right, bits_minus_shift_count,         \
                      "bits_minus_shift_count");                     \
            LLVMOPLShr(left, right, tmp_l, "tmp_l");                 \
            LLVMOPShl(left, bits_minus_shift_count, tmp_r, "tmp_r"); \
            LLVMOPOr(tmp_l, tmp_r, res, name);                       \
        }                                                            \
    } while (0)

// math指令
#define LLVMOPF32Abs(res) res = call_llvm_float_math_intrinsic(comp_ctx, func_ctx, true, "llvm.fabs.f32", res)
#define LLVMOPF32Neg(res) res = LLVMBuildFNeg(comp_ctx->builder, res, "fneg")
#define LLVMOPF32Ceil(res) res = call_llvm_float_math_intrinsic(comp_ctx, func_ctx, true, "llvm.ceil.f32", res)
#define LLVMOPF32Floor(res) res = call_llvm_float_math_intrinsic(comp_ctx, func_ctx, true, "llvm.floor.f32", res)
#define LLVMOPF32Trunc(res) res = call_llvm_float_math_intrinsic(comp_ctx, func_ctx, true, "llvm.trunc.f32", res)
#define LLVMOPF32Nearest(res) res = call_llvm_float_math_intrinsic(comp_ctx, func_ctx, true, "llvm.rint.f32", res)

#if WASM_ENABLE_FPU == 0
#define LLVMOPF32Sqrt(res) res = call_llvm_float_math_intrinsic(comp_ctx, func_ctx, true, "llvm.sqrt.f32", res)
#define LLVMOPF64Sqrt(res) res = call_llvm_float_math_intrinsic(comp_ctx, func_ctx, false, "llvm.sqrt.f64", res)
#else
#define LLVMOPF32Sqrt(res) res = call_llvm_libm_experimental_constrained_intrinsic(comp_ctx, func_ctx, true, "llvm.experimental.constrained.sqrt.f32", res, comp_ctx->fp_rounding_mode, comp_ctx->fp_exception_behavior)
#define LLVMOPF64Sqrt(res) res = call_llvm_libm_experimental_constrained_intrinsic(comp_ctx, func_ctx, false, "llvm.experimental.constrained.sqrt.f64", res, comp_ctx->fp_rounding_mode, comp_ctx->fp_exception_behavior)
#endif

#define LLVMOPF64Abs(res) res = call_llvm_float_math_intrinsic(comp_ctx, func_ctx, false, "llvm.fabs.f64", res)
#define LLVMOPF64Neg(res) res = LLVMBuildFNeg(comp_ctx->builder, res, "fneg")
#define LLVMOPF64Ceil(res) res = call_llvm_float_math_intrinsic(comp_ctx, func_ctx, false, "llvm.ceil.f64", res)
#define LLVMOPF64Floor(res) res = call_llvm_float_math_intrinsic(comp_ctx, func_ctx, false, "llvm.floor.f64", res)
#define LLVMOPF64Trunc(res) res = call_llvm_float_math_intrinsic(comp_ctx, func_ctx, false, "llvm.trunc.f64", res)
#define LLVMOPF64Nearest(res) res = call_llvm_float_math_intrinsic(comp_ctx, func_ctx, false, "llvm.rint.f64", res)

#endif