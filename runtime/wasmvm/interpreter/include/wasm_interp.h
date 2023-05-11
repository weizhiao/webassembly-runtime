#ifndef _WASM_INTERP_H
#define _WASM_INTERP_H

#include "wasm_type.h"
#include "wasm_exception.h"
#include "wasm_exec_env.h"

typedef struct WASMFuncFrame
{
    WASMFunction *function;

    uint8 *ip;

    uint32 *sp;
    uint32 *lp;

    WASMBranchTable *cur_branch_table;

} WASMFuncFrame;

void wasm_interp_call_wasm(WASMModule *module_inst, WASMExecEnv *exec_env,
                           WASMFunction *function, uint32 argc,
                           uint32 argv[]);

#endif /* end of _WASM_INTERP_H */
