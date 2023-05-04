#include "wasm_validator.h"
#include "runtime_utils.h"
#include "wasm_leb_validator.h"
#include "wasm_block_validator.h"
#include "wasm_stack_validator.h"

#if WASM_ENABLE_JIT != 0
#define ADD_EXTINFO(res)                                          \
    do                                                            \
    {                                                             \
        ExtInfo *_op_info = wasm_runtime_malloc(sizeof(ExtInfo)); \
        _op_info->next_op = NULL;                                 \
        _op_info->idx = res;                                      \
        func->last_op_info->next_op = _op_info;                   \
        func->last_op_info = _op_info;                            \
    } while (0)

#define INIT_BLOCK_IN_FUNCTION()                                    \
    do                                                              \
    {                                                               \
        WASMBlock *_block = wasm_runtime_malloc(sizeof(WASMBlock)); \
        _block->next_block = NULL;                                  \
        _block->pre_block = NULL;                                   \
        _block->is_set = false;                                     \
        func->blocks = func->last_block = _block;                   \
    } while (0)

#define ADD_BLOCK_IN_FUNCTION()                                     \
    do                                                              \
    {                                                               \
        WASMBlock *_block = wasm_runtime_malloc(sizeof(WASMBlock)); \
        _block->pre_block = func->last_block;                       \
        _block->next_block = NULL;                                  \
        _block->is_set = false;                                     \
        func->last_block->next_block = _block;                      \
        func->last_block = _block;                                  \
    } while (0)

#define SET_BLOCK_IN_FUNCTION(cur_block)          \
    do                                            \
    {                                             \
        WASMBlock *_block = func->last_block;     \
        while (_block->is_set)                    \
        {                                         \
            _block = _block->pre_block;           \
        }                                         \
        _block->is_set = true;                    \
        _block->stack_num = cur_block->stack_num; \
        _block->else_addr = cur_block->else_addr; \
        _block->end_addr = cur_block->end_addr;   \
    } while (0)
#endif

#define validate_leb_template(p, p_end, res, type) \
    do                                             \
    {                                              \
        read_leb_##type(p, p_end, res);            \
    } while (0)

#define validate_leb_uint32(p, p_end, res) validate_leb_template(p, p_end, res, uint32)
#define validate_leb_int32(p, p_end, res) validate_leb_template(p, p_end, res, int32)
#define validate_leb_int64(p, p_end, res) validate_leb_template(p, p_end, res, int64)

#define validate_read_uint8(p, res) \
    do                              \
    {                               \
        res = read_uint8(p);        \
    } while (0)

static inline bool
is_32bit_type(uint8 type)
{
    if (type == VALUE_TYPE_I32 || type == VALUE_TYPE_F32)
        return true;
    return false;
}

static inline bool
is_64bit_type(uint8 type)
{
    if (type == VALUE_TYPE_I64 || type == VALUE_TYPE_F64)
        return true;
    return false;
}

static bool
wasm_emit_branch_table(WASMLoaderContext *ctx, uint8 opcode, uint32 depth)
{
    WASMBranchTable *branch_table;
    BranchBlock *frame_csp;
    BlockType *block_type;
    uint32 push;
    if (ctx->branch_table_num == ctx->branch_table_size)
    {
        ctx->branch_table_bottom = wasm_runtime_realloc(
            ctx->branch_table_bottom, (ctx->branch_table_size + 16) * sizeof(WASMBranchTable));
        if (!ctx->branch_table_bottom)
        {
            return false;
        }
        ctx->branch_table_size += 16;
        ctx->branch_table = ctx->branch_table_bottom + ctx->branch_table_num;
    }

    branch_table = ctx->branch_table;

    // 目前用于存放指令字节
    branch_table->stp = opcode;

    // 将该跳转指令对应的表索引存入相应控制块拥有的栈中
    frame_csp = ctx->frame_csp - depth - 1;

    if (frame_csp->table_queue_num >= frame_csp->table_queue_size)
    {
        frame_csp->table_queue_bottom = wasm_runtime_realloc(
            frame_csp->table_queue_bottom, (frame_csp->table_queue_size + 8) * sizeof(uint32));
        if (!frame_csp->table_queue_bottom)
        {
            return false;
        }
        frame_csp->table_queue_size += 8;
        frame_csp->table_queue = frame_csp->table_queue_bottom + frame_csp->table_queue_num;
    }

    *(frame_csp->table_queue) = ctx->branch_table_num;
    frame_csp->table_queue++;
    frame_csp->table_queue_num++;

    switch (opcode)
    {
    case WASM_OP_BR:
    case WASM_OP_BR_IF:
    case WASM_OP_BR_TABLE:
        block_type = &(frame_csp->block_type);
        if (block_type->is_value_type)
        {
            push = wasm_value_type_cell_num(block_type->u.value_type);
        }
        else
        {
            push = block_type->u.type->ret_cell_num;
        }
        branch_table->pop = ctx->stack_cell_num - frame_csp->stack_cell_num;
        branch_table->push = push;
        break;
    case WASM_OP_IF:
        branch_table->pop = 0;
        branch_table->push = 0;
        break;
    case WASM_OP_ELSE:
        branch_table->pop = 0;
        branch_table->push = 0;
        break;
    default:
        return false;
    }

    ctx->branch_table++;
    ctx->branch_table_num++;
    return true;
}

#define GET_LOCAL_INDEX_TYPE_AND_OFFSET()                        \
    do                                                           \
    {                                                            \
        validate_leb_uint32(p, p_end, local_idx);                \
        if (local_idx >= param_count + local_count)              \
        {                                                        \
            wasm_set_exception(module, "unknown local");         \
            goto fail;                                           \
        }                                                        \
        local_type = local_idx < param_count                     \
                         ? param_types[local_idx]                \
                         : local_types[local_idx - param_count]; \
        local_offset = local_offsets[local_idx];                 \
    } while (0)

static inline bool
check_memory(WASMModule *module)
{
    if (module->memory_count == 0 && module->import_memory_count == 0)
    {
        wasm_set_exception(module, "unknown memory");
        return false;
    }
    return true;
}

