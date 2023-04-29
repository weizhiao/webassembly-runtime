#include "wasm_stack_validator.h"

bool wasm_loader_push_frame_ref(WASMModule *module, WASMLoaderContext *ctx, uint8 type)
{
    // 如果空间不够会增加16个位置
    if (ctx->stack_num == ctx->frame_ref_size)
    {
        ctx->frame_ref_bottom = wasm_runtime_realloc(ctx->frame_ref_bottom, ctx->frame_ref_size + 16);
        if (!ctx->frame_ref_bottom)
        {
            return false;
        }
        ctx->frame_ref_size += 16;
        ctx->frame_ref = ctx->frame_ref_bottom + ctx->stack_cell_num;
    }

    *ctx->frame_ref++ = type;
    // 增加个数,即使为void
    ctx->stack_num++;
    ctx->stack_cell_num += wasm_value_type_cell_num(type);

    if (ctx->stack_num > ctx->max_stack_num)
    {
        ctx->max_stack_num++;
    }
    if (ctx->stack_cell_num > ctx->max_stack_cell_num)
    {
        ctx->max_stack_cell_num = ctx->stack_cell_num;
    }
    return true;
}

bool wasm_loader_pop_frame_ref(WASMModule *module, WASMLoaderContext *ctx, uint8 type)
{
    BranchBlock *cur_block = ctx->frame_csp - 1;
    uint8 *frame_ref = ctx->frame_ref - 1;
    int32 available_stack_num =
        (int32)(ctx->stack_num - cur_block->stack_num);

    if (available_stack_num <= 0 && cur_block->is_stack_polymorphic)
        return true;

    if (available_stack_num <= 0)
    {
        wasm_set_exception(module, "type mismatch");
        return false;
    }

    CHECK_STACK_POP(*frame_ref, type);

    ctx->frame_ref--;
    // 减少个数
    ctx->stack_num--;
    ctx->stack_cell_num -= wasm_value_type_cell_num(type);

    return true;
}