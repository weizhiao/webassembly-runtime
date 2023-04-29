#include "wasm_jit_llvm.h"
#include "wasm_jit_compiler.h"
#include "wasm_jit_emit_exception.h"
#include "wasm_jit_intrinsic.h"
#include "runtime_log.h"

LLVMTypeRef
wasm_type_to_llvm_type(JITLLVMTypes *llvm_types, uint8 wasm_type)
{
    switch (wasm_type)
    {
    case VALUE_TYPE_I32:
    case VALUE_TYPE_FUNCREF:
    case VALUE_TYPE_EXTERNREF:
        return llvm_types->int32_type;
    case VALUE_TYPE_I64:
        return llvm_types->int64_type;
    case VALUE_TYPE_F32:
        return llvm_types->float32_type;
    case VALUE_TYPE_F64:
        return llvm_types->float64_type;
    case VALUE_TYPE_V128:
        return llvm_types->i64x2_vec_type;
    case VALUE_TYPE_VOID:
        return llvm_types->void_type;
    default:
        break;
    }
    return NULL;
}

/**
 * Add LLVM function
 */
static LLVMValueRef
wasm_jit_add_llvm_func(JITCompContext *comp_ctx, LLVMModuleRef module,
                       WASMType *func_type, uint32 func_index,
                       LLVMTypeRef *p_func_type)
{
    LLVMValueRef func = NULL;
    LLVMTypeRef *param_types, ret_type, llvm_func_type;
    LLVMValueRef local_value;
    LLVMTypeRef func_type_wrapper;
    LLVMValueRef func_wrapper;
    LLVMBasicBlockRef func_begin;
    char func_name[48];
    uint64 size;
    uint32 i, j = 0, param_count;
    uint32 backend_thread_num, compile_thread_num;

    /* exec env as first parameter */
    param_count = func_type->param_count + 1;

    if (func_type->result_count > 1)
        param_count += func_type->result_count - 1;

    /* Initialize parameter types of the LLVM function */
    size = sizeof(LLVMTypeRef) * ((uint64)param_count);
    if (size >= UINT32_MAX || !(param_types = wasm_runtime_malloc((uint32)size)))
    {
        wasm_jit_set_last_error("allocate memory failed.");
        return NULL;
    }

    /* exec env as first parameter */
    param_types[j++] = comp_ctx->exec_env_type;
    for (i = 0; i < func_type->param_count; i++)
        param_types[j++] = TO_LLVM_TYPE(func_type->param[i]);
    /* Extra results' address */
    for (i = 1; i < func_type->result_count; i++, j++)
    {
        param_types[j] = TO_LLVM_TYPE(func_type->result[j]);
        if (!(param_types[j] = LLVMPointerType(param_types[j], 0)))
        {
            wasm_jit_set_last_error("llvm get pointer type failed.");
            goto fail;
        }
    }

    /* Resolve return type of the LLVM function */
    if (func_type->result_count)
        ret_type = TO_LLVM_TYPE(func_type->result[0]);
    else
        ret_type = VOID_TYPE;

    /* Resolve function prototype */
    if (!(llvm_func_type =
              LLVMFunctionType(ret_type, param_types, param_count, false)))
    {
        wasm_jit_set_last_error("create LLVM function type failed.");
        goto fail;
    }

    /* Add LLVM function */
    snprintf(func_name, sizeof(func_name), "%s%d", WASM_JIT_FUNC_PREFIX, func_index);
    if (!(func = LLVMAddFunction(module, func_name, llvm_func_type)))
    {
        wasm_jit_set_last_error("add LLVM function failed.");
        goto fail;
    }

    j = 0;
    local_value = LLVMGetParam(func, j++);
    LLVMSetValueName(local_value, "exec_env");

    /* Set parameter names */
    for (i = 0; i < func_type->param_count; i++)
    {
        local_value = LLVMGetParam(func, j++);
        LLVMSetValueName(local_value, "");
    }

    if (p_func_type)
        *p_func_type = llvm_func_type;

    backend_thread_num = WASM_ORC_JIT_BACKEND_THREAD_NUM;
    compile_thread_num = WASM_ORC_JIT_COMPILE_THREAD_NUM;

    /* Add the jit wrapper function with simple prototype, so that we
       can easily call it to trigger its compilation and let LLVM JIT
       compile the actual jit functions by adding them into the function
       list in the PartitionFunction callback */
    if ((func_index % (backend_thread_num * compile_thread_num) < backend_thread_num))
    {
        func_type_wrapper = LLVMFunctionType(VOID_TYPE, NULL, 0, false);
        if (!func_type_wrapper)
        {
            wasm_jit_set_last_error("create LLVM function type failed.");
            goto fail;
        }

        snprintf(func_name, sizeof(func_name), "%s%d%s", WASM_JIT_FUNC_PREFIX,
                 func_index, "_wrapper");
        if (!(func_wrapper =
                  LLVMAddFunction(module, func_name, func_type_wrapper)))
        {
            wasm_jit_set_last_error("add LLVM function failed.");
            goto fail;
        }

        if (!(func_begin = LLVMAppendBasicBlockInContext(
                  comp_ctx->context, func_wrapper, "func_begin")))
        {
            wasm_jit_set_last_error("add LLVM basic block failed.");
            goto fail;
        }

        LLVMPositionBuilderAtEnd(comp_ctx->builder, func_begin);
        if (!LLVMBuildRetVoid(comp_ctx->builder))
        {
            wasm_jit_set_last_error("llvm build ret failed.");
            goto fail;
        }
    }

fail:
    wasm_runtime_free(param_types);
    return func;
}

/**
 * Create first JITBlock, or function block for the function
 */
