#ifndef _WASM_WASI_H
#define _WASM_WASI_H

#include "wasm_type.h"
#include "wasmtime_ssp.h"
#include "wasm_exec_env.h"
#include "posix.h"

typedef struct WASIContext {
    struct fd_table *curfds;
    struct fd_prestats *prestats;
    struct argv_environ_values *argv_environ;
    struct addr_pool *addr_pool;
    char *ns_lookup_buf;
    char **ns_lookup_list;
    char *argv_buf;
    char **argv_list;
    char *env_buf;
    char **env_list;
    uint32_t exit_code;
} * wasi_ctx_t, WASIContext;

typedef struct WASIArguments {
    const char **dir_list;
    uint32 dir_count;
    const char **map_dir_list;
    uint32 map_dir_count;
    const char **env;
    uint32 env_count;
    const char **addr_pool;
    uint32 addr_count;
    const char **ns_lookup_pool;
    uint32 ns_lookup_count;
    char **argv;
    uint32 argc;
    int stdio[3];
} WASIArguments;

WASMModule *
wasm_runtime_get_module_inst(WASMExecEnv *exec_env);

WASIContext *
wasm_runtime_get_wasi_ctx(WASMModule *module_inst);

#endif