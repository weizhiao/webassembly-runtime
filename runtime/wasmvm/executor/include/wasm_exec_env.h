#ifndef _WASM_EXEC_ENV_H
#define _WASM_EXEC_ENV_H

#include "wasm_type.h"

typedef struct WASMExecEnv
{
    struct WASMExecEnv *next;
    struct WASMExecEnv *prev;
    WASMModule *module_inst;

#if WASM_ENABLE_JIT != 0
    uint32 argv_buf[64];
#endif

    struct
    {
        uint8 *top_boundary;

        uint8 *func_frame_top;

        uint8 *top;

        uint8 *bottom;
    } exec_stack;

} WASMExecEnv;

WASMExecEnv *
wasm_exec_env_create(WASMModule *module_inst);

void wasm_exec_env_destroy(WASMExecEnv *exec_env);

#endif
