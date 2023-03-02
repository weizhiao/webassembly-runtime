#include "loader.h"

bool
load_export_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 export_count, i, j, index;
    uint64 total_size;
    uint32 str_len;
    WASMExport *export;
    const char *name;

    read_leb_uint32(p, p_end, export_count);

    if (export_count) {
        module->export_count = export_count;
        total_size = sizeof(WASMExport) * (uint64)export_count;
        if (!(module->exports = wasm_runtime_malloc(total_size))) {
            return false;
        }

        export = module->exports;
        for (i = 0; i < export_count; i++, export ++) {
            read_leb_uint32(p, p_end, str_len);

            for (j = 0; j < i; j++) {
                name = module->exports[j].name;
                if (strlen(name) == str_len && memcmp(name, p, str_len) == 0) {
                    set_error_buf(error_buf, error_buf_size,
                                  "duplicate export name");
                    return false;
                }
            }

            if (!load_utf8_str(
                      &p, str_len, &export->name, error_buf,
                      error_buf_size)) {
                return false;
            }
            export->kind = read_uint8(p);
            read_leb_uint32(p, p_end, index);
            export->index = index;

            switch (export->kind) {
                /* function index */
                case EXPORT_KIND_FUNC:
                    if (index >= module->function_count
                                     + module->import_function_count) {
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown function");
                        return false;
                    }
                    break;
                case EXPORT_KIND_TABLE:
                    if (index
                        >= module->table_count + module->import_table_count) {
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown table");
                        return false;
                    }
                    break;
                /* memory index */
                case EXPORT_KIND_MEMORY:
                    if (index
                        >= module->memory_count + module->import_memory_count) {
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown memory");
                        return false;
                    }
                    break;
                /* global index */
                case EXPORT_KIND_GLOBAL:
                    if (index
                        >= module->global_count + module->import_global_count) {
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown global");
                        return false;
                    }
                    break;
                default:
                    set_error_buf(error_buf, error_buf_size,
                                  "invalid export kind");
                    return false;
            }
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load export section success.\n");
    return true;
fail:
    return false;
}