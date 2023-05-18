#include "wasm_interp.h"
#include "runtime_log.h"
#include "wasm_executor.h"
#include "wasm_exception.h"
#include "wasm_memory.h"

static WASMFunction *
wasm_lookup_function(const WASMModule *module_inst, const char *name)
{
    uint32 i;
    for (i = 0; i < module_inst->export_func_count; i++)
        if (!strcmp(module_inst->export_functions[i].name, name))
            return module_inst->export_functions[i].function;
    return NULL;
}

static bool
wasm_call_function(WASMExecEnv *exec_env, WASMFunction *function,
                   unsigned argc, uint32 argv[])
{
    WASMModule *module_inst = exec_env->module_inst;

    wasm_interp_call_wasm(module_inst, exec_env, function, argc, argv);
    return !wasm_get_exception(module_inst) ? true : false;
}

WASMFunction *
wasm_runtime_lookup_wasi_start_function(WASMModule *module)
{
    uint32 i;
    WASMFunction *func;
    for (i = 0; i < module->export_func_count; i++)
    {
        if (!strcmp(module->export_functions[i].name, "_start"))
        {
            func = module->export_functions[i].function;
            if (func->param_count != 0 || func->result_count != 0)
            {
                LOG_ERROR("Lookup wasi _start function failed: "
                          "invalid function type.\n");
                return NULL;
            }
            return func;
        }
    }
    return NULL;
}

static bool
check_main_func_type(const WASMType *type)
{
    if (!(type->param_count == 0 || type->param_count == 2) || type->result_count > 1)
    {
        LOG_ERROR(
            "WASM execute application failed: invalid main function type.\n");
        return false;
    }

    if (type->param_count == 2 && !(type->param[0] == VALUE_TYPE_I32 && type->param[1] == VALUE_TYPE_I32))
    {
        LOG_ERROR(
            "WASM execute application failed: invalid main function type.\n");
        return false;
    }

    if (type->result_count && type->result[0] != VALUE_TYPE_I32)
    {
        LOG_ERROR(
            "WASM execute application failed: invalid main function type.\n");
        return false;
    }

    return true;
}

static bool
execute_post_instantiate_functions(WASMModule *module)
{
    WASMFunction *start_func = NULL;
    WASMFunction *initialize_func = NULL;
    WASMExecEnv *exec_env = NULL;
    bool ret = false;

    if (module->start_function != (uint32)-1)
    {
        start_func = module->functions + module->start_function;
    }

    initialize_func =
        wasm_lookup_function(module, "_initialize");

    if (!start_func && !initialize_func)
    {
        return true;
    }

    if (!exec_env && !(exec_env =
                           wasm_exec_env_create(module)))
    {
        wasm_set_exception(module, "allocate memory failed");
        return false;
    }

    if (initialize_func && !wasm_call_function(exec_env, initialize_func, 0, NULL))
    {
        goto fail;
    }

    if (start_func && !wasm_call_function(exec_env, start_func, 0, NULL))
    {
        goto fail;
    }

    ret = true;

fail:
    return ret;
}

bool execute_main(WASMModule *module_inst, int32 argc, char *argv[])
{
    WASMFunction *func;
    WASMType *func_type = NULL;
    WASMExecEnv *exec_env = NULL;
    bool ret;

    exec_env = wasm_exec_env_create(module_inst);
    if (!exec_env)
    {
        wasm_set_exception(module_inst,
                           "create exec_env failed");
        return false;
    }

    if ((func = wasm_runtime_lookup_wasi_start_function(module_inst)))
    {
        return wasm_call_function(exec_env, func, 0, NULL);
    }

    if (!(func = wasm_lookup_function(module_inst, "main")) && !(func = wasm_lookup_function(module_inst, "__main_argc_argv")) && !(func = wasm_lookup_function(module_inst, "_main")))
    {
        wasm_set_exception(
            module_inst, "lookup the entry point symbol (like _start, main, "
                         "_main, __main_argc_argv) failed");
        return false;
    }

    func_type = func->func_type;

    if (!check_main_func_type(func_type))
    {
        wasm_set_exception(module_inst,
                           "invalid function type of main function");
        return false;
    }

    ret = wasm_call_function(exec_env, func, argc, argv);

    return ret;
}