static bool
wasm_jit_create_func_block(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                           WASMFunction *func, WASMType *func_type)
{
    JITBlock *block = func_ctx->block_stack;
    uint32 param_count = func_type->param_count,
           result_count = func_type->result_count;

    block->label_type = LABEL_TYPE_FUNCTION;
    block->param_count = param_count;
    block->param_types = func_type->param;
    block->result_count = result_count;
    block->result_types = func_type->result;
    block->else_param_phis = NULL;
    block->is_translate_else = false;
    block->param_phis = NULL;
    block->result_phis = NULL;
    block->llvm_end_block = NULL;
    block->llvm_else_block = NULL;
    block->is_polymorphic = false;

    if (!(block->llvm_entry_block = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func_ctx->func, "func_begin")))
    {
        wasm_jit_set_last_error("add LLVM basic block failed.");
        return false;
    }

    func_ctx->block_stack++;
    return true;
}

static bool
create_argv_buf(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    LLVMValueRef argv_buf_offset = I32_THREE, argv_buf_addr;

    /* Get argv buffer address */
    if (!(argv_buf_addr = LLVMBuildInBoundsGEP2(
              comp_ctx->builder, OPQ_PTR_TYPE, func_ctx->exec_env,
              &argv_buf_offset, 1, "argv_buf_addr")))
    {
        wasm_jit_set_last_error("llvm build in bounds gep failed");
        return false;
    }

    /* Convert to int32 pointer type */
    if (!(func_ctx->argv_buf = LLVMBuildBitCast(comp_ctx->builder, argv_buf_addr,
                                                INT32_PTR_TYPE, "argv_buf_ptr")))
    {
        wasm_jit_set_last_error("llvm build load failed");
        return false;
    }

    return true;
}

static bool
create_local_variables(JITCompContext *comp_ctx,
                       JITFuncContext *func_ctx, WASMFunction *func)
{
    WASMType *func_type = func->func_type;
    char local_name[32];
    uint32 i, j = 1;

    for (i = 0; i < func_type->param_count; i++, j++)
    {
        snprintf(local_name, sizeof(local_name), "l%d", i);
        func_ctx->locals[i] =
            LLVMBuildAlloca(comp_ctx->builder,
                            TO_LLVM_TYPE(func_type->param[i]), local_name);
        if (!func_ctx->locals[i])
        {
            wasm_jit_set_last_error("llvm build alloca failed.");
            return false;
        }
        if (!LLVMBuildStore(comp_ctx->builder, LLVMGetParam(func_ctx->func, j),
                            func_ctx->locals[i]))
        {
            wasm_jit_set_last_error("llvm build store failed.");
            return false;
        }
    }

    for (i = 0; i < func->local_count; i++)
    {
        LLVMTypeRef local_type;
        LLVMValueRef local_value = NULL;
        snprintf(local_name, sizeof(local_name), "l%d",
                 func_type->param_count + i);
        local_type = TO_LLVM_TYPE(func->local_types[i]);
        func_ctx->locals[func_type->param_count + i] =
            LLVMBuildAlloca(comp_ctx->builder, local_type, local_name);
        if (!func_ctx->locals[func_type->param_count + i])
        {
            wasm_jit_set_last_error("llvm build alloca failed.");
            return false;
        }
        switch (func->local_types[i])
        {
        case VALUE_TYPE_I32:
            local_value = I32_ZERO;
            break;
        case VALUE_TYPE_I64:
            local_value = I64_ZERO;
            break;
        case VALUE_TYPE_F32:
            local_value = F32_ZERO;
            break;
        case VALUE_TYPE_F64:
            local_value = F64_ZERO;
            break;
        case VALUE_TYPE_V128:
            local_value = V128_i64x2_ZERO;
            break;
        case VALUE_TYPE_FUNCREF:
        case VALUE_TYPE_EXTERNREF:
            local_value = REF_NULL;
            break;
        default:
            break;
        }
        if (!LLVMBuildStore(comp_ctx->builder, local_value,
                            func_ctx->locals[func_type->param_count + i]))
        {
            wasm_jit_set_last_error("llvm build store failed.");
            return false;
        }
    }

    return true;
}

