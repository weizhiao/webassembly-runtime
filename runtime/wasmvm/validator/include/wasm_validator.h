#ifndef _WASM_VALIDATOR_H
#define _WASM_VALIDATOR_H

#include "wasm_type.h"
#include "wasm_opcode.h"
#include "wasm_memory.h"
#include "wasm_exception.h"

typedef struct BlockType
{
    union
    {
        uint8 value_type;
        WASMType *type;
    } u;
    bool is_value_type;
} BlockType;

typedef struct BranchBlock
{
    uint8 label_type;
    BlockType block_type;
    uint8 *start_addr;
    uint8 *else_addr;
    uint8 *end_addr;
    // 以4字节为单位的计数
    uint32 stack_cell_num;
    // 以数量为单位的计数
    uint32 stack_num;

    uint32 *table_queue;
    uint32 *table_queue_bottom;
    uint32 table_queue_size;
    uint32 table_queue_num;
    uint32 branch_table_else_idx;
    uint32 branch_table_end_idx;

    bool is_stack_polymorphic;
} BranchBlock;

typedef struct WASMLoaderContext
{
    /* frame ref stack */
    uint8 *frame_ref;
    uint8 *frame_ref_bottom;
    uint32 frame_ref_size;
    uint32 stack_cell_num;
    uint32 max_stack_cell_num;
    uint32 stack_num;
    uint32 max_stack_num;

    /* frame csp stack */
    BranchBlock *frame_csp;
    BranchBlock *frame_csp_bottom;
    uint32 frame_csp_size;
    uint32 csp_num;
    uint32 max_csp_num;

    WASMBranchTable *branch_table;
    WASMBranchTable *branch_table_bottom;
    uint32 branch_table_num;
    uint32 branch_table_size;

} WASMLoaderContext;

#endif