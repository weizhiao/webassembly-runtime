#ifndef WASM_JIT_H
#define WASM_JIT_H

#include "platform.h"
#include "wasm_llvm_opcode.h"
#include "wasm_type.h"
#include "wasm_memory.h"
#include "runtime_log.h"

#ifndef WASM_JIT_FUNC_PREFIX
#define WASM_JIT_FUNC_PREFIX "wasm_jit_func#"
#endif

char *
wasm_jit_get_last_error();

void wasm_jit_set_last_error(const char *error);

void wasm_jit_set_last_error_v(const char *format, ...);

#define HANDLE_FAILURE(callee)                                 \
    do                                                         \
    {                                                          \
        wasm_jit_set_last_error_v("call %s failed", (callee)); \
    } while (0)

#endif
