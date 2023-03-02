#include "loader.h"

bool
load_type_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                  char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end, *p_org;
    uint32 type_count, param_count, result_count, i, j;
    uint32 param_cell_num, ret_cell_num;
    uint64 total_size;
    uint8 flag;
    WASMType *type;

    read_leb_uint32(p, p_end, type_count);

    if (type_count) {
        module->type_count = type_count;
        total_size = sizeof(WASMType *) * (uint64)type_count;
        if (!(module->types = wasm_runtime_malloc(total_size))) {
            return false;
        }

        //现在这是一个常量
        total_size = sizeof(WASMType);

        for (i = 0; i < type_count; i++) {
            flag = read_uint8(p);
            if (flag != 0x60) {
                set_error_buf(error_buf, error_buf_size, "invalid type flag");
                return false;
            }

            if (!(type = module->types[i] = wasm_runtime_malloc(total_size))) {
                return false;
            }

            read_leb_uint32(p, p_end, param_count);
            
            type->param = p;
            p += param_count;

            read_leb_uint32(p, p_end, result_count);
            
            type->result = p;
            p += result_count;

            type->ref_count = 1;
            type->param_count = (uint16)param_count;
            type->result_count = (uint16)result_count;

            for (j = 0; j < param_count; j++) {
                if (!is_value_type(type->param[j])) {
                    set_error_buf(error_buf, error_buf_size,
                                  "unknown param value type");
                    return false;
                }
            }

            for (j = 0; j < result_count; j++){
                if (!is_value_type(type->result[j])) {
                    set_error_buf(error_buf, error_buf_size,
                                  "unknown result value type");
                    return false;
                }
            }

            param_cell_num = wasm_get_cell_num(type->param, param_count);
            ret_cell_num = wasm_get_cell_num(type->result, result_count);
            if (param_cell_num > UINT16_MAX || ret_cell_num > UINT16_MAX) {
                set_error_buf(error_buf, error_buf_size,
                              "param count or result count too large");
                return false;
            }
            type->param_cell_num = (uint16)param_cell_num;
            type->ret_cell_num = (uint16)ret_cell_num;

            /* 将相同的类型用1种替换 */
            for (j = 0; j < i; j++) {
                if (wasm_type_equal(type, module->types[j])) {
                    if (module->types[j]->ref_count == UINT16_MAX) {
                        set_error_buf(error_buf, error_buf_size,
                                      "wasm type's ref count too large");
                        return false;
                    }
                    wasm_runtime_free(type);
                    module->types[i] = module->types[j];
                    module->types[j]->ref_count++;
                    break;
                }
            }
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load type section success.\n");
    return true;
fail:
    return false;
}