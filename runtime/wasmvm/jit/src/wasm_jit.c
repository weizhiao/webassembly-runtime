#include "wasm_jit.h"

static char wasm_jit_error[128];

char *
wasm_jit_get_last_error()
{
    return wasm_jit_error[0] == '\0' ? "" : wasm_jit_error;
}

void wasm_jit_set_last_error_v(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(wasm_jit_error, sizeof(wasm_jit_error), format, args);
    va_end(args);
}

void wasm_jit_set_last_error(const char *error)
{
    if (error)
        snprintf(wasm_jit_error, sizeof(wasm_jit_error), "Error: %s", error);
    else
        wasm_jit_error[0] = '\0';
}
