#ifndef _WASM_JIT_INIT_H
#define _WASM_JIT_INIT_H

#include "wasm_type.h"

bool init_llvm_jit_functions_stage1(WASMModule *module);

bool init_llvm_jit_functions_stage2(WASMModule *module);

bool compile_jit_functions(WASMModule *module);

#endif
