#include "instantiate.h"
#include "wasm_native.h"

bool
functions_instantiate(WASMModule *module,
                    char *error_buf, uint32 error_buf_size)
{
    uint32 i, import_function_count, sum_function_count;
    WASMFunction *func;
    WASMType *type;
    void *linked_func = NULL;
    const char *linked_signature = NULL;
    void *linked_attachment = NULL;
    bool linked_call_conv_raw = false;
    const char *module_name, *field_name;

    import_function_count = module->import_function_count;
    sum_function_count = module->function_count + import_function_count;

    func = module->functions;

    for(i = 0; i < import_function_count; i++, func++){
        module_name = func->module_name;
        field_name = func->field_name;
        type = func->func_type;
        linked_func = wasm_native_resolve_symbol(
            module_name, field_name, type, &linked_signature,
            &linked_attachment, &linked_call_conv_raw);
        if (linked_func) {
            func->func_kind = Native_Func;
            func->func_ptr = linked_func;
        }
        else{
            func->func_kind = External_Func;
            func->func_ptr = NULL;
        }
        func->signature = linked_signature;
        func->attachment = linked_attachment;
    }

    module->function_count = sum_function_count;
    LOG_VERBOSE("Instantiate function success.\n");
    return true;
fail:
    LOG_VERBOSE("Instantiate function fail.\n");
    set_error_buf(error_buf, error_buf_size, "Instantiate function fail.\n");
    return false;
}