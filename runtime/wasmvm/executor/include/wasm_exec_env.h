#ifndef _WASM_EXEC_ENV_H
#define _WASM_EXEC_ENV_H

#include "wasm_type.h"

#ifdef __cplusplus
extern "C" {
#endif


/* Execution environment */
typedef struct WASMExecEnv {
    /* Next thread's exec env of a WASM module instance. */
    struct WASMExecEnv *next;

    /* Previous thread's exec env of a WASM module instance. */
    struct WASMExecEnv *prev;

    /* Note: field module_inst, argv_buf, native_stack_boundary,
       suspend_flags, aux_stack_boundary, aux_stack_bottom, and
       native_symbol are used by AOTed code, don't change the
       places of them */

    /* The WASM module instance of current thread */
    WASMModule *module_inst;

    /* The boundary of native stack. When runtime detects that native
       frame may overrun this boundary, it throws stack overflow
       exception. */
    uint8 *native_stack_boundary;

    /* Used to terminate or suspend current thread
        bit 0: need to terminate
        bit 1: need to suspend
        bit 2: need to go into breakpoint
        bit 3: return from pthread_exit */
    union {
        uint32 flags;
        uintptr_t __padding__;
    } suspend_flags;

    /* Auxiliary stack boundary */
    union {
        uint32 boundary;
        uintptr_t __padding__;
    } aux_stack_boundary;

    /* Auxiliary stack bottom */
    union {
        uint32 bottom;
        uintptr_t __padding__;
    } aux_stack_bottom;

    /*
     * The lowest stack pointer value observed.
     * Assumption: native stack grows to the lower address.
     */
    uint8 *native_stack_top_min;

    /* attachment for native function */
    void *attachment;

    void *user_data;

    /* Current interpreter frame of current thread */
    struct WASMInterpFrame *cur_frame;

    /* The native thread handle of current thread */
    korp_tid handle;

    BlockAddr block_addr_cache[BLOCK_ADDR_CACHE_SIZE][BLOCK_ADDR_CONFLICT_SIZE];

    /* The WASM stack size */
    uint32 wasm_stack_size;

    /* The WASM stack of current thread */
    union {
        uint64 __make_it_8_byte_aligned_;

        struct {
            /* The top boundary of the stack. */
            uint8 *top_boundary;

            /* Top cell index which is free. */
            uint8 *top;

            /* The WASM stack. */
            uint8 bottom[1];
        } s;
    } wasm_stack;
} WASMExecEnv;


WASMExecEnv *
wasm_exec_env_create(WASMModule *module_inst,
                     uint32 stack_size);

void
wasm_exec_env_destroy(WASMExecEnv *exec_env);

static inline void *
wasm_exec_env_alloc_wasm_frame(WASMExecEnv *exec_env, unsigned size)
{
    uint8 *addr = exec_env->wasm_stack.s.top;

    /* For classic interpreter, the outs area doesn't contain the const cells,
       its size cannot be larger than the frame size, so here checking stack
       overflow with multiplying by 2 is enough. For fast interpreter, since
       the outs area contains const cells, its size may be larger than current
       frame size, we should check again before putting the function arguments
       into the outs area. */
    if (size * 2
        > (uint32)(uintptr_t)(exec_env->wasm_stack.s.top_boundary - addr)) {
        /* WASM stack overflow. */
        return NULL;
    }

    exec_env->wasm_stack.s.top += size;

#if WASM_ENABLE_MEMORY_PROFILING != 0
    {
        uint32 wasm_stack_used =
            exec_env->wasm_stack.s.top - exec_env->wasm_stack.s.bottom;
        if (wasm_stack_used > exec_env->max_wasm_stack_used)
            exec_env->max_wasm_stack_used = wasm_stack_used;
    }
#endif
    return addr;
}

static inline void
wasm_exec_env_free_wasm_frame(WASMExecEnv *exec_env, void *prev_top)
{
    exec_env->wasm_stack.s.top = (uint8 *)prev_top;
}

#ifdef __cplusplus
}
#endif

#endif /* end of _WASM_EXEC_ENV_H */
