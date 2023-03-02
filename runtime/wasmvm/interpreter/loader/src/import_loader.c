#include "loader.h"
#include "wasm_native.h"

static char *
get_import_type_str(const uint8 *str, uint32 len, char *error_buf, uint32 error_buf_size)
{
    if (!check_utf8_str(str, len)) {
        set_error_buf(error_buf, error_buf_size, "invalid UTF-8 encoding");
        return NULL;
    }

    if (len == 0) {
        return "";
    }

    char *c_str = (char *)str - 1;
    memmove(c_str, c_str + 1, len);
    c_str[len] = '\0';
    return c_str;
}

static bool
load_global_import(const uint8 **p_buf, const uint8 *buf_end, WASMGlobalImport *global, char *error_buf,
                   uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint8 type = 0;
    uint8 mutable = 0;
    bool ret = false;

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
load_memory_import(const uint8 **p_buf, const uint8 *buf_end, WASMMemoryImport *memory,
                   char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint32 flag = 0;
    uint32 init_page_count = 0;
    uint32 max_page_count = 0;

    read_leb_uint1(p, p_end, flag);
    read_leb_uint32(p, p_end, init_page_count);
    if (!check_memory_init_size(init_page_count, error_buf,
                                error_buf_size)) {
        return false;
    }

    if (flag & 1) {
        read_leb_uint32(p, p_end, max_page_count);
        if (!check_memory_max_size(init_page_count,
                                   max_page_count, error_buf,
                                   error_buf_size)) {
            return false;
        }
    }
    else{
        max_page_count = DEFAULT_MAX_PAGES;
    }

    memory->flags = flag;
    memory->init_page_count = init_page_count;
    memory->max_page_count = max_page_count;
    memory->num_bytes_per_page = DEFAULT_NUM_BYTES_PER_PAGE;

    *p_buf = p;
    return true;
fail:
    return false;
}

static bool
load_function_import(const uint8 **p_buf, const uint8 *buf_end,
                     const WASMModule *parent_module,
                     const char *sub_module_name, const char *function_name,
                     WASMFunctionImport *function, char *error_buf,
                     uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint32 declare_type_index = 0;
    WASMType *declare_func_type = NULL;
    void *linked_func = NULL;
    const char *linked_signature = NULL;
    void *linked_attachment = NULL;
    bool linked_call_conv_raw = false;
    bool is_native_symbol = false;

    read_leb_uint32(p, p_end, declare_type_index);
    *p_buf = p;

    if (declare_type_index >= parent_module->type_count) {
        set_error_buf(error_buf, error_buf_size, "unknown type");
        return false;
    }

    declare_func_type = parent_module->types[declare_type_index];

    linked_func = wasm_native_resolve_symbol(
        sub_module_name, function_name, declare_func_type, &linked_signature,
        &linked_attachment, &linked_call_conv_raw);
    if (linked_func) {
        is_native_symbol = true;
    }

    function->func_type = declare_func_type;
    function->func_ptr_linked = is_native_symbol ? linked_func : NULL;
    function->signature = linked_signature;
    function->attachment = linked_attachment;
    function->call_conv_raw = linked_call_conv_raw;
    return true;
fail:
    return false;
}

static bool
load_table_import(const uint8 **p_buf, const uint8 *buf_end, WASMTableImport *table,
                  char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint32 elem_type = 0, flag = 0,
           init_size = 0, max_size = 0;

    elem_type = read_uint8(p);

    read_leb_uint1(p, p_end, flag);
    read_leb_uint32(p, p_end, init_size);

    if (flag) {
        read_leb_uint32(p, p_end, max_size);
        if (!check_table_max_size(init_size, max_size,
                                  error_buf, error_buf_size))
            return false;
    }

    adjust_table_max_size(init_size, flag,
                          &max_size);

    *p_buf = p;

    table->elem_type = elem_type;
    table->init_size = init_size;
    table->flags = flag;
    table->max_size = max_size;

    return true;
fail:
    return false;
}

bool
load_import_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end, *p_old;
    uint32 import_count, name_len, type_index, i, u32, flags;
    uint64 total_size;
    WASMImport *import;
    WASMImport *import_functions = NULL, *import_tables = NULL;
    WASMImport *import_memories = NULL, *import_globals = NULL;
    char *sub_module_name, *field_name;
    uint8 u8, kind;

    read_leb_uint32(p, p_end, import_count);

    if (import_count) {
        module->import_count = import_count;
        total_size = sizeof(WASMImport) * (uint64)import_count;
        if (!(module->imports = wasm_runtime_malloc(total_size))) {
            return false;
        }

        p_old = p;

        /* 第一遍读取获得导入各种类型的个数*/
        for (i = 0; i < import_count; i++) {
            /* module name */
            read_leb_uint32(p, p_end, name_len);
            p += name_len;

            /* field name */
            read_leb_uint32(p, p_end, name_len);
            p += name_len;

            kind = read_uint8(p);

            switch (kind) {
                case IMPORT_KIND_FUNC: /* import function */
                    read_leb_uint32(p, p_end, type_index);
                    module->import_function_count++;
                    break;

                case IMPORT_KIND_TABLE: /* import table */
                    p++;
                    read_leb_uint1(p, p_end, flags);
                    read_leb_uint32(p, p_end, u32);
                    if (flags & 1)
                        read_leb_uint32(p, p_end, u32);
                    module->import_table_count++;
                    break;

                case IMPORT_KIND_MEMORY: /* import memory */
                    read_leb_uint1(p, p_end, flags);
                    read_leb_uint32(p, p_end, u32);
                    if (flags & 1)
                        read_leb_uint32(p, p_end, u32);
                    module->import_memory_count++;
                    break;

                case IMPORT_KIND_GLOBAL: /* import global */
                    p += 2;
                    module->import_global_count++;
                    break;

                default:
                    set_error_buf(error_buf, error_buf_size,
                                  "invalid import kind");
                    return false;
            }
        }

        if (module->import_function_count)
            import_functions = module->import_functions = module->imports;
        if (module->import_table_count)
            import_tables = module->import_tables =
                module->imports + module->import_function_count;
        if (module->import_memory_count)
            import_memories = module->import_memories =
                module->imports + module->import_function_count
                + module->import_table_count;
        if (module->import_global_count)
            import_globals = module->import_globals =
                module->imports + module->import_function_count
                + module->import_table_count + module->import_memory_count;

        p = p_old;

        for (i = 0; i < import_count; i++) {
            /* 加载 module name */
            read_leb_uint32(p, p_end, name_len);
            if (!(sub_module_name = get_import_type_str(
                p, name_len, error_buf, error_buf_size))) {
                return false;
            }
            p += name_len;

            /* 加载 field name */
            read_leb_uint32(p, p_end, name_len);
            if (!(field_name = get_import_type_str(
                p, name_len, error_buf, error_buf_size))) {
                return false;
            }
            p += name_len;

            kind = read_uint8(p);

            switch (kind) {
                case IMPORT_KIND_FUNC: /* import function */
                    import = import_functions++;
                    if (!load_function_import(
                            &p, p_end, module, sub_module_name, field_name,
                            &import->u.function, error_buf, error_buf_size)) {
                        return false;
                    }
                    break;

                case IMPORT_KIND_TABLE: /* import table */
                    import = import_tables++;
                    if (!load_table_import(&p, p_end, &import->u.table,
                                           error_buf, error_buf_size)) {
                        LOG_DEBUG("can not import such a table (%s,%s)",
                                  sub_module_name, field_name);
                        return false;
                    }
                    break;

                case IMPORT_KIND_MEMORY: /* import memory */
                    import = import_memories++;
                    if (!load_memory_import(&p, p_end, &import->u.memory,
                                            error_buf, error_buf_size)) {
                        return false;
                    }
                    break;

                case IMPORT_KIND_GLOBAL: /* import global */
                    import = import_globals++;
                    if (!load_global_import(&p, p_end, &import->u.global,
                                            error_buf, error_buf_size)) {
                        return false;
                    }
                    break;

                default:
                    set_error_buf(error_buf, error_buf_size,
                                  "invalid import kind");
                    return false;
            }
            import->kind = kind;
            import->module_name = sub_module_name;
            import->field_name = field_name;
        }

#if WASM_ENABLE_LIBC_WASI != 0
        import = module->import_functions;
        for (i = 0; i < module->import_function_count; i++, import++) {
            if (!strcmp(import->u.names.module_name, "wasi_unstable")
                || !strcmp(import->u.names.module_name,
                           "wasi_snapshot_preview1")) {
                module->import_wasi_api = true;
                break;
            }
        }
#endif
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load import section success.\n");
    (void)u8;
    (void)u32;
    (void)type_index;
    return true;
fail:
    return false;
}