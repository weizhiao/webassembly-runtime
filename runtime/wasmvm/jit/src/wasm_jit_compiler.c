#include "wasm_jit_compiler.h"
#include "wasm_jit_emit_compare.h"
#include "wasm_jit_emit_conversion.h"
#include "wasm_jit_emit_memory.h"
#include "wasm_jit_emit_exception.h"
#include "wasm_jit_emit_numberic.h"
#include "wasm_jit_emit_control.h"
#include "wasm_jit_emit_function.h"
#include "wasm_fast_readleb.h"
#include "wasm_opcode.h"
#include <errno.h>

static inline LLVMTypeRef GET_LLVM_PTRTYPE(JITCompContext *comp_ctx, uint8 valuetype)
{
    LLVMTypeRef ptr_type;
    switch (valuetype)
    {
    case VALUE_TYPE_I32:
    case VALUE_TYPE_EXTERNREF:
    case VALUE_TYPE_FUNCREF:
        ptr_type = comp_ctx->basic_types.int32_ptr_type;
        break;
    case VALUE_TYPE_I64:
        ptr_type = comp_ctx->basic_types.int64_ptr_type;
        break;
    case VALUE_TYPE_F32:
        ptr_type = comp_ctx->basic_types.float32_ptr_type;
        break;
    case VALUE_TYPE_F64:
        ptr_type = comp_ctx->basic_types.float64_ptr_type;
        break;
    }
    return ptr_type;
}

#define GET_GLOBAL_LLVMPTR(global_idx)                                                                                                        \
    do                                                                                                                                        \
    {                                                                                                                                         \
        global = globals + global_idx;                                                                                                        \
        value_type = global->type;                                                                                                            \
        offset = global->data_offset;                                                                                                         \
        llvm_offset = I32_CONST(offset);                                                                                                      \
        if (!(llvm_ptr = LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE, func_ctx->global_base_addr, &llvm_offset, 1, "global_ptr_tmp"))) \
        {                                                                                                                                     \
            return false;                                                                                                                     \
        }                                                                                                                                     \
        llvm_ptr_type = GET_LLVM_PTRTYPE(comp_ctx, value_type);                                                                               \
        llvm_ptr = LLVMBuildBitCast(comp_ctx->builder, llvm_ptr, llvm_ptr_type, "global_ptr");                                                \
    } while (0)

#define GET_LOCAL_TYPE(local_idx)                                        \
    do                                                                   \
    {                                                                    \
        if (local_idx < func_param_count)                                \
        {                                                                \
            value_type = func_param_types[local_idx];                    \
        }                                                                \
        else                                                             \
        {                                                                \
            value_type = func_local_types[local_idx - func_param_count]; \
        }                                                                \
    } while (0)

#define CHECK_BUF(buf, buf_end, length)                                  \
    do                                                                   \
    {                                                                    \
        if (buf + length > buf_end)                                      \
        {                                                                \
            wasm_jit_set_last_error("read leb failed: unexpected end."); \
            return false;                                                \
        }                                                                \
    } while (0)

#define GET_OP_INFO(value)          \
    do                              \
    {                               \
        value = op_info->idx;       \
        op_info = op_info->next_op; \
    } while (0)

