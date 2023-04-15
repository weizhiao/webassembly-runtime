#include "wasm_loader.h"

bool
load_code_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module)
{
    const uint8 *p = buf, *p_end = buf_end;
    const uint8 *code_end, *p_org;
    uint8 *local_types, *param_types;
    uint16 *local_offsets;
    WASMFunction * func;
    uint16 local_cell_num, local_offset;
    uint32 count, body_size, local_entry_count, local_count,
            sub_local_count, i, j, k, cur_local_idx, param_count;
    uint64 total_size;
    uint8 type;

    read_leb_uint32(p, p_end, count);
    
    if(module->function_count != count){
        wasm_set_exception(module, "code size mismatch");
        return false;
    }

    if(count){
        func = module->functions + module->import_function_count;
        for(i = 0; i < count; i++, func++){

            //初始化
            local_offset = 0;
            local_count = 0;
            local_cell_num = 0;
            cur_local_idx = 0;
            param_count = func->param_count;
            param_types = func->param_types;
            local_offsets = NULL;
            local_types = NULL;

            read_leb_uint32(p, p_end, body_size);
            code_end = p + body_size;

            read_leb_uint32(p, p_end, local_entry_count);

            p_org = p;
            for(j = 0; j < local_entry_count; j++){
                read_leb_uint32(p, p_end, sub_local_count);
                type = read_uint8(p);
                local_cell_num += (uint16)sub_local_count * wasm_value_type_cell_num(type);
                local_count += sub_local_count;
            }

            //初始化local_types
            total_size = local_count;

            if(total_size > 0 && !(local_types = wasm_runtime_malloc(total_size))){
                wasm_set_exception(module, "malloc error");
                goto fail;
            }

            p = p_org;
            
            for(j = 0; j < local_entry_count; j++){
                read_leb_uint32(p, p_end, sub_local_count);
                type = read_uint8(p);
                for(k = 0; k < sub_local_count; k++, cur_local_idx++){
                    local_types[cur_local_idx] = type;
                }
            }

            //初始化local_offsets
            total_size = (param_count + local_count) * sizeof(uint16);

            if(total_size > 0 && !(local_offsets = wasm_runtime_malloc(total_size))){
                wasm_set_exception(module, "malloc error");
                goto fail;
            }

            for(j = 0; j < param_count; j++){
                local_offsets[j] = local_offset;
                local_offset += wasm_value_type_cell_num(param_types[j]);
            }

            for(j =0; j < local_count; j++){
                local_offsets[j + param_count] = local_offset;
                local_offset += wasm_value_type_cell_num(local_types[j]);
            }

            //赋值
            func->local_count = local_count;
            func->local_cell_num = local_cell_num;
            func->func_ptr = (void *)p;
            func->local_offsets = local_offsets;
            func->local_types = local_types;
            func->code_end = (uint8*)code_end;

            p = code_end;
        }
    }

    if (p != p_end) {
        wasm_set_exception(module, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load code section success.\n");
    return true;
fail:
    LOG_VERBOSE("Load code section fail.\n");
    return false;
}