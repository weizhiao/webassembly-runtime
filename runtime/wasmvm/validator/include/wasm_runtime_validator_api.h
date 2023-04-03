#ifndef _WASM_RUNTIME_VALIDATOR_API_H
#define _WASM_RUNTIME_VALIDATOR_API_H

#include "wasm_type.h"

bool wasm_validator(WASMModule *module, char *error_buf,
                             uint32 error_buf_size);

#endif