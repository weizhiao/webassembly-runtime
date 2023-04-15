#ifndef _WASM_EXCEPTION_H
#define _WASM_EXCEPTION_H

#include "wasm_type.h"

//设置错误
void
wasm_set_exception(WASMModuleInstance *module_inst, const char *exception);

//获取错误
const char *
wasm_get_exception(WASMModuleInstance *module_inst);

#endif