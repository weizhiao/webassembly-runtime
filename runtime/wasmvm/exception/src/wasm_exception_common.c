#include "wasm_exception.h"

const char *
wasm_get_exception(WASMModuleInstance *module_inst)
{
    if (module_inst->cur_exception[0] == '\0')
        return NULL;
    else
        return module_inst->cur_exception;
}

void
wasm_set_exception(WASMModuleInstance *module_inst, const char *exception)
{
    WASMExecEnv *exec_env = NULL;

    if (exception) {
        snprintf(module_inst->cur_exception, sizeof(module_inst->cur_exception),
                 "Exception: %s", exception);
    }
    else {
        module_inst->cur_exception[0] = '\0';
    }

}