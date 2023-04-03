#include "wasm_loader.h"

bool
load_function_section(const uint8 *buf, const uint8 *buf_end,
                      WASMModule *module, char *error_buf,
                      uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 func_count;
    uint32 type_index, i;
    WASMFunction *func;
    WASMType * type;

    read_leb_uint32(p, p_end, func_count);

    if (func_count) {
        func = module->functions + module->import_function_count;
        for (i = 0; i < func_count; i++, func++) {
            func->func_kind = Wasm_Func;
            read_leb_uint32(p, p_end, type_index);
            if (type_index >= module->type_count) {
                set_error_buf(error_buf, error_buf_size, "unknown type");
                return false;
            }
            type = module->types[type_index];

            /* 设置函数的类型 */
            func->func_type = type;
            func->param_types = type->param;
            func->result_types = type->result;
            func->param_cell_num = type->param_cell_num;
            func->ret_cell_num = type->ret_cell_num;
            func->result_count = type->result_count;
            func->param_count = type->param_count;
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load function section success.\n");
    return true;
fail:
    LOG_VERBOSE("Load function section fail.\n");
    return false;
}