#include "wasm_loader.h"
#include "wasm_runtime_loader_api.h"

WASMModule *
wasm_loader(uint8 *buf, uint32 size, char *error_buf, uint32 error_buf_size){
    uint32 magic_number, version, payload_len = 0, name_len = 0;
    const uint8 *p = buf, *p_end = p + size;
    const uint8 *section_data_start, *section_data_end;
    uint8 id;
    WASMModule *module = create_module(error_buf, error_buf_size);

    if(!module){
        return NULL;
    }

    magic_number = read_uint32(p);

    if (magic_number != WASM_MAGIC_NUMBER) {
        set_error_buf(error_buf, error_buf_size, "magic not detected");
        goto fail;
    }

    version = read_uint32(p);

    if (version != WASM_CURRENT_VERSION) {
        set_error_buf(error_buf, error_buf_size, "unknown version");
        goto fail;
    }

    if(!init_load(p, p_end, module, error_buf, error_buf_size)){
        goto fail;
    }

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
        
        section_data_start = p;
        section_data_end = p + payload_len;
        p = section_data_end;

        switch (id)
        {
        case SECTION_TYPE_TYPE:
            if(!load_type_section(section_data_start, section_data_end, 
                module, error_buf, error_buf_size)){
                    goto fail;
                }
            break;

        case SECTION_TYPE_IMPORT:
            if(!load_import_section(section_data_start, section_data_end, 
                module, error_buf, error_buf_size)){
                    goto fail;
                }
            break;

        case SECTION_TYPE_FUNCTION:
            if(!load_function_section(section_data_start, section_data_end, 
                module, error_buf, error_buf_size)){
                    goto fail;
                }
            break;
        
        case SECTION_TYPE_TABLE:
            if(!load_table_section(section_data_start, section_data_end, 
                module, error_buf, error_buf_size)){
                    goto fail;
                }
            break;
        
        case SECTION_TYPE_MEMORY:
            if(!load_memory_section(section_data_start, section_data_end, 
                module, error_buf, error_buf_size)){
                    goto fail;
                }
            break;

        case SECTION_TYPE_GLOBAL:
            if(!load_global_section(section_data_start, section_data_end, 
                module, error_buf, error_buf_size)){
                    goto fail;
                }
            break;
        
        case SECTION_TYPE_EXPORT:
            if(!load_export_section(section_data_start, section_data_end, 
                module, error_buf, error_buf_size)){
                    goto fail;
                }
            break;
        
        case SECTION_TYPE_START:
            if(!load_start_section(section_data_start, section_data_end, 
                module, error_buf, error_buf_size)){
                    goto fail;
                }
            break;

        case SECTION_TYPE_ELEMENT:
            if(!load_element_section(section_data_start, section_data_end, 
                module, error_buf, error_buf_size)){
                    goto fail;
                }
            break;
        
        case SECTION_TYPE_CODE:
            if(!load_code_section(section_data_start, section_data_end, 
                module, error_buf, error_buf_size)){
                    goto fail;
                }
            break;
        
        case SECTION_TYPE_DATA:
            if(!load_data_section(section_data_start, section_data_end, 
                module, error_buf, error_buf_size)){
                    goto fail;
                }
            break;

#if WASM_ENABLE_BULK_MEMORY != 0
        case SECTION_TYPE_DATACOUNT:
            

#endif

        default:
            break;
        }
    }

    LOG_VERBOSE("Load success.\n");
    return module;
fail:
    LOG_VERBOSE("Load fail.\n");
    wasm_module_destory(module, Load);
    return NULL;
}