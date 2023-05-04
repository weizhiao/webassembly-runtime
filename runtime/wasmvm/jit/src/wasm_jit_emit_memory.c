#include "wasm_jit_emit_memory.h"
#include "wasm_jit_emit_exception.h"
#include "wasm_jit_emit_control.h"
#include "wasm_exception.h"

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

#define ADD_BASIC_BLOCK(block, name)                                        \
    do                                                                      \
    {                                                                       \
        if (!(block = LLVMAppendBasicBlockInContext(comp_ctx->context,      \
                                                    func_ctx->func, name))) \
        {                                                                   \
            wasm_jit_set_last_error("llvm add basic block failed.");        \
            goto fail;                                                      \
        }                                                                   \
    } while (0)

#define SET_BUILD_POS(block) LLVMPositionBuilderAtEnd(comp_ctx->builder, block)

void *
wasm_jit_memmove(void *dest, const void *src, size_t n)
{
    return memmove(dest, src, n);
}

bool llvm_jit_memory_init(WASMModule *module, uint32 seg_index,
                          uint32 offset, uint32 len, uint32 dst)
{
    uint8 *data = NULL;
    uint8 *maddr;
    uint64 seg_len = 0;

    seg_len = module->data_segments[seg_index].data_length;
    data = module->data_segments[seg_index].data;

    if (!wasm_runtime_validate_app_addr(module,
                                        dst, len))
        return false;

    if ((uint64)offset + (uint64)len > seg_len)
    {
        wasm_set_exception(module, "out of bounds memory access");
        return false;
    }

    maddr = wasm_runtime_addr_app_to_native(module, dst);

    memcpy(maddr, data + offset, len);
    return true;
}

bool llvm_jit_data_drop(WASMModule *module, uint32 seg_index)
{

    module->data_segments[seg_index].data_length = 0;
    /* Currently we can't free the dropped data segment
       as they are stored in wasm bytecode */
    return true;
}

LLVMValueRef
wasm_jit_check_memory_overflow(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                               uint32 offset, uint32 bytes)
{
    LLVMValueRef offset_const = I32_CONST(offset);
    LLVMValueRef addr, maddr, offset1;
    LLVMValueRef mem_base_addr;

    if (func_ctx->mem_space_unchanged)
    {
        mem_base_addr = func_ctx->mem_info.mem_base_addr;
    }
    else
    {
        LLVMOPLoad(mem_base_addr,
                   func_ctx->mem_info.mem_base_addr, OPQ_PTR_TYPE);
    }

    POP_I32(addr);

    LLVMOPAdd(offset_const, addr, offset1, "offset1");

    if (!(maddr = LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE,
                                        mem_base_addr, &offset1, 1, "maddr")))
    {
        wasm_jit_set_last_error("llvm build add failed.");
        goto fail;
    }
    return maddr;
fail:
    return NULL;
}

bool wasm_jit_compile_op_i32_load(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 align, uint32 offset, uint32 bytes, bool sign,
                                  bool atomic)
{
    LLVMValueRef maddr, value = NULL;
    LLVMTypeRef data_type;

    if (!(maddr = wasm_jit_check_memory_overflow(comp_ctx, func_ctx, offset, bytes)))
        return false;

    switch (bytes)
    {
    case 4:
        LLVMOPBitCast(maddr, INT32_PTR_TYPE);
        LLVMOPLoad(value, maddr, I32_TYPE);
        break;
    case 2:
    case 1:
        if (bytes == 2)
        {
            LLVMOPBitCast(maddr, INT16_PTR_TYPE);
            data_type = INT16_TYPE;
        }
        else
        {
            LLVMOPBitCast(maddr, INT8_PTR_TYPE);
            data_type = INT8_TYPE;
        }
        {
            LLVMOPLoad(value, maddr, data_type);
            if (sign)
                LLVMOPSExt(value, I32_TYPE);
            else
                LLVMOPZExt(value, I32_TYPE);
        }
        break;
    default:;
        break;
    }

    PUSH_I32(value);
    (void)data_type;
    return true;
fail:
    return false;
}

