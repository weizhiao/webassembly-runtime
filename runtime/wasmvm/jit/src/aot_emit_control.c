#include "wasm_jit_emit_control.h"
#include "wasm_jit_emit_exception.h"

static char *block_name_prefix[] = {"block", "loop", "if"};
static char *block_name_suffix[] = {"begin", "else", "end"};
static uint32 block_indexes[3] = {0};

static void inline destory_jit_block(JITBlock *block)
{
    if (block->param_phis)
        wasm_runtime_free(block->param_phis);
    if (block->else_param_phis)
        wasm_runtime_free(block->else_param_phis);
    if (block->result_phis)
        wasm_runtime_free(block->result_phis);
}

#define PUSH_JITBLOCK(block)           \
    do                                 \
    {                                  \
        func_ctx->block_stack = block; \
        func_ctx->block_stack++;       \
    } while (0)

#define RESAT_VALUE_BLOCK()                                                      \
    do                                                                           \
    {                                                                            \
        func_ctx->value_stack = func_ctx->value_stack_bottom + block->stack_num; \
    } while (0)

#define POP_JITBLOCK()                                    \
    do                                                    \
    {                                                     \
        JITBlock *_cur_block = func_ctx->block_stack - 1; \
        destory_jit_block(_cur_block);                    \
        func_ctx->block_stack--;                          \
    } while (0)

#define GET_CUR_JITBLOCK() func_ctx->block_stack - 1

#define BR_TARGET_JITBLOCK(br_depth) func_ctx->block_stack - br_depth - 1

#define HANDLE_POLYMORPHIC()                  \
    do                                        \
    {                                         \
        cur_block->is_polymorphic = true;     \
        if (cur_block->is_translate_else)     \
        {                                     \
            *frame_ip = cur_block->else_addr; \
        }                                     \
        else                                  \
        {                                     \
            *frame_ip = cur_block->end_addr;  \
        }                                     \
    } while (0)

/* clang-format off */
enum {
    LABEL_BEGIN = 0,
    LABEL_ELSE,
    LABEL_END
};
/* clang-format on */

static inline void
format_block_name(char *name, uint32 name_size, uint32 block_index,
                  uint32 label_type, uint32 label_id)
{
    if (label_type != LABEL_TYPE_FUNCTION)
        snprintf(name, name_size, "%s%d%s%s", block_name_prefix[label_type],
                 block_index, "_", block_name_suffix[label_id]);
    else
        snprintf(name, name_size, "%s", "func_end");
}

#define CREATE_BLOCK(new_llvm_block, name)                           \
    do                                                               \
    {                                                                \
        if (!(new_llvm_block = LLVMAppendBasicBlockInContext(        \
                  comp_ctx->context, func_ctx->func, name)))         \
        {                                                            \
            wasm_jit_set_last_error("add LLVM basic block failed."); \
            goto fail;                                               \
        }                                                            \
    } while (0)

#define CURR_BLOCK() LLVMGetInsertBlock(comp_ctx->builder)

#define MOVE_BLOCK_AFTER(llvm_block, llvm_block_after) \
    LLVMMoveBasicBlockAfter(llvm_block, llvm_block_after)

#define MOVE_BLOCK_AFTER_CURR(llvm_block) \
    LLVMMoveBasicBlockAfter(llvm_block, CURR_BLOCK())

#define MOVE_BLOCK_BEFORE(llvm_block, llvm_block_before) \
    LLVMMoveBasicBlockBefore(llvm_block, llvm_block_before)

#define BUILD_BR(llvm_block)                                  \
    do                                                        \
    {                                                         \
        if (!LLVMBuildBr(comp_ctx->builder, llvm_block))      \
        {                                                     \
            wasm_jit_set_last_error("llvm build br failed."); \
            goto fail;                                        \
        }                                                     \
    } while (0)

#define BUILD_COND_BR(value_if, block_then, block_else)               \
    do                                                                \
    {                                                                 \
        if (!LLVMBuildCondBr(comp_ctx->builder, value_if, block_then, \
                             block_else))                             \
        {                                                             \
            wasm_jit_set_last_error("llvm build cond br failed.");    \
            goto fail;                                                \
        }                                                             \
    } while (0)