#define CHECK_MEMORY()             \
    do                             \
    {                              \
        if (!check_memory(module)) \
            goto fail;             \
    } while (0)

static bool
check_memory_access_align(WASMModule *module, uint8 opcode, uint32 align)
{
    uint8 mem_access_aligns[] = {
        2, 3, 2, 3, 0, 0, 1, 1, 0, 0, 1, 1, 2, 2, /* loads */
        2, 3, 2, 3, 0, 1, 0, 1, 2                 /* stores */
    };
    if (align > mem_access_aligns[opcode - WASM_OP_I32_LOAD])
    {
        wasm_set_exception(module,
                           "alignment must not be larger than natural");
        return false;
    }
    return true;
}

static inline uint32
block_get_param_types(BlockType *block_type, uint8 **p_param_types)
{
    uint32 param_count = 0;
    if (!block_type->is_value_type)
    {
        WASMType *wasm_type = block_type->u.type;
        *p_param_types = wasm_type->param;
        param_count = wasm_type->param_count;
    }
    else
    {
        *p_param_types = NULL;
        param_count = 0;
    }

    return param_count;
}

static inline uint32
block_get_result_types(BlockType *block_type, uint8 **p_result_types)
{
    uint32 result_count = 0;
    if (block_type->is_value_type)
    {
        if (block_type->u.value_type != VALUE_TYPE_VOID)
        {
            *p_result_types = &block_type->u.value_type;
            result_count = 1;
        }
    }
    else
    {
        WASMType *wasm_type = block_type->u.type;
        *p_result_types = wasm_type->result;
        result_count = wasm_type->result_count;
    }
    return result_count;
}

static bool
is_byte_a_type(uint8 type)
{
    return is_value_type(type) || (type == VALUE_TYPE_VOID);
}

static bool
wasm_loader_check_br(WASMModule *module, WASMLoaderContext *loader_ctx, uint32 depth)
{
    BranchBlock *target_block, *cur_block;
    BlockType *target_block_type;
    uint8 *types = NULL, *frame_ref;
    uint32 arity = 0;
    int32 i;

    if (loader_ctx->csp_num < depth + 1)
    {
        wasm_set_exception(module,
                           "unknown label, "
                           "unexpected end of section or function");
        return false;
    }

    cur_block = loader_ctx->frame_csp - 1;
    target_block = loader_ctx->frame_csp - (depth + 1);
    target_block_type = &target_block->block_type;
    frame_ref = loader_ctx->frame_ref - 1;

    // 对于loop需要特殊处理
    if (target_block->label_type == LABEL_TYPE_LOOP)
        arity = block_get_param_types(target_block_type, &types);
    else
        arity = block_get_result_types(target_block_type, &types);

    if (cur_block->is_stack_polymorphic)
    {
        for (i = (int32)arity - 1; i >= 0; i--)
        {
            POP_TYPE(types[i]);
        }
        for (i = 0; i < (int32)arity; i++)
        {
            PUSH_TYPE(types[i]);
        }
        return true;
    }

    if (loader_ctx->stack_num < arity)
    {
        wasm_set_exception(module, "type mismatch");
        return false;
    }

    for (i = (int32)arity - 1; i >= 0; i--)
    {
        CHECK_STACK_POP(*frame_ref, types[i]);
        frame_ref--;
    }

    return true;
fail:
    return false;
}

static bool
check_block_stack(WASMModule *module, WASMLoaderContext *loader_ctx, BranchBlock *block)
{
    BlockType *block_type = &block->block_type;
    uint8 *return_types = NULL;
    int32 return_count = 0;
    int32 available_stack_num, i;
    uint8 *frame_ref = NULL;

    available_stack_num = (int32)(loader_ctx->stack_num - block->stack_num);

    return_count = (int32)block_get_result_types(block_type, &return_types);

    if (block->is_stack_polymorphic)
    {
        for (i = return_count - 1; i >= 0; i--)
        {
            POP_TYPE(return_types[i]);
        }

        /* Check stack is empty */
        if (loader_ctx->stack_cell_num != block->stack_cell_num)
        {
            wasm_set_exception(module, "type mismatch: stack size does not match block type");
            return false;
        }

        for (i = 0; i < return_count; i++)
        {
            PUSH_TYPE(return_types[i]);
        }
        return true;
    }

    if (available_stack_num != return_count)
    {
        wasm_set_exception(module,
                           "type mismatch: stack size does not match block type");
        return false;
    }

    /* Check stack values match return types */
    frame_ref = loader_ctx->frame_ref - 1;
    for (i = (int32)return_count - 1; i >= 0; i--)
    {
        CHECK_STACK_POP(*frame_ref, return_types[i]);
        frame_ref--;
    }

    return true;
fail:
    return false;
}

/* reset the stack to the state of before entering the last block */
#define RESET_STACK()                                                                 \
    do                                                                                \
    {                                                                                 \
        BranchBlock *_frame_csp = loader_ctx->frame_csp - 1;                          \
        loader_ctx->stack_cell_num = _frame_csp->stack_cell_num;                      \
        loader_ctx->stack_num = _frame_csp->stack_num;                                \
        loader_ctx->frame_ref = loader_ctx->frame_ref_bottom + loader_ctx->stack_num; \
    } while (0)

/* set current block's stack polymorphic state */
#define SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(flag)          \
    do                                                       \
    {                                                        \
        BranchBlock *_cur_block = loader_ctx->frame_csp - 1; \
        _cur_block->is_stack_polymorphic = flag;             \
    } while (0)

#define BLOCK_HAS_PARAM(block_type) \
    (!block_type.is_value_type && block_type.u.type->param_count > 0)

static bool
check_table_index(WASMModule *module, uint32 table_index)
{
    if (table_index != 0)
    {
        wasm_set_exception(module, "zero byte expected");
        return false;
    }

    if (table_index >= module->import_table_count + module->table_count)
    {
        wasm_set_exception(module, "unknown table");
        return false;
    }
    return true;
}

