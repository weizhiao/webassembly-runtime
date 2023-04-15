#ifndef _WASM_MEMORY_H
#define _WASM_MEMORY_H

#include "platform.h"
#include "wasm_type.h"

//运行时的malloc
void *
wasm_runtime_malloc(uint64 size);

//运行时的free
void
wasm_runtime_free(void *ptr);

//运行时的realloc
void *
wasm_runtime_realloc(void *ptr, uint32 size);

//销毁module
void 
wasm_module_destory(WASMModule *module, Stage stage);

//创建module
WASMModule *
create_module();

bool
wasm_runtime_validate_native_addr(WASMModule *module_inst_comm,
                                  void *native_ptr, uint32 size);

bool
wasm_runtime_validate_app_addr(WASMModule *module_inst_comm,
                               uint32 app_offset, uint32 size);

void *
wasm_runtime_addr_app_to_native(WASMModule *module_inst,
                                uint32 app_offset);

uint32
wasm_runtime_addr_native_to_app(WASMModule *module_inst_comm,
                                void *native_ptr);

bool
wasm_enlarge_memory(WASMModuleInstance *module, uint32 inc_page_count);

bool
wasm_runtime_validate_app_str_addr(WASMModuleInstance *module_inst_comm,
                                   uint32 app_str_offset);

#endif