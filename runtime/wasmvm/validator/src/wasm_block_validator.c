#include "wasm_block_validator.h"

bool wasm_validator_push_block(WASMLoaderContext *ctx, uint8 label_type,
                               BlockType block_type, uint8 *start_addr)
{
    BranchBlock *frame_csp;
    if (ctx->csp_num == ctx->frame_csp_size)
    {
        ctx->frame_csp_bottom = wasm_runtime_realloc(
            ctx->frame_csp_bottom,
            (ctx->frame_csp_size + 8) * sizeof(BranchBlock));
        if (!ctx->frame_csp_bottom)
        {
            return false;
        }
        ctx->frame_csp_size += 8;
        ctx->frame_csp = ctx->frame_csp_bottom + ctx->csp_num;
    }
    frame_csp = ctx->frame_csp;
    frame_csp->label_type = label_type;
    frame_csp->block_type = block_type;
    frame_csp->start_addr = start_addr;
    frame_csp->stack_num = ctx->stack_num;
    frame_csp->else_addr = NULL;
    frame_csp->end_addr = NULL;
    frame_csp->stack_cell_num = ctx->stack_cell_num;
    frame_csp->table_queue_num = 0;
    frame_csp->table_queue_size = 0;
    frame_csp->table_queue_bottom = NULL;

    if (label_type == LABEL_TYPE_LOOP)
    {
        frame_csp->branch_table_end_idx = ctx->branch_table_num;
    }

    ctx->frame_csp++;
    ctx->csp_num++;
    if (ctx->csp_num > ctx->max_csp_num)
    {
        ctx->max_csp_num = ctx->csp_num;
    }
    return true;
}

bool wasm_validator_pop_block(WASMModule *module, WASMLoaderContext *ctx)
{
    BranchBlock *frame_csp;
    WASMBranchTable *branch_table;
    uint8 *end_addr;
    uint32 i;
    if (ctx->csp_num < 1)
    {
        wasm_set_exception(module,
                           "type mismatch: "
                           "expect data but block stack was empty");
        return false;
    }

    frame_csp = ctx->frame_csp - 1;
    if (frame_csp->label_type != LABEL_TYPE_LOOP)
    {
        frame_csp->branch_table_end_idx = ctx->branch_table_num;
        end_addr = frame_csp->end_addr;
    }
    else
    {
        end_addr = frame_csp->start_addr;
    }

    for (i = 0; i < frame_csp->table_queue_num; i++)
    {
        branch_table = ctx->branch_table_bottom + frame_csp->table_queue_bottom[i];
        uint32 opcode = branch_table->idx;

        switch (opcode)
        {
        case WASM_OP_BR:
        case WASM_OP_BR_IF:
        case WASM_OP_BR_TABLE:
            branch_table->ip = end_addr;
            branch_table->idx = frame_csp->branch_table_end_idx;
            break;
        case WASM_OP_IF:
            branch_table->ip = frame_csp->else_addr;
            branch_table->idx = frame_csp->branch_table_else_idx;
            break;
        case WASM_OP_ELSE:
            branch_table->ip = end_addr;
            branch_table->idx = frame_csp->branch_table_end_idx;
            break;
        default:
            wasm_set_exception(module, "unsupport branch opcode");
            return false;
        }
    }

    if (frame_csp->table_queue_num)
    {
        wasm_runtime_free(frame_csp->table_queue_bottom);
    }

    ctx->frame_csp--;
    ctx->csp_num--;

    return true;
}