#ifndef _WASM_LLVM_OPCODE_H
#define _WASM_LLVM_OPCODE_H

#include "llvm-c/Core.h"

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

#define LLVMOPSExt(value, dst_type) LLVM_OP_CAST_TEMPLATE(SExt, value, dst_type, "data_s_ext")
#define LLVMOPZExt(value, dst_type) LLVM_OP_CAST_TEMPLATE(ZExt, value, dst_type, "data_z_ext")
#define LLVMOPFPToSI(value, dst_type, name) LLVM_OP_CAST_TEMPLATE(FPToSI, value, dst_type, name)
#define LLVMOPFPToUI(value, dst_type, name) LLVM_OP_CAST_TEMPLATE(FPToUI, value, dst_type, name)
#define LLVMOPBitCast(ptr, ptr_type) LLVM_OP_CAST_TEMPLATE(BitCast, ptr, ptr_type, "bit_cast")
#define LLVMOPTrunc(value, dst_type) LLVM_OP_CAST_TEMPLATE(Trunc, value, dst_type, "val_trunc")
#define LLVMOPSIntCast(value, dst_type) LLVM_OP_INTCAST_TEMPLATE(value, dst_type, true, "int_cast")

#define LLVMOPFCmp(res, Op, left, right, name)                         \
    do                                                                 \
    {                                                                  \
        res = LLVMBuildFCmp(comp_ctx->builder, Op, left, right, name); \
    } while (0)

// 算术类指令
#define LLVM_OP_TEMPLATE(Op, left, right, res, name)               \
    do                                                             \
    {                                                              \
        res = LLVMBuild##Op(comp_ctx->builder, left, right, name); \
    } while (0)

#define LLVMOPAdd(left, right, res, name) LLVM_OP_TEMPLATE(Add, left, right, res, name)
#define LLVMOPSub(left, right, res, name) LLVM_OP_TEMPLATE(Sub, left, right, res, name)

#define LLVMOPOr(left, right, res, name) LLVM_OP_TEMPLATE(Or, left, right, res, name)
#define LLVMOPAnd(left, right, res, name) LLVM_OP_TEMPLATE(And, left, right, res, name)

#define LLVMOPShl(left, right, res, name) LLVM_OP_TEMPLATE(Shl, left, right, res, name)
#define LLVMOPUDiv(left, right, res, name) LLVM_OP_TEMPLATE(UDiv, left, right, res, name)
#define LLVMOPAShr(left, right, res, name) LLVM_OP_TEMPLATE(AShr, left, right, res, name)
#define LLVMOPLShr(left, right, res, name) LLVM_OP_TEMPLATE(LShr, left, right, res, name)

#endif