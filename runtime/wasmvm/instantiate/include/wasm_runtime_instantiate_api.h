#ifndef _RUNTIME_INSTANTIATE_API_H
#define _RUNTIME_INSTANTIATE_API_H

#include "wasm_type.h"

bool
wasm_instantiate(WASMModule *module, uint32 stack_size,
                char *error_buf, uint32 error_buf_size);

#endif