#include "wasm_memory.h"
#include "wasm_exception.h"

void *
wasm_runtime_malloc(uint64 size)
{
    return os_malloc((uint32)size);
}

void wasm_runtime_free(void *ptr)
{
    os_free(ptr);
}

void *
wasm_runtime_realloc(void *ptr, uint32 size)
{
    return os_realloc(ptr, size);
}

bool wasm_enlarge_memory(WASMModule *module, uint32 inc_page_count)
{
    WASMMemory *memory = module->memories;
    uint8 *memory_data_old, *memory_data_new;
    uint32 num_bytes_per_page, total_size_old;
    uint32 cur_page_count, max_page_count, total_page_count;
    uint64 total_size_new;
    bool ret = true;

    memory_data_old = memory->memory_data;
    total_size_old = memory->memory_data_size;

    num_bytes_per_page = memory->num_bytes_per_page;
    cur_page_count = memory->cur_page_count;
    max_page_count = memory->max_page_count;
    total_page_count = inc_page_count + cur_page_count;
    total_size_new = num_bytes_per_page * (uint64)total_page_count;

    if (inc_page_count <= 0)
        /* No need to enlarge memory */
        return true;

    if (total_page_count > max_page_count)
    {
        return false;
    }

    if (total_size_new > UINT32_MAX)
    {
        /* Resize to 1 page with size 4G-1 */
        num_bytes_per_page = UINT32_MAX;
        total_page_count = max_page_count = 1;
        total_size_new = UINT32_MAX;
    }

    if (!(memory_data_new =
              wasm_runtime_realloc(memory_data_old, (uint32)total_size_new)))
    {
        return false;
    }

    memset(memory_data_new + total_size_old, 0,
           (uint32)total_size_new - total_size_old);

    memory->num_bytes_per_page = num_bytes_per_page;
    memory->cur_page_count = total_page_count;
    memory->max_page_count = max_page_count;
    memory->memory_data_size = (uint32)total_size_new;

    memory->memory_data = memory_data_new;
    memory->memory_data_end = memory_data_new + (uint32)total_size_new;

    return ret;
}

uint32
wasm_runtime_addr_native_to_app(WASMModule *module_inst_comm,
                                void *native_ptr)
{
    WASMModule *module_inst = (WASMModule *)module_inst_comm;
    WASMMemory *memory_inst;
    uint8 *addr = (uint8 *)native_ptr;

    memory_inst = module_inst->memories;
    if (!memory_inst)
    {
        return 0;
    }

    if (memory_inst->memory_data <= addr && addr < memory_inst->memory_data_end)
        return (uint32)(addr - memory_inst->memory_data);

    return 0;
}

bool wasm_runtime_validate_app_addr(WASMModule *module_inst_comm,
                                    uint32 app_offset, uint32 size)
{
    WASMModule *module_inst = module_inst_comm;
    WASMMemory *memory_inst;

    memory_inst = module_inst->memories;
    if (!memory_inst)
    {
        goto fail;
    }

    /* integer overflow check */
    if (app_offset > UINT32_MAX - size)
    {
        goto fail;
    }

    if (app_offset + size <= memory_inst->memory_data_size)
    {
        return true;
    }

fail:
    wasm_set_exception(module_inst, "out of bounds memory access");
    return false;
}

void *
wasm_runtime_addr_app_to_native(WASMModule *module_inst,
                                uint32 app_offset)
{
    WASMMemory *memory_inst;
    uint8 *addr;

    memory_inst = module_inst->memories;
    if (!memory_inst)
    {
        return NULL;
    }

    addr = memory_inst->memory_data + app_offset;

    if (memory_inst->memory_data <= addr && addr < memory_inst->memory_data_end)
    {
        return addr;
    }
    return NULL;
}

