#ifndef _WASM_NATIVE_H
#define _WASM_NATIVE_H

#include "wasm_type.h"

#define MAX_REG_FLOATS 8

#define MAX_REG_INTS 6

typedef void (*GenericFunctionPointer)();
void
invokeNative(GenericFunctionPointer f, uint64 *args, uint64 n_stacks);

typedef float64 (*Float64FuncPtr)(GenericFunctionPointer, uint64 *, uint64);
typedef float32 (*Float32FuncPtr)(GenericFunctionPointer, uint64 *, uint64);
typedef int64 (*Int64FuncPtr)(GenericFunctionPointer, uint64 *, uint64);
typedef int32 (*Int32FuncPtr)(GenericFunctionPointer, uint64 *, uint64);
typedef void (*VoidFuncPtr)(GenericFunctionPointer, uint64 *, uint64);

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

typedef struct NativeSymbol {
    const char *symbol;
    void *func_ptr;
    const char *signature;
    void *attachment;
} NativeSymbol;

typedef struct NativeSymbolsNode {
    struct NativeSymbolsNode *next;
    const char *module_name;
    NativeSymbol *native_symbols;
    uint32 n_native_symbols;
    bool call_conv_raw;
} NativeSymbolsNode, *NativeSymbolsList;

/**
 * Resolve native symbol in all libraries, including libc-builtin, libc-wasi,
 * base lib and extension lib, and user registered natives
 * function, which can be auto checked by vm before calling native function
 *
 * @param module_name the module name of the import function
 * @param func_name the function name of the import function
 * @param func_type the function prototype of the import function
 * @param p_signature output the signature if resolve success
 *
 * @return the native function pointer if success, NULL otherwise
 */
void *
wasm_native_resolve_symbol(const char *module_name, const char *field_name,
                           const WASMType *func_type, const char **p_signature,
                           void **p_attachment, bool *p_call_conv_raw);

bool
wasm_native_register_natives(const char *module_name,
                             NativeSymbol *native_symbols,
                             uint32 n_native_symbols);

bool
wasm_native_register_natives_raw(const char *module_name,
                                 NativeSymbol *native_symbols,
                                 uint32 n_native_symbols);

bool
wasm_native_unregister_natives(const char *module_name,
                               NativeSymbol *native_symbols);

void
wasm_native_destroy();

bool
wasm_runtime_invoke_native(WASMExecEnv *exec_env, const WASMFunction *func, uint32 *argv, uint32 argc,
                           uint32 *argv_ret);

#endif /* end of _WASM_NATIVE_H */
