#include "instantiate.h"

bool
tables_instantiate(WASMModule *module)
{
    uint32 i, length, default_max_size, cur_size, max_size;
    uint32 table_count = module->import_table_count + module->table_count;
    uint64 total_size = 0;
    WASMTable *table, *tables;
    WASMElement *elements, *element;
    uint32 *table_data;
    WASMGlobal *globals;

    globals = module->globals;
    table = tables = module->tables;
    elements = module->elements;

    for (i = 0; i < table_count; i++, table++) {
        total_size = 0;
        cur_size = table->cur_size;
        max_size = table->max_size;
        default_max_size = cur_size * 2 > TABLE_MAX_SIZE ? 
                            cur_size * 2 : TABLE_MAX_SIZE;

        if (max_size == (uint32)-1) {
            max_size = default_max_size;
        }
        else {
            max_size = max_size < default_max_size ? max_size : default_max_size;
        }

        total_size = sizeof(uint32) * (table->possible_grow
                                 ? max_size
                                 : cur_size);
        
        if(!(table_data = wasm_runtime_malloc(total_size))){
            goto fail;
        }

        memset(table_data, -1, (uint32)total_size);

        //赋值
        table->max_size = max_size;
        table->table_data = table_data;
    }

    element = elements;

    for (i = 0; i < module->element_count; i++, element++) {
        table = tables + element->table_index;
        length = element->function_count;
        table_data = table->table_data;
        
        if (element->base_offset.init_expr_type
            == INIT_EXPR_TYPE_GET_GLOBAL) {
            element->base_offset.u.i32 =
                globals[element->base_offset.u.global_index]
                    .initial_value.i32;
        }

        /* check offset since length might negative */
        if ((uint32)element->base_offset.u.i32 > table->cur_size) {
            LOG_DEBUG("base_offset(%d) > table->cur_size(%d)",
                      element->base_offset.u.i32, table->cur_size);
            wasm_set_exception(module,
                          "elements segment does not fit");
            goto fail;
        }


        if ((uint32)element->base_offset.u.i32 + length > table->cur_size) {
            LOG_DEBUG("base_offset(%d) + length(%d)> table->cur_size(%d)",
                      element->base_offset.u.i32, length, table->cur_size);
            wasm_set_exception(module,
                          "elements segment does not fit");
            goto fail;
        }

        memcpy(
            table_data + element->base_offset.u.i32,
            element->func_indexes,(uint32)(length * sizeof(uint32)));
    }

    module->table_count = table_count;
    
    LOG_VERBOSE("Instantiate table success.\n");
    return true;
fail:
    LOG_VERBOSE("Instantiate table fail.\n");
    wasm_set_exception(module, "Instantiate table fail.\n");
    return false;

}