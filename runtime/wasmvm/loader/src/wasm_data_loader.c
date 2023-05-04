#include "wasm_loader.h"

bool load_data_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 data_seg_count, i, mem_index, data_seg_len;
    uint64 total_size;
    WASMDataSeg *dataseg;

    read_leb_uint32(p, p_end, data_seg_count);

    if (data_seg_count)
    {
        module->data_seg_count = data_seg_count;
        total_size = sizeof(WASMDataSeg) * (uint64)data_seg_count;
        if (!(dataseg = module->data_segments = wasm_runtime_malloc(total_size)))
        {
            wasm_set_exception(module, "malloc error");
            return false;
        }

        for (i = 0; i < data_seg_count; i++, dataseg++)
        {
            read_leb_uint32(p, p_end, mem_index);
            dataseg->memory_index = mem_index;

            if (!load_init_expr(module, &p, p_end, &dataseg->base_offset, VALUE_TYPE_I32))
                return false;

            read_leb_uint32(p, p_end, data_seg_len);

            dataseg->data_length = data_seg_len;
            dataseg->data = (uint8 *)p;
            p += data_seg_len;
        }
    }

    if (p != p_end)
    {
        wasm_set_exception(module, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load data section success.\n");
    return true;
fail:
    LOG_VERBOSE("Load data section fail.\n");
    return false;
}

bool load_datacount_section(const uint8 *buf, const uint8 *buf_end,
                            WASMModule *module)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 data_seg_count1 = 0;

    read_leb_uint32(p, p_end, data_seg_count1);
    module->data_seg_count1 = data_seg_count1;

    if (p != p_end)
    {
        wasm_set_exception(module, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load datacount section success.\n");
    return true;
fail:
    return false;
}