static bool
create_memory_info(WASMModule *module, JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    LLVMValueRef offset;
    WASMFunction *func = func_ctx->wasm_func;
    LLVMTypeRef int8_pptr_type = comp_ctx->basic_types.int8_pptr_type;
    uint32 offset_of_memory_data;
    uint32 offset_of_global_data;
    bool mem_space_unchanged =
        (!func->has_op_memory_grow && !func->has_op_func_call) || (!module->possible_memory_grow);

    func_ctx->mem_space_unchanged = mem_space_unchanged;

    // 获取内存信息
    offset_of_memory_data = offsetof(WASMModule, memories);

    offset = I32_CONST(offset_of_memory_data + offsetof(WASMMemory, memory_data));
    if (!(func_ctx->mem_info.mem_base_addr = LLVMBuildInBoundsGEP2(
              comp_ctx->builder, INT8_TYPE, func_ctx->wasm_module, &offset, 1,
              "mem_base_addr_offset")))
    {
        wasm_jit_set_last_error("llvm build in bounds gep failed");
        return false;
    }
    offset = I32_CONST(offset_of_memory_data + offsetof(WASMMemory, cur_page_count));
    if (!(func_ctx->mem_info.mem_cur_page_count_addr =
              LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE,
                                    func_ctx->wasm_module, &offset, 1,
                                    "mem_cur_page_offset")))
    {
        wasm_jit_set_last_error("llvm build in bounds gep failed");
        return false;
    }
    offset = I32_CONST(offset_of_memory_data + offsetof(WASMMemory, memory_data_size));
    if (!(func_ctx->mem_info.mem_data_size_addr = LLVMBuildInBoundsGEP2(
              comp_ctx->builder, INT8_TYPE, func_ctx->wasm_module, &offset, 1,
              "mem_data_size_offset")))
    {
        wasm_jit_set_last_error("llvm build in bounds gep failed");
        return false;
    }

    if (!(func_ctx->mem_info.mem_base_addr = LLVMBuildBitCast(
              comp_ctx->builder, func_ctx->mem_info.mem_base_addr,
              int8_pptr_type, "mem_base_addr_ptr")))
    {
        wasm_jit_set_last_error("llvm build bit cast failed");
        return false;
    }
    if (!(func_ctx->mem_info.mem_cur_page_count_addr = LLVMBuildBitCast(
              comp_ctx->builder, func_ctx->mem_info.mem_cur_page_count_addr,
              INT32_PTR_TYPE, "mem_cur_page_ptr")))
    {
        wasm_jit_set_last_error("llvm build bit cast failed");
        return false;
    }
    if (!(func_ctx->mem_info.mem_data_size_addr = LLVMBuildBitCast(
              comp_ctx->builder, func_ctx->mem_info.mem_data_size_addr,
              INT32_PTR_TYPE, "mem_data_size_ptr")))
    {
        wasm_jit_set_last_error("llvm build bit cast failed");
        return false;
    }
    if (mem_space_unchanged)
    {
        if (!(func_ctx->mem_info.mem_base_addr = LLVMBuildLoad2(
                  comp_ctx->builder, OPQ_PTR_TYPE,
                  func_ctx->mem_info.mem_base_addr, "mem_base_addr")))
        {
            wasm_jit_set_last_error("llvm build load failed");
            return false;
        }
        if (!(func_ctx->mem_info.mem_cur_page_count_addr =
                  LLVMBuildLoad2(comp_ctx->builder, I32_TYPE,
                                 func_ctx->mem_info.mem_cur_page_count_addr,
                                 "mem_cur_page_count")))
        {
            wasm_jit_set_last_error("llvm build load failed");
            return false;
        }
        if (!(func_ctx->mem_info.mem_data_size_addr = LLVMBuildLoad2(
                  comp_ctx->builder, I32_TYPE,
                  func_ctx->mem_info.mem_data_size_addr, "mem_data_size")))
        {
            wasm_jit_set_last_error("llvm build load failed");
            return false;
        }
    }

    // 获取全局变量信息
    offset_of_global_data = offsetof(WASMModule, global_data);

    offset = I32_CONST(offset_of_global_data);
    if (!(func_ctx->global_info = LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE,
                                                        func_ctx->wasm_module, &offset, 1, "global_base_addr_offset")))
    {
        wasm_jit_set_last_error("llvm build in bounds gep failed");
        return false;
    }

    if (!(func_ctx->global_info = LLVMBuildBitCast(comp_ctx->builder, func_ctx->global_info, INT8_PTR_TYPE,
                                                   "global_base_addr_ptr")))
    {
        wasm_jit_set_last_error("llvm build in bounds gep failed");
        return false;
    }

    return true;
}

static bool
create_cur_exception(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    LLVMValueRef offset;

    offset = I32_CONST(offsetof(WASMModule, cur_exception));
    func_ctx->cur_exception =
        LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE, func_ctx->wasm_module,
                              &offset, 1, "cur_exception");
    if (!func_ctx->cur_exception)
    {
        wasm_jit_set_last_error("llvm build in bounds gep failed.");
        return false;
    }
    return true;
}

static bool
create_func_type_indexes(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    LLVMValueRef offset, func_type_indexes_ptr;
    LLVMTypeRef int32_ptr_type;

    offset = I32_CONST(offsetof(WASMModule, func_type_indexes));
    func_type_indexes_ptr =
        LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE, func_ctx->wasm_module,
                              &offset, 1, "func_type_indexes_ptr");

    if (!(int32_ptr_type = LLVMPointerType(INT32_PTR_TYPE, 0)))
    {
        wasm_jit_set_last_error("llvm get pointer type failed.");
        return false;
    }

    func_ctx->func_type_indexes =
        LLVMBuildBitCast(comp_ctx->builder, func_type_indexes_ptr,
                         int32_ptr_type, "func_type_indexes_tmp");

    func_ctx->func_type_indexes =
        LLVMBuildLoad2(comp_ctx->builder, INT32_PTR_TYPE,
                       func_ctx->func_type_indexes, "func_type_indexes");

    return true;
}

static bool
create_func_ptrs(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    LLVMValueRef offset;

    offset = I32_CONST(offsetof(WASMModule, func_ptrs));
    func_ctx->func_ptrs =
        LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE, func_ctx->wasm_module,
                              &offset, 1, "func_ptrs_offset");
    func_ctx->func_ptrs =
        LLVMBuildBitCast(comp_ctx->builder, func_ctx->func_ptrs,
                         INT8_PPTR_TYPE, "func_ptrs_tmp");

    func_ctx->func_ptrs = LLVMBuildLoad2(comp_ctx->builder, OPQ_PTR_TYPE,
                                         func_ctx->func_ptrs, "func_ptrs_ptr");

    func_ctx->func_ptrs =
        LLVMBuildBitCast(comp_ctx->builder, func_ctx->func_ptrs,
                         INT8_PPTR_TYPE, "func_ptrs");

    return true;
}

