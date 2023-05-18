#ifndef _WASM_TYPE_H
#define _WASM_TYPE_H

#include "platform.h"

#define VALUE_TYPE_I32 0x7F
#define VALUE_TYPE_I64 0X7E
#define VALUE_TYPE_F32 0x7D
#define VALUE_TYPE_F64 0x7C
#define VALUE_TYPE_FUNCREF 0x70
#define VALUE_TYPE_VOID 0x40

// 使用其他全局变量初始化的全局变量
#define VALUE_TYPE_GLOBAL 0x43

#define DEFAULT_NUM_BYTES_PER_PAGE 65536
#define DEFAULT_MAX_PAGES 65536

#define NULL_REF (0xFFFFFFFF)

#define TABLE_MAX_SIZE (1024)

#define INIT_EXPR_TYPE_I32_CONST 0x41
#define INIT_EXPR_TYPE_I64_CONST 0x42
#define INIT_EXPR_TYPE_F32_CONST 0x43
#define INIT_EXPR_TYPE_F64_CONST 0x44
#define INIT_EXPR_TYPE_V128_CONST 0xFD
#define INIT_EXPR_TYPE_FUNCREF_CONST 0xD2
#define INIT_EXPR_TYPE_REFNULL_CONST 0xD0
#define INIT_EXPR_TYPE_GET_GLOBAL 0x23
#define INIT_EXPR_TYPE_ERROR 0xff

#define WASM_MAGIC_NUMBER 0x6d736100
#define WASM_CURRENT_VERSION 1

#define SECTION_TYPE_USER 0
#define SECTION_TYPE_TYPE 1
#define SECTION_TYPE_IMPORT 2
#define SECTION_TYPE_FUNCTION 3
#define SECTION_TYPE_TABLE 4
#define SECTION_TYPE_MEMORY 5
#define SECTION_TYPE_GLOBAL 6
#define SECTION_TYPE_EXPORT 7
#define SECTION_TYPE_START 8
#define SECTION_TYPE_ELEMENT 9
#define SECTION_TYPE_CODE 10
#define SECTION_TYPE_DATA 11
#define SECTION_TYPE_DATACOUNT 12

#define IMPORT_KIND_FUNC 0
#define IMPORT_KIND_TABLE 1
#define IMPORT_KIND_MEMORY 2
#define IMPORT_KIND_GLOBAL 3

#define EXPORT_KIND_FUNC 0
#define EXPORT_KIND_TABLE 1
#define EXPORT_KIND_MEMORY 2
#define EXPORT_KIND_GLOBAL 3

#define LABEL_TYPE_BLOCK 0
#define LABEL_TYPE_LOOP 1
#define LABEL_TYPE_IF 2
#define LABEL_TYPE_FUNCTION 3

typedef enum
{
    Wasm_Func = 0,
    Native_Func,
    External_Func
} FuncKind;

typedef enum
{
    Load = 0,
    Validate,
    Instantiate,
    Execute
} WASMModuleStage;

typedef union WASMValue
{
    int32 i32;
    uint32 u32;
    uint32 global_index;
    uint32 ref_index;
    int64 i64;
    uint64 u64;
    float32 f32;
    float64 f64;
    uintptr_t addr;
} WASMValue;

typedef struct InitializerExpression
{
    uint8 init_expr_type;
    WASMValue u;
} InitializerExpression;

typedef struct WASMType
{
    uint8 *param;
    uint8 *result;
    uint16 param_count;
    uint16 result_count;
    uint16 param_cell_num;
    uint16 ret_cell_num;
    uint16 ref_count;
} WASMType;

typedef struct WASMExport
{
    char *name;
    uint8 kind;
    uint32 index;
} WASMExport;

typedef struct WASMElement
{
    uint32 mode;
    uint32 elem_type;
    bool is_dropped;
    uint32 table_index;
    InitializerExpression base_offset;
    uint32 function_count;
    uint32 *func_indexes;
} WASMElement;

typedef struct WASMDataSeg
{
    uint32 memory_index;
    InitializerExpression base_offset;
    uint32 data_length;
    uint8 *data;
} WASMDataSeg;

typedef struct WASMBranchTable
{
    uint8 *ip;
    uint32 idx;
    uint32 push;
    uint32 pop;
} WASMBranchTable;

