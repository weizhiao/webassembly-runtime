#include "wasm_loader.h"

bool
load_export_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 export_count, i, index;
    uint64 total_size;
    uint32 str_len;
    WASMExport *export, *exports;

    read_leb_uint32(p, p_end, export_count);

    if (export_count) {
        total_size = sizeof(WASMExport) * (uint64)export_count;
        if (!(exports = wasm_runtime_malloc(total_size))) {
            return false;
        }

        export = exports;
        for (i = 0; i < export_count; i++, export++) {
            read_leb_uint32(p, p_end, str_len);

            if (!load_utf8_str(module, &p, str_len, &export->name)) {
                return false;
            }
            export->kind = read_uint8(p);
            read_leb_uint32(p, p_end, index);
            export->index = index;
        }

        module->exports = exports;
        module->export_count = export_count;
    }

    if (p != p_end) {
        wasm_set_exception(module, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load export section success.\n");
    return true;
fail:
    LOG_VERBOSE("Load export section fail.\n");
    return false;
}