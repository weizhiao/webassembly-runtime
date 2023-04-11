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

    uint32 *table_stack;
    uint32 *table_stack_bottom;
    uint32 table_stack_size;
    uint32 table_stack_num;
    uint32 branch_table_end_idx;

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

    WASMBranchTable *branch_table;
    WASMBranchTable *branch_table_bottom;
    WASMBranchTable *branch_table_boundary;
    uint32 branch_table_num;
    uint32 branch_table_size;

} WASMLoaderContext;


bool wasm_validator(WASMModule *module, char *error_buf,
                             uint32 error_buf_size);

#endif