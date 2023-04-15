#include "wasm_loader.h"

static bool
load_global_import(WASMModule *module, const uint8 **p_buf, const uint8 *buf_end, WASMGlobalImport *global)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint8 type;
    uint8 mutable;

    type = read_uint8(p);
    read_leb_uint1(p, p_end, mutable);

    *p_buf = p;

    global->type = type;
    global->is_mutable = mutable;

    return true;
fail:
    return false;
}

static bool
load_memory_import(WASMModule *module, const uint8 **p_buf, const uint8 *buf_end, WASMMemoryImport *memory)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint32 flag = 0;
    uint32 init_page_count = 0;
    uint32 max_page_count = 0;

    read_leb_uint1(p, p_end, flag);
    read_leb_uint32(p, p_end, init_page_count);

    if (flag & 1) {
        read_leb_uint32(p, p_end, max_page_count);
    }
    else{
        max_page_count = -1;
    }

    memory->cur_page_count = init_page_count;
    memory->max_page_count = max_page_count;
    memory->num_bytes_per_page = DEFAULT_NUM_BYTES_PER_PAGE;

    *p_buf = p;
    return true;
fail:
    return false;
}

static bool
load_function_import(WASMModule *module, const uint8 **p_buf, const uint8 *buf_end,
                     const WASMModule *parent_module,
                     const char *sub_module_name, const char *function_name,
                     WASMFunctionImport *function)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint32 type_index = 0;
    WASMType *type;

    read_leb_uint32(p, p_end, type_index);
    if (type_index >= parent_module->type_count) {
        wasm_set_exception(module, "unknown type");
        return false;
    }

    type = parent_module->types[type_index];

    //赋值
    function->module_name = sub_module_name;
    function->field_name = function_name;
    function->func_type = type;
    function->param_types = type->param;
    function->result_types = type->result;
    function->param_cell_num = type->param_cell_num;
    function->ret_cell_num = type->ret_cell_num;
    function->result_count = type->result_count;
    function->param_count = type->param_count;

    *p_buf = p;
    return true;
fail:
    return false;
}

static bool
load_table_import(WASMModule *module, const uint8 **p_buf, const uint8 *buf_end, WASMTableImport *table)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint32 elem_type = 0, flag = 0,
           init_size = 0, max_size = 0;

    elem_type = read_uint8(p);

    read_leb_uint1(p, p_end, flag);
    read_leb_uint32(p, p_end, init_size);

    if (flag) {
        read_leb_uint32(p, p_end, max_size);
    }
    else{
        max_size = -1;
    }

    *p_buf = p;

    table->elem_type = elem_type;
    table->cur_size = init_size;
    table->max_size = max_size;

    return true;
fail:
    return false;
}

bool
load_import_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 import_count, name_len, i;
    WASMFunctionImport *import_functions = module->functions;
    WASMTableImport *import_tables = module->tables;
    WASMMemoryImport *import_memories = module->memories;
    WASMGlobalImport *import_globals = module->globals;
    char *sub_module_name, *field_name;
    uint8 kind;

    read_leb_uint32(p, p_end, import_count);

    if (import_count) {

        for (i = 0; i < import_count; i++) {
            /* 加载 module name */
            read_leb_uint32(p, p_end, name_len);
            if (!load_utf8_str(module, &p, name_len, &sub_module_name)) {
                goto fail;
            }

            /* 加载 field name */
            read_leb_uint32(p, p_end, name_len);
            if (!load_utf8_str(module, &p, name_len, &field_name)) {
                goto fail;
            }

            kind = read_uint8(p);

            switch (kind) {
                case IMPORT_KIND_FUNC: /* import function */
                    if (!load_function_import(module,
                            &p, p_end, module, sub_module_name, field_name,
                            import_functions)) {
                        goto fail;
                    }
                    import_functions++;
                    break;

                case IMPORT_KIND_TABLE: /* import table */
                    if (!load_table_import(module, &p, p_end, import_tables)) {
                        LOG_DEBUG("can not import such a table (%s,%s)",
                                  sub_module_name, field_name);
                        goto fail;
                    }
                    import_globals++;
                    break;

                case IMPORT_KIND_MEMORY: /* import memory */
                    if (!load_memory_import(module, &p, p_end, import_memories)) {
                        goto fail;
                    }
                    import_memories++;
                    break;

                case IMPORT_KIND_GLOBAL: /* import global */
                    if (!load_global_import(module, &p, p_end,import_globals)) {
                        goto fail;
                    }
                    import_globals++;
                    break;

                default:
                    wasm_set_exception(module, "invalid import kind");
                    goto fail;
            }
        }
    }

    if (p != p_end) {
        wasm_set_exception(module, "section size mismatch");
        goto fail;
    }

    LOG_VERBOSE("Load import section success.\n");
    return true;
fail:
    LOG_VERBOSE("Load import section fail.\n");
    return false;
}