// 创建函数编译环境
static JITFuncContext *
wasm_jit_create_func_context(WASMModule *wasm_module, JITCompContext *comp_ctx,
                             WASMFunction *wasm_func, uint32 func_index)
{
    JITFuncContext *func_ctx;
    WASMType *func_type = wasm_func->func_type;
    JITBlock *wasm_jit_block;
    LLVMValueRef wasm_module_offset = I32_TWO, wasm_module_addr;
    uint64 size;

    size = offsetof(JITFuncContext, locals) + sizeof(LLVMValueRef) * ((uint64)func_type->param_count + wasm_func->local_count);
    if (size >= UINT32_MAX || !(func_ctx = wasm_runtime_malloc(size)))
    {
        wasm_jit_set_last_error("allocate memory failed.");
        return NULL;
    }

    memset(func_ctx, 0, (uint32)size);
    func_ctx->wasm_func = wasm_func;
    func_ctx->module = comp_ctx->module;

    size = wasm_func->max_block_num * sizeof(JITBlock);

    if (!(func_ctx->block_stack_bottom = func_ctx->block_stack = wasm_runtime_malloc(size)))
    {
        wasm_jit_set_last_error("allocate memory failed.");
        return NULL;
    }

    size = wasm_func->max_stack_num * sizeof(JITValue);

    if (!(func_ctx->value_stack_bottom = func_ctx->value_stack = wasm_runtime_malloc(size)))
    {
        wasm_jit_set_last_error("allocate memory failed.");
        return NULL;
    }

    if (!(func_ctx->func =
              wasm_jit_add_llvm_func(comp_ctx, func_ctx->module, func_type,
                                     func_index, &func_ctx->llvm_func_type)))
    {
        goto fail;
    }

    if (!wasm_jit_create_func_block(comp_ctx, func_ctx, wasm_func, func_type))
    {
        goto fail;
    }

    wasm_jit_block = func_ctx->block_stack - 1;

    LLVMPositionBuilderAtEnd(comp_ctx->builder, wasm_jit_block->llvm_entry_block);

    func_ctx->exec_env = LLVMGetParam(func_ctx->func, 0);

    if (!(wasm_module_addr = LLVMBuildInBoundsGEP2(
              comp_ctx->builder, OPQ_PTR_TYPE, func_ctx->exec_env,
              &wasm_module_offset, 1, "wasm_module_addr")))
    {
        wasm_jit_set_last_error("llvm build in bounds gep failed");
        goto fail;
    }

    if (!(func_ctx->wasm_module = LLVMBuildLoad2(comp_ctx->builder, OPQ_PTR_TYPE,
                                                 wasm_module_addr, "wasm_module")))
    {
        wasm_jit_set_last_error("llvm build load failed");
        goto fail;
    }

    /* Get argv buffer address */
    if (!create_argv_buf(comp_ctx, func_ctx))
    {
        goto fail;
    }

    /* Create local variables */
    if (!create_local_variables(comp_ctx, func_ctx, wasm_func))
    {
        goto fail;
    }

    /* Create base addr, end addr, data size of mem, heap */
    if (!create_memory_info(wasm_module, comp_ctx, func_ctx))
    {
        goto fail;
    }

    /* Load current exception */
    if (!create_cur_exception(comp_ctx, func_ctx))
    {
        goto fail;
    }

    /* Load function type indexes */
    if (!create_func_type_indexes(comp_ctx, func_ctx))
    {
        goto fail;
    }

    /* Load function pointers */
    if (!create_func_ptrs(comp_ctx, func_ctx))
    {
        goto fail;
    }

    return func_ctx;

fail:
    wasm_runtime_free(func_ctx);
    return NULL;
}

static void
wasm_jit_destroy_func_contexts(JITFuncContext **func_ctxes, uint32 count)
{
    uint32 i;

    for (i = 0; i < count; i++)
        if (func_ctxes[i])
        {
            wasm_runtime_free(func_ctxes[i]);
        }
    wasm_runtime_free(func_ctxes);
}

/**
 * Create function compiler contexts
 */
static JITFuncContext **
wasm_jit_create_func_contexts(WASMModule *wasm_module, JITCompContext *comp_ctx)
{
    JITFuncContext **func_ctxes;
    WASMFunction *func;
    uint32 define_function_count;
    uint64 size;
    uint32 i;

    define_function_count = comp_ctx->func_ctx_count;

    /* Allocate memory */
    size = sizeof(JITFuncContext *) * (uint64)define_function_count;
    if (size >= UINT32_MAX || !(func_ctxes = wasm_runtime_malloc(size)))
    {
        wasm_jit_set_last_error("allocate memory failed.");
        return NULL;
    }

    memset(func_ctxes, 0, size);

    func = wasm_module->functions + wasm_module->import_function_count;

    /* Create each function context */
    for (i = 0; i < define_function_count; i++, func++)
    {
        if (!(func_ctxes[i] =
                  wasm_jit_create_func_context(wasm_module, comp_ctx, func, i)))
        {
            wasm_jit_destroy_func_contexts(func_ctxes, define_function_count);
            return NULL;
        }
    }

    return func_ctxes;
}

static bool
wasm_jit_set_llvm_basic_types(JITLLVMTypes *basic_types, LLVMContextRef context)
{
    basic_types->int1_type = LLVMInt1TypeInContext(context);
    basic_types->int8_type = LLVMInt8TypeInContext(context);
    basic_types->int16_type = LLVMInt16TypeInContext(context);
    basic_types->int32_type = LLVMInt32TypeInContext(context);
    basic_types->int64_type = LLVMInt64TypeInContext(context);
    basic_types->float32_type = LLVMFloatTypeInContext(context);
    basic_types->float64_type = LLVMDoubleTypeInContext(context);
    basic_types->void_type = LLVMVoidTypeInContext(context);

    basic_types->meta_data_type = LLVMMetadataTypeInContext(context);

    basic_types->int8_ptr_type = LLVMPointerType(basic_types->int8_type, 0);

    if (basic_types->int8_ptr_type)
    {
        basic_types->int8_pptr_type =
            LLVMPointerType(basic_types->int8_ptr_type, 0);
    }

    basic_types->int16_ptr_type = LLVMPointerType(basic_types->int16_type, 0);
    basic_types->int32_ptr_type = LLVMPointerType(basic_types->int32_type, 0);
    basic_types->int64_ptr_type = LLVMPointerType(basic_types->int64_type, 0);
    basic_types->float32_ptr_type =
        LLVMPointerType(basic_types->float32_type, 0);
    basic_types->float64_ptr_type =
        LLVMPointerType(basic_types->float64_type, 0);

    basic_types->i8x16_vec_type = LLVMVectorType(basic_types->int8_type, 16);
    basic_types->i16x8_vec_type = LLVMVectorType(basic_types->int16_type, 8);
    basic_types->i32x4_vec_type = LLVMVectorType(basic_types->int32_type, 4);
    basic_types->i64x2_vec_type = LLVMVectorType(basic_types->int64_type, 2);
    basic_types->f32x4_vec_type = LLVMVectorType(basic_types->float32_type, 4);
    basic_types->f64x2_vec_type = LLVMVectorType(basic_types->float64_type, 2);

    basic_types->v128_type = basic_types->i64x2_vec_type;
    basic_types->v128_ptr_type = LLVMPointerType(basic_types->v128_type, 0);

    basic_types->i1x2_vec_type = LLVMVectorType(basic_types->int1_type, 2);

    basic_types->funcref_type = LLVMInt32TypeInContext(context);
    basic_types->externref_type = LLVMInt32TypeInContext(context);

    return (basic_types->int8_ptr_type && basic_types->int8_pptr_type && basic_types->int16_ptr_type && basic_types->int32_ptr_type && basic_types->int64_ptr_type && basic_types->float32_ptr_type && basic_types->float64_ptr_type && basic_types->i8x16_vec_type && basic_types->i16x8_vec_type && basic_types->i32x4_vec_type && basic_types->i64x2_vec_type && basic_types->f32x4_vec_type && basic_types->f64x2_vec_type && basic_types->i1x2_vec_type && basic_types->meta_data_type && basic_types->funcref_type && basic_types->externref_type)
               ? true
               : false;
}

