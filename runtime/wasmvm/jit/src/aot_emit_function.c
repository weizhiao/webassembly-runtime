#include "wasm_jit_emit_function.h"
#include "wasm_jit_emit_exception.h"
#include "wasm_jit_emit_control.h"
#include "wasm_jit_emit_table.h"
#include "wasm_exec_env.h"
#include "wasm_exception.h"
#include "wasm_native.h"

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

bool wasm_check_app_addr_and_convert(WASMModule *module_inst, bool is_str,
                                     uint32 app_buf_addr, uint32 app_buf_size,
                                     void **p_native_addr)
{
    WASMMemory *memory_inst = module_inst->memories;
    uint8 *native_addr;

    if (!memory_inst)
    {
        goto fail;
    }

    native_addr = memory_inst->memory_data + app_buf_addr;

    /* No need to check the app_offset and buf_size if memory access
       boundary check with hardware trap is enabled */
#ifndef OS_ENABLE_HW_BOUND_CHECK
    if (app_buf_addr >= memory_inst->memory_data_size)
    {
        goto fail;
    }

    if (!is_str)
    {
        if (app_buf_size > memory_inst->memory_data_size - app_buf_addr)
        {
            goto fail;
        }
    }
    else
    {
        const char *str, *str_end;

        /* The whole string must be in the linear memory */
        str = (const char *)native_addr;
        str_end = (const char *)memory_inst->memory_data_end;
        while (str < str_end && *str != '\0')
            str++;
        if (str == str_end)
            goto fail;
    }
#endif

    *p_native_addr = (void *)native_addr;
    return true;
fail:
    wasm_set_exception(module_inst, "out of bounds memory access");
    return false;
}

bool jit_check_app_addr_and_convert(WASMModule *module_inst, bool is_str,
                                    uint32 app_buf_addr, uint32 app_buf_size,
                                    void **p_native_addr)
{
    bool ret = wasm_check_app_addr_and_convert(
        module_inst, is_str, app_buf_addr, app_buf_size, p_native_addr);

    return ret;
}

static bool
create_func_return_block(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    LLVMBasicBlockRef block_curr = LLVMGetInsertBlock(comp_ctx->builder);
    WASMType *wasm_jit_func_type = func_ctx->wasm_func->func_type;

    /* Create function return block if it isn't created */
    if (!func_ctx->func_return_block)
    {
        if (!(func_ctx->func_return_block = LLVMAppendBasicBlockInContext(
                  comp_ctx->context, func_ctx->func, "func_ret")))
        {
            wasm_jit_set_last_error("llvm add basic block failed.");
            return false;
        }

        /* Create return IR */
        LLVMPositionBuilderAtEnd(comp_ctx->builder,
                                 func_ctx->func_return_block);
        if (!comp_ctx->enable_bound_check)
        {
            if (!wasm_jit_emit_exception(comp_ctx, func_ctx, EXCE_ALREADY_THROWN,
                                         false, NULL, NULL))
            {
                return false;
            }
        }
        else if (!wasm_jit_build_zero_function_ret(comp_ctx, func_ctx,
                                                   wasm_jit_func_type))
        {
            return false;
        }
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, block_curr);
    return true;
}