typedef enum WASMExceptionID
{
    EXCE_UNREACHABLE = 0,
    EXCE_OUT_OF_MEMORY,
    EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS,
    EXCE_INTEGER_OVERFLOW,
    EXCE_INTEGER_DIVIDE_BY_ZERO,
    EXCE_INVALID_CONVERSION_TO_INTEGER,
    EXCE_INVALID_FUNCTION_TYPE_INDEX,
    EXCE_INVALID_FUNCTION_INDEX,
    EXCE_UNDEFINED_ELEMENT,
    EXCE_UNINITIALIZED_ELEMENT,
    EXCE_CALL_UNLINKED_IMPORT_FUNC,
    EXCE_NATIVE_STACK_OVERFLOW,
    EXCE_UNALIGNED_ATOMIC,
    EXCE_AUX_STACK_OVERFLOW,
    EXCE_AUX_STACK_UNDERFLOW,
    EXCE_OUT_OF_BOUNDS_TABLE_ACCESS,
    EXCE_OPERAND_STACK_OVERFLOW,
    EXCE_FAILED_TO_COMPILE_FAST_JIT_FUNC,
    EXCE_ALREADY_THROWN,
    EXCE_NUM,
} WASMExceptionID;

typedef union
{
    uint64 u64;
    uint32 u32[2];
} MemBound;

typedef struct WASMMemory
{
    /* Number bytes per page */
    uint32 num_bytes_per_page;
    /* Current page count */
    uint32 cur_page_count;
    /* Maximum page count */
    uint32 max_page_count;
    /* Memory data size */
    uint32 memory_data_size;

    uint8 *memory_data;
    /* Memory data end address */
    uint8 *memory_data_end;

} WASMMemory, WASMMemoryImport;

typedef struct WASMTable
{
    /* Current size */
    uint32 cur_size;
    /* Maximum size */
    uint32 max_size;

    bool possible_grow;

    uint8 elem_type;

    /* Table elements */
    uint32 *table_data;
} WASMTable, WASMTableImport;

typedef struct WASMGlobal
{
    uint8 type;
    bool is_mutable;
    uint32 data_offset;
    WASMValue initial_value;
} WASMGlobal, WASMGlobalImport;

#if WASM_ENABLE_JIT != 0
typedef struct WASMBlock
{
    struct WASMBlock *next_block;
    struct WASMBlock *pre_block;
    uint8 *end_addr;
    uint8 *else_addr;
    uint32 stack_num;
    bool is_set;
} WASMBlock;

typedef struct ExtInfo
{
    struct ExtInfo *next_op;
    uint32 idx;
} ExtInfo;
#endif

typedef struct WASMFunction
{

    const char *module_name;
    const char *field_name;
    const char *signature;

    WASMType *func_type;
    uint32 type_index;

    void *func_ptr;
    FuncKind func_kind;

    uint8 *code_end;

    // 参数数量
    uint16 param_count;
    // 返回值数量
    uint16 result_count;

    uint16 local_count;
    uint16 param_cell_num;
    uint16 ret_cell_num;
    uint16 local_cell_num;
    uint16 *local_offsets;
    uint8 *param_types;
    uint8 *local_types;
    uint8 *result_types;
    uint32 max_stack_cell_num;
    uint32 max_block_num;
    uint32 max_stack_num;
    WASMBranchTable *branch_table;

#if WASM_ENABLE_JIT
    bool has_memory_operations;
    bool has_op_memory;
    bool has_op_func_call;
    bool has_op_call_indirect;
    // 用于记录JIT需要使用的block数据
    WASMBlock *blocks;
    WASMBlock *last_block;
    // 用于记录重写指令的数据
    ExtInfo *op_info;
    ExtInfo *last_op_info;
#endif
} WASMFunctionImport, WASMFunction;

typedef struct WASMExportFuncInstance
{
    char *name;
    WASMFunction *function;
} WASMExportFuncInstance;

typedef struct WASMExportGlobInstance
{
    char *name;
    WASMGlobal *global;
} WASMExportGlobInstance;

typedef struct WASMExportTabInstance
{
    char *name;
    WASMTable *table;
} WASMExportTabInstance;

typedef struct WASMExportMemInstance
{
    char *name;
    WASMMemory *memory;
} WASMExportMemInstance;

struct WASIContext;
typedef struct WASIContext WASIContext;

#if WASM_ENABLE_JIT != 0
struct WASMModule;
struct JITCompContext;