static bool
wasm_jit_compile_func(WASMModule *wasm_module, JITCompContext *comp_ctx, uint32 func_index)
{
    JITFuncContext *func_ctx = comp_ctx->jit_func_ctxes[func_index];
    WASMFunction *wasm_func = func_ctx->wasm_func;
    WASMGlobal *global;
    WASMGlobal *globals = wasm_module->globals;
    WASMBlock *wasm_block = wasm_func->blocks->next_block;
    ExtInfo *op_info = wasm_func->op_info->next_op;
    uint8 *frame_ip = (uint8 *)wasm_func->func_ptr, opcode, *p_f32, *p_f64;
    uint8 *frame_ip_end = wasm_func->code_end;
    uint8 *func_param_types = wasm_func->param_types;
    uint8 *func_result_types = wasm_func->result_types;
    uint8 *param_types;
    uint8 *result_types;
    uint8 *func_local_types = wasm_func->local_types;
    uint8 value_type;
    uint16 func_param_count = wasm_func->param_count;
    uint16 func_result_count = wasm_func->result_count;
    uint16 param_count, result_count;
    uint32 br_depth, *br_depths, br_count;
    uint32 func_idx, type_idx, mem_idx, local_idx, global_idx, i;
    uint32 bytes = 4, align;
    uint32 type_index;
    uint32 offset;
    LLVMValueRef llvm_offset, llvm_cond;
    LLVMValueRef llvm_ptr;
    LLVMTypeRef llvm_ptr_type;
    bool sign = true;
    int32 i32_const;
    int64 i64_const;
    float32 f32_const;
    float64 f64_const;
    char name[32];
    LLVMValueRef llvm_value;
    LLVMValueRef res;
    WASMType *wasm_type = NULL;
    JITBlock *start_block = func_ctx->block_stack - 1;
    start_block->end_addr = wasm_block->end_addr;
    wasm_block = wasm_block->next_block;

    /* Start to translate the opcodes */
    LLVMPositionBuilderAtEnd(
        comp_ctx->builder,
        start_block->llvm_entry_block);
    while (frame_ip < frame_ip_end)
    {
        opcode = *frame_ip++;

        switch (opcode)
        {
        case WASM_OP_UNREACHABLE:
            if (!wasm_jit_compile_op_unreachable(comp_ctx, func_ctx, &frame_ip))
                return false;
            break;

        case WASM_OP_NOP:
            break;

        case WASM_OP_BLOCK:
        case WASM_OP_LOOP:
        case WASM_OP_IF:
        {
            value_type = read_uint8(frame_ip);
            if (value_type == VALUE_TYPE_I32 || value_type == VALUE_TYPE_I64 || value_type == VALUE_TYPE_F32 || value_type == VALUE_TYPE_F64 || value_type == VALUE_TYPE_V128 || value_type == VALUE_TYPE_VOID || value_type == VALUE_TYPE_FUNCREF || value_type == VALUE_TYPE_EXTERNREF)
            {
                param_count = 0;
                param_types = NULL;
                if (value_type == VALUE_TYPE_VOID)
                {
                    result_count = 0;
                    result_types = NULL;
                }
                else
                {
                    result_count = 1;
                    result_types = &value_type;
                }
            }
            else
            {
                frame_ip--;
                read_leb_uint32(frame_ip, frame_ip_end, type_index);
                wasm_type = wasm_module->types[type_index];
                param_count = wasm_type->param_count;
                param_types = wasm_type->param;
                result_count = wasm_type->result_count;
                result_types = wasm_type->result;
            }
            if (!wasm_compile_op_block(
                    comp_ctx, func_ctx, wasm_block,
                    (uint32)(LABEL_TYPE_BLOCK + opcode - WASM_OP_BLOCK),
                    param_count, param_types, result_count, result_types))
                return false;
            // 使用下一个block
            wasm_block = wasm_block->next_block;
            break;
        }
        case WASM_OP_ELSE:
            if (!wasm_jit_compile_op_else(comp_ctx, func_ctx))
                return false;
            break;

        case WASM_OP_END:
            if (!wasm_jit_compile_op_end(comp_ctx, func_ctx))
                return false;
            break;

        case WASM_OP_BR:
            read_leb_uint32(frame_ip, frame_ip_end, br_depth);
            if (!wasm_jit_compile_op_br(comp_ctx, func_ctx, br_depth, &frame_ip))
                return false;
            break;

        case WASM_OP_BR_IF:
            read_leb_uint32(frame_ip, frame_ip_end, br_depth);
            if (!wasm_jit_compile_op_br_if(comp_ctx, func_ctx, br_depth, &frame_ip))
                return false;
            break;

        case WASM_OP_BR_TABLE:
            read_leb_uint32(frame_ip, frame_ip_end, br_count);
            if (!(br_depths = wasm_runtime_malloc((uint32)sizeof(uint32) * (br_count + 1))))
            {
                wasm_jit_set_last_error("allocate memory failed.");
                goto fail;
            }
            for (i = 0; i <= br_count; i++)
                br_depths[i] = *frame_ip++;

            if (!wasm_jit_compile_op_br_table(comp_ctx, func_ctx, br_depths,
                                              br_count, &frame_ip))
            {
                wasm_runtime_free(br_depths);
                return false;
            }

            wasm_runtime_free(br_depths);
            break;

        case WASM_OP_RETURN:
            if (!wasm_jit_compile_op_return(comp_ctx, func_ctx, &frame_ip))
                return false;
            break;

        case WASM_OP_CALL:
            read_leb_uint32(frame_ip, frame_ip_end, func_idx);
            if (!wasm_jit_compile_op_call(wasm_module, comp_ctx, func_ctx, func_idx))
                return false;
            break;

        case WASM_OP_CALL_INDIRECT:
        {
            uint32 tbl_idx;

            read_leb_uint32(frame_ip, frame_ip_end, type_idx);
            read_leb_uint32(frame_ip, frame_ip_end, tbl_idx);

            if (!wasm_jit_compile_op_call_indirect(wasm_module, comp_ctx, func_ctx, type_idx,
                                                   tbl_idx))
                return false;
            break;
        }
        case WASM_OP_DROP:
        case WASM_OP_DROP_64:
            DROP();
            break;

        case WASM_OP_SELECT:
        case WASM_OP_SELECT_64:
            LLVMValueRef llvm_val1, llvm_val2;
            POP_COND(llvm_cond);
            POP(llvm_val2);
            POP(llvm_val1);
            if (!(llvm_value =
                      LLVMBuildSelect(comp_ctx->builder, llvm_cond, llvm_val1, llvm_val2, "select")))
            {
                wasm_jit_set_last_error("llvm build select failed.");
                return false;
            }

            PUSH(llvm_value);
            break;

        case WASM_OP_GET_LOCAL:
            skip_leb_uint32(frame_ip, frame_ip_end);
            goto get_local;
        case EXT_OP_GET_LOCAL_FAST:
            frame_ip++;
        get_local:
            GET_OP_INFO(local_idx);
            GET_LOCAL_TYPE(local_idx);
            snprintf(name, sizeof(name), "%s%d%s", "local", local_idx, "#");
            if (!(llvm_value = LLVMBuildLoad2(comp_ctx->builder, TO_LLVM_TYPE(value_type),
                                              func_ctx->locals[local_idx], name)))
            {
                wasm_jit_set_last_error("llvm build load fail");
                return false;
            }

            PUSH(llvm_value);
            break;

        case WASM_OP_SET_LOCAL:
            skip_leb_uint32(frame_ip, frame_ip_end);
            goto set_local;
        case EXT_OP_SET_LOCAL_FAST:
            frame_ip++;
        set_local:
            GET_OP_INFO(local_idx);
            GET_LOCAL_TYPE(local_idx);

            POP(llvm_value);

            if (!LLVMBuildStore(comp_ctx->builder, llvm_value,
                                func_ctx->locals[local_idx]))
            {
                wasm_jit_set_last_error("llvm build store fail");
                return false;
            }

            break;

        case WASM_OP_TEE_LOCAL:
            skip_leb_uint32(frame_ip, frame_ip_end);
            goto tee_local;
        case EXT_OP_TEE_LOCAL_FAST:
            frame_ip++;
        tee_local:
            GET_OP_INFO(local_idx);
            GET_LOCAL_TYPE(local_idx);

            POP(llvm_value);

            if (!LLVMBuildStore(comp_ctx->builder, llvm_value,
                                func_ctx->locals[local_idx]))
            {
                wasm_jit_set_last_error("llvm build store fail");
                return false;
            }

            PUSH(llvm_value);
            break;

        case WASM_OP_GET_GLOBAL:
        case WASM_OP_GET_GLOBAL_64:
            read_leb_uint32(frame_ip, frame_ip_end, global_idx);
            GET_GLOBAL_LLVMPTR(global_idx);
            llvm_value = LLVMBuildLoad2(comp_ctx->builder, TO_LLVM_TYPE(value_type), llvm_ptr, "global");
            LLVMSetAlignment(llvm_value, 4);
            PUSH(llvm_value);
            break;

        case WASM_OP_SET_GLOBAL:
        case WASM_OP_SET_GLOBAL_64:
            read_leb_uint32(frame_ip, frame_ip_end, global_idx);
            GET_GLOBAL_LLVMPTR(global_idx);
            POP(llvm_value);
            if (!(res = LLVMBuildStore(comp_ctx->builder, llvm_value, llvm_ptr)))
            {
                wasm_jit_set_last_error("llvm build store failed.");
                return false;
            }
            LLVMSetAlignment(res, 4);
            break;

        case WASM_OP_I32_LOAD:
            bytes = 4;
            sign = true;
            goto op_i32_load;
        case WASM_OP_I32_LOAD8_S:
        case WASM_OP_I32_LOAD8_U:
            bytes = 1;
            sign = (opcode == WASM_OP_I32_LOAD8_S) ? true : false;
            goto op_i32_load;
        case WASM_OP_I32_LOAD16_S:
        case WASM_OP_I32_LOAD16_U:
            bytes = 2;
            sign = (opcode == WASM_OP_I32_LOAD16_S) ? true : false;
        op_i32_load:
            read_leb_uint32(frame_ip, frame_ip_end, align);
            read_leb_uint32(frame_ip, frame_ip_end, offset);
            if (!wasm_jit_compile_op_i32_load(comp_ctx, func_ctx, align, offset,
                                              bytes, sign, false))
                return false;
            break;

        case WASM_OP_I64_LOAD:
            bytes = 8;
            sign = true;
            goto op_i64_load;
        case WASM_OP_I64_LOAD8_S:
        case WASM_OP_I64_LOAD8_U:
            bytes = 1;
            sign = (opcode == WASM_OP_I64_LOAD8_S) ? true : false;
            goto op_i64_load;
        case WASM_OP_I64_LOAD16_S:
        case WASM_OP_I64_LOAD16_U:
            bytes = 2;
            sign = (opcode == WASM_OP_I64_LOAD16_S) ? true : false;
            goto op_i64_load;
        case WASM_OP_I64_LOAD32_S:
        case WASM_OP_I64_LOAD32_U:
            bytes = 4;
            sign = (opcode == WASM_OP_I64_LOAD32_S) ? true : false;
        op_i64_load:
            read_leb_uint32(frame_ip, frame_ip_end, align);
            read_leb_uint32(frame_ip, frame_ip_end, offset);
            if (!wasm_jit_compile_op_i64_load(comp_ctx, func_ctx, align, offset,
                                              bytes, sign, false))
                return false;
            break;

        case WASM_OP_F32_LOAD:
            read_leb_uint32(frame_ip, frame_ip_end, align);
            read_leb_uint32(frame_ip, frame_ip_end, offset);
            if (!wasm_jit_compile_op_f32_load(comp_ctx, func_ctx, align, offset))
                return false;
            break;

        case WASM_OP_F64_LOAD:
            read_leb_uint32(frame_ip, frame_ip_end, align);
            read_leb_uint32(frame_ip, frame_ip_end, offset);
            if (!wasm_jit_compile_op_f64_load(comp_ctx, func_ctx, align, offset))
                return false;
            break;

        case WASM_OP_I32_STORE:
            bytes = 4;
            goto op_i32_store;
        case WASM_OP_I32_STORE8:
            bytes = 1;
            goto op_i32_store;
        case WASM_OP_I32_STORE16:
            bytes = 2;
        op_i32_store:
            read_leb_uint32(frame_ip, frame_ip_end, align);
            read_leb_uint32(frame_ip, frame_ip_end, offset);
            if (!wasm_jit_compile_op_i32_store(comp_ctx, func_ctx, align, offset,
                                               bytes, false))
                return false;
            break;

        case WASM_OP_I64_STORE:
            bytes = 8;
            goto op_i64_store;
        case WASM_OP_I64_STORE8:
            bytes = 1;
            goto op_i64_store;
        case WASM_OP_I64_STORE16:
            bytes = 2;
            goto op_i64_store;
        case WASM_OP_I64_STORE32:
            bytes = 4;
        op_i64_store:
            read_leb_uint32(frame_ip, frame_ip_end, align);
            read_leb_uint32(frame_ip, frame_ip_end, offset);
            if (!wasm_jit_compile_op_i64_store(comp_ctx, func_ctx, align, offset,
                                               bytes, false))
                return false;
            break;

        case WASM_OP_F32_STORE:
            read_leb_uint32(frame_ip, frame_ip_end, align);
            read_leb_uint32(frame_ip, frame_ip_end, offset);
            if (!wasm_jit_compile_op_f32_store(comp_ctx, func_ctx, align,
                                               offset))
                return false;
            break;

        case WASM_OP_F64_STORE:
            read_leb_uint32(frame_ip, frame_ip_end, align);
            read_leb_uint32(frame_ip, frame_ip_end, offset);
            if (!wasm_jit_compile_op_f64_store(comp_ctx, func_ctx, align,
                                               offset))
                return false;
            break;

        case WASM_OP_MEMORY_SIZE:
            read_leb_uint32(frame_ip, frame_ip_end, mem_idx);
            if (!wasm_jit_compile_op_memory_size(comp_ctx, func_ctx))
                return false;
            (void)mem_idx;
            break;

        case WASM_OP_MEMORY_GROW:
            read_leb_uint32(frame_ip, frame_ip_end, mem_idx);
            if (!wasm_jit_compile_op_memory_grow(comp_ctx, func_ctx))
                return false;
            break;

        case WASM_OP_I32_CONST:
            read_leb_int32(frame_ip, frame_ip_end, i32_const);
            llvm_value = I32_CONST((uint32)i32_const);
            PUSH_I32(llvm_value);
            break;

        case WASM_OP_I64_CONST:
            read_leb_int64(frame_ip, frame_ip_end, i64_const);
            llvm_value = I64_CONST((uint64)i64_const);
            PUSH_I64(llvm_value);
            break;

        case WASM_OP_F32_CONST:
            p_f32 = (uint8 *)&f32_const;
            for (i = 0; i < sizeof(float32); i++)
                *p_f32++ = *frame_ip++;
            llvm_value = F32_CONST(f32_const);
            PUSH_F32(llvm_value);
            break;

        case WASM_OP_F64_CONST:
            p_f64 = (uint8 *)&f64_const;
            for (i = 0; i < sizeof(float64); i++)
                *p_f64++ = *frame_ip++;
            llvm_value = F64_CONST(f64_const);
            PUSH_F64(llvm_value);
            break;

        case WASM_OP_I32_EQZ:
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
            if (!wasm_jit_compile_op_i32_compare(
                    comp_ctx, func_ctx, INT_EQZ + opcode - WASM_OP_I32_EQZ))
                return false;
            break;

        case WASM_OP_I64_EQZ:
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
            if (!wasm_jit_compile_op_i64_compare(
                    comp_ctx, func_ctx, INT_EQZ + opcode - WASM_OP_I64_EQZ))
                return false;
            break;

        case WASM_OP_F32_EQ:
        case WASM_OP_F32_NE:
        case WASM_OP_F32_LT:
        case WASM_OP_F32_GT:
        case WASM_OP_F32_LE:
        case WASM_OP_F32_GE:
            if (!wasm_jit_compile_op_f32_compare(
                    comp_ctx, func_ctx, FLOAT_EQ + opcode - WASM_OP_F32_EQ))
                return false;
            break;

        case WASM_OP_F64_EQ:
        case WASM_OP_F64_NE:
        case WASM_OP_F64_LT:
        case WASM_OP_F64_GT:
        case WASM_OP_F64_LE:
        case WASM_OP_F64_GE:
            if (!wasm_jit_compile_op_f64_compare(
                    comp_ctx, func_ctx, FLOAT_EQ + opcode - WASM_OP_F64_EQ))
                return false;
            break;

        case WASM_OP_I32_CLZ:
            if (!wasm_jit_compile_op_i32_clz(comp_ctx, func_ctx))
                return false;
            break;

        case WASM_OP_I32_CTZ:
            if (!wasm_jit_compile_op_i32_ctz(comp_ctx, func_ctx))
                return false;
            break;

        case WASM_OP_I32_POPCNT:
            if (!wasm_jit_compile_op_i32_popcnt(comp_ctx, func_ctx))
                return false;
            break;

        case WASM_OP_I32_ADD:
        case WASM_OP_I32_SUB:
        case WASM_OP_I32_MUL:
        case WASM_OP_I32_DIV_S:
        case WASM_OP_I32_DIV_U:
        case WASM_OP_I32_REM_S:
        case WASM_OP_I32_REM_U:
            if (!wasm_jit_compile_op_i32_arithmetic(
                    comp_ctx, func_ctx, INT_ADD + opcode - WASM_OP_I32_ADD,
                    &frame_ip))
                return false;
            break;

        case WASM_OP_I32_AND:
        case WASM_OP_I32_OR:
        case WASM_OP_I32_XOR:
            if (!wasm_jit_compile_op_i32_bitwise(
                    comp_ctx, func_ctx, INT_SHL + opcode - WASM_OP_I32_AND))
                return false;
            break;

        case WASM_OP_I32_SHL:
        case WASM_OP_I32_SHR_S:
        case WASM_OP_I32_SHR_U:
        case WASM_OP_I32_ROTL:
        case WASM_OP_I32_ROTR:
            if (!wasm_jit_compile_op_i32_shift(
                    comp_ctx, func_ctx, INT_SHL + opcode - WASM_OP_I32_SHL))
                return false;
            break;

        case WASM_OP_I64_CLZ:
            if (!wasm_jit_compile_op_i64_clz(comp_ctx, func_ctx))
                return false;
            break;

        case WASM_OP_I64_CTZ:
            if (!wasm_jit_compile_op_i64_ctz(comp_ctx, func_ctx))
                return false;
            break;

        case WASM_OP_I64_POPCNT:
            if (!wasm_jit_compile_op_i64_popcnt(comp_ctx, func_ctx))
                return false;
            break;

        case WASM_OP_I64_ADD:
        case WASM_OP_I64_SUB:
        case WASM_OP_I64_MUL:
        case WASM_OP_I64_DIV_S:
        case WASM_OP_I64_DIV_U:
        case WASM_OP_I64_REM_S:
        case WASM_OP_I64_REM_U:
            if (!wasm_jit_compile_op_i64_arithmetic(
                    comp_ctx, func_ctx, INT_ADD + opcode - WASM_OP_I64_ADD,
                    &frame_ip))
                return false;
            break;

        case WASM_OP_I64_AND:
        case WASM_OP_I64_OR:
        case WASM_OP_I64_XOR:
            if (!wasm_jit_compile_op_i64_bitwise(
                    comp_ctx, func_ctx, INT_SHL + opcode - WASM_OP_I64_AND))
                return false;
            break;

        case WASM_OP_I64_SHL:
        case WASM_OP_I64_SHR_S:
        case WASM_OP_I64_SHR_U:
        case WASM_OP_I64_ROTL:
        case WASM_OP_I64_ROTR:
            if (!wasm_jit_compile_op_i64_shift(
                    comp_ctx, func_ctx, INT_SHL + opcode - WASM_OP_I64_SHL))
                return false;
            break;

        case WASM_OP_F32_ABS:
        case WASM_OP_F32_NEG:
        case WASM_OP_F32_CEIL:
        case WASM_OP_F32_FLOOR:
        case WASM_OP_F32_TRUNC:
        case WASM_OP_F32_NEAREST:
        case WASM_OP_F32_SQRT:
            if (!wasm_jit_compile_op_f32_math(comp_ctx, func_ctx,
                                              FLOAT_ABS + opcode - WASM_OP_F32_ABS))
                return false;
            break;

        case WASM_OP_F32_ADD:
        case WASM_OP_F32_SUB:
        case WASM_OP_F32_MUL:
        case WASM_OP_F32_DIV:
        case WASM_OP_F32_MIN:
        case WASM_OP_F32_MAX:
            if (!wasm_jit_compile_op_f32_arithmetic(comp_ctx, func_ctx,
                                                    FLOAT_ADD + opcode - WASM_OP_F32_ADD))
                return false;
            break;

        case WASM_OP_F32_COPYSIGN:
            if (!wasm_jit_compile_op_f32_copysign(comp_ctx, func_ctx))
                return false;
            break;

        case WASM_OP_F64_ABS:
        case WASM_OP_F64_NEG:
        case WASM_OP_F64_CEIL:
        case WASM_OP_F64_FLOOR:
        case WASM_OP_F64_TRUNC:
        case WASM_OP_F64_NEAREST:
        case WASM_OP_F64_SQRT:
            if (!wasm_jit_compile_op_f64_math(comp_ctx, func_ctx,
                                              FLOAT_ABS + opcode - WASM_OP_F64_ABS))
                return false;
            break;

        case WASM_OP_F64_ADD:
        case WASM_OP_F64_SUB:
        case WASM_OP_F64_MUL:
        case WASM_OP_F64_DIV:
        case WASM_OP_F64_MIN:
        case WASM_OP_F64_MAX:
            if (!wasm_jit_compile_op_f64_arithmetic(comp_ctx, func_ctx,
                                                    FLOAT_ADD + opcode - WASM_OP_F64_ADD))
                return false;
            break;

        case WASM_OP_F64_COPYSIGN:
            if (!wasm_jit_compile_op_f64_copysign(comp_ctx, func_ctx))
                return false;
            break;

        case WASM_OP_I32_WRAP_I64:
            POP_I64(llvm_value);
            LLVMOPTrunc(llvm_value, I32_TYPE);
            PUSH_I32(llvm_value);
            break;

        case WASM_OP_I32_TRUNC_S_F32:
        case WASM_OP_I32_TRUNC_U_F32:
            sign = (opcode == WASM_OP_I32_TRUNC_S_F32) ? true : false;
            if (!wasm_jit_compile_op_i32_trunc_f32(comp_ctx, func_ctx, sign,
                                                   false))
                return false;
            break;

        case WASM_OP_I32_TRUNC_S_F64:
        case WASM_OP_I32_TRUNC_U_F64:
            sign = (opcode == WASM_OP_I32_TRUNC_S_F64) ? true : false;
            if (!wasm_jit_compile_op_i32_trunc_f64(comp_ctx, func_ctx, sign,
                                                   false))
                return false;
            break;

        case WASM_OP_I64_EXTEND_S_I32:
            POP_I32(llvm_value);
            LLVMOPSExt(llvm_value, I64_TYPE);
            PUSH_I64(llvm_value);
            break;
        case WASM_OP_I64_EXTEND_U_I32:
            POP_I32(llvm_value);
            LLVMOPZExt(llvm_value, I64_TYPE);
            PUSH_I64(llvm_value);
            break;

        case WASM_OP_I64_TRUNC_S_F32:
        case WASM_OP_I64_TRUNC_U_F32:
            sign = (opcode == WASM_OP_I64_TRUNC_S_F32) ? true : false;
            if (!wasm_jit_compile_op_i64_trunc_f32(comp_ctx, func_ctx, sign,
                                                   false))
                return false;
            break;

        case WASM_OP_I64_TRUNC_S_F64:
        case WASM_OP_I64_TRUNC_U_F64:
            sign = (opcode == WASM_OP_I64_TRUNC_S_F64) ? true : false;
            if (!wasm_jit_compile_op_i64_trunc_f64(comp_ctx, func_ctx, sign,
                                                   false))
                return false;
            break;

        case WASM_OP_F32_CONVERT_S_I32:
        case WASM_OP_F32_CONVERT_U_I32:
            sign = (opcode == WASM_OP_F32_CONVERT_S_I32) ? true : false;
            if (!wasm_jit_compile_op_f32_convert_i32(comp_ctx, func_ctx, sign))
                return false;
            break;

        case WASM_OP_F32_CONVERT_S_I64:
        case WASM_OP_F32_CONVERT_U_I64:
            sign = (opcode == WASM_OP_F32_CONVERT_S_I64) ? true : false;
            if (!wasm_jit_compile_op_f32_convert_i64(comp_ctx, func_ctx, sign))
                return false;
            break;

        case WASM_OP_F32_DEMOTE_F64:
            if (!wasm_jit_compile_op_f32_demote_f64(comp_ctx, func_ctx))
                return false;
            break;

        case WASM_OP_F64_CONVERT_S_I32:
        case WASM_OP_F64_CONVERT_U_I32:
            sign = (opcode == WASM_OP_F64_CONVERT_S_I32) ? true : false;
            if (!wasm_jit_compile_op_f64_convert_i32(comp_ctx, func_ctx, sign))
                return false;
            break;

        case WASM_OP_F64_CONVERT_S_I64:
        case WASM_OP_F64_CONVERT_U_I64:
            sign = (opcode == WASM_OP_F64_CONVERT_S_I64) ? true : false;
            if (!wasm_jit_compile_op_f64_convert_i64(comp_ctx, func_ctx, sign))
                return false;
            break;

        case WASM_OP_F64_PROMOTE_F32:
            if (!wasm_jit_compile_op_f64_promote_f32(comp_ctx, func_ctx))
                return false;
            break;

        case WASM_OP_I32_REINTERPRET_F32:
            POP_F32(llvm_value);
            LLVMOPBitCast(llvm_value, I32_TYPE);
            PUSH_I32(llvm_value);
            break;

        case WASM_OP_I64_REINTERPRET_F64:
            POP_F64(llvm_value);
            LLVMOPBitCast(llvm_value, I64_TYPE);
            PUSH_I64(llvm_value);
            break;

        case WASM_OP_F32_REINTERPRET_I32:
            POP_I32(llvm_value);
            LLVMOPBitCast(llvm_value, F32_TYPE);
            PUSH_F32(llvm_value);
            break;

        case WASM_OP_F64_REINTERPRET_I64:
            POP_I64(llvm_value);
            LLVMOPBitCast(llvm_value, F64_TYPE);
            PUSH_F64(llvm_value);
            break;

        case WASM_OP_I32_EXTEND8_S:
            POP_I32(llvm_value);
            LLVMOPSIntCast(llvm_value, INT8_TYPE);
            LLVMOPSExt(llvm_value, I32_TYPE);
            PUSH_I32(llvm_value);
            break;

        case WASM_OP_I32_EXTEND16_S:
            POP_I32(llvm_value);
            LLVMOPSIntCast(llvm_value, INT16_TYPE);
            LLVMOPSExt(llvm_value, I32_TYPE);
            PUSH_I32(llvm_value);
            break;

        case WASM_OP_I64_EXTEND8_S:
            POP_I64(llvm_value);
            LLVMOPSIntCast(llvm_value, INT8_TYPE);
            LLVMOPSExt(llvm_value, I64_TYPE);
            PUSH_I64(llvm_value);
            break;

        case WASM_OP_I64_EXTEND16_S:
            POP_I64(llvm_value);
            LLVMOPSIntCast(llvm_value, INT16_TYPE);
            LLVMOPSExt(llvm_value, I64_TYPE);
            PUSH_I64(llvm_value);
            break;

        case WASM_OP_I64_EXTEND32_S:
            POP_I64(llvm_value);
            LLVMOPSIntCast(llvm_value, I32_TYPE);
            LLVMOPSExt(llvm_value, I64_TYPE);
            PUSH_I64(llvm_value);
            break;

        case WASM_OP_MISC_PREFIX:
        {
            uint32 opcode1;

            read_leb_uint32(frame_ip, frame_ip_end, opcode1);
            opcode = (uint32)opcode1;

            switch (opcode)
            {
            case WASM_OP_I32_TRUNC_SAT_S_F32:
            case WASM_OP_I32_TRUNC_SAT_U_F32:
                sign = (opcode == WASM_OP_I32_TRUNC_SAT_S_F32) ? true
                                                               : false;
                if (!wasm_jit_compile_op_i32_trunc_f32(comp_ctx, func_ctx,
                                                       sign, true))
                    return false;
                break;
            case WASM_OP_I32_TRUNC_SAT_S_F64:
            case WASM_OP_I32_TRUNC_SAT_U_F64:
                sign = (opcode == WASM_OP_I32_TRUNC_SAT_S_F64) ? true
                                                               : false;
                if (!wasm_jit_compile_op_i32_trunc_f64(comp_ctx, func_ctx,
                                                       sign, true))
                    return false;
                break;
            case WASM_OP_I64_TRUNC_SAT_S_F32:
            case WASM_OP_I64_TRUNC_SAT_U_F32:
                sign = (opcode == WASM_OP_I64_TRUNC_SAT_S_F32) ? true
                                                               : false;
                if (!wasm_jit_compile_op_i64_trunc_f32(comp_ctx, func_ctx,
                                                       sign, true))
                    return false;
                break;
            case WASM_OP_I64_TRUNC_SAT_S_F64:
            case WASM_OP_I64_TRUNC_SAT_U_F64:
                sign = (opcode == WASM_OP_I64_TRUNC_SAT_S_F64) ? true
                                                               : false;
                if (!wasm_jit_compile_op_i64_trunc_f64(comp_ctx, func_ctx,
                                                       sign, true))
                    return false;
                break;
            case WASM_OP_MEMORY_INIT:
            {
                uint32 seg_index;
                read_leb_uint32(frame_ip, frame_ip_end, seg_index);
                frame_ip++;
                if (!wasm_jit_compile_op_memory_init(comp_ctx, func_ctx,
                                                     seg_index))
                    return false;
                break;
            }
            case WASM_OP_DATA_DROP:
            {
                uint32 seg_index;
                read_leb_uint32(frame_ip, frame_ip_end, seg_index);
                if (!wasm_jit_compile_op_data_drop(comp_ctx, func_ctx,
                                                   seg_index))
                    return false;
                break;
            }
            case WASM_OP_MEMORY_COPY:
            {
                frame_ip += 2;
                if (!wasm_jit_compile_op_memory_copy(comp_ctx, func_ctx))
                    return false;
                break;
            }
            case WASM_OP_MEMORY_FILL:
            {
                frame_ip++;
                if (!wasm_jit_compile_op_memory_fill(comp_ctx, func_ctx))
                    return false;
                break;
            }

            default:
                wasm_jit_set_last_error("unsupported opcode");
                return false;
            }
            break;
        }

        default:
            wasm_jit_set_last_error("unsupported opcode");
            return false;
        }
    }

    /* Move func_return block to the bottom */
    if (func_ctx->func_return_block)
    {
        LLVMBasicBlockRef last_block = LLVMGetLastBasicBlock(func_ctx->func);
        if (last_block != func_ctx->func_return_block)
            LLVMMoveBasicBlockAfter(func_ctx->func_return_block, last_block);
    }

    /* Move got_exception block to the bottom */
    if (func_ctx->got_exception_block)
    {
        LLVMBasicBlockRef last_block = LLVMGetLastBasicBlock(func_ctx->func);
        if (last_block != func_ctx->got_exception_block)
            LLVMMoveBasicBlockAfter(func_ctx->got_exception_block, last_block);
    }

    wasm_runtime_free(func_ctx->block_stack_bottom);
    wasm_runtime_free(func_ctx->value_stack_bottom);

    return true;

fail:
    return false;
}