bool wasm_runtime_validate_native_addr(WASMModule *module_inst_comm,
                                       void *native_ptr, uint32 size)
{
    WASMModule *module_inst = (WASMModule *)module_inst_comm;
    WASMMemory *memory_inst;
    uint8 *addr = (uint8 *)native_ptr;

    memory_inst = module_inst->memories;
    if (!memory_inst)
    {
        goto fail;
    }

    /* integer overflow check */
    if ((uintptr_t)addr > UINTPTR_MAX - size)
    {
        goto fail;
    }

    if (memory_inst->memory_data <= addr && addr + size <= memory_inst->memory_data_end)
    {
        return true;
    }

fail:
    wasm_set_exception(module_inst, "out of bounds memory access");
    return false;
}

void wasm_module_destory(WASMModule *module)
{
    uint32 i, table_count, memory_count, import_function_count, define_function_count;
    WASMModuleStage module_stage = module->module_stage;
    WASMType *type;
    WASMTable *table;
    WASMMemory *memory;
    WASMFunction *function;

    table_count = module->table_count;
    memory_count = module->memory_count;
    import_function_count = module->import_function_count;

    switch (module_stage)
    {
    case Execute:
    case Instantiate:
        define_function_count = module->function_count - import_function_count;
        break;
    case Validate:
    case Load:
        define_function_count = module->function_count;
        break;
    }

    if (!module)
        return;

    switch (module_stage)
    {
    case Execute:
    case Instantiate:
        if (module->global_data)
        {
            wasm_runtime_free(module->global_data);
        }
        if (module->wasi_ctx)
        {
            wasm_runtime_free(module->wasi_ctx);
        }
        if (module->export_functions)
        {
            wasm_runtime_free(module->export_functions);
        }
        if (table_count)
        {
            table = module->tables;
            for (i = 0; i < table_count; i++, table++)
            {
                if (table->table_data)
                {
                    wasm_runtime_free(table->table_data);
                }
            }
        }
        if (memory_count)
        {
            memory = module->memories;
            for (i = 0; i < memory_count; i++, memory++)
            {
                if (memory->memory_data)
                {
                    wasm_runtime_free(memory->memory_data);
                }
            }
        }
    case Validate:
        // 清除跳转表
        function = module->functions + import_function_count;
        for (i = 0; i < define_function_count; i++, function++)
        {
            if (function->branch_table)
            {
                wasm_runtime_free(function->branch_table);
            }
        }
    case Load:
        // 清除type段
        if (module->types)
        {
            for (i = 0; i < module->type_count; i++)
            {
                type = module->types[i];
                if (type)
                {
                    if (type->ref_count > 1)
                    {
                        type->ref_count--;
                    }
                    else
                    {
                        wasm_runtime_free(type);
                    }
                }
            }
            wasm_runtime_free(module->types);
        }

        // 清除global段
        if (module->globals)
        {
            wasm_runtime_free(module->globals);
        }

        // 清除table
        if (module->tables)
        {
            wasm_runtime_free(module->tables);
        }

        // 清除memory
        if (module->memories)
        {
            wasm_runtime_free(module->memories);
        }

        // 清除function
        if (module->functions)
        {
            function = module->functions + import_function_count;
            for (i = 0; i < define_function_count; i++, function++)
            {
                if (function->local_offsets)
                {
                    wasm_runtime_free(function->local_offsets);
                }
                if (function->local_types)
                {
                    wasm_runtime_free(function->local_types);
                }
            }
            wasm_runtime_free(module->functions);
        }

        // 清除export
        if (module->exports)
            wasm_runtime_free(module->exports);

        // 清除element
        if (module->elements)
        {
            for (i = 0; i < module->element_count; i++)
            {
                if (module->elements[i].func_indexes)
                    wasm_runtime_free(module->elements[i].func_indexes);
            }
            wasm_runtime_free(module->elements);
        }

        // 清除data
        if (module->data_segments)
        {
            wasm_runtime_free(module->data_segments);
        }
        break;
    }

    wasm_runtime_free(module);
}

WASMModule *
wasm_module_create()
{
    WASMModule *module = wasm_runtime_malloc(sizeof(WASMModule));

    if (!module)
    {
        return NULL;
    }

    memset(module, 0, sizeof(WASMModule));

    module->module_stage = Load;
    module->start_function = (uint32)-1;

    return module;
}