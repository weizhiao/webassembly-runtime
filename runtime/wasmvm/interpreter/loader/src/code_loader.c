#include "loader.h"

bool
load_code_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end, *code_end;
    WASMFunction * func;
    WASMLocal * local;
    uint16 local_cell_num = 0;
    uint32 count, body_size, local_count, i, j;
    uint64 total_size;

    read_leb_uint32(p, p_end, count);
    
    if(module->function_count != count){
        set_error_buf(error_buf, error_buf_size, "code size mismatch");
        return false;
    }

    if(count){
        func = module->functions;
        for(i = 0; i < count; i++, func++){
            read_leb_uint32(p, p_end, body_size);
            code_end = p + body_size;
            func->code_size = body_size;

            read_leb_uint32(p, p_end, local_count);
            func->local_count = local_count;

            if(local_count){
                total_size = sizeof(WASMLocal) * local_count;
                if(!(local = func->local_entry = wasm_runtime_malloc(total_size))){
                    return false;
                }
            }

            for(j = 0; j < local_count; j++, local++){
                read_leb_uint32(p, p_end, local->count);
                local->type = read_uint8(p);

                local_cell_num += (uint16)local->count * wasm_value_type_cell_num(local->type);

            }
            func->local_cell_num = local_cell_num;

            func->code = (uint8 *)p;
            p = code_end;
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load code section success.\n");
    return true;
fail:
    return false;
}