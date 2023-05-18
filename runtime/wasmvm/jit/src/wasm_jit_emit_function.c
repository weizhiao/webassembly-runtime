#include "wasm_jit_emit_function.h"
#include "wasm_jit_emit_exception.h"
#include "wasm_jit_emit_control.h"
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

static inline uint64
get_tbl_inst_offset(uint32 tbl_idx)
{
    uint64 offset = 0;

    offset = tbl_idx * sizeof(WASMTable);

    return offset;
}

// 参数和返回值的内存由调用者提供
static bool jit_call_indirect(JITCompContext *comp_ctx, JITFuncContext *func_ctx, WASMType *wasm_type,
                              JITFuncType *jit_func_type, LLVMValueRef llvm_func_idx, LLVMValueRef *llvm_param_values, LLVMValueRef *llvm_ret_values)
{
    LLVMTypeRef llvm_func_type, llvm_ret_type, *llvm_param_types, *llvm_result_types;
    LLVMTypeRef import_func_param_types[4];
    LLVMBuilderRef builder = comp_ctx->builder;
    LLVMValueRef import_func_param_values[4], llvm_func;
    LLVMTypeRef func_ptr_type, ret_ptr_type, llvm_param_ptr_type;
    LLVMValueRef llvm_res, llvm_ret_idx, llvm_ret_ptr, llvm_param_idx, llvm_param_ptr;
    LLVMTypeRef llvm_param_type;
    uint32 cell_num, param_count, result_count, i;
    uint8 *wasm_param_types, *wasm_result_types;
    char buf[32];

    param_count = wasm_type->param_count;
    result_count = wasm_type->result_count;
    wasm_result_types = wasm_type->result;
    wasm_param_types = wasm_type->param;
    llvm_param_types = jit_func_type->llvm_param_types;
    llvm_result_types = jit_func_type->llvm_result_types;

    import_func_param_types[0] = comp_ctx->exec_env_type; /* exec_env */
    import_func_param_types[1] = I32_TYPE;                /* func_idx */
    import_func_param_types[2] = I32_TYPE_PTR;            /* argv */
    import_func_param_types[3] = I32_TYPE_PTR;            /* argv_ret */

    llvm_func_type = LLVMFunctionType(INT8_TYPE, import_func_param_types, 4, false);

    func_ptr_type = LLVMPointerType(llvm_func_type, 0);

    llvm_func = I64_CONST((uint64)(uintptr_t)wasm_runtime_invoke_native);
    llvm_func = LLVMConstIntToPtr(llvm_func, func_ptr_type);

    cell_num = 0;
    for (i = 0; i < param_count; i++)
    {
        llvm_param_type = llvm_param_types[i + 1];
        llvm_param_idx = I32_CONST(cell_num);
        llvm_param_ptr_type = LLVMPointerType(llvm_param_type, 0);

        snprintf(buf, sizeof(buf), "%s%d", "elem", i);
        LLVMBuildGEP(llvm_param_ptr, I32_TYPE,
                     func_ctx->argv_buf, llvm_param_idx, buf);
        llvm_param_ptr = LLVMBuildBitCast(comp_ctx->builder, llvm_param_ptr,
                                          llvm_param_ptr_type, buf);

        llvm_res = LLVMBuildStore(comp_ctx->builder, llvm_param_values[i + 1],
                                  llvm_param_ptr);

        LLVMSetAlignment(llvm_res, 1);

        cell_num += wasm_value_type_cell_num(wasm_param_types[i]);
    }

    // 都使用argv_buf
    import_func_param_values[0] = func_ctx->exec_env;
    import_func_param_values[1] = llvm_func_idx;
    import_func_param_values[2] = func_ctx->argv_buf;
    import_func_param_values[3] = func_ctx->argv_buf;
    llvm_res = LLVMBuildCall2(comp_ctx->builder, llvm_func_type, llvm_func,
                              import_func_param_values, 4, "res");

    // 返回参数全存在argv_buf中
    cell_num = 0;
    for (i = 0; i < result_count; i++)
    {
        llvm_ret_type = llvm_result_types[i];
        llvm_ret_idx = I32_CONST(cell_num);
        ret_ptr_type = LLVMPointerType(llvm_ret_type, 0);
        snprintf(buf, sizeof(buf), "argv_ret%d", i);

        LLVMBuildGEP(llvm_ret_ptr, I32_TYPE,
                     func_ctx->argv_buf, llvm_ret_idx, buf);

        llvm_ret_ptr = LLVMBuildBitCast(comp_ctx->builder, llvm_ret_ptr,
                                        ret_ptr_type, buf);

        snprintf(buf, sizeof(buf), "ret%d", i);
        llvm_ret_values[i] = LLVMBuildLoad2(comp_ctx->builder, llvm_ret_type, llvm_ret_ptr, buf);
        cell_num += wasm_value_type_cell_num(wasm_result_types[i]);
    }

    return true;
}