static bool
check_function_index(WASMModule *module, uint32 function_index)
{
    if (function_index >= module->import_function_count + module->function_count)
    {
        wasm_set_exception(module, "unknown function");
        return false;
    }
    return true;
}

static void
wasm_loader_ctx_destroy(WASMLoaderContext *ctx)
{
    if (ctx)
    {
        if (ctx->frame_ref_bottom)
        {
            wasm_runtime_free(ctx->frame_ref_bottom);
        }
        if (ctx->frame_csp_bottom)
        {
            wasm_runtime_free(ctx->frame_csp_bottom);
        }
        wasm_runtime_free(ctx);
    }
}

static WASMLoaderContext *
wasm_loader_ctx_init()
{
    WASMLoaderContext *loader_ctx =
        wasm_runtime_malloc(sizeof(WASMLoaderContext));
    if (!loader_ctx)
        return NULL;

    // 初始化数值栈
    loader_ctx->stack_num = 0;
    loader_ctx->max_stack_num = 0;
    loader_ctx->stack_cell_num = 0;
    loader_ctx->max_stack_cell_num = 0;
    loader_ctx->frame_ref_size = 32;
    if (!(loader_ctx->frame_ref_bottom = loader_ctx->frame_ref = wasm_runtime_malloc(
              loader_ctx->frame_ref_size)))
        goto fail;

    // 初始化控制栈
    loader_ctx->csp_num = 0;
    loader_ctx->max_csp_num = 0;
    loader_ctx->frame_csp_size = 8;
    if (!(loader_ctx->frame_csp_bottom = loader_ctx->frame_csp = wasm_runtime_malloc(
              8 * sizeof(BranchBlock))))
        goto fail;

    // 初始化控制表
    loader_ctx->branch_table_num = 0;
    loader_ctx->branch_table_size = 0;
    loader_ctx->branch_table_bottom = NULL;

    return loader_ctx;

fail:
    wasm_loader_ctx_destroy(loader_ctx);
    return NULL;
}

