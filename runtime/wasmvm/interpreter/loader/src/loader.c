#include "loader.h"
#include "loader_export.h"

static WASMModule *
create_module(char *error_buf, uint32 error_buf_size)
{
    WASMModule *module = wasm_runtime_malloc(sizeof(WASMModule));

    if (!module) {
        return NULL;
    }

    module->module_type = Wasm_Module_Bytecode;

    module->start_function = (uint32)-1;

    return module;
}

WASMModule *
wasm_loader(uint8 *buf, uint32 size, char *error_buf, uint32 error_buf_size){
    uint32 magic_number, version, payload_len = 0, name_len = 0;
    const uint8 *p = buf, *p_end = p + size, *name = NULL;
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

    while(p < p_end){
        read_leb_uint7(p, p_end, id);
        read_leb_uint32(p, p_end, payload_len);
        //目前不支持custom段
        if(id == SECTION_TYPE_USER){
            read_leb_uint32(p, p_end, name_len);
            if(name_len){
                name = p;
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


        default:
            break;
        }
    }

    // for (i = 0; i < module->function_count; i++) {
    //     WASMFunction *func = module->functions[i];
    //     if (!wasm_loader_prepare_bytecode(module, func, i, error_buf,
    //                                       error_buf_size)) {
    //         return false;
    //     }

    //     if (i == module->function_count - 1
    //         && func->code + func->code_size != buf_code_end) {
    //         set_error_buf(error_buf, error_buf_size,
    //                       "code section size mismatch");
    //         return false;
    //     }
    // }

    // if (!module->possible_memory_grow) {
    //     WASMMemoryImport *memory_import;
    //     WASMMemory *memory;

    //     if (aux_data_end_global && aux_heap_base_global
    //         && aux_stack_top_global) {
    //         uint64 init_memory_size;
    //         uint32 shrunk_memory_size = align_uint(aux_heap_base, 8);

    //         if (module->import_memory_count) {
    //             memory_import = &module->import_memories[0].u.memory;
    //             init_memory_size = (uint64)memory_import->num_bytes_per_page
    //                                * memory_import->init_page_count;
    //             if (shrunk_memory_size <= init_memory_size) {
    //                 /* Reset memory info to decrease memory usage */
    //                 memory_import->num_bytes_per_page = shrunk_memory_size;
    //                 memory_import->init_page_count = 1;
    //                 LOG_VERBOSE("Shrink import memory size to %d",
    //                             shrunk_memory_size);
    //             }
    //         }
    //         if (module->memory_count) {
    //             memory = &module->memories[0];
    //             init_memory_size = (uint64)memory->num_bytes_per_page
    //                                * memory->init_page_count;
    //             if (shrunk_memory_size <= init_memory_size) {
    //                 /* Reset memory info to decrease memory usage */
    //                 memory->num_bytes_per_page = shrunk_memory_size;
    //                 memory->init_page_count = 1;
    //                 LOG_VERBOSE("Shrink memory size to %d", shrunk_memory_size);
    //             }
    //         }
    //     }

    //     if (module->import_memory_count) {
    //         memory_import = &module->import_memories[0].u.memory;
    //         if (memory_import->init_page_count < DEFAULT_MAX_PAGES)
    //             memory_import->num_bytes_per_page *=
    //                 memory_import->init_page_count;
    //         else
    //             memory_import->num_bytes_per_page = UINT32_MAX;

    //         if (memory_import->init_page_count > 0)
    //             memory_import->init_page_count = memory_import->max_page_count =
    //                 1;
    //         else
    //             memory_import->init_page_count = memory_import->max_page_count =
    //                 0;
    //     }
    //     if (module->memory_count) {
    //         memory = &module->memories[0];
    //         if (memory->init_page_count < DEFAULT_MAX_PAGES)
    //             memory->num_bytes_per_page *= memory->init_page_count;
    //         else
    //             memory->num_bytes_per_page = UINT32_MAX;

    //         if (memory->init_page_count > 0)
    //             memory->init_page_count = memory->max_page_count = 1;
    //         else
    //             memory->init_page_count = memory->max_page_count = 0;
    //     }
    // }

    //calculate_global_data_offset(module);

    return module;
fail:
    wasm_loader_unload(module);
    return NULL;
}