#ifndef WASM_EXECUTOR_H
#define WASM_EXECUTOR_H

#include "wasm_type.h"
#include "wasm_exec_env.h"

//调用wasm函数
bool
wasm_call_function(WASMExecEnv *exec_env, WASMFunction *function,
                   unsigned argc, uint32 argv[]);

//执行wasm模块
bool
execute_main(WASMModule *module_inst, int32 argc, char *argv[]);

#endif