bool wasm_jit_compile_wasm(WASMModule *module)
{
    uint32 i;
    JITCompContext *comp_ctx = module->comp_ctx;

    for (i = 0; i < comp_ctx->func_ctx_count; i++)
    {
        if (!wasm_jit_compile_func(module, comp_ctx, i))
        {
            return false;
        }
    }

    /* Run IR optimization before feeding in ORCJIT and AOT codegen */
    // if (1)
    // {
    //     /* Run passes for AOT/JIT mode.
    //        TODO: Apply these passes in the do_ir_transform callback of
    //        TransformLayer when compiling each jit function, so as to
    //        speedup the launch process. Now there are two issues in the
    //        JIT: one is memory leak in do_ir_transform, the other is
    //        possible core dump. */
    //     wasm_jit_apply_llvm_new_pass_manager(comp_ctx, comp_ctx->module);

    //     /* Run specific passes for AOT indirect mode in last since general
    //        optimization may create some intrinsic function calls like
    //        llvm.memset, so let's remove these function calls here. */
    // }

    LLVMErrorRef err;
    LLVMOrcJITDylibRef orc_main_dylib;
    LLVMOrcThreadSafeModuleRef orc_thread_safe_module;

    orc_main_dylib = LLVMOrcLLLazyJITGetMainJITDylib(comp_ctx->orc_jit);
    if (!orc_main_dylib)
    {
        wasm_jit_set_last_error(
            "failed to get orc orc_jit main dynmaic library");
        return false;
    }

    orc_thread_safe_module = LLVMOrcCreateNewThreadSafeModule(
        comp_ctx->module, comp_ctx->orc_thread_safe_context);
    if (!orc_thread_safe_module)
    {
        wasm_jit_set_last_error("failed to create thread safe module");
        return false;
    }

    if ((err = LLVMOrcLLLazyJITAddLLVMIRModule(
             comp_ctx->orc_jit, orc_main_dylib, orc_thread_safe_module)))
    {
        /* If adding the ThreadSafeModule fails then we need to clean it up
           by ourselves, otherwise the orc orc_jit will manage the memory.
         */
        LLVMOrcDisposeThreadSafeModule(orc_thread_safe_module);
        wasm_jit_handle_llvm_errmsg("failed to addIRModule", err);
        return false;
    }

    return true;
}

bool wasm_jit_emit_llvm_file(JITCompContext *comp_ctx, const char *file_name)
{
    char *err = NULL;

    if (LLVMPrintModuleToFile(comp_ctx->module, file_name, &err) != 0)
    {
        if (err)
        {
            LLVMDisposeMessage(err);
            err = NULL;
        }
        wasm_jit_set_last_error("emit llvm ir to file failed.");
        return false;
    }

    return true;
}