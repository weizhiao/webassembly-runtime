#include"wasm_memory.h"

void *
wasm_runtime_malloc(uint64 size)
{
    return os_malloc((uint32)size);
}

void
wasm_runtime_free(void *ptr)
{
    os_free(ptr);
}

void *
wasm_runtime_realloc(void *ptr, uint32 size)
{
    return os_realloc(ptr, size);
}

bool
wasm_enlarge_memory_internal(WASMModuleInstance *module, uint32 inc_page_count)
{
    WASMMemory *memory = module->memories;
    uint8 *memory_data_old, *memory_data_new, *heap_data_old;
    uint32 num_bytes_per_page, heap_size, total_size_old;
    uint32 cur_page_count, max_page_count, total_page_count;
    uint64 total_size_new;
    bool ret = true;

    if (!memory)
        return false;

    heap_data_old = memory->heap_data;
    heap_size = (uint32)(memory->heap_data_end - memory->heap_data);

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

    if (total_page_count < cur_page_count /* integer overflow */
        || total_page_count > max_page_count) {
        return false;
    }

    if (total_size_new > UINT32_MAX) {
        /* Resize to 1 page with size 4G-1 */
        num_bytes_per_page = UINT32_MAX;
        total_page_count = max_page_count = 1;
        total_size_new = UINT32_MAX;
    }

    if (!(memory_data_new =
              wasm_runtime_realloc(memory_data_old, (uint32)total_size_new))) {
        if (!(memory_data_new = wasm_runtime_malloc((uint32)total_size_new))) {
            return false;
        }
        if (memory_data_old) {
            memcpy(memory_data_new, memory_data_old, total_size_old);
            wasm_runtime_free(memory_data_old);
        }
    }

    memset(memory_data_new + total_size_old, 0,
           (uint32)total_size_new - total_size_old);

    memory->heap_data = memory_data_new + (heap_data_old - memory_data_old);
    memory->heap_data_end = memory->heap_data + heap_size;

    memory->num_bytes_per_page = num_bytes_per_page;
    memory->cur_page_count = total_page_count;
    memory->max_page_count = max_page_count;
    memory->memory_data_size = (uint32)total_size_new;

    memory->memory_data = memory_data_new;
    memory->memory_data_end = memory_data_new + (uint32)total_size_new;

#if WASM_ENABLE_FAST_JIT != 0 || WASM_ENABLE_JIT != 0 || WASM_ENABLE_AOT != 0
#if UINTPTR_MAX == UINT64_MAX
    memory->mem_bound_check_1byte.u64 = total_size_new - 1;
    memory->mem_bound_check_2bytes.u64 = total_size_new - 2;
    memory->mem_bound_check_4bytes.u64 = total_size_new - 4;
    memory->mem_bound_check_8bytes.u64 = total_size_new - 8;
    memory->mem_bound_check_16bytes.u64 = total_size_new - 16;
#else
    memory->mem_bound_check_1byte.u32[0] = (uint32)total_size_new - 1;
    memory->mem_bound_check_2bytes.u32[0] = (uint32)total_size_new - 2;
    memory->mem_bound_check_4bytes.u32[0] = (uint32)total_size_new - 4;
    memory->mem_bound_check_8bytes.u32[0] = (uint32)total_size_new - 8;
    memory->mem_bound_check_16bytes.u32[0] = (uint32)total_size_new - 16;
#endif
#endif

    return ret;
}

uint32
wasm_runtime_addr_native_to_app(WASMModule *module_inst_comm,
                                void *native_ptr)
{
    WASMModuleInstance *module_inst = (WASMModuleInstance *)module_inst_comm;
    WASMMemory *memory_inst;
    uint8 *addr = (uint8 *)native_ptr;

    memory_inst = module_inst->memories;
    if (!memory_inst) {
        return 0;
    }

    if (memory_inst->memory_data <= addr && addr < memory_inst->memory_data_end)
        return (uint32)(addr - memory_inst->memory_data);

    return 0;
}

