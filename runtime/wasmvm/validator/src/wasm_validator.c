#include "wasm_opcode.h"
#include "wasm_validator.h"
#include "wasm_memory.h"
#include "runtime_utils.h"

#define REF_ANY VALUE_TYPE_ANY
#define REF_I32 VALUE_TYPE_I32
#define REF_F32 VALUE_TYPE_F32
#define REF_I64_1 VALUE_TYPE_I64
#define REF_I64_2 VALUE_TYPE_I64
#define REF_F64_1 VALUE_TYPE_F64
#define REF_F64_2 VALUE_TYPE_F64
#define REF_V128_1 VALUE_TYPE_V128
#define REF_V128_2 VALUE_TYPE_V128
#define REF_V128_3 VALUE_TYPE_V128
#define REF_V128_4 VALUE_TYPE_V128
#define REF_FUNCREF VALUE_TYPE_FUNCREF
#define REF_EXTERNREF VALUE_TYPE_EXTERNREF

static char *
type2str(uint8 type);

static bool
is_32bit_type(uint8 type);

static bool
is_64bit_type(uint8 type);

static bool
check_stack_push(WASMLoaderContext *ctx, char *error_buf, uint32 error_buf_size);

static bool
is_32bit_type(uint8 type)
{
    if (type == VALUE_TYPE_I32 || type == VALUE_TYPE_F32
    )
        return true;
    return false;
}

static bool
is_64bit_type(uint8 type)
{
    if (type == VALUE_TYPE_I64 || type == VALUE_TYPE_F64)
        return true;
    return false;
}

static bool
check_stack_pop(uint8 *frame_ref, int32 stack_cell_num, uint8 type, char *error_buf,
                uint32 error_buf_size)
{

    if (stack_cell_num > 0 && *(frame_ref - 1) == VALUE_TYPE_ANY) {
        /* the stack top is a value of any type, return success */
        return true;
    }

    if ((is_32bit_type(type) && stack_cell_num < 1)
        || (is_64bit_type(type) && stack_cell_num < 2)
    ) {
        set_error_buf(error_buf, error_buf_size,
                      "type mismatch: expect data but stack was empty");
        return false;
    }

    if ((is_32bit_type(type) && *(frame_ref - 1) != type)
        || (is_64bit_type(type)
            && (*(frame_ref - 2) != type || *(frame_ref - 1) != type))
    ) {
        set_error_buf_v(error_buf, error_buf_size, "%s%s%s",
                        "type mismatch: expect ", type2str(type),
                        " but got other");
        return false;
    }

    return true;
}

static bool
wasm_loader_push_frame_ref(WASMLoaderContext *ctx, uint8 type, char *error_buf,
                           uint32 error_buf_size)
{
    if (type == VALUE_TYPE_VOID)
        return true;

    //如果空间不够会增加16个位置
    if (!check_stack_push(ctx, error_buf, error_buf_size))
        return false;

    *ctx->frame_ref++ = type;
    ctx->stack_cell_num++;
    if (is_32bit_type(type) || type == VALUE_TYPE_ANY)
        goto check_stack_and_return;

    *ctx->frame_ref++ = type;
    ctx->stack_cell_num++;

check_stack_and_return:
    if (ctx->stack_cell_num > ctx->max_stack_cell_num) {
        ctx->max_stack_cell_num = ctx->stack_cell_num;
        if (ctx->max_stack_cell_num > UINT16_MAX) {
            set_error_buf(error_buf, error_buf_size,
                          "operand stack depth limit exceeded");
            return false;
        }
    }
    return true;
}

static bool
wasm_loader_pop_frame_ref(WASMLoaderContext *ctx, uint8 type, char *error_buf,
                          uint32 error_buf_size)
{
    BranchBlock *cur_block = ctx->frame_csp - 1;
    int32 available_stack_cell =
        (int32)(ctx->stack_cell_num - cur_block->stack_cell_num);

    if (available_stack_cell <= 0 && cur_block->is_stack_polymorphic)
        return true;

    if (type == VALUE_TYPE_VOID)
        return true;

    if (!check_stack_pop(ctx->frame_ref, available_stack_cell, type, error_buf, error_buf_size))
        return false;

    ctx->frame_ref--;
    ctx->stack_cell_num--;

    if (is_32bit_type(type) || *ctx->frame_ref == VALUE_TYPE_ANY)
        return true;

    ctx->frame_ref--;
    ctx->stack_cell_num--;

    return true;
}

#if WASM_ENABLE_FAST_INTERP != 0

#if WASM_ENABLE_LABELS_AS_VALUES != 0
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS != 0
#define emit_label(opcode)                                      \
    do {                                                        \
        wasm_loader_emit_ptr(loader_ctx, handle_table[opcode]); \
        LOG_OP("\nemit_op [%02x]\t", opcode);                   \
    } while (0)
#define skip_label()                                            \
    do {                                                        \
        wasm_loader_emit_backspace(loader_ctx, sizeof(void *)); \
        LOG_OP("\ndelete last op\n");                           \
    } while (0)
#else /* else of WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS */
#define emit_label(opcode)                                                     \
    do {                                                                       \
        int32 offset =                                                         \
            (int32)((uint8 *)handle_table[opcode] - (uint8 *)handle_table[0]); \
        if (!(offset >= INT16_MIN && offset < INT16_MAX)) {                    \
            set_error_buf(error_buf, error_buf_size,                           \
                          "pre-compiled label offset out of range");           \
            goto fail;                                                         \
        }                                                                      \
        wasm_loader_emit_int16(loader_ctx, offset);                            \
        LOG_OP("\nemit_op [%02x]\t", opcode);                                  \
    } while (0)
#define skip_label()                                           \
    do {                                                       \
        wasm_loader_emit_backspace(loader_ctx, sizeof(int16)); \
        LOG_OP("\ndelete last op\n");                          \
    } while (0)
#endif /* end of WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS */
#else  /* else of WASM_ENABLE_LABELS_AS_VALUES */
#define emit_label(opcode)                          \
    do {                                            \
        wasm_loader_emit_uint8(loader_ctx, opcode); \
        LOG_OP("\nemit_op [%02x]\t", opcode);       \
    } while (0)
#define skip_label()                                           \
    do {                                                       \
        wasm_loader_emit_backspace(loader_ctx, sizeof(uint8)); \
        LOG_OP("\ndelete last op\n");                          \
    } while (0)
#endif /* end of WASM_ENABLE_LABELS_AS_VALUES */

#define emit_empty_label_addr_and_frame_ip(type)                             \
    do {                                                                     \
        if (!add_label_patch_to_list(loader_ctx->frame_csp - 1, type,        \
                                     loader_ctx->p_code_compiled, error_buf, \
                                     error_buf_size))                        \
            goto fail;                                                       \
        /* label address, to be patched */                                   \
        wasm_loader_emit_ptr(loader_ctx, NULL);                              \
    } while (0)

#define emit_br_info(frame_csp)                                         \
    do {                                                                \
        if (!wasm_loader_emit_br_info(loader_ctx, frame_csp, error_buf, \
                                      error_buf_size))                  \
            goto fail;                                                  \
    } while (0)

#define LAST_OP_OUTPUT_I32()                                                   \
    (last_op >= WASM_OP_I32_EQZ && last_op <= WASM_OP_I32_ROTR)                \
        || (last_op == WASM_OP_I32_LOAD || last_op == WASM_OP_F32_LOAD)        \
        || (last_op >= WASM_OP_I32_LOAD8_S && last_op <= WASM_OP_I32_LOAD16_U) \
        || (last_op >= WASM_OP_F32_ABS && last_op <= WASM_OP_F32_COPYSIGN)     \
        || (last_op >= WASM_OP_I32_WRAP_I64                                    \
            && last_op <= WASM_OP_I32_TRUNC_U_F64)                             \
        || (last_op >= WASM_OP_F32_CONVERT_S_I32                               \
            && last_op <= WASM_OP_F32_DEMOTE_F64)                              \
        || (last_op == WASM_OP_I32_REINTERPRET_F32)                            \
        || (last_op == WASM_OP_F32_REINTERPRET_I32)                            \
        || (last_op == EXT_OP_COPY_STACK_TOP)

#define LAST_OP_OUTPUT_I64()                                                   \
    (last_op >= WASM_OP_I64_CLZ && last_op <= WASM_OP_I64_ROTR)                \
        || (last_op >= WASM_OP_F64_ABS && last_op <= WASM_OP_F64_COPYSIGN)     \
        || (last_op == WASM_OP_I64_LOAD || last_op == WASM_OP_F64_LOAD)        \
        || (last_op >= WASM_OP_I64_LOAD8_S && last_op <= WASM_OP_I64_LOAD32_U) \
        || (last_op >= WASM_OP_I64_EXTEND_S_I32                                \
            && last_op <= WASM_OP_I64_TRUNC_U_F64)                             \
        || (last_op >= WASM_OP_F64_CONVERT_S_I32                               \
            && last_op <= WASM_OP_F64_PROMOTE_F32)                             \
        || (last_op == WASM_OP_I64_REINTERPRET_F64)                            \
        || (last_op == WASM_OP_F64_REINTERPRET_I64)                            \
        || (last_op == EXT_OP_COPY_STACK_TOP_I64)

#define GET_CONST_OFFSET(type, val)                                    \
    do {                                                               \
        if (!(wasm_loader_get_const_offset(loader_ctx, type, &val,     \
                                           &operand_offset, error_buf, \
                                           error_buf_size)))           \
            goto fail;                                                 \
    } while (0)

#define GET_CONST_F32_OFFSET(type, fval)                               \
    do {                                                               \
        if (!(wasm_loader_get_const_offset(loader_ctx, type, &fval,    \
                                           &operand_offset, error_buf, \
                                           error_buf_size)))           \
            goto fail;                                                 \
    } while (0)

#define GET_CONST_F64_OFFSET(type, fval)                               \
    do {                                                               \
        if (!(wasm_loader_get_const_offset(loader_ctx, type, &fval,    \
                                           &operand_offset, error_buf, \
                                           error_buf_size)))           \
            goto fail;                                                 \
    } while (0)

#define emit_operand(ctx, offset)            \
    do {                                     \
        wasm_loader_emit_int16(ctx, offset); \
        LOG_OP("%d\t", offset);              \
    } while (0)

#define emit_byte(ctx, byte)               \
    do {                                   \
        wasm_loader_emit_uint8(ctx, byte); \
        LOG_OP("%d\t", byte);              \
    } while (0)

#define emit_uint32(ctx, value)              \
    do {                                     \
        wasm_loader_emit_uint32(ctx, value); \
        LOG_OP("%d\t", value);               \
    } while (0)

#define emit_uint64(ctx, value)                     \
    do {                                            \
        wasm_loader_emit_const(ctx, &value, false); \
        LOG_OP("%lld\t", value);                    \
    } while (0)

#define emit_float32(ctx, value)                   \
    do {                                           \
        wasm_loader_emit_const(ctx, &value, true); \
        LOG_OP("%f\t", value);                     \
    } while (0)

#define emit_float64(ctx, value)                    \
    do {                                            \
        wasm_loader_emit_const(ctx, &value, false); \
        LOG_OP("%f\t", value);                      \
    } while (0)

static bool
wasm_loader_ctx_reinit(WASMLoaderContext *ctx)
{
    if (!(ctx->p_code_compiled =
              loader_malloc(ctx->code_compiled_peak_size, NULL, 0)))
        return false;
    ctx->p_code_compiled_end =
        ctx->p_code_compiled + ctx->code_compiled_peak_size;

    /* clean up frame ref */
    memset(ctx->frame_ref_bottom, 0, ctx->frame_ref_size);
    ctx->frame_ref = ctx->frame_ref_bottom;
    ctx->stack_cell_num = 0;

    /* clean up frame csp */
    memset(ctx->frame_csp_bottom, 0, ctx->frame_csp_size);
    ctx->frame_csp = ctx->frame_csp_bottom;
    ctx->csp_num = 0;
    ctx->max_csp_num = 0;

    /* clean up frame offset */
    memset(ctx->frame_offset_bottom, 0, ctx->frame_offset_size);
    ctx->frame_offset = ctx->frame_offset_bottom;
    ctx->dynamic_offset = ctx->start_dynamic_offset;

    /* init preserved local offsets */
    ctx->preserved_local_offset = ctx->max_dynamic_offset;

    /* const buf is reserved */
    return true;
}

static void
increase_compiled_code_space(WASMLoaderContext *ctx, int32 size)
{
    ctx->code_compiled_size += size;
    if (ctx->code_compiled_size >= ctx->code_compiled_peak_size) {
        ctx->code_compiled_peak_size = ctx->code_compiled_size;
    }
}

static void
wasm_loader_emit_const(WASMLoaderContext *ctx, void *value, bool is_32_bit)
{
    uint32 size = is_32_bit ? sizeof(uint32) : sizeof(uint64);

    if (ctx->p_code_compiled) {
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS == 0
        bh_assert(((uintptr_t)ctx->p_code_compiled & 1) == 0);
#endif
        bh_memcpy_s(ctx->p_code_compiled,
                    (uint32)(ctx->p_code_compiled_end - ctx->p_code_compiled),
                    value, size);
        ctx->p_code_compiled += size;
    }
    else {
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS == 0
        bh_assert((ctx->code_compiled_size & 1) == 0);
#endif
        increase_compiled_code_space(ctx, size);
    }
}

static void
wasm_loader_emit_uint32(WASMLoaderContext *ctx, uint32 value)
{
    if (ctx->p_code_compiled) {
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS == 0
        bh_assert(((uintptr_t)ctx->p_code_compiled & 1) == 0);
#endif
        STORE_U32(ctx->p_code_compiled, value);
        ctx->p_code_compiled += sizeof(uint32);
    }
    else {
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS == 0
        bh_assert((ctx->code_compiled_size & 1) == 0);
#endif
        increase_compiled_code_space(ctx, sizeof(uint32));
    }
}

static void
wasm_loader_emit_int16(WASMLoaderContext *ctx, int16 value)
{
    if (ctx->p_code_compiled) {
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS == 0
        bh_assert(((uintptr_t)ctx->p_code_compiled & 1) == 0);
#endif
        STORE_U16(ctx->p_code_compiled, (uint16)value);
        ctx->p_code_compiled += sizeof(int16);
    }
    else {
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS == 0
        bh_assert((ctx->code_compiled_size & 1) == 0);
#endif
        increase_compiled_code_space(ctx, sizeof(uint16));
    }
}

static void
wasm_loader_emit_uint8(WASMLoaderContext *ctx, uint8 value)
{
    if (ctx->p_code_compiled) {
        *(ctx->p_code_compiled) = value;
        ctx->p_code_compiled += sizeof(uint8);
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS == 0
        ctx->p_code_compiled++;
        bh_assert(((uintptr_t)ctx->p_code_compiled & 1) == 0);
#endif
    }
    else {
        increase_compiled_code_space(ctx, sizeof(uint8));
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS == 0
        increase_compiled_code_space(ctx, sizeof(uint8));
        bh_assert((ctx->code_compiled_size & 1) == 0);
#endif
    }
}

static void
wasm_loader_emit_ptr(WASMLoaderContext *ctx, void *value)
{
    if (ctx->p_code_compiled) {
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS == 0
        bh_assert(((uintptr_t)ctx->p_code_compiled & 1) == 0);
#endif
        STORE_PTR(ctx->p_code_compiled, value);
        ctx->p_code_compiled += sizeof(void *);
    }
    else {
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS == 0
        bh_assert((ctx->code_compiled_size & 1) == 0);
#endif
        increase_compiled_code_space(ctx, sizeof(void *));
    }
}