bool wasm_validator_code(WASMModule *module, WASMFunction *func)
{
    uint8 *p = (uint8 *)func->func_ptr, *p_end = func->code_end, *p_org;
    uint32 param_count, local_count, global_count;
    uint8 *param_types, *local_types, local_type, global_type;
    BlockType func_block_type;
    uint16 *local_offsets, local_offset;
    uint32 type_idx, func_idx, local_idx, global_idx, table_idx;
    uint32 table_seg_idx, data_seg_idx, count, align, mem_offset, i;
    int32 i32_const = 0;
    int64 i64_const;
    uint8 opcode;
    WASMLoaderContext *loader_ctx;
    BranchBlock *frame_csp_tmp;

    global_count = module->import_global_count + module->global_count;

    param_count = func->param_count;
    param_types = func->func_type->param;

    func_block_type.is_value_type = false;
    func_block_type.u.type = func->func_type;

    local_count = func->local_count;
    local_types = func->local_types;
    local_offsets = func->local_offsets;

    if (!(loader_ctx = wasm_loader_ctx_init()))
        goto fail;

    PUSH_BLOCK(loader_ctx, LABEL_TYPE_FUNCTION, func_block_type, p);

#if WASM_ENABLE_JIT != 0
    INIT_BLOCK_IN_FUNCTION();
    ADD_BLOCK_IN_FUNCTION();
    func->op_info = func->last_op_info = wasm_runtime_malloc(sizeof(ExtInfo));
#endif

    while (p < p_end)
    {
        opcode = *p++;

        switch (opcode)
        {
        case WASM_OP_UNREACHABLE:
            RESET_STACK();
            SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(true);
            break;

        case WASM_OP_NOP:
            break;

        case WASM_OP_IF:
            POP_I32();
        case WASM_OP_BLOCK:
        case WASM_OP_LOOP:
        {
            uint8 value_type;
            BlockType block_type;

            validate_read_uint8(p, value_type);
            if (is_byte_a_type(value_type))
            {
                /* If the first byte is one of these special values:
                 * 0x40/0x7F/0x7E/0x7D/0x7C, take it as the type of
                 * the single return value. */
                block_type.is_value_type = true;
                block_type.u.value_type = value_type;
            }
            else
            {
                uint32 type_index;
                p--;
                validate_leb_uint32(p, p_end, type_index);
                if (type_index >= module->type_count)
                {
                    wasm_set_exception(module,
                                       "unknown type");
                    goto fail;
                }
                block_type.is_value_type = false;
                block_type.u.type = module->types[type_index];
            }

            /* Pop block parameters from stack */
            if (BLOCK_HAS_PARAM(block_type))
            {
                WASMType *wasm_type = block_type.u.type;
                uint32 cur_param_count = wasm_type->param_count;
                for (i = 0; i < wasm_type->param_count; i++)
                    POP_TYPE(wasm_type->param[cur_param_count - i - 1]);
            }

            PUSH_BLOCK(loader_ctx, LABEL_TYPE_BLOCK + (opcode - WASM_OP_BLOCK), block_type, p);

#if WASM_ENABLE_JIT != 0
            ADD_BLOCK_IN_FUNCTION();
#endif

            /* Pass parameters to block */
            if (BLOCK_HAS_PARAM(block_type))
            {
                WASMType *wasm_type = block_type.u.type;
                for (i = 0; i < wasm_type->param_count; i++)
                    PUSH_TYPE(wasm_type->param[i]);
            }

            if (opcode == WASM_OP_IF && !wasm_emit_branch_table(loader_ctx, WASM_OP_IF, 0))
                goto fail;

            break;
        }

        case WASM_OP_ELSE:
        {
            BranchBlock *cur_block = loader_ctx->frame_csp - 1;
            BlockType block_type = cur_block->block_type;

            if (loader_ctx->csp_num < 2 || cur_block->label_type != LABEL_TYPE_IF)
            {
                wasm_set_exception(module,
                                   "opcode else found without matched opcode if");
                goto fail;
            }

            if (!check_block_stack(module, loader_ctx, cur_block))
                goto fail;

            // 针对if指令设置跳转表
            cur_block->else_addr = p;
            cur_block->branch_table_else_idx = loader_ctx->branch_table_num;

            RESET_STACK();
            SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(false);

            if (BLOCK_HAS_PARAM(block_type))
            {
                for (i = 0; i < block_type.u.type->param_count; i++)
                    PUSH_TYPE(block_type.u.type->param[i]);
            }

            if (!wasm_emit_branch_table(loader_ctx, WASM_OP_ELSE, 0))
                goto fail;

            break;
        }

        case WASM_OP_END:
        {
            BranchBlock *cur_block = loader_ctx->frame_csp - 1;

            if (!check_block_stack(module, loader_ctx, cur_block))
                goto fail;

            if (cur_block->label_type == LABEL_TYPE_IF && !cur_block->else_addr)
            {
                uint32 block_param_count = 0, block_ret_count = 0;
                uint8 *block_param_types = NULL, *block_ret_types = NULL;
                BlockType *cur_block_type = &cur_block->block_type;
                if (cur_block_type->is_value_type)
                {
                    if (cur_block_type->u.value_type != VALUE_TYPE_VOID)
                    {
                        block_ret_count = 1;
                        block_ret_types = &cur_block_type->u.value_type;
                    }
                }
                else
                {
                    block_param_count = cur_block_type->u.type->param_count;
                    block_ret_count = cur_block_type->u.type->result_count;
                    block_param_types = cur_block_type->u.type->param;
                    block_ret_types = cur_block_type->u.type->result;
                }
                if (block_param_count != block_ret_count || (block_param_count && memcmp(block_param_types, block_ret_types,
                                                                                         block_param_count)))
                {
                    wasm_set_exception(module,
                                       "type mismatch: else branch missing");
                    goto fail;
                }
                cur_block->else_addr = p - 1;
                // 这种情况下跳转表的else和end两者是相同的
                cur_block->branch_table_else_idx = loader_ctx->branch_table_num;
            }

            cur_block->end_addr = p - 1;

#if WASM_ENABLE_JIT != 0
            SET_BLOCK_IN_FUNCTION(cur_block);
#endif
            POP_BLOCK();

            if (loader_ctx->csp_num == 0 && p < p_end)
            {

                wasm_set_exception(module, "section size mismatch");
                goto fail;
            }

            break;
        }

        case WASM_OP_BR:
        {
            uint32 depth;
            validate_leb_uint32(p, p_end, depth);
            if (!wasm_loader_check_br(module, loader_ctx, depth))
                goto fail;
            if (!wasm_emit_branch_table(loader_ctx, WASM_OP_BR, depth))
                goto fail;
            RESET_STACK();
            SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(true);
            break;
        }

        case WASM_OP_BR_IF:
        {
            POP_I32();
            uint32 depth;
            validate_leb_uint32(p, p_end, depth);
            if (!wasm_loader_check_br(module, loader_ctx, depth))
                goto fail;
            if (!wasm_emit_branch_table(loader_ctx, WASM_OP_BR_IF, depth))
                goto fail;

            break;
        }

        case WASM_OP_BR_TABLE:
        {
            uint8 *ret_types = NULL;
            uint32 ret_count = 0;

            uint32 depth;

            validate_leb_uint32(p, p_end, count);
            POP_I32();

            for (i = 0; i <= count; i++)
            {
                validate_leb_uint32(p, p_end, depth);
                if (!wasm_loader_check_br(module, loader_ctx, depth))
                    goto fail;
                if (!wasm_emit_branch_table(loader_ctx, WASM_OP_BR_TABLE, depth))
                    goto fail;

                frame_csp_tmp = loader_ctx->frame_csp - depth - 1;

                if (i == 0)
                {
                    if (frame_csp_tmp->label_type != LABEL_TYPE_LOOP)
                        ret_count = block_get_result_types(
                            &frame_csp_tmp->block_type, &ret_types);
                }
                else
                {
                    uint8 *tmp_ret_types = NULL;
                    uint32 tmp_ret_count = 0;

                    /* Check whether all table items have the same return
                     * type */
                    if (frame_csp_tmp->label_type != LABEL_TYPE_LOOP)
                        tmp_ret_count = block_get_result_types(
                            &frame_csp_tmp->block_type, &tmp_ret_types);

                    if (ret_count != tmp_ret_count || (ret_count && 0 != memcmp(ret_types, tmp_ret_types,
                                                                                ret_count)))
                    {
                        wasm_set_exception(
                            module,
                            "type mismatch: br_table targets must "
                            "all use same result type");
                        goto fail;
                    }
                }
            }

            RESET_STACK();
            SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(true);
            break;
        }

        case WASM_OP_RETURN:
        {
            int32 idx;
            uint8 ret_type;
            for (idx = (int32)func->result_count - 1; idx >= 0;
                 idx--)
            {
                ret_type = func->result_types[idx];
                POP_TYPE(ret_type);
            }

            RESET_STACK();
            SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(true);
            break;
        }

        case WASM_OP_CALL:
        {
            WASMType *func_type;
            int32 idx;

            validate_leb_uint32(p, p_end, func_idx);
            if (!check_function_index(module, func_idx))
            {
                goto fail;
            }

            func_type = module->functions[func_idx].func_type;

            if (func_type->param_count > 0)
            {
                for (idx = (int32)(func_type->param_count - 1); idx >= 0;
                     idx--)
                {
                    POP_TYPE(func_type->param[idx]);
                }
            }

            for (i = 0; i < func_type->result_count; i++)
            {
                PUSH_TYPE(func_type->result[i]);
            }
            break;
        }

        case WASM_OP_CALL_INDIRECT:
        {
            int32 idx;
            WASMType *func_type;
#if WASM_ENABLE_JIT != 0
            func->has_op_call_indirect = true;
#endif
            validate_leb_uint32(p, p_end, type_idx);
            validate_leb_uint32(p, p_end, table_idx);
            if (!check_table_index(module, table_idx))
            {
                goto fail;
            }

            /* skip elem idx */
            POP_I32();

            if (type_idx >= module->type_count)
            {
                wasm_set_exception(module, "unknown type");
                goto fail;
            }

            func_type = module->types[type_idx];

            if (func_type->param_count > 0)
            {
                for (idx = (int32)(func_type->param_count - 1); idx >= 0;
                     idx--)
                {
                    POP_TYPE(func_type->param[idx]);
                }
            }

            for (i = 0; i < func_type->result_count; i++)
            {
                PUSH_TYPE(func_type->result[i]);
            }
            break;
        }

        case WASM_OP_DROP:
        {
            uint8 type = *(loader_ctx->frame_ref - 1);
            POP_TYPE(type);
            if (is_64bit_type(type))
            {
                *(p - 1) = WASM_OP_DROP_64;
            }
            break;
        }

        case WASM_OP_SELECT:
        {
            uint8 type;

            POP_I32();

            type = *(loader_ctx->frame_ref - 1);
            POP2_AND_PUSH(type, type);

            if (is_64bit_type(type))
            {
                *(p - 1) = WASM_OP_SELECT_64;
            }
            break;
        }

        case WASM_OP_GET_LOCAL:
        {
            p_org = p - 1;
            GET_LOCAL_INDEX_TYPE_AND_OFFSET();
            PUSH_TYPE(local_type);
#if WASM_ENABLE_JIT != 0
            ADD_EXTINFO(local_idx);
#endif
            if (local_offset < 0x80)
            {
                *p_org++ = EXT_OP_GET_LOCAL_FAST;

                if (is_32bit_type(local_type))
                {
                    *p_org++ = (uint8)local_offset;
                }
                else
                {
                    *p_org++ = (uint8)(local_offset | 0x80);
                }
                while (p_org < p)
                {
                    *p_org++ = WASM_OP_NOP;
                }
            }
            break;
        }

        case WASM_OP_SET_LOCAL:
        {
            p_org = p - 1;
            GET_LOCAL_INDEX_TYPE_AND_OFFSET();
            POP_TYPE(local_type);
#if WASM_ENABLE_JIT != 0
            ADD_EXTINFO(local_idx);
#endif
            if (local_offset < 0x80)
            {
                *p_org++ = EXT_OP_SET_LOCAL_FAST;
                if (is_32bit_type(local_type))
                {
                    *p_org++ = (uint8)local_offset;
                }
                else
                {
                    *p_org++ = (uint8)(local_offset | 0x80);
                }
                while (p_org < p)
                {
                    *p_org++ = WASM_OP_NOP;
                }
            }
            break;
        }

        case WASM_OP_TEE_LOCAL:
        {
            p_org = p - 1;
            GET_LOCAL_INDEX_TYPE_AND_OFFSET();
            POP_TYPE(local_type);
            PUSH_TYPE(local_type);

#if WASM_ENABLE_JIT != 0
            ADD_EXTINFO(local_idx);
#endif

            if (local_offset < 0x80)
            {
                *p_org++ = EXT_OP_TEE_LOCAL_FAST;
                if (is_32bit_type(local_type))
                {
                    *p_org++ = (uint8)local_offset;
                }
                else
                {
                    *p_org++ = (uint8)(local_offset | 0x80);
                }
                while (p_org < p)
                {
                    *p_org++ = WASM_OP_NOP;
                }
            }
            break;
        }

        case WASM_OP_GET_GLOBAL:
        {
            p_org = p - 1;
            validate_leb_uint32(p, p_end, global_idx);
            if (global_idx >= global_count)
            {
                wasm_set_exception(module, "unknown global");
                goto fail;
            }

            global_type = module->globals[global_idx].type;

            PUSH_TYPE(global_type);

            if (global_type == VALUE_TYPE_I64 || global_type == VALUE_TYPE_F64)
            {
                *p_org = WASM_OP_GET_GLOBAL_64;
            }
            break;
        }

        case WASM_OP_SET_GLOBAL:
        {
            bool is_mutable = false;

            p_org = p - 1;
            validate_leb_uint32(p, p_end, global_idx);
            if (global_idx >= global_count)
            {
                wasm_set_exception(module, "unknown global");
                goto fail;
            }

            is_mutable = module->globals[global_idx].is_mutable;
            if (!is_mutable)
            {
                wasm_set_exception(module,
                                   "global is immutable");
                goto fail;
            }

            global_type = module->globals[global_idx].type;

            POP_TYPE(global_type);

            if (global_type == VALUE_TYPE_I64 || global_type == VALUE_TYPE_F64)
            {
                *p_org = WASM_OP_SET_GLOBAL_64;
            }
            break;
        }

        /* load */
        case WASM_OP_I32_LOAD:
        case WASM_OP_I32_LOAD8_S:
        case WASM_OP_I32_LOAD8_U:
        case WASM_OP_I32_LOAD16_S:
        case WASM_OP_I32_LOAD16_U:
        case WASM_OP_I64_LOAD:
        case WASM_OP_I64_LOAD8_S:
        case WASM_OP_I64_LOAD8_U:
        case WASM_OP_I64_LOAD16_S:
        case WASM_OP_I64_LOAD16_U:
        case WASM_OP_I64_LOAD32_S:
        case WASM_OP_I64_LOAD32_U:
        case WASM_OP_F32_LOAD:
        case WASM_OP_F64_LOAD:
        /* store */
        case WASM_OP_I32_STORE:
        case WASM_OP_I32_STORE8:
        case WASM_OP_I32_STORE16:
        case WASM_OP_I64_STORE:
        case WASM_OP_I64_STORE8:
        case WASM_OP_I64_STORE16:
        case WASM_OP_I64_STORE32:
        case WASM_OP_F32_STORE:
        case WASM_OP_F64_STORE:
        {
            CHECK_MEMORY();
#if WASM_ENABLE_JIT != 0
            func->has_op_memory = true;
#endif
            validate_leb_uint32(p, p_end, align);      /* align */
            validate_leb_uint32(p, p_end, mem_offset); /* offset */
            if (!check_memory_access_align(module, opcode, align))
            {
                goto fail;
            }
            switch (opcode)
            {
            /* load */
            case WASM_OP_I32_LOAD:
            case WASM_OP_I32_LOAD8_S:
            case WASM_OP_I32_LOAD8_U:
            case WASM_OP_I32_LOAD16_S:
            case WASM_OP_I32_LOAD16_U:
                POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_I32);
                break;
            case WASM_OP_I64_LOAD:
            case WASM_OP_I64_LOAD8_S:
            case WASM_OP_I64_LOAD8_U:
            case WASM_OP_I64_LOAD16_S:
            case WASM_OP_I64_LOAD16_U:
            case WASM_OP_I64_LOAD32_S:
            case WASM_OP_I64_LOAD32_U:
                POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_I64);
                break;
            case WASM_OP_F32_LOAD:
                POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_F32);
                break;
            case WASM_OP_F64_LOAD:
                POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_F64);
                break;
            /* store */
            case WASM_OP_I32_STORE:
            case WASM_OP_I32_STORE8:
            case WASM_OP_I32_STORE16:
                POP_I32();
                POP_I32();
                break;
            case WASM_OP_I64_STORE:
            case WASM_OP_I64_STORE8:
            case WASM_OP_I64_STORE16:
            case WASM_OP_I64_STORE32:
                POP_I64();
                POP_I32();
                break;
            case WASM_OP_F32_STORE:
                POP_F32();
                POP_I32();
                break;
            case WASM_OP_F64_STORE:
                POP_F64();
                POP_I32();
                break;
            default:
                break;
            }
            break;
        }

        case WASM_OP_MEMORY_SIZE:
            CHECK_MEMORY();
            /* reserved byte 0x00 */
            if (*p++ != 0x00)
            {
                wasm_set_exception(module,
                                   "zero byte expected");
                goto fail;
            }
            PUSH_I32();

            break;

        case WASM_OP_MEMORY_GROW:
            CHECK_MEMORY();