#define SET_BUILDER_POS(llvm_block) \
    LLVMPositionBuilderAtEnd(comp_ctx->builder, llvm_block)

#define CREATE_RESULT_VALUE_PHIS(block)                                           \
    do                                                                            \
    {                                                                             \
        if (block->result_count && !block->result_phis)                           \
        {                                                                         \
            uint32 _i;                                                            \
            uint64 _size;                                                         \
            LLVMBasicBlockRef _block_curr = CURR_BLOCK();                         \
            /* Allocate memory */                                                 \
            _size = sizeof(LLVMValueRef) * (uint64)block->result_count;           \
            if (_size >= UINT32_MAX || !(block->result_phis =                     \
                                             wasm_runtime_malloc((uint32)_size))) \
            {                                                                     \
                wasm_jit_set_last_error("allocate memory failed.");               \
                goto fail;                                                        \
            }                                                                     \
            SET_BUILDER_POS(block->llvm_end_block);                               \
            for (_i = 0; _i < block->result_count; _i++)                          \
            {                                                                     \
                if (!(block->result_phis[_i] = LLVMBuildPhi(                      \
                          comp_ctx->builder,                                      \
                          TO_LLVM_TYPE(block->result_types[_i]), "phi")))         \
                {                                                                 \
                    wasm_jit_set_last_error("llvm build phi failed.");            \
                    goto fail;                                                    \
                }                                                                 \
            }                                                                     \
            SET_BUILDER_POS(_block_curr);                                         \
        }                                                                         \
    } while (0)

#define ADD_TO_RESULT_PHIS(block, value, idx)                              \
    do                                                                     \
    {                                                                      \
        LLVMBasicBlockRef _block_curr = CURR_BLOCK();                      \
        LLVMAddIncoming(block->result_phis[idx], &value, &_block_curr, 1); \
    } while (0)

#define BUILD_ICMP(op, left, right, res, name)                              \
    do                                                                      \
    {                                                                       \
        if (!(res =                                                         \
                  LLVMBuildICmp(comp_ctx->builder, op, left, right, name))) \
        {                                                                   \
            wasm_jit_set_last_error("llvm build icmp failed.");             \
            goto fail;                                                      \
        }                                                                   \
    } while (0)

#define ADD_TO_PARAM_PHIS(block, value, idx)                              \
    do                                                                    \
    {                                                                     \
        LLVMBasicBlockRef _block_curr = CURR_BLOCK();                     \
        LLVMAddIncoming(block->param_phis[idx], &value, &_block_curr, 1); \
    } while (0)