/* Orc JIT thread arguments */
typedef struct OrcJitThreadArg
{
#if WASM_ENABLE_JIT != 0
    struct JITCompContext *comp_ctx;
#endif
    struct WASMModule *module;
    uint32 group_idx;
} OrcJitThreadArg;
#endif

typedef struct WASMModule
{
    // 各种类型
    WASMMemory memories[1];
    WASMType **types;
    WASMFunction *functions;
    WASMTable *tables;
    WASMGlobal *globals;
    WASMExport *exports;
    WASMElement *elements;
    WASMDataSeg *data_segments;
    uint32 start_function;

    WASMModuleStage module_stage;

    // 各种类型的数量
    uint32 type_count;
    uint32 function_count;
    uint32 table_count;
    uint32 memory_count;
    uint32 global_count;
    uint32 export_count;
    uint32 element_count;
    uint32 data_seg_count;
    uint32 data_seg_count1;

    // 各种导入的数量
    uint32 import_function_count;
    uint32 import_table_count;
    uint32 import_memory_count;
    uint32 import_global_count;

    // 默认的栈大小
    uint32 default_value_stack_size;
    uint32 default_execution_stack_size;

    // 全局数据
    uint8 *global_data;

    char cur_exception[EXCEPTION_BUF_LEN];

    // WASI
    WASIContext *wasi_ctx;

    uint32 export_func_count;
    WASMExportFuncInstance *export_functions;

#if WASM_ENABLE_JIT != 0
    bool has_op_memory_grow;
    /* backend compilation threads */
    korp_tid orcjit_threads[WASM_ORC_JIT_BACKEND_THREAD_NUM];
    /* backend thread arguments */
    OrcJitThreadArg orcjit_thread_args[WASM_ORC_JIT_BACKEND_THREAD_NUM];
    /* whether to stop the compilation of backend threads */
    bool orcjit_stop_compiling;
    struct JITCompContext *comp_ctx;
    /**
     * func pointers of LLVM JITed (un-imported) functions
     * for non Multi-Tier JIT mode:
     *   each pointer is set to the lookuped llvm jit func ptr, note that it
     *   is a stub and will trigger the actual compilation when it is called
     * for Multi-Tier JIT mode:
     *   each pointer is inited as call_to_fast_jit code block, when the llvm
     *   jit func ptr is actually compiled, it is set to the compiled llvm jit
     *   func ptr
     */
    void **func_ptrs;
    /* whether the func pointers are compiled */
    bool *func_ptrs_compiled;
    uint32 *func_type_indexes;
#endif
} WASMModule;

inline static unsigned
align_uint(unsigned v, unsigned b)
{
    unsigned m = b - 1;
    return (v + m) & ~m;
}

// 返回value type的字节数
inline static uint32
wasm_value_type_size(uint8 value_type)
{
    switch (value_type)
    {
    case VALUE_TYPE_I32:
    case VALUE_TYPE_F32:
        return sizeof(int32);
    case VALUE_TYPE_I64:
    case VALUE_TYPE_F64:
        return sizeof(int64);
    case VALUE_TYPE_VOID:
        return 0;
    }
    return 0;
}

inline static uint16
wasm_value_type_cell_num(uint8 value_type)
{
    switch (value_type)
    {
    case VALUE_TYPE_I32:
    case VALUE_TYPE_F32:
        return 1;
    case VALUE_TYPE_I64:
    case VALUE_TYPE_F64:
        return 2;
    case VALUE_TYPE_VOID:
        return 0;
    }
    return 0;
}

inline static uint32
wasm_get_cell_num(const uint8 *types, uint32 type_count)
{
    uint32 cell_num = 0;
    uint32 i;
    for (i = 0; i < type_count; i++)
        cell_num += wasm_value_type_cell_num(types[i]);
    return cell_num;
}

// 检查类型是否为value
static inline bool
is_value_type(uint8 type)
{
    if (type == VALUE_TYPE_I32 || type == VALUE_TYPE_I64 || type == VALUE_TYPE_F32 || type == VALUE_TYPE_F64)
        return true;
    return false;
}

inline static uint32
wasm_get_cur_type_idx(WASMType **types,
                      WASMType *cur_type)
{
    uint32 i;

    for (i = 0;; i++)
    {
        if (cur_type == types[i])
            return i;
    }
    return -1;
}

#endif