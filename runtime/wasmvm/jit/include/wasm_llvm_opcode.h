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

#endif