bool wasm_compile_op_block(JITCompContext *comp_ctx, JITFuncContext *func_ctx, WASMBlock *wasm_block,
                           uint32 label_type, uint32 param_count, uint8 *param_types,
                           uint32 result_count, uint8 *result_types)
{
    JITBlock *block = func_ctx->block_stack;
    uint32 i, param_index;
    uint64 size;
    LLVMValueRef value;
    char name[32];

    // 初始化
    block->label_type = label_type;
    block->param_count = param_count;
    block->param_types = param_types;
    block->result_count = result_count;
    block->result_types = result_types;
    block->stack_num = wasm_block->stack_num;
    block->else_addr = wasm_block->else_addr;
    block->end_addr = wasm_block->end_addr;
    block->else_param_phis = NULL;
    block->param_phis = NULL;
    block->result_phis = NULL;
    block->llvm_else_block = NULL;
    block->is_translate_else = false;
    block->is_polymorphic = false;

    // 开始块
    format_block_name(name, sizeof(name), block_indexes[label_type], label_type,
                      LABEL_BEGIN);
    CREATE_BLOCK(block->llvm_entry_block, name);
    MOVE_BLOCK_AFTER_CURR(block->llvm_entry_block);

    // 结束块
    format_block_name(name, sizeof(name), block_indexes[label_type],
                      label_type, LABEL_END);
    CREATE_BLOCK(block->llvm_end_block, name);

    if (label_type == LABEL_TYPE_BLOCK || label_type == LABEL_TYPE_LOOP)
    {
        BUILD_BR(block->llvm_entry_block);
    }
    else if (label_type == LABEL_TYPE_IF)
    {
        POP_COND(value);

        if (block->else_addr)
        {
            /* Create else block */
            format_block_name(name, sizeof(name), block_indexes[label_type],
                              label_type, LABEL_ELSE);
            CREATE_BLOCK(block->llvm_else_block, name);
            MOVE_BLOCK_AFTER(block->llvm_else_block,
                             block->llvm_entry_block);
            /* Create condition br IR */
            BUILD_COND_BR(value, block->llvm_entry_block,
                          block->llvm_else_block);
        }
        else
        {
            /* Create condition br IR */
            BUILD_COND_BR(value, block->llvm_entry_block,
                          block->llvm_end_block);
        }
    }
    else
    {
        wasm_jit_set_last_error("Invalid block type.");
        goto fail;
    }

    LLVMBasicBlockRef block_curr = CURR_BLOCK();

    if (block->param_count)
    {
        size = sizeof(LLVMValueRef) * (uint64)block->param_count;
        if (!(block->param_phis = wasm_runtime_malloc(size)))
        {
            wasm_jit_set_last_error("allocate memory failed.");
            return false;
        }

        if (block->llvm_else_block && !(block->else_param_phis = wasm_runtime_malloc(size)))
        {
            wasm_runtime_free(block->param_phis);
            block->param_phis = NULL;
            wasm_jit_set_last_error("allocate memory failed.");
            return false;
        }

        /* Create param phis */
        for (i = 0; i < block->param_count; i++)
        {
            SET_BUILDER_POS(block->llvm_entry_block);
            snprintf(name, sizeof(name), "%s%d_phi%d",
                     block_name_prefix[block->label_type], block_indexes[block->label_type],
                     i);
            if (!(block->param_phis[i] = LLVMBuildPhi(
                      comp_ctx->builder, TO_LLVM_TYPE(block->param_types[i]),
                      name)))
            {
                wasm_jit_set_last_error("llvm build phi failed.");
                goto fail;
            }

            if (block->llvm_else_block)
            {
                /* Build else param phis */
                SET_BUILDER_POS(block->llvm_else_block);
                snprintf(name, sizeof(name), "else%d_phi%d", block_indexes[block->label_type],
                         i);
                if (!(block->else_param_phis[i] = LLVMBuildPhi(
                          comp_ctx->builder,
                          TO_LLVM_TYPE(block->param_types[i]), name)))
                {
                    wasm_jit_set_last_error("llvm build phi failed.");
                    goto fail;
                }
            }
        }
        SET_BUILDER_POS(block_curr);

        /* Pop param values from current block's
         * value stack and add to param phis.
         */
        for (i = 0; i < block->param_count; i++)
        {
            param_index = block->param_count - 1 - i;
            POP(value);
            LLVMAddIncoming(block->param_phis[param_index], &value,
                            &block_curr, 1);
            if (block->llvm_else_block)
            {
                /* has else branch, add to else param phis */
                LLVMAddIncoming(block->else_param_phis[param_index], &value,
                                &block_curr, 1);
            }
        }
    }

    /* Push param phis to the new block */
    for (i = 0; i < block->param_count; i++)
    {
        PUSH(block->param_phis[i]);
    }

    /* Push the new block to block stack */
    PUSH_JITBLOCK(block);

    SET_BUILDER_POS(block->llvm_entry_block);

    block_indexes[label_type]++;

    return true;
fail:
    wasm_jit_block_destroy(block);
    return false;
}

bool wasm_jit_compile_op_else(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    JITBlock *block = GET_CUR_JITBLOCK();
    LLVMValueRef value;
    uint32 i, result_index;

    block->is_translate_else = true;
    block->is_polymorphic = false;

    /* Comes from the if branch of BLOCK if */
    CREATE_RESULT_VALUE_PHIS(block);
    for (i = 0; i < block->result_count; i++)
    {
        result_index = block->result_count - 1 - i;
        POP(value);
        ADD_TO_RESULT_PHIS(block, value, result_index);
    }

    BUILD_BR(block->llvm_end_block);

    // 理论上要清理
    RESAT_VALUE_BLOCK();
    for (i = 0; i < block->param_count; i++)
        PUSH(block->else_param_phis[i]);

    SET_BUILDER_POS(block->llvm_else_block);

    return true;
fail:
    return false;
}

