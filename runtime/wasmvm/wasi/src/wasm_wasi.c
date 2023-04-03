#include "wasm_wasi.h"

WASMModule *
wasm_runtime_get_module_inst(WASMExecEnv *exec_env)
{
    return exec_env->module_inst;
}

WASIContext *
wasm_runtime_get_wasi_ctx(WASMModule *module_inst)
{
    return module_inst->wasi_ctx;
}