/* Check whether there was exception thrown, if yes, return directly */
static bool
check_call_return(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                  LLVMValueRef res)
{
    LLVMBasicBlockRef block_curr, check_call_succ;
    LLVMValueRef cmp;

    /* Create function return block if it isn't created */
    if (!create_func_return_block(comp_ctx, func_ctx))
        return false;

    if (!(cmp = LLVMBuildICmp(comp_ctx->builder, LLVMIntNE, res, I8_ZERO,
                              "cmp")))
    {
        wasm_jit_set_last_error("llvm build icmp failed.");
        return false;
    }

    /* Add check exection success block */
    if (!(check_call_succ = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func_ctx->func, "check_call_succ")))
    {
        wasm_jit_set_last_error("llvm add basic block failed.");
        return false;
    }

    block_curr = LLVMGetInsertBlock(comp_ctx->builder);
    LLVMMoveBasicBlockAfter(check_call_succ, block_curr);

    LLVMPositionBuilderAtEnd(comp_ctx->builder, block_curr);
    /* Create condition br */
    if (!LLVMBuildCondBr(comp_ctx->builder, cmp, check_call_succ,
                         func_ctx->func_return_block))
    {
        wasm_jit_set_last_error("llvm build cond br failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, check_call_succ);
    return true;
}

/**
 * Check whether the app address and its buffer are inside the linear memory,
 * if no, throw exception
 */
static bool
check_app_addr_and_convert(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                           bool is_str_arg, LLVMValueRef app_addr,
                           LLVMValueRef buf_size,
                           LLVMValueRef *p_native_addr_converted)
{
    LLVMTypeRef func_type, func_ptr_type, func_param_types[5];
    LLVMValueRef func, func_param_values[5], res, native_addr_ptr;

    /* prepare function type of wasm_jit_check_app_addr_and_convert */
    func_param_types[0] = comp_ctx->wasm_module_type;           /* module_inst */
    func_param_types[1] = INT8_TYPE;                            /* is_str_arg */
    func_param_types[2] = I32_TYPE;                             /* app_offset */
    func_param_types[3] = I32_TYPE;                             /* buf_size */
    func_param_types[4] = comp_ctx->basic_types.int8_pptr_type; /* p_native_addr */
    if (!(func_type =
              LLVMFunctionType(INT8_TYPE, func_param_types, 5, false)))
    {
        wasm_jit_set_last_error("llvm add function type failed.");
        return false;
    }

    /* prepare function pointer */

    if (!(func_ptr_type = LLVMPointerType(func_type, 0)))
    {
        wasm_jit_set_last_error("create LLVM function type failed.");
        return false;
    }

    /* JIT mode, call the function directly */
    if (!(func =
              I64_CONST((uint64)(uintptr_t)jit_check_app_addr_and_convert)) ||
        !(func = LLVMConstIntToPtr(func, func_ptr_type)))
    {
        wasm_jit_set_last_error("create LLVM value failed.");
        return false;
    }

    if (!(native_addr_ptr = LLVMBuildBitCast(
              comp_ctx->builder, func_ctx->argv_buf,
              comp_ctx->basic_types.int8_pptr_type, "p_native_addr")))
    {
        wasm_jit_set_last_error("llvm build bit cast failed.");
        return false;
    }

    func_param_values[0] = func_ctx->wasm_module;
    func_param_values[1] = I8_CONST(is_str_arg);
    func_param_values[2] = app_addr;
    func_param_values[3] = buf_size;
    func_param_values[4] = native_addr_ptr;

    /* call wasm_jit_check_app_addr_and_convert() function */
    if (!(res = LLVMBuildCall2(comp_ctx->builder, func_type, func,
                               func_param_values, 5, "res")))
    {
        wasm_jit_set_last_error("llvm build call failed.");
        return false;
    }

    /* Check whether exception was thrown when executing the function */
    if (comp_ctx->enable_bound_check && !check_call_return(comp_ctx, func_ctx, res))
    {
        return false;
    }

    if (!(*p_native_addr_converted =
              LLVMBuildLoad2(comp_ctx->builder, OPQ_PTR_TYPE, native_addr_ptr,
                             "native_addr")))
    {
        wasm_jit_set_last_error("llvm build load failed.");
        return false;
    }

    return true;
}

/* Check whether there was exception thrown, if yes, return directly */
static bool
check_exception_thrown(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    LLVMBasicBlockRef block_curr, check_exce_succ;
    LLVMValueRef value, cmp;

    /* Create function return block if it isn't created */
    if (!create_func_return_block(comp_ctx, func_ctx))
        return false;

    /* Load the first byte of wasm_jit_module_inst->cur_exception, and check
       whether it is '\0'. If yes, no exception was thrown. */
    if (!(value = LLVMBuildLoad2(comp_ctx->builder, INT8_TYPE,
                                 func_ctx->cur_exception, "exce_value")) ||
        !(cmp = LLVMBuildICmp(comp_ctx->builder, LLVMIntEQ, value, I8_ZERO,
                              "cmp")))
    {
        wasm_jit_set_last_error("llvm build icmp failed.");
        return false;
    }

    /* Add check exection success block */
    if (!(check_exce_succ = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func_ctx->func, "check_exce_succ")))
    {
        wasm_jit_set_last_error("llvm add basic block failed.");
        return false;
    }

    block_curr = LLVMGetInsertBlock(comp_ctx->builder);
    LLVMMoveBasicBlockAfter(check_exce_succ, block_curr);

    LLVMPositionBuilderAtEnd(comp_ctx->builder, block_curr);
    /* Create condition br */
    if (!LLVMBuildCondBr(comp_ctx->builder, cmp, check_exce_succ,
                         func_ctx->func_return_block))
    {
        wasm_jit_set_last_error("llvm build cond br failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, check_exce_succ);
    return true;
}

static bool
wasm_jit_call_define_function(JITCompContext *comp_ctx, JITFuncContext *func_ctx, WASMFunction *wasm_func, LLVMValueRef *param_values,
                              uint32 func_idx, uint8 *ext_ret_types)
{
    WASMType *func_type = wasm_func->func_type;
    LLVMValueRef func, value_ret, ext_ret;
    LLVMTypeRef llvm_func_type;
    uint32 i;
    uint32 param_count = func_type->param_count;
    uint32 result_count = func_type->result_count;
    uint32 ext_ret_count = result_count > 1 ? result_count - 1 : 0;
    // 这里的func_idx已经减去了导入的函数个数
    JITFuncContext *call = comp_ctx->func_ctxes[func_idx];
    char buf[32];

    llvm_func_type = call->llvm_func_type;
    func = call->func;

    /* Call the function */
    if (!(value_ret = LLVMBuildCall2(
              comp_ctx->builder, llvm_func_type, func, param_values,
              (uint32)param_count + 1 + ext_ret_count,
              (func_type->result_count > 0 ? "call" : ""))))
    {
        wasm_jit_set_last_error("LLVM build call failed.");
        return false;
    }

    /* Check whether there was exception thrown when executing
           the function */
    if (!check_exception_thrown(comp_ctx, func_ctx))
    {
        return false;
    }

    if (func_type->result_count > 0)
    {
        /* Push the first result to stack */
        PUSH(value_ret);
        /* Load extra result from its address and push to stack */
        for (i = 0; i < ext_ret_count; i++)
        {
            snprintf(buf, sizeof(buf), "func%d_ext_ret%d", func_idx, i);
            if (!(ext_ret = LLVMBuildLoad2(
                      comp_ctx->builder, TO_LLVM_TYPE(ext_ret_types[i]),
                      param_values[1 + param_count + i], buf)))
            {
                wasm_jit_set_last_error("llvm build load failed.");
                return false;
            }
            PUSH(ext_ret);
        }
    }
    return true;
}

bool wasm_jit_call_import_function(JITCompContext *comp_ctx, JITFuncContext *func_ctx, WASMFunction *wasm_func, LLVMValueRef *param_values,
                                   uint32 func_idx, uint8 *ext_ret_types)
{
    WASMType *func_type = wasm_func->func_type;
    const char *signature = wasm_func->signature;
    LLVMTypeRef native_func_type, func_ptr_type;
    LLVMValueRef func_ptr, func, value_ret, ext_ret, llvm_func_idx;
    uint64 total_size;
    LLVMTypeRef *param_types;
    uint8 wasm_ret_type;
    LLVMTypeRef ret_type;
    uint32 i, j;
    uint32 param_count = func_type->param_count;
    uint32 result_count = func_type->result_count;
    uint32 ext_ret_count = result_count > 1 ? result_count - 1 : 0;
    char buf[32];

    llvm_func_idx = I32_CONST(func_idx);

    /* Initialize parameter types of the LLVM function */
    total_size = sizeof(LLVMTypeRef) * (uint64)(param_count + 1);
    if (total_size >= UINT32_MAX || !(param_types = wasm_runtime_malloc((uint32)total_size)))
    {
        wasm_jit_set_last_error("allocate memory failed.");
        return false;
    }

    j = 0;
    param_types[j++] = comp_ctx->exec_env_type;

    for (i = 0; i < param_count; i++, j++)
    {
        param_types[j] = TO_LLVM_TYPE(func_type->param[i]);
        if (signature)
        {
            LLVMValueRef native_addr, native_addr_size;
            if (signature[i + 1] == '*' || signature[i + 1] == '$')
            {
                param_types[j] = INT8_PTR_TYPE;
            }
            if (signature[i + 1] == '*')
            {
                if (signature[i + 2] == '~')
                    native_addr_size = param_values[i + 2];
                else
                    native_addr_size = I32_ONE;
                if (!check_app_addr_and_convert(
                        comp_ctx, func_ctx, false, param_values[j],
                        native_addr_size, &native_addr))
                {
                    return false;
                }
                param_values[j] = native_addr;
            }
            else if (signature[i + 1] == '$')
            {
                native_addr_size = I32_ZERO;
                if (!check_app_addr_and_convert(
                        comp_ctx, func_ctx, true, param_values[j],
                        native_addr_size, &native_addr))
                {
                    return false;
                }
                param_values[j] = native_addr;
            }
        }
    }

    if (func_type->result_count)
    {
        wasm_ret_type = func_type->result[0];
        ret_type = TO_LLVM_TYPE(wasm_ret_type);
    }
    else
    {
        wasm_ret_type = VALUE_TYPE_VOID;
        ret_type = VOID_TYPE;
    }

    /* call native func directly */

    if (!(native_func_type = LLVMFunctionType(
              ret_type, param_types, param_count + 1, false)))
    {
        wasm_jit_set_last_error("llvm add function type failed.");
        return false;
    }

    if (!(func_ptr_type = LLVMPointerType(native_func_type, 0)))
    {
        wasm_jit_set_last_error("create LLVM function type failed.");
        return false;
    }

    if (!(func_ptr = LLVMBuildInBoundsGEP2(
              comp_ctx->builder, OPQ_PTR_TYPE, func_ctx->func_ptrs,
              &llvm_func_idx, 1, "native_func_ptr_tmp")))
    {
        wasm_jit_set_last_error("llvm build inbounds gep failed.");
        return false;
    }

    if (!(func_ptr = LLVMBuildLoad2(comp_ctx->builder, OPQ_PTR_TYPE,
                                    func_ptr, "native_func_ptr")))
    {
        wasm_jit_set_last_error("llvm build load failed.");
        return false;
    }

    if (!(func = LLVMBuildBitCast(comp_ctx->builder, func_ptr,
                                  func_ptr_type, "native_func")))
    {
        wasm_jit_set_last_error("llvm bit cast failed.");
        return false;
    }

    /* Call the function */
    if (!(value_ret = LLVMBuildCall2(
              comp_ctx->builder, native_func_type, func, param_values,
              (uint32)param_count + 1 + ext_ret_count,
              (func_type->result_count > 0 ? "call" : ""))))
    {
        wasm_jit_set_last_error("LLVM build call failed.");
        return false;
    }

    /* Check whether there was exception thrown when executing
       the function */
    if (!check_exception_thrown(comp_ctx, func_ctx))
    {
        return false;
    }

    if (func_type->result_count > 0)
    {
        /* Push the first result to stack */
        PUSH(value_ret);
        /* Load extra result from its address and push to stack */
        for (i = 0; i < ext_ret_count; i++)
        {
            snprintf(buf, sizeof(buf), "func_ext_ret%d", i);
            if (!(ext_ret = LLVMBuildLoad2(
                      comp_ctx->builder, TO_LLVM_TYPE(ext_ret_types[i]),
                      param_values[1 + param_count + i], buf)))
            {
                wasm_jit_set_last_error("llvm build load failed.");
                return false;
            }
            PUSH(ext_ret);
        }
    }
    return true;
}

bool wasm_jit_compile_op_call(WASMModule *wasm_module, JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                              uint32 func_idx)
{
    uint32 import_func_count = wasm_module->import_function_count;
    WASMFunction *wasm_func = wasm_module->functions + func_idx;
    uint32 ext_ret_cell_num = 0, cell_num = 0;
    WASMType *func_type = wasm_func->func_type;
    LLVMTypeRef *param_types = NULL;
    LLVMTypeRef ext_ret_ptr_type;
    LLVMValueRef *param_values = NULL;
    LLVMValueRef ext_ret_ptr, ext_ret_idx;
    int32 i, j = 0, param_count, result_count, ext_ret_count;
    uint64 total_size;
    uint8 *ext_ret_types = NULL;
    bool ret = false;
    char buf[32];

    param_count = (int32)func_type->param_count;
    result_count = (int32)func_type->result_count;
    ext_ret_count = result_count > 1 ? result_count - 1 : 0;
    total_size =
        sizeof(LLVMValueRef) * (uint64)(param_count + 1 + ext_ret_count);
    if (total_size >= UINT32_MAX || !(param_values = wasm_runtime_malloc(total_size)))
    {
        wasm_jit_set_last_error("allocate memory failed.");
        return false;
    }

    // 第一个参数
    param_values[j++] = func_ctx->exec_env;

    // 获得参数
    for (i = param_count - 1; i >= 0; i--)
        POP(param_values[i + j]);

    if (ext_ret_count > 0)
    {
        ext_ret_types = func_type->result + 1;
        ext_ret_cell_num = wasm_get_cell_num(ext_ret_types, ext_ret_count);
        if (ext_ret_cell_num > 64)
        {
            wasm_jit_set_last_error("prepare extra results's return "
                                    "address arguments failed: "
                                    "maximum 64 parameter cell number supported.");
            goto fail;
        }

        for (i = 0; i < ext_ret_count; i++)
        {
            if (!(ext_ret_idx = I32_CONST(cell_num)) || !(ext_ret_ptr_type =
                                                              LLVMPointerType(TO_LLVM_TYPE(ext_ret_types[i]), 0)))
            {
                wasm_jit_set_last_error("llvm add const or pointer type failed.");
                goto fail;
            }

            snprintf(buf, sizeof(buf), "ext_ret%d_ptr", i);
            if (!(ext_ret_ptr = LLVMBuildInBoundsGEP2(
                      comp_ctx->builder, I32_TYPE, func_ctx->argv_buf,
                      &ext_ret_idx, 1, buf)))
            {
                wasm_jit_set_last_error("llvm build GEP failed.");
                goto fail;
            }
            snprintf(buf, sizeof(buf), "ext_ret%d_ptr_cast", i);
            if (!(ext_ret_ptr = LLVMBuildBitCast(comp_ctx->builder, ext_ret_ptr,
                                                 ext_ret_ptr_type, buf)))
            {
                wasm_jit_set_last_error("llvm build bit cast failed.");
                goto fail;
            }
            param_values[param_count + 1 + i] = ext_ret_ptr;
            cell_num += wasm_value_type_cell_num(ext_ret_types[i]);
        }
    }

    if (func_idx < import_func_count)
    {
        ret = wasm_jit_call_import_function(comp_ctx, func_ctx, wasm_func, param_values,
                                            func_idx, ext_ret_types);
    }
    else
    {
        ret = wasm_jit_call_define_function(comp_ctx, func_ctx, wasm_func, param_values,
                                            func_idx - import_func_count, ext_ret_types);
    }

fail:
    if (param_types)
        wasm_runtime_free(param_types);
    if (param_values)
        wasm_runtime_free(param_values);
    return ret;
}

// bool jit_call_indirect(WASMExecEnv *exec_env, uint32 tbl_idx, uint32 table_elem_idx,
//                        uint32 argc, uint32 *argv)
// {
//     WASMModule *wasm_module = exec_env->module_inst;
//     WASMFunction *wasm_func;
//     WASMType *func_type;
//     WASMTable *tbl_inst;
//     void **func_ptrs = wasm_module->func_ptrs, *func_ptr;
//     uint32 func_idx, ext_ret_count;
//     bool ret;

//     tbl_inst = wasm_module->tables + tbl_idx;

//     if (table_elem_idx >= tbl_inst->cur_size)
//     {
//         goto fail;
//     }

//     func_idx = tbl_inst->table_data[table_elem_idx];
//     if (func_idx == NULL_REF)
//     {
//         goto fail;
//     }

//     wasm_func = wasm_module->functions + func_idx;
//     func_type = wasm_func->func_type;

//     // 后续要修改
//     func_ptr = func_ptrs[func_idx];

//     ext_ret_count = func_type->result_count > 1 ? func_type->result_count - 1 : 0;
//     if (ext_ret_count > 0)
//     {
//         uint32 argv1_buf[32], *argv1 = argv1_buf;
//         uint32 *ext_rets = NULL, *argv_ret = argv;
//         uint32 cell_num = 0, i;
//         uint8 *ext_ret_types = func_type->result + 1;
//         uint32 ext_ret_cell = wasm_get_cell_num(ext_ret_types, ext_ret_count);
//         uint64 size;

//         /* Allocate memory all arguments */
//         size =
//             sizeof(uint32) * (uint64)argc            /* original arguments */
//             + sizeof(void *) * (uint64)ext_ret_count /* extra result values' addr */
//             + sizeof(uint32) * (uint64)ext_ret_cell; /* extra result values */
//         if (size > sizeof(argv1_buf) && !(argv1 = wasm_runtime_malloc(size)))
//         {
//             goto fail;
//         }

//         /* Copy original arguments */
//         memcpy(argv1, argv, sizeof(uint32) * argc);

//         /* Get the extra result value's address */
//         ext_rets =
//             argv1 + argc + sizeof(void *) / sizeof(uint32) * ext_ret_count;

//         /* Append each extra result value's address to original arguments */
//         for (i = 0; i < ext_ret_count; i++)
//         {
//             *(uintptr_t *)(argv1 + argc + sizeof(void *) / sizeof(uint32) * i) =
//                 (uintptr_t)(ext_rets + cell_num);
//             cell_num += wasm_value_type_cell_num(ext_ret_types[i]);
//         }

//         ret = wasm_runtime_invoke_native(exec_env, wasm_func, argv1, argv);
//         if (!ret)
//         {
//             if (argv1 != argv1_buf)
//                 wasm_runtime_free(argv1);
//             goto fail;
//         }

//         /* Get extra result values */
//         switch (func_type->result[0])
//         {
//         case VALUE_TYPE_I32:
//         case VALUE_TYPE_F32:
//             argv_ret++;
//             break;
//         case VALUE_TYPE_I64:
//         case VALUE_TYPE_F64:
//             argv_ret += 2;
//             break;
//         default:
//             break;
//         }
//         ext_rets =
//             argv1 + argc + sizeof(void *) / sizeof(uint32) * ext_ret_count;
//         memcpy(argv_ret, ext_rets, sizeof(uint32) * cell_num);

//         if (argv1 != argv1_buf)
//             wasm_runtime_free(argv1);

//         return true;
//     }
//     else
//     {
//         ret = wasm_runtime_invoke_native(exec_env, wasm_func, argv, argv);
//         if (!ret)
//             goto fail;

//         return true;
//     }

// fail:
//     return false;
// }

// static bool
// call_wasm_jit_call_indirect_func(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
//                                  WASMType *func_type, LLVMValueRef table_idx,
//                                  LLVMValueRef table_elem_idx,
//                                  LLVMTypeRef *param_types,
//                                  LLVMValueRef *param_values, uint32 param_count,
//                                  uint32 param_cell_num, uint32 result_count,
//                                  uint8 *wasm_ret_types, LLVMValueRef *value_rets,
//                                  LLVMValueRef *p_res)
// {
//     LLVMTypeRef llvm_func_type, func_ptr_type, func_param_types[6];
//     LLVMTypeRef ret_type, ret_ptr_type, elem_ptr_type;
//     LLVMValueRef func, ret_idx, ret_ptr, elem_idx, elem_ptr;
//     LLVMValueRef func_param_values[6], res = NULL;
//     char buf[32];
//     uint32 i, cell_num = 0, ret_cell_num, argv_cell_num;

//     func_param_types[0] = comp_ctx->exec_env_type; /* exec_env */
//     func_param_types[1] = I32_TYPE;                /* table_idx */
//     func_param_types[2] = I32_TYPE;                /* table_elem_idx */
//     func_param_types[3] = I32_TYPE;                /* argc */
//     func_param_types[4] = INT32_PTR_TYPE;          /* argv */
//     if (!(llvm_func_type = LLVMFunctionType(INT8_TYPE, func_param_types, 5, false)))
//     {
//         wasm_jit_set_last_error("llvm add function type failed.");
//         return false;
//     }

//     /* prepare function pointer */

//     if (!(func_ptr_type = LLVMPointerType(llvm_func_type, 0)))
//     {
//         wasm_jit_set_last_error("create LLVM function type failed.");
//         return false;
//     }

//     /* JIT mode, call the function directly */
//     if (!(func = I64_CONST((uint64)(uintptr_t)jit_call_indirect)) || !(func = LLVMConstIntToPtr(func, func_ptr_type)))
//     {
//         wasm_jit_set_last_error("create LLVM value failed.");
//         return false;
//     }

//     ret_cell_num = wasm_get_cell_num(wasm_ret_types, result_count);
//     argv_cell_num =
//         param_cell_num > ret_cell_num ? param_cell_num : ret_cell_num;
//     if (argv_cell_num > 64)
//     {
//         wasm_jit_set_last_error("prepare native arguments failed: "
//                                 "maximum 64 parameter cell number supported.");
//         return false;
//     }

//     /* prepare frame_lp */
//     for (i = 0; i < param_count; i++)
//     {
//         if (!(elem_idx = I32_CONST(cell_num)) || !(elem_ptr_type = LLVMPointerType(param_types[i], 0)))
//         {
//             wasm_jit_set_last_error("llvm add const or pointer type failed.");
//             return false;
//         }

//         snprintf(buf, sizeof(buf), "%s%d", "elem", i);
//         if (!(elem_ptr =
//                   LLVMBuildInBoundsGEP2(comp_ctx->builder, I32_TYPE,
//                                         func_ctx->argv_buf, &elem_idx, 1, buf)) ||
//             !(elem_ptr = LLVMBuildBitCast(comp_ctx->builder, elem_ptr,
//                                           elem_ptr_type, buf)))
//         {
//             wasm_jit_set_last_error("llvm build bit cast failed.");
//             return false;
//         }

//         if (!(res = LLVMBuildStore(comp_ctx->builder, param_values[i],
//                                    elem_ptr)))
//         {
//             wasm_jit_set_last_error("llvm build store failed.");
//             return false;
//         }
//         LLVMSetAlignment(res, 1);

//         cell_num += wasm_value_type_cell_num(func_type->param[i]);
//     }

//     func_param_values[0] = func_ctx->exec_env;
//     func_param_values[1] = table_idx;
//     func_param_values[2] = table_elem_idx;
//     func_param_values[3] = I32_CONST(param_cell_num);
//     func_param_values[4] = func_ctx->argv_buf;

//     /* call wasm_jit_call_indirect() function */
//     if (!(res = LLVMBuildCall2(comp_ctx->builder, llvm_func_type, func,
//                                func_param_values, 5, "res")))
//     {
//         wasm_jit_set_last_error("llvm build call failed.");
//         return false;
//     }

//     /* get function result values */
//     cell_num = 0;
//     for (i = 0; i < result_count; i++)
//     {
//         ret_type = TO_LLVM_TYPE(wasm_ret_types[i]);
//         if (!(ret_idx = I32_CONST(cell_num)) || !(ret_ptr_type = LLVMPointerType(ret_type, 0)))
//         {
//             wasm_jit_set_last_error("llvm add const or pointer type failed.");
//             return false;
//         }

//         snprintf(buf, sizeof(buf), "argv_ret%d", i);
//         if (!(ret_ptr =
//                   LLVMBuildInBoundsGEP2(comp_ctx->builder, I32_TYPE,
//                                         func_ctx->argv_buf, &ret_idx, 1, buf)) ||
//             !(ret_ptr = LLVMBuildBitCast(comp_ctx->builder, ret_ptr,
//                                          ret_ptr_type, buf)))
//         {
//             wasm_jit_set_last_error("llvm build GEP or bit cast failed.");
//             return false;
//         }

//         snprintf(buf, sizeof(buf), "ret%d", i);
//         if (!(value_rets[i] =
//                   LLVMBuildLoad2(comp_ctx->builder, ret_type, ret_ptr, buf)))
//         {
//             wasm_jit_set_last_error("llvm build load failed.");
//             return false;
//         }
//         cell_num += wasm_value_type_cell_num(wasm_ret_types[i]);
//     }

//     *p_res = res;
//     return true;
// }

bool wasm_jit_compile_op_call_indirect(WASMModule *wasm_module, JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                       uint32 type_idx, uint32 tbl_idx)
{
    WASMType *wasm_type;
    LLVMValueRef llvm_elem_idx, llvm_table_elem, llvm_func_idx;
    LLVMValueRef ftype_idx_ptr, ftype_idx, ftype_idx_const, llvm_import_func_count;
    LLVMValueRef cmp_elem_idx, cmp_func_idx, cmp_ftype_idx;
    LLVMValueRef llvm_func, func_ptr, table_size_const;
    LLVMValueRef ext_ret_offset, ext_ret_ptr, ext_ret;
    LLVMValueRef *llvm_param_values = NULL, *llvm_value_rets = NULL;
    LLVMValueRef *result_phis = NULL, value_ret;
    LLVMTypeRef *llvm_param_types = NULL;
    LLVMTypeRef llvm_func_type, llvm_func_ptr_type;
    LLVMTypeRef ext_ret_ptr_type, llvm_ret_type;
    LLVMBasicBlockRef check_elem_idx_succ, check_ftype_idx_succ;
    LLVMBasicBlockRef check_func_idx_succ, block_return, block_curr;
    LLVMBasicBlockRef block_call_import, block_call_non_import;
    LLVMValueRef llvm_offset;
    uint32 total_param_count, param_count, result_count;
    uint32 ext_cell_num, i, j;
    uint8 *wasm_param_types, *wasm_result_types;
    uint64 total_size;
    char buf[32];
    bool ret = false;

    ftype_idx_const = I32_CONST(type_idx);

    wasm_type = wasm_module->types[type_idx];
    wasm_param_types = wasm_type->param;
    wasm_result_types = wasm_type->result;
    param_count = wasm_type->param_count;
    result_count = wasm_type->result_count;

    POP_I32(llvm_elem_idx);

    /* get the cur size of the table instance */
    if (!(llvm_offset = I32_CONST(get_tbl_inst_offset(tbl_idx) + offsetof(WASMTable, cur_size))))
    {
        HANDLE_FAILURE("LLVMConstInt");
        goto fail;
    }

    if (!(table_size_const = LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE,
                                                   func_ctx->wasm_module, &llvm_offset,
                                                   1, "cur_size_i8p")))
    {
        HANDLE_FAILURE("LLVMBuildGEP");
        goto fail;
    }

    if (!(table_size_const =
              LLVMBuildBitCast(comp_ctx->builder, table_size_const,
                               INT32_PTR_TYPE, "cur_siuze_i32p")))
    {
        HANDLE_FAILURE("LLVMBuildBitCast");
        goto fail;
    }

    if (!(table_size_const = LLVMBuildLoad2(comp_ctx->builder, I32_TYPE,
                                            table_size_const, "cur_size")))
    {
        HANDLE_FAILURE("LLVMBuildLoad");
        goto fail;
    }

    /* Check if (uint32)elem index >= table size */
    if (!(cmp_elem_idx = LLVMBuildICmp(comp_ctx->builder, LLVMIntUGE, llvm_elem_idx,
                                       table_size_const, "cmp_elem_idx")))
    {
        wasm_jit_set_last_error("llvm build icmp failed.");
        goto fail;
    }

    /* Throw exception if elem index >= table size */
    if (!(check_elem_idx_succ = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func_ctx->func, "check_elem_idx_succ")))
    {
        wasm_jit_set_last_error("llvm add basic block failed.");
        goto fail;
    }

    LLVMMoveBasicBlockAfter(check_elem_idx_succ,
                            LLVMGetInsertBlock(comp_ctx->builder));

    if (!(wasm_jit_emit_exception(comp_ctx, func_ctx, EXCE_UNDEFINED_ELEMENT, true,
                                  cmp_elem_idx, check_elem_idx_succ)))
        goto fail;

    /* load data as i32* */
    if (!(llvm_offset = I32_CONST(get_tbl_inst_offset(tbl_idx) + offsetof(WASMTable, table_data))))
    {
        HANDLE_FAILURE("LLVMConstInt");
        goto fail;
    }

    if (!(llvm_table_elem = LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE,
                                                  func_ctx->wasm_module, &llvm_offset, 1,
                                                  "table_elem_i8p")))
    {
        wasm_jit_set_last_error("llvm build add failed.");
        goto fail;
    }

    if (!(llvm_table_elem = LLVMBuildBitCast(comp_ctx->builder, llvm_table_elem,
                                             INT32_PTR_TYPE, "table_elem_i32p")))
    {
        HANDLE_FAILURE("LLVMBuildBitCast");
        goto fail;
    }

    /* Load function index */
    if (!(llvm_table_elem =
              LLVMBuildInBoundsGEP2(comp_ctx->builder, I32_TYPE, llvm_table_elem,
                                    &llvm_elem_idx, 1, "table_elem")))
    {
        HANDLE_FAILURE("LLVMBuildNUWAdd");
        goto fail;
    }

    if (!(llvm_func_idx = LLVMBuildLoad2(comp_ctx->builder, I32_TYPE, llvm_table_elem,
                                         "func_idx")))
    {
        wasm_jit_set_last_error("llvm build load failed.");
        goto fail;
    }

    /* Check if func_idx == -1 */
    if (!(cmp_func_idx = LLVMBuildICmp(comp_ctx->builder, LLVMIntEQ, llvm_func_idx,
                                       I32_NEG_ONE, "cmp_func_idx")))
    {
        wasm_jit_set_last_error("llvm build icmp failed.");
        goto fail;
    }

    /* Throw exception if func_idx == -1 */
    if (!(check_func_idx_succ = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func_ctx->func, "check_func_idx_succ")))
    {
        wasm_jit_set_last_error("llvm add basic block failed.");
        goto fail;
    }

    LLVMMoveBasicBlockAfter(check_func_idx_succ,
                            LLVMGetInsertBlock(comp_ctx->builder));

    if (!(wasm_jit_emit_exception(comp_ctx, func_ctx, EXCE_UNINITIALIZED_ELEMENT,
                                  true, cmp_func_idx, check_func_idx_succ)))
        goto fail;

    /* Load function type index */
    if (!(ftype_idx_ptr = LLVMBuildInBoundsGEP2(
              comp_ctx->builder, I32_TYPE, func_ctx->func_type_indexes,
              &llvm_func_idx, 1, "ftype_idx_ptr")))
    {
        wasm_jit_set_last_error("llvm build inbounds gep failed.");
        goto fail;
    }

    if (!(ftype_idx = LLVMBuildLoad2(comp_ctx->builder, I32_TYPE, ftype_idx_ptr,
                                     "ftype_idx")))
    {
        wasm_jit_set_last_error("llvm build load failed.");
        goto fail;
    }

    /* Check if function type index not equal */
    if (!(cmp_ftype_idx = LLVMBuildICmp(comp_ctx->builder, LLVMIntNE, ftype_idx,
                                        ftype_idx_const, "cmp_ftype_idx")))
    {
        wasm_jit_set_last_error("llvm build icmp failed.");
        goto fail;
    }

    /* Throw exception if ftype_idx != ftype_idx_const */
    if (!(check_ftype_idx_succ = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func_ctx->func, "check_ftype_idx_succ")))
    {
        wasm_jit_set_last_error("llvm add basic block failed.");
        goto fail;
    }

    LLVMMoveBasicBlockAfter(check_ftype_idx_succ,
                            LLVMGetInsertBlock(comp_ctx->builder));

    if (!(wasm_jit_emit_exception(comp_ctx, func_ctx,
                                  EXCE_INVALID_FUNCTION_TYPE_INDEX, true,
                                  cmp_ftype_idx, check_ftype_idx_succ)))
        goto fail;

    /* Initialize parameter types of the LLVM function */
    total_param_count = 1 + param_count;

    /* Extra function results' addresses (except the first one) are
       appended to aot function parameters. */
    if (result_count > 1)
        total_param_count += result_count - 1;

    total_size = sizeof(LLVMTypeRef) * (uint64)total_param_count;
    if (total_size >= UINT32_MAX || !(llvm_param_types = wasm_runtime_malloc((uint32)total_size)))
    {
        wasm_jit_set_last_error("allocate memory failed.");
        goto fail;
    }

    /* Prepare param types */
    j = 0;
    llvm_param_types[j++] = comp_ctx->exec_env_type;
    for (i = 0; i < param_count; i++)
        llvm_param_types[j++] = TO_LLVM_TYPE(wasm_param_types[i]);

    for (i = 1; i < result_count; i++, j++)
    {
        llvm_param_types[j] = TO_LLVM_TYPE(wasm_result_types[i]);
        if (!(llvm_param_types[j] = LLVMPointerType(llvm_param_types[j], 0)))
        {
            wasm_jit_set_last_error("llvm get pointer type failed.");
            goto fail;
        }
    }

    /* Allocate memory for parameters */
    total_size = sizeof(LLVMValueRef) * (uint64)total_param_count;
    if (!(llvm_param_values = wasm_runtime_malloc(total_size)))
    {
        wasm_jit_set_last_error("allocate memory failed.");
        goto fail;
    }

    /* First parameter is exec env */
    j = 0;
    llvm_param_values[j++] = func_ctx->exec_env;

    /* Pop parameters from stack */
    for (i = param_count - 1; (int32)i >= 0; i--)
        POP(llvm_param_values[i + j]);

    /* Prepare extra parameters */
    ext_cell_num = 0;
    for (i = 1; i < result_count; i++)
    {
        ext_ret_offset = I32_CONST(ext_cell_num);

        snprintf(buf, sizeof(buf), "ext_ret%d_ptr", i - 1);
        if (!(ext_ret_ptr = LLVMBuildInBoundsGEP2(comp_ctx->builder, I32_TYPE,
                                                  func_ctx->argv_buf,
                                                  &ext_ret_offset, 1, buf)))
        {
            wasm_jit_set_last_error("llvm build GEP failed.");
            goto fail;
        }

        ext_ret_ptr_type = llvm_param_types[param_count + i];
        snprintf(buf, sizeof(buf), "ext_ret%d_ptr_cast", i - 1);
        if (!(ext_ret_ptr = LLVMBuildBitCast(comp_ctx->builder, ext_ret_ptr,
                                             ext_ret_ptr_type, buf)))
        {
            wasm_jit_set_last_error("llvm build bit cast failed.");
            goto fail;
        }

        llvm_param_values[param_count + i] = ext_ret_ptr;
        ext_cell_num += wasm_value_type_cell_num(wasm_result_types[i]);
    }

    if (ext_cell_num > 64)
    {
        wasm_jit_set_last_error("prepare call-indirect arguments failed: "
                                "maximum 64 extra cell number supported.");
        goto fail;
    }

    /* Add basic blocks */
    block_call_import = LLVMAppendBasicBlockInContext(
        comp_ctx->context, func_ctx->func, "call_indirect");
    block_call_non_import = LLVMAppendBasicBlockInContext(
        comp_ctx->context, func_ctx->func, "call_non_import");
    block_return = LLVMAppendBasicBlockInContext(comp_ctx->context,
                                                 func_ctx->func, "func_return");
    if (!block_call_import || !block_call_non_import || !block_return)
    {
        wasm_jit_set_last_error("llvm add basic block failed.");
        goto fail;
    }

    LLVMMoveBasicBlockAfter(block_call_import,
                            LLVMGetInsertBlock(comp_ctx->builder));
    LLVMMoveBasicBlockAfter(block_call_non_import, block_call_import);
    LLVMMoveBasicBlockAfter(block_return, block_call_non_import);

    llvm_import_func_count = I32_CONST(wasm_module->import_function_count);

    /* Check if func_idx < import_func_count */
    if (!(cmp_func_idx = LLVMBuildICmp(comp_ctx->builder, LLVMIntULT, llvm_func_idx,
                                       llvm_import_func_count, "cmp_func_idx")))
    {
        wasm_jit_set_last_error("llvm build icmp failed.");
        goto fail;
    }

    /* If func_idx < import_func_count, jump to call import block,
       else jump to call non-import block */
    if (!LLVMBuildCondBr(comp_ctx->builder, cmp_func_idx, block_call_import,
                         block_call_non_import))
    {
        wasm_jit_set_last_error("llvm build cond br failed.");
        goto fail;
    }

    /* Add result phis for return block */
    LLVMPositionBuilderAtEnd(comp_ctx->builder, block_return);

    if (result_count > 0)
    {
        total_size = sizeof(LLVMValueRef) * (uint64)result_count;
        if (!(result_phis = wasm_runtime_malloc(total_size)))
        {
            wasm_jit_set_last_error("allocate memory failed.");
            goto fail;
        }
        memset(result_phis, 0, (uint32)total_size);
        for (i = 0; i < result_count; i++)
        {
            LLVMTypeRef tmp_type = TO_LLVM_TYPE(wasm_result_types[i]);
            if (!(result_phis[i] =
                      LLVMBuildPhi(comp_ctx->builder, tmp_type, "phi")))
            {
                wasm_jit_set_last_error("llvm build phi failed.");
                goto fail;
            }
        }
    }

    /* Translate call import block */
    LLVMPositionBuilderAtEnd(comp_ctx->builder, block_call_import);

    /* Allocate memory for result values */
    if (result_count > 0)
    {
        total_size = sizeof(LLVMValueRef) * (uint64)result_count;
        if (total_size >= UINT32_MAX || !(llvm_value_rets = wasm_runtime_malloc(total_size)))
        {
            wasm_jit_set_last_error("allocate memory failed.");
            goto fail;
        }
        memset(llvm_value_rets, 0, (uint32)total_size);
    }

    LLVMTypeRef import_func_param_types[4];
    LLVMValueRef import_func_param_values[4];
    LLVMTypeRef func_ptr_type, ret_ptr_type, llvm_param_ptr_type;
    LLVMValueRef llvm_res, llvm_ret_idx, llvm_ret_ptr, llvm_param_idx, llvm_param_ptr;
    uint32 cell_num;
    import_func_param_types[0] = comp_ctx->exec_env_type; /* exec_env */
    import_func_param_types[1] = I32_TYPE;                /* func_idx */
    import_func_param_types[2] = INT32_PTR_TYPE;          /* argv */
    import_func_param_types[3] = INT32_PTR_TYPE;          /* argv_ret */

    if (!(llvm_func_type = LLVMFunctionType(INT8_TYPE, import_func_param_types, 4, false)))
    {
        wasm_jit_set_last_error("llvm add function type failed.");
        return false;
    }

    /* prepare function pointer */

    if (!(func_ptr_type = LLVMPointerType(llvm_func_type, 0)))
    {
        wasm_jit_set_last_error("create LLVM function type failed.");
        return false;
    }

    /* JIT mode, call the function directly */
    if (!(llvm_func = I64_CONST((uint64)(uintptr_t)wasm_runtime_invoke_native)) || !(llvm_func = LLVMConstIntToPtr(llvm_func, func_ptr_type)))
    {
        wasm_jit_set_last_error("create LLVM value failed.");
        return false;
    }

    cell_num = 0;
    /* prepare frame_lp */
    for (i = 0; i < param_count; i++)
    {
        if (!(llvm_param_idx = I32_CONST(cell_num)) || !(llvm_param_ptr_type = LLVMPointerType(TO_LLVM_TYPE(wasm_param_types[i]), 0)))
        {
            wasm_jit_set_last_error("llvm add const or pointer type failed.");
            return false;
        }

        snprintf(buf, sizeof(buf), "%s%d", "elem", i);
        if (!(llvm_param_ptr =
                  LLVMBuildInBoundsGEP2(comp_ctx->builder, I32_TYPE,
                                        func_ctx->argv_buf, &llvm_param_idx, 1, buf)) ||
            !(llvm_param_ptr = LLVMBuildBitCast(comp_ctx->builder, llvm_param_ptr,
                                                llvm_param_ptr_type, buf)))
        {
            wasm_jit_set_last_error("llvm build bit cast failed.");
            return false;
        }

        // 注意第一个是exec_env
        if (!(llvm_res = LLVMBuildStore(comp_ctx->builder, llvm_param_values[i + 1],
                                        llvm_param_ptr)))
        {
            wasm_jit_set_last_error("llvm build store failed.");
            return false;
        }
        LLVMSetAlignment(llvm_res, 1);

        cell_num += wasm_value_type_cell_num(wasm_param_types[i]);
    }

    // 都使用argv_buf
    import_func_param_values[0] = func_ctx->exec_env;
    import_func_param_values[1] = llvm_func_idx;
    import_func_param_values[2] = func_ctx->argv_buf;
    import_func_param_values[3] = func_ctx->argv_buf;

    if (!(llvm_res = LLVMBuildCall2(comp_ctx->builder, llvm_func_type, llvm_func,
                                    import_func_param_values, 4, "res")))
    {
        wasm_jit_set_last_error("llvm build call failed.");
        return false;
    }

    // 返回参数全存在argv_buf中
    cell_num = 0;
    for (i = 0; i < result_count; i++)
    {
        llvm_ret_type = TO_LLVM_TYPE(wasm_result_types[i]);
        if (!(llvm_ret_idx = I32_CONST(cell_num)) || !(ret_ptr_type = LLVMPointerType(llvm_ret_type, 0)))
        {
            wasm_jit_set_last_error("llvm add const or pointer type failed.");
            return false;
        }

        snprintf(buf, sizeof(buf), "argv_ret%d", i);
        if (!(llvm_ret_ptr =
                  LLVMBuildInBoundsGEP2(comp_ctx->builder, I32_TYPE,
                                        func_ctx->argv_buf, &llvm_ret_idx, 1, buf)) ||
            !(llvm_ret_ptr = LLVMBuildBitCast(comp_ctx->builder, llvm_ret_ptr,
                                              ret_ptr_type, buf)))
        {
            wasm_jit_set_last_error("llvm build GEP or bit cast failed.");
            return false;
        }

        snprintf(buf, sizeof(buf), "ret%d", i);
        if (!(llvm_value_rets[i] =
                  LLVMBuildLoad2(comp_ctx->builder, llvm_ret_type, llvm_ret_ptr, buf)))
        {
            wasm_jit_set_last_error("llvm build load failed.");
            return false;
        }
        cell_num += wasm_value_type_cell_num(wasm_result_types[i]);
    }

    /* Check whether exception was thrown when executing the function */
    if (comp_ctx->enable_bound_check && !check_call_return(comp_ctx, func_ctx, llvm_res))
        goto fail;

    block_curr = LLVMGetInsertBlock(comp_ctx->builder);
    for (i = 0; i < result_count; i++)
    {
        LLVMAddIncoming(result_phis[i], &llvm_value_rets[i], &block_curr, 1);
    }

    if (!LLVMBuildBr(comp_ctx->builder, block_return))
    {
        wasm_jit_set_last_error("llvm build br failed.");
        goto fail;
    }

    /* Translate call non-import block */
    LLVMPositionBuilderAtEnd(comp_ctx->builder, block_call_non_import);

    /* Load function pointer */
    if (!(func_ptr = LLVMBuildInBoundsGEP2(comp_ctx->builder, OPQ_PTR_TYPE,
                                           func_ctx->func_ptrs, &llvm_func_idx, 1,
                                           "func_ptr_tmp")))
    {
        wasm_jit_set_last_error("llvm build inbounds gep failed.");
        goto fail;
    }

    if (!(func_ptr = LLVMBuildLoad2(comp_ctx->builder, OPQ_PTR_TYPE, func_ptr,
                                    "func_ptr")))
    {
        wasm_jit_set_last_error("llvm build load failed.");
        goto fail;
    }

    /* Resolve return type of the LLVM function */
    if (result_count)
    {
        llvm_ret_type = TO_LLVM_TYPE(wasm_result_types[0]);
    }
    else
    {
        llvm_ret_type = VOID_TYPE;
    }

    if (!(llvm_func_type =
              LLVMFunctionType(llvm_ret_type, llvm_param_types, total_param_count, false)) ||
        !(llvm_func_ptr_type = LLVMPointerType(llvm_func_type, 0)))
    {
        wasm_jit_set_last_error("llvm add function type failed.");
        goto fail;
    }

    if (!(llvm_func = LLVMBuildBitCast(comp_ctx->builder, func_ptr,
                                       llvm_func_ptr_type, "indirect_func")))
    {
        wasm_jit_set_last_error("llvm build bit cast failed.");
        goto fail;
    }

    if (!(value_ret = LLVMBuildCall2(comp_ctx->builder, llvm_func_type, llvm_func,
                                     llvm_param_values, total_param_count,
                                     result_count > 0 ? "ret" : "")))
    {
        wasm_jit_set_last_error("llvm build call failed.");
        goto fail;
    }

    /* Check whether exception was thrown when executing the function */
    if (comp_ctx->enable_bound_check && !check_exception_thrown(comp_ctx, func_ctx))
        goto fail;

    if (result_count > 0)
    {
        block_curr = LLVMGetInsertBlock(comp_ctx->builder);

        /* Push the first result to stack */
        LLVMAddIncoming(result_phis[0], &value_ret, &block_curr, 1);

        /* Load extra result from its address and push to stack */
        for (i = 1; i < result_count; i++)
        {
            llvm_ret_type = TO_LLVM_TYPE(wasm_result_types[i]);
            snprintf(buf, sizeof(buf), "ext_ret%d", i - 1);
            if (!(ext_ret = LLVMBuildLoad2(comp_ctx->builder, llvm_ret_type,
                                           llvm_param_values[param_count + i],
                                           buf)))
            {
                wasm_jit_set_last_error("llvm build load failed.");
                goto fail;
            }
            LLVMAddIncoming(result_phis[i], &ext_ret, &block_curr, 1);
        }
    }

    if (!LLVMBuildBr(comp_ctx->builder, block_return))
    {
        wasm_jit_set_last_error("llvm build br failed.");
        goto fail;
    }

    /* Translate function return block */
    LLVMPositionBuilderAtEnd(comp_ctx->builder, block_return);

    for (i = 0; i < result_count; i++)
    {
        PUSH(result_phis[i]);
    }

    ret = true;

