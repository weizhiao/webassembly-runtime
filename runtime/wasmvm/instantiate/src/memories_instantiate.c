#include "instantiate.h"

bool
memory_instantiate(WASMMemory *memory, char *error_buf, uint32 error_buf_size)
{
    uint32 num_bytes_per_page, init_page_count, max_page_count;
    uint64 memory_data_size;


    max_page_count = memory->max_page_count;
    num_bytes_per_page = memory->num_bytes_per_page;
    init_page_count = memory->cur_page_count;

    if(max_page_count == -1){
        max_page_count = DEFAULT_MAX_PAGES;
        memory->max_page_count = max_page_count;
    }


    memory_data_size = (uint64)num_bytes_per_page * init_page_count;
    if (memory_data_size > 0
        && !(memory->memory_data = wasm_runtime_malloc(memory_data_size))) {
        goto fail;
    }

    memory->memory_data_size = (uint32)memory_data_size;
    memory->memory_data_end = memory->memory_data + (uint32)memory_data_size;

    return true;

fail:
    return false;
}

bool
memories_instantiate(WASMModule *module, char *error_buf, uint32 error_buf_size)
{
    uint32 i, base_offset, length,
           memory_count = module->import_memory_count + module->memory_count;
    WASMMemory *memory, *memories;
    WASMDataSeg *data_seg;
    WASMGlobal *globals;

    globals = module->globals;
    memory = memories = module->memories;

    for (i = 0; i < memory_count; i++, memory++) {
            if (!(memory_instantiate(memory, error_buf, error_buf_size))) {
                //memories_deinstantiate(memory, memory_count);
                goto fail;
            }
    }

    module->memory_count = memory_count;
    data_seg = module->data_segments;

    /* Initialize the memory data with data segment section */
    for (i = 0; i < module->data_seg_count; i++, data_seg++) {
        memory = memories + data_seg->memory_index;
        uint8 *memory_data = NULL;
        uint32 memory_size = 0;

        memory_data = memory->memory_data;
        memory_size = memory->num_bytes_per_page * memory->cur_page_count;

        if (data_seg->base_offset.init_expr_type == INIT_EXPR_TYPE_GET_GLOBAL) {

            if (!globals
                || globals[data_seg->base_offset.u.global_index].type
                       != VALUE_TYPE_I32) {
                set_error_buf(error_buf, error_buf_size,
                              "data segment does not fit");
                goto fail;
            }

            base_offset =
                globals[data_seg->base_offset.u.global_index].initial_value.i32;
        }
        else {
            base_offset = (uint32)data_seg->base_offset.u.i32;
        }

        /* check offset */
        if (base_offset > memory_size) {
            LOG_DEBUG("base_offset(%d) > memory_size(%d)", base_offset,
                      memory_size);
            set_error_buf(error_buf, error_buf_size,
                          "data segment does not fit");
            goto fail;
        }

        /* check offset + length(could be zero) */
        length = data_seg->data_length;
        if (base_offset + length > memory_size) {
            LOG_DEBUG("base_offset(%d) + length(%d) > memory_size(%d)",
                      base_offset, length, memory_size);
            set_error_buf(error_buf, error_buf_size,
                          "data segment does not fit");
            goto fail;
        }

        if (memory_data) {
            memcpy(memory_data + base_offset, data_seg->data, length);
        }
    }

    LOG_VERBOSE("Instantiate memory success.\n");
    return  true;
fail:
    LOG_VERBOSE("Instantiate memory fail.\n");
    set_error_buf(error_buf, error_buf_size, "Instantiate memory fail.\n");
    return false;
}