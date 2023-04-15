#include "wasm_native.h"
#include "wasm_memory.h"
#include "wasm_exception.h"

static NativeSymbolsList g_native_symbols_list = NULL;

typedef void (*GenericFunctionPointer)();

typedef float64 (*Float64FuncPtr)(GenericFunctionPointer, uint64 *, uint64);
typedef float32 (*Float32FuncPtr)(GenericFunctionPointer, uint64 *, uint64);
typedef int64 (*Int64FuncPtr)(GenericFunctionPointer, uint64 *, uint64);
typedef int32 (*Int32FuncPtr)(GenericFunctionPointer, uint64 *, uint64);
typedef void (*VoidFuncPtr)(GenericFunctionPointer, uint64 *, uint64);

// 用于调用wasi
static void
invokeNative(void (*native_code)(), uint64 argv[], uint64 argc)
{

    switch (argc)
    {
    case 0:
        native_code();
        break;
    case 1:
        native_code(argv[0]);
        break;
    case 2:
        native_code(argv[0], argv[1]);
        break;
    case 3:
        native_code(argv[0], argv[1], argv[2]);
        break;
    case 4:
        native_code(argv[0], argv[1], argv[2], argv[3]);
        break;
    case 5:
        native_code(argv[0], argv[1], argv[2], argv[3], argv[4]);
        break;
    case 6:
        native_code(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]);
        break;
    case 7:
        native_code(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
                    argv[6]);
        break;
    case 8:
        native_code(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
                    argv[6], argv[7]);
        break;
    case 9:
        native_code(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
                    argv[6], argv[7], argv[8]);
        break;
    case 10:
        native_code(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
                    argv[6], argv[7], argv[8], argv[9]);
        break;
    case 11:
        native_code(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
                    argv[6], argv[7], argv[8], argv[9], argv[10]);
        break;
    case 12:
        native_code(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
                    argv[6], argv[7], argv[8], argv[9], argv[10], argv[11]);
        break;
    case 13:
        native_code(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
                    argv[6], argv[7], argv[8], argv[9], argv[10], argv[11],
                    argv[12]);
        break;
    case 14:
        native_code(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
                    argv[6], argv[7], argv[8], argv[9], argv[10], argv[11],
                    argv[12], argv[13]);
        break;
    case 15:
        native_code(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
                    argv[6], argv[7], argv[8], argv[9], argv[10], argv[11],
                    argv[12], argv[13], argv[14]);
        break;
    case 16:
        native_code(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
                    argv[6], argv[7], argv[8], argv[9], argv[10], argv[11],
                    argv[12], argv[13], argv[14], argv[15]);
        break;
    case 17:
        native_code(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
                    argv[6], argv[7], argv[8], argv[9], argv[10], argv[11],
                    argv[12], argv[13], argv[14], argv[15], argv[16]);
        break;
    case 18:
        native_code(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
                    argv[6], argv[7], argv[8], argv[9], argv[10], argv[11],
                    argv[12], argv[13], argv[14], argv[15], argv[16],
                    argv[17]);
        break;
    case 19:
        native_code(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
                    argv[6], argv[7], argv[8], argv[9], argv[10], argv[11],
                    argv[12], argv[13], argv[14], argv[15], argv[16],
                    argv[17], argv[18]);
        break;
    case 20:
        native_code(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
                    argv[6], argv[7], argv[8], argv[9], argv[10], argv[11],
                    argv[12], argv[13], argv[14], argv[15], argv[16],
                    argv[17], argv[18], argv[19]);
        break;
    default:
    {
        /* FIXME: If this happen, add more cases. */
        WASMExecEnv *exec_env = *(WASMExecEnv **)argv;
        WASMModuleInstance *module_inst = exec_env->module_inst;
        wasm_set_exception(
            module_inst,
            "the argument number of native function exceeds maximum");
        return;
    }
    }
}

static volatile Float64FuncPtr invokeNative_Float64 =
    (Float64FuncPtr)(uintptr_t)invokeNative;
static volatile Float32FuncPtr invokeNative_Float32 =
    (Float32FuncPtr)(uintptr_t)invokeNative;
static volatile Int64FuncPtr invokeNative_Int64 =
    (Int64FuncPtr)(uintptr_t)invokeNative;
static volatile Int32FuncPtr invokeNative_Int32 =
    (Int32FuncPtr)(uintptr_t)invokeNative;
static volatile VoidFuncPtr invokeNative_Void =
    (VoidFuncPtr)(uintptr_t)invokeNative;

