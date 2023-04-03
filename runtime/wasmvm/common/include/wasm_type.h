#ifndef _WASM_TYPE_H
#define _WASM_TYPE_H

#include "platform.h"
#include "bh_list.h"

/** Value Type */
#define VALUE_TYPE_I32 0x7F
#define VALUE_TYPE_I64 0X7E
#define VALUE_TYPE_F32 0x7D
#define VALUE_TYPE_F64 0x7C
#define VALUE_TYPE_V128 0x7B
#define VALUE_TYPE_FUNCREF 0x70
#define VALUE_TYPE_EXTERNREF 0x6F
#define VALUE_TYPE_VOID 0x40
/* Used by AOT */
#define VALUE_TYPE_I1 0x41
/*  Used by loader to represent any type of i32/i64/f32/f64 */
#define VALUE_TYPE_ANY 0x42

//使用其他全局变量初始化的全局变量
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
/* = WASM_OP_REF_FUNC */
#define INIT_EXPR_TYPE_FUNCREF_CONST 0xD2
/* = WASM_OP_REF_NULL */
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
#if WASM_ENABLE_BULK_MEMORY != 0
#define SECTION_TYPE_DATACOUNT 12
#endif

#define SUB_SECTION_TYPE_MODULE 0
#define SUB_SECTION_TYPE_FUNC 1
#define SUB_SECTION_TYPE_LOCAL 2

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

typedef enum {
    Wasm_Module_Bytecode = 0,
    Wasm_Module_AoT,
    Package_Type_Unknown = 0xFFFF
} package_type_t;

typedef enum {
    Wasm_Func = 0,
    Native_Func,
    External_Func
}FuncKind;

typedef enum {
    Load = 0,
    Validate,
    Instantiate
}Stage;

typedef union V128 {
    int8 i8x16[16];
    int16 i16x8[8];
    int32 i32x8[4];
    int64 i64x2[2];
    float32 f32x4[4];
    float64 f64x2[2];
} V128;

typedef union WASMValue {
    int32 i32;
    uint32 u32;
    uint32 global_index;
    uint32 ref_index;
    int64 i64;
    uint64 u64;
    float32 f32;
    float64 f64;
    uintptr_t addr;
    V128 v128;
} WASMValue;

typedef struct InitializerExpression {
    uint8 init_expr_type;
    WASMValue u;
} InitializerExpression;

typedef struct WASMType {
    uint8 *param;
    uint8 *result; 
    uint16 param_count;
    uint16 result_count;
    uint16 param_cell_num;
    uint16 ret_cell_num;
    uint16 ref_count;
} WASMType;

typedef struct WASMExport {
    char *name;
    uint8 kind;
    uint32 index;
} WASMExport;

typedef struct WASMElement {
    /* 0 to 7 */
    uint32 mode;
    uint32 elem_type;
    bool is_dropped;
    uint32 table_index;
    InitializerExpression base_offset;
    uint32 function_count;
    uint32 *func_indexes;
} WASMElement;

typedef struct WASMDataSeg {
    uint32 memory_index;
    InitializerExpression base_offset;
    uint32 data_length;
    uint8 *data;
} WASMDataSeg;

typedef struct BlockAddr {
    const uint8 *start_addr;
    uint8 *else_addr;
    uint8 *end_addr;
} BlockAddr;

typedef struct StringNode {
    struct StringNode *next;
    char *str;
} StringNode, *StringList;

typedef struct BrTableCache {
    struct BrTableCache *next;
    /* Address of br_table opcode */
    uint8 *br_table_op_addr;
    uint32 br_count;
    uint32 br_depths[1];
} BrTableCache;

/**
 * When LLVM JIT, WAMR compiler or AOT is enabled, we should ensure that
 * some offsets of the same field in the interpreter module instance and
 * aot module instance are the same, so that the LLVM JITed/AOTed code
 * can smoothly access the interpreter module instance.
 * Same for the memory instance and table instance.
 * We use the macro DefPointer to define some related pointer fields.
 */
