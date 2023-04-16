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

#if WASM_ENABLE_LIBC_WASI != 0
    /*
     * WASI reactor instances may assume that _initialize will be called by
     * the environment at most once, and that none of their other exports
     * are accessed before that call.
     */
    initialize_func =
        wasm_lookup_function(module, "_initialize");
#endif

    if (!start_func && !initialize_func)
    {
        /* No post instantiation functions to call */
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

    /* Execute start function for both main insance and sub instance */
    if (start_func && !wasm_call_function(exec_env, start_func, 0, NULL))
    {
        goto fail;
    }

    ret = true;

fail:
    // wasm_exec_env_destroy(exec_env);
    return ret;
}

bool execute_func(WASMModule *module_inst, const char *name,
                  int32 argc, char *argv[])
{
    WASMFunction *target_func;
    WASMType *type = NULL;
    WASMExecEnv *exec_env = NULL;
    uint32 argc1, *argv1 = NULL, cell_num = 0, j, k = 0;
    int32 i, p;
    uint64 total_size;
    const char *exception;
    char buf[128];

    LOG_DEBUG("call a function \"%s\" with %d arguments", name, argc);

    if (!(target_func =
              wasm_lookup_function(module_inst, name)))
    {
        snprintf(buf, sizeof(buf), "lookup function %s failed", name);
        wasm_set_exception(module_inst, buf);
        goto fail;
    }

    type = target_func->func_type;

    if (type->param_count != (uint32)argc)
    {
        wasm_set_exception(module_inst, "invalid input argument count");
        goto fail;
    }

    argc1 = type->param_cell_num;
    cell_num = (argc1 > type->ret_cell_num) ? argc1 : type->ret_cell_num;

    total_size = sizeof(uint32) * (uint64)(cell_num > 2 ? cell_num : 2);
    if (!(argv1 = wasm_runtime_malloc((uint32)total_size)))
    {
        goto fail;
    }

    /* Parse arguments */
    for (i = 0, p = 0; i < argc; i++)
    {
        char *endptr = NULL;
        if (argv[i][0] == '\0')
        {
            snprintf(buf, sizeof(buf), "invalid input argument %" PRId32, i);
            wasm_set_exception(module_inst, buf);
            goto fail;
        }
        switch (type->param[i])
        {
        case VALUE_TYPE_I32:
            argv1[p++] = (uint32)strtoul(argv[i], &endptr, 0);
            break;
        case VALUE_TYPE_I64:
        {
            union
            {
                uint64 val;
                uint32 parts[2];
            } u;
            u.val = strtoull(argv[i], &endptr, 0);
            argv1[p++] = u.parts[0];
            argv1[p++] = u.parts[1];
            break;
        }
        case VALUE_TYPE_F32:
        {
            float32 f32 = strtof(argv[i], &endptr);
            // if (isnan(f32)) {
            //     if (argv[i][0] == '-') {
            //         union ieee754_float u;
            //         u.f = f32;
            //         if (is_little_endian())
            //             u.ieee.ieee_little_endian.negative = 1;
            //         else
            //             u.ieee.ieee_big_endian.negative = 1;
            //         bh_memcpy_s(&f32, sizeof(float), &u.f, sizeof(float));
            //     }
            //     if (endptr[0] == ':') {
            //         uint32 sig;
            //         union ieee754_float u;
            //         sig = (uint32)strtoul(endptr + 1, &endptr, 0);
            //         u.f = f32;
            //         if (is_little_endian())
            //             u.ieee.ieee_little_endian.mantissa = sig;
            //         else
            //             u.ieee.ieee_big_endian.mantissa = sig;
            //         bh_memcpy_s(&f32, sizeof(float), &u.f, sizeof(float));
            //     }
            // }
            memcpy(&argv1[p], &f32,
                   (uint32)sizeof(float));
            p++;
            break;
        }
        case VALUE_TYPE_F64:
        {
            union
            {
                float64 val;
                uint32 parts[2];
            } u;
            u.val = strtod(argv[i], &endptr);
            // if (isnan(u.val)) {
            //     if (argv[i][0] == '-') {
            //         union ieee754_double ud;
            //         ud.d = u.val;
            //         if (is_little_endian())
            //             ud.ieee.ieee_little_endian.negative = 1;
            //         else
            //             ud.ieee.ieee_big_endian.negative = 1;
            //         bh_memcpy_s(&u.val, sizeof(double), &ud.d,
            //                     sizeof(double));
            //     }
            //     if (endptr[0] == ':') {
            //         uint64 sig;
            //         union ieee754_double ud;
            //         sig = strtoull(endptr + 1, &endptr, 0);
            //         ud.d = u.val;
            //         if (is_little_endian()) {
            //             ud.ieee.ieee_little_endian.mantissa0 = sig >> 32;
            //             ud.ieee.ieee_little_endian.mantissa1 = (uint32)sig;
            //         }
            //         else {
            //             ud.ieee.ieee_big_endian.mantissa0 = sig >> 32;
            //             ud.ieee.ieee_big_endian.mantissa1 = (uint32)sig;
            //         }
            //         bh_memcpy_s(&u.val, sizeof(double), &ud.d,
            //                     sizeof(double));
            //     }
            // }
            argv1[p++] = u.parts[0];
            argv1[p++] = u.parts[1];
            break;
        }
        default:
            break;
        }
        if (endptr && *endptr != '\0' && *endptr != '_')
        {
            snprintf(buf, sizeof(buf), "invalid input argument %" PRId32 ": %s",
                     i, argv[i]);
            wasm_set_exception(module_inst, buf);
            goto fail;
        }
    }

    wasm_set_exception(module_inst, NULL);

    exec_env = wasm_exec_env_create(module_inst);

    if (!exec_env)
    {
        wasm_set_exception(module_inst,
                           "create singleton exec_env failed");
        goto fail;
    }

    if (!wasm_call_function(exec_env, target_func, argc1, argv1))
    {
        goto fail;
    }

    /* print return value */
    for (j = 0; j < type->result_count; j++)
    {
        switch (type->result[j])
        {
        case VALUE_TYPE_I32:
        {
            os_printf("0x%" PRIx32 ":i32", argv1[k]);
            k++;
            break;
        }
        case VALUE_TYPE_I64:
        {
            union
            {
                uint64 val;
                uint32 parts[2];
            } u;
            u.parts[0] = argv1[k];
            u.parts[1] = argv1[k + 1];
            k += 2;
            os_printf("0x%" PRIx64 ":i64", u.val);
            break;
        }
        case VALUE_TYPE_F32:
        {
            os_printf("%.7g:f32", *(float32 *)(argv1 + k));
            k++;
            break;
        }
        case VALUE_TYPE_F64:
        {
            union
            {
                float64 val;
                uint32 parts[2];
            } u;
            u.parts[0] = argv1[k];
            u.parts[1] = argv1[k + 1];
            k += 2;
            os_printf("%.7g:f64", u.val);
            break;
        }
        default:
            break;
        }
        if (j < (uint32)(type->result_count - 1))
            os_printf(",");
    }
    os_printf("\n");

    wasm_runtime_free(argv1);
    return true;

fail:
    if (argv1)
        wasm_runtime_free(argv1);
    if (exec_env)
        wasm_runtime_free(exec_env);

    exception = wasm_get_exception(module_inst);
    os_printf("%s\n", exception);
    return false;
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