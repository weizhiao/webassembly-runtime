#ifndef _WASM_INTERP_H
#define _WASM_INTERP_H

#include "wasm_type.h"
#include "wasm_exception.h"
#include "wasm_exec_env.h"

typedef struct WASMFuncFrame {
    /* The frame of the caller that are calling the current function. */
    struct WASMFuncFrame *prev_frame;

    WASMFunction *function;

    /* Instruction pointer of the bytecode array.  */
    uint8 *ip;

    uint32 frame_csp_num;

    uint32 *sp;
    uint32 *lp;
} WASMFuncFrame;

/**
 * Calculate the size of interpreter area of frame of a function.
 *
 * @param all_cell_num number of all cells including local variables
 * and the working stack slots
 *
 * @return the size of interpreter area of the frame
 */
static inline unsigned
wasm_interp_interp_frame_size(unsigned all_cell_num)
{
    unsigned frame_size;

    frame_size = (uint32)offsetof(WASMFuncFrame, lp) + all_cell_num * 4;
    return align_uint(frame_size, 4);
}

void
wasm_interp_call_wasm(WASMModule *module_inst, WASMExecEnv *exec_env,
                      WASMFunction *function, uint32 argc,
                      uint32 argv[]);


#endif /* end of _WASM_INTERP_H */