uint32
get_libc_wasi_export_apis(NativeSymbol **p_libc_wasi_apis);

bool wasm_runtime_invoke_native(WASMExecEnv *exec_env, const WASMFunction *func, uint32 *argv, uint32 argc,
                                uint32 *argv_ret)
{
    WASMModule *module = exec_env->module_inst;
    uint64 argv_buf[32] = {0}, *argv1 = argv_buf, *stacks, size;
    uint32 *argv_src = argv, i, argc1, n_stacks = 0;
    uint32 result_count = func->result_count, param_count = func->param_count;
    uint32 ext_ret_count = result_count > 1 ? result_count - 1 : 0;
    bool ret = false;
    uint8 *param = func->param_types, *result = func->result_types;
    const char *signature = func->signature;
    void *func_ptr;
    uint32 arg_i32;
    uint64 arg_i64;

    argc1 = 1 + param_count + ext_ret_count;
    if (argc1 > sizeof(argv_buf) / sizeof(uint64))
    {
        size = sizeof(uint64) * (uint64)argc1;
        if (!(argv1 = wasm_runtime_malloc((uint32)size)))
        {
            return false;
        }
    }

    stacks = argv1;
    stacks[n_stacks++] = (uint64)(uintptr_t)exec_env;

    for (i = 0; i < param_count; i++, param++)
    {
        switch (*param)
        {
        case VALUE_TYPE_I32:
        case VALUE_TYPE_FUNCREF:
        {
            arg_i32 = *argv_src++;
            if (signature[i + 1] == '*') {
                arg_i64 = (uint64)wasm_runtime_addr_app_to_native(module, arg_i32);
            }
            else {
                arg_i64 = arg_i32;
            }
            stacks[n_stacks++] = arg_i64;
            break;
        }
        case VALUE_TYPE_I64:
            stacks[n_stacks++] = *(uint64 *)argv_src;
            argv_src += 2;
            break;
        case VALUE_TYPE_F32:
            *(float32 *)&stacks[n_stacks++] = *(float32 *)argv_src++;
            break;
        case VALUE_TYPE_F64:
            *(float64 *)&stacks[n_stacks++] = *(float64 *)argv_src;
            argv_src += 2;
            break;
        default:
            break;
        }
    }

    for (i = 0; i < ext_ret_count; i++)
    {
        stacks[n_stacks++] = *(uint64 *)argv_src;
        argv_src += 2;
    }

    func_ptr = func->func_ptr;
    if (result_count == 0)
    {
        invokeNative_Void(func_ptr, argv1, n_stacks);
    }
    else
    {
        switch (*result)
        {
        case VALUE_TYPE_I32:
        case VALUE_TYPE_FUNCREF:
            argv_ret[0] =
                (uint32)invokeNative_Int32(func_ptr, argv1, n_stacks);
            break;
        case VALUE_TYPE_I64:
            *(int64 *)(argv_ret) =
                (int64)(invokeNative_Int64(func_ptr, argv1, n_stacks));
            break;
        case VALUE_TYPE_F32:
            *(float32 *)(argv_ret) =
                (float32)invokeNative_Float32(func_ptr, argv1, n_stacks);
            break;
        case VALUE_TYPE_F64:
            *(float64 *)(argv_ret) =
                (float64)(invokeNative_Float64(func_ptr, argv1, n_stacks));
            break;
        default:
            break;
        }
    }

    ret = !wasm_get_exception(module) ? true : false;

    if (argv1 != argv_buf)
        wasm_runtime_free(argv1);

    return ret;
}

static bool
compare_type_with_signautre(uint8 type, const char signature)
{
    const char num_sig_map[] = {'F', 'f', 'I', 'i'};

    if (VALUE_TYPE_F64 <= type && type <= VALUE_TYPE_I32 && signature == num_sig_map[type - VALUE_TYPE_F64])
    {
        return true;
    }

#if WASM_ENABLE_REF_TYPES != 0
    if ('r' == signature && type == VALUE_TYPE_EXTERNREF)
        return true;
#endif

    /* TODO: a v128 parameter */
    return false;
}

