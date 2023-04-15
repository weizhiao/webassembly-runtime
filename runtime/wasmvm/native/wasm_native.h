#ifndef _WASM_NATIVE_H
#define _WASM_NATIVE_H

#include "wasm_type.h"
#include "wasm_exec_env.h"

typedef struct NativeSymbol
{
    const char *symbol;
    void *func_ptr;
    const char *signature;
    void *attachment;
} NativeSymbol;

typedef struct NativeSymbolsNode
{
    struct NativeSymbolsNode *next;
    const char *module_name;
    NativeSymbol *native_symbols;
    uint32 n_native_symbols;
} NativeSymbolsNode, *NativeSymbolsList;

bool wasm_native_init();

void *
wasm_native_resolve_symbol(const char *module_name, const char *field_name,
                           const WASMType *func_type, const char **p_signature);

void 
wasm_native_destroy();

bool 
wasm_runtime_invoke_native(WASMExecEnv *exec_env, const WASMFunction *func, uint32 *argv,
                                uint32 *argv_ret);

#endif /* end of _WASM_NATIVE_H */
