#include "instantiate.h"

bool memory_instantiate(WASMMemory *memory)
{
    uint32 num_bytes_per_page, init_page_count, max_page_count;
    uint64 memory_data_size;

    max_page_count = memory->max_page_count;
    num_bytes_per_page = memory->num_bytes_per_page;
    init_page_count = memory->cur_page_count;

    memory_data_size = (uint64)num_bytes_per_page * init_page_count;
    if (memory_data_size > 0 && !(memory->memory_data = wasm_runtime_malloc(memory_data_size)))
    {
        goto fail;
    }

    memset(memory->memory_data, 0, memory_data_size);

    memory->memory_data_size = (uint32)memory_data_size;
    memory->memory_data_end = memory->memory_data + (uint32)memory_data_size;

    return true;

fail:
    return false;
}

bool memories_instantiate(WASMModule *module)
{
    uint32 i, base_offset, length,
        memory_count = module->import_memory_count + module->memory_count;
    WASMMemory *memory, *memories;
    WASMDataSeg *data_seg;
    WASMGlobal *globals;

    globals = module->globals;
    memory = memories = module->memories;

    for (i = 0; i < memory_count; i++, memory++)
    {
        if (!memory_instantiate(memory))
        {
            goto fail;
        }
    }

    module->memory_count = memory_count;
    data_seg = module->data_segments;

    for (i = 0; i < module->data_seg_count; i++, data_seg++)
    {
        memory = memories + data_seg->memory_index;
        uint8 *memory_data = NULL;
        uint32 memory_size = 0;

        memory_data = memory->memory_data;
        memory_size = memory->num_bytes_per_page * memory->cur_page_count;

        if (data_seg->base_offset.init_expr_type == INIT_EXPR_TYPE_GET_GLOBAL)
        {
            base_offset = globals[data_seg->base_offset.u.global_index].initial_value.i32;
        }
        else
        {
            base_offset = (uint32)data_seg->base_offset.u.i32;
        }

        length = data_seg->data_length;

        memcpy(memory_data + base_offset, data_seg->data, length);
    }

    LOG_VERBOSE("Instantiate memory success.\n");
    return true;
fail:
    LOG_VERBOSE("Instantiate memory fail.\n");
    wasm_set_exception(module, "Instantiate memory fail.\n");
    return false;
}