bool wasm_jit_compile_op_i64_load(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 align, uint32 offset, uint32 bytes, bool sign,
                                  bool atomic)
{
    LLVMValueRef maddr, value = NULL;
    LLVMTypeRef data_type;

    if (!(maddr = wasm_jit_check_memory_overflow(comp_ctx, func_ctx, offset, bytes)))
        return false;

    switch (bytes)
    {
    case 8:
        LLVMOPBitCast(maddr, INT64_PTR_TYPE);
        LLVMOPLoad(value, maddr, I64_TYPE);
        break;
    case 4:
    case 2:
    case 1:
        if (bytes == 4)
        {
            LLVMOPBitCast(maddr, INT32_PTR_TYPE);
            data_type = I32_TYPE;
        }
        else if (bytes == 2)
        {
            LLVMOPBitCast(maddr, INT16_PTR_TYPE);
            data_type = INT16_TYPE;
        }
        else
        {
            LLVMOPBitCast(maddr, INT8_PTR_TYPE);
            data_type = INT8_TYPE;
        }
        {
            LLVMOPLoad(value, maddr, data_type);
            if (sign)
                LLVMOPSExt(value, I64_TYPE);
            else
                LLVMOPZExt(value, I64_TYPE);
        }
        break;
    default:;
        break;
    }

    PUSH_I64(value);
    (void)data_type;
    return true;
fail:
    return false;
}

bool wasm_jit_compile_op_f32_load(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 align, uint32 offset)
{
    LLVMValueRef maddr, value;

    if (!(maddr = wasm_jit_check_memory_overflow(comp_ctx, func_ctx, offset, 4)))
        return false;

    LLVMOPBitCast(maddr, F32_PTR_TYPE);
    LLVMOPLoad(value, maddr, F32_TYPE);
    PUSH_F32(value);
    return true;
fail:
    return false;
}

bool wasm_jit_compile_op_f64_load(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 align, uint32 offset)
{
    LLVMValueRef maddr, value;

    if (!(maddr = wasm_jit_check_memory_overflow(comp_ctx, func_ctx, offset, 8)))
        return false;

    LLVMOPBitCast(maddr, F64_PTR_TYPE);
    LLVMOPLoad(value, maddr, F64_TYPE);
    PUSH_F64(value);
    return true;
fail:
    return false;
}

bool wasm_jit_compile_op_i32_store(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                   uint32 align, uint32 offset, uint32 bytes, bool atomic)
{
    LLVMValueRef maddr, value, res;

    POP_I32(value);

    if (!(maddr = wasm_jit_check_memory_overflow(comp_ctx, func_ctx, offset, bytes)))
        return false;

    switch (bytes)
    {
    case 4:
        LLVMOPBitCast(maddr, INT32_PTR_TYPE);
        break;
    case 2:
        LLVMOPBitCast(maddr, INT16_PTR_TYPE);
        LLVMOPTrunc(value, INT16_TYPE);
        break;
    case 1:
        LLVMOPBitCast(maddr, INT8_PTR_TYPE);
        LLVMOPTrunc(value, INT8_TYPE);
        break;
    default:;
        break;
    }

    LLVMOPStore(res, value, maddr);
    return true;
fail:
    return false;
}

bool wasm_jit_compile_op_i64_store(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                   uint32 align, uint32 offset, uint32 bytes, bool atomic)
{
    LLVMValueRef maddr, value, res;

    POP_I64(value);

    if (!(maddr = wasm_jit_check_memory_overflow(comp_ctx, func_ctx, offset, bytes)))
        return false;

    switch (bytes)
    {
    case 8:
        LLVMOPBitCast(maddr, INT64_PTR_TYPE);
        break;
    case 4:
        LLVMOPBitCast(maddr, INT32_PTR_TYPE);
        LLVMOPTrunc(value, I32_TYPE);
        break;
    case 2:
        LLVMOPBitCast(maddr, INT16_PTR_TYPE);
        LLVMOPTrunc(value, INT16_TYPE);
        break;
    case 1:
        LLVMOPBitCast(maddr, INT8_PTR_TYPE);
        LLVMOPTrunc(value, INT8_TYPE);
        break;
    default:;
        break;
    }
    LLVMOPStore(res, value, maddr);
    return true;
fail:
    return false;
}

