#include "loader.h"

bool
load_element_section(const uint8 *buf, const uint8 *buf_end,
                           WASMModule *module, char *error_buf,
                           uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 element_count, function_count, i, j;
    uint64 total_size;
    WASMElement *element;

    read_leb_uint32(p, p_end, element_count);

    if (element_count) {
        module->element_count = element_count;
        total_size = sizeof(WASMElement) * (uint64)element_count;
        if (!(module->elements = wasm_runtime_malloc(total_size))) {
            return false;
        }

        element = module->elements;
        for (i = 0; i < element_count; i++, element++) {
            if (p >= p_end) {
                set_error_buf(error_buf, error_buf_size,
                              "invalid value type or "
                              "invalid elements segment kind");
                return false;
            }
            read_leb_uint32(p, p_end, element->table_index);

            if (!load_init_expr(&p, p_end, &element->base_offset,
                                VALUE_TYPE_I32, error_buf, error_buf_size))
                return false;
            
            read_leb_uint32(p, p_end, function_count);

            element->function_count = function_count;
            if(function_count){
                total_size = function_count;
                if(!(element->func_indexes = wasm_runtime_malloc(total_size))){
                    return false;
                }

                for(j = 0; j < function_count; j++){
                    read_leb_uint32(p, p_end, element->func_indexes[j]);
                }
            }
            
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load element section success.\n");
    return true;
fail:
    return false;
}