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

typedef struct WASMValidator
{
    uint8 *value_stack;
    uint8 *value_stack_bottom;
    uint32 value_stack_size;
    uint32 stack_cell_num;
    uint32 max_stack_cell_num;
    uint32 stack_num;
    uint32 max_stack_num;

    BranchBlock *block_stack;
    BranchBlock *block_stack_bottom;
    uint32 block_stack_size;
    uint32 block_stack_num;
    uint32 max_block_stack_num;

    WASMBranchTable *branch_table;
    WASMBranchTable *branch_table_bottom;
    uint32 branch_table_num;
    uint32 branch_table_size;

} WASMValidator;

#endif