bool wasm_jit_compile_op_f32_store(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                   uint32 align, uint32 offset)
{
    LLVMValueRef maddr, value, res;

    POP_F32(value);

    if (!(maddr = wasm_jit_check_memory_overflow(comp_ctx, func_ctx, offset, 4)))
        return false;

    LLVMOPBitCast(maddr, F32_PTR_TYPE);
    LLVMOPStore(res, value, maddr);
    return true;
fail:
    return false;
}

bool wasm_jit_compile_op_f64_store(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                   uint32 align, uint32 offset)
{
    LLVMValueRef maddr, value, res;

    POP_F64(value);

    if (!(maddr = wasm_jit_check_memory_overflow(comp_ctx, func_ctx, offset, 8)))
        return false;

    LLVMOPBitCast(maddr, F64_PTR_TYPE);
    LLVMOPStore(res, value, maddr);
    return true;
fail:
    return false;
}

static LLVMValueRef
get_memory_curr_page_count(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    LLVMValueRef mem_size;

    if (func_ctx->mem_space_unchanged)
    {
        mem_size = func_ctx->mem_info.mem_cur_page_count_addr;
    }
    else
    {
        LLVMOPLoad(mem_size, func_ctx->mem_info.mem_cur_page_count_addr, I32_TYPE);
    }

    return mem_size;
fail:
    return NULL;
}

bool wasm_jit_compile_op_memory_size(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    LLVMValueRef mem_size = get_memory_curr_page_count(comp_ctx, func_ctx);

    if (mem_size)
        PUSH_I32(mem_size);
    return mem_size ? true : false;
}

bool wasm_jit_compile_op_memory_grow(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    LLVMValueRef mem_size = get_memory_curr_page_count(comp_ctx, func_ctx);
    LLVMValueRef delta, param_values[2], ret_value, func, value;
    LLVMTypeRef param_types[2], ret_type, func_type, func_ptr_type;

    if (!mem_size)
        return false;

    POP_I32(delta);

    /* Function type of wasm_jit_enlarge_memory() */
    param_types[0] = INT8_PTR_TYPE;
    param_types[1] = I32_TYPE;
    ret_type = INT8_TYPE;

    if (!(func_type = LLVMFunctionType(ret_type, param_types, 2, false)))
    {
        wasm_jit_set_last_error("llvm add function type failed.");
        return false;
    }

    /* JIT mode, call the function directly */
    if (!(func_ptr_type = LLVMPointerType(func_type, 0)))
    {
        wasm_jit_set_last_error("llvm add pointer type failed.");
        return false;
    }
    if (!(value = I64_CONST((uint64)(uintptr_t)wasm_enlarge_memory)) || !(func = LLVMConstIntToPtr(value, func_ptr_type)))
    {
        wasm_jit_set_last_error("create LLVM value failed.");
        return false;
    }

    /* Call function wasm_jit_enlarge_memory() */
    param_values[0] = func_ctx->wasm_module;
    param_values[1] = delta;
    if (!(ret_value = LLVMBuildCall2(comp_ctx->builder, func_type, func,
                                     param_values, 2, "call")))
    {
        wasm_jit_set_last_error("llvm build call failed.");
        return false;
    }

    BUILD_ICMP(LLVMIntUGT, ret_value, I8_ZERO, ret_value, "mem_grow_ret");

    /* ret_value = ret_value == true ? delta : pre_page_count */
    if (!(ret_value = LLVMBuildSelect(comp_ctx->builder, ret_value, mem_size,
                                      I32_NEG_ONE, "mem_grow_ret")))
    {
        wasm_jit_set_last_error("llvm build select failed.");
        return false;
    }

    PUSH_I32(ret_value);
    return true;
fail:
    return false;
}

