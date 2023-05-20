#include "wasm_type.h"
#include "wasm_exec_env.h"
#include "wasm_memory.h"

WASMExecEnv *
wasm_exec_env_create(WASMModule *module_inst)
{
    uint64 total_size;
    WASMExecEnv *exec_env;
    uint32 value_stack_size, execution_stack_size;

    value_stack_size = module_inst->default_value_stack_size;
    execution_stack_size = module_inst->default_execution_stack_size;

    total_size = sizeof(WASMExecEnv);

    if (total_size >= UINT32_MAX || !(exec_env = wasm_runtime_malloc(total_size)))
        return NULL;

    if (!(exec_env->exec_stack.bottom = wasm_runtime_malloc(value_stack_size)))
        return NULL;

    memset(exec_env->exec_stack.bottom, 0, value_stack_size);

    exec_env->module_inst = module_inst;

    exec_env->exec_stack.top_boundary =
        exec_env->exec_stack.bottom + value_stack_size;
    exec_env->exec_stack.func_frame_top = exec_env->exec_stack.top_boundary;
    exec_env->exec_stack.top = exec_env->exec_stack.bottom;

    return exec_env;
}