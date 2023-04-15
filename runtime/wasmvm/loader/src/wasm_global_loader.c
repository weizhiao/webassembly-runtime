#include "wasm_loader.h"

bool
load_global_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 global_count, i;
    WASMGlobal *global;
    InitializerExpression expr;
    uint8 mutable;

    read_leb_uint32(p, p_end, global_count);

    if (global_count) {

        global = module->globals + module->import_global_count;

        for (i = 0; i < global_count; i++, global++) {
            global->type = read_uint8(p);
            read_leb_uint1(p, p_end, mutable);
            global->is_mutable = mutable ? true : false;

            if (!load_init_expr(module, &p, p_end, &expr, global->type))
                return false;

            memcpy(&global->initial_value, &expr.u, sizeof(WASMValue));

            if (INIT_EXPR_TYPE_GET_GLOBAL == expr.init_expr_type) {
                global->type = VALUE_TYPE_GLOBAL;
                uint32 target_global_index = expr.u.global_index;
                if (target_global_index >= module->import_global_count) {
                    wasm_set_exception(module, "unknown global");
                    return false;
                }
            }
            else if (INIT_EXPR_TYPE_FUNCREF_CONST
                     == expr.init_expr_type) {
                // if (!check_function_index(module, global->init_expr.u.ref_index,
                //                           )) {
                //     return false;
                // }
            }
        }
    }

    if (p != p_end) {
        wasm_set_exception(module, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load global section success.\n");
    return true;
fail:
    LOG_VERBOSE("Load global section fail.\n");
    return false;
}