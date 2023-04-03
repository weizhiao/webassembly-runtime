#include "wasm_exception.h"

bool
wasm_runtime_get_app_addr_range(WASMModuleInstance *module_inst_comm,
                                uint32 app_offset, uint32 *p_app_start_offset,
                                uint32 *p_app_end_offset)
{
    WASMModuleInstance *module_inst = (WASMModuleInstance *)module_inst_comm;
    WASMMemory *memory_inst;
    uint32 memory_data_size;

    memory_inst = module_inst->memories;
    if (!memory_inst) {
        return false;
    }

    memory_data_size = memory_inst->memory_data_size;

    if (app_offset < memory_data_size) {
        if (p_app_start_offset)
            *p_app_start_offset = 0;
        if (p_app_end_offset)
            *p_app_end_offset = memory_data_size;
        return true;
    }

    return false;
}

bool
wasm_runtime_get_native_addr_range(WASMModuleInstance *module_inst_comm,
                                   uint8 *native_ptr,
                                   uint8 **p_native_start_addr,
                                   uint8 **p_native_end_addr)
{
    WASMModuleInstance *module_inst = (WASMModuleInstance *)module_inst_comm;
    WASMMemory *memory_inst;
    uint8 *addr = (uint8 *)native_ptr;

    memory_inst = module_inst->memories;
    if (!memory_inst) {
        return false;
    }

    if (memory_inst->memory_data <= addr
        && addr < memory_inst->memory_data_end) {
        if (p_native_start_addr)
            *p_native_start_addr = memory_inst->memory_data;
        if (p_native_end_addr)
            *p_native_end_addr = memory_inst->memory_data_end;
        return true;
    }

    return false;
}

bool
wasm_runtime_validate_app_addr(WASMModule *module_inst_comm,
                               uint32 app_offset, uint32 size)
{
    WASMModule *module_inst = module_inst_comm;
    WASMMemory *memory_inst;

    memory_inst = module_inst->memories;
    if (!memory_inst) {
        goto fail;
    }

    /* integer overflow check */
    if (app_offset > UINT32_MAX - size) {
        goto fail;
    }

    if (app_offset + size <= memory_inst->memory_data_size) {
        return true;
    }

fail:
    wasm_set_exception(module_inst, "out of bounds memory access");
    return false;
}

bool
wasm_runtime_validate_app_str_addr(WASMModuleInstance *module_inst_comm,
                                   uint32 app_str_offset)
{
    WASMModuleInstance *module_inst = (WASMModuleInstance *)module_inst_comm;
    uint32 app_end_offset;
    char *str, *str_end;

    if (!wasm_runtime_get_app_addr_range(module_inst_comm, app_str_offset, NULL,
                                         &app_end_offset))
        goto fail;

    str = wasm_runtime_addr_app_to_native(module_inst_comm, app_str_offset);
    str_end = str + (app_end_offset - app_str_offset);
    while (str < str_end && *str != '\0')
        str++;
    if (str == str_end)
        goto fail;

    return true;
fail:
    wasm_set_exception(module_inst, "out of bounds memory access");
    return false;
}