#include "wasm_loader.h"

bool
init_load(const uint8 *buf, const uint8 *buf_end,
        WASMModule *module, char *error_buf,uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end, *section_end;
    uint8 id, flags, kind;
    uint32 payload_len, name_len, i;
    uint32 import_count, u32;
    uint64 total_size;

    while(p < p_end){
        read_leb_uint7(p, p_end, id);
        read_leb_uint32(p, p_end, payload_len);
        //目前不支持custom段
        if(id == SECTION_TYPE_USER){
            read_leb_uint32(p, p_end, name_len);
            if(name_len){
                p += name_len;
            }
        }

        section_end = p + payload_len;

        switch (id)
        {

        case SECTION_TYPE_IMPORT:
            read_leb_uint32(p, p_end, import_count);

            if (import_count) {

                /* 获得导入各种类型的个数*/
                for (i = 0; i < import_count; i++) {
                    /* module name */
                    read_leb_uint32(p, p_end, name_len);
                    p += name_len;

                    /* field name */
                    read_leb_uint32(p, p_end, name_len);
                    p += name_len;

                    kind = read_uint8(p);

                    switch (kind) {
                        case IMPORT_KIND_FUNC: /* import function */
                            read_leb_uint32(p, p_end, u32);
                            module->import_function_count++;
                            break;

                        case IMPORT_KIND_TABLE: /* import table */
                            p++;
                            read_leb_uint1(p, p_end, flags);
                            read_leb_uint32(p, p_end, u32);
                            if (flags & 1)
                                read_leb_uint32(p, p_end, u32);
                            module->import_table_count++;
                            break;

                        case IMPORT_KIND_MEMORY: /* import memory */
                            read_leb_uint1(p, p_end, flags);
                            read_leb_uint32(p, p_end, u32);
                            if (flags & 1)
                                read_leb_uint32(p, p_end, u32);
                            module->import_memory_count++;
                            break;

                        case IMPORT_KIND_GLOBAL: /* import global */
                            p += 2;
                            module->import_global_count++;
                            break;

                        default:
                            set_error_buf(error_buf, error_buf_size,
                                  "invalid import kind");
                    }   
                }
            }
            break;

        case SECTION_TYPE_FUNCTION:
            read_leb_uint32(p, p_end, u32);
            module->function_count = u32;
            break;
        
        case SECTION_TYPE_TABLE:
            read_leb_uint32(p, p_end, u32);
            module->table_count = u32;
            break;
        
        case SECTION_TYPE_MEMORY:
            read_leb_uint32(p, p_end, u32);
            module->memory_count = u32;
            break;
        case SECTION_TYPE_GLOBAL:
            read_leb_uint32(p, p_end, u32);
            module->global_count = u32;
            break;

        default:
            break;
        }

        p = section_end;
    }

    total_size = (module->global_count + module->import_global_count) * 
                    sizeof(WASMGlobal);
    if(!(module->globals = wasm_runtime_malloc(total_size))){
        return false;
    }

    total_size = (module->function_count + module->import_function_count) * 
                    sizeof(WASMFunction);
    if(!(module->functions = wasm_runtime_malloc(total_size))){
        return false;
    }

    //用于销毁function
    memset(module->functions, 0, total_size);

    total_size = (module->memory_count + module->import_memory_count) * 
                    sizeof(WASMMemory);
    if(!(module->memories = wasm_runtime_malloc(total_size))){
        return false;
    }

    memset(module->memories, 0, total_size);

    total_size = (module->table_count + module->import_table_count) * 
                    sizeof(WASMTable);
    if(!(module->tables = wasm_runtime_malloc(total_size))){
        return false;
    }
    
    memset(module->tables, 0, total_size);

    LOG_VERBOSE("Init load success.\n");
    return true;
fail:
    LOG_VERBOSE("Init load fail.\n");
    return false;
}