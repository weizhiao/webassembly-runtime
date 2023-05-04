#ifndef _WASM_JIT_LLVM_H
#define _WASM_JIT_LLVM_H

#include "wasm_jit.h"
#include "llvm/Config/llvm-config.h"
#include "llvm-c/Types.h"
#include "llvm-c/Target.h"
#include "llvm-c/Core.h"
#include "llvm-c/Object.h"
#include "llvm-c/ExecutionEngine.h"
#include "llvm-c/Analysis.h"
#include "llvm-c/BitWriter.h"
#include "llvm-c/Transforms/Utils.h"
#include "llvm-c/Transforms/Scalar.h"
#include "llvm-c/Transforms/Vectorize.h"
#include "llvm-c/Transforms/PassManagerBuilder.h"

#include "llvm-c/Orc.h"
#include "llvm-c/Error.h"
#include "llvm-c/Support.h"
#include "llvm-c/Initialization.h"
#include "llvm-c/TargetMachine.h"
#include "llvm-c/LLJIT.h"

#include "wasm_jit_orc_extra.h"
#ifdef __cplusplus
extern "C"
{
#endif

/* Opaque pointer type */
#define OPQ_PTR_TYPE INT8_PTR_TYPE

#ifndef NDEBUG
#define DEBUG_PASS
#define DUMP_MODULE
#else
#undef DEBUG_PASS
#undef DUMP_MODULE
#endif
    typedef enum IntCond
    {
        INT_EQZ = 0,
        INT_EQ,
        INT_NE,
        INT_LT_S,
        INT_LT_U,
        INT_GT_S,
        INT_GT_U,
        INT_LE_S,
        INT_LE_U,
        INT_GE_S,
        INT_GE_U
    } IntCond;

    typedef enum FloatCond
    {
        FLOAT_EQ = 0,
        FLOAT_NE,
        FLOAT_LT,
        FLOAT_GT,
        FLOAT_LE,
        FLOAT_GE,
        FLOAT_UNO
    } FloatCond;
    /**
     * Value in the WASM operation stack, each stack element
     * is an LLVM value
     */
    typedef struct JITValue
    {
        LLVMValueRef value;
    } JITValue;

    /**
     * Value stack, represents stack elements in a WASM block
     */
    typedef struct JITValueStack
    {
        JITValue *value_list_head;
        JITValue *value_list_end;
    } JITValueStack;

    typedef struct JITBlock
    {
        /* LABEL_TYPE_BLOCK/LOOP/IF/FUNCTION */
        uint32 label_type;

        // 是否在翻译else分支
        bool is_translate_else;
        bool is_polymorphic;

        uint8 *else_addr;
        uint8 *end_addr;

        /* LLVM label points to code begin */
        LLVMBasicBlockRef llvm_entry_block;
        /* LLVM label points to code else */
        LLVMBasicBlockRef llvm_else_block;
        /* LLVM label points to code end */
        LLVMBasicBlockRef llvm_end_block;

        /* Param count/types/PHIs of this block */
        uint32 param_count;
        uint32 stack_num;
        uint8 *param_types;
        LLVMValueRef *param_phis;
        LLVMValueRef *else_param_phis;

        /* Result count/types/PHIs of this block */
        uint32 result_count;
        uint8 *result_types;
        LLVMValueRef *result_phis;
    } JITBlock;

    typedef struct JITMemInfo
    {
        LLVMValueRef mem_base_addr;
        LLVMValueRef mem_data_size_addr;
        LLVMValueRef mem_cur_page_count_addr;
        LLVMValueRef mem_bound_check_1byte;
        LLVMValueRef mem_bound_check_2bytes;
        LLVMValueRef mem_bound_check_4bytes;
        LLVMValueRef mem_bound_check_8bytes;
        LLVMValueRef mem_bound_check_16bytes;
    } JITMemInfo;

    typedef struct JITFuncContext
    {
        WASMFunction *wasm_func;
        LLVMValueRef func;
        LLVMTypeRef llvm_func_type;
        /* LLVM module for this function, note that in LAZY JIT mode,
           each aot function belongs to an individual module */
        LLVMModuleRef module;

        JITBlock *block_stack;
        JITBlock *block_stack_bottom;
        JITValue *value_stack;
        JITValue *value_stack_bottom;

        LLVMValueRef exec_env;
        LLVMValueRef wasm_module;
        LLVMValueRef argv_buf;
        LLVMValueRef native_stack_bound;
        LLVMValueRef native_stack_top_min_addr;
        LLVMValueRef native_symbol;
        LLVMValueRef func_ptrs;

        JITMemInfo mem_info;
        LLVMValueRef global_base_addr;
        LLVMValueRef tables_base_addr;

        LLVMValueRef cur_exception;

        bool mem_space_unchanged;

        LLVMBasicBlockRef got_exception_block;
        LLVMBasicBlockRef func_return_block;
        LLVMValueRef exception_id_phi;
        LLVMValueRef func_type_indexes;
        LLVMValueRef locals[1];
    } JITFuncContext;

    typedef struct JITLLVMTypes
    {
        LLVMTypeRef int1_type;
        LLVMTypeRef int8_type;
        LLVMTypeRef int16_type;
        LLVMTypeRef int32_type;
        LLVMTypeRef int64_type;
        LLVMTypeRef float32_type;
        LLVMTypeRef float64_type;
        LLVMTypeRef void_type;

        LLVMTypeRef int8_ptr_type;
        LLVMTypeRef int8_pptr_type;
        LLVMTypeRef int16_ptr_type;
        LLVMTypeRef int32_ptr_type;
        LLVMTypeRef int64_ptr_type;
        LLVMTypeRef float32_ptr_type;
        LLVMTypeRef float64_ptr_type;

        LLVMTypeRef meta_data_type;
    } JITLLVMTypes;

    typedef struct JITLLVMConsts
    {
        LLVMValueRef i1_zero;
        LLVMValueRef i1_one;
        LLVMValueRef i8_zero;
        LLVMValueRef i32_zero;
        LLVMValueRef i64_zero;
        LLVMValueRef f32_zero;
        LLVMValueRef f64_zero;
        LLVMValueRef i32_one;
        LLVMValueRef i32_two;
        LLVMValueRef i32_three;
        LLVMValueRef i32_neg_one;
        LLVMValueRef i64_neg_one;
        LLVMValueRef i32_min;
        LLVMValueRef i64_min;
        LLVMValueRef i32_31;
        LLVMValueRef i32_32;
        LLVMValueRef i64_63;
        LLVMValueRef i64_64;
    } JITLLVMConsts;

    typedef struct JITFuncType
    {
        LLVMTypeRef *llvm_param_types;
        LLVMTypeRef *llvm_result_types;
        LLVMTypeRef llvm_func_type;
    } JITFuncType;

    /**
     * Compiler context
     */
    typedef struct JITCompContext
    {

        /* LLVM variables required to emit LLVM IR */
        LLVMContextRef context;
        LLVMBuilderRef builder;
        LLVMTargetMachineRef target_machine;
        char *target_cpu;
        char target_arch[16];

        /* required by JIT */
        LLVMOrcLLLazyJITRef orc_jit;
        LLVMOrcThreadSafeContextRef orc_thread_safe_context;

        LLVMModuleRef module;

        /* Disable LLVM link time optimization */
        bool disable_llvm_lto;

        uint32 opt_level;
        uint32 size_level;

        /* LLVM floating-point rounding mode metadata */
        LLVMValueRef fp_rounding_mode;

        /* LLVM floating-point exception behavior metadata */
        LLVMValueRef fp_exception_behavior;

        /* LLVM data types */
        JITLLVMTypes basic_types;
        LLVMTypeRef exec_env_type;
        LLVMTypeRef wasm_module_type;

        JITLLVMConsts llvm_consts;
        JITFuncContext **jit_func_ctxes;
        JITFuncType *jit_func_types;
        uint32 func_ctx_count;
    } JITCompContext;

    bool wasm_jit_compiler_init(void);

    void wasm_jit_compiler_destroy(void);

    JITCompContext *
    wasm_jit_create_comp_context(WASMModule *wasm_module);

    void wasm_jit_destroy_comp_context(JITCompContext *comp_ctx);

    int32 wasm_jit_get_native_symbol_index(JITCompContext *comp_ctx, const char *symbol);

    bool wasm_jit_compile_wasm(WASMModule *module);

    uint8 *
    wasm_jit_emit_elf_file(JITCompContext *comp_ctx, uint32 *p_elf_file_size);

    void wasm_jit_destroy_elf_file(uint8 *elf_file);

    void wasm_jit_block_destroy(JITBlock *block);

    LLVMTypeRef
    wasm_type_to_llvm_type(JITLLVMTypes *llvm_types, uint8 wasm_type);

    bool wasm_jit_build_zero_function_ret(JITCompContext *comp_ctx, JITFuncContext *func_ctx,
                                          WASMType *func_type);

    LLVMValueRef
    wasm_jit_call_llvm_intrinsic(const JITCompContext *comp_ctx,
                                 const JITFuncContext *func_ctx, const char *intrinsic,
                                 LLVMTypeRef ret_type, LLVMTypeRef *param_types,
                                 int param_count, ...);

    LLVMValueRef
    wasm_jit_call_llvm_intrinsic_v(const JITCompContext *comp_ctx,
                                   const JITFuncContext *func_ctx, const char *intrinsic,
                                   LLVMTypeRef ret_type, LLVMTypeRef *param_types,
                                   int param_count, va_list param_value_list);

    void wasm_jit_add_expand_memory_op_pass(LLVMPassManagerRef pass);

    void wasm_jit_add_simple_loop_unswitch_pass(LLVMPassManagerRef pass);

    void wasm_jit_apply_llvm_new_pass_manager(JITCompContext *comp_ctx, LLVMModuleRef module);

    void wasm_jit_handle_llvm_errmsg(const char *string, LLVMErrorRef err);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif
