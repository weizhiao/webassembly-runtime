#include "loader.h"

bool
load_data_section(const uint8 *buf, const uint8 *buf_end,
                          WASMModule *module, char *error_buf,
                          uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 data_seg_count, i, mem_index, data_seg_len;
    uint64 total_size;
    WASMDataSeg *dataseg;
    InitializerExpression init_expr;

    read_leb_uint32(p, p_end, data_seg_count);

    if (data_seg_count) {
        module->data_seg_count = data_seg_count;
        total_size = sizeof(WASMDataSeg) * (uint64)data_seg_count;
        if (!(dataseg = module->data_segments = wasm_runtime_malloc(total_size))) {
            return false;
        }

        for (i = 0; i < data_seg_count; i++, dataseg++) {
            read_leb_uint32(p, p_end, mem_index);
            dataseg->memory_index = mem_index;

            if (!load_init_expr(&p, p_end, &dataseg->base_offset, VALUE_TYPE_I32,
                                    error_buf, error_buf_size))
                return false;

            read_leb_uint32(p, p_end, data_seg_len);

            dataseg->data_length = data_seg_len;
            dataseg->data = (uint8 *)p;
            p += data_seg_len;
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load data section success.\n");
    return true;
fail:
    return false;
}