#include"wasm_type.h"
#include "wasm_exec_env.h"
#include "wasm_memory.h"

WASMExecEnv *
wasm_exec_env_create(WASMModuleInstance *module_inst,
                              uint32 stack_size)
{
    uint64 total_size =
        offsetof(WASMExecEnv, wasm_stack.s.bottom) + (uint64)stack_size;
    WASMExecEnv *exec_env;

    if (total_size >= UINT32_MAX
        || !(exec_env = wasm_runtime_malloc((uint32)total_size)))
        return NULL;

    memset((void* )exec_env, 0, (uint32)total_size);

    exec_env->module_inst = module_inst;
    exec_env->wasm_stack_size = stack_size;
    exec_env->wasm_stack.s.top_boundary =
        exec_env->wasm_stack.s.bottom + stack_size;
    exec_env->wasm_stack.s.top = exec_env->wasm_stack.s.bottom;

    return exec_env;
}

void
wasm_exec_env_set_thread_info(WASMExecEnv *exec_env)
{
    exec_env->handle = os_self_thread();
}
