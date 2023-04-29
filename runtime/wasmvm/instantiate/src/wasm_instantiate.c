#include "instantiate.h"
#include "wasm_runtime_instantiate_api.h"

#if WASM_ENABLE_JIT != 0
#include "wasm_jit_init.h"
#endif

bool wasm_instantiate(WASMModule *module, uint32 value_stack_size, uint32 execution_stack_size)
{
    module->module_stage = Instantiate;

    /* Instantiate memories/tables/functions */
    if (!globals_instantiate(module) || !memories_instantiate(module) || !tables_instantiate(module) || !export_instantiate(module) || !functions_instantiate(module))
    {
        goto fail;
    }

    /* Initialize the thread related data */
    if (value_stack_size == 0)
        value_stack_size = DEFAULT_VALUE_STACK_SIZE;

    module->default_value_stack_size = value_stack_size;
    module->default_execution_stack_size = execution_stack_size;

#if WASM_ENABLE_JIT != 0
    if (!init_llvm_jit_functions_stage1(module))
    {
        return false;
    }

    if (!init_llvm_jit_functions_stage2(module))
    {
        return false;
    }

    if (!compile_jit_functions(module))
    {
        return false;
    }
#endif

    LOG_VERBOSE("Instantiate success.\n");
    return true;

fail:
    LOG_VERBOSE("Instantiate fail.\n");
    return false;
}