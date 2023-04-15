#include "wasm_loader.h"

bool
load_table_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 table_count, i;
    uint32 cur_size, max_size;
    uint8 flag, elem_type;
    WASMTable *table;

    read_leb_uint32(p, p_end, table_count);

    if (table_count) {
        table = module->tables + module->import_table_count;
        for (i = 0; i < table_count; i++, table++){
            elem_type = read_uint8(p);
            read_leb_uint1(p, p_end, flag);
            read_leb_uint32(p, p_end, cur_size);

            if (flag) {
                read_leb_uint32(p, p_end, max_size);
            }
            else {
                //无最大值限制
                max_size = -1;
            }
            
            //赋值
            table->elem_type = elem_type;
            table->cur_size = cur_size;
            table->max_size = max_size;
        }
    }

    if (p != p_end) {
        wasm_set_exception(module, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load table section success.\n");
    return true;
fail:
    LOG_VERBOSE("Load table section fail.\n");
    return false;
}