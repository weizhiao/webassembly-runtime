#ifndef _WASM_VALIDATOR_H
#define _WASM_VALIDATOR_h

#include "wasm_type.h"

typedef struct BlockType {
    /* Block type may be expressed in one of two forms:
     * either by the type of the single return value or
     * by a type index of module.
     */
    union {
        uint8 value_type;
        WASMType *type;
    } u;
    bool is_value_type;
} BlockType;

typedef struct BranchBlock {
    uint8 label_type;
    BlockType block_type;
    uint8 *start_addr;
    uint8 *else_addr;
    uint8 *end_addr;
    uint32 stack_cell_num;
#if WASM_ENABLE_FAST_INTERP != 0
    uint16 dynamic_offset;
    uint8 *code_compiled;
    BranchBlockPatch *patch_list;
    /* This is used to save params frame_offset of of if block */
    int16 *param_frame_offsets;
#endif

    /* Indicate the operand stack is in polymorphic state.
     * If the opcode is one of unreachable/br/br_table/return, stack is marked
     * to polymorphic state until the block's 'end' opcode is processed.
     * If stack is in polymorphic state and stack is empty, instruction can
     * pop any type of value directly without decreasing stack top pointer
     * and stack cell num. */
    bool is_stack_polymorphic;
} BranchBlock;

typedef struct WASMLoaderContext {
    /* frame ref stack */
    uint8 *frame_ref;
    uint8 *frame_ref_bottom;
    uint8 *frame_ref_boundary;
    uint32 frame_ref_size;
    uint32 stack_cell_num;
    uint32 max_stack_cell_num;

    /* frame csp stack */
    BranchBlock *frame_csp;
    BranchBlock *frame_csp_bottom;
    BranchBlock *frame_csp_boundary;
    uint32 frame_csp_size;
    uint32 csp_num;
    uint32 max_csp_num;

#if WASM_ENABLE_FAST_INTERP != 0
    /* frame offset stack */
    int16 *frame_offset;
    int16 *frame_offset_bottom;
    int16 *frame_offset_boundary;
    uint32 frame_offset_size;
    int16 dynamic_offset;
    int16 start_dynamic_offset;
    int16 max_dynamic_offset;

    /* preserved local offset */
    int16 preserved_local_offset;

    /* const buffer */
    uint8 *const_buf;
    uint16 num_const;
    uint16 const_cell_num;
    uint32 const_buf_size;

    /* processed code */
    uint8 *p_code_compiled;
    uint8 *p_code_compiled_end;
    uint32 code_compiled_size;
    /* If the last opcode will be dropped, the peak memory usage will be larger
     * than the final code_compiled_size, we record the peak size to ensure
     * there will not be invalid memory access during second traverse */
    uint32 code_compiled_peak_size;
#endif
} WASMLoaderContext;


bool wasm_validator(WASMModule *module, char *error_buf,
                             uint32 error_buf_size);

#endif