static void
wasm_loader_emit_backspace(WASMLoaderContext *ctx, uint32 size)
{
    if (ctx->p_code_compiled) {
        ctx->p_code_compiled -= size;
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS == 0
        if (size == sizeof(uint8)) {
            ctx->p_code_compiled--;
            bh_assert(((uintptr_t)ctx->p_code_compiled & 1) == 0);
        }
#endif
    }
    else {
        ctx->code_compiled_size -= size;
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS == 0
        if (size == sizeof(uint8)) {
            ctx->code_compiled_size--;
            bh_assert((ctx->code_compiled_size & 1) == 0);
        }
#endif
    }
}

static bool
preserve_referenced_local(WASMLoaderContext *loader_ctx, uint8 opcode,
                          uint32 local_index, uint32 local_type,
                          bool *preserved, char *error_buf,
                          uint32 error_buf_size)
{
    uint32 i = 0;
    int16 preserved_offset = (int16)local_index;

    *preserved = false;
    while (i < loader_ctx->stack_cell_num) {
        uint8 cur_type = loader_ctx->frame_ref_bottom[i];

        /* move previous local into dynamic space before a set/tee_local opcode
         */
        if (loader_ctx->frame_offset_bottom[i] == (int16)local_index) {
            if (!(*preserved)) {
                *preserved = true;
                skip_label();
                preserved_offset = loader_ctx->preserved_local_offset;
                if (loader_ctx->p_code_compiled) {
                    bh_assert(preserved_offset != (int16)local_index);
                }
                if (is_32bit_type(local_type)) {
                    /* Only increase preserve offset in the second traversal */
                    if (loader_ctx->p_code_compiled)
                        loader_ctx->preserved_local_offset++;
                    emit_label(EXT_OP_COPY_STACK_TOP);
                }
                else {
                    if (loader_ctx->p_code_compiled)
                        loader_ctx->preserved_local_offset += 2;
                    emit_label(EXT_OP_COPY_STACK_TOP_I64);
                }
                emit_operand(loader_ctx, local_index);
                emit_operand(loader_ctx, preserved_offset);
                emit_label(opcode);
            }
            loader_ctx->frame_offset_bottom[i] = preserved_offset;
        }

        if (is_32bit_type(cur_type))
            i++;
        else
            i += 2;
    }

    (void)error_buf;
    (void)error_buf_size;
    return true;
#if WASM_ENABLE_LABELS_AS_VALUES != 0
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS == 0
fail:
    return false;
#endif
#endif
}

static bool
preserve_local_for_block(WASMLoaderContext *loader_ctx, uint8 opcode,
                         char *error_buf, uint32 error_buf_size)
{
    uint32 i = 0;
    bool preserve_local;

    /* preserve locals before blocks to ensure that "tee/set_local" inside
        blocks will not influence the value of these locals */
    while (i < loader_ctx->stack_cell_num) {
        int16 cur_offset = loader_ctx->frame_offset_bottom[i];
        uint8 cur_type = loader_ctx->frame_ref_bottom[i];

        if ((cur_offset < loader_ctx->start_dynamic_offset)
            && (cur_offset >= 0)) {
            if (!(preserve_referenced_local(loader_ctx, opcode, cur_offset,
                                            cur_type, &preserve_local,
                                            error_buf, error_buf_size)))
                return false;
        }

        if (is_32bit_type(cur_type)) {
            i++;
        }
        else {
            i += 2;
        }
    }

    return true;
}

static bool
add_label_patch_to_list(BranchBlock *frame_csp, uint8 patch_type,
                        uint8 *p_code_compiled, char *error_buf,
                        uint32 error_buf_size)
{
    BranchBlockPatch *patch =
        loader_malloc(sizeof(BranchBlockPatch), error_buf, error_buf_size);
    if (!patch) {
        return false;
    }
    patch->patch_type = patch_type;
    patch->code_compiled = p_code_compiled;
    if (!frame_csp->patch_list) {
        frame_csp->patch_list = patch;
        patch->next = NULL;
    }
    else {
        patch->next = frame_csp->patch_list;
        frame_csp->patch_list = patch;
    }
    return true;
}

static void
apply_label_patch(WASMLoaderContext *ctx, uint8 depth, uint8 patch_type)
{
    BranchBlock *frame_csp = ctx->frame_csp - depth;
    BranchBlockPatch *node = frame_csp->patch_list;
    BranchBlockPatch *node_prev = NULL, *node_next;

    if (!ctx->p_code_compiled)
        return;

    while (node) {
        node_next = node->next;
        if (node->patch_type == patch_type) {
            STORE_PTR(node->code_compiled, ctx->p_code_compiled);
            if (node_prev == NULL) {
                frame_csp->patch_list = node_next;
            }
            else {
                node_prev->next = node_next;
            }
            wasm_runtime_free(node);
        }
        else {
            node_prev = node;
        }
        node = node_next;
    }
}

static bool
wasm_loader_emit_br_info(WASMLoaderContext *ctx, BranchBlock *frame_csp,
                         char *error_buf, uint32 error_buf_size)
{
    /* br info layout:
     *  a) arity of target block
     *  b) total cell num of arity values
     *  c) each arity value's cell num
     *  d) each arity value's src frame offset
     *  e) each arity values's dst dynamic offset
     *  f) branch target address
     *
     *  Note: b-e are omitted when arity is 0 so that
     *  interpreter can recover the br info quickly.
     */
    BlockType *block_type = &frame_csp->block_type;
    uint8 *types = NULL, cell;
    uint32 arity = 0;
    int32 i;
    int16 *frame_offset = ctx->frame_offset;
    uint16 dynamic_offset;

    /* Note: loop's arity is different from if and block. loop's arity is
     * its parameter count while if and block arity is result count.
     */
    if (frame_csp->label_type == LABEL_TYPE_LOOP)
        arity = block_type_get_param_types(block_type, &types);
    else
        arity = block_type_get_result_types(block_type, &types);

    /* Part a */
    emit_uint32(ctx, arity);

    if (arity) {
        /* Part b */
        emit_uint32(ctx, wasm_get_cell_num(types, arity));
        /* Part c */
        for (i = (int32)arity - 1; i >= 0; i--) {
            cell = (uint8)wasm_value_type_cell_num(types[i]);
            emit_byte(ctx, cell);
        }
        /* Part d */
        for (i = (int32)arity - 1; i >= 0; i--) {
            cell = (uint8)wasm_value_type_cell_num(types[i]);
            frame_offset -= cell;
            emit_operand(ctx, *(int16 *)(frame_offset));
        }
        /* Part e */
        dynamic_offset =
            frame_csp->dynamic_offset + wasm_get_cell_num(types, arity);
        for (i = (int32)arity - 1; i >= 0; i--) {
            cell = (uint8)wasm_value_type_cell_num(types[i]);
            dynamic_offset -= cell;
            emit_operand(ctx, dynamic_offset);
        }
    }

    /* Part f */
    if (frame_csp->label_type == LABEL_TYPE_LOOP) {
        wasm_loader_emit_ptr(ctx, frame_csp->code_compiled);
    }
    else {
        if (!add_label_patch_to_list(frame_csp, PATCH_END, ctx->p_code_compiled,
                                     error_buf, error_buf_size))
            return false;
        /* label address, to be patched */
        wasm_loader_emit_ptr(ctx, NULL);
    }

    return true;
}

static bool
wasm_loader_push_frame_offset(WASMLoaderContext *ctx, uint8 type,
                              bool disable_emit, int16 operand_offset,
                              char *error_buf, uint32 error_buf_size)
{
    if (type == VALUE_TYPE_VOID)
        return true;

    /* only check memory overflow in first traverse */
    if (ctx->p_code_compiled == NULL) {
        if (!check_offset_push(ctx, error_buf, error_buf_size))
            return false;
    }

    if (disable_emit)
        *(ctx->frame_offset)++ = operand_offset;
    else {
        emit_operand(ctx, ctx->dynamic_offset);
        *(ctx->frame_offset)++ = ctx->dynamic_offset;
        ctx->dynamic_offset++;
        if (ctx->dynamic_offset > ctx->max_dynamic_offset) {
            ctx->max_dynamic_offset = ctx->dynamic_offset;
            if (ctx->max_dynamic_offset >= INT16_MAX) {
                goto fail;
            }
        }
    }

    if (is_32bit_type(type))
        return true;

    if (ctx->p_code_compiled == NULL) {
        if (!check_offset_push(ctx, error_buf, error_buf_size))
            return false;
    }

    ctx->frame_offset++;
    if (!disable_emit) {
        ctx->dynamic_offset++;
        if (ctx->dynamic_offset > ctx->max_dynamic_offset) {
            ctx->max_dynamic_offset = ctx->dynamic_offset;
            if (ctx->max_dynamic_offset >= INT16_MAX) {
                goto fail;
            }
        }
    }
    return true;

fail:
    set_error_buf(error_buf, error_buf_size,
                  "fast interpreter offset overflow");
    return false;
}

/* This function should be in front of wasm_loader_pop_frame_ref
    as they both use ctx->stack_cell_num, and ctx->stack_cell_num
    will be modified by wasm_loader_pop_frame_ref */
static bool
wasm_loader_pop_frame_offset(WASMLoaderContext *ctx, uint8 type,
                             char *error_buf, uint32 error_buf_size)
{
    /* if ctx->frame_csp equals ctx->frame_csp_bottom,
        then current block is the function block */
    uint32 depth = ctx->frame_csp > ctx->frame_csp_bottom ? 1 : 0;
    BranchBlock *cur_block = ctx->frame_csp - depth;
    int32 available_stack_cell =
        (int32)(ctx->stack_cell_num - cur_block->stack_cell_num);

    /* Directly return success if current block is in stack
     * polymorphic state while stack is empty. */
    if (available_stack_cell <= 0 && cur_block->is_stack_polymorphic)
        return true;

    if (type == VALUE_TYPE_VOID)
        return true;

    if (is_32bit_type(type)) {
        /* Check the offset stack bottom to ensure the frame offset
            stack will not go underflow. But we don't thrown error
            and return true here, because the error msg should be
            given in wasm_loader_pop_frame_ref */
        if (!check_offset_pop(ctx, 1))
            return true;

        ctx->frame_offset -= 1;
        if ((*(ctx->frame_offset) > ctx->start_dynamic_offset)
            && (*(ctx->frame_offset) < ctx->max_dynamic_offset))
            ctx->dynamic_offset -= 1;
    }
    else {
        if (!check_offset_pop(ctx, 2))
            return true;

        ctx->frame_offset -= 2;
        if ((*(ctx->frame_offset) > ctx->start_dynamic_offset)
            && (*(ctx->frame_offset) < ctx->max_dynamic_offset))
            ctx->dynamic_offset -= 2;
    }
    emit_operand(ctx, *(ctx->frame_offset));

    (void)error_buf;
    (void)error_buf_size;
    return true;
}

static bool
wasm_loader_push_pop_frame_offset(WASMLoaderContext *ctx, uint8 pop_cnt,
                                  uint8 type_push, uint8 type_pop,
                                  bool disable_emit, int16 operand_offset,
                                  char *error_buf, uint32 error_buf_size)
{
    uint8 i;

    for (i = 0; i < pop_cnt; i++) {
        if (!wasm_loader_pop_frame_offset(ctx, type_pop, error_buf,
                                          error_buf_size))
            return false;
    }
    if (!wasm_loader_push_frame_offset(ctx, type_push, disable_emit,
                                       operand_offset, error_buf,
                                       error_buf_size))
        return false;

    return true;
}

static bool
wasm_loader_push_frame_ref_offset(WASMLoaderContext *ctx, uint8 type,
                                  bool disable_emit, int16 operand_offset,
                                  char *error_buf, uint32 error_buf_size)
{
    if (!(wasm_loader_push_frame_offset(ctx, type, disable_emit, operand_offset,
                                        error_buf, error_buf_size)))
        return false;
    if (!(wasm_loader_push_frame_ref(ctx, type, error_buf, error_buf_size)))
        return false;

    return true;
}

static bool
wasm_loader_pop_frame_ref_offset(WASMLoaderContext *ctx, uint8 type,
                                 char *error_buf, uint32 error_buf_size)
{
    /* put wasm_loader_pop_frame_offset in front of wasm_loader_pop_frame_ref */
    if (!wasm_loader_pop_frame_offset(ctx, type, error_buf, error_buf_size))
        return false;
    if (!wasm_loader_pop_frame_ref(ctx, type, error_buf, error_buf_size))
        return false;

    return true;
}

static bool
wasm_loader_push_pop_frame_ref_offset(WASMLoaderContext *ctx, uint8 pop_cnt,
                                      uint8 type_push, uint8 type_pop,
                                      bool disable_emit, int16 operand_offset,
                                      char *error_buf, uint32 error_buf_size)
{
    if (!wasm_loader_push_pop_frame_offset(ctx, pop_cnt, type_push, type_pop,
                                           disable_emit, operand_offset,
                                           error_buf, error_buf_size))
        return false;
    if (!wasm_loader_push_pop_frame_ref(ctx, pop_cnt, type_push, type_pop,
                                        error_buf, error_buf_size))
        return false;

    return true;
}

static bool
wasm_loader_get_const_offset(WASMLoaderContext *ctx, uint8 type, void *value,
                             int16 *offset, char *error_buf,
                             uint32 error_buf_size)
{
    int8 bytes_to_increase;
    int16 operand_offset = 0;
    Const *c;

    /* Search existing constant */
    for (c = (Const *)ctx->const_buf;
         (uint8 *)c < ctx->const_buf + ctx->num_const * sizeof(Const); c++) {
        /* TODO: handle v128 type? */
        if ((type == c->value_type)
            && ((type == VALUE_TYPE_I64 && *(int64 *)value == c->value.i64)
                || (type == VALUE_TYPE_I32 && *(int32 *)value == c->value.i32)
#if WASM_ENABLE_REF_TYPES != 0
                || (type == VALUE_TYPE_FUNCREF
                    && *(int32 *)value == c->value.i32)
                || (type == VALUE_TYPE_EXTERNREF
                    && *(int32 *)value == c->value.i32)
#endif
                || (type == VALUE_TYPE_F64
                    && (0 == memcmp(value, &(c->value.f64), sizeof(float64))))
                || (type == VALUE_TYPE_F32
                    && (0
                        == memcmp(value, &(c->value.f32), sizeof(float32)))))) {
            operand_offset = c->slot_index;
            break;
        }
        if (is_32bit_type(c->value_type))
            operand_offset += 1;
        else
            operand_offset += 2;
    }

    if ((uint8 *)c == ctx->const_buf + ctx->num_const * sizeof(Const)) {
        /* New constant, append to the const buffer */
        if ((type == VALUE_TYPE_F64) || (type == VALUE_TYPE_I64)) {
            bytes_to_increase = 2;
        }
        else {
            bytes_to_increase = 1;
        }

        /* The max cell num of const buffer is 32768 since the valid index range
         * is -32768 ~ -1. Return an invalid index 0 to indicate the buffer is
         * full */
        if (ctx->const_cell_num > INT16_MAX - bytes_to_increase + 1) {
            *offset = 0;
            return true;
        }

        if ((uint8 *)c == ctx->const_buf + ctx->const_buf_size) {
            MEM_REALLOC(ctx->const_buf, ctx->const_buf_size,
                        ctx->const_buf_size + 4 * sizeof(Const));
            ctx->const_buf_size += 4 * sizeof(Const);
            c = (Const *)(ctx->const_buf + ctx->num_const * sizeof(Const));
        }
        c->value_type = type;
        switch (type) {
            case VALUE_TYPE_F64:
                bh_memcpy_s(&(c->value.f64), sizeof(WASMValue), value,
                            sizeof(float64));
                ctx->const_cell_num += 2;
                /* The const buf will be reversed, we use the second cell */
                /* of the i64/f64 const so the finnal offset is corrent */
                operand_offset++;
                break;
            case VALUE_TYPE_I64:
                c->value.i64 = *(int64 *)value;
                ctx->const_cell_num += 2;
                operand_offset++;
                break;
            case VALUE_TYPE_F32:
                bh_memcpy_s(&(c->value.f32), sizeof(WASMValue), value,
                            sizeof(float32));
                ctx->const_cell_num++;
                break;
            case VALUE_TYPE_I32:
                c->value.i32 = *(int32 *)value;
                ctx->const_cell_num++;
                break;
#if WASM_ENABLE_REF_TYPES != 0
            case VALUE_TYPE_EXTERNREF:
            case VALUE_TYPE_FUNCREF:
                c->value.i32 = *(int32 *)value;
                ctx->const_cell_num++;
                break;
#endif
            default:
                break;
        }
        c->slot_index = operand_offset;
        ctx->num_const++;
        LOG_OP("#### new const [%d]: %ld\n", ctx->num_const,
               (int64)c->value.i64);
    }
    /* use negetive index for const */
    operand_offset = -(operand_offset + 1);
    *offset = operand_offset;
    return true;
fail:
    return false;
}