static LLVMValueRef
check_bulk_memory_overflow(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                           LLVMValueRef offset, LLVMValueRef bytes)
{
    LLVMValueRef maddr, max_addr, cmp;
    LLVMValueRef mem_base_addr;
    LLVMBasicBlockRef block_curr = LLVMGetInsertBlock(comp_ctx->builder);
    LLVMBasicBlockRef check_succ;
    LLVMValueRef mem_size;

    /* Get memory base address and memory data size */
    if (func_ctx->mem_space_unchanged)
    {
        mem_base_addr = func_ctx->mem_info.mem_base_addr;
    }
    else
    {
        LLVMOPLoad(mem_base_addr,
                   func_ctx->mem_info.mem_base_addr,
                   OPQ_PTR_TYPE);
    }

    if (func_ctx->mem_space_unchanged)
    {
        mem_size = func_ctx->mem_info.mem_data_size_addr;
    }
    else
    {
        LLVMOPLoad(mem_size,
                   func_ctx->mem_info.mem_data_size_addr, I32_TYPE);
    }

    ADD_BASIC_BLOCK(check_succ, "check_succ");
    LLVMMoveBasicBlockAfter(check_succ, block_curr);

    offset =
        LLVMBuildZExt(comp_ctx->builder, offset, I64_TYPE, "extend_offset");
    bytes = LLVMBuildZExt(comp_ctx->builder, bytes, I64_TYPE, "extend_len");
    mem_size =
        LLVMBuildZExt(comp_ctx->builder, mem_size, I64_TYPE, "extend_size");

    LLVMOPAdd(offset, bytes, max_addr, "max_addr");
    BUILD_ICMP(LLVMIntUGT, max_addr, mem_size, cmp, "cmp_max_mem_addr");
    if (!wasm_jit_emit_exception(comp_ctx, func_ctx,
                                 EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS, true, cmp,
                                 check_succ))
    {
        goto fail;
    }

    /* maddr = mem_base_addr + offset */
    if (!(maddr = LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE,
                                        mem_base_addr, &offset, 1, "maddr")))
    {
        wasm_jit_set_last_error("llvm build add failed.");
        goto fail;
    }
    return maddr;
fail:
    return NULL;
}

bool wasm_jit_compile_op_memory_init(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                     uint32 seg_index)
{
    LLVMValueRef seg, offset, dst, len, param_values[5], ret_value, func, value;
    LLVMTypeRef param_types[5], ret_type, func_type, func_ptr_type;
    WASMType *wasm_jit_func_type = func_ctx->wasm_func->func_type;
    LLVMBasicBlockRef block_curr = LLVMGetInsertBlock(comp_ctx->builder);
    LLVMBasicBlockRef mem_init_fail, init_success;

    seg = I32_CONST(seg_index);

    POP_I32(len);
    POP_I32(offset);
    POP_I32(dst);

    param_types[0] = INT8_PTR_TYPE;
    param_types[1] = I32_TYPE;
    param_types[2] = I32_TYPE;
    param_types[3] = I32_TYPE;
    param_types[4] = I32_TYPE;
    ret_type = INT8_TYPE;

    GET_WASM_JIT_FUNCTION(llvm_jit_memory_init, 5);

    /* Call function wasm_jit_memory_init() */
    param_values[0] = func_ctx->wasm_module;
    param_values[1] = seg;
    param_values[2] = offset;
    param_values[3] = len;
    param_values[4] = dst;
    if (!(ret_value = LLVMBuildCall2(comp_ctx->builder, func_type, func,
                                     param_values, 5, "call")))
    {
        wasm_jit_set_last_error("llvm build call failed.");
        return false;
    }

    BUILD_ICMP(LLVMIntUGT, ret_value, I8_ZERO, ret_value, "mem_init_ret");

    ADD_BASIC_BLOCK(mem_init_fail, "mem_init_fail");
    ADD_BASIC_BLOCK(init_success, "init_success");

    LLVMMoveBasicBlockAfter(mem_init_fail, block_curr);
    LLVMMoveBasicBlockAfter(init_success, block_curr);

    if (!LLVMBuildCondBr(comp_ctx->builder, ret_value, init_success,
                         mem_init_fail))
    {
        wasm_jit_set_last_error("llvm build cond br failed.");
        goto fail;
    }

    /* If memory.init failed, return this function
       so the runtime can catch the exception */
    LLVMPositionBuilderAtEnd(comp_ctx->builder, mem_init_fail);
    if (!wasm_jit_build_zero_function_ret(comp_ctx, func_ctx, wasm_jit_func_type))
    {
        goto fail;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, init_success);

    return true;
fail:
    return false;
}