bool
wasm_runtime_validate_native_addr(WASMModule *module_inst_comm,
                                  void *native_ptr, uint32 size)
{
    WASMModuleInstance *module_inst = (WASMModuleInstance *)module_inst_comm;
    WASMMemory *memory_inst;
    uint8 *addr = (uint8 *)native_ptr;

    memory_inst = module_inst->memories;
    if (!memory_inst) {
        goto fail;
    }

    /* integer overflow check */
    if ((uintptr_t)addr > UINTPTR_MAX - size) {
        goto fail;
    }

    if (memory_inst->memory_data <= addr
        && addr + size <= memory_inst->memory_data_end) {
        return true;
    }

fail:
    wasm_set_exception(module_inst, "out of bounds memory access");
    return false;
}

bool
wasm_enlarge_memory(WASMModuleInstance *module, uint32 inc_page_count)
{
    bool ret = false;

#if WASM_ENABLE_SHARED_MEMORY != 0
    WASMSharedMemNode *node =
        wasm_module_get_shared_memory((WASMModuleCommon *)module->module);
    if (node)
        os_mutex_lock(&node->shared_mem_lock);
#endif
    ret = wasm_enlarge_memory_internal(module, inc_page_count);
#if WASM_ENABLE_SHARED_MEMORY != 0
    if (node)
        os_mutex_unlock(&node->shared_mem_lock);
#endif

    return ret;
}

void 
wasm_module_destory(WASMModule *module, Stage stage)
{
    uint32 i, table_count, memory_count;
    WASMType *type;
    WASMTable *table;
    WASMMemory *memory;

    if (!module)
        return;

    //销毁实例化阶段额外创建的内存
    if (stage == Instantiate) {
        table_count = module->table_count;
        memory_count = module->memory_count;

        if(module->global_data){
            wasm_runtime_free(module->global_data);
        }
        if(module->wasi_ctx){
            wasm_runtime_free(module->wasi_ctx);
        }
        if(module->export_functions){
            wasm_runtime_free(module->export_functions);
        }
        if(table_count){
            table = module->tables;
            for(i = 0; i < table_count; i++, table++){
                if(table->table_data){
                    wasm_runtime_free(table->table_data);
                }
            }
        }
        if(memory_count){
            memory = module->memories;
            for(i = 0; i < memory_count; i++, memory++){
                if(memory->memory_data){
                    wasm_runtime_free(memory->memory_data);
                }
            }
        }
    }

    //清除type段
    if (module->types) {
        for (i = 0; i < module->type_count; i++) {
            type = module->types[i];
            if (type){
                if(type->ref_count > 1){
                    type->ref_count--;
                }
                else{
                    wasm_runtime_free(type);
                }
            }
        }
        wasm_runtime_free(module->types);
    }

    //清除global段
    if(module->globals){
        wasm_runtime_free(module->globals);
    }

    //清除table
    if(module->tables){
        wasm_runtime_free(module->tables);
    }

    //清除memory
    if(module->memories){
        wasm_runtime_free(module->memories);
    }

    //清除function
    if (module->functions) {
        for (i = 0; i < module->function_count; i++) {
            WASMFunction *func = module->functions + i;
            if (func->local_offsets) {
                wasm_runtime_free(func->local_offsets);
            }
            if (func->local_types) {
                wasm_runtime_free(func->local_types);
            }
        }
        wasm_runtime_free(module->functions);
    }

    //清除export
    if (module->exports)
        wasm_runtime_free(module->exports);

    //清除element
    if (module->elements) {
        for (i = 0; i < module->element_count; i++) {
            if (module->elements[i].func_indexes)
                wasm_runtime_free(module->elements[i].func_indexes);
        }
        wasm_runtime_free(module->elements);
    }

    //清除data
    if (module->data_segments) {
        wasm_runtime_free(module->data_segments);
    }

#if WASM_ENABLE_FAST_INTERP == 0
    if (module->br_table_cache_list) {
        BrTableCache *node = bh_list_first_elem(module->br_table_cache_list);
        BrTableCache *node_next;
        while (node) {
            node_next = bh_list_elem_next(node);
            wasm_runtime_free(node);
            node = node_next;
        }
    }
#endif
    wasm_runtime_free(module);
}

WASMModule *
create_module(char *error_buf, uint32 error_buf_size)
{
    WASMModule *module = wasm_runtime_malloc(sizeof(WASMModule));

    if (!module) {
        return NULL;
    }

    memset(module, 0, sizeof(WASMModule));

    module->module_type = Wasm_Module_Bytecode;
    module->start_function = (uint32)-1;

    return module;
}