#ifndef _WASM_TYPE_H
#define _WASM_TYPE_H

#include"platform.h"

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

typedef struct WASMTable {
    uint32 init_size;
    /* specified if (flags & 1), else it is 0x10000 */
    uint32 max_size;
    uint8 elem_type;
    uint8 flags;
    bool possible_grow;
} WASMTable;

typedef struct WASMMemory {
    uint8 flags;
    uint32 num_bytes_per_page;
    uint32 init_page_count;
    uint32 max_page_count;
} WASMMemory;

typedef struct WASMTableImport {
    uint8 elem_type;
    uint32 flags;
    uint32 init_size;
    /* specified if (flags & 1), else it is 0x10000 */
    uint32 max_size;
    bool possible_grow;
} WASMTableImport;

typedef struct WASMMemoryImport {
    uint32 flags;
    uint32 num_bytes_per_page;
    uint32 init_page_count;
    uint32 max_page_count;
} WASMMemoryImport;

typedef struct WASMFunctionImport {
    /* function type */
    WASMType *func_type;
    /* native function pointer after linked */
    void *func_ptr_linked;
    /* signature from registered native symbols */
    const char *signature;
    /* attachment */
    void *attachment;
    bool call_conv_raw;
    bool call_conv_wasm_c_api;
} WASMFunctionImport;

typedef struct WASMGlobalImport {
    uint8 type;
    bool is_mutable;
    WASMValue global_data_linked;
    bool is_linked;
} WASMGlobalImport;

typedef struct WASMImport {
    uint8 kind;
    char *module_name;
    char *field_name;
    union {
        WASMFunctionImport function;
        WASMTableImport table;
        WASMMemoryImport memory;
        WASMGlobalImport global;
    } u;
} WASMImport;

typedef struct WASMLocal {
    uint32 count;
    uint8 type;
}WASMLocal;

typedef struct WASMFunction {
    /* the type of function */
    WASMType *func_type;
    uint32 local_count;
    
    WASMLocal* local_entry;

    /* cell num of parameters */
    uint16 param_cell_num;
    /* cell num of return type */
    uint16 ret_cell_num;
    /* cell num of local variables */
    uint16 local_cell_num;
    /* offset of each local, including function parameters
       and local variables */
    uint16 *local_offsets;

    uint32 max_stack_cell_num;
    uint32 max_block_num;
    uint32 code_size;
    uint8 *code;

}WASMFunction;

typedef struct WASMGlobal {
    uint8 type;
    bool is_mutable;
    InitializerExpression init_expr;
}WASMGlobal;

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

#if WASM_ENABLE_LIBC_WASI != 0
typedef struct WASIArguments {
    const char **dir_list;
    uint32 dir_count;
    const char **map_dir_list;
    uint32 map_dir_count;
    const char **env;
    uint32 env_count;
    /* in CIDR noation */
    const char **addr_pool;
    uint32 addr_count;
    const char **ns_lookup_pool;
    uint32 ns_lookup_count;
    char **argv;
    uint32 argc;
    int stdio[3];
} WASIArguments;
#endif

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

#if WASM_ENABLE_DEBUG_INTERP != 0
typedef struct WASMFastOPCodeNode {
    struct WASMFastOPCodeNode *next;
    uint64 offset;
    uint8 orig_op;
} WASMFastOPCodeNode;
#endif

#if WASM_ENABLE_LOAD_CUSTOM_SECTION != 0
typedef struct WASMCustomSection {
    struct WASMCustomSection *next;
    /* Start address of the section name */
    char *name_addr;
    /* Length of the section name decoded from leb */
    uint32 name_len;
    /* Start address of the content (name len and name skipped) */
    uint8 *content_addr;
    uint32 content_len;
} WASMCustomSection;
#endif

#if WASM_ENABLE_FAST_JIT != 0 || WASM_ENABLE_JIT != 0
struct AOTCompData;
struct AOTCompContext;

/* Orc JIT thread arguments */
typedef struct OrcJitThreadArg {
#if WASM_ENABLE_JIT != 0
    struct AOTCompContext *comp_ctx;
#endif
    struct WASMModule *module;
    uint32 group_idx;
} OrcJitThreadArg;
#endif

struct WASMModuleInstance;

typedef struct WASMModule {
    uint32 module_type;

    uint32 type_count;
    uint32 import_count;
    uint32 function_count;
    uint32 table_count;
    uint32 memory_count;
    uint32 global_count;
    uint32 export_count;
    uint32 element_count;
    /* data seg count read from data segment section */
    uint32 data_seg_count;

    uint32 import_function_count;
    uint32 import_table_count;
    uint32 import_memory_count;
    uint32 import_global_count;

    WASMImport *import_functions;
    WASMImport *import_tables;
    WASMImport *import_memories;
    WASMImport *import_globals;

    WASMType **types;
    WASMImport *imports;
    WASMFunction *functions;
    WASMTable *tables;
    WASMMemory *memories;
    WASMGlobal *globals;
    WASMExport *exports;
    WASMElement *elements;
    WASMDataSeg *data_segments;
    uint32 start_function;

    /* total global variable size */
    uint32 global_data_size;

    /* the index of malloc/free function,
       -1 means unexported */
    uint32 malloc_function;
    uint32 free_function;

    /* the index of __retain function,
       -1 means unexported */
    uint32 retain_function;

    /* Whether there is possible memory grow, e.g. memory.grow opcode */
    bool possible_memory_grow;

    StringList const_str_list;
}WASMModule;

typedef struct BlockType {
    /* Block type may be expressed in one of two forms:
     * either by the type of the single return value or
     * by a type index of module.
     */
    union {
        uint8 value_type;
        WASMType *type;
    } u;
    bool is_value_type;
} BlockType;

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

#if WASM_ENABLE_REF_TYPES != 0
inline static uint16
wasm_value_type_cell_num_outside(uint8 value_type)
{
    if (VALUE_TYPE_EXTERNREF == value_type) {
        return sizeof(uintptr_t) / sizeof(uint32);
    }
    else {
        return wasm_value_type_cell_num(value_type);
    }
}
#endif

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