#include "loader.h"

bool
load_memory_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 maximum = DEFAULT_MAX_PAGES;
    uint32 memory_count, i;
    uint64 total_size;
    WASMMemory *memory;

    read_leb_uint32(p, p_end, memory_count);

    if (memory_count) {
        module->memory_count = memory_count;
        total_size = sizeof(WASMMemory) * (uint64)memory_count;
        if (!(module->memories = wasm_runtime_malloc(total_size))) {
            return false;
        }

        /* load each memory */
        memory = module->memories;
        for (i = 0; i < memory_count; i++, memory++){
            read_leb_uint1(p, p_end, memory->flags);
            read_leb_uint32(p, p_end, memory->init_page_count);
            if(memory->flags){
                read_leb_uint32(p, p_end, maximum);
            }
            memory->max_page_count = maximum;
            memory->num_bytes_per_page = DEFAULT_NUM_BYTES_PER_PAGE;
            
        }

    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load memory section success.\n");
    return true;
fail:
    return false;
}