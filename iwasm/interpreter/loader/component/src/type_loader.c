#include "type_loader.h"
#include "read_leb.h"

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
        if (!(module->types =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        for (i = 0; i < type_count; i++) {
            CHECK_BUF(p, p_end, 1);
            flag = read_uint8(p);
            if (flag != 0x60) {
                set_error_buf(error_buf, error_buf_size, "invalid type flag");
                return false;
            }

            read_leb_uint32(p, p_end, param_count);

            /* Resolve param count and result count firstly */
            p_org = p;
            CHECK_BUF(p, p_end, param_count);
            p += param_count;
            read_leb_uint32(p, p_end, result_count);
            CHECK_BUF(p, p_end, result_count);
            p = p_org;

            if (param_count > UINT16_MAX || result_count > UINT16_MAX) {
                set_error_buf(error_buf, error_buf_size,
                              "param count or result count too large");
                return false;
            }

            total_size = offsetof(WASMType, types)
                         + sizeof(uint8) * (uint64)(param_count + result_count);
            if (!(type = module->types[i] =
                      loader_malloc(total_size, error_buf, error_buf_size))) {
                return false;
            }

            /* Resolve param types and result types */
            type->ref_count = 1;
            type->param_count = (uint16)param_count;
            type->result_count = (uint16)result_count;
            for (j = 0; j < param_count; j++) {
                CHECK_BUF(p, p_end, 1);
                type->types[j] = read_uint8(p);
            }
            read_leb_uint32(p, p_end, result_count);
            for (j = 0; j < result_count; j++) {
                CHECK_BUF(p, p_end, 1);
                type->types[param_count + j] = read_uint8(p);
            }
            for (j = 0; j < param_count + result_count; j++) {
                if (!is_value_type(type->types[j])) {
                    set_error_buf(error_buf, error_buf_size,
                                  "unknown value type");
                    return false;
                }
            }

            param_cell_num = wasm_get_cell_num(type->types, param_count);
            ret_cell_num =
                wasm_get_cell_num(type->types + param_count, result_count);
            if (param_cell_num > UINT16_MAX || ret_cell_num > UINT16_MAX) {
                set_error_buf(error_buf, error_buf_size,
                              "param count or result count too large");
                return false;
            }
            type->param_cell_num = (uint16)param_cell_num;
            type->ret_cell_num = (uint16)ret_cell_num;

            /* If there is already a same type created, use it instead */
            for (j = 0; j < i; j++) {
                if (wasm_type_equal(type, module->types[j])) {
                    if (module->types[j]->ref_count == UINT16_MAX) {
                        set_error_buf(error_buf, error_buf_size,
                                      "wasm type's ref count too large");
                        return false;
                    }
                    destroy_wasm_type(type);
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