static bool
jit_call_direct(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                WASMType *wasm_type, JITFuncType *jit_func_type, LLVMValueRef llvm_func_idx, LLVMValueRef *llvm_param_values, LLVMValueRef *llvm_ret_values, uint32 func_idx)
{
    LLVMValueRef llvm_ret, ext_ret, llvm_func_ptr, llvm_func;
    LLVMBuilderRef builder = comp_ctx->builder;
    LLVMTypeRef llvm_func_type, *llvm_ext_result_types, llvm_func_ptr_type;
    uint32 i;
    uint32 param_count = wasm_type->param_count;
    uint32 result_count = wasm_type->result_count;
    uint32 ext_ret_count = result_count > 1 ? result_count - 1 : 0;
    char buf[32];

    llvm_func_type = jit_func_type->llvm_func_type;

    if (func_idx == -1)
    {
        LLVMBuildGEP(llvm_func_ptr, OPQ_PTR_TYPE,
                     func_ctx->func_ptrs, llvm_func_idx, "func_ptr_tmp");

        llvm_func_ptr = LLVMBuildLoad2(comp_ctx->builder, OPQ_PTR_TYPE, llvm_func_ptr,
                                       "func_ptr");
        llvm_func_ptr_type = LLVMPointerType(llvm_func_type, 0);

        if (!(llvm_func = LLVMBuildBitCast(comp_ctx->builder, llvm_func_ptr,
                                           llvm_func_ptr_type, "indirect_func")))
        {
            wasm_jit_set_last_error("llvm build bit cast failed.");
            return false;
        }

        if (!(llvm_ret = LLVMBuildCall2(comp_ctx->builder, llvm_func_type, llvm_func,
                                        llvm_param_values, param_count + 1 + ext_ret_count,
                                        result_count > 0 ? "ret" : "")))
        {
            wasm_jit_set_last_error("llvm build call failed.");
            return false;
        }
    }
    else
    {
        llvm_func = comp_ctx->jit_func_ctxes[func_idx]->func;
        llvm_ret = LLVMBuildCall2(comp_ctx->builder, llvm_func_type, llvm_func,
                                  llvm_param_values, param_count + 1 + ext_ret_count,
                                  result_count > 0 ? "ret" : "");
    }

    if (wasm_type->result_count > 0)
    {
        llvm_ret_values[0] = llvm_ret;
        llvm_ext_result_types = jit_func_type->llvm_result_types + 1;
        for (i = 0; i < ext_ret_count; i++)
        {
            snprintf(buf, sizeof(buf), "func_ext_ret%d", i);
            if (!(ext_ret = LLVMBuildLoad2(
                      comp_ctx->builder, llvm_ext_result_types[i],
                      llvm_param_values[1 + param_count + i], buf)))
            {
                wasm_jit_set_last_error("llvm build load failed.");
                return false;
            }
            llvm_ret_values[1 + i] = ext_ret;
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
    LLVMValueRef *llvm_param_values = NULL;
    LLVMValueRef *llvm_ret_values = NULL;
    LLVMValueRef ext_ret_ptr, ext_ret_idx, llvm_func_idx;
    LLVMBuilderRef builder = comp_ctx->builder;
    int32 i, j = 0, param_count, result_count, ext_ret_count;
    uint64 total_size;
    uint8 *ext_ret_types = NULL;
    bool ret = false;
    char buf[32];
    JITFuncType *jit_func_type = comp_ctx->jit_func_types + wasm_func->type_index;
    LLVMTypeRef *llvm_param_types = jit_func_type->llvm_param_types;

    param_count = (int32)func_type->param_count;
    result_count = (int32)func_type->result_count;
    ext_ret_count = result_count > 1 ? result_count - 1 : 0;
    total_size = sizeof(LLVMValueRef) * (uint64)(param_count + 1 + ext_ret_count);
    llvm_param_values = wasm_runtime_malloc(total_size);
    total_size = sizeof(LLVMValueRef) * result_count;
    llvm_func_idx = I32_CONST(func_idx);

    if (total_size > 0)
        llvm_ret_values = wasm_runtime_malloc(total_size);

    // 第一个参数
    llvm_param_values[j++] = func_ctx->exec_env;

    // 获得参数
    for (i = param_count - 1; i >= 0; i--)
        POP(llvm_param_values[i + j]);

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
            if (!(ext_ret_idx = I32_CONST(cell_num)) || !(ext_ret_ptr_type = llvm_param_types[param_count + 1 + i]))
            {
                wasm_jit_set_last_error("llvm add const or pointer type failed.");
                goto fail;
            }

            snprintf(buf, sizeof(buf), "ext_ret%d_ptr", i);
            LLVMBuildGEP(ext_ret_ptr, I32_TYPE, func_ctx->argv_buf,
                         ext_ret_idx, buf);

            snprintf(buf, sizeof(buf), "ext_ret%d_ptr_cast", i);
            if (!(ext_ret_ptr = LLVMBuildBitCast(comp_ctx->builder, ext_ret_ptr,
                                                 ext_ret_ptr_type, buf)))
            {
                wasm_jit_set_last_error("llvm build bit cast failed.");
                goto fail;
            }
            llvm_param_values[param_count + 1 + i] = ext_ret_ptr;
            cell_num += wasm_value_type_cell_num(ext_ret_types[i]);
        }
    }

    if (func_idx < import_func_count)
    {
        ret = jit_call_indirect(comp_ctx, func_ctx, wasm_func->func_type, jit_func_type, llvm_func_idx, llvm_param_values, llvm_ret_values);
    }
    else
    {
        ret = jit_call_direct(comp_ctx, func_ctx, wasm_func->func_type, jit_func_type, llvm_func_idx, llvm_param_values, llvm_ret_values, func_idx - import_func_count);
    }
    for (i = 0; i < result_count; i++)
        PUSH(llvm_ret_values[i]);
    return true;
fail:
    if (param_types)
        wasm_runtime_free(param_types);
    if (llvm_param_values)
        wasm_runtime_free(llvm_param_values);
    if (llvm_ret_values)
        wasm_runtime_free(llvm_ret_values);
    return ret;
}

