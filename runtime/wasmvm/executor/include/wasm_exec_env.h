#ifndef _WASM_EXEC_ENV_H
#define _WASM_EXEC_ENV_H

#include "wasm_type.h"

/* Execution environment */
typedef struct WASMExecEnv
{
    /* Next thread's exec env of a WASM module instance. */
    struct WASMExecEnv *next;

    /* Previous thread's exec env of a WASM module instance. */
    struct WASMExecEnv *prev;

    /* The WASM module instance of current thread */
    WASMModule *module_inst;

#if WASM_ENABLE_JIT != 0
    uint32 argv_buf[64];
#endif

    struct
    {
        uint8 *top_boundary;

        uint8 *top;

        uint8 *bottom;
    } value_stack;

    struct
    {
        uint8 *top_boundary;

        uint8 *top;

        uint8 *bottom;
    } exectution_stack;

} WASMExecEnv;

WASMExecEnv *
wasm_exec_env_create(WASMModule *module_inst);

void wasm_exec_env_destroy(WASMExecEnv *exec_env);

#endif /* end of _WASM_EXEC_ENV_H */