#if WASM_ENABLE_JIT != 0
            module->has_op_memory_grow = true;
#endif
            if (*p++ != 0x00)
            {
                wasm_set_exception(module, "zero byte expected");
                goto fail;
            }
            POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_I32);

            break;

        case WASM_OP_I32_CONST:
            validate_leb_int32(p, p_end, i32_const);

            PUSH_I32();
            break;

        case WASM_OP_I64_CONST:
            validate_leb_int64(p, p_end, i64_const);

            PUSH_I64();
            break;

        case WASM_OP_F32_CONST:
            p += sizeof(float32);
            PUSH_F32();
            break;

        case WASM_OP_F64_CONST:
            p += sizeof(float64);
            PUSH_F64();
            break;

        case WASM_OP_I32_EQZ:
            POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_I32);
            break;

        case WASM_OP_I32_EQ:
        case WASM_OP_I32_NE:
        case WASM_OP_I32_LT_S:
        case WASM_OP_I32_LT_U:
        case WASM_OP_I32_GT_S:
        case WASM_OP_I32_GT_U:
        case WASM_OP_I32_LE_S:
        case WASM_OP_I32_LE_U:
        case WASM_OP_I32_GE_S:
        case WASM_OP_I32_GE_U:
            POP2_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_I32);
            break;

        case WASM_OP_I64_EQZ:
            POP_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_I32);
            break;

        case WASM_OP_I64_EQ:
        case WASM_OP_I64_NE:
        case WASM_OP_I64_LT_S:
        case WASM_OP_I64_LT_U:
        case WASM_OP_I64_GT_S:
        case WASM_OP_I64_GT_U:
        case WASM_OP_I64_LE_S:
        case WASM_OP_I64_LE_U:
        case WASM_OP_I64_GE_S:
        case WASM_OP_I64_GE_U:
            POP2_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_I32);
            break;

        case WASM_OP_F32_EQ:
        case WASM_OP_F32_NE:
        case WASM_OP_F32_LT:
        case WASM_OP_F32_GT:
        case WASM_OP_F32_LE:
        case WASM_OP_F32_GE:
            POP2_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_I32);
            break;

        case WASM_OP_F64_EQ:
        case WASM_OP_F64_NE:
        case WASM_OP_F64_LT:
        case WASM_OP_F64_GT:
        case WASM_OP_F64_LE:
        case WASM_OP_F64_GE:
            POP2_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_I32);
            break;

        case WASM_OP_I32_CLZ:
        case WASM_OP_I32_CTZ:
        case WASM_OP_I32_POPCNT:
            POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_I32);
            break;

        case WASM_OP_I32_ADD:
        case WASM_OP_I32_SUB:
        case WASM_OP_I32_MUL:
        case WASM_OP_I32_DIV_S:
        case WASM_OP_I32_DIV_U:
        case WASM_OP_I32_REM_S:
        case WASM_OP_I32_REM_U:
        case WASM_OP_I32_AND:
        case WASM_OP_I32_OR:
        case WASM_OP_I32_XOR:
        case WASM_OP_I32_SHL:
        case WASM_OP_I32_SHR_S:
        case WASM_OP_I32_SHR_U:
        case WASM_OP_I32_ROTL:
        case WASM_OP_I32_ROTR:
            POP2_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_I32);
            break;

        case WASM_OP_I64_CLZ:
        case WASM_OP_I64_CTZ:
        case WASM_OP_I64_POPCNT:
            POP_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_I64);
            break;

        case WASM_OP_I64_ADD:
        case WASM_OP_I64_SUB:
        case WASM_OP_I64_MUL:
        case WASM_OP_I64_DIV_S:
        case WASM_OP_I64_DIV_U:
        case WASM_OP_I64_REM_S:
        case WASM_OP_I64_REM_U:
        case WASM_OP_I64_AND:
        case WASM_OP_I64_OR:
        case WASM_OP_I64_XOR:
        case WASM_OP_I64_SHL:
        case WASM_OP_I64_SHR_S:
        case WASM_OP_I64_SHR_U:
        case WASM_OP_I64_ROTL:
        case WASM_OP_I64_ROTR:
            POP2_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_I64);
            break;

        case WASM_OP_F32_ABS:
        case WASM_OP_F32_NEG:
        case WASM_OP_F32_CEIL:
        case WASM_OP_F32_FLOOR:
        case WASM_OP_F32_TRUNC:
        case WASM_OP_F32_NEAREST:
        case WASM_OP_F32_SQRT:
            POP_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_F32);
            break;

        case WASM_OP_F32_ADD:
        case WASM_OP_F32_SUB:
        case WASM_OP_F32_MUL:
        case WASM_OP_F32_DIV:
        case WASM_OP_F32_MIN:
        case WASM_OP_F32_MAX:
        case WASM_OP_F32_COPYSIGN:
            POP2_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_F32);
            break;

        case WASM_OP_F64_ABS:
        case WASM_OP_F64_NEG:
        case WASM_OP_F64_CEIL:
        case WASM_OP_F64_FLOOR:
        case WASM_OP_F64_TRUNC:
        case WASM_OP_F64_NEAREST:
        case WASM_OP_F64_SQRT:
            POP_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_F64);
            break;

        case WASM_OP_F64_ADD:
        case WASM_OP_F64_SUB:
        case WASM_OP_F64_MUL:
        case WASM_OP_F64_DIV:
        case WASM_OP_F64_MIN:
        case WASM_OP_F64_MAX:
        case WASM_OP_F64_COPYSIGN:
            POP2_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_F64);
            break;

        case WASM_OP_I32_WRAP_I64:
            POP_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_I32);
            break;

        case WASM_OP_I32_TRUNC_S_F32:
        case WASM_OP_I32_TRUNC_U_F32:
            POP_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_I32);
            break;

        case WASM_OP_I32_TRUNC_S_F64:
        case WASM_OP_I32_TRUNC_U_F64:
            POP_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_I32);
            break;

        case WASM_OP_I64_EXTEND_S_I32:
        case WASM_OP_I64_EXTEND_U_I32:
            POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_I64);
            break;

        case WASM_OP_I64_TRUNC_S_F32:
        case WASM_OP_I64_TRUNC_U_F32:
            POP_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_I64);
            break;

        case WASM_OP_I64_TRUNC_S_F64:
        case WASM_OP_I64_TRUNC_U_F64:
            POP_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_I64);
            break;

        case WASM_OP_F32_CONVERT_S_I32:
        case WASM_OP_F32_CONVERT_U_I32:
            POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_F32);
            break;

        case WASM_OP_F32_CONVERT_S_I64:
        case WASM_OP_F32_CONVERT_U_I64:
            POP_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_F32);
            break;

        case WASM_OP_F32_DEMOTE_F64:
            POP_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_F32);
            break;

        case WASM_OP_F64_CONVERT_S_I32:
        case WASM_OP_F64_CONVERT_U_I32:
            POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_F64);
            break;

        case WASM_OP_F64_CONVERT_S_I64:
        case WASM_OP_F64_CONVERT_U_I64:
            POP_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_F64);
            break;

        case WASM_OP_F64_PROMOTE_F32:
            POP_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_F64);
            break;

        case WASM_OP_I32_REINTERPRET_F32:
            POP_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_I32);
            break;

        case WASM_OP_I64_REINTERPRET_F64:
            POP_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_I64);
            break;

        case WASM_OP_F32_REINTERPRET_I32:
            POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_F32);
            break;

        case WASM_OP_F64_REINTERPRET_I64:
            POP_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_F64);
            break;

        case WASM_OP_I32_EXTEND8_S:
        case WASM_OP_I32_EXTEND16_S:
            POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_I32);
            break;

        case WASM_OP_I64_EXTEND8_S:
        case WASM_OP_I64_EXTEND16_S:
        case WASM_OP_I64_EXTEND32_S:
            POP_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_I64);
            break;

        case WASM_OP_MISC_PREFIX:
        {
            uint32 opcode1;

            validate_leb_uint32(p, p_end, opcode1);
            switch (opcode1)
            {
            case WASM_OP_I32_TRUNC_SAT_S_F32:
            case WASM_OP_I32_TRUNC_SAT_U_F32:
                POP_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_I32);
                break;
            case WASM_OP_I32_TRUNC_SAT_S_F64:
            case WASM_OP_I32_TRUNC_SAT_U_F64:
                POP_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_I32);
                break;
            case WASM_OP_I64_TRUNC_SAT_S_F32:
            case WASM_OP_I64_TRUNC_SAT_U_F32:
                POP_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_I64);
                break;
            case WASM_OP_I64_TRUNC_SAT_S_F64:
            case WASM_OP_I64_TRUNC_SAT_U_F64:
                POP_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_I64);
                break;
            case WASM_OP_MEMORY_INIT:
            {
                validate_leb_uint32(p, p_end, data_seg_idx);
                if (module->import_memory_count == 0 && module->memory_count == 0)
                    goto fail_unknown_memory;

                if (*p++ != 0x00)
                    goto fail_zero_byte_expected;

                if (data_seg_idx >= module->data_seg_count)
                {
                    wasm_set_exception(module, "unknown data segment");
                    goto fail;
                }

                if (module->data_seg_count1 == 0)
                    goto fail_data_cnt_sec_require;
#if WASM_ENABLE_JIT != 0
                func->has_op_memory = true;
#endif
                POP_I32();
                POP_I32();
                POP_I32();
                break;
            }
            case WASM_OP_DATA_DROP:
            {
                validate_leb_uint32(p, p_end, data_seg_idx);
                if (data_seg_idx >= module->data_seg_count)
                {
                    wasm_set_exception(module, "unknown data segment");
                    goto fail;
                }

                if (module->data_seg_count1 == 0)
                    goto fail_data_cnt_sec_require;
                break;
            }
            case WASM_OP_MEMORY_COPY:
            {
                /* both src and dst memory index should be 0 */
                if (*(int16 *)p != 0x0000)
                    goto fail_zero_byte_expected;
                p += 2;

                if (module->import_memory_count == 0 && module->memory_count == 0)
                    goto fail_unknown_memory;
#if WASM_ENABLE_JIT != 0
                func->has_op_memory = true;
#endif
                POP_I32();
                POP_I32();
                POP_I32();
                break;
            }
            case WASM_OP_MEMORY_FILL:
            {
                if (*p++ != 0x00)
                {
                    goto fail_zero_byte_expected;
                }
                if (module->import_memory_count == 0 && module->memory_count == 0)
                {
                    goto fail_unknown_memory;
                }
#if WASM_ENABLE_JIT != 0
                func->has_op_memory = true;
#endif
                POP_I32();
                POP_I32();
                POP_I32();
                break;
            }
            fail_zero_byte_expected:
                wasm_set_exception(module,
                                   "zero byte expected");
                goto fail;

            fail_unknown_memory:
                wasm_set_exception(module,
                                   "unknown memory 0");
                goto fail;
            fail_data_cnt_sec_require:
                wasm_set_exception(module,
                                   "data count section required");
                goto fail;

            default:
                wasm_set_exception(module, "unsupported opcode");
                goto fail;
            }
            break;
        }

        default:
            wasm_set_exception(module, "unsupported opcode");
            goto fail;
        }
    }

    if (loader_ctx->csp_num > 0)
    {
        wasm_set_exception(module, "END opcode expected");
        goto fail;
    }

    func->branch_table = loader_ctx->branch_table_bottom;
    func->max_stack_cell_num = loader_ctx->max_stack_cell_num;
    func->max_block_num = loader_ctx->max_csp_num;
    func->max_stack_num = loader_ctx->max_stack_num;
    return true;