bool wasm_jit_compile_op_call_indirect(WASMModule *wasm_module, JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                       uint32 type_idx, uint32 tbl_idx)
{
    WASMType *wasm_type;
    LLVMBuilderRef builder = comp_ctx->builder;
    LLVMValueRef llvm_elem_idx, llvm_table_elem, llvm_func_idx;
    LLVMValueRef ftype_idx_ptr, ftype_idx, ftype_idx_const, llvm_import_func_count;
    LLVMValueRef cmp_elem_idx, cmp_func_idx, cmp_ftype_idx;
    LLVMValueRef table_size_const;
    LLVMValueRef ext_ret_offset, ext_ret_ptr;
    LLVMValueRef *llvm_param_values = NULL, *llvm_ret_values = NULL;
    LLVMValueRef *result_phis = NULL;
    LLVMTypeRef *llvm_param_types = NULL;
    LLVMTypeRef ext_ret_ptr_type;
    LLVMBasicBlockRef check_elem_idx_succ, check_ftype_idx_succ;
    LLVMBasicBlockRef check_func_idx_succ, block_return, block_curr;
    LLVMBasicBlockRef block_call_import, block_call_non_import;
    LLVMValueRef llvm_offset;
    LLVMValueRef llvm_tables_base_addr = func_ctx->tables_base_addr;
    JITFuncType *jit_func_type;
    uint32 total_param_count, param_count, result_count;
    uint32 ext_cell_num, i, j;
    uint8 *wasm_result_types;
    uint64 total_size;
    char buf[32];
    bool ret = false;

    wasm_type = wasm_module->types[type_idx];
    jit_func_type = comp_ctx->jit_func_types + type_idx;
    wasm_result_types = wasm_type->result;
    param_count = wasm_type->param_count;
    result_count = wasm_type->result_count;

    type_idx = wasm_get_cur_type_idx(wasm_module->types, wasm_type);

    ftype_idx_const = I32_CONST(type_idx);

    POP_I32(llvm_elem_idx);

    if (!(llvm_offset = I32_CONST(get_tbl_inst_offset(tbl_idx) + offsetof(WASMTable, cur_size))))
    {
        HANDLE_FAILURE("LLVMConstInt");
        goto fail;
    }

    LLVMBuildGEP(table_size_const, INT8_TYPE,
                 llvm_tables_base_addr, llvm_offset, "cur_size_i8p");

    if (!(table_size_const =
              LLVMBuildBitCast(comp_ctx->builder, table_size_const,
                               I32_TYPE_PTR, "cur_siuze_i32p")))
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

    if (!(cmp_elem_idx = LLVMBuildICmp(comp_ctx->builder, LLVMIntUGE, llvm_elem_idx,
                                       table_size_const, "cmp_elem_idx")))
    {
        wasm_jit_set_last_error("llvm build icmp failed.");
        goto fail;
    }

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

    if (!(llvm_offset = I32_CONST(get_tbl_inst_offset(tbl_idx) + offsetof(WASMTable, table_data))))
    {
        HANDLE_FAILURE("LLVMConstInt");
        goto fail;
    }

    LLVMBuildGEP(llvm_table_elem, INT8_TYPE,
                 llvm_tables_base_addr, llvm_offset,
                 "table_elem_i8p");

    llvm_table_elem = LLVMBuildBitCast(comp_ctx->builder, llvm_table_elem,
                                       INT8_PPTR_TYPE, "table_data_ptr");

    llvm_table_elem = LLVMBuildLoad2(comp_ctx->builder, INT8_TYPE_PTR, llvm_table_elem,
                                     "table_data_base");

    if (!(llvm_table_elem = LLVMBuildBitCast(comp_ctx->builder, llvm_table_elem,
                                             I32_TYPE_PTR, "table_elem_i32p")))
    {
        HANDLE_FAILURE("LLVMBuildBitCast");
        goto fail;
    }

    LLVMBuildGEP(llvm_table_elem, I32_TYPE, llvm_table_elem,
                 llvm_elem_idx, "table_elem");

    if (!(llvm_func_idx = LLVMBuildLoad2(comp_ctx->builder, I32_TYPE, llvm_table_elem,
                                         "func_idx")))
    {
        wasm_jit_set_last_error("llvm build load failed.");
        goto fail;
    }

    if (!(cmp_func_idx = LLVMBuildICmp(comp_ctx->builder, LLVMIntEQ, llvm_func_idx,
                                       I32_NEG_ONE, "cmp_func_idx")))
    {
        wasm_jit_set_last_error("llvm build icmp failed.");
        goto fail;
    }

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

    LLVMBuildGEP(ftype_idx_ptr, I32_TYPE, func_ctx->func_type_indexes,
                 llvm_func_idx, "ftype_idx_ptr");

    if (!(ftype_idx = LLVMBuildLoad2(comp_ctx->builder, I32_TYPE, ftype_idx_ptr,
                                     "ftype_idx")))
    {
        wasm_jit_set_last_error("llvm build load failed.");
        goto fail;
    }

    if (!(cmp_ftype_idx = LLVMBuildICmp(comp_ctx->builder, LLVMIntNE, ftype_idx,
                                        ftype_idx_const, "cmp_ftype_idx")))
    {
        wasm_jit_set_last_error("llvm build icmp failed.");
        goto fail;
    }

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

    total_param_count = 1 + param_count;

    if (result_count > 1)
        total_param_count += result_count - 1;

    llvm_param_types = jit_func_type->llvm_param_types;

    total_size = sizeof(LLVMValueRef) * (uint64)total_param_count;
    if (!(llvm_param_values = wasm_runtime_malloc(total_size)))
    {
        wasm_jit_set_last_error("allocate memory failed.");
        goto fail;
    }

    j = 0;
    llvm_param_values[j++] = func_ctx->exec_env;

    for (i = param_count - 1; (int32)i >= 0; i--)
        POP(llvm_param_values[i + j]);

    ext_cell_num = 0;
    for (i = 1; i < result_count; i++)
    {
        ext_ret_offset = I32_CONST(ext_cell_num);

        snprintf(buf, sizeof(buf), "ext_ret%d_ptr", i - 1);
        LLVMBuildGEP(ext_ret_ptr, I32_TYPE,
                     func_ctx->argv_buf,
                     ext_ret_offset, buf);
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

    if (!(cmp_func_idx = LLVMBuildICmp(comp_ctx->builder, LLVMIntULT, llvm_func_idx,
                                       llvm_import_func_count, "cmp_func_idx")))
    {
        wasm_jit_set_last_error("llvm build icmp failed.");
        goto fail;
    }

    if (!LLVMBuildCondBr(comp_ctx->builder, cmp_func_idx, block_call_import,
                         block_call_non_import))
    {
        wasm_jit_set_last_error("llvm build cond br failed.");
        goto fail;
    }

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

    LLVMPositionBuilderAtEnd(comp_ctx->builder, block_call_import);

    if (result_count > 0)
    {
        total_size = sizeof(LLVMValueRef) * (uint64)result_count;
        if (!(llvm_ret_values = wasm_runtime_malloc(total_size)))
        {
            wasm_jit_set_last_error("allocate memory failed.");
            goto fail;
        }
    }

    jit_call_indirect(comp_ctx, func_ctx, wasm_type, jit_func_type, llvm_func_idx, llvm_param_values, llvm_ret_values);

    block_curr = LLVMGetInsertBlock(comp_ctx->builder);
    for (i = 0; i < result_count; i++)
    {
        LLVMAddIncoming(result_phis[i], &llvm_ret_values[i], &block_curr, 1);
    }

    if (!LLVMBuildBr(comp_ctx->builder, block_return))
    {
        wasm_jit_set_last_error("llvm build br failed.");
        goto fail;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, block_call_non_import);

    jit_call_direct(comp_ctx, func_ctx, wasm_type, jit_func_type, llvm_func_idx, llvm_param_values, llvm_ret_values, -1);

    block_curr = LLVMGetInsertBlock(comp_ctx->builder);
    for (i = 0; i < result_count; i++)
    {
        LLVMAddIncoming(result_phis[i], &llvm_ret_values[i], &block_curr, 1);
    }

    if (!LLVMBuildBr(comp_ctx->builder, block_return))
    {
        wasm_jit_set_last_error("llvm build br failed.");
        goto fail;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, block_return);

    for (i = 0; i < result_count; i++)
    {
        PUSH(result_phis[i]);
    }

    ret = true;

fail:
    if (llvm_param_values)
        wasm_runtime_free(llvm_param_values);
    if (llvm_ret_values)
        wasm_runtime_free(llvm_ret_values);
    if (result_phis)
        wasm_runtime_free(result_phis);
    return ret;
}