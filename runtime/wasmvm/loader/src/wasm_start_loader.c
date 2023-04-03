#include "wasm_loader.h"

bool
load_start_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                   char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 start_function;

    read_leb_uint32(p, p_end, start_function);

    if (start_function
        >= module->function_count + module->import_function_count) {
        set_error_buf(error_buf, error_buf_size, "unknown function");
        return false;
    }

    module->start_function = start_function;

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load start section success.\n");
    return true;
fail:
    LOG_VERBOSE("Load start section fail.\n");
    return false;
}