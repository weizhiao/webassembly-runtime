#ifndef _WASM_RUNTIME_INIT_API_H
#define _WASM_RUNTIME_INIT_API_H

#include "platform.h"
#include "runtime_utils.h"

//初始化wasmvm环境
bool
wasm_runtime_init_env();

//初始化wasi环境
bool 
wasm_runtime_wasi_init(WASMModule *module_inst,
                       const char *dir_list[], uint32 dir_count,
                       const char *map_dir_list[], uint32 map_dir_count,
                       const char *env[], uint32 env_count,
                       const char *addr_pool[], uint32 addr_pool_size,
                       const char *ns_lookup_pool[], uint32 ns_lookup_pool_size,
                       char *argv[], uint32 argc, int stdinfd, int stdoutfd,
                       int stderrfd, char *error_buf, uint32 error_buf_size);

#endif