bool wasm_jit_compile_op_data_drop(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                   uint32 seg_index)
{
    LLVMValueRef seg, param_values[2], ret_value, func, value;
    LLVMTypeRef param_types[2], ret_type, func_type, func_ptr_type;

    seg = I32_CONST(seg_index);
    CHECK_LLVM_CONST(seg);

    param_types[0] = INT8_PTR_TYPE;
    param_types[1] = I32_TYPE;
    ret_type = INT8_TYPE;

    GET_WASM_JIT_FUNCTION(llvm_jit_data_drop, 2);

    /* Call function wasm_jit_data_drop() */
    param_values[0] = func_ctx->wasm_module;
    param_values[1] = seg;
    if (!(ret_value = LLVMBuildCall2(comp_ctx->builder, func_type, func,
                                     param_values, 2, "call")))
    {
        wasm_jit_set_last_error("llvm build call failed.");
        return false;
    }

    return true;
fail:
    return false;
}

bool wasm_jit_compile_op_memory_copy(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    LLVMValueRef src, dst, src_addr, dst_addr, len, res;

    POP_I32(len);
    POP_I32(src);
    POP_I32(dst);

    if (!(src_addr = check_bulk_memory_overflow(comp_ctx, func_ctx, src, len)))
        return false;

    if (!(dst_addr = check_bulk_memory_overflow(comp_ctx, func_ctx, dst, len)))
        return false;

    LLVMTypeRef param_types[3], ret_type, func_type, func_ptr_type;
    LLVMValueRef func, params[3];

    param_types[0] = INT8_PTR_TYPE;
    param_types[1] = INT8_PTR_TYPE;
    param_types[2] = I32_TYPE;
    ret_type = INT8_PTR_TYPE;

    if (!(func_type = LLVMFunctionType(ret_type, param_types, 3, false)))
    {
        wasm_jit_set_last_error("create LLVM function type failed.");
        return false;
    }

    if (!(func_ptr_type = LLVMPointerType(func_type, 0)))
    {
        wasm_jit_set_last_error("create LLVM function pointer type failed.");
        return false;
    }

    if (!(func = I64_CONST((uint64)(uintptr_t)wasm_jit_memmove)) || !(func = LLVMConstIntToPtr(func, func_ptr_type)))
    {
        wasm_jit_set_last_error("create LLVM value failed.");
        return false;
    }

    params[0] = dst_addr;
    params[1] = src_addr;
    params[2] = len;
    if (!(res = LLVMBuildCall2(comp_ctx->builder, func_type, func, params,
                               3, "call_memmove")))
    {
        wasm_jit_set_last_error("llvm build memmove failed.");
        return false;
    }

    return true;
}

static void *
jit_memset(void *s, int c, size_t n)
{
    return memset(s, c, n);
}

bool wasm_jit_compile_op_memory_fill(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    LLVMValueRef val, dst, dst_addr, len, res;
    LLVMTypeRef param_types[3], ret_type, func_type, func_ptr_type;
    LLVMValueRef func, params[3];

    POP_I32(len);
    POP_I32(val);
    POP_I32(dst);

    if (!(dst_addr = check_bulk_memory_overflow(comp_ctx, func_ctx, dst, len)))
        return false;

    param_types[0] = INT8_PTR_TYPE;
    param_types[1] = I32_TYPE;
    param_types[2] = I32_TYPE;
    ret_type = INT8_PTR_TYPE;

    if (!(func_type = LLVMFunctionType(ret_type, param_types, 3, false)))
    {
        wasm_jit_set_last_error("create LLVM function type failed.");
        return false;
    }

    if (!(func_ptr_type = LLVMPointerType(func_type, 0)))
    {
        wasm_jit_set_last_error("create LLVM function pointer type failed.");
        return false;
    }

    if (!(func = I64_CONST((uint64)(uintptr_t)jit_memset)) || !(func = LLVMConstIntToPtr(func, func_ptr_type)))
    {
        wasm_jit_set_last_error("create LLVM value failed.");
        return false;
    }

    params[0] = dst_addr;
    params[1] = val;
    params[2] = len;
    if (!(res = LLVMBuildCall2(comp_ctx->builder, func_type, func, params, 3,
                               "call_memset")))
    {
        wasm_jit_set_last_error("llvm build memset failed.");
        return false;
    }

    return true;
}
