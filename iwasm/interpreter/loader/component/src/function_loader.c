#include"function_loader.h"

static bool
load_function_section(const uint8 *buf, const uint8 *buf_end,
                      const uint8 *buf_code, const uint8 *buf_code_end,
                      WASMModule *module, char *error_buf,
                      uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    const uint8 *p_code = buf_code, *p_code_end, *p_code_save;
    uint32 func_count;
    uint64 total_size;
    uint32 code_count = 0, code_size, type_index, i, j, k, local_type_index;
    uint32 local_count, local_set_count, sub_local_count, local_cell_num;
    uint8 type;
    WASMFunction *func;

    read_leb_uint32(p, p_end, func_count);

    if (buf_code)
        read_leb_uint32(p_code, buf_code_end, code_count);

    if (func_count != code_count) {
        set_error_buf(error_buf, error_buf_size,
                      "function and code section have inconsistent lengths or "
                      "unexpected end");
        return false;
    }

    if (func_count) {
        module->function_count = func_count;
        total_size = sizeof(WASMFunction *) * (uint64)func_count;
        if (!(module->functions =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        for (i = 0; i < func_count; i++) {
            /* Resolve function type */
            read_leb_uint32(p, p_end, type_index);
            if (type_index >= module->type_count) {
                set_error_buf(error_buf, error_buf_size, "unknown type");
                return false;
            }

#if (WASM_ENABLE_WAMR_COMPILER != 0) || (WASM_ENABLE_JIT != 0)
            type_index = wasm_get_smallest_type_idx(
                module->types, module->type_count, type_index);
#endif

            read_leb_uint32(p_code, buf_code_end, code_size);
            if (code_size == 0 || p_code + code_size > buf_code_end) {
                set_error_buf(error_buf, error_buf_size,
                              "invalid function code size");
                return false;
            }

            /* Resolve local set count */
            p_code_end = p_code + code_size;
            local_count = 0;
            read_leb_uint32(p_code, buf_code_end, local_set_count);
            p_code_save = p_code;

            /* Calculate total local count */
            for (j = 0; j < local_set_count; j++) {
                read_leb_uint32(p_code, buf_code_end, sub_local_count);
                if (sub_local_count > UINT32_MAX - local_count) {
                    set_error_buf(error_buf, error_buf_size, "too many locals");
                    return false;
                }
                CHECK_BUF(p_code, buf_code_end, 1);
                /* 0x7F/0x7E/0x7D/0x7C */
                type = read_uint8(p_code);
                local_count += sub_local_count;
            }

            /* Alloc memory, layout: function structure + local types */
            code_size = (uint32)(p_code_end - p_code);

            total_size = sizeof(WASMFunction) + (uint64)local_count;
            if (!(func = module->functions[i] =
                      loader_malloc(total_size, error_buf, error_buf_size))) {
                return false;
            }

            /* Set function type, local count, code size and code body */
            func->func_type = module->types[type_index];
            func->local_count = local_count;
            if (local_count > 0)
                func->local_types = (uint8 *)func + sizeof(WASMFunction);
            func->code_size = code_size;
            /*
             * we shall make a copy of code body [p_code, p_code + code_size]
             * when we are worrying about inappropriate releasing behaviour.
             * all code bodies are actually in a buffer which user allocates in
             * his embedding environment and we don't have power on them.
             * it will be like:
             * code_body_cp = malloc(code_size);
             * memcpy(code_body_cp, p_code, code_size);
             * func->code = code_body_cp;
             */
            func->code = (uint8 *)p_code;

            /* Load each local type */
            p_code = p_code_save;
            local_type_index = 0;
            for (j = 0; j < local_set_count; j++) {
                read_leb_uint32(p_code, buf_code_end, sub_local_count);
                /* Note: sub_local_count is allowed to be 0 */
                if (local_type_index > UINT32_MAX - sub_local_count
                    || local_type_index + sub_local_count > local_count) {
                    set_error_buf(error_buf, error_buf_size,
                                  "invalid local count");
                    return false;
                }
                CHECK_BUF(p_code, buf_code_end, 1);
                /* 0x7F/0x7E/0x7D/0x7C */
                type = read_uint8(p_code);
                if (!is_value_type(type)) {
                    if (type == VALUE_TYPE_V128)
                        set_error_buf(error_buf, error_buf_size,
                                      "v128 value type requires simd feature");
                    else if (type == VALUE_TYPE_FUNCREF
                             || type == VALUE_TYPE_EXTERNREF)
                        set_error_buf(error_buf, error_buf_size,
                                      "ref value type requires "
                                      "reference types feature");
                    else
                        set_error_buf_v(error_buf, error_buf_size,
                                        "invalid local type 0x%02X", type);
                    return false;
                }
                for (k = 0; k < sub_local_count; k++) {
                    func->local_types[local_type_index++] = type;
                }
            }

            func->param_cell_num = func->func_type->param_cell_num;
            func->ret_cell_num = func->func_type->ret_cell_num;
            local_cell_num =
                wasm_get_cell_num(func->local_types, func->local_count);

            if (local_cell_num > UINT16_MAX) {
                set_error_buf(error_buf, error_buf_size,
                              "local count too large");
                return false;
            }

            func->local_cell_num = (uint16)local_cell_num;

            if (!init_function_local_offsets(func, error_buf, error_buf_size))
                return false;

            p_code = p_code_end;
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