static bool
wasm_jit_create_llvm_consts(JITLLVMConsts *consts, JITCompContext *comp_ctx)
{
#define CREATE_I1_CONST(name, value)                                       \
    if (!(consts->i1_##name =                                              \
              LLVMConstInt(comp_ctx->basic_types.int1_type, value, true))) \
        return false;

    CREATE_I1_CONST(zero, 0)
    CREATE_I1_CONST(one, 1)
#undef CREATE_I1_CONST

    if (!(consts->i8_zero = I8_CONST(0)))
        return false;

    if (!(consts->f32_zero = F32_CONST(0)))
        return false;

    if (!(consts->f64_zero = F64_CONST(0)))
        return false;

#define CREATE_I32_CONST(name, value)                                \
    if (!(consts->i32_##name = LLVMConstInt(I32_TYPE, value, true))) \
        return false;

    CREATE_I32_CONST(min, (uint32)INT32_MIN)
    CREATE_I32_CONST(neg_one, (uint32)-1)
    CREATE_I32_CONST(zero, 0)
    CREATE_I32_CONST(one, 1)
    CREATE_I32_CONST(two, 2)
    CREATE_I32_CONST(three, 3)
    CREATE_I32_CONST(four, 4)
    CREATE_I32_CONST(five, 5)
    CREATE_I32_CONST(six, 6)
    CREATE_I32_CONST(seven, 7)
    CREATE_I32_CONST(eight, 8)
    CREATE_I32_CONST(nine, 9)
    CREATE_I32_CONST(ten, 10)
    CREATE_I32_CONST(eleven, 11)
    CREATE_I32_CONST(twelve, 12)
    CREATE_I32_CONST(thirteen, 13)
    CREATE_I32_CONST(fourteen, 14)
    CREATE_I32_CONST(fifteen, 15)
    CREATE_I32_CONST(31, 31)
    CREATE_I32_CONST(32, 32)
#undef CREATE_I32_CONST

#define CREATE_I64_CONST(name, value)                                \
    if (!(consts->i64_##name = LLVMConstInt(I64_TYPE, value, true))) \
        return false;

    CREATE_I64_CONST(min, (uint64)INT64_MIN)
    CREATE_I64_CONST(neg_one, (uint64)-1)
    CREATE_I64_CONST(zero, 0)
    CREATE_I64_CONST(63, 63)
    CREATE_I64_CONST(64, 64)
#undef CREATE_I64_CONST

#define CREATE_V128_CONST(name, type)                     \
    if (!(consts->name##_vec_zero = LLVMConstNull(type))) \
        return false;                                     \
    if (!(consts->name##_undef = LLVMGetUndef(type)))     \
        return false;

    CREATE_V128_CONST(i8x16, V128_i8x16_TYPE)
    CREATE_V128_CONST(i16x8, V128_i16x8_TYPE)
    CREATE_V128_CONST(i32x4, V128_i32x4_TYPE)
    CREATE_V128_CONST(i64x2, V128_i64x2_TYPE)
    CREATE_V128_CONST(f32x4, V128_f32x4_TYPE)
    CREATE_V128_CONST(f64x2, V128_f64x2_TYPE)
#undef CREATE_V128_CONST

#define CREATE_VEC_ZERO_MASK(slot)                                       \
    {                                                                    \
        LLVMTypeRef type = LLVMVectorType(I32_TYPE, slot);               \
        if (!type || !(consts->i32x##slot##_zero = LLVMConstNull(type))) \
            return false;                                                \
    }

    CREATE_VEC_ZERO_MASK(16)
    CREATE_VEC_ZERO_MASK(8)
    CREATE_VEC_ZERO_MASK(4)
    CREATE_VEC_ZERO_MASK(2)
#undef CREATE_VEC_ZERO_MASK

    return true;
}

static void
get_target_arch_from_triple(const char *triple, char *arch_buf, uint32 buf_size)
{
    uint32 i = 0;
    while (*triple != '-' && *triple != '\0' && i < buf_size - 1)
        arch_buf[i++] = *triple++;
    /* Make sure buffer is long enough */
}

void wasm_jit_handle_llvm_errmsg(const char *string, LLVMErrorRef err)
{
    char *err_msg = LLVMGetErrorMessage(err);
    wasm_jit_set_last_error_v("%s: %s", string, err_msg);
    LLVMDisposeErrorMessage(err_msg);
}

static bool
create_target_machine_detect_host(JITCompContext *comp_ctx)
{
    char *triple = NULL;
    LLVMTargetRef target = NULL;
    char *err_msg = NULL;
    char *cpu = NULL;
    char *features = NULL;
    LLVMTargetMachineRef target_machine = NULL;
    bool ret = false;

    triple = LLVMGetDefaultTargetTriple();
    if (triple == NULL)
    {
        wasm_jit_set_last_error("failed to get default target triple.");
        goto fail;
    }

    if (LLVMGetTargetFromTriple(triple, &target, &err_msg) != 0)
    {
        wasm_jit_set_last_error_v("failed to get llvm target from triple %s.",
                                  err_msg);
        LLVMDisposeMessage(err_msg);
        goto fail;
    }

    if (!LLVMTargetHasJIT(target))
    {
        wasm_jit_set_last_error("unspported JIT on this platform.");
        goto fail;
    }

    cpu = LLVMGetHostCPUName();
    if (cpu == NULL)
    {
        wasm_jit_set_last_error("failed to get host cpu information.");
        goto fail;
    }

    features = LLVMGetHostCPUFeatures();
    if (features == NULL)
    {
        wasm_jit_set_last_error("failed to get host cpu features.");
        goto fail;
    }

    LOG_VERBOSE("LLVM ORCJIT detected CPU \"%s\", with features \"%s\"\n", cpu,
                features);

    /* create TargetMachine */
    target_machine = LLVMCreateTargetMachine(
        target, triple, cpu, features, LLVMCodeGenLevelDefault,
        LLVMRelocDefault, LLVMCodeModelJITDefault);
    if (!target_machine)
    {
        wasm_jit_set_last_error("failed to create target machine.");
        goto fail;
    }
    comp_ctx->target_machine = target_machine;

    /* Save target arch */
    get_target_arch_from_triple(triple, comp_ctx->target_arch,
                                sizeof(comp_ctx->target_arch));
    ret = true;

fail:
    if (triple)
        LLVMDisposeMessage(triple);
    if (features)
        LLVMDisposeMessage(features);
    if (cpu)
        LLVMDisposeMessage(cpu);

    return ret;
}

static bool
orc_jit_create(JITCompContext *comp_ctx)
{
    LLVMErrorRef err;
    LLVMOrcLLLazyJITRef orc_jit = NULL;
    LLVMOrcLLLazyJITBuilderRef builder = NULL;
    LLVMOrcJITTargetMachineBuilderRef jtmb = NULL;
    bool ret = false;

    builder = LLVMOrcCreateLLLazyJITBuilder();
    if (builder == NULL)
    {
        wasm_jit_set_last_error("failed to create jit builder.");
        goto fail;
    }

    err = LLVMOrcJITTargetMachineBuilderDetectHost(&jtmb);
    if (err != LLVMErrorSuccess)
    {
        wasm_jit_handle_llvm_errmsg(
            "quited to create LLVMOrcJITTargetMachineBuilderRef", err);
        goto fail;
    }

    LLVMOrcLLLazyJITBuilderSetNumCompileThreads(
        builder, WASM_ORC_JIT_COMPILE_THREAD_NUM);

    /* Ownership transfer:
       LLVMOrcJITTargetMachineBuilderRef -> LLVMOrcLLJITBuilderRef */
    LLVMOrcLLLazyJITBuilderSetJITTargetMachineBuilder(builder, jtmb);
    err = LLVMOrcCreateLLLazyJIT(&orc_jit, builder);
    if (err != LLVMErrorSuccess)
    {
        wasm_jit_handle_llvm_errmsg("quited to create llvm lazy orcjit instance",
                                    err);
        goto fail;
    }
    /* Ownership transfer: LLVMOrcLLJITBuilderRef -> LLVMOrcLLJITRef */
    builder = NULL;

    /* Ownership transfer: local -> JITCompContext */
    comp_ctx->orc_jit = orc_jit;
    orc_jit = NULL;
    ret = true;

fail:
    if (builder)
        LLVMOrcDisposeLLLazyJITBuilder(builder);

    if (orc_jit)
        LLVMOrcDisposeLLLazyJIT(orc_jit);
    return ret;
}

bool wasm_jit_compiler_init(void)
{
    /* Initialize LLVM environment */

    LLVMInitializeCore(LLVMGetGlobalPassRegistry());
    /* Init environment of native for JIT compiler */
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();

    return true;
}

void wasm_jit_compiler_destroy(void)
{
    LLVMShutdown();
}

JITCompContext *
wasm_jit_create_comp_context(WASMModule *wasm_module, wasm_jit_comp_option_t option)
{
    JITCompContext *comp_ctx, *ret = NULL;
    char *fp_round = "round.tonearest",
         *fp_exce = "fpexcept.strict";
    uint32 i;
    LLVMTargetDataRef target_data_ref;

    /* Allocate memory */
    if (!(comp_ctx = wasm_runtime_malloc(sizeof(JITCompContext))))
    {
        wasm_jit_set_last_error("allocate memory failed.");
        return NULL;
    }

    memset(comp_ctx, 0, sizeof(JITCompContext));

    LLVMInitializeNativeTarget();
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();

    /* Create LLVM context, module and builder */
    comp_ctx->orc_thread_safe_context = LLVMOrcCreateNewThreadSafeContext();
    if (!comp_ctx->orc_thread_safe_context)
    {
        wasm_jit_set_last_error("create LLVM ThreadSafeContext failed.");
        goto fail;
    }

    /* Get a reference to the underlying LLVMContext, note:
         different from non LAZY JIT mode, no need to dispose this context,
         if will be disposed when the thread safe context is disposed */
    if (!(comp_ctx->context = LLVMOrcThreadSafeContextGetContext(
              comp_ctx->orc_thread_safe_context)))
    {
        wasm_jit_set_last_error("get context from LLVM ThreadSafeContext failed.");
        goto fail;
    }

    if (!(comp_ctx->builder = LLVMCreateBuilderInContext(comp_ctx->context)))
    {
        wasm_jit_set_last_error("create LLVM builder failed.");
        goto fail;
    }

    /* Create LLVM module for each jit function, note:
       different from non ORC JIT mode, no need to dispose it,
       it will be disposed when the thread safe context is disposed */
    if (!(comp_ctx->module = LLVMModuleCreateWithNameInContext(
              "WASM Module", comp_ctx->context)))
    {
        wasm_jit_set_last_error("create LLVM module failed.");
        goto fail;
    }

    comp_ctx->enable_bulk_memory = true;

    if (option->enable_tail_call)
        comp_ctx->enable_tail_call = true;

    if (option->enable_ref_types)
        comp_ctx->enable_ref_types = true;

    if (option->disable_llvm_intrinsics)
        comp_ctx->disable_llvm_intrinsics = true;

    if (option->disable_llvm_lto)
        comp_ctx->disable_llvm_lto = true;

    if (option->enable_stack_estimation)
        comp_ctx->enable_stack_estimation = true;

    comp_ctx->opt_level = option->opt_level;
    comp_ctx->size_level = option->size_level;

    comp_ctx->custom_sections_wp = option->custom_sections;
    comp_ctx->custom_sections_count = option->custom_sections_count;

    /* Create TargetMachine */
    if (!create_target_machine_detect_host(comp_ctx))
        goto fail;

    /* Create LLJIT Instance */
    if (!orc_jit_create(comp_ctx))
        goto fail;

#ifndef OS_ENABLE_HW_BOUND_CHECK
    comp_ctx->enable_bound_check = true;
    /* Always enable stack boundary check if `bounds-checks`
       is enabled */
#else
    comp_ctx->enable_bound_check = false;
    /* When `bounds-checks` is disabled, we set stack boundary
       check status according to the compilation option */
#if WASM_DISABLE_STACK_HW_BOUND_CHECK != 0
    /* Native stack overflow check with hardware trap is disabled,
       we need to enable the check by LLVM JITed/AOTed code */
    comp_ctx->enable_stack_bound_check = true;
#else
    /* Native stack overflow check with hardware trap is enabled,
       no need to enable the check by LLVM JITed/AOTed code */
    comp_ctx->enable_stack_bound_check = false;
#endif
#endif

    if (option->enable_simd)
    {
        char *tmp;
        bool check_simd_ret;

        comp_ctx->enable_simd = true;

        if (!(tmp = LLVMGetTargetMachineCPU(comp_ctx->target_machine)))
        {
            wasm_jit_set_last_error("get CPU from Target Machine fail");
            goto fail;
        }

        check_simd_ret =
            wasm_jit_check_simd_compatibility(comp_ctx->target_arch, tmp);
        LLVMDisposeMessage(tmp);
        if (!check_simd_ret)
        {
            wasm_jit_set_last_error("SIMD compatibility check failed, "
                                    "try adding --cpu=<cpu> to specify a cpu "
                                    "or adding --disable-simd to disable SIMD");
            goto fail;
        }
    }

    if (!(target_data_ref =
              LLVMCreateTargetDataLayout(comp_ctx->target_machine)))
    {
        wasm_jit_set_last_error("create LLVM target data layout failed.");
        goto fail;
    }
    comp_ctx->pointer_size = LLVMPointerSize(target_data_ref);
    LLVMDisposeTargetData(target_data_ref);

    comp_ctx->optimize = true;

    /* Create metadata for llvm float experimental constrained intrinsics */
    if (!(comp_ctx->fp_rounding_mode = LLVMMDStringInContext(
              comp_ctx->context, fp_round, (uint32)strlen(fp_round))) ||
        !(comp_ctx->fp_exception_behavior = LLVMMDStringInContext(
              comp_ctx->context, fp_exce, (uint32)strlen(fp_exce))))
    {
        wasm_jit_set_last_error("create float llvm metadata failed.");
        goto fail;
    }

    if (!wasm_jit_set_llvm_basic_types(&comp_ctx->basic_types, comp_ctx->context))
    {
        wasm_jit_set_last_error("create LLVM basic types failed.");
        goto fail;
    }

    if (!wasm_jit_create_llvm_consts(&comp_ctx->llvm_consts, comp_ctx))
    {
        wasm_jit_set_last_error("create LLVM const values failed.");
        goto fail;
    }

    /* set exec_env data type to int8** */
    comp_ctx->exec_env_type = comp_ctx->basic_types.int8_pptr_type;

    /* set wasm_jit_inst data type to int8* */
    comp_ctx->wasm_module_type = INT8_PTR_TYPE;

    /* Create function context for each function */
    comp_ctx->func_ctx_count = wasm_module->function_count - wasm_module->import_function_count;
    if (comp_ctx->func_ctx_count > 0 && !(comp_ctx->func_ctxes =
                                              wasm_jit_create_func_contexts(wasm_module, comp_ctx)))
        goto fail;

    ret = comp_ctx;

fail:

    if (!ret)
        wasm_jit_destroy_comp_context(comp_ctx);

    (void)i;
    return ret;
}

void wasm_jit_destroy_comp_context(JITCompContext *comp_ctx)
{
    if (!comp_ctx)
        return;

    if (comp_ctx->target_machine)
        LLVMDisposeTargetMachine(comp_ctx->target_machine);

    if (comp_ctx->builder)
        LLVMDisposeBuilder(comp_ctx->builder);

    if (comp_ctx->orc_thread_safe_context)
        LLVMOrcDisposeThreadSafeContext(comp_ctx->orc_thread_safe_context);

    /* Note: don't dispose comp_ctx->context and comp_ctx->module as
       they are disposed when disposing the thread safe context */

    /* Has to be the last one */
    if (comp_ctx->orc_jit)
        LLVMOrcDisposeLLLazyJIT(comp_ctx->orc_jit);

    if (comp_ctx->func_ctxes)
        wasm_jit_destroy_func_contexts(comp_ctx->func_ctxes,
                                       comp_ctx->func_ctx_count);

    if (comp_ctx->target_cpu)
    {
        wasm_runtime_free(comp_ctx->target_cpu);
    }

    wasm_runtime_free(comp_ctx);
}

void wasm_jit_block_destroy(JITBlock *block)
{
    if (block->param_phis)
        wasm_runtime_free(block->param_phis);
    if (block->else_param_phis)
        wasm_runtime_free(block->else_param_phis);
    if (block->result_phis)
        wasm_runtime_free(block->result_phis);
}

bool wasm_jit_build_zero_function_ret(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                      WASMType *func_type)
{
    LLVMValueRef ret = NULL;

    if (func_type->result_count)
    {
        switch (func_type->result[0])
        {
        case VALUE_TYPE_I32:
            ret = LLVMBuildRet(comp_ctx->builder, I32_ZERO);
            break;
        case VALUE_TYPE_I64:
            ret = LLVMBuildRet(comp_ctx->builder, I64_ZERO);
            break;
        case VALUE_TYPE_F32:
            ret = LLVMBuildRet(comp_ctx->builder, F32_ZERO);
            break;
        case VALUE_TYPE_F64:
            ret = LLVMBuildRet(comp_ctx->builder, F64_ZERO);
            break;
        case VALUE_TYPE_V128:
            ret =
                LLVMBuildRet(comp_ctx->builder, LLVM_CONST(i64x2_vec_zero));
            break;
        case VALUE_TYPE_FUNCREF:
        case VALUE_TYPE_EXTERNREF:
            ret = LLVMBuildRet(comp_ctx->builder, REF_NULL);
            break;
        default:;
        }
    }
    else
    {
        ret = LLVMBuildRetVoid(comp_ctx->builder);
    }

    if (!ret)
    {
        wasm_jit_set_last_error("llvm build ret failed.");
        return false;
    }
    return true;
}

static LLVMValueRef
__call_llvm_intrinsic(const JITCompContext *comp_ctx,
                      const JITFuncContext *func_ctx, const char *name,
                      LLVMTypeRef ret_type, LLVMTypeRef *param_types,
                      int param_count, LLVMValueRef *param_values)
{
    LLVMValueRef func, ret;
    LLVMTypeRef func_type;

    /* Declare llvm intrinsic function if necessary */
    if (!(func = LLVMGetNamedFunction(func_ctx->module, name)))
    {
        if (!(func_type = LLVMFunctionType(ret_type, param_types,
                                           (uint32)param_count, false)))
        {
            wasm_jit_set_last_error(
                "create LLVM intrinsic function type failed.");
            return NULL;
        }

        if (!(func = LLVMAddFunction(func_ctx->module, name, func_type)))
        {
            wasm_jit_set_last_error("add LLVM intrinsic function failed.");
            return NULL;
        }
    }

#if LLVM_VERSION_MAJOR >= 14
    func_type =
        LLVMFunctionType(ret_type, param_types, (uint32)param_count, false);
#endif

    /* Call the LLVM intrinsic function */
    if (!(ret = LLVMBuildCall2(comp_ctx->builder, func_type, func, param_values,
                               (uint32)param_count, "call")))
    {
        wasm_jit_set_last_error("llvm build intrinsic call failed.");
        return NULL;
    }

    return ret;
}

LLVMValueRef
wasm_jit_call_llvm_intrinsic(const JITCompContext *comp_ctx,
                             const JITFuncContext *func_ctx, const char *intrinsic,
                             LLVMTypeRef ret_type, LLVMTypeRef *param_types,
                             int param_count, ...)
{
    LLVMValueRef *param_values, ret;
    va_list argptr;
    uint64 total_size;
    int i = 0;

    /* Create param values */
    total_size = sizeof(LLVMValueRef) * (uint64)param_count;
    if (total_size >= UINT32_MAX || !(param_values = wasm_runtime_malloc((uint32)total_size)))
    {
        wasm_jit_set_last_error("allocate memory for param values failed.");
        return false;
    }

    /* Load each param value */
    va_start(argptr, param_count);
    while (i < param_count)
        param_values[i++] = va_arg(argptr, LLVMValueRef);
    va_end(argptr);

    ret = __call_llvm_intrinsic(comp_ctx, func_ctx, intrinsic, ret_type,
                                param_types, param_count, param_values);

    wasm_runtime_free(param_values);

    return ret;
}

LLVMValueRef
wasm_jit_call_llvm_intrinsic_v(const JITCompContext *comp_ctx,
                               const JITFuncContext *func_ctx, const char *intrinsic,
                               LLVMTypeRef ret_type, LLVMTypeRef *param_types,
                               int param_count, va_list param_value_list)
{
    LLVMValueRef *param_values, ret;
    uint64 total_size;
    int i = 0;

    /* Create param values */
    total_size = sizeof(LLVMValueRef) * (uint64)param_count;
    if (total_size >= UINT32_MAX || !(param_values = wasm_runtime_malloc((uint32)total_size)))
    {
        wasm_jit_set_last_error("allocate memory for param values failed.");
        return false;
    }

    /* Load each param value */
    while (i < param_count)
        param_values[i++] = va_arg(param_value_list, LLVMValueRef);

    ret = __call_llvm_intrinsic(comp_ctx, func_ctx, intrinsic, ret_type,
                                param_types, param_count, param_values);

    wasm_runtime_free(param_values);

    return ret;
}

LLVMValueRef
wasm_jit_get_func_from_table(const JITCompContext *comp_ctx, LLVMValueRef base,
                             LLVMTypeRef func_type, int32 index)
{
    LLVMValueRef func;
    LLVMValueRef func_addr;

    if (!(func_addr = I32_CONST(index)))
    {
        wasm_jit_set_last_error("construct function index failed.");
        goto fail;
    }

    if (!(func_addr =
              LLVMBuildInBoundsGEP2(comp_ctx->builder, OPQ_PTR_TYPE, base,
                                    &func_addr, 1, "func_addr")))
    {
        wasm_jit_set_last_error("get function addr by index failed.");
        goto fail;
    }

    func =
        LLVMBuildLoad2(comp_ctx->builder, OPQ_PTR_TYPE, func_addr, "func_tmp");

    if (func == NULL)
    {
        wasm_jit_set_last_error("get function pointer failed.");
        goto fail;
    }

    if (!(func =
              LLVMBuildBitCast(comp_ctx->builder, func, func_type, "func")))
    {
        wasm_jit_set_last_error("cast function fialed.");
        goto fail;
    }

    return func;
fail:
    return NULL;
}
