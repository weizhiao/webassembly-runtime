#include "wasm_loader.h"

bool
load_type_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 type_count, param_count, result_count, i, j;
    uint32 param_cell_num, ret_cell_num;
    uint64 total_size;
    uint8 flag;
    WASMType *type, **types;

    read_leb_uint32(p, p_end, type_count);

    if (type_count) {
        total_size = sizeof(WASMType *) * (uint64)type_count;
        if (!(types = wasm_runtime_malloc(total_size))) {
            return false;
        }

        //用于在销毁时判断空指针
        memset(types, 0, total_size);

        //现在这是一个常量
        total_size = sizeof(WASMType);

        for (i = 0; i < type_count; i++) {
            flag = read_uint8(p);
            if (flag != 0x60) {
                wasm_set_exception(module, "invalid type flag");
                return false;
            }

            if (!(type = types[i] = wasm_runtime_malloc(total_size))) {
                return false;
            }

            read_leb_uint32(p, p_end, param_count);
            
            type->param = (uint8 *)p;
            p += param_count;

            read_leb_uint32(p, p_end, result_count);
            
            type->result = (uint8 *)p;
            p += result_count;

            type->ref_count = 1;
            type->param_count = (uint16)param_count;
            type->result_count = (uint16)result_count;

            for (j = 0; j < param_count; j++) {
                if (!is_value_type(type->param[j])) {
                    wasm_set_exception(module,
                                  "unknown param value type");
                    return false;
                }
            }

            for (j = 0; j < result_count; j++){
                if (!is_value_type(type->result[j])) {
                    wasm_set_exception(module,
                                  "unknown result value type");
                    return false;
                }
            }

            param_cell_num = wasm_get_cell_num(type->param, param_count);
            ret_cell_num = wasm_get_cell_num(type->result, result_count);
            if (param_cell_num > UINT16_MAX || ret_cell_num > UINT16_MAX) {
                wasm_set_exception(module,
                              "param count or result count too large");
                return false;
            }
            type->param_cell_num = (uint16)param_cell_num;
            type->ret_cell_num = (uint16)ret_cell_num;

            /* 将相同的类型用1种替换 */
            for (j = 0; j < i; j++) {
                if (wasm_type_equal(type, types[j])) {
                    if (types[j]->ref_count == UINT16_MAX) {
                        wasm_set_exception(module,
                                      "wasm type's ref count too large");
                        return false;
                    }
                    wasm_runtime_free(type);
                    types[i] = types[j];
                    types[j]->ref_count++;
                    break;
                }
            }
        }

        //赋值
        module->type_count = type_count;
        module->types = types;
    }

    if (p != p_end) {
        wasm_set_exception(module, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load type section success.\n");
    return true;
fail:
    LOG_VERBOSE("Load type section fail.\n");
    return false;
}