/*
    PUSH(POP)_XXX = push(pop) frame_ref + push(pop) frame_offset
    -- Mostly used for the binary / compare operation
    PUSH(POP)_OFFSET_TYPE only push(pop) the frame_offset stack
    -- Mostly used in block / control instructions

    The POP will always emit the offset on the top of the frame_offset stack
    PUSH can be used in two ways:
    1. directly PUSH:
            PUSH_XXX();
        will allocate a dynamic space and emit
    2. silent PUSH:
            operand_offset = xxx; disable_emit = true;
            PUSH_XXX();
        only push the frame_offset stack, no emit
*/

#define TEMPLATE_PUSH(Type)                                                   \
    do {                                                                      \
        if (!wasm_loader_push_frame_ref_offset(loader_ctx, VALUE_TYPE_##Type, \
                                               disable_emit, operand_offset,  \
                                               error_buf, error_buf_size))    \
            goto fail;                                                        \
    } while (0)

#define TEMPLATE_POP(Type)                                                   \
    do {                                                                     \
        if (!wasm_loader_pop_frame_ref_offset(loader_ctx, VALUE_TYPE_##Type, \
                                              error_buf, error_buf_size))    \
            goto fail;                                                       \
    } while (0)

#define PUSH_OFFSET_TYPE(type)                                              \
    do {                                                                    \
        if (!(wasm_loader_push_frame_offset(loader_ctx, type, disable_emit, \
                                            operand_offset, error_buf,      \
                                            error_buf_size)))               \
            goto fail;                                                      \
    } while (0)

#define POP_OFFSET_TYPE(type)                                           \
    do {                                                                \
        if (!(wasm_loader_pop_frame_offset(loader_ctx, type, error_buf, \
                                           error_buf_size)))            \
            goto fail;                                                  \
    } while (0)

#define POP_AND_PUSH(type_pop, type_push)                         \
    do {                                                          \
        if (!(wasm_loader_push_pop_frame_ref_offset(              \
                loader_ctx, 1, type_push, type_pop, disable_emit, \
                operand_offset, error_buf, error_buf_size)))      \
            goto fail;                                            \
    } while (0)

/* type of POPs should be the same */
#define POP2_AND_PUSH(type_pop, type_push)                        \
    do {                                                          \
        if (!(wasm_loader_push_pop_frame_ref_offset(              \
                loader_ctx, 2, type_push, type_pop, disable_emit, \
                operand_offset, error_buf, error_buf_size)))      \
            goto fail;                                            \
    } while (0)

#else /* WASM_ENABLE_FAST_INTERP */

#define TEMPLATE_PUSH(Type)                                             \
    do {                                                                \
        if (!(wasm_loader_push_frame_ref(loader_ctx, VALUE_TYPE_##Type, \
                                         error_buf, error_buf_size)))   \
            goto fail;                                                  \
    } while (0)

#define TEMPLATE_POP(Type)                                             \
    do {                                                               \
        if (!(wasm_loader_pop_frame_ref(loader_ctx, VALUE_TYPE_##Type, \
                                        error_buf, error_buf_size)))   \
            goto fail;                                                 \
    } while (0)

#define POP_AND_PUSH(type_pop, type_push)                              \
    do {                                                               \
        if (!(wasm_loader_push_pop_frame_ref(loader_ctx, 1, type_push, \
                                             type_pop, error_buf,      \
                                             error_buf_size)))         \
            goto fail;                                                 \
    } while (0)

/* type of POPs should be the same */
#define POP2_AND_PUSH(type_pop, type_push)                             \
    do {                                                               \
        if (!(wasm_loader_push_pop_frame_ref(loader_ctx, 2, type_push, \
                                             type_pop, error_buf,      \
                                             error_buf_size)))         \
            goto fail;                                                 \
    } while (0)
#endif /* WASM_ENABLE_FAST_INTERP */

#define PUSH_I32() TEMPLATE_PUSH(I32)
#define PUSH_F32() TEMPLATE_PUSH(F32)
#define PUSH_I64() TEMPLATE_PUSH(I64)
#define PUSH_F64() TEMPLATE_PUSH(F64)
#define PUSH_V128() TEMPLATE_PUSH(V128)
#define PUSH_FUNCREF() TEMPLATE_PUSH(FUNCREF)
#define PUSH_EXTERNREF() TEMPLATE_PUSH(EXTERNREF)

#define POP_I32() TEMPLATE_POP(I32)
#define POP_F32() TEMPLATE_POP(F32)
#define POP_I64() TEMPLATE_POP(I64)
#define POP_F64() TEMPLATE_POP(F64)
#define POP_V128() TEMPLATE_POP(V128)
#define POP_FUNCREF() TEMPLATE_POP(FUNCREF)
#define POP_EXTERNREF() TEMPLATE_POP(EXTERNREF)

#if WASM_ENABLE_FAST_INTERP != 0

static bool
reserve_block_ret(WASMLoaderContext *loader_ctx, uint8 opcode,
                  bool disable_emit, char *error_buf, uint32 error_buf_size)
{
    int16 operand_offset = 0;
    BranchBlock *block = (opcode == WASM_OP_ELSE) ? loader_ctx->frame_csp - 1
                                                  : loader_ctx->frame_csp;
    BlockType *block_type = &block->block_type;
    uint8 *return_types = NULL;
    uint32 return_count = 0, value_count = 0, total_cel_num = 0;
    int32 i = 0;
    int16 dynamic_offset, dynamic_offset_org, *frame_offset = NULL,
                                              *frame_offset_org = NULL;

    return_count = block_type_get_result_types(block_type, &return_types);

    /* If there is only one return value, use EXT_OP_COPY_STACK_TOP/_I64 instead
     * of EXT_OP_COPY_STACK_VALUES for interpreter performance. */
    if (return_count == 1) {
        uint8 cell = (uint8)wasm_value_type_cell_num(return_types[0]);
        if (cell <= 2 /* V128 isn't supported whose cell num is 4 */
            && block->dynamic_offset != *(loader_ctx->frame_offset - cell)) {
            /* insert op_copy before else opcode */
            if (opcode == WASM_OP_ELSE)
                skip_label();
            emit_label(cell == 1 ? EXT_OP_COPY_STACK_TOP
                                 : EXT_OP_COPY_STACK_TOP_I64);
            emit_operand(loader_ctx, *(loader_ctx->frame_offset - cell));
            emit_operand(loader_ctx, block->dynamic_offset);

            if (opcode == WASM_OP_ELSE) {
                *(loader_ctx->frame_offset - cell) = block->dynamic_offset;
            }
            else {
                loader_ctx->frame_offset -= cell;
                loader_ctx->dynamic_offset = block->dynamic_offset;
                PUSH_OFFSET_TYPE(return_types[0]);
                wasm_loader_emit_backspace(loader_ctx, sizeof(int16));
            }
            if (opcode == WASM_OP_ELSE)
                emit_label(opcode);
        }
        return true;
    }

    /* Copy stack top values to block's results which are in dynamic space.
     * The instruction format:
     *   Part a: values count
     *   Part b: all values total cell num
     *   Part c: each value's cell_num, src offset and dst offset
     *   Part d: each value's src offset and dst offset
     *   Part e: each value's dst offset
     */
    frame_offset = frame_offset_org = loader_ctx->frame_offset;
    dynamic_offset = dynamic_offset_org =
        block->dynamic_offset + wasm_get_cell_num(return_types, return_count);

    /* First traversal to get the count of values needed to be copied. */
    for (i = (int32)return_count - 1; i >= 0; i--) {
        uint8 cells = (uint8)wasm_value_type_cell_num(return_types[i]);

        frame_offset -= cells;
        dynamic_offset -= cells;
        if (dynamic_offset != *frame_offset) {
            value_count++;
            total_cel_num += cells;
        }
    }

    if (value_count) {
        uint32 j = 0;
        uint8 *emit_data = NULL, *cells = NULL;
        int16 *src_offsets = NULL;
        uint16 *dst_offsets = NULL;
        uint64 size =
            (uint64)value_count
            * (sizeof(*cells) + sizeof(*src_offsets) + sizeof(*dst_offsets));

        /* Allocate memory for the emit data */
        if (!(emit_data = loader_malloc(size, error_buf, error_buf_size)))
            return false;

        cells = emit_data;
        src_offsets = (int16 *)(cells + value_count);
        dst_offsets = (uint16 *)(src_offsets + value_count);

        /* insert op_copy before else opcode */
        if (opcode == WASM_OP_ELSE)
            skip_label();
        emit_label(EXT_OP_COPY_STACK_VALUES);
        /* Part a) */
        emit_uint32(loader_ctx, value_count);
        /* Part b) */
        emit_uint32(loader_ctx, total_cel_num);

        /* Second traversal to get each value's cell num,  src offset and dst
         * offset. */
        frame_offset = frame_offset_org;
        dynamic_offset = dynamic_offset_org;
        for (i = (int32)return_count - 1, j = 0; i >= 0; i--) {
            uint8 cell = (uint8)wasm_value_type_cell_num(return_types[i]);
            frame_offset -= cell;
            dynamic_offset -= cell;
            if (dynamic_offset != *frame_offset) {
                /* cell num */
                cells[j] = cell;
                /* src offset */
                src_offsets[j] = *frame_offset;
                /* dst offset */
                dst_offsets[j] = dynamic_offset;
                j++;
            }
            if (opcode == WASM_OP_ELSE) {
                *frame_offset = dynamic_offset;
            }
            else {
                loader_ctx->frame_offset = frame_offset;
                loader_ctx->dynamic_offset = dynamic_offset;
                PUSH_OFFSET_TYPE(return_types[i]);
                wasm_loader_emit_backspace(loader_ctx, sizeof(int16));
                loader_ctx->frame_offset = frame_offset_org;
                loader_ctx->dynamic_offset = dynamic_offset_org;
            }
        }

        bh_assert(j == value_count);

        /* Emit the cells, src_offsets and dst_offsets */
        for (j = 0; j < value_count; j++)
            emit_byte(loader_ctx, cells[j]);
        for (j = 0; j < value_count; j++)
            emit_operand(loader_ctx, src_offsets[j]);
        for (j = 0; j < value_count; j++)
            emit_operand(loader_ctx, dst_offsets[j]);

        if (opcode == WASM_OP_ELSE)
            emit_label(opcode);

        wasm_runtime_free(emit_data);
    }

    return true;

fail:
    return false;
}
#endif /* WASM_ENABLE_FAST_INTERP */



#define RESERVE_BLOCK_RET()                                                 \
    do {                                                                    \
        if (!reserve_block_ret(loader_ctx, opcode, disable_emit, error_buf, \
                               error_buf_size))                             \
            goto fail;                                                      \
    } while (0)

#define PUSH_TYPE(type)                                               \
    do {                                                              \
        if (!(wasm_loader_push_frame_ref(loader_ctx, type, error_buf, \
                                         error_buf_size)))            \
            goto fail;                                                \
    } while (0)

#define POP_TYPE(type)                                               \
    do {                                                             \
        if (!(wasm_loader_pop_frame_ref(loader_ctx, type, error_buf, \
                                        error_buf_size)))            \
            goto fail;                                               \
    } while (0)

#define PUSH_CSP(label_type, block_type, _start_addr)                       \
    do {                                                                    \
        if (!wasm_loader_push_frame_csp(loader_ctx, label_type, block_type, \
                                        _start_addr, error_buf,             \
                                        error_buf_size))                    \
            goto fail;                                                      \
    } while (0)

#define POP_CSP()                                                              \
    do {                                                                       \
        if (!wasm_loader_pop_frame_csp(loader_ctx, error_buf, error_buf_size)) \
            goto fail;                                                         \
    } while (0)

#define GET_LOCAL_INDEX_TYPE_AND_OFFSET()                              \
    do {                                                               \
        read_leb_uint32(p, p_end, local_idx);                          \
        if (local_idx >= param_count + local_count) {                  \
            set_error_buf(error_buf, error_buf_size, "unknown local"); \
            goto fail;                                                 \
        }                                                              \
        local_type = local_idx < param_count                           \
                         ? param_types[local_idx]                      \
                         : local_types[local_idx - param_count];       \
        local_offset = local_offsets[local_idx];                       \
    } while (0)

static bool
check_memory(WASMModule *module, char *error_buf, uint32 error_buf_size)
{
    if (module->memory_count == 0 && module->import_memory_count == 0) {
        set_error_buf(error_buf, error_buf_size, "unknown memory");
        return false;
    }
    return true;
}

#define CHECK_MEMORY()                                        \
    do {                                                      \
        if (!check_memory(module, error_buf, error_buf_size)) \
            goto fail;                                        \
    } while (0)

static bool
check_memory_access_align(uint8 opcode, uint32 align, char *error_buf,
                          uint32 error_buf_size)
{
    uint8 mem_access_aligns[] = {
        2, 3, 2, 3, 0, 0, 1, 1, 0, 0, 1, 1, 2, 2, /* loads */
        2, 3, 2, 3, 0, 1, 0, 1, 2                 /* stores */
    };
    if (align > mem_access_aligns[opcode - WASM_OP_I32_LOAD]) {
        set_error_buf(error_buf, error_buf_size,
                      "alignment must not be larger than natural");
        return false;
    }
    return true;
}

static inline uint32
block_type_get_param_types(BlockType *block_type, uint8 **p_param_types)
{
    uint32 param_count = 0;
    if (!block_type->is_value_type) {
        WASMType *wasm_type = block_type->u.type;
        *p_param_types = wasm_type->param;
        param_count = wasm_type->param_count;
    }
    else {
        *p_param_types = NULL;
        param_count = 0;
    }

    return param_count;
}