#if (WASM_ENABLE_JIT != 0 || WASM_ENABLE_WAMR_COMPILER != 0 \
     || WASM_ENABLE_AOT != 0)                               \
    && UINTPTR_MAX == UINT32_MAX
/* Add u32 padding if LLVM JIT, WAMR compiler or AOT is enabled on
   32-bit platform */
#define DefPointer(type, field) \
    type field;                 \
    uint32 field##_padding
#else
#define DefPointer(type, field) type field
#endif

typedef enum WASMExceptionID {
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

typedef union {
    uint64 u64;
    uint32 u32[2];
} MemBound;

typedef struct WASMMemory {
    /* Number bytes per page */
    uint32 num_bytes_per_page;
    /* Current page count */
    uint32 cur_page_count;
    /* Maximum page count */
    uint32 max_page_count;
    /* Memory data size */
    uint32 memory_data_size;

    uint8 * memory_data;
    /* Memory data end address */
    uint8 * memory_data_end;

    /* Heap data base address */
    uint8 * heap_data;
    /* Heap data end address */
    uint8 * heap_data_end;
    /* The heap created */
    void * heap_handle;

}WASMMemory, WASMMemoryImport;

typedef struct WASMTable {
    /* Current size */
    uint32 cur_size;
    /* Maximum size */
    uint32 max_size;

    bool possible_grow;

    uint8 elem_type;

    /* Table elements */
    uint32 * table_data;
}WASMTable, WASMTableImport;

typedef struct WASMGlobal {
    /* value type, VALUE_TYPE_I32/I64/F32/F64 */
    uint8 type;
    /* mutable or constant */
    bool is_mutable;
    /* data offset to base_addr of WASMMemoryInstance */
    uint32 data_offset;
    /* initial value */
    WASMValue initial_value;
}WASMGlobal, WASMGlobalImport;

typedef struct WASMFunction {

    const char *module_name;
    const char *field_name;
    /* signature from registered native symbols */
    const char *signature;
    /* attachment */
    void *attachment;
    
    WASMType *func_type;

    void *func_ptr;
    FuncKind func_kind;

    uint8 *code_end;
    
    //参数数量
    uint16 param_count;
    //返回值数量
    uint16 result_count;
    
    uint16 local_count;
    /* cell num of parameters */
    uint16 param_cell_num;
    /* cell num of return type */
    uint16 ret_cell_num;
    /* cell num of local variables, 0 for import function */
    uint16 local_cell_num;
    uint16 *local_offsets;
    /* parameter types */
    uint8 *param_types;
    /* local types, NULL for import function */
    uint8 *local_types;
    uint8 *result_types;
    uint32 max_stack_cell_num;
    uint32 max_block_num;
}WASMFunctionImport, WASMFunction;

typedef struct WASMExportFuncInstance {
    char *name;
    WASMFunction *function;
} WASMExportFuncInstance;

typedef struct WASMExportGlobInstance {
    char *name;
    WASMGlobal *global;
} WASMExportGlobInstance;

typedef struct WASMExportTabInstance {
    char *name;
    WASMTable *table;
} WASMExportTabInstance;

typedef struct WASMExportMemInstance {
    char *name;
    WASMMemory *memory;
} WASMExportMemInstance;

/* wasm-c-api import function info */
typedef struct CApiFuncImport {
    /* host func pointer after linked */
    void *func_ptr_linked;
    /* whether the host func has env argument */
    bool with_env_arg;
    /* the env argument of the host func */
    void *env_arg;
} CApiFuncImport;

struct WASMExecEnv;
struct WASIContext;
typedef struct WASMExecEnv WASMExecEnv;
typedef struct WASIContext WASIContext;

typedef struct WASMModule {
    //module的类型
    uint32 module_type;

    //各种类型的数量
    uint32 type_count;
    uint32 function_count;
    uint32 table_count;
    uint32 memory_count;
    uint32 global_count;
    uint32 export_count;
    uint32 element_count;
    uint32 data_seg_count;
#if WASM_ENABLE_BULK_MEMORY != 0
    uint32 data_seg_count1;
#endif

    //各种导入的数量
    uint32 import_function_count;
    uint32 import_table_count;
    uint32 import_memory_count;
    uint32 import_global_count;

    //各种类型
    WASMType **types;
    WASMFunction *functions;
    WASMTable *tables;
    WASMMemory *memories;
    WASMGlobal *globals;
    WASMExport *exports;
    WASMElement *elements;
    WASMDataSeg *data_segments;
    uint32 start_function;

    //默认的栈大小
    uint32 default_wasm_stack_size;

    //全局数据
    uint8 *global_data;

    char cur_exception[EXCEPTION_BUF_LEN];

    //WASI
    WASIContext *wasi_ctx;

    uint32 export_func_count;
    WASMExportFuncInstance *export_functions;

    bh_list br_table_cache_list_head;
    bh_list *br_table_cache_list;

    bool possible_memory_grow;

}WASMModule,WASMModuleInstance;

typedef struct WASMBranchBlock {
    uint8 *begin_addr;
    uint8 *target_addr;
    uint32 *frame_sp;
    uint32 cell_num;
} WASMBranchBlock;

/* Execution environment, e.g. stack info */
/**
 * Align an unsigned value on a alignment boundary.
 *
 * @param v the value to be aligned
 * @param b the alignment boundary (2, 4, 8, ...)
 *
 * @return the aligned value
 */
inline static unsigned
align_uint(unsigned v, unsigned b)
{
    unsigned m = b - 1;
    return (v + m) & ~m;
}

/**
 * Return the hash value of c string.
 */
inline static uint32
wasm_string_hash(const char *str)
{
    unsigned h = (unsigned)strlen(str);
    const uint8 *p = (uint8 *)str;
    const uint8 *end = p + h;

    while (p != end)
        h = ((h << 5) - h) + *p++;
    return h;
}

/**
 * Whether two c strings are equal.
 */
inline static bool
wasm_string_equal(const char *s1, const char *s2)
{
    return strcmp(s1, s2) == 0 ? true : false;
}

//返回value type的字节数
inline static uint32
wasm_value_type_size(uint8 value_type)
{
    switch (value_type) {
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
    return wasm_value_type_size(value_type) / 4;
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

// inline static uint32
// wasm_get_smallest_type_idx(WASMType **types, uint32 type_count,
//                            uint32 cur_type_idx)
// {
//     uint32 i;

//     for (i = 0; i < cur_type_idx; i++) {
//         if (wasm_type_equal(types[cur_type_idx], types[i]))
//             return i;
//     }
//     (void)type_count;
//     return cur_type_idx;
// }

// static inline uint32
// block_type_get_param_types(BlockType *block_type, uint8 **p_param_types)
// {
//     uint32 param_count = 0;
//     if (!block_type->is_value_type) {
//         WASMType *wasm_type = block_type->u.type;
//         *p_param_types = wasm_type->types;
//         param_count = wasm_type->param_count;
//     }
//     else {
//         *p_param_types = NULL;
//         param_count = 0;
//     }

//     return param_count;
// }

// static inline uint32
// block_type_get_result_types(BlockType *block_type, uint8 **p_result_types)
// {
//     uint32 result_count = 0;
//     if (block_type->is_value_type) {
//         if (block_type->u.value_type != VALUE_TYPE_VOID) {
//             *p_result_types = &block_type->u.value_type;
//             result_count = 1;
//         }
//     }
//     else {
//         WASMType *wasm_type = block_type->u.type;
//         *p_result_types = wasm_type->types + wasm_type->param_count;
//         result_count = wasm_type->result_count;
//     }
//     return result_count;
// }

#endif 