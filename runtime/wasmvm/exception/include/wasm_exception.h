#ifndef _WASM_EXCEPTION_H
#define _WASM_EXCEPTION_H

#include "wasm_type.h"

//设置错误
void
wasm_set_exception(WASMModuleInstance *module_inst, const char *exception);

//获取错误
const char *
wasm_get_exception(WASMModuleInstance *module_inst);

//判断是否超出线性内存的大小
bool
wasm_runtime_validate_app_addr(WASMModuleInstance *module_inst_comm,
                               uint32 app_offset, uint32 size);

//
bool
wasm_runtime_validate_app_str_addr(WASMModuleInstance *module_inst_comm,
                                   uint32 app_str_offset);

#endif