static inline uint32
block_type_get_result_types(BlockType *block_type, uint8 **p_result_types)
{
    uint32 result_count = 0;
    if (block_type->is_value_type) {
        if (block_type->u.value_type != VALUE_TYPE_VOID) {
            *p_result_types = &block_type->u.value_type;
            result_count = 1;
        }
    }
    else {
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

static char *
type2str(uint8 type)
{
    char *type_str[] = { "v128", "f64", "f32", "i64", "i32" };

    if (type >= VALUE_TYPE_V128 && type <= VALUE_TYPE_I32)
        return type_str[type - VALUE_TYPE_V128];
    else if (type == VALUE_TYPE_FUNCREF)
        return "funcref";
    else if (type == VALUE_TYPE_EXTERNREF)
        return "externref";
    else
        return "unknown type";
}


static bool
wasm_loader_check_br(WASMLoaderContext *loader_ctx, uint32 depth,
                     char *error_buf, uint32 error_buf_size)
{
    BranchBlock *target_block, *cur_block;
    BlockType *target_block_type;
    uint8 *types = NULL, *frame_ref;
    uint32 arity = 0;
    int32 i, available_stack_cell;
    uint16 cell_num;

    if (loader_ctx->csp_num < depth + 1) {
        set_error_buf(error_buf, error_buf_size,
                      "unknown label, "
                      "unexpected end of section or function");
        return false;
    }

    cur_block = loader_ctx->frame_csp - 1;
    target_block = loader_ctx->frame_csp - (depth + 1);
    target_block_type = &target_block->block_type;
    frame_ref = loader_ctx->frame_ref;

    //对于loop需要特殊处理
    if (target_block->label_type == LABEL_TYPE_LOOP)
        arity = block_type_get_param_types(target_block_type, &types);
    else
        arity = block_type_get_result_types(target_block_type, &types);

    if (cur_block->is_stack_polymorphic) {
        for (i = (int32)arity - 1; i >= 0; i--) {
#if WASM_ENABLE_FAST_INTERP != 0
            POP_OFFSET_TYPE(types[i]);
#endif
            POP_TYPE(types[i]);
        }
        for (i = 0; i < (int32)arity; i++) {
#if WASM_ENABLE_FAST_INTERP != 0
            bool disable_emit = true;
            int16 operand_offset = 0;
            PUSH_OFFSET_TYPE(types[i]);
#endif
            PUSH_TYPE(types[i]);
        }
        return true;
    }

    available_stack_cell =
        (int32)(loader_ctx->stack_cell_num - cur_block->stack_cell_num);

    /* Check stack top values match target block type */
    for (i = (int32)arity - 1; i >= 0; i--) {
        if (!check_stack_pop(frame_ref, available_stack_cell, types[i],
                                    error_buf, error_buf_size))
            return false;
        cell_num = wasm_value_type_cell_num(types[i]);
        frame_ref -= cell_num;
        available_stack_cell -= cell_num;
    }

    return true;

fail:
    return false;
}

static BranchBlock *
check_branch_block(WASMLoaderContext *loader_ctx, uint8 **p_buf, uint8 *buf_end,
                   char *error_buf, uint32 error_buf_size)
{
    uint8 *p = *p_buf, *p_end = buf_end;
    BranchBlock *frame_csp_tmp;
    uint32 depth;

    read_leb_uint32(p, p_end, depth);
    if (!wasm_loader_check_br(loader_ctx, depth, error_buf, 
                                  error_buf_size))              
            goto fail;  
    frame_csp_tmp = loader_ctx->frame_csp - depth - 1;
#if WASM_ENABLE_FAST_INTERP != 0
    emit_br_info(frame_csp_tmp);
#endif

    *p_buf = p;
    return frame_csp_tmp;
fail:
    return NULL;
}

static bool
check_block_stack(WASMLoaderContext *loader_ctx, BranchBlock *block,
                  char *error_buf, uint32 error_buf_size)
{
    BlockType *block_type = &block->block_type;
    uint8 *return_types = NULL;
    uint32 return_count = 0;
    int32 available_stack_cell, return_cell_num, i;
    uint8 *frame_ref = NULL;

    available_stack_cell =
        (int32)(loader_ctx->stack_cell_num - block->stack_cell_num);

    return_count = block_type_get_result_types(block_type, &return_types);
    return_cell_num =
        return_count > 0 ? wasm_get_cell_num(return_types, return_count) : 0;

    if (block->is_stack_polymorphic) {
        for (i = (int32)return_count - 1; i >= 0; i--) {
#if WASM_ENABLE_FAST_INTERP != 0
            POP_OFFSET_TYPE(return_types[i]);
#endif
            POP_TYPE(return_types[i]);
        }

        /* Check stack is empty */
        if (loader_ctx->stack_cell_num != block->stack_cell_num) {
            set_error_buf(
                error_buf, error_buf_size,
                "type mismatch: stack size does not match block type");
            goto fail;
        }

        for (i = 0; i < (int32)return_count; i++) {
#if WASM_ENABLE_FAST_INTERP != 0
            bool disable_emit = true;
            int16 operand_offset = 0;
            PUSH_OFFSET_TYPE(return_types[i]);
#endif
            PUSH_TYPE(return_types[i]);
        }
        return true;
    }

    /* Check stack cell num equals return cell num */
    if (available_stack_cell != return_cell_num) {
        set_error_buf(error_buf, error_buf_size,
                      "type mismatch: stack size does not match block type");
        goto fail;
    }

    /* Check stack values match return types */
    frame_ref = loader_ctx->frame_ref;
    for (i = (int32)return_count - 1; i >= 0; i--) {
        if (!check_stack_pop(frame_ref, available_stack_cell,
                                    return_types[i], error_buf, error_buf_size))
            return false;
        frame_ref -= wasm_value_type_cell_num(return_types[i]);
        available_stack_cell -= wasm_value_type_cell_num(return_types[i]);
    }

    return true;

fail:
    return false;
}

#if WASM_ENABLE_FAST_INTERP != 0
/* Copy parameters to dynamic space.
 * 1) POP original parameter out;
 * 2) Push and copy original values to dynamic space.
 * The copy instruction format:
 *   Part a: param count
 *   Part b: all param total cell num
 *   Part c: each param's cell_num, src offset and dst offset
 *   Part d: each param's src offset
 *   Part e: each param's dst offset
 */
static bool
copy_params_to_dynamic_space(WASMLoaderContext *loader_ctx, bool is_if_block,
                             char *error_buf, uint32 error_buf_size)
{
    int16 *frame_offset = NULL;
    uint8 *cells = NULL, cell;
    int16 *src_offsets = NULL;
    uint8 *emit_data = NULL;
    uint32 i;
    BranchBlock *block = loader_ctx->frame_csp - 1;
    BlockType *block_type = &block->block_type;
    WASMType *wasm_type = block_type->u.type;
    uint32 param_count = block_type->u.type->param_count;
    int16 condition_offset = 0;
    bool disable_emit = false;
    int16 operand_offset = 0;

    uint64 size = (uint64)param_count * (sizeof(*cells) + sizeof(*src_offsets));

    /* For if block, we also need copy the condition operand offset. */
    if (is_if_block)
        size += sizeof(*cells) + sizeof(*src_offsets);

    /* Allocate memory for the emit data */
    if (!(emit_data = loader_malloc(size, error_buf, error_buf_size)))
        return false;

    cells = emit_data;
    src_offsets = (int16 *)(cells + param_count);

    if (is_if_block)
        condition_offset = *loader_ctx->frame_offset;

    /* POP original parameter out */
    for (i = 0; i < param_count; i++) {
        POP_OFFSET_TYPE(wasm_type->types[param_count - i - 1]);
        wasm_loader_emit_backspace(loader_ctx, sizeof(int16));
    }
    frame_offset = loader_ctx->frame_offset;

    /* Get each param's cell num and src offset */
    for (i = 0; i < param_count; i++) {
        cell = (uint8)wasm_value_type_cell_num(wasm_type->types[i]);
        cells[i] = cell;
        src_offsets[i] = *frame_offset;
        frame_offset += cell;
    }

    /* emit copy instruction */
    emit_label(EXT_OP_COPY_STACK_VALUES);
    /* Part a) */
    emit_uint32(loader_ctx, is_if_block ? param_count + 1 : param_count);
    /* Part b) */
    emit_uint32(loader_ctx, is_if_block ? wasm_type->param_cell_num + 1
                                        : wasm_type->param_cell_num);
    /* Part c) */
    for (i = 0; i < param_count; i++)
        emit_byte(loader_ctx, cells[i]);
    if (is_if_block)
        emit_byte(loader_ctx, 1);

    /* Part d) */
    for (i = 0; i < param_count; i++)
        emit_operand(loader_ctx, src_offsets[i]);
    if (is_if_block)
        emit_operand(loader_ctx, condition_offset);

    /* Part e) */
    /* Push to dynamic space. The push will emit the dst offset. */
    for (i = 0; i < param_count; i++)
        PUSH_OFFSET_TYPE(wasm_type->types[i]);
    if (is_if_block)
        PUSH_OFFSET_TYPE(VALUE_TYPE_I32);

    /* Free the emit data */
    wasm_runtime_free(emit_data);

    return true;

fail:
    return false;
}
#endif

/* reset the stack to the state of before entering the last block */
#if WASM_ENABLE_FAST_INTERP != 0
#define RESET_STACK()                                                     \
    do {                                                                  \
        loader_ctx->stack_cell_num =                                      \
            (loader_ctx->frame_csp - 1)->stack_cell_num;                  \
        loader_ctx->frame_ref =                                           \
            loader_ctx->frame_ref_bottom + loader_ctx->stack_cell_num;    \
        loader_ctx->frame_offset =                                        \
            loader_ctx->frame_offset_bottom + loader_ctx->stack_cell_num; \
    } while (0)
#else
#define RESET_STACK()                                                  \
    do {                                                               \
        loader_ctx->stack_cell_num =                                   \
            (loader_ctx->frame_csp - 1)->stack_cell_num;               \
        loader_ctx->frame_ref =                                        \
            loader_ctx->frame_ref_bottom + loader_ctx->stack_cell_num; \
    } while (0)
#endif

/* set current block's stack polymorphic state */
#define SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(flag)          \
    do {                                                     \
        BranchBlock *_cur_block = loader_ctx->frame_csp - 1; \
        _cur_block->is_stack_polymorphic = flag;             \
    } while (0)

#define BLOCK_HAS_PARAM(block_type) \
    (!block_type.is_value_type && block_type.u.type->param_count > 0)

#define PRESERVE_LOCAL_FOR_BLOCK()                                    \
    do {                                                              \
        if (!(preserve_local_for_block(loader_ctx, opcode, error_buf, \
                                       error_buf_size))) {            \
            goto fail;                                                \
        }                                                             \
    } while (0)

#if WASM_ENABLE_REF_TYPES != 0
static bool
get_table_elem_type(const WASMModule *module, uint32 table_idx,
                    uint8 *p_elem_type, char *error_buf, uint32 error_buf_size)
{
    if (!check_table_index(module, table_idx, error_buf, error_buf_size)) {
        return false;
    }

    if (p_elem_type) {
        if (table_idx < module->import_table_count)
            *p_elem_type = module->import_tables[table_idx].u.table.elem_type;
        else
            *p_elem_type =
                module->tables[module->import_table_count + table_idx]
                    .elem_type;
    }
    return true;
}

static bool
get_table_seg_elem_type(const WASMModule *module, uint32 table_seg_idx,
                        uint8 *p_elem_type, char *error_buf,
                        uint32 error_buf_size)
{
    if (table_seg_idx >= module->table_seg_count) {
        set_error_buf_v(error_buf, error_buf_size, "unknown elem segment %u",
                        table_seg_idx);
        return false;
    }

    if (p_elem_type) {
        *p_elem_type = module->table_segments[table_seg_idx].elem_type;
    }
    return true;
}
#endif

typedef struct Const {
    WASMValue value;
    uint16 slot_index;
    uint8 value_type;
} Const;

#define MEM_REALLOC(mem, size_new)                               \
    do {                                                                   \
        void *mem_new = wasm_runtime_realloc(mem, size_new);                    \
        if (!mem_new)                                                      \
            goto fail;                                                     \
        mem = mem_new;                                                     \
    } while (0)

#define CHECK_CSP_PUSH()                                                  \
    do {                                                                  \
        if (ctx->frame_csp >= ctx->frame_csp_boundary) {                  \
            MEM_REALLOC(                                                  \
                ctx->frame_csp_bottom,               \
                (uint32)(ctx->frame_csp_size + 8 * sizeof(BranchBlock))); \
            ctx->frame_csp_size += (uint32)(8 * sizeof(BranchBlock));     \
            ctx->frame_csp_boundary =                                     \
                ctx->frame_csp_bottom                                     \
                + ctx->frame_csp_size / sizeof(BranchBlock);              \
            ctx->frame_csp = ctx->frame_csp_bottom + ctx->csp_num;        \
        }                                                                 \
    } while (0)

#define CHECK_CSP_POP()                                             \
    do {                                                            \
        if (ctx->csp_num < 1) {                                     \
            set_error_buf(error_buf, error_buf_size,                \
                          "type mismatch: "                         \
                          "expect data but block stack was empty"); \
            goto fail;                                              \
        }                                                           \
    } while (0)

#if WASM_ENABLE_FAST_INTERP != 0
static bool
check_offset_push(WASMLoaderContext *ctx, char *error_buf,
                  uint32 error_buf_size)
{
    uint32 cell_num = (uint32)(ctx->frame_offset - ctx->frame_offset_bottom);
    if (ctx->frame_offset >= ctx->frame_offset_boundary) {
        MEM_REALLOC(ctx->frame_offset_bottom, ctx->frame_offset_size,
                    ctx->frame_offset_size + 16);
        ctx->frame_offset_size += 16;
        ctx->frame_offset_boundary =
            ctx->frame_offset_bottom + ctx->frame_offset_size / sizeof(int16);
        ctx->frame_offset = ctx->frame_offset_bottom + cell_num;
    }
    return true;
fail:
    return false;
}

static bool
check_offset_pop(WASMLoaderContext *ctx, uint32 cells)
{
    if (ctx->frame_offset - cells < ctx->frame_offset_bottom)
        return false;
    return true;
}

static void
free_label_patch_list(BranchBlock *frame_csp)
{
    BranchBlockPatch *label_patch = frame_csp->patch_list;
    BranchBlockPatch *next;
    while (label_patch != NULL) {
        next = label_patch->next;
        wasm_runtime_free(label_patch);
        label_patch = next;
    }
    frame_csp->patch_list = NULL;
}

static void
free_all_label_patch_lists(BranchBlock *frame_csp, uint32 csp_num)
{
    BranchBlock *tmp_csp = frame_csp;

    for (uint32 i = 0; i < csp_num; i++) {
        free_label_patch_list(tmp_csp);
        tmp_csp++;
    }
}

#endif /* end of WASM_ENABLE_FAST_INTERP */

static bool
check_table_index(const WASMModule *module, uint32 table_index, char *error_buf,
                  uint32 error_buf_size)
{
#if WASM_ENABLE_REF_TYPES == 0
    if (table_index != 0) {
        set_error_buf(error_buf, error_buf_size, "zero byte expected");
        return false;
    }
#endif

    if (table_index >= module->import_table_count + module->table_count) {
        set_error_buf_v(error_buf, error_buf_size, "unknown table %d",
                        table_index);
        return false;
    }
    return true;
}

static bool
check_function_index(const WASMModule *module, uint32 function_index,
                     char *error_buf, uint32 error_buf_size)
{
    if (function_index
        >= module->import_function_count + module->function_count) {
        set_error_buf_v(error_buf, error_buf_size, "unknown function %d",
                        function_index);
        return false;
    }
    return true;
}

static bool
check_stack_push(WASMLoaderContext *ctx, char *error_buf, uint32 error_buf_size)
{
    if (ctx->frame_ref >= ctx->frame_ref_boundary) {
        MEM_REALLOC(ctx->frame_ref_bottom, ctx->frame_ref_size + 16);
        ctx->frame_ref_size += 16;
        ctx->frame_ref_boundary = ctx->frame_ref_bottom + ctx->frame_ref_size;
        ctx->frame_ref = ctx->frame_ref_bottom + ctx->stack_cell_num;
    }
    return true;
fail:
    return false;
}

static void
wasm_loader_ctx_destroy(WASMLoaderContext *ctx)
{
    if (ctx) {
        if (ctx->frame_ref_bottom)
            wasm_runtime_free(ctx->frame_ref_bottom);
        if (ctx->frame_csp_bottom) {
#if WASM_ENABLE_FAST_INTERP != 0
            free_all_label_patch_lists(ctx->frame_csp_bottom, ctx->csp_num);
#endif
            wasm_runtime_free(ctx->frame_csp_bottom);
        }
#if WASM_ENABLE_FAST_INTERP != 0
        if (ctx->frame_offset_bottom)
            wasm_runtime_free(ctx->frame_offset_bottom);
        if (ctx->const_buf)
            wasm_runtime_free(ctx->const_buf);
#endif
        wasm_runtime_free(ctx);
    }
}

static WASMLoaderContext *
wasm_loader_ctx_init(WASMFunction *func, char *error_buf, uint32 error_buf_size)
{
    WASMLoaderContext *loader_ctx =
        wasm_runtime_malloc(sizeof(WASMLoaderContext));
    if (!loader_ctx)
        return NULL;
    loader_ctx->csp_num = 0;
    loader_ctx->max_csp_num = 0;
    loader_ctx->stack_cell_num = 0;
    loader_ctx->max_stack_cell_num = 0;
    loader_ctx->frame_ref_size = 32;
    if (!(loader_ctx->frame_ref_bottom = loader_ctx->frame_ref = wasm_runtime_malloc(
              loader_ctx->frame_ref_size)))
        goto fail;
    loader_ctx->frame_ref_boundary = loader_ctx->frame_ref_bottom + 32;

    loader_ctx->frame_csp_size = sizeof(BranchBlock) * 8;
    if (!(loader_ctx->frame_csp_bottom = loader_ctx->frame_csp = wasm_runtime_malloc(
              loader_ctx->frame_csp_size)))
        goto fail;
    loader_ctx->frame_csp_boundary = loader_ctx->frame_csp_bottom + 8;

#if WASM_ENABLE_FAST_INTERP != 0
    loader_ctx->frame_offset_size = sizeof(int16) * 32;
    if (!(loader_ctx->frame_offset_bottom = loader_ctx->frame_offset =
              loader_malloc(loader_ctx->frame_offset_size, error_buf,
                            error_buf_size)))
        goto fail;
    loader_ctx->frame_offset_boundary = loader_ctx->frame_offset_bottom + 32;

    loader_ctx->num_const = 0;
    loader_ctx->const_buf_size = sizeof(Const) * 8;
    if (!(loader_ctx->const_buf = loader_malloc(loader_ctx->const_buf_size,
                                                error_buf, error_buf_size)))
        goto fail;

    if (func->param_cell_num >= (int32)INT16_MAX - func->local_cell_num) {
        set_error_buf(error_buf, error_buf_size,
                      "fast interpreter offset overflow");
        goto fail;
    }

    loader_ctx->start_dynamic_offset = loader_ctx->dynamic_offset =
        loader_ctx->max_dynamic_offset =
            func->param_cell_num + func->local_cell_num;
#endif
    return loader_ctx;

fail:
    wasm_loader_ctx_destroy(loader_ctx);
    return NULL;
}

static bool
wasm_loader_push_pop_frame_ref(WASMLoaderContext *ctx, uint8 pop_cnt,
                               uint8 type_push, uint8 type_pop, char *error_buf,
                               uint32 error_buf_size)
{
    for (int i = 0; i < pop_cnt; i++) {
        if (!wasm_loader_pop_frame_ref(ctx, type_pop, error_buf,
                                       error_buf_size))
            return false;
    }
    if (!wasm_loader_push_frame_ref(ctx, type_push, error_buf, error_buf_size))
        return false;
    return true;
}

static bool
wasm_loader_push_frame_csp(WASMLoaderContext *ctx, uint8 label_type,
                           BlockType block_type, uint8 *start_addr,
                           char *error_buf, uint32 error_buf_size)
{
    CHECK_CSP_PUSH();
    memset(ctx->frame_csp, 0, sizeof(BranchBlock));
    ctx->frame_csp->label_type = label_type;
    ctx->frame_csp->block_type = block_type;
    ctx->frame_csp->start_addr = start_addr;
    ctx->frame_csp->stack_cell_num = ctx->stack_cell_num;
#if WASM_ENABLE_FAST_INTERP != 0
    ctx->frame_csp->dynamic_offset = ctx->dynamic_offset;
    ctx->frame_csp->patch_list = NULL;
#endif
    ctx->frame_csp++;
    ctx->csp_num++;
    if (ctx->csp_num > ctx->max_csp_num) {
        ctx->max_csp_num = ctx->csp_num;
        if (ctx->max_csp_num > UINT16_MAX) {
            set_error_buf(error_buf, error_buf_size,
                          "label stack depth limit exceeded");
            return false;
        }
    }
    return true;
fail:
    return false;
}

static bool
wasm_loader_pop_frame_csp(WASMLoaderContext *ctx, char *error_buf,
                          uint32 error_buf_size)
{
    CHECK_CSP_POP();
#if WASM_ENABLE_FAST_INTERP != 0
    if ((ctx->frame_csp - 1)->param_frame_offsets)
        wasm_runtime_free((ctx->frame_csp - 1)->param_frame_offsets);
#endif
    ctx->frame_csp--;
    ctx->csp_num--;

    return true;
fail:
    return false;
}

bool
wasm_validator_code(WASMModule *module, WASMFunction *func,
                             uint32 cur_func_idx, char *error_buf,
                             uint32 error_buf_size)
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
    bool return_value = false;
    WASMLoaderContext *loader_ctx;
    BranchBlock *frame_csp_tmp;
#if WASM_ENABLE_FAST_INTERP != 0
    uint8 *func_const_end, *func_const = NULL;
    int16 operand_offset = 0;
    uint8 last_op = 0;
    bool disable_emit, preserve_local = false;
    float32 f32_const;
    float64 f64_const;

    LOG_OP("\nProcessing func | [%d] params | [%d] locals | [%d] return\n",
           func->param_cell_num, func->local_cell_num, func->ret_cell_num);
#endif

    global_count = module->import_global_count + module->global_count;


    param_count = func->param_count;
    param_types = func->func_type->param;

    func_block_type.is_value_type = false;
    func_block_type.u.type = func->func_type;

    local_count = func->local_count;
    local_types = func->local_types;
    local_offsets = func->local_offsets;

    if (!(loader_ctx = wasm_loader_ctx_init(func, error_buf, error_buf_size))) {
        goto fail;
    }

#if WASM_ENABLE_FAST_INTERP != 0
    /* For the first traverse, the initial value of preserved_local_offset has
     * not been determined, we use the INT16_MAX to represent that a slot has
     * been copied to preserve space. For second traverse, this field will be
     * set to the appropriate value in wasm_loader_ctx_reinit.
     * This is for Issue #1230,
     * https://github.com/bytecodealliance/wasm-micro-runtime/issues/1230, the
     * drop opcodes need to know which slots are preserved, so those slots will
     * not be treated as dynamically allocated slots */
    loader_ctx->preserved_local_offset = INT16_MAX;

re_scan:
    if (loader_ctx->code_compiled_size > 0) {
        if (!wasm_loader_ctx_reinit(loader_ctx)) {
            set_error_buf(error_buf, error_buf_size, "allocate memory failed");
            goto fail;
        }
        p = func->code;
        func->code_compiled = loader_ctx->p_code_compiled;
        func->code_compiled_size = loader_ctx->code_compiled_size;
    }
#endif

    PUSH_CSP(LABEL_TYPE_FUNCTION, func_block_type, p);

    while (p < p_end) {
        opcode = *p++;
#if WASM_ENABLE_FAST_INTERP != 0
        p_org = p;
        disable_emit = false;
        emit_label(opcode);
#endif

        switch (opcode) {
            case WASM_OP_UNREACHABLE:
                RESET_STACK();
                SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(true);
                break;

            case WASM_OP_NOP:
#if WASM_ENABLE_FAST_INTERP != 0
                skip_label();
#endif
                break;

            case WASM_OP_IF:
#if WASM_ENABLE_FAST_INTERP != 0
                PRESERVE_LOCAL_FOR_BLOCK();
#endif
                POP_I32();
                goto handle_op_block_and_loop;
            case WASM_OP_BLOCK:
            case WASM_OP_LOOP:
#if WASM_ENABLE_FAST_INTERP != 0
                PRESERVE_LOCAL_FOR_BLOCK();
#endif
            handle_op_block_and_loop:
            {
                uint8 value_type;
                BlockType block_type;

                p_org = p - 1;
                value_type = read_uint8(p);
                if (is_byte_a_type(value_type)) {
                    /* If the first byte is one of these special values:
                     * 0x40/0x7F/0x7E/0x7D/0x7C, take it as the type of
                     * the single return value. */
                    block_type.is_value_type = true;
                    block_type.u.value_type = value_type;
                }
                else {
                    uint32 type_index;
                    /* Resolve the leb128 encoded type index as block type */
                    p--;
                    read_leb_uint32(p, p_end, type_index);
                    if (type_index >= module->type_count) {
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown type");
                        goto fail;
                    }
                    block_type.is_value_type = false;
                    block_type.u.type = module->types[type_index];
#if WASM_ENABLE_FAST_INTERP == 0
                    /* If block use type index as block type, change the opcode
                     * to new extended opcode so that interpreter can resolve
                     * the block quickly.
                     */
#if WASM_ENABLE_DEBUG_INTERP != 0
                    if (!record_fast_op(module, p_org, *p_org, error_buf,
                                        error_buf_size)) {
                        goto fail;
                    }
#endif
                    *p_org = EXT_OP_BLOCK + (opcode - WASM_OP_BLOCK);
#endif
                }

                /* Pop block parameters from stack */
                if (BLOCK_HAS_PARAM(block_type)) {
                    WASMType *wasm_type = block_type.u.type;
                    for (i = 0; i < wasm_type->param_count; i++)
                        POP_TYPE(
                            wasm_type->param[wasm_type->param_count - i - 1]);
                }

                PUSH_CSP(LABEL_TYPE_BLOCK + (opcode - WASM_OP_BLOCK),
                         block_type, p);

                /* Pass parameters to block */
                if (BLOCK_HAS_PARAM(block_type)) {
                    WASMType *wasm_type = block_type.u.type;
                    for (i = 0; i < wasm_type->param_count; i++)
                        PUSH_TYPE(wasm_type->param[i]);
                }

#if WASM_ENABLE_FAST_INTERP != 0
                if (opcode == WASM_OP_BLOCK) {
                    skip_label();
                }
                else if (opcode == WASM_OP_LOOP) {
                    skip_label();
                    if (BLOCK_HAS_PARAM(block_type)) {
                        /* Make sure params are in dynamic space */
                        if (!copy_params_to_dynamic_space(
                                loader_ctx, false, error_buf, error_buf_size))
                            goto fail;
                    }
                    (loader_ctx->frame_csp - 1)->code_compiled =
                        loader_ctx->p_code_compiled;
                }
                else if (opcode == WASM_OP_IF) {
                    /* If block has parameters, we should make sure they are in
                     * dynamic space. Otherwise, when else branch is missing,
                     * the later opcode may consume incorrect operand offset.
                     * Spec case:
                     *   (func (export "params-id") (param i32) (result i32)
                     *       (i32.const 1)
                     *       (i32.const 2)
                     *       (if (param i32 i32) (result i32 i32) (local.get 0)
                     * (then)) (i32.add)
                     *   )
                     *
                     * So we should emit a copy instruction before the if.
                     *
                     * And we also need to save the parameter offsets and
                     * recover them before entering else branch.
                     *
                     */
                    if (BLOCK_HAS_PARAM(block_type)) {
                        BranchBlock *block = loader_ctx->frame_csp - 1;
                        uint64 size;

                        /* skip the if condition operand offset */
                        wasm_loader_emit_backspace(loader_ctx, sizeof(int16));
                        /* skip the if label */
                        skip_label();
                        /* Emit a copy instruction */
                        if (!copy_params_to_dynamic_space(
                                loader_ctx, true, error_buf, error_buf_size))
                            goto fail;

                        /* Emit the if instruction */
                        emit_label(opcode);
                        /* Emit the new condition operand offset */
                        POP_OFFSET_TYPE(VALUE_TYPE_I32);

                        /* Save top param_count values of frame_offset stack, so
                         * that we can recover it before executing else branch
                         */
                        size = sizeof(int16)
                               * (uint64)block_type.u.type->param_cell_num;
                        if (!(block->param_frame_offsets = loader_malloc(
                                  size, error_buf, error_buf_size)))
                            goto fail;
                        bh_memcpy_s(block->param_frame_offsets, (uint32)size,
                                    loader_ctx->frame_offset
                                        - size / sizeof(int16),
                                    (uint32)size);
                    }

                    emit_empty_label_addr_and_frame_ip(PATCH_ELSE);
                    emit_empty_label_addr_and_frame_ip(PATCH_END);
                }
#endif
                break;
            }

            case WASM_OP_ELSE:
            {
                BlockType block_type = (loader_ctx->frame_csp - 1)->block_type;

                if (loader_ctx->csp_num < 2
                    || (loader_ctx->frame_csp - 1)->label_type
                           != LABEL_TYPE_IF) {
                    set_error_buf(
                        error_buf, error_buf_size,
                        "opcode else found without matched opcode if");
                    goto fail;
                }

                /* check whether if branch's stack matches its result type */
                if (!check_block_stack(loader_ctx, loader_ctx->frame_csp - 1,
                                       error_buf, error_buf_size))
                    goto fail;

                (loader_ctx->frame_csp - 1)->else_addr = p - 1;

#if WASM_ENABLE_FAST_INTERP != 0
                /* if the result of if branch is in local or const area, add a
                 * copy op */
                RESERVE_BLOCK_RET();

                emit_empty_label_addr_and_frame_ip(PATCH_END);
                apply_label_patch(loader_ctx, 1, PATCH_ELSE);
#endif
                RESET_STACK();
                SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(false);

                /* Pass parameters to if-false branch */
                if (BLOCK_HAS_PARAM(block_type)) {
                    for (i = 0; i < block_type.u.type->param_count; i++)
                        PUSH_TYPE(block_type.u.type->param[i]);
                }

#if WASM_ENABLE_FAST_INTERP != 0
                /* Recover top param_count values of frame_offset stack */
                if (BLOCK_HAS_PARAM((block_type))) {
                    uint32 size;
                    BranchBlock *block = loader_ctx->frame_csp - 1;
                    size = sizeof(int16) * block_type.u.type->param_cell_num;
                    bh_memcpy_s(loader_ctx->frame_offset, size,
                                block->param_frame_offsets, size);
                    loader_ctx->frame_offset += (size / sizeof(int16));
                }
#endif

                break;
            }

            case WASM_OP_END:
            {
                BranchBlock *cur_block = loader_ctx->frame_csp - 1;

                /* check whether block stack matches its result type */
                if (!check_block_stack(loader_ctx, cur_block, error_buf,
                                       error_buf_size))
                    goto fail;

                /* if no else branch, and return types do not match param types,
                 * fail */
                if (cur_block->label_type == LABEL_TYPE_IF
                    && !cur_block->else_addr) {
                    uint32 block_param_count = 0, block_ret_count = 0;
                    uint8 *block_param_types = NULL, *block_ret_types = NULL;
                    BlockType *cur_block_type = &cur_block->block_type;
                    if (cur_block_type->is_value_type) {
                        if (cur_block_type->u.value_type != VALUE_TYPE_VOID) {
                            block_ret_count = 1;
                            block_ret_types = &cur_block_type->u.value_type;
                        }
                    }
                    else {
                        block_param_count = cur_block_type->u.type->param_count;
                        block_ret_count = cur_block_type->u.type->result_count;
                        block_param_types = cur_block_type->u.type->param;
                        block_ret_types = cur_block_type->u.type->result;
                    }
                    if (block_param_count != block_ret_count
                        || (block_param_count
                            && memcmp(block_param_types, block_ret_types,
                                      block_param_count))) {
                        set_error_buf(error_buf, error_buf_size,
                                      "type mismatch: else branch missing");
                        goto fail;
                    }
                }

                POP_CSP();

#if WASM_ENABLE_FAST_INTERP != 0
                skip_label();
                /* copy the result to the block return address */
                RESERVE_BLOCK_RET();

                apply_label_patch(loader_ctx, 0, PATCH_END);
                free_label_patch_list(loader_ctx->frame_csp);
                if (loader_ctx->frame_csp->label_type == LABEL_TYPE_FUNCTION) {
                    int32 idx;
                    uint8 ret_type;

                    emit_label(WASM_OP_RETURN);
                    for (idx = (int32)func->func_type->result_count - 1;
                         idx >= 0; idx--) {
                        ret_type = *(func->func_type->types
                                     + func->func_type->param_count + idx);
                        POP_OFFSET_TYPE(ret_type);
                    }
                }
#endif
                if (loader_ctx->csp_num > 0) {
                    loader_ctx->frame_csp->end_addr = p - 1;
                }
                else {
                    if (p < p_end) {
                        set_error_buf(error_buf, error_buf_size,
                                      "section size mismatch");
                        goto fail;
                    }
                }

                break;
            }

            case WASM_OP_BR:
            {
                if (!(frame_csp_tmp = check_branch_block(
                          loader_ctx, &p, p_end, error_buf, error_buf_size)))
                    goto fail;

                RESET_STACK();
                SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(true);
                break;
            }

            case WASM_OP_BR_IF:
            {
                POP_I32();

                if (!(frame_csp_tmp = check_branch_block(
                          loader_ctx, &p, p_end, error_buf, error_buf_size)))
                    goto fail;

                break;
            }

            case WASM_OP_BR_TABLE:
            {
                uint8 *ret_types = NULL;
                uint32 ret_count = 0;
#if WASM_ENABLE_FAST_INTERP == 0
                uint8 *p_depth_begin, *p_depth;
                uint32 depth, j;
                BrTableCache *br_table_cache = NULL;

                p_org = p - 1;
#endif

                read_leb_uint32(p, p_end, count);
#if WASM_ENABLE_FAST_INTERP != 0
                emit_uint32(loader_ctx, count);
#endif
                POP_I32();

#if WASM_ENABLE_FAST_INTERP == 0
                p_depth_begin = p_depth = p;
#endif
                for (i = 0; i <= count; i++) {
                    if (!(frame_csp_tmp =
                              check_branch_block(loader_ctx, &p, p_end,
                                                 error_buf, error_buf_size)))
                        goto fail;

                    if (i == 0) {
                        if (frame_csp_tmp->label_type != LABEL_TYPE_LOOP)
                            ret_count = block_type_get_result_types(
                                &frame_csp_tmp->block_type, &ret_types);
                    }
                    else {
                        uint8 *tmp_ret_types = NULL;
                        uint32 tmp_ret_count = 0;

                        /* Check whether all table items have the same return
                         * type */
                        if (frame_csp_tmp->label_type != LABEL_TYPE_LOOP)
                            tmp_ret_count = block_type_get_result_types(
                                &frame_csp_tmp->block_type, &tmp_ret_types);

                        if (ret_count != tmp_ret_count
                            || (ret_count
                                && 0
                                       != memcmp(ret_types, tmp_ret_types,
                                                 ret_count))) {
                            set_error_buf(
                                error_buf, error_buf_size,
                                "type mismatch: br_table targets must "
                                "all use same result type");
                            goto fail;
                        }
                    }

#if WASM_ENABLE_FAST_INTERP == 0
                    depth = (uint32)(loader_ctx->frame_csp - 1 - frame_csp_tmp);
                    if (br_table_cache) {
                        br_table_cache->br_depths[i] = depth;
                    }
                    else {
                        if (depth > 255) {
                            /* The depth cannot be stored in one byte,
                               create br_table cache to store each depth */
#if WASM_ENABLE_DEBUG_INTERP != 0
                            if (!record_fast_op(module, p_org, *p_org,
                                                error_buf, error_buf_size)) {
                                goto fail;
                            }
#endif
                            if (!(br_table_cache = wasm_runtime_malloc(
                                      offsetof(BrTableCache, br_depths)
                                          + sizeof(uint32)
                                                * (uint64)(count + 1)))) {
                                goto fail;
                            }
                            *p_org = EXT_OP_BR_TABLE_CACHE;
                            br_table_cache->br_table_op_addr = p_org;
                            br_table_cache->br_count = count;
                            /* Copy previous depths which are one byte */
                            for (j = 0; j < i; j++) {
                                br_table_cache->br_depths[j] = p_depth_begin[j];
                            }
                            br_table_cache->br_depths[i] = depth;
                            bh_list_insert(module->br_table_cache_list,
                                           br_table_cache);
                        }
                        else {
                            /* The depth can be stored in one byte, use the
                               byte of the leb to store it */
                            *p_depth++ = (uint8)depth;
                        }
                    }
#endif
                }

#if WASM_ENABLE_FAST_INTERP == 0
                /* Set the tailing bytes to nop */
                if (br_table_cache)
                    p_depth = p_depth_begin;
                while (p_depth < p)
                    *p_depth++ = WASM_OP_NOP;
#endif

                RESET_STACK();
                SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(true);
                break;
            }

            case WASM_OP_RETURN:
            {
                int32 idx;
                uint8 ret_type;
                for (idx = (int32)func->result_count - 1; idx >= 0;
                     idx--) {
                    ret_type = func->result_types[idx];
                    POP_TYPE(ret_type);
#if WASM_ENABLE_FAST_INTERP != 0
                    /* emit the offset after return opcode */
                    POP_OFFSET_TYPE(ret_type);
#endif
                }

                RESET_STACK();
                SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(true);

                break;
            }

            case WASM_OP_CALL:
#if WASM_ENABLE_TAIL_CALL != 0
            case WASM_OP_RETURN_CALL:
#endif
            {
                WASMType *func_type;
                int32 idx;

                read_leb_uint32(p, p_end, func_idx);
#if WASM_ENABLE_FAST_INTERP != 0
                /* we need to emit func_idx before arguments */
                emit_uint32(loader_ctx, func_idx);
#endif

                if (!check_function_index(module, func_idx, error_buf,
                                          error_buf_size)) {
                    goto fail;
                }

                func_type = module->functions[func_idx].func_type;

                if (func_type->param_count > 0) {
                    for (idx = (int32)(func_type->param_count - 1); idx >= 0;
                         idx--) {
                        POP_TYPE(func_type->param[idx]);
#if WASM_ENABLE_FAST_INTERP != 0
                        POP_OFFSET_TYPE(func_type->types[idx]);
#endif
                    }
                }

#if WASM_ENABLE_TAIL_CALL != 0
                if (opcode == WASM_OP_CALL) {
#endif
                    for (i = 0; i < func_type->result_count; i++) {
                        PUSH_TYPE(func_type->result[i]);
#if WASM_ENABLE_FAST_INTERP != 0
                        /* Here we emit each return value's dynamic_offset. But
                         * in fact these offsets are continuous, so interpreter
                         * only need to get the first return value's offset.
                         */
                        PUSH_OFFSET_TYPE(
                            func_type->types[func_type->param_count + i]);
#endif
                    }
#if WASM_ENABLE_TAIL_CALL != 0
                }
                else {
                    uint8 type;
                    if (func_type->result_count
                        != func->func_type->result_count) {
                        set_error_buf_v(error_buf, error_buf_size, "%s%u%s",
                                        "type mismatch: expect ",
                                        func->func_type->result_count,
                                        " return values but got other");
                        goto fail;
                    }
                    for (i = 0; i < func_type->result_count; i++) {
                        type = func->func_type
                                   ->types[func->func_type->param_count + i];
                        if (func_type->types[func_type->param_count + i]
                            != type) {
                            set_error_buf_v(error_buf, error_buf_size, "%s%s%s",
                                            "type mismatch: expect ",
                                            type2str(type), " but got other");
                            goto fail;
                        }
                    }
                    RESET_STACK();
                    SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(true);
                }
#endif
#if WASM_ENABLE_FAST_JIT != 0 || WASM_ENABLE_JIT != 0 \
    || WASM_ENABLE_WAMR_COMPILER != 0
                func->has_op_func_call = true;
#endif
                break;
            }

            /*
             * if disable reference type: call_indirect typeidx, 0x00
             * if enable reference type:  call_indirect typeidx, tableidx
             */
            case WASM_OP_CALL_INDIRECT:
#if WASM_ENABLE_TAIL_CALL != 0
            case WASM_OP_RETURN_CALL_INDIRECT:
#endif
            {
                int32 idx;
                WASMType *func_type;

                read_leb_uint32(p, p_end, type_idx);
#if WASM_ENABLE_REF_TYPES != 0
                read_leb_uint32(p, p_end, table_idx);
#else
                table_idx = read_uint8(p);
#endif
                if (!check_table_index(module, table_idx, error_buf,
                                       error_buf_size)) {
                    goto fail;
                }

#if WASM_ENABLE_FAST_INTERP != 0
                /* we need to emit before arguments */
#if WASM_ENABLE_TAIL_CALL != 0
                emit_byte(loader_ctx, opcode);
#endif
                emit_uint32(loader_ctx, type_idx);
                emit_uint32(loader_ctx, table_idx);
#endif

                /* skip elem idx */
                POP_I32();

                if (type_idx >= module->type_count) {
                    set_error_buf(error_buf, error_buf_size, "unknown type");
                    goto fail;
                }

                func_type = module->types[type_idx];

                if (func_type->param_count > 0) {
                    for (idx = (int32)(func_type->param_count - 1); idx >= 0;
                         idx--) {
                        POP_TYPE(func_type->param[idx]);
#if WASM_ENABLE_FAST_INTERP != 0
                        POP_OFFSET_TYPE(func_type->types[idx]);
#endif
                    }
                }

#if WASM_ENABLE_TAIL_CALL != 0
                if (opcode == WASM_OP_CALL_INDIRECT) {
#endif
                    for (i = 0; i < func_type->result_count; i++) {
                        PUSH_TYPE(func_type->result[i]);
#if WASM_ENABLE_FAST_INTERP != 0
                        PUSH_OFFSET_TYPE(
                            func_type->types[func_type->param_count + i]);
#endif
                    }
#if WASM_ENABLE_TAIL_CALL != 0
                }
                else {
                    uint8 type;
                    if (func_type->result_count
                        != func->func_type->result_count) {
                        set_error_buf_v(error_buf, error_buf_size, "%s%u%s",
                                        "type mismatch: expect ",
                                        func->func_type->result_count,
                                        " return values but got other");
                        goto fail;
                    }
                    for (i = 0; i < func_type->result_count; i++) {
                        type = func->func_type
                                   ->types[func->func_type->param_count + i];
                        if (func_type->types[func_type->param_count + i]
                            != type) {
                            set_error_buf_v(error_buf, error_buf_size, "%s%s%s",
                                            "type mismatch: expect ",
                                            type2str(type), " but got other");
                            goto fail;
                        }
                    }
                    RESET_STACK();
                    SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(true);
                }
#endif
#if WASM_ENABLE_FAST_JIT != 0 || WASM_ENABLE_JIT != 0 \
    || WASM_ENABLE_WAMR_COMPILER != 0
                func->has_op_func_call = true;
#endif
#if WASM_ENABLE_JIT != 0 || WASM_ENABLE_WAMR_COMPILER != 0
                func->has_op_call_indirect = true;
#endif
                break;
            }

            case WASM_OP_DROP:
            {
                BranchBlock *cur_block = loader_ctx->frame_csp - 1;
                int32 available_stack_cell =
                    (int32)(loader_ctx->stack_cell_num
                            - cur_block->stack_cell_num);

                if (available_stack_cell <= 0
                    && !cur_block->is_stack_polymorphic) {
                    set_error_buf(error_buf, error_buf_size,
                                  "type mismatch, opcode drop was found "
                                  "but stack was empty");
                    goto fail;
                }

                if (available_stack_cell > 0) {
                    if (is_32bit_type(*(loader_ctx->frame_ref - 1))) {
                        loader_ctx->frame_ref--;
                        loader_ctx->stack_cell_num--;
#if WASM_ENABLE_FAST_INTERP != 0
                        skip_label();
                        loader_ctx->frame_offset--;
                        if ((*(loader_ctx->frame_offset)
                             > loader_ctx->start_dynamic_offset)
                            && (*(loader_ctx->frame_offset)
                                < loader_ctx->max_dynamic_offset))
                            loader_ctx->dynamic_offset--;
#endif
                    }
                    else if (is_64bit_type(*(loader_ctx->frame_ref - 1))) {
                        loader_ctx->frame_ref -= 2;
                        loader_ctx->stack_cell_num -= 2;
#if WASM_ENABLE_FAST_INTERP == 0
                        *(p - 1) = WASM_OP_DROP_64;
#endif
#if WASM_ENABLE_FAST_INTERP != 0
                        skip_label();
                        loader_ctx->frame_offset -= 2;
                        if ((*(loader_ctx->frame_offset)
                             > loader_ctx->start_dynamic_offset)
                            && (*(loader_ctx->frame_offset)
                                < loader_ctx->max_dynamic_offset))
                            loader_ctx->dynamic_offset -= 2;
#endif
                    }
#if WASM_ENABLE_SIMD != 0
#if (WASM_ENABLE_WAMR_COMPILER != 0) || (WASM_ENABLE_JIT != 0)
                    else if (*(loader_ctx->frame_ref - 1) == REF_V128_1) {
                        loader_ctx->frame_ref -= 4;
                        loader_ctx->stack_cell_num -= 4;
                    }
#endif
#endif
                    else {
                        set_error_buf(error_buf, error_buf_size,
                                      "type mismatch");
                        goto fail;
                    }
                }
                else {
#if WASM_ENABLE_FAST_INTERP != 0
                    skip_label();
#endif
                }
                break;
            }

            case WASM_OP_SELECT:
            {
                uint8 ref_type;
                BranchBlock *cur_block = loader_ctx->frame_csp - 1;
                int32 available_stack_cell;

                POP_I32();

                available_stack_cell = (int32)(loader_ctx->stack_cell_num
                                               - cur_block->stack_cell_num);

                if (available_stack_cell <= 0
                    && !cur_block->is_stack_polymorphic) {
                    set_error_buf(error_buf, error_buf_size,
                                  "type mismatch or invalid result arity, "
                                  "opcode select was found "
                                  "but stack was empty");
                    goto fail;
                }

                if (available_stack_cell > 0) {
                    switch (*(loader_ctx->frame_ref - 1)) {
                        case REF_I32:
                        case REF_F32:
                            break;
                        case REF_I64_2:
                        case REF_F64_2:
#if WASM_ENABLE_FAST_INTERP == 0
                            *(p - 1) = WASM_OP_SELECT_64;
#endif
#if WASM_ENABLE_FAST_INTERP != 0
                            if (loader_ctx->p_code_compiled) {
                                uint8 opcode_tmp = WASM_OP_SELECT_64;
                                uint8 *p_code_compiled_tmp =
                                    loader_ctx->p_code_compiled - 2;
#if WASM_ENABLE_LABELS_AS_VALUES != 0
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS != 0
                                *(void **)(p_code_compiled_tmp
                                           - sizeof(void *)) =
                                    handle_table[opcode_tmp];
#else
                                int32 offset =
                                    (int32)((uint8 *)handle_table[opcode_tmp]
                                            - (uint8 *)handle_table[0]);
                                if (!(offset >= INT16_MIN
                                      && offset < INT16_MAX)) {
                                    set_error_buf(error_buf, error_buf_size,
                                                  "pre-compiled label offset "
                                                  "out of range");
                                    goto fail;
                                }
                                *(int16 *)(p_code_compiled_tmp
                                           - sizeof(int16)) = (int16)offset;
#endif /* end of WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS */
#else  /* else of WASM_ENABLE_LABELS_AS_VALUES */
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS != 0
                                *(p_code_compiled_tmp - 1) = opcode_tmp;
#else
                                *(p_code_compiled_tmp - 2) = opcode_tmp;
#endif /* end of WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS */
#endif /* end of WASM_ENABLE_LABELS_AS_VALUES */
                            }
#endif /* end of WASM_ENABLE_FAST_INTERP */
                            break;
#if WASM_ENABLE_SIMD != 0
#if (WASM_ENABLE_WAMR_COMPILER != 0) || (WASM_ENABLE_JIT != 0)
                        case REF_V128_4:
                            break;
#endif /* (WASM_ENABLE_WAMR_COMPILER != 0) || (WASM_ENABLE_JIT != 0) */
#endif /* WASM_ENABLE_SIMD != 0 */
                        default:
                        {
                            set_error_buf(error_buf, error_buf_size,
                                          "type mismatch");
                            goto fail;
                        }
                    }

                    ref_type = *(loader_ctx->frame_ref - 1);
#if WASM_ENABLE_FAST_INTERP != 0
                    POP_OFFSET_TYPE(ref_type);
                    POP_TYPE(ref_type);
                    POP_OFFSET_TYPE(ref_type);
                    POP_TYPE(ref_type);
                    PUSH_OFFSET_TYPE(ref_type);
                    PUSH_TYPE(ref_type);
#else
                    POP2_AND_PUSH(ref_type, ref_type);
#endif
                }
                else {
#if WASM_ENABLE_FAST_INTERP != 0
                    PUSH_OFFSET_TYPE(VALUE_TYPE_ANY);
#endif
                    PUSH_TYPE(VALUE_TYPE_ANY);
                }
                break;
            }

#if WASM_ENABLE_REF_TYPES != 0
            case WASM_OP_SELECT_T:
            {
                uint8 vec_len, ref_type;

                read_leb_uint32(p, p_end, vec_len);
                if (!vec_len) {
                    set_error_buf(error_buf, error_buf_size,
                                  "invalid result arity");
                    goto fail;
                }

                CHECK_BUF(p, p_end, 1);
                ref_type = read_uint8(p);
                if (!is_value_type(ref_type)) {
                    set_error_buf(error_buf, error_buf_size,
                                  "unknown value type");
                    goto fail;
                }

                POP_I32();

#if WASM_ENABLE_FAST_INTERP != 0
                if (loader_ctx->p_code_compiled) {
                    uint8 opcode_tmp = WASM_OP_SELECT;
                    uint8 *p_code_compiled_tmp =
                        loader_ctx->p_code_compiled - 2;

                    if (ref_type == VALUE_TYPE_V128) {
#if (WASM_ENABLE_SIMD == 0) \
    || ((WASM_ENABLE_WAMR_COMPILER == 0) && (WASM_ENABLE_JIT == 0))
                        set_error_buf(error_buf, error_buf_size,
                                      "SIMD v128 type isn't supported");
                        goto fail;
#endif
                    }
                    else {
                        if (ref_type == VALUE_TYPE_F64
                            || ref_type == VALUE_TYPE_I64)
                            opcode_tmp = WASM_OP_SELECT_64;
#if WASM_ENABLE_LABELS_AS_VALUES != 0
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS != 0
                        *(void **)(p_code_compiled_tmp - sizeof(void *)) =
                            handle_table[opcode_tmp];
#else
                        int32 offset = (int32)((uint8 *)handle_table[opcode_tmp]
                                               - (uint8 *)handle_table[0]);
                        if (!(offset >= INT16_MIN && offset < INT16_MAX)) {
                            set_error_buf(
                                error_buf, error_buf_size,
                                "pre-compiled label offset out of range");
                            goto fail;
                        }
                        *(int16 *)(p_code_compiled_tmp - sizeof(int16)) =
                            (int16)offset;
#endif /* end of WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS */
#else  /* else of WASM_ENABLE_LABELS_AS_VALUES */
#if WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS != 0
                        *(p_code_compiled_tmp - 1) = opcode_tmp;
#else
                        *(p_code_compiled_tmp - 2) = opcode_tmp;
#endif /* end of WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS */
#endif /* end of WASM_ENABLE_LABELS_AS_VALUES */
                    }
                }
#endif /* WASM_ENABLE_FAST_INTERP != 0 */

#if WASM_ENABLE_FAST_INTERP != 0
                POP_OFFSET_TYPE(ref_type);
                POP_TYPE(ref_type);
                POP_OFFSET_TYPE(ref_type);
                POP_TYPE(ref_type);
                PUSH_OFFSET_TYPE(ref_type);
                PUSH_TYPE(ref_type);
#else
                POP2_AND_PUSH(ref_type, ref_type);
#endif /* WASM_ENABLE_FAST_INTERP != 0 */

                (void)vec_len;
                break;
            }

            /* table.get x. tables[x]. [i32] -> [t] */
            /* table.set x. tables[x]. [i32 t] -> [] */
            case WASM_OP_TABLE_GET:
            case WASM_OP_TABLE_SET:
            {
                uint8 decl_ref_type;

                read_leb_uint32(p, p_end, table_idx);
                if (!get_table_elem_type(module, table_idx, &decl_ref_type,
                                         error_buf, error_buf_size))
                    goto fail;

#if WASM_ENABLE_FAST_INTERP != 0
                emit_uint32(loader_ctx, table_idx);
#endif

                if (opcode == WASM_OP_TABLE_GET) {
                    POP_I32();
#if WASM_ENABLE_FAST_INTERP != 0
                    PUSH_OFFSET_TYPE(decl_ref_type);
#endif
                    PUSH_TYPE(decl_ref_type);
                }
                else {
#if WASM_ENABLE_FAST_INTERP != 0
                    POP_OFFSET_TYPE(decl_ref_type);
#endif
                    POP_TYPE(decl_ref_type);
                    POP_I32();
                }
                break;
            }
            case WASM_OP_REF_NULL:
            {
                uint8 ref_type;

                CHECK_BUF(p, p_end, 1);
                ref_type = read_uint8(p);
                if (ref_type != VALUE_TYPE_FUNCREF
                    && ref_type != VALUE_TYPE_EXTERNREF) {
                    set_error_buf(error_buf, error_buf_size,
                                  "unknown value type");
                    goto fail;
                }
#if WASM_ENABLE_FAST_INTERP != 0
                PUSH_OFFSET_TYPE(ref_type);
#endif
                PUSH_TYPE(ref_type);
                break;
            }
            case WASM_OP_REF_IS_NULL:
            {
#if WASM_ENABLE_FAST_INTERP != 0
                if (!wasm_loader_pop_frame_ref_offset(loader_ctx,
                                                      VALUE_TYPE_FUNCREF,
                                                      error_buf, error_buf_size)
                    && !wasm_loader_pop_frame_ref_offset(
                        loader_ctx, VALUE_TYPE_EXTERNREF, error_buf,
                        error_buf_size)) {
                    goto fail;
                }
#else
                if (!wasm_loader_pop_frame_ref(loader_ctx, VALUE_TYPE_FUNCREF,
                                               error_buf, error_buf_size)
                    && !wasm_loader_pop_frame_ref(loader_ctx,
                                                  VALUE_TYPE_EXTERNREF,
                                                  error_buf, error_buf_size)) {
                    goto fail;
                }
#endif
                PUSH_I32();
                break;
            }
            case WASM_OP_REF_FUNC:
            {
                read_leb_uint32(p, p_end, func_idx);

                if (!check_function_index(module, func_idx, error_buf,
                                          error_buf_size)) {
                    goto fail;
                }

                if (func_idx == cur_func_idx + module->import_function_count) {
                    WASMTableSeg *table_seg = module->table_segments;
                    bool func_declared = false;
                    uint32 j;

                    /* Check whether current function is declared */
                    for (i = 0; i < module->table_seg_count; i++, table_seg++) {
                        if (table_seg->elem_type == VALUE_TYPE_FUNCREF
                            && wasm_elem_is_declarative(table_seg->mode)) {
                            for (j = 0; j < table_seg->function_count; j++) {
                                if (table_seg->func_indexes[j] == func_idx) {
                                    func_declared = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (!func_declared) {
                        set_error_buf(error_buf, error_buf_size,
                                      "undeclared function reference");
                        goto fail;
                    }
                }

#if WASM_ENABLE_FAST_INTERP != 0
                emit_uint32(loader_ctx, func_idx);
#endif
                PUSH_FUNCREF();
                break;
            }
#endif /* WASM_ENABLE_REF_TYPES */

            case WASM_OP_GET_LOCAL:
            {
                p_org = p - 1;
                GET_LOCAL_INDEX_TYPE_AND_OFFSET();
                PUSH_TYPE(local_type);

#if WASM_ENABLE_FAST_INTERP != 0
                /* Get Local is optimized out */
                skip_label();
                disable_emit = true;
                operand_offset = local_offset;
                PUSH_OFFSET_TYPE(local_type);
#else
#if (WASM_ENABLE_WAMR_COMPILER == 0) && (WASM_ENABLE_JIT == 0) \
    && (WASM_ENABLE_FAST_JIT == 0) && (WASM_ENABLE_DEBUG_INTERP == 0)
                if (local_offset < 0x80) {
                    *p_org++ = EXT_OP_GET_LOCAL_FAST;
                    if (is_32bit_type(local_type)) {
                        *p_org++ = (uint8)local_offset;
                    }
                    else {
                        *p_org++ = (uint8)(local_offset | 0x80);
                    }
                    while (p_org < p) {
                        *p_org++ = WASM_OP_NOP;
                    }
                }
#endif
#endif /* end of WASM_ENABLE_FAST_INTERP != 0 */
                break;
            }

            case WASM_OP_SET_LOCAL:
            {
                p_org = p - 1;
                GET_LOCAL_INDEX_TYPE_AND_OFFSET();
                POP_TYPE(local_type);

#if WASM_ENABLE_FAST_INTERP != 0
                if (!(preserve_referenced_local(
                        loader_ctx, opcode, local_offset, local_type,
                        &preserve_local, error_buf, error_buf_size)))
                    goto fail;

                if (local_offset < 256) {
                    skip_label();
                    if ((!preserve_local) && (LAST_OP_OUTPUT_I32())) {
                        if (loader_ctx->p_code_compiled)
                            STORE_U16(loader_ctx->p_code_compiled - 2,
                                      local_offset);
                        loader_ctx->frame_offset--;
                        loader_ctx->dynamic_offset--;
                    }
                    else if ((!preserve_local) && (LAST_OP_OUTPUT_I64())) {
                        if (loader_ctx->p_code_compiled)
                            STORE_U16(loader_ctx->p_code_compiled - 2,
                                      local_offset);
                        loader_ctx->frame_offset -= 2;
                        loader_ctx->dynamic_offset -= 2;
                    }
                    else {
                        if (is_32bit_type(local_type)) {
                            emit_label(EXT_OP_SET_LOCAL_FAST);
                            emit_byte(loader_ctx, (uint8)local_offset);
                        }
                        else {
                            emit_label(EXT_OP_SET_LOCAL_FAST_I64);
                            emit_byte(loader_ctx, (uint8)local_offset);
                        }
                        POP_OFFSET_TYPE(local_type);
                    }
                }
                else { /* local index larger than 255, reserve leb */
                    emit_uint32(loader_ctx, local_idx);
                    POP_OFFSET_TYPE(local_type);
                }
#else
#if (WASM_ENABLE_WAMR_COMPILER == 0) && (WASM_ENABLE_JIT == 0) \
    && (WASM_ENABLE_FAST_JIT == 0) && (WASM_ENABLE_DEBUG_INTERP == 0)
                if (local_offset < 0x80) {
                    *p_org++ = EXT_OP_SET_LOCAL_FAST;
                    if (is_32bit_type(local_type)) {
                        *p_org++ = (uint8)local_offset;
                    }
                    else {
                        *p_org++ = (uint8)(local_offset | 0x80);
                    }
                    while (p_org < p) {
                        *p_org++ = WASM_OP_NOP;
                    }
                }
#endif
#endif /* end of WASM_ENABLE_FAST_INTERP != 0 */
                break;
            }

            case WASM_OP_TEE_LOCAL:
            {
                p_org = p - 1;
                GET_LOCAL_INDEX_TYPE_AND_OFFSET();
#if WASM_ENABLE_FAST_INTERP != 0
                /* If the stack is in polymorphic state, do fake pop and push on
                    offset stack to keep the depth of offset stack to be the
                   same with ref stack */
                BranchBlock *cur_block = loader_ctx->frame_csp - 1;
                if (cur_block->is_stack_polymorphic) {
                    POP_OFFSET_TYPE(local_type);
                    PUSH_OFFSET_TYPE(local_type);
                }
#endif
                POP_TYPE(local_type);
                PUSH_TYPE(local_type);

#if WASM_ENABLE_FAST_INTERP != 0
                if (!(preserve_referenced_local(
                        loader_ctx, opcode, local_offset, local_type,
                        &preserve_local, error_buf, error_buf_size)))
                    goto fail;

                if (local_offset < 256) {
                    skip_label();
                    if (is_32bit_type(local_type)) {
                        emit_label(EXT_OP_TEE_LOCAL_FAST);
                        emit_byte(loader_ctx, (uint8)local_offset);
                    }
                    else {
                        emit_label(EXT_OP_TEE_LOCAL_FAST_I64);
                        emit_byte(loader_ctx, (uint8)local_offset);
                    }
                }
                else { /* local index larger than 255, reserve leb */
                    emit_uint32(loader_ctx, local_idx);
                }
                emit_operand(loader_ctx,
                             *(loader_ctx->frame_offset
                               - wasm_value_type_cell_num(local_type)));
#else
#if (WASM_ENABLE_WAMR_COMPILER == 0) && (WASM_ENABLE_JIT == 0) \
    && (WASM_ENABLE_FAST_JIT == 0) && (WASM_ENABLE_DEBUG_INTERP == 0)
                if (local_offset < 0x80) {
                    *p_org++ = EXT_OP_TEE_LOCAL_FAST;
                    if (is_32bit_type(local_type)) {
                        *p_org++ = (uint8)local_offset;
                    }
                    else {
                        *p_org++ = (uint8)(local_offset | 0x80);
                    }
                    while (p_org < p) {
                        *p_org++ = WASM_OP_NOP;
                    }
                }
#endif
#endif /* end of WASM_ENABLE_FAST_INTERP != 0 */
                break;
            }

            case WASM_OP_GET_GLOBAL:
            {
                p_org = p - 1;
                read_leb_uint32(p, p_end, global_idx);
                if (global_idx >= global_count) {
                    set_error_buf(error_buf, error_buf_size, "unknown global");
                    goto fail;
                }

                global_type = module->globals[global_idx].type;

                PUSH_TYPE(global_type);

#if WASM_ENABLE_FAST_INTERP == 0
                if (global_type == VALUE_TYPE_I64
                    || global_type == VALUE_TYPE_F64) {
#if WASM_ENABLE_DEBUG_INTERP != 0
                    if (!record_fast_op(module, p_org, *p_org, error_buf,
                                        error_buf_size)) {
                        goto fail;
                    }
#endif
                    *p_org = WASM_OP_GET_GLOBAL_64;
                }
#else  /* else of WASM_ENABLE_FAST_INTERP */
                if (global_type == VALUE_TYPE_I64
                    || global_type == VALUE_TYPE_F64) {
                    skip_label();
                    emit_label(WASM_OP_GET_GLOBAL_64);
                }
                emit_uint32(loader_ctx, global_idx);
                PUSH_OFFSET_TYPE(global_type);
#endif /* end of WASM_ENABLE_FAST_INTERP */
                break;
            }

            case WASM_OP_SET_GLOBAL:
            {
                bool is_mutable = false;

                p_org = p - 1;
                read_leb_uint32(p, p_end, global_idx);
                if (global_idx >= global_count) {
                    set_error_buf(error_buf, error_buf_size, "unknown global");
                    goto fail;
                }

                is_mutable = module->globals[global_idx].is_mutable;
                if (!is_mutable) {
                    set_error_buf(error_buf, error_buf_size,
                                  "global is immutable");
                    goto fail;
                }

                global_type = module->globals[global_idx].type;

                POP_TYPE(global_type);

#if WASM_ENABLE_FAST_INTERP == 0
                if (global_type == VALUE_TYPE_I64
                    || global_type == VALUE_TYPE_F64) {
#if WASM_ENABLE_DEBUG_INTERP != 0
                    if (!record_fast_op(module, p_org, *p_org, error_buf,
                                        error_buf_size)) {
                        goto fail;
                    }
#endif
                    *p_org = WASM_OP_SET_GLOBAL_64;
                }
#else  /* else of WASM_ENABLE_FAST_INTERP */
                if (global_type == VALUE_TYPE_I64
                    || global_type == VALUE_TYPE_F64) {
                    skip_label();
                    emit_label(WASM_OP_SET_GLOBAL_64);
                }
                else if (module->aux_stack_size > 0
                         && global_idx == module->aux_stack_top_global_index) {
                    skip_label();
                    emit_label(WASM_OP_SET_GLOBAL_AUX_STACK);
                }
                emit_uint32(loader_ctx, global_idx);
                POP_OFFSET_TYPE(global_type);
#endif /* end of WASM_ENABLE_FAST_INTERP */
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
#if WASM_ENABLE_FAST_INTERP != 0
                /* change F32/F64 into I32/I64 */
                if (opcode == WASM_OP_F32_LOAD) {
                    skip_label();
                    emit_label(WASM_OP_I32_LOAD);
                }
                else if (opcode == WASM_OP_F64_LOAD) {
                    skip_label();
                    emit_label(WASM_OP_I64_LOAD);
                }
                else if (opcode == WASM_OP_F32_STORE) {
                    skip_label();
                    emit_label(WASM_OP_I32_STORE);
                }
                else if (opcode == WASM_OP_F64_STORE) {
                    skip_label();
                    emit_label(WASM_OP_I64_STORE);
                }
#endif
                CHECK_MEMORY();
                read_leb_uint32(p, p_end, align);      /* align */
                read_leb_uint32(p, p_end, mem_offset); /* offset */
                if (!check_memory_access_align(opcode, align, error_buf,
                                               error_buf_size)) {
                    goto fail;
                }
#if WASM_ENABLE_FAST_INTERP != 0
                emit_uint32(loader_ctx, mem_offset);
#endif
#if WASM_ENABLE_JIT != 0 || WASM_ENABLE_WAMR_COMPILER != 0
                func->has_memory_operations = true;
#endif
                switch (opcode) {
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
                if (*p++ != 0x00) {
                    set_error_buf(error_buf, error_buf_size,
                                  "zero byte expected");
                    goto fail;
                }
                PUSH_I32();

                module->possible_memory_grow = true;
#if WASM_ENABLE_JIT != 0 || WASM_ENABLE_WAMR_COMPILER != 0
                func->has_memory_operations = true;
#endif
                break;

            case WASM_OP_MEMORY_GROW:
                CHECK_MEMORY();
                /* reserved byte 0x00 */
                if (*p++ != 0x00) {
                    set_error_buf(error_buf, error_buf_size,
                                  "zero byte expected");
                    goto fail;
                }
                POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_I32);

                module->possible_memory_grow = true;
#if WASM_ENABLE_FAST_JIT != 0 || WASM_ENABLE_JIT != 0 \
    || WASM_ENABLE_WAMR_COMPILER != 0
                func->has_op_memory_grow = true;
#endif
#if WASM_ENABLE_JIT != 0 || WASM_ENABLE_WAMR_COMPILER != 0
                func->has_memory_operations = true;
#endif
                break;

            case WASM_OP_I32_CONST:
                read_leb_int32(p, p_end, i32_const);
#if WASM_ENABLE_FAST_INTERP != 0
                skip_label();
                disable_emit = true;
                GET_CONST_OFFSET(VALUE_TYPE_I32, i32_const);

                if (operand_offset == 0) {
                    disable_emit = false;
                    emit_label(WASM_OP_I32_CONST);
                    emit_uint32(loader_ctx, i32_const);
                }
#else
                (void)i32_const;
#endif
                PUSH_I32();
                break;

            case WASM_OP_I64_CONST:
                read_leb_int64(p, p_end, i64_const);
#if WASM_ENABLE_FAST_INTERP != 0
                skip_label();
                disable_emit = true;
                GET_CONST_OFFSET(VALUE_TYPE_I64, i64_const);

                if (operand_offset == 0) {
                    disable_emit = false;
                    emit_label(WASM_OP_I64_CONST);
                    emit_uint64(loader_ctx, i64_const);
                }
#endif
                PUSH_I64();
                break;

            case WASM_OP_F32_CONST:
                p += sizeof(float32);
#if WASM_ENABLE_FAST_INTERP != 0
                skip_label();
                disable_emit = true;
                bh_memcpy_s((uint8 *)&f32_const, sizeof(float32), p_org,
                            sizeof(float32));
                GET_CONST_F32_OFFSET(VALUE_TYPE_F32, f32_const);

                if (operand_offset == 0) {
                    disable_emit = false;
                    emit_label(WASM_OP_F32_CONST);
                    emit_float32(loader_ctx, f32_const);
                }
#endif
                PUSH_F32();
                break;

            case WASM_OP_F64_CONST:
                p += sizeof(float64);
#if WASM_ENABLE_FAST_INTERP != 0
                skip_label();
                disable_emit = true;
                /* Some MCU may require 8-byte align */
                bh_memcpy_s((uint8 *)&f64_const, sizeof(float64), p_org,
                            sizeof(float64));
                GET_CONST_F64_OFFSET(VALUE_TYPE_F64, f64_const);

                if (operand_offset == 0) {
                    disable_emit = false;
                    emit_label(WASM_OP_F64_CONST);
                    emit_float64(loader_ctx, f64_const);
                }
#endif
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

                read_leb_uint32(p, p_end, opcode1);
#if WASM_ENABLE_FAST_INTERP != 0
                emit_byte(loader_ctx, ((uint8)opcode1));
#endif
                switch (opcode1) {
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
#if WASM_ENABLE_BULK_MEMORY != 0
                    case WASM_OP_MEMORY_INIT:
                    {
                        read_leb_uint32(p, p_end, data_seg_idx);
#if WASM_ENABLE_FAST_INTERP != 0
                        emit_uint32(loader_ctx, data_seg_idx);
#endif
                        if (module->import_memory_count == 0
                            && module->memory_count == 0)
                            goto fail_unknown_memory;

                        if (*p++ != 0x00)
                            goto fail_zero_byte_expected;

                        if (data_seg_idx >= module->data_seg_count) {
                            set_error_buf_v(error_buf, error_buf_size,
                                            "unknown data segment %d",
                                            data_seg_idx);
                            goto fail;
                        }

                        if (module->data_seg_count1 == 0)
                            goto fail_data_cnt_sec_require;

                        POP_I32();
                        POP_I32();
                        POP_I32();
#if WASM_ENABLE_JIT != 0 || WASM_ENABLE_WAMR_COMPILER != 0
                        func->has_memory_operations = true;
#endif
                        break;
                    }
                    case WASM_OP_DATA_DROP:
                    {
                        read_leb_uint32(p, p_end, data_seg_idx);
#if WASM_ENABLE_FAST_INTERP != 0
                        emit_uint32(loader_ctx, data_seg_idx);
#endif
                        if (data_seg_idx >= module->data_seg_count) {
                            set_error_buf(error_buf, error_buf_size,
                                          "unknown data segment");
                            goto fail;
                        }

                        if (module->data_seg_count1 == 0)
                            goto fail_data_cnt_sec_require;

#if WASM_ENABLE_JIT != 0 || WASM_ENABLE_WAMR_COMPILER != 0
                        func->has_memory_operations = true;
#endif
                        break;
                    }
                    case WASM_OP_MEMORY_COPY:
                    {
                        /* both src and dst memory index should be 0 */
                        if (*(int16 *)p != 0x0000)
                            goto fail_zero_byte_expected;
                        p += 2;

                        if (module->import_memory_count == 0
                            && module->memory_count == 0)
                            goto fail_unknown_memory;

                        POP_I32();
                        POP_I32();
                        POP_I32();
#if WASM_ENABLE_JIT != 0 || WASM_ENABLE_WAMR_COMPILER != 0
                        func->has_memory_operations = true;
#endif
                        break;
                    }
                    case WASM_OP_MEMORY_FILL:
                    {
                        if (*p++ != 0x00) {
                            goto fail_zero_byte_expected;
                        }
                        if (module->import_memory_count == 0
                            && module->memory_count == 0) {
                            goto fail_unknown_memory;
                        }

                        POP_I32();
                        POP_I32();
                        POP_I32();
#if WASM_ENABLE_JIT != 0 || WASM_ENABLE_WAMR_COMPILER != 0
                        func->has_memory_operations = true;
#endif
                        break;
                    }
                    fail_zero_byte_expected:
                        set_error_buf(error_buf, error_buf_size,
                                      "zero byte expected");
                        goto fail;

                    fail_unknown_memory:
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown memory 0");
                        goto fail;
                    fail_data_cnt_sec_require:
                        set_error_buf(error_buf, error_buf_size,
                                      "data count section required");
                        goto fail;
#endif /* WASM_ENABLE_BULK_MEMORY */

                    default:
                        set_error_buf_v(error_buf, error_buf_size,
                                        "%s %02x %02x", "unsupported opcode",
                                        0xfc, opcode1);
                        goto fail;
                }
                break;
            }

            default:
                set_error_buf_v(error_buf, error_buf_size, "%s %02x",
                                "unsupported opcode", opcode);
                goto fail;
        }

#if WASM_ENABLE_FAST_INTERP != 0
        last_op = opcode;
#endif
    }

    if (loader_ctx->csp_num > 0) {
        if (cur_func_idx < module->function_count - 1)
            /* Function with missing end marker (between two functions) */
            set_error_buf(error_buf, error_buf_size, "END opcode expected");
        else
            /* Function with missing end marker
               (at EOF or end of code sections) */
            set_error_buf(error_buf, error_buf_size,
                          "unexpected end of section or function, "
                          "or section size mismatch");
        goto fail;
    }

#if WASM_ENABLE_FAST_INTERP != 0
    if (loader_ctx->p_code_compiled == NULL)
        goto re_scan;

    func->const_cell_num = loader_ctx->const_cell_num;
    if (func->const_cell_num > 0) {
        int32 j;

        if (!(func->consts = func_const = loader_malloc(
                  func->const_cell_num * 4, error_buf, error_buf_size)))
            goto fail;

        func_const_end = func->consts + func->const_cell_num * 4;
        /* reverse the const buf */
        for (j = loader_ctx->num_const - 1; j >= 0; j--) {
            Const *c = (Const *)(loader_ctx->const_buf + j * sizeof(Const));
            if (c->value_type == VALUE_TYPE_F64
                || c->value_type == VALUE_TYPE_I64) {
                bh_memcpy_s(func_const, (uint32)(func_const_end - func_const),
                            &(c->value.f64), (uint32)sizeof(int64));
                func_const += sizeof(int64);
            }
            else {
                bh_memcpy_s(func_const, (uint32)(func_const_end - func_const),
                            &(c->value.f32), (uint32)sizeof(int32));
                func_const += sizeof(int32);
            }
        }
    }

    func->max_stack_cell_num = loader_ctx->preserved_local_offset
                               - loader_ctx->start_dynamic_offset + 1;
#else
    func->max_stack_cell_num = loader_ctx->max_stack_cell_num;
#endif
    func->max_block_num = loader_ctx->max_csp_num;
    return_value = true;

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
    return return_value;
}

bool wasm_validator(WASMModule *module, char *error_buf,
                             uint32 error_buf_size)
{
    uint32 i;
    WASMTable *table;
    WASMFunction *func;
    WASMGlobal *global, *globals;

        //     if (!check_memory_max_size(init_page_count,
        //                            max_page_count, error_buf,
        //                            error_buf_size)) {
        //     return false;
        // }

    //检查table的正确性
    // if (!check_table_max_size(table->cur_size, table->max_size, error_buf,
    //                               error_buf_size))
    // return false;

            //     for (j = 0; j < i; j++) {
            //     name = module->exports[j].name;
            //     if (strlen(name) == str_len && memcmp(name, p, str_len) == 0) {
            //         set_error_buf(error_buf, error_buf_size,
            //                       "duplicate export name");
            //         return false;
            //     }
            // }

// switch (export->kind) {
//                 /* function index */
//                 case EXPORT_KIND_FUNC:
//                     if (index >= module->function_count
//                                      + module->import_function_count) {
//                         set_error_buf(error_buf, error_buf_size,
//                                       "unknown function");
//                         return false;
//                     }
//                     break;
//                 case EXPORT_KIND_TABLE:
//                     if (index
//                         >= module->table_count + module->import_table_count) {
//                         set_error_buf(error_buf, error_buf_size,
//                                       "unknown table");
//                         return false;
//                     }
//                     break;
//                 /* memory index */
//                 case EXPORT_KIND_MEMORY:
//                     if (index
//                         >= module->memory_count + module->import_memory_count) {
//                         set_error_buf(error_buf, error_buf_size,
//                                       "unknown memory");
//                         return false;
//                     }
//                     break;
//                 /* global index */
//                 case EXPORT_KIND_GLOBAL:
//                     if (index
//                         >= module->global_count + module->import_global_count) {
//                         set_error_buf(error_buf, error_buf_size,
//                                       "unknown global");
//                         return false;
//                     }
//                     break;
//                 default:
//                     set_error_buf(error_buf, error_buf_size,
//                                   "invalid export kind");
//                     return false;
//             }

//函数type验证
            // if (type_index >= module->type_count) {
            //     set_error_buf(error_buf, error_buf_size, "unknown type");
            //     return false;
            // }

    globals = module->globals;
    global = module->globals + module->import_global_count;
    for( i = 0; i < module->global_count; i++, global++) {
        if (global->type == VALUE_TYPE_GLOBAL) {
            global->type = globals[global->initial_value.global_index].type;
            memcpy(
                &(global->initial_value),
                &(globals[global->initial_value.global_index].initial_value),
                sizeof(WASMValue));
        }
    }

    func = module->functions + module->import_function_count;
    for ( i = 0; i < module->function_count; i++, func++) {
        if (!wasm_validator_code(module, func, i, error_buf,
                                          error_buf_size)) {
            goto fail;
        }
    }

    if (!module->possible_memory_grow) {
        WASMMemory *memory = module->memories;

        for(i = 0; i < module->memory_count; i++, memory++)
            if (memory->cur_page_count < DEFAULT_MAX_PAGES)
                memory->num_bytes_per_page *= memory->cur_page_count;
            else
                memory->num_bytes_per_page = UINT32_MAX;

            if (memory->cur_page_count > 0)
                memory->cur_page_count = memory->max_page_count = 1;
            else
                memory->cur_page_count = memory->max_page_count = 0;
    }

    LOG_VERBOSE("Validate success.\n");
    return true;
fail:
    LOG_VERBOSE("Validate fail.\n");
    wasm_module_destory(module,Validate);
    return false;
}