fail:
    if (llvm_param_values)
        wasm_runtime_free(llvm_param_values);
    if (llvm_param_types)
        wasm_runtime_free(llvm_param_types);
    if (llvm_value_rets)
        wasm_runtime_free(llvm_value_rets);
    if (result_phis)
        wasm_runtime_free(result_phis);
    return ret;
}

bool wasm_jit_compile_op_ref_null(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    PUSH_I32(REF_NULL);

    return true;
}

bool wasm_jit_compile_op_ref_is_null(JITCompContext *comp_ctx, JITFuncContext *func_ctx)
{
    LLVMValueRef lhs, res;

    POP_I32(lhs);

    if (!(res = LLVMBuildICmp(comp_ctx->builder, LLVMIntEQ, lhs, REF_NULL,
                              "cmp_w_null")))
    {
        HANDLE_FAILURE("LLVMBuildICmp");
        goto fail;
    }

    if (!(res = LLVMBuildZExt(comp_ctx->builder, res, I32_TYPE, "r_i")))
    {
        HANDLE_FAILURE("LLVMBuildZExt");
        goto fail;
    }

    PUSH_I32(res);

    return true;
fail:
    return false;
}

bool wasm_jit_compile_op_ref_func(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                  uint32 func_idx)
{
    LLVMValueRef ref_idx;

    if (!(ref_idx = I32_CONST(func_idx)))
    {
        HANDLE_FAILURE("LLVMConstInt");
        goto fail;
    }

    PUSH_I32(ref_idx);

    return true;
fail:
    return false;
}
