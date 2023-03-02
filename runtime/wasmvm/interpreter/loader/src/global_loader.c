#include "loader.h"

bool
load_global_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 global_count, i;
    uint64 total_size;
    WASMGlobal *global;
    uint8 mutable;

    read_leb_uint32(p, p_end, global_count);

    if (global_count) {
        module->global_count = global_count;
        total_size = sizeof(WASMGlobal) * (uint64)global_count;
        if (!(module->globals = wasm_runtime_malloc(total_size))) {
            return false;
        }

        global = module->globals;

        for (i = 0; i < global_count; i++, global++) {
            global->type = read_uint8(p);
            read_leb_uint1(p, p_end, mutable);
            global->is_mutable = mutable ? true : false;

            if (!load_init_expr(&p, p_end, &(global->init_expr), global->type,
                                error_buf, error_buf_size))
                return false;

            if (INIT_EXPR_TYPE_GET_GLOBAL == global->init_expr.init_expr_type) {
                uint32 target_global_index = global->init_expr.u.global_index;
                if (target_global_index >= module->import_global_count) {
                    set_error_buf(error_buf, error_buf_size, "unknown global");
                    return false;
                }
            }
            else if (INIT_EXPR_TYPE_FUNCREF_CONST
                     == global->init_expr.init_expr_type) {
                // if (!check_function_index(module, global->init_expr.u.ref_index,
                //                           error_buf, error_buf_size)) {
                //     return false;
                // }
            }
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load global section success.\n");
    return true;
fail:
    return false;
}