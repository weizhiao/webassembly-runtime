#include "loader.h"

bool
load_start_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                   char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    WASMType *type;
    uint32 start_function;

    read_leb_uint32(p, p_end, start_function);

    if (start_function
        >= module->function_count + module->import_function_count) {
        set_error_buf(error_buf, error_buf_size, "unknown function");
        return false;
    }

    if (start_function < module->import_function_count)
        type = module->import_functions[start_function].u.function.func_type;
    else
        type = module->functions[start_function - module->import_function_count]
                   .func_type;
    if (type->param_count != 0 || type->result_count != 0) {
        set_error_buf(error_buf, error_buf_size, "invalid start function");
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
    return false;
}