#include "wasm_exception.h"

const char *
wasm_get_exception(WASMModule *module)
{
    if (module->cur_exception[0] == '\0')
        return NULL;
    else
        return module->cur_exception;
}

void
wasm_set_exception(WASMModule *module, const char *exception)
{
    if (exception) {
        snprintf(module->cur_exception, sizeof(module->cur_exception),
                 "Exception: %s", exception);
    }
    else {
        module->cur_exception[0] = '\0';
    }

}