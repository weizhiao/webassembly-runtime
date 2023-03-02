#include "loader.h"

bool
load_function_section(const uint8 *buf, const uint8 *buf_end,
                      WASMModule *module, char *error_buf,
                      uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 func_count;
    uint64 total_size;
    uint32 type_index, i;
    WASMFunction *func;

    read_leb_uint32(p, p_end, func_count);

    if (func_count) {
        module->function_count = func_count;
        total_size = sizeof(WASMFunction) * (uint64)func_count;
        if (!(module->functions = wasm_runtime_malloc(total_size))) {
            return false;
        }
        func = module->functions;
        for (i = 0; i < func_count; i++, func++) {
            read_leb_uint32(p, p_end, type_index);
            if (type_index >= module->type_count) {
                set_error_buf(error_buf, error_buf_size, "unknown type");
                return false;
            }
            /* 设置函数的类型 */
            func->func_type = module->types[type_index];
            func->param_cell_num = func->func_type->param_cell_num;
            func->ret_cell_num = func->func_type->ret_cell_num;
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load function section success.\n");
    return true;
fail:
    return false;
}