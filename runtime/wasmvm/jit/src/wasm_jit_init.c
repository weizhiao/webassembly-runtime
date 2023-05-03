#include "wasm_jit.h"
#include "wasm_jit_compiler.h"

static bool
init_func_type_indexes(WASMModule *module)
{
    uint32 i;
    uint32 all_function_count = module->function_count;
    uint64 total_size = (uint64)sizeof(uint32) * all_function_count;
    WASMType **wasm_types = module->types;

    /* Allocate memory */
    if (!(module->func_type_indexes =
              wasm_runtime_malloc(total_size)))
    {
        return false;
    }

    for (i = 0; i < all_function_count; i++)
    {
        WASMFunction *func_inst = module->functions + i;
        WASMType *func_type = func_inst->func_type;
        module->func_type_indexes[i] = wasm_get_cur_type_idx(wasm_types, func_type);
    }

    return true;
}

bool init_llvm_jit_functions_stage1(WASMModule *module)
{
    uint64 size;
    uint32 i;

    if (module->function_count == 0)
        return true;

    init_func_type_indexes(module);

    size = sizeof(void *) * (uint64)module->function_count;
    if (!(module->func_ptrs = wasm_runtime_malloc(size)))
    {
        return false;
    }
    memset(module->func_ptrs, 0, size);

    size = sizeof(bool) * (uint64)module->function_count;
    if (!(module->func_ptrs_compiled = wasm_runtime_malloc(size)))
    {
        return false;
    }
    memset(module->func_ptrs_compiled, 0, size);

    // 导入的函数是编译好的
    for (i = 0; i < module->import_function_count; i++)
    {
        module->func_ptrs[i] = module->functions[i].func_ptr;
        module->func_ptrs_compiled[i] = true;
    }

    module->comp_ctx = wasm_jit_create_comp_context(module);
    if (!module->comp_ctx)
    {
        return false;
    }

    return true;
}

bool init_llvm_jit_functions_stage2(WASMModule *module)
{
    char *wasm_jit_last_error;
    uint32 i, define_function_count;
    uint32 import_function_count;

    import_function_count = module->import_function_count;
    define_function_count = module->function_count - import_function_count;

    if (define_function_count == 0)
        return true;

    if (!wasm_jit_compile_wasm(module))
    {
        wasm_jit_last_error = wasm_jit_get_last_error();
        return false;
    }

    wasm_jit_emit_llvm_file(module->comp_ctx, "test.text");

    for (i = 0; i < define_function_count; i++)
    {
        LLVMOrcJITTargetAddress func_addr = 0;
        LLVMErrorRef error;
        char func_name[48];

        snprintf(func_name, sizeof(func_name), "%s%d", WASM_JIT_FUNC_PREFIX, i);
        error = LLVMOrcLLLazyJITLookup(module->comp_ctx->orc_jit, &func_addr,
                                       func_name);
        if (error != LLVMErrorSuccess)
        {
            char *err_msg = LLVMGetErrorMessage(error);
            LLVMDisposeErrorMessage(err_msg);
            return false;
        }

        /**
         * No need to lock the func_ptr[func_idx] here as it is basic
         * data type, the load/store for it can be finished by one cpu
         * instruction, and there can be only one cpu instruction
         * loading/storing at the same time.
         */
        module->func_ptrs[i + import_function_count] = (void *)func_addr;
    }

    return true;
}

static void *
orcjit_thread_callback(void *arg)
{
    OrcJitThreadArg *thread_arg = (OrcJitThreadArg *)arg;
    JITCompContext *comp_ctx = thread_arg->comp_ctx;
    WASMModule *module = thread_arg->module;
    uint32 group_idx = thread_arg->group_idx;
    uint32 group_stride = WASM_ORC_JIT_BACKEND_THREAD_NUM;
    uint32 define_func_count = module->function_count - module->import_function_count;
    uint32 import_func_count = module->import_function_count;
    uint32 i;

    for (i = group_idx; i < define_func_count;
         i += group_stride * WASM_ORC_JIT_COMPILE_THREAD_NUM)
    {
        LLVMOrcJITTargetAddress func_addr = 0;
        LLVMErrorRef error;
        char func_name[48];
        typedef void (*F)(void);
        union
        {
            F f;
            void *v;
        } u;
        uint32 j;

        snprintf(func_name, sizeof(func_name), "%s%d%s", WASM_JIT_FUNC_PREFIX, i,
                 "_wrapper");
        error = LLVMOrcLLLazyJITLookup(comp_ctx->orc_jit, &func_addr, func_name);
        if (error != LLVMErrorSuccess)
        {
            char *err_msg = LLVMGetErrorMessage(error);
            os_printf("failed to compile llvm jit function %u: %s", i, err_msg);
            LLVMDisposeErrorMessage(err_msg);
            break;
        }

        u.v = (void *)func_addr;
        u.f();

        for (j = 0; j < WASM_ORC_JIT_COMPILE_THREAD_NUM; j++)
        {
            if (i + j * group_stride < define_func_count)
            {
                module->func_ptrs_compiled[i + j * group_stride + import_func_count] = true;
            }
        }
    }

    return NULL;
}

bool compile_jit_functions(WASMModule *module)
{
    uint32 thread_num = WASM_ORC_JIT_BACKEND_THREAD_NUM;
    uint32 import_func_count = module->import_function_count;
    uint32 define_function_count = module->function_count - module->import_function_count;
    uint32 i, j;

    /* Create threads to compile the jit functions */
    for (i = 0; i < thread_num && i < define_function_count; i++)
    {
        module->orcjit_thread_args[i].comp_ctx = module->comp_ctx;
        module->orcjit_thread_args[i].module = module;
        module->orcjit_thread_args[i].group_idx = i;

        if (os_thread_create(&module->orcjit_threads[i], orcjit_thread_callback,
                             (void *)&module->orcjit_thread_args[i],
                             APP_THREAD_STACK_SIZE_DEFAULT) != 0)
        {
            /* Terminate the threads created */
            module->orcjit_stop_compiling = true;
            for (j = 0; j < i; j++)
            {
                os_thread_join(module->orcjit_threads[j], NULL);
            }
            return false;
        }
    }

    /* Wait until all jit functions are compiled for eager mode */
    for (i = 0; i < thread_num; i++)
    {
        if (module->orcjit_threads[i])
            os_thread_join(module->orcjit_threads[i], NULL);
    }

    /* Ensure all the llvm-jit functions are compiled */
    for (i = 0; i < module->function_count; i++)
    {
        if (!module->func_ptrs_compiled[i])
        {
            return false;
        }
    }

    return true;
}