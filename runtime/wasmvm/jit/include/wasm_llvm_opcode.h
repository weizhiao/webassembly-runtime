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

#define LLVMOPCast(ptr, ptr_type)                                \
    do                                                           \
    {                                                            \
        ptr = LLVMBuildBitCast(comp_ctx->builder, ptr, ptr_type, \
                               "data_ptr");                      \
    } while (0)

#define LLVMOPLoad(res, maddr, data_type)                         \
    do                                                            \
    {                                                             \
        res = LLVMBuildLoad2(comp_ctx->builder, data_type, maddr, \
                             "data");                             \
        LLVMSetAlignment(res, 1);                                 \
    } while (0)

#define LLVMOPTrunc(value, data_type)                               \
    do                                                              \
    {                                                               \
        value = LLVMBuildTrunc(comp_ctx->builder, value, data_type, \
                               "val_trunc");                        \
    } while (0)

#define LLVMOPStore(res, llvm_value, maddr)                         \
    do                                                              \
    {                                                               \
        res = LLVMBuildStore(comp_ctx->builder, llvm_value, maddr); \
        LLVMSetAlignment(res, 1);                                   \
    } while (0)

#define LLVMOPSExt(value, dst_type)                               \
    do                                                            \
    {                                                             \
        value = LLVMBuildSExt(comp_ctx->builder, value, dst_type, \
                              "data_s_ext");                      \
    } while (0)

#define LLVMOPZExt(value, dst_type)                               \
    do                                                            \
    {                                                             \
        value = LLVMBuildZExt(comp_ctx->builder, value, dst_type, \
                              "data_z_ext");                      \
    } while (0)

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