bool wasm_jit_compile_op_end(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    JITBlock *block = GET_CUR_JITBLOCK();
    LLVMValueRef value;
    uint32 i, result_index, result_count;
    bool ret;

    result_count = block->result_count;

    if (block->is_polymorphic)
    {
        SET_BUILDER_POS(block->llvm_end_block);
        POP_JITBLOCK();
        return true;
    }

    if (block->label_type == LABEL_TYPE_FUNCTION)
    {
        ret = wasm_jit_compile_op_return(comp_ctx, func_ctx, NULL);
        POP_JITBLOCK();
        return ret;
    }

    // 将结束块移动到当前块后
    MOVE_BLOCK_AFTER_CURR(block->llvm_end_block);

    /* Handle block result values */
    CREATE_RESULT_VALUE_PHIS(block);
    for (i = 0; i < result_count; i++)
    {
        value = NULL;
        result_index = result_count - 1 - i;
        POP(value);
        ADD_TO_RESULT_PHIS(block, value, result_index);
    }

    for (i = 0; i < result_count; i++)
    {
        PUSH(block->result_phis[i]);
    }

    /* Jump to the end block */
    BUILD_BR(block->llvm_end_block);
    SET_BUILDER_POS(block->llvm_end_block);

    POP_JITBLOCK();

    return true;
fail:
    return false;
}

bool wasm_jit_compile_op_br(JITCompContext *comp_ctx, JITFuncContext *func_ctx, uint32 br_depth, uint8 **frame_ip)
{
    JITBlock *block_dst, *cur_block = GET_CUR_JITBLOCK();
    LLVMValueRef value_ret, value_param;
    uint32 i, param_index, result_index;

    HANDLE_POLYMORPHIC();
    block_dst = BR_TARGET_JITBLOCK(br_depth);

    if (block_dst->label_type == LABEL_TYPE_LOOP)
    {
        for (i = 0; i < block_dst->param_count; i++)
        {
            param_index = block_dst->param_count - 1 - i;
            POP(value_param);
            ADD_TO_PARAM_PHIS(block_dst, value_param, param_index);
        }
        BUILD_BR(block_dst->llvm_entry_block);
    }
    else
    {
        /* Handle result values */
        CREATE_RESULT_VALUE_PHIS(block_dst);
        for (i = 0; i < block_dst->result_count; i++)
        {
            result_index = block_dst->result_count - 1 - i;
            POP(value_ret);
            ADD_TO_RESULT_PHIS(block_dst, value_ret, result_index);
        }
        /* Jump to the end block */
        BUILD_BR(block_dst->llvm_end_block);
    }

    return true;
fail:
    return false;
}

bool wasm_jit_compile_op_br_if(JITCompContext *comp_ctx, JITFuncContext *func_ctx, uint32 br_depth, uint8 **frame_ip)
{
    JITBlock *block_dst;
    LLVMValueRef value_cmp, value, *values = NULL;
    LLVMBasicBlockRef llvm_else_block;
    uint32 i, param_index, result_index;
    uint64 size;

    POP_COND(value_cmp);

    if (!LLVMIsConstant(value_cmp))
    {
        /* Compare value is not constant, create condition br IR */
        block_dst = BR_TARGET_JITBLOCK(br_depth);

        /* Create llvm else block */
        CREATE_BLOCK(llvm_else_block, "br_if_else");
        MOVE_BLOCK_AFTER_CURR(llvm_else_block);

        if (block_dst->label_type == LABEL_TYPE_LOOP)
        {
            if (block_dst->param_count)
            {
                size = sizeof(LLVMValueRef) * (uint64)block_dst->param_count;
                if (!(values = wasm_runtime_malloc((uint32)size)))
                {
                    wasm_jit_set_last_error("allocate memory failed.");
                    goto fail;
                }
                for (i = 0; i < block_dst->param_count; i++)
                {
                    param_index = block_dst->param_count - 1 - i;
                    POP(value);
                    ADD_TO_PARAM_PHIS(block_dst, value, param_index);
                    values[param_index] = value;
                }
                for (i = 0; i < block_dst->param_count; i++)
                {
                    PUSH(values[i]);
                }
                wasm_runtime_free(values);
                values = NULL;
            }

            BUILD_COND_BR(value_cmp, block_dst->llvm_entry_block,
                          llvm_else_block);

            /* Move builder to else block */
            SET_BUILDER_POS(llvm_else_block);
        }
        else
        {
            /* Handle result values */
            if (block_dst->result_count)
            {
                size = sizeof(LLVMValueRef) * (uint64)block_dst->result_count;
                if (!(values = wasm_runtime_malloc((uint32)size)))
                {
                    wasm_jit_set_last_error("allocate memory failed.");
                    goto fail;
                }
                CREATE_RESULT_VALUE_PHIS(block_dst);
                for (i = 0; i < block_dst->result_count; i++)
                {
                    result_index = block_dst->result_count - 1 - i;
                    POP(value);
                    values[result_index] = value;
                    ADD_TO_RESULT_PHIS(block_dst, value, result_index);
                }
                for (i = 0; i < block_dst->result_count; i++)
                {
                    PUSH(values[i]);
                }
                wasm_runtime_free(values);
                values = NULL;
            }

            /* Condition jump to end block */
            BUILD_COND_BR(value_cmp, block_dst->llvm_end_block,
                          llvm_else_block);

            /* Move builder to else block */
            SET_BUILDER_POS(llvm_else_block);
        }
    }
    else
    {
        if ((int32)LLVMConstIntGetZExtValue(value_cmp) != 0)
        {
            /* Compare value is not 0, condition is true, same as op_br */
            return wasm_jit_compile_op_br(comp_ctx, func_ctx, br_depth, frame_ip);
        }
        else
        {
            /* Compare value is not 0, condition is false, skip br_if */
            return true;
        }
    }
    return true;
fail:
    if (values)
        wasm_runtime_free(values);
    return false;
}

bool wasm_jit_compile_op_br_table(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 *br_depths, uint32 br_count, uint8 **frame_ip)
{
    uint32 i, j;
    LLVMValueRef value_switch, value_cmp, value_case, value, *values = NULL;
    LLVMBasicBlockRef default_llvm_block, target_llvm_block;
    JITBlock *target_block, *cur_block = GET_CUR_JITBLOCK();
    uint32 br_depth, depth_idx;
    uint32 param_index, result_index;
    uint64 size;

    HANDLE_POLYMORPHIC();
    target_block = BR_TARGET_JITBLOCK(br_depths[br_count]);
    default_llvm_block = target_block->label_type != LABEL_TYPE_LOOP
                             ? target_block->llvm_end_block
                             : target_block->llvm_entry_block;

    POP_I32(value_cmp);

    if (!LLVMIsConstant(value_cmp))
    {
        /* Compare value is not constant, create switch IR */
        for (i = 0; i <= br_count; i++)
        {
            target_block = func_ctx->block_stack - br_depths[i] - 1;

            if (target_block->label_type != LABEL_TYPE_LOOP)
            {
                /* Handle result values */
                if (target_block->result_count)
                {
                    size = sizeof(LLVMValueRef) * (uint64)target_block->result_count;
                    if (!(values = wasm_runtime_malloc((uint32)size)))
                    {
                        wasm_jit_set_last_error("allocate memory failed.");
                        goto fail;
                    }
                    CREATE_RESULT_VALUE_PHIS(target_block);
                    for (j = 0; j < target_block->result_count; j++)
                    {
                        result_index = target_block->result_count - 1 - j;
                        POP(value);
                        values[result_index] = value;
                        ADD_TO_RESULT_PHIS(target_block, value, result_index);
                    }
                    for (j = 0; j < target_block->result_count; j++)
                    {
                        PUSH(values[j]);
                    }
                    wasm_runtime_free(values);
                }
            }
            else
            {
                /* Handle Loop parameters */
                if (target_block->param_count)
                {
                    size = sizeof(LLVMValueRef) * (uint64)target_block->param_count;
                    if (!(values = wasm_runtime_malloc((uint32)size)))
                    {
                        wasm_jit_set_last_error("allocate memory failed.");
                        goto fail;
                    }
                    for (j = 0; j < target_block->param_count; j++)
                    {
                        param_index = target_block->param_count - 1 - j;
                        POP(value);
                        values[param_index] = value;
                        ADD_TO_PARAM_PHIS(target_block, value, param_index);
                    }
                    for (j = 0; j < target_block->param_count; j++)
                    {
                        PUSH(values[j]);
                    }
                    wasm_runtime_free(values);
                }
            }
        }

        /* Create switch IR */
        if (!(value_switch = LLVMBuildSwitch(comp_ctx->builder, value_cmp,
                                             default_llvm_block, br_count)))
        {
            wasm_jit_set_last_error("llvm build switch failed.");
            return false;
        }

        /* Add each case for switch IR */
        for (i = 0; i < br_count; i++)
        {
            value_case = I32_CONST(i);
            target_block = func_ctx->block_stack - br_depths[i] - 1;
            target_llvm_block = target_block->label_type != LABEL_TYPE_LOOP
                                    ? target_block->llvm_end_block
                                    : target_block->llvm_entry_block;
            LLVMAddCase(value_switch, value_case, target_llvm_block);
        }

        return true;
    }
    else
    {
        /* Compare value is constant, create br IR */
        depth_idx = (uint32)LLVMConstIntGetZExtValue(value_cmp);
        br_depth = br_depths[br_count];
        if (depth_idx < br_count)
        {
            br_depth = br_depths[depth_idx];
        }
        return wasm_jit_compile_op_br(comp_ctx, func_ctx, br_depth, frame_ip);
    }
fail:
    if (values)
        wasm_runtime_free(values);
    return false;
}

bool wasm_jit_compile_op_return(JITCompContext *comp_ctx, JITFuncContext *func_ctx, uint8 **frame_ip)
{
    JITBlock *cur_block = GET_CUR_JITBLOCK();
    LLVMValueRef llvm_value;
    LLVMValueRef ret;
    WASMType *func_type;
    uint32 i, param_index, result_index, result_count, param_count;

    func_type = func_ctx->wasm_func->func_type;
    result_count = func_type->result_count;
    param_count = func_type->param_count;
    if (frame_ip)
    {
        HANDLE_POLYMORPHIC();
    }

    if (result_count)
    {
        /* Store extra result values to function parameters */
        for (i = 0; i < result_count - 1; i++)
        {
            result_index = result_count - 1 - i;
            POP(llvm_value);
            param_index = param_count + result_index;
            if (!LLVMBuildStore(comp_ctx->builder, llvm_value,
                                LLVMGetParam(func_ctx->func, param_index)))
            {
                wasm_jit_set_last_error("llvm build store failed.");
                goto fail;
            }
        }
        /* Return the first result value */
        POP(llvm_value);
        if (!(ret = LLVMBuildRet(comp_ctx->builder, llvm_value)))
        {
            wasm_jit_set_last_error("llvm build return failed.");
            goto fail;
        }
    }
    else
    {
        if (!(ret = LLVMBuildRetVoid(comp_ctx->builder)))
        {
            wasm_jit_set_last_error("llvm build return void failed.");
            goto fail;
        }
    }

    return true;
fail:
    return false;
}

bool wasm_jit_compile_op_unreachable(JITCompContext *comp_ctx, JITFuncContext *func_ctx, uint8 **frame_ip)
{
    JITBlock *cur_block = GET_CUR_JITBLOCK();
    HANDLE_POLYMORPHIC();
    if (!wasm_jit_emit_exception(comp_ctx, func_ctx, EXCE_UNREACHABLE, false, NULL,
                                 NULL))
        return false;

    return true;
}
