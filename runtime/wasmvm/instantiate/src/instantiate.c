#include "instantiate.h"
#include "wasm_runtime_instantiate_api.h"

bool
wasm_instantiate(WASMModule *module, uint32 value_stack_size, uint32 execution_stack_size)
{
    if (!module)
        return false;

    module->module_type = Wasm_Module_Bytecode;

    /* Instantiate memories/tables/functions */
    if (!globals_instantiate(module)
        || !memories_instantiate(module)
        || !tables_instantiate(module)
        || !export_instantiate(module)
        || !functions_instantiate(module)
        )
    {
        goto fail;
    }


    /* Initialize the thread related data */
    if (value_stack_size == 0)
        value_stack_size = DEFAULT_VALUE_STACK_SIZE;
    

    module->default_value_stack_size = value_stack_size;
    module->default_execution_stack_size = execution_stack_size;

    LOG_VERBOSE("Instantiate success.\n");
    return true;

fail:
    LOG_VERBOSE("Instantiate fail.\n");
    wasm_module_destory(module, Instantiate);
    return false;
}