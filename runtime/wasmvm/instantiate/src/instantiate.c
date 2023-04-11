#include "instantiate.h"
#include "wasm_runtime_instantiate_api.h"

bool
wasm_instantiate(WASMModule *module, uint32 value_stack_size, uint32 execution_stack_size
                , char *error_buf, uint32 error_buf_size)
{
    if (!module)
        return false;

    module->module_type = Wasm_Module_Bytecode;

    /* Instantiate memories/tables/functions */
    if (!globals_instantiate(module, error_buf,error_buf_size)
        || !(memories_instantiate(module, error_buf, error_buf_size))
        || !(tables_instantiate(module, error_buf, error_buf_size))
        || !(export_instantiate(module, error_buf, error_buf_size))
        || !(functions_instantiate(module, error_buf, error_buf_size))
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