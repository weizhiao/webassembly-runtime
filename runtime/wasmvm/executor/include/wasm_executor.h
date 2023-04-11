#ifndef WASM_EXECUTOR_H
#define WASM_EXECUTOR_H

#include "wasm_type.h"
#include "wasm_exec_env.h"

//执行wasm模块
bool
execute_main(WASMModule *module_inst, int32 argc, char *argv[]);

//执行wasm模块中的某个函数
bool
execute_func(WASMModule *module_inst, const char *name,
             int32 argc, char *argv[]);

#endif