static bool
check_symbol_signature(const WASMType *type, const char *signature)
{
    const char *p = signature, *p_end;
    char sig;
    uint32 i = 0;

    if (!p || strlen(p) < 2)
        return false;

    p_end = p + strlen(signature);

    if (*p++ != '(')
        return false;

    if ((uint32)(p_end - p) < (uint32)(type->param_count + 1))
        return false;

    for (i = 0; i < type->param_count; i++)
    {
        sig = *p++;

        if (compare_type_with_signautre(type->param[i], sig))
            continue;

        if (type->param[i] != VALUE_TYPE_I32)
            return false;

        if (sig == '*')
        {
            if (i + 1 < type->param_count && type->param[i + 1] == VALUE_TYPE_I32 && *p == '~')
            {
                i++;
                p++;
            }
        }
        else if (sig == '$')
        {
        }
        else
        {
            return false;
        }
    }

    if (*p++ != ')')
        return false;

    if (type->result_count)
    {
        if (p >= p_end)
            return false;

        if (!compare_type_with_signautre(type->result[0], *p))
            return false;

        p++;
    }

    if (*p != '\0')
        return false;

    return true;
}

static inline void
swap_symbol(NativeSymbol *left, NativeSymbol *right)
{
    NativeSymbol temp = *left;
    *left = *right;
    *right = temp;
}

static void
quick_sort_symbols(NativeSymbol *native_symbols, int left, int right)
{
    NativeSymbol base_symbol;
    int pin_left = left;
    int pin_right = right;

    if (left >= right)
    {
        return;
    }

    base_symbol = native_symbols[left];
    while (left < right)
    {
        while (left < right && strcmp(native_symbols[right].symbol, base_symbol.symbol) > 0)
        {
            right--;
        }

        if (left < right)
        {
            swap_symbol(&native_symbols[left], &native_symbols[right]);
            left++;
        }

        while (left < right && strcmp(native_symbols[left].symbol, base_symbol.symbol) < 0)
        {
            left++;
        }

        if (left < right)
        {
            swap_symbol(&native_symbols[left], &native_symbols[right]);
            right--;
        }
    }
    native_symbols[left] = base_symbol;

    quick_sort_symbols(native_symbols, pin_left, left - 1);
    quick_sort_symbols(native_symbols, left + 1, pin_right);
}

static void *
lookup_symbol(NativeSymbol *native_symbols, uint32 n_native_symbols,
              const char *symbol, const char **p_signature)
{
    int low = 0, mid, ret;
    int high = (int32)n_native_symbols - 1;

    while (low <= high)
    {
        mid = (low + high) / 2;
        ret = strcmp(symbol, native_symbols[mid].symbol);
        if (ret == 0)
        {
            *p_signature = native_symbols[mid].signature;
            return native_symbols[mid].func_ptr;
        }
        else if (ret < 0)
            high = mid - 1;
        else
            low = mid + 1;
    }

    return NULL;
}

void *
wasm_native_resolve_symbol(const char *module_name, const char *field_name,
                           const WASMType *func_type, const char **p_signature)
{
    NativeSymbolsNode *node, *node_next;
    const char *signature = NULL;
    void *func_ptr = NULL;

    node = g_native_symbols_list;
    while (node)
    {
        node_next = node->next;
        if (!strcmp(node->module_name, module_name))
        {
            if (!(func_ptr = lookup_symbol(node->native_symbols, node->n_native_symbols,
                                         field_name, &signature)))
                return NULL;
        }
        node = node_next;
    }

    if (!func_type || !check_symbol_signature(func_type, signature))
    {
        return NULL;
    }
    else
    {
        *p_signature = signature;
    }

    return func_ptr;
}

static bool
wasm_native_register_natives(const char *module_name,
                             NativeSymbol *native_symbols,
                             uint32 n_native_symbols)
{
    NativeSymbolsNode *node;

    if (!(node = wasm_runtime_malloc(sizeof(NativeSymbolsNode))))
        return false;

    node->module_name = module_name;
    node->native_symbols = native_symbols;
    node->n_native_symbols = n_native_symbols;

    node->next = g_native_symbols_list;
    g_native_symbols_list = node;

    quick_sort_symbols(native_symbols, 0, (int)(n_native_symbols - 1));

    return true;
}

bool wasm_native_init()
{
    NativeSymbol *native_symbols;
    uint32 n_native_symbols;

    n_native_symbols = get_libc_wasi_export_apis(&native_symbols);
    if (!wasm_native_register_natives("wasi_unstable", native_symbols,
                                      n_native_symbols))
        goto fail;
    if (!wasm_native_register_natives("wasi_snapshot_preview1", native_symbols,
                                      n_native_symbols))
        goto fail;

    return true;
fail:
    wasm_native_destroy();
    return false;
}

void wasm_native_destroy()
{
    NativeSymbolsNode *node, *node_next;

    node = g_native_symbols_list;
    while (node)
    {
        node_next = node->next;
        wasm_runtime_free(node);
        node = node_next;
    }

    g_native_symbols_list = NULL;
}
