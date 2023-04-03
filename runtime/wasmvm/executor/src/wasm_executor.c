#include "wasm_interp.h"
#include "runtime_log.h"
#include "wasm_executor.h"
#include "wasm_exception.h"

bool
wasm_call_function(WASMExecEnv *exec_env, WASMFunction *function,
                   unsigned argc, uint32 argv[])
{
    WASMModuleInstance *module_inst = exec_env->module_inst;

    /* set thread handle and stack boundary */
    wasm_exec_env_set_thread_info(exec_env);

    wasm_interp_call_wasm(module_inst, exec_env, function, argc, argv);
    return !wasm_get_exception(module_inst) ? true : false;
}

bool
wasm_runtime_call_wasm(WASMExecEnv *exec_env,
                       WASMFunction *function, uint32 argc,
                       uint32 argv[])
{
    bool ret = false;
    uint32 *new_argv = NULL, param_argc;

    new_argv = argv;
    param_argc = argc;


    if (exec_env->module_inst->module_type == Wasm_Module_Bytecode)
        ret = wasm_call_function(exec_env, function,
                                 param_argc, new_argv);

    return ret;
}

WASMFunction *
wasm_runtime_lookup_wasi_start_function(WASMModule *module_inst)
{
    uint32 i;

    if (module_inst->module_type == Wasm_Module_Bytecode) {
        WASMModule *wasm_inst = module_inst;
        WASMFunction *func;
        for (i = 0; i < wasm_inst->export_func_count; i++) {
            if (!strcmp(wasm_inst->export_functions[i].name, "_start")) {
                func = wasm_inst->export_functions[i].function;
                if (func->param_count != 0
                    || func->result_count != 0) {
                    LOG_ERROR("Lookup wasi _start function failed: "
                              "invalid function type.\n");
                    return NULL;
                }
                return func;
            }
        }
        return NULL;
    }
    return NULL;
}

/**
 * Implementation of wasm_application_execute_main()
 */
static bool
check_main_func_type(const WASMType *type)
{
    if (!(type->param_count == 0 || type->param_count == 2)
        || type->result_count > 1) {
        LOG_ERROR(
            "WASM execute application failed: invalid main function type.\n");
        return false;
    }

    if (type->param_count == 2
        && !(type->param[0] == VALUE_TYPE_I32
             && type->param[1] == VALUE_TYPE_I32)) {
        LOG_ERROR(
            "WASM execute application failed: invalid main function type.\n");
        return false;
    }

    if (type->result_count
        && type->result[0] != VALUE_TYPE_I32) {
        LOG_ERROR(
            "WASM execute application failed: invalid main function type.\n");
        return false;
    }

    return true;
}

bool
execute_main(WASMModule *module_inst, int32 argc, char *argv[])
{
    WASMFunction *func;
    WASMType *func_type = NULL;
    WASMExecEnv *exec_env = NULL;
    uint32 argc1 = 0, argv1[2] = { 0 };
    uint32 total_argv_size = 0;
    uint64 total_size;
    uint32 argv_buf_offset = 0;
    int32 i;
    char *argv_buf, *p, *p_end;
    uint32 *argv_offsets, module_type;
    bool ret, is_import_func = true;

    exec_env = wasm_exec_env_create(module_inst, module_inst->default_wasm_stack_size);
    if (!exec_env) {
        wasm_set_exception(module_inst,
                                   "create exec_env failed");
        return false;
    }

    if ((func = wasm_runtime_lookup_wasi_start_function(module_inst))) {
        return wasm_runtime_call_wasm(exec_env, func, 0, NULL);
    }

    if (!(func = wasm_lookup_function(module_inst, "main", NULL))
        && !(func = wasm_lookup_function(module_inst,
                                                 "__main_argc_argv", NULL))
        && !(func = wasm_lookup_function(module_inst, "_main", NULL))) {
        wasm_set_exception(
            module_inst, "lookup the entry point symbol (like _start, main, "
                         "_main, __main_argc_argv) failed");
        return false;
    }

#if WASM_ENABLE_INTERP != 0
    if (module_inst->module_type == Wasm_Module_Bytecode) {
        is_import_func = func->func_kind;
    }
#endif

    if (is_import_func) {
        wasm_set_exception(module_inst, "lookup main function failed");
        return false;
    }

    module_type = module_inst->module_type;
    func_type = func->func_type;

    if (!func_type) {
        LOG_ERROR("invalid module instance type");
        return false;
    }

    if (!check_main_func_type(func_type)) {
        wasm_set_exception(module_inst,
                                   "invalid function type of main function");
        return false;
    }

    ret = wasm_runtime_call_wasm(exec_env, func, argc, argv);
    if (ret && func_type->result_count > 0 && argc > 0 && argv)
        /* copy the return value */
        *(int *)argv = (int)argv1[0];

    return ret;
}