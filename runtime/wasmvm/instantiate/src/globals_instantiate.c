#include "instantiate.h"

bool
globals_instantiate(WASMModule *module,
                    char *error_buf, uint32 error_buf_size)
{
    uint32 global_data_offset = 0;
    uint32 i, global_count = module->import_global_count + module->global_count;
    WASMGlobal *global, *globals;
    uint8 *global_data, *global_data_start;
    globals = global = module->globals;

    for (i = 0; i < module->import_global_count; i++, global++) {
        global->data_offset = global_data_offset;
        global_data_offset += wasm_value_type_size(global->type);
    }

    /* instantiate globals from global section */
    for (i = 0; i < module->global_count; i++, global++) {
        global->data_offset = global_data_offset;
        global_data_offset += wasm_value_type_size(global->type);
    }

    if (global_count > 0) {
        /* Initialize the global data */
        if (!(global_data = global_data_start = wasm_runtime_malloc(global_data_offset))){
            goto fail;
        }
        global = globals;
        for (i = 0; i < global_count; i++, global++) {
            switch (global->type) {
                case VALUE_TYPE_I32:
                case VALUE_TYPE_F32:
                    *(int32 *)global_data = global->initial_value.i32;
                    global_data += sizeof(int32);
                    break;
                case VALUE_TYPE_I64:
                case VALUE_TYPE_F64:
                    *(int64 *)global_data = global->initial_value.i64;
                    global_data += sizeof(int64);
                    break;
            }
        }
    }

    module->global_count = global_count;
    module->global_data = global_data_start;

    LOG_VERBOSE("Instantiate global success.\n");
    return true;
fail:
    LOG_VERBOSE("Instantiate global fail.\n");
    set_error_buf(error_buf, error_buf_size, "Instantiate global fail.\n");
    return false;
}