fail:
    wasm_loader_ctx_destroy(loader_ctx);

    (void)table_idx;
    (void)table_seg_idx;
    (void)data_seg_idx;
    (void)i64_const;
    (void)local_offset;
    (void)p_org;
    (void)mem_offset;
    (void)align;
    return false;
}

#if WASM_ENABLE_THREAD != 0
void *wasm_validator_code_callback(void *arg)
{
    bool ret;
    uint64 *args = (uint64 *)arg;
    uint64 i = args[0];
    WASMModule *module = (WASMModule *)args[1];
    uint32 function_count = module->function_count;
    WASMFunction *func = module->functions + module->import_function_count + i;
    for (; i < function_count; i += WASM_VALIDATE_THREAD_NUM, func += WASM_VALIDATE_THREAD_NUM)
    {
        ret = wasm_validator_code(module, func);
        if (!ret)
        {
            break;
        }
    }
    os_thread_exit((void *)ret);
    return NULL;
}

#endif

bool wasm_validator(WASMModule *module)
{
    uint32 i, j, index, str_len;
    char *name;
    WASMFunction *func;
    WASMGlobal *global, *globals;
    WASMExport *export;

    module->module_stage = Validate;

    export = module->exports;
    for (i = 0; i < module->export_count; i++, export ++)
    {
        index = export->index;
        switch (export->kind)
        {
        /* function index */
        case EXPORT_KIND_FUNC:
            if (index >= module->function_count + module->import_function_count)
            {
                wasm_set_exception(module, "unknown function");
                return false;
            }
            break;
        case EXPORT_KIND_TABLE:
            if (index >= module->table_count + module->import_table_count)
            {
                wasm_set_exception(module, "unknown table");
                return false;
            }
            break;
        /* memory index */
        case EXPORT_KIND_MEMORY:
            if (index >= module->memory_count + module->import_memory_count)
            {
                wasm_set_exception(module, "unknown memory");
                return false;
            }
            break;
        /* global index */
        case EXPORT_KIND_GLOBAL:
            if (index >= module->global_count + module->import_global_count)
            {
                wasm_set_exception(module, "unknown global");
                return false;
            }
            break;
        default:
            wasm_set_exception(module, "invalid export kind");
            return false;
        }

        str_len = strlen(export->name);

        // 检查是否重复导出
        for (j = 0; j < i; j++)
        {
            name = module->exports[j].name;
            if (strlen(name) == str_len && memcmp(name, export->name, str_len) == 0)
            {
                wasm_set_exception(module, "duplicate export name");
                return false;
            }
        }
    }

    globals = module->globals;
    global = module->globals + module->import_global_count;
    for (i = 0; i < module->global_count; i++, global++)
    {
        if (global->type == VALUE_TYPE_GLOBAL)
        {
            global->type = globals[global->initial_value.global_index].type;
            memcpy(
                &(global->initial_value),
                &(globals[global->initial_value.global_index].initial_value),
                sizeof(WASMValue));
        }
    }
#if WASM_ENABLE_THREAD != 0
    korp_tid threads[WASM_VALIDATE_THREAD_NUM];
    uint64 args[WASM_VALIDATE_THREAD_NUM][2];
    bool ret_value;
    void *temp_value;
    for (i = 0; i < WASM_VALIDATE_THREAD_NUM; i++)
    {
        args[i][0] = (uint64)i;
        args[i][1] = (uint64)module;
        os_thread_create(&threads[i], wasm_validator_code_callback,
                         (void *)args[i],
                         APP_THREAD_STACK_SIZE_DEFAULT);
    }

    for (i = 0; i < WASM_VALIDATE_THREAD_NUM; i++)
    {
        os_thread_join(threads[i], &temp_value);
        ret_value |= (bool)temp_value;
    }
    if (!ret_value)
    {
        goto fail;
    }
#else
    func = module->functions + module->import_function_count;
    for (i = 0; i < module->function_count; i++, func++)
    {
        if (!wasm_validator_code(module, func))
        {
            goto fail;
        }
    }
#endif

    LOG_VERBOSE("Validate success.\n");
    return true;
fail:
    LOG_VERBOSE("Validate fail.\n");
    return false;
}