#include "loader.h"

static bool
load_table(const uint8 **p_buf, const uint8 *buf_end, WASMTable *table,
           char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end, *p_org;
    table->elem_type = read_uint8(p);

    p_org = p;
    read_leb_uint1(p, p_end, table->flags);

    read_leb_uint32(p, p_end, table->init_size);

    if (table->flags) {
        read_leb_uint32(p, p_end, table->max_size);
        if (!check_table_max_size(table->init_size, table->max_size, error_buf,
                                  error_buf_size))
            return false;
    }

    adjust_table_max_size(table->init_size, table->flags, &table->max_size);

    *p_buf = p;
    return true;
fail:
    return false;
}

bool
load_table_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                   char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 table_count, i;
    uint64 total_size;
    WASMTable *table;

    read_leb_uint32(p, p_end, table_count);

    if (table_count) {
        module->table_count = table_count;
        total_size = sizeof(WASMTable) * (uint64)table_count;
        if (!(module->tables = wasm_runtime_malloc(total_size))) {
            return false;
        }

        /* load each table */
        table = module->tables;
        for (i = 0; i < table_count; i++, table++)
            if (!load_table(&p, p_end, table, error_buf, error_buf_size))
                return false;
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load table section success.\n");
    return true;
fail:
    return false;
}