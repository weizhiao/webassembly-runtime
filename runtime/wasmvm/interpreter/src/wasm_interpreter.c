#include "wasm_interp.h"
#include "wasm_opcode.h"
#include "wasm_native.h"
#include "wasm_memory.h"
#include "wasm_fast_readleb.h"

#define WASM_ENABLE_DEBUG_INTERP 0

#if WASM_ENABLE_DEBUG_INTERP != 0

static FILE *call_info;

void print_opcode(uint8 opcode, uint32 fidx)
{
    switch (opcode)
    {
    case WASM_OP_CALL:
        os_printf("\n#####call function %d####\n", fidx);
        fprintf(call_info, "\n#####call function %d####\n", fidx);
        break;
    case WASM_OP_BLOCK:
        os_printf("\nblock\n");
        break;
    case WASM_OP_LOOP:
        os_printf("\nloop\n");
        break;
    case WASM_OP_IF:
        os_printf("\nif\n");
        break;
    case WASM_OP_END:
        os_printf("end\n\n");
        break;
    case WASM_OP_I32_CONST:
        os_printf("opcode:i32.const\n");
        break;
    case WASM_OP_I32_LOAD:
        os_printf("opcode:i32.load\n");
        break;
    case WASM_OP_BR_IF:
        os_printf("opcode:br_if\n");
        break;
    default:
        os_printf("opcode:%d\n", opcode);
        break;
    }
}

#endif

/* For LOAD opcodes */
#define LOAD_I64(addr) (*(int64 *)(addr))
#define LOAD_F64(addr) (*(float64 *)(addr))
#define LOAD_I32(addr) (*(int32 *)(addr))
#define LOAD_U32(addr) (*(uint32 *)(addr))
#define LOAD_I16(addr) (*(int16 *)(addr))
#define LOAD_U16(addr) (*(uint16 *)(addr))

#define STORE_U32(addr, value)               \
    do                                       \
    {                                        \
        *(uint32 *)(addr) = (uint32)(value); \
    } while (0)
#define STORE_U16(addr, value)               \
    do                                       \
    {                                        \
        *(uint16 *)(addr) = (uint16)(value); \
    } while (0)

#define PUT_I32_TO_ADDR(addr, value)       \
    do                                     \
    {                                      \
        *(int32 *)(addr) = (int32)(value); \
    } while (0)
#define PUT_F32_TO_ADDR(addr, value)           \
    do                                         \
    {                                          \
        *(float32 *)(addr) = (float32)(value); \
    } while (0)
#define PUT_I64_TO_ADDR(addr, value)       \
    do                                     \
    {                                      \
        *(int64 *)(addr) = (int64)(value); \
    } while (0)
#define PUT_F64_TO_ADDR(addr, value)           \
    do                                         \
    {                                          \
        *(float64 *)(addr) = (float64)(value); \
    } while (0)

#define GET_I64_FROM_ADDR(addr) (*(int64 *)(addr))
#define GET_F64_FROM_ADDR(addr) (*(float64 *)(addr))
#define GET_I32_FROM_ADDR(addr) (*(int32 *)(addr))
#define GET_F32_FROM_ADDR(addr) (*(float32 *)addr)

#define CHECK_MEMORY_OVERFLOW(bytes)                    \
    do                                                  \
    {                                                   \
        uint64 offset1 = (uint64)offset + (uint64)addr; \
        if (offset1 + bytes <= (uint64)linear_mem_size) \
            maddr = memory->memory_data + offset1;      \
        else                                            \
            goto out_of_bounds;                         \
    } while (0)

#define CHECK_BULK_MEMORY_OVERFLOW(start, bytes, maddr) \
    do                                                  \
    {                                                   \
        uint64 offset1 = (uint32)(start);               \
        if (offset1 + bytes <= (uint64)linear_mem_size) \
            maddr = memory->memory_data + offset1;      \
        else                                            \
            goto out_of_bounds;                         \
    } while (0)

static inline uint32
rotl32(uint32 n, uint32 c)
{
    const uint32 mask = (31);
    c = c % 32;
    c &= mask;
    return (n << c) | (n >> ((0 - c) & mask));
}

static inline uint32
rotr32(uint32 n, uint32 c)
{
    const uint32 mask = (31);
    c = c % 32;
    c &= mask;
    return (n >> c) | (n << ((0 - c) & mask));
}

static inline uint64
rotl64(uint64 n, uint64 c)
{
    const uint64 mask = (63);
    c = c % 64;
    c &= mask;
    return (n << c) | (n >> ((0 - c) & mask));
}

static inline uint64
rotr64(uint64 n, uint64 c)
{
    const uint64 mask = (63);
    c = c % 64;
    c &= mask;
    return (n >> c) | (n << ((0 - c) & mask));
}

static inline float32
f32_min(float32 a, float32 b)
{
    if (isnan(a) || isnan(b))
        return NAN;
    else if (a == 0 && a == b)
        return signbit(a) ? a : b;
    else
        return a > b ? b : a;
}

static inline float32
f32_max(float32 a, float32 b)
{
    if (isnan(a) || isnan(b))
        return NAN;
    else if (a == 0 && a == b)
        return signbit(a) ? b : a;
    else
        return a > b ? a : b;
}

static inline float64
f64_min(float64 a, float64 b)
{
    if (isnan(a) || isnan(b))
        return NAN;
    else if (a == 0 && a == b)
        return signbit(a) ? a : b;
    else
        return a > b ? b : a;
}

static inline float64
f64_max(float64 a, float64 b)
{
    if (isnan(a) || isnan(b))
        return NAN;
    else if (a == 0 && a == b)
        return signbit(a) ? b : a;
    else
        return a > b ? a : b;
}

#if WASM_ENABLE_BUILTIN != 0

#define clz32 __builtin_clz
#define clz64 __builtin_clzll
#define ctz32 __builtin_ctz
#define ctz64 __builtin_ctzll
#define popcount32 __builtin_popcount
#define popcount64 __builtin_popcountll
#define local_copysign copysign
#define local_copysignf copysignf

#else

static inline uint32
clz32(uint32 type)
{
    uint32 num = 0;
    if (type == 0)
        return 32;
    while (!(type & 0x80000000))
    {
        num++;
        type <<= 1;
    }
    return num;
}

static inline uint32
clz64(uint64 type)
{
    uint32 num = 0;
    if (type == 0)
        return 64;
    while (!(type & 0x8000000000000000LL))
    {
        num++;
        type <<= 1;
    }
    return num;
}

static inline uint32
ctz32(uint32 type)
{
    uint32 num = 0;
    if (type == 0)
        return 32;
    while (!(type & 1))
    {
        num++;
        type >>= 1;
    }
    return num;
}

static inline uint32
ctz64(uint64 type)
{
    uint32 num = 0;
    if (type == 0)
        return 64;
    while (!(type & 1))
    {
        num++;
        type >>= 1;
    }
    return num;
}

static inline uint32
popcount32(uint32 u)
{
    uint32 ret = 0;
    while (u)
    {
        u = (u & (u - 1));
        ret++;
    }
    return ret;
}

static inline uint32
popcount64(uint64 u)
{
    uint32 ret = 0;
    while (u)
    {
        u = (u & (u - 1));
        ret++;
    }
    return ret;
}

static float
local_copysignf(float x, float y)
{
    union
    {
        float f;
        uint32_t i;
    } ux = {x}, uy = {y};
    ux.i &= 0x7fffffff;
    ux.i |= uy.i & 0x80000000;
    return ux.f;
}

static double
local_copysign(double x, double y)
{
    union
    {
        double f;
        uint64_t i;
    } ux = {x}, uy = {y};
    ux.i &= -1ULL / 2;
    ux.i |= uy.i & 1ULL << 63;
    return ux.f;
}

#endif

#define PUSH_I32(value)                   \
    do                                    \
    {                                     \
        PUT_I32_TO_ADDR(frame_sp, value); \
        frame_sp++;                       \
    } while (0)

#define PUSH_F32(value)                   \
    do                                    \
    {                                     \
        PUT_F32_TO_ADDR(frame_sp, value); \
        frame_sp++;                       \
    } while (0)

#define PUSH_I64(value)                   \
    do                                    \
    {                                     \
        PUT_I64_TO_ADDR(frame_sp, value); \
        frame_sp += 2;                    \
    } while (0)

#define PUSH_F64(value)                   \
    do                                    \
    {                                     \
        PUT_F64_TO_ADDR(frame_sp, value); \
        frame_sp += 2;                    \
    } while (0)

#define POP_I32() (--frame_sp, *(int32 *)frame_sp)

#define POP_F32() (--frame_sp, *(float32 *)frame_sp)

#define POP_I64() (frame_sp -= 2, GET_I64_FROM_ADDR(frame_sp))

#define POP_F64() (frame_sp -= 2, GET_F64_FROM_ADDR(frame_sp))

#define DEF_OP_I_CONST(ctype, src_op_type)              \
    do                                                  \
    {                                                   \
        ctype cval;                                     \
        read_leb_##ctype(frame_ip, frame_ip_end, cval); \
        PUSH_##src_op_type(cval);                       \
    } while (0)

#define DEF_OP_EQZ(src_op_type)             \
    do                                      \
    {                                       \
        int32 pop_val;                      \
        pop_val = POP_##src_op_type() == 0; \
        PUSH_I32(pop_val);                  \
    } while (0)

#define DEF_OP_CMP(src_type, src_op_type, cond) \
    do                                          \
    {                                           \
        uint32 res;                             \
        src_type val1, val2;                    \
        val2 = (src_type)POP_##src_op_type();   \
        val1 = (src_type)POP_##src_op_type();   \
        res = val1 cond val2;                   \
        PUSH_I32(res);                          \
    } while (0)

#define DEF_OP_BIT_COUNT(src_type, src_op_type, operation) \
    do                                                     \
    {                                                      \
        src_type val1, val2;                               \
        val1 = (src_type)POP_##src_op_type();              \
        val2 = (src_type)operation(val1);                  \
        PUSH_##src_op_type(val2);                          \
    } while (0)

#define DEF_OP_NUMERIC(src_type1, src_type2, src_op_type, operation)  \
    do                                                                \
    {                                                                 \
        frame_sp -= sizeof(src_type2) / sizeof(uint32);               \
        *(src_type1 *)(frame_sp - sizeof(src_type1) / sizeof(uint32)) \
            operation## = *(src_type2 *)(frame_sp);                   \
    } while (0)

#define DEF_OP_NUMERIC_64 DEF_OP_NUMERIC

#define DEF_OP_NUMERIC2(src_type1, src_type2, src_op_type, operation) \
    do                                                                \
    {                                                                 \
        frame_sp -= sizeof(src_type2) / sizeof(uint32);               \
        *(src_type1 *)(frame_sp - sizeof(src_type1) / sizeof(uint32)) \
            operation## = (*(src_type2 *)(frame_sp) % 32);            \
    } while (0)

#define DEF_OP_NUMERIC2_64(src_type1, src_type2, src_op_type, operation) \
    do                                                                   \
    {                                                                    \
        src_type1 val1;                                                  \
        src_type2 val2;                                                  \
        frame_sp -= 2;                                                   \
        val1 = (src_type1)GET_##src_op_type##_FROM_ADDR(frame_sp - 2);   \
        val2 = (src_type2)GET_##src_op_type##_FROM_ADDR(frame_sp);       \
        val1 operation## = (val2 % 64);                                  \
        PUT_##src_op_type##_TO_ADDR(frame_sp - 2, val1);                 \
    } while (0)

#define DEF_OP_MATH(src_type, src_op_type, method) \
    do                                             \
    {                                              \
        src_type src_val;                          \
        src_val = POP_##src_op_type();             \
        PUSH_##src_op_type(method(src_val));       \
    } while (0)

#define TRUNC_FUNCTION(func_name, src_type, dst_type, signed_type)  \
    static dst_type func_name(src_type src_value, src_type src_min, \
                              src_type src_max, dst_type dst_min,   \
                              dst_type dst_max, bool is_sign)       \
    {                                                               \
        dst_type dst_value = 0;                                     \
        if (!isnan(src_value))                                      \
        {                                                           \
            if (src_value <= src_min)                               \
                dst_value = dst_min;                                \
            else if (src_value >= src_max)                          \
                dst_value = dst_max;                                \
            else                                                    \
            {                                                       \
                if (is_sign)                                        \
                    dst_value = (dst_type)(signed_type)src_value;   \
                else                                                \
                    dst_value = (dst_type)src_value;                \
            }                                                       \
        }                                                           \
        return dst_value;                                           \
    }

TRUNC_FUNCTION(trunc_f32_to_i32, float32, uint32, int32)
TRUNC_FUNCTION(trunc_f32_to_i64, float32, uint64, int64)
TRUNC_FUNCTION(trunc_f64_to_i32, float64, uint32, int32)
TRUNC_FUNCTION(trunc_f64_to_i64, float64, uint64, int64)

static bool
trunc_f32_to_int(WASMModule *module, uint32 *frame_sp, float32 src_min,
                 float32 src_max, bool saturating, bool is_i32, bool is_sign)
{
    float32 src_value = POP_F32();
    uint64 dst_value_i64;
    uint32 dst_value_i32;

    if (!saturating)
    {
        if (isnan(src_value))
        {
            wasm_set_exception(module, "invalid conversion to integer");
            return false;
        }
        else if (src_value <= src_min || src_value >= src_max)
        {
            wasm_set_exception(module, "integer overflow");
            return false;
        }
    }

    if (is_i32)
    {
        uint32 dst_min = is_sign ? INT32_MIN : 0;
        uint32 dst_max = is_sign ? INT32_MAX : UINT32_MAX;
        dst_value_i32 = trunc_f32_to_i32(src_value, src_min, src_max, dst_min,
                                         dst_max, is_sign);
        PUSH_I32(dst_value_i32);
    }
    else
    {
        uint64 dst_min = is_sign ? INT64_MIN : 0;
        uint64 dst_max = is_sign ? INT64_MAX : UINT64_MAX;
        dst_value_i64 = trunc_f32_to_i64(src_value, src_min, src_max, dst_min,
                                         dst_max, is_sign);
        PUSH_I64(dst_value_i64);
    }
    return true;
}

static bool
trunc_f64_to_int(WASMModule *module, uint32 *frame_sp, float64 src_min,
                 float64 src_max, bool saturating, bool is_i32, bool is_sign)
{
    float64 src_value = POP_F64();
    uint64 dst_value_i64;
    uint32 dst_value_i32;

    if (!saturating)
    {
        if (isnan(src_value))
        {
            wasm_set_exception(module, "invalid conversion to integer");
            return false;
        }
        else if (src_value <= src_min || src_value >= src_max)
        {
            wasm_set_exception(module, "integer overflow");
            return false;
        }
    }

    if (is_i32)
    {
        uint32 dst_min = is_sign ? INT32_MIN : 0;
        uint32 dst_max = is_sign ? INT32_MAX : UINT32_MAX;
        dst_value_i32 = trunc_f64_to_i32(src_value, src_min, src_max, dst_min,
                                         dst_max, is_sign);
        PUSH_I32(dst_value_i32);
    }
    else
    {
        uint64 dst_min = is_sign ? INT64_MIN : 0;
        uint64 dst_max = is_sign ? INT64_MAX : UINT64_MAX;
        dst_value_i64 = trunc_f64_to_i64(src_value, src_min, src_max, dst_min,
                                         dst_max, is_sign);
        PUSH_I64(dst_value_i64);
    }
    return true;
}

#define DEF_OP_TRUNC_F32(min, max, is_i32, is_sign)                      \
    do                                                                   \
    {                                                                    \
        if (!trunc_f32_to_int(module, frame_sp, min, max, false, is_i32, \
                              is_sign))                                  \
            goto got_exception;                                          \
    } while (0)

#define DEF_OP_TRUNC_F64(min, max, is_i32, is_sign)                      \
    do                                                                   \
    {                                                                    \
        if (!trunc_f64_to_int(module, frame_sp, min, max, false, is_i32, \
                              is_sign))                                  \
            goto got_exception;                                          \
    } while (0)

#define DEF_OP_TRUNC_SAT_F32(min, max, is_i32, is_sign)                  \
    do                                                                   \
    {                                                                    \
        (void)trunc_f32_to_int(module, frame_sp, min, max, true, is_i32, \
                               is_sign);                                 \
    } while (0)

#define DEF_OP_TRUNC_SAT_F64(min, max, is_i32, is_sign)                  \
    do                                                                   \
    {                                                                    \
        (void)trunc_f64_to_int(module, frame_sp, min, max, true, is_i32, \
                               is_sign);                                 \
    } while (0)

#define DEF_OP_CONVERT(dst_type, dst_op_type, src_type, src_op_type) \
    do                                                               \
    {                                                                \
        dst_type value = (dst_type)(src_type)POP_##src_op_type();    \
        PUSH_##dst_op_type(value);                                   \
    } while (0)

#define GET_LOCAL_INDEX_TYPE_AND_OFFSET()                                \
    do                                                                   \
    {                                                                    \
        uint32 param_count = cur_func->param_count;                      \
        read_leb_uint32(frame_ip, frame_ip_end, local_idx);              \
        local_offset = cur_func->local_offsets[local_idx];               \
        if (local_idx < param_count)                                     \
            local_type = cur_func->param_types[local_idx];               \
        else                                                             \
            local_type = cur_func->local_types[local_idx - param_count]; \
    } while (0)

static inline int32
sign_ext_8_32(int8 val)
{
    if (val & 0x80)
        return (int32)val | (int32)0xffffff00;
    return val;
}

static inline int32
sign_ext_16_32(int16 val)
{
    if (val & 0x8000)
        return (int32)val | (int32)0xffff0000;
    return val;
}

static inline int64
sign_ext_8_64(int8 val)
{
    if (val & 0x80)
        return (int64)val | (int64)0xffffffffffffff00LL;
    return val;
}

static inline int64
sign_ext_16_64(int16 val)
{
    if (val & 0x8000)
        return (int64)val | (int64)0xffffffffffff0000LL;
    return val;
}

static inline int64
sign_ext_32_64(int32 val)
{
    if (val & (int32)0x80000000)
        return (int64)val | (int64)0xffffffff00000000LL;
    return val;
}

static inline void
word_copy(uint32 *dest, uint32 *src, unsigned num)
{
    if (dest != src)
    {
        /* No overlap buffer */
        for (; num > 0; num--)
            *dest++ = *src++;
    }
}

static inline WASMFuncFrame *
ALLOC_FRAME(WASMExecEnv *exec_env, WASMFuncFrame *prev_frame)
{
    if (exec_env->exectution_stack.top + sizeof(WASMFuncFrame) >= exec_env->exectution_stack.top_boundary)
    {
        wasm_set_exception(exec_env->module_inst,
                           "wasm exectution stack overflow");
        return NULL;
    }

    WASMFuncFrame *frame = (WASMFuncFrame *)exec_env->exectution_stack.top;
    frame->prev_frame = prev_frame;

    exec_env->exectution_stack.top += sizeof(WASMFuncFrame);

    return frame;
}

static inline void
FREE_FRAME(WASMExecEnv *exec_env, WASMFuncFrame *frame)
{
    exec_env->exectution_stack.top = (uint8 *)frame;
}

#define CONTROL_TRANSFER()                                       \
    do                                                           \
    {                                                            \
        uint32 push = cur_branch_table->push;                    \
        uint32 pop = cur_branch_table->pop;                      \
        frame_ip = cur_branch_table->ip;                         \
        word_copy(frame_sp - pop, frame_sp - push, push);        \
        frame_sp = frame_sp - pop + push;                        \
        cur_branch_table = branch_table + cur_branch_table->stp; \
    } while (0)

static void
wasm_interp_call_func_native(WASMExecEnv *exec_env,
                             uint32 func_idx,
                             WASMFuncFrame *prev_frame)
{
    WASMModule *module = exec_env->module_inst;
    WASMFunction *func_import = module->functions + func_idx;
    bool ret = false;

    switch (func_import->func_kind)
    {
    case Native_Func:
        ret = wasm_runtime_invoke_native(
            exec_env, func_idx, prev_frame->sp, prev_frame->sp);
        break;
    case External_Func:
        break;
    default:
        break;
    }

    if (!ret)
        return;

    if (func_import->ret_cell_num == 1)
    {
        prev_frame->sp++;
    }
    else if (func_import->ret_cell_num == 2)
    {
        prev_frame->sp += 2;
    }
}

#if WASM_ENABLE_DISPATCH != 0

#define HANDLE_OP(opcode) HANDLE_##opcode:
#define FETCH_OPCODE_AND_DISPATCH() goto *handle_table[*frame_ip++]

#define HANDLE_OP_END() FETCH_OPCODE_AND_DISPATCH()

#else /* else of WASM_ENABLE_DISPATCH */
#define HANDLE_OP(opcode) case opcode:

#if WASM_ENABLE_DEBUG_INTERP != 0
#define HANDLE_OP_END()         \
    print_opcode(opcode, fidx); \
    continue

#else
#define HANDLE_OP_END() continue
#endif

#endif /* end of WASM_ENABLE_DISPATCH */

static inline uint8 *
get_global_addr(uint8 *global_data, WASMGlobal *global)
{
    return global_data + global->data_offset;
}

static void
wasm_interp_call_func_bytecode(WASMModule *module,
                               WASMExecEnv *exec_env,
                               WASMFunction *function,
                               WASMFuncFrame *prev_frame)
{
    WASMMemory *memory = module->memories;
    uint8 *global_data = module->global_data;
    uint32 num_bytes_per_page = memory ? memory->num_bytes_per_page : 0;
    uint32 linear_mem_size =
        memory ? num_bytes_per_page * memory->cur_page_count : 0;
    WASMType **wasm_types = module->types;
    WASMGlobal *globals = module->globals, *global;
    uint32 *value_stack = (uint32 *)exec_env->value_stack.top;
    WASMFunction *cur_func = function;

    // 初始化栈帧
    WASMFuncFrame *frame = ALLOC_FRAME(exec_env, prev_frame);
    frame->lp = value_stack - cur_func->param_cell_num;
    frame->sp = value_stack + cur_func->local_cell_num;
    frame->ip = (uint8 *)cur_func->func_ptr;
    frame->function = function;

    WASMBranchTable *branch_table = cur_func->branch_table;
    WASMBranchTable *cur_branch_table = branch_table;

    register uint8 *frame_ip = frame->ip;
    register uint32 *frame_lp = frame->lp;
    register uint32 *frame_sp = frame->sp;
    uint8 *frame_ip_end = cur_func->code_end;

    uint8 opcode;
    uint32 i, cond, count, fidx, tidx, lidx;
    int32 val;
    uint8 *maddr = NULL;
    uint32 local_idx, local_offset, global_idx;
    uint8 local_type, *global_addr;

#if WASM_ENABLE_DEBUG_INTERP != 0
    call_info = fopen("call_info.log", "w");

#endif

#if WASM_ENABLE_DISPATCH != 0
#define HANDLE_OPCODE(op) &&HANDLE_##op
    DEFINE_GOTO_TABLE(const void *, handle_table);
#undef HANDLE_OPCODE
#endif

#if WASM_ENABLE_DISPATCH == 0
    while (frame_ip < frame_ip_end)
    {
        opcode = *frame_ip++;
        switch (opcode)
        {
#else
    FETCH_OPCODE_AND_DISPATCH();
#endif
            /* control instructions */
            HANDLE_OP(WASM_OP_UNREACHABLE)
            {
                wasm_set_exception(module, "unreachable");
                goto got_exception;
            }

            HANDLE_OP(WASM_OP_NOP) { HANDLE_OP_END(); }

            HANDLE_OP(WASM_OP_BLOCK)
            {
                skip_leb_uint32(frame_ip, frame_ip_end);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_LOOP)
            {
                skip_leb_uint32(frame_ip, frame_ip_end);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_IF)
            {
                skip_leb_uint32(frame_ip, frame_ip_end);
                cond = (uint32)POP_I32();

                if (!cond)
                {
                    frame_ip = cur_branch_table->ip;
                    cur_branch_table = branch_table + cur_branch_table->stp;
                }
                else
                {
                    cur_branch_table++;
                }
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_ELSE)
            {
                frame_ip = cur_branch_table->ip;
                cur_branch_table = branch_table + cur_branch_table->stp;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_END)
            {
                if (frame_ip == frame_ip_end)
                {
                    frame_sp -= cur_func->ret_cell_num;
                    for (i = 0; i < cur_func->ret_cell_num; i++)
                    {
                        *prev_frame->sp++ = frame_sp[i];
                    }
                    goto return_func;
                }
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_BR)
            {
                CONTROL_TRANSFER();
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_BR_IF)
            {
                skip_leb_uint32(frame_ip, frame_ip_end);
                cond = (uint32)POP_I32();
                if (cond)
                {
                    CONTROL_TRANSFER();
                }
                else
                {
                    cur_branch_table++;
                }
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_BR_TABLE)
            {
                read_leb_uint32(frame_ip, frame_ip_end, count);
                lidx = POP_I32();
                if (lidx > count)
                    lidx = count;
                cur_branch_table += lidx;
                CONTROL_TRANSFER();
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_RETURN)
            {
                frame_sp -= cur_func->ret_cell_num;
                for (i = 0; i < cur_func->ret_cell_num; i++)
                {
                    *prev_frame->sp++ = frame_sp[i];
                }
                goto return_func;
            }

            HANDLE_OP(WASM_OP_CALL)
            {
                read_leb_uint32(frame_ip, frame_ip_end, fidx);

                frame->function = cur_func;

                cur_func = module->functions + fidx;
                goto call_func_from_interp;
            }

            HANDLE_OP(WASM_OP_CALL_INDIRECT)
            {
                WASMType *cur_type, *cur_func_type;
                WASMTable *tbl_inst;
                uint32 tbl_idx;

                read_leb_uint32(frame_ip, frame_ip_end, tidx);
                cur_type = wasm_types[tidx];

                read_leb_uint32(frame_ip, frame_ip_end, tbl_idx);

                tbl_inst = module->tables + tbl_idx;

                val = POP_I32();
                if ((uint32)val >= tbl_inst->cur_size)
                {
                    wasm_set_exception(module, "undefined element");
                    goto got_exception;
                }

                fidx = tbl_inst->table_data[val];
                if (fidx == NULL_REF)
                {
                    wasm_set_exception(module, "uninitialized element");
                    goto got_exception;
                }

                if (fidx >= module->function_count)
                {
                    wasm_set_exception(module, "unknown function");
                    goto got_exception;
                }

                frame->function = cur_func;
                /* always call module own functions */
                cur_func = module->functions + fidx;

                cur_func_type = cur_func->func_type;

                if (cur_type != cur_func_type)
                {
                    wasm_set_exception(module, "indirect call type mismatch");
                    goto got_exception;
                }

                goto call_func_from_interp;
            }

            /* parametric instructions */
            HANDLE_OP(WASM_OP_DROP)
            {
                frame_sp--;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_DROP_64)
            {
                frame_sp -= 2;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_SELECT)
            {
                cond = (uint32)POP_I32();
                frame_sp--;
                if (!cond)
                    *(frame_sp - 1) = *frame_sp;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_SELECT_64)
            {
                cond = (uint32)POP_I32();
                frame_sp -= 2;
                if (!cond)
                {
                    *(frame_sp - 2) = *frame_sp;
                    *(frame_sp - 1) = *(frame_sp + 1);
                }
                HANDLE_OP_END();
            }

            /* variable instructions */
            HANDLE_OP(WASM_OP_GET_LOCAL)
            {
                GET_LOCAL_INDEX_TYPE_AND_OFFSET();

                switch (local_type)
                {
                case VALUE_TYPE_I32:
                case VALUE_TYPE_F32:
                    PUSH_I32(GET_I32_FROM_ADDR(frame_lp + local_offset));
                    break;
                case VALUE_TYPE_I64:
                case VALUE_TYPE_F64:
                    PUSH_I64(GET_I64_FROM_ADDR(frame_lp + local_offset));
                    break;
                default:
                    wasm_set_exception(module, "invalid local type");
                    goto got_exception;
                }

                HANDLE_OP_END();
            }

            HANDLE_OP(EXT_OP_GET_LOCAL_FAST)
            {
                local_offset = *frame_ip++;
                if (local_offset & 0x80)
                    PUSH_I64(
                        GET_I64_FROM_ADDR(frame_lp + (local_offset & 0x7F)));
                else
                    PUSH_I32(GET_I32_FROM_ADDR(frame_lp + local_offset));
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_SET_LOCAL)
            {
                GET_LOCAL_INDEX_TYPE_AND_OFFSET();

                switch (local_type)
                {
                case VALUE_TYPE_I32:
                case VALUE_TYPE_F32:
                    PUT_I32_TO_ADDR(frame_lp + local_offset, POP_I32());
                    break;
                case VALUE_TYPE_I64:
                case VALUE_TYPE_F64:
                    PUT_I64_TO_ADDR(frame_lp + local_offset, POP_I64());
                    break;
                default:
                    wasm_set_exception(module, "invalid local type");
                    goto got_exception;
                }

                HANDLE_OP_END();
            }

            HANDLE_OP(EXT_OP_SET_LOCAL_FAST)
            {
                local_offset = *frame_ip++;
                if (local_offset & 0x80)
                    PUT_I64_TO_ADDR(
                        frame_lp + (local_offset & 0x7F),
                        POP_I64());
                else
                    PUT_I32_TO_ADDR(frame_lp + local_offset, POP_I32());
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_TEE_LOCAL)
            {
                GET_LOCAL_INDEX_TYPE_AND_OFFSET();

                switch (local_type)
                {
                case VALUE_TYPE_I32:
                case VALUE_TYPE_F32:
                    PUT_I32_TO_ADDR(frame_lp + local_offset, GET_I32_FROM_ADDR(frame_sp - 1));
                    break;
                case VALUE_TYPE_I64:
                case VALUE_TYPE_F64:
                    PUT_I64_TO_ADDR((frame_lp + local_offset), GET_I64_FROM_ADDR(frame_sp - 2));
                    break;
                default:
                    wasm_set_exception(module, "invalid local type");
                    goto got_exception;
                }

                HANDLE_OP_END();
            }

            HANDLE_OP(EXT_OP_TEE_LOCAL_FAST)
            {
                local_offset = *frame_ip++;
                if (local_offset & 0x80)
                    PUT_I64_TO_ADDR(
                        frame_lp + (local_offset & 0x7F),
                        GET_I64_FROM_ADDR(frame_sp - 2));
                else
                    PUT_I32_TO_ADDR(
                        frame_lp + local_offset,
                        GET_I32_FROM_ADDR(frame_sp - 1));
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_GET_GLOBAL)
            {
                read_leb_uint32(frame_ip, frame_ip_end, global_idx);
                global = globals + global_idx;
                global_addr = get_global_addr(global_data, global);
                PUSH_I32(GET_I32_FROM_ADDR(global_addr));
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_GET_GLOBAL_64)
            {
                read_leb_uint32(frame_ip, frame_ip_end, global_idx);
                global = globals + global_idx;
                global_addr = get_global_addr(global_data, global);
                PUSH_I64(GET_I64_FROM_ADDR(global_addr));
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_SET_GLOBAL)
            {
                read_leb_uint32(frame_ip, frame_ip_end, global_idx);
                global = globals + global_idx;
                global_addr = get_global_addr(global_data, global);
                *(int32 *)global_addr = POP_I32();
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_SET_GLOBAL_64)
            {
                read_leb_uint32(frame_ip, frame_ip_end, global_idx);
                global = globals + global_idx;
                global_addr = get_global_addr(global_data, global);
                PUT_I64_TO_ADDR(global_addr, POP_I64());
                HANDLE_OP_END();
            }

            /* memory load instructions */
            HANDLE_OP(WASM_OP_I32_LOAD)
            HANDLE_OP(WASM_OP_F32_LOAD)
            {
                uint32 offset, flags, addr;

                read_leb_uint32(frame_ip, frame_ip_end, flags);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                addr = POP_I32();
                CHECK_MEMORY_OVERFLOW(4);
                PUSH_I32(LOAD_I32(maddr));
                (void)flags;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_LOAD)
            HANDLE_OP(WASM_OP_F64_LOAD)
            {
                uint32 offset, flags, addr;

                read_leb_uint32(frame_ip, frame_ip_end, flags);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                addr = POP_I32();
                CHECK_MEMORY_OVERFLOW(8);
                PUSH_I64(LOAD_I64(maddr));
                (void)flags;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_LOAD8_S)
            {
                uint32 offset, flags, addr;

                read_leb_uint32(frame_ip, frame_ip_end, flags);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                addr = POP_I32();
                CHECK_MEMORY_OVERFLOW(1);
                PUSH_I32(sign_ext_8_32(*(int8 *)maddr));
                (void)flags;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_LOAD8_U)
            {
                uint32 offset, flags, addr;

                read_leb_uint32(frame_ip, frame_ip_end, flags);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                addr = POP_I32();
                CHECK_MEMORY_OVERFLOW(1);
                PUSH_I32((uint32)(*(uint8 *)maddr));
                (void)flags;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_LOAD16_S)
            {
                uint32 offset, flags, addr;

                read_leb_uint32(frame_ip, frame_ip_end, flags);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                addr = POP_I32();
                CHECK_MEMORY_OVERFLOW(2);
                PUSH_I32(sign_ext_16_32(LOAD_I16(maddr)));
                (void)flags;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_LOAD16_U)
            {
                uint32 offset, flags, addr;

                read_leb_uint32(frame_ip, frame_ip_end, flags);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                addr = POP_I32();
                CHECK_MEMORY_OVERFLOW(2);
                PUSH_I32((uint32)(LOAD_U16(maddr)));
                (void)flags;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_LOAD8_S)
            {
                uint32 offset, flags, addr;

                read_leb_uint32(frame_ip, frame_ip_end, flags);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                addr = POP_I32();
                CHECK_MEMORY_OVERFLOW(1);
                PUSH_I64(sign_ext_8_64(*(int8 *)maddr));
                (void)flags;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_LOAD8_U)
            {
                uint32 offset, flags, addr;

                read_leb_uint32(frame_ip, frame_ip_end, flags);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                addr = POP_I32();
                CHECK_MEMORY_OVERFLOW(1);
                PUSH_I64((uint64)(*(uint8 *)maddr));
                (void)flags;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_LOAD16_S)
            {
                uint32 offset, flags, addr;

                read_leb_uint32(frame_ip, frame_ip_end, flags);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                addr = POP_I32();
                CHECK_MEMORY_OVERFLOW(2);
                PUSH_I64(sign_ext_16_64(LOAD_I16(maddr)));
                (void)flags;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_LOAD16_U)
            {
                uint32 offset, flags, addr;

                read_leb_uint32(frame_ip, frame_ip_end, flags);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                addr = POP_I32();
                CHECK_MEMORY_OVERFLOW(2);
                PUSH_I64((uint64)(LOAD_U16(maddr)));
                (void)flags;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_LOAD32_S)
            {
                uint32 offset, flags, addr;

                opcode = *(frame_ip - 1);
                read_leb_uint32(frame_ip, frame_ip_end, flags);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                addr = POP_I32();
                CHECK_MEMORY_OVERFLOW(4);
                PUSH_I64(sign_ext_32_64(LOAD_I32(maddr)));
                (void)flags;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_LOAD32_U)
            {
                uint32 offset, flags, addr;

                read_leb_uint32(frame_ip, frame_ip_end, flags);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                addr = POP_I32();
                CHECK_MEMORY_OVERFLOW(4);
                PUSH_I64((uint64)(LOAD_U32(maddr)));
                (void)flags;
                HANDLE_OP_END();
            }

            /* memory store instructions */
            HANDLE_OP(WASM_OP_I32_STORE)
            HANDLE_OP(WASM_OP_F32_STORE)
            {
                uint32 offset, flags, addr;

                read_leb_uint32(frame_ip, frame_ip_end, flags);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                frame_sp--;
                addr = POP_I32();
                CHECK_MEMORY_OVERFLOW(4);
                STORE_U32(maddr, frame_sp[1]);
                (void)flags;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_STORE)
            HANDLE_OP(WASM_OP_F64_STORE)
            {
                uint32 offset, flags, addr;

                read_leb_uint32(frame_ip, frame_ip_end, flags);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                frame_sp -= 2;
                addr = POP_I32();
                CHECK_MEMORY_OVERFLOW(8);
                PUT_I64_TO_ADDR(maddr,
                                GET_I64_FROM_ADDR(frame_sp + 1));
                (void)flags;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_STORE8)
            HANDLE_OP(WASM_OP_I32_STORE16)
            {
                uint32 offset, flags, addr;
                uint32 sval;

                opcode = *(frame_ip - 1);
                read_leb_uint32(frame_ip, frame_ip_end, flags);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                sval = (uint32)POP_I32();
                addr = POP_I32();

                if (opcode == WASM_OP_I32_STORE8)
                {
                    CHECK_MEMORY_OVERFLOW(1);
                    *(uint8 *)maddr = (uint8)sval;
                }
                else
                {
                    CHECK_MEMORY_OVERFLOW(2);
                    STORE_U16(maddr, (uint16)sval);
                }
                (void)flags;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_STORE8)
            HANDLE_OP(WASM_OP_I64_STORE16)
            HANDLE_OP(WASM_OP_I64_STORE32)
            {
                uint32 offset, flags, addr;
                uint64 sval;

                opcode = *(frame_ip - 1);
                read_leb_uint32(frame_ip, frame_ip_end, flags);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                sval = (uint64)POP_I64();
                addr = POP_I32();

                if (opcode == WASM_OP_I64_STORE8)
                {
                    CHECK_MEMORY_OVERFLOW(1);
                    *(uint8 *)maddr = (uint8)sval;
                }
                else if (opcode == WASM_OP_I64_STORE16)
                {
                    CHECK_MEMORY_OVERFLOW(2);
                    STORE_U16(maddr, (uint16)sval);
                }
                else
                {
                    CHECK_MEMORY_OVERFLOW(4);
                    STORE_U32(maddr, (uint32)sval);
                }
                (void)flags;
                HANDLE_OP_END();
            }

            /* memory size and memory grow instructions */
            HANDLE_OP(WASM_OP_MEMORY_SIZE)
            {
                uint32 reserved;
                read_leb_uint32(frame_ip, frame_ip_end, reserved);
                PUSH_I32(memory->cur_page_count);
                (void)reserved;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_MEMORY_GROW)
            {
                uint32 reserved, delta,
                    prev_page_count = memory->cur_page_count;

                read_leb_uint32(frame_ip, frame_ip_end, reserved);
                delta = (uint32)POP_I32();

                if (!wasm_enlarge_memory(module, delta))
                {
                    PUSH_I32(-1);
                }
                else
                {
                    /* success, return previous page count */
                    PUSH_I32(prev_page_count);
                    /* update memory size, no need to update memory ptr as
                       it isn't changed in wasm_enlarge_memory */
                    linear_mem_size =
                        num_bytes_per_page * memory->cur_page_count;
                }

                (void)reserved;
                HANDLE_OP_END();
            }

            /* constant instructions */
            HANDLE_OP(WASM_OP_I32_CONST)
            DEF_OP_I_CONST(int32, I32);
            HANDLE_OP_END();

            HANDLE_OP(WASM_OP_I64_CONST)
            DEF_OP_I_CONST(int64, I64);
            HANDLE_OP_END();

            HANDLE_OP(WASM_OP_F32_CONST)
            {
                uint8 *p_float = (uint8 *)frame_sp++;
                for (i = 0; i < sizeof(float32); i++)
                    *p_float++ = *frame_ip++;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_CONST)
            {
                uint8 *p_float = (uint8 *)frame_sp++;
                frame_sp++;
                for (i = 0; i < sizeof(float64); i++)
                    *p_float++ = *frame_ip++;
                HANDLE_OP_END();
            }

            /* comparison instructions of i32 */
            HANDLE_OP(WASM_OP_I32_EQZ)
            {
                DEF_OP_EQZ(I32);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_EQ)
            {
                DEF_OP_CMP(uint32, I32, ==);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_NE)
            {
                DEF_OP_CMP(uint32, I32, !=);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_LT_S)
            {
                DEF_OP_CMP(int32, I32, <);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_LT_U)
            {
                DEF_OP_CMP(uint32, I32, <);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_GT_S)
            {
                DEF_OP_CMP(int32, I32, >);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_GT_U)
            {
                DEF_OP_CMP(uint32, I32, >);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_LE_S)
            {
                DEF_OP_CMP(int32, I32, <=);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_LE_U)
            {
                DEF_OP_CMP(uint32, I32, <=);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_GE_S)
            {
                DEF_OP_CMP(int32, I32, >=);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_GE_U)
            {
                DEF_OP_CMP(uint32, I32, >=);
                HANDLE_OP_END();
            }

            /* comparison instructions of i64 */
            HANDLE_OP(WASM_OP_I64_EQZ)
            {
                DEF_OP_EQZ(I64);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_EQ)
            {
                DEF_OP_CMP(uint64, I64, ==);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_NE)
            {
                DEF_OP_CMP(uint64, I64, !=);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_LT_S)
            {
                DEF_OP_CMP(int64, I64, <);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_LT_U)
            {
                DEF_OP_CMP(uint64, I64, <);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_GT_S)
            {
                DEF_OP_CMP(int64, I64, >);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_GT_U)
            {
                DEF_OP_CMP(uint64, I64, >);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_LE_S)
            {
                DEF_OP_CMP(int64, I64, <=);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_LE_U)
            {
                DEF_OP_CMP(uint64, I64, <=);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_GE_S)
            {
                DEF_OP_CMP(int64, I64, >=);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_GE_U)
            {
                DEF_OP_CMP(uint64, I64, >=);
                HANDLE_OP_END();
            }

            /* comparison instructions of f32 */
            HANDLE_OP(WASM_OP_F32_EQ)
            {
                DEF_OP_CMP(float32, F32, ==);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_NE)
            {
                DEF_OP_CMP(float32, F32, !=);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_LT)
            {
                DEF_OP_CMP(float32, F32, <);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_GT)
            {
                DEF_OP_CMP(float32, F32, >);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_LE)
            {
                DEF_OP_CMP(float32, F32, <=);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_GE)
            {
                DEF_OP_CMP(float32, F32, >=);
                HANDLE_OP_END();
            }

            /* comparison instructions of f64 */
            HANDLE_OP(WASM_OP_F64_EQ)
            {
                DEF_OP_CMP(float64, F64, ==);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_NE)
            {
                DEF_OP_CMP(float64, F64, !=);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_LT)
            {
                DEF_OP_CMP(float64, F64, <);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_GT)
            {
                DEF_OP_CMP(float64, F64, >);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_LE)
            {
                DEF_OP_CMP(float64, F64, <=);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_GE)
            {
                DEF_OP_CMP(float64, F64, >=);
                HANDLE_OP_END();
            }

            /* numberic instructions of i32 */
            HANDLE_OP(WASM_OP_I32_CLZ)
            {
                DEF_OP_BIT_COUNT(uint32, I32, clz32);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_CTZ)
            {
                DEF_OP_BIT_COUNT(uint32, I32, ctz32);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_POPCNT)
            {
                DEF_OP_BIT_COUNT(uint32, I32, popcount32);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_ADD)
            {
                DEF_OP_NUMERIC(uint32, uint32, I32, +);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_SUB)
            {
                DEF_OP_NUMERIC(uint32, uint32, I32, -);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_MUL)
            {
                DEF_OP_NUMERIC(uint32, uint32, I32, *);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_DIV_S)
            {
                int32 a, b;

                b = POP_I32();
                a = POP_I32();
                if (a == (int32)0x80000000 && b == -1)
                {
                    wasm_set_exception(module, "integer overflow");
                    goto got_exception;
                }
                if (b == 0)
                {
                    wasm_set_exception(module, "integer divide by zero");
                    goto got_exception;
                }
                PUSH_I32(a / b);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_DIV_U)
            {
                uint32 a, b;

                b = (uint32)POP_I32();
                a = (uint32)POP_I32();
                if (b == 0)
                {
                    wasm_set_exception(module, "integer divide by zero");
                    goto got_exception;
                }
                PUSH_I32(a / b);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_REM_S)
            {
                int32 a, b;

                b = POP_I32();
                a = POP_I32();
                if (a == (int32)0x80000000 && b == -1)
                {
                    PUSH_I32(0);
                    HANDLE_OP_END();
                }
                if (b == 0)
                {
                    wasm_set_exception(module, "integer divide by zero");
                    goto got_exception;
                }
                PUSH_I32(a % b);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_REM_U)
            {
                uint32 a, b;

                b = (uint32)POP_I32();
                a = (uint32)POP_I32();
                if (b == 0)
                {
                    wasm_set_exception(module, "integer divide by zero");
                    goto got_exception;
                }
                PUSH_I32(a % b);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_AND)
            {
                DEF_OP_NUMERIC(uint32, uint32, I32, &);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_OR)
            {
                DEF_OP_NUMERIC(uint32, uint32, I32, |);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_XOR)
            {
                DEF_OP_NUMERIC(uint32, uint32, I32, ^);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_SHL)
            {
                DEF_OP_NUMERIC2(uint32, uint32, I32, <<);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_SHR_S)
            {
                DEF_OP_NUMERIC2(int32, uint32, I32, >>);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_SHR_U)
            {
                DEF_OP_NUMERIC2(uint32, uint32, I32, >>);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_ROTL)
            {
                uint32 a, b;

                b = (uint32)POP_I32();
                a = (uint32)POP_I32();
                PUSH_I32(rotl32(a, b));
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_ROTR)
            {
                uint32 a, b;

                b = (uint32)POP_I32();
                a = (uint32)POP_I32();
                PUSH_I32(rotr32(a, b));
                HANDLE_OP_END();
            }

            /* numberic instructions of i64 */
            HANDLE_OP(WASM_OP_I64_CLZ)
            {
                DEF_OP_BIT_COUNT(uint64, I64, clz64);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_CTZ)
            {
                DEF_OP_BIT_COUNT(uint64, I64, ctz64);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_POPCNT)
            {
                DEF_OP_BIT_COUNT(uint64, I64, popcount64);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_ADD)
            {
                DEF_OP_NUMERIC_64(uint64, uint64, I64, +);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_SUB)
            {
                DEF_OP_NUMERIC_64(uint64, uint64, I64, -);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_MUL)
            {
                DEF_OP_NUMERIC_64(uint64, uint64, I64, *);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_DIV_S)
            {
                int64 a, b;

                b = POP_I64();
                a = POP_I64();
                if (a == (int64)0x8000000000000000LL && b == -1)
                {
                    wasm_set_exception(module, "integer overflow");
                    goto got_exception;
                }
                if (b == 0)
                {
                    wasm_set_exception(module, "integer divide by zero");
                    goto got_exception;
                }
                PUSH_I64(a / b);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_DIV_U)
            {
                uint64 a, b;

                b = (uint64)POP_I64();
                a = (uint64)POP_I64();
                if (b == 0)
                {
                    wasm_set_exception(module, "integer divide by zero");
                    goto got_exception;
                }
                PUSH_I64(a / b);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_REM_S)
            {
                int64 a, b;

                b = POP_I64();
                a = POP_I64();
                if (a == (int64)0x8000000000000000LL && b == -1)
                {
                    PUSH_I64(0);
                    HANDLE_OP_END();
                }
                if (b == 0)
                {
                    wasm_set_exception(module, "integer divide by zero");
                    goto got_exception;
                }
                PUSH_I64(a % b);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_REM_U)
            {
                uint64 a, b;

                b = (uint64)POP_I64();
                a = (uint64)POP_I64();
                if (b == 0)
                {
                    wasm_set_exception(module, "integer divide by zero");
                    goto got_exception;
                }
                PUSH_I64(a % b);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_AND)
            {
                DEF_OP_NUMERIC_64(uint64, uint64, I64, &);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_OR)
            {
                DEF_OP_NUMERIC_64(uint64, uint64, I64, |);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_XOR)
            {
                DEF_OP_NUMERIC_64(uint64, uint64, I64, ^);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_SHL)
            {
                DEF_OP_NUMERIC2_64(uint64, uint64, I64, <<);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_SHR_S)
            {
                DEF_OP_NUMERIC2_64(int64, uint64, I64, >>);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_SHR_U)
            {
                DEF_OP_NUMERIC2_64(uint64, uint64, I64, >>);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_ROTL)
            {
                uint64 a, b;

                b = (uint64)POP_I64();
                a = (uint64)POP_I64();
                PUSH_I64(rotl64(a, b));
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_ROTR)
            {
                uint64 a, b;

                b = (uint64)POP_I64();
                a = (uint64)POP_I64();
                PUSH_I64(rotr64(a, b));
                HANDLE_OP_END();
            }

            /* numberic instructions of f32 */
            HANDLE_OP(WASM_OP_F32_ABS)
            {
                DEF_OP_MATH(float32, F32, fabsf);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_NEG)
            {
                uint32 u32 = frame_sp[-1];
                uint32 sign_bit = u32 & ((uint32)1 << 31);
                if (sign_bit)
                    frame_sp[-1] = u32 & ~((uint32)1 << 31);
                else
                    frame_sp[-1] = u32 | ((uint32)1 << 31);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_CEIL)
            {
                DEF_OP_MATH(float32, F32, ceilf);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_FLOOR)
            {
                DEF_OP_MATH(float32, F32, floorf);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_TRUNC)
            {
                DEF_OP_MATH(float32, F32, truncf);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_NEAREST)
            {
                DEF_OP_MATH(float32, F32, rintf);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_SQRT)
            {
                DEF_OP_MATH(float32, F32, sqrtf);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_ADD)
            {
                DEF_OP_NUMERIC(float32, float32, F32, +);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_SUB)
            {
                DEF_OP_NUMERIC(float32, float32, F32, -);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_MUL)
            {
                DEF_OP_NUMERIC(float32, float32, F32, *);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_DIV)
            {
                DEF_OP_NUMERIC(float32, float32, F32, /);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_MIN)
            {
                float32 a, b;

                b = POP_F32();
                a = POP_F32();

                PUSH_F32(f32_min(a, b));
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_MAX)
            {
                float32 a, b;

                b = POP_F32();
                a = POP_F32();

                PUSH_F32(f32_max(a, b));
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_COPYSIGN)
            {
                float32 a, b;

                b = POP_F32();
                a = POP_F32();
                PUSH_F32(local_copysignf(a, b));
                HANDLE_OP_END();
            }

            /* numberic instructions of f64 */
            HANDLE_OP(WASM_OP_F64_ABS)
            {
                DEF_OP_MATH(float64, F64, fabs);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_NEG)
            {
                uint64 u64 = GET_I64_FROM_ADDR(frame_sp - 2);
                uint64 sign_bit = u64 & (((uint64)1) << 63);
                if (sign_bit)
                    PUT_I64_TO_ADDR(frame_sp - 2, (u64 & ~(((uint64)1) << 63)));
                else
                    PUT_I64_TO_ADDR(frame_sp - 2, (u64 | (((uint64)1) << 63)));
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_CEIL)
            {
                DEF_OP_MATH(float64, F64, ceil);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_FLOOR)
            {
                DEF_OP_MATH(float64, F64, floor);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_TRUNC)
            {
                DEF_OP_MATH(float64, F64, trunc);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_NEAREST)
            {
                DEF_OP_MATH(float64, F64, rint);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_SQRT)
            {
                DEF_OP_MATH(float64, F64, sqrt);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_ADD)
            {
                DEF_OP_NUMERIC_64(float64, float64, F64, +);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_SUB)
            {
                DEF_OP_NUMERIC_64(float64, float64, F64, -);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_MUL)
            {
                DEF_OP_NUMERIC_64(float64, float64, F64, *);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_DIV)
            {
                DEF_OP_NUMERIC_64(float64, float64, F64, /);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_MIN)
            {
                float64 a, b;

                b = POP_F64();
                a = POP_F64();

                PUSH_F64(f64_min(a, b));
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_MAX)
            {
                float64 a, b;

                b = POP_F64();
                a = POP_F64();

                PUSH_F64(f64_max(a, b));
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_COPYSIGN)
            {
                float64 a, b;

                b = POP_F64();
                a = POP_F64();
                PUSH_F64(local_copysign(a, b));
                HANDLE_OP_END();
            }

            /* conversions of i32 */
            HANDLE_OP(WASM_OP_I32_WRAP_I64)
            {
                int32 value = (int32)(POP_I64() & 0xFFFFFFFFLL);
                PUSH_I32(value);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_TRUNC_S_F32)
            {
                DEF_OP_TRUNC_F32(-2147483904.0f, 2147483648.0f, true, true);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_TRUNC_U_F32)
            {
                DEF_OP_TRUNC_F32(-1.0f, 4294967296.0f, true, false);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_TRUNC_S_F64)
            {
                DEF_OP_TRUNC_F64(-2147483649.0, 2147483648.0, true, true);
                frame_sp--;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_TRUNC_U_F64)
            {
                DEF_OP_TRUNC_F64(-1.0, 4294967296.0, true, false);
                frame_sp--;
                HANDLE_OP_END();
            }

            /* conversions of i64 */
            HANDLE_OP(WASM_OP_I64_EXTEND_S_I32)
            {
                DEF_OP_CONVERT(int64, I64, int32, I32);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_EXTEND_U_I32)
            {
                DEF_OP_CONVERT(int64, I64, uint32, I32);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_TRUNC_S_F32)
            {
                DEF_OP_TRUNC_F32(-9223373136366403584.0f,
                                 9223372036854775808.0f, false, true);
                frame_sp++;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_TRUNC_U_F32)
            {
                DEF_OP_TRUNC_F32(-1.0f, 18446744073709551616.0f, false, false);
                frame_sp++;
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_TRUNC_S_F64)
            {
                DEF_OP_TRUNC_F64(-9223372036854777856.0, 9223372036854775808.0,
                                 false, true);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_TRUNC_U_F64)
            {
                DEF_OP_TRUNC_F64(-1.0, 18446744073709551616.0, false, false);
                HANDLE_OP_END();
            }

            /* conversions of f32 */
            HANDLE_OP(WASM_OP_F32_CONVERT_S_I32)
            {
                DEF_OP_CONVERT(float32, F32, int32, I32);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_CONVERT_U_I32)
            {
                DEF_OP_CONVERT(float32, F32, uint32, I32);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_CONVERT_S_I64)
            {
                DEF_OP_CONVERT(float32, F32, int64, I64);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_CONVERT_U_I64)
            {
                DEF_OP_CONVERT(float32, F32, uint64, I64);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F32_DEMOTE_F64)
            {
                DEF_OP_CONVERT(float32, F32, float64, F64);
                HANDLE_OP_END();
            }

            /* conversions of f64 */
            HANDLE_OP(WASM_OP_F64_CONVERT_S_I32)
            {
                DEF_OP_CONVERT(float64, F64, int32, I32);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_CONVERT_U_I32)
            {
                DEF_OP_CONVERT(float64, F64, uint32, I32);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_CONVERT_S_I64)
            {
                DEF_OP_CONVERT(float64, F64, int64, I64);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_CONVERT_U_I64)
            {
                DEF_OP_CONVERT(float64, F64, uint64, I64);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_F64_PROMOTE_F32)
            {
                DEF_OP_CONVERT(float64, F64, float32, F32);
                HANDLE_OP_END();
            }

            /* reinterpretations */
            HANDLE_OP(WASM_OP_I32_REINTERPRET_F32)
            HANDLE_OP(WASM_OP_I64_REINTERPRET_F64)
            HANDLE_OP(WASM_OP_F32_REINTERPRET_I32)
            HANDLE_OP(WASM_OP_F64_REINTERPRET_I64) { HANDLE_OP_END(); }

            HANDLE_OP(WASM_OP_I32_EXTEND8_S)
            {
                DEF_OP_CONVERT(int32, I32, int8, I32);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I32_EXTEND16_S)
            {
                DEF_OP_CONVERT(int32, I32, int16, I32);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_EXTEND8_S)
            {
                DEF_OP_CONVERT(int64, I64, int8, I64);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_EXTEND16_S)
            {
                DEF_OP_CONVERT(int64, I64, int16, I64);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_I64_EXTEND32_S)
            {
                DEF_OP_CONVERT(int64, I64, int32, I64);
                HANDLE_OP_END();
            }

            HANDLE_OP(WASM_OP_MISC_PREFIX)
            {
                uint32 opcode1;

                read_leb_uint32(frame_ip, frame_ip_end, opcode1);
                opcode = (uint8)opcode1;

                switch (opcode)
                {
                case WASM_OP_I32_TRUNC_SAT_S_F32:
                    DEF_OP_TRUNC_SAT_F32(-2147483904.0f, 2147483648.0f,
                                         true, true);
                    break;
                case WASM_OP_I32_TRUNC_SAT_U_F32:
                    DEF_OP_TRUNC_SAT_F32(-1.0f, 4294967296.0f, true, false);
                    break;
                case WASM_OP_I32_TRUNC_SAT_S_F64:
                    DEF_OP_TRUNC_SAT_F64(-2147483649.0, 2147483648.0, true,
                                         true);
                    frame_sp--;
                    break;
                case WASM_OP_I32_TRUNC_SAT_U_F64:
                    DEF_OP_TRUNC_SAT_F64(-1.0, 4294967296.0, true, false);
                    frame_sp--;
                    break;
                case WASM_OP_I64_TRUNC_SAT_S_F32:
                    DEF_OP_TRUNC_SAT_F32(-9223373136366403584.0f,
                                         9223372036854775808.0f, false,
                                         true);
                    frame_sp++;
                    break;
                case WASM_OP_I64_TRUNC_SAT_U_F32:
                    DEF_OP_TRUNC_SAT_F32(-1.0f, 18446744073709551616.0f,
                                         false, false);
                    frame_sp++;
                    break;
                case WASM_OP_I64_TRUNC_SAT_S_F64:
                    DEF_OP_TRUNC_SAT_F64(-9223372036854777856.0,
                                         9223372036854775808.0, false,
                                         true);
                    break;
                case WASM_OP_I64_TRUNC_SAT_U_F64:
                    DEF_OP_TRUNC_SAT_F64(-1.0f, 18446744073709551616.0,
                                         false, false);
                    break;
                case WASM_OP_MEMORY_INIT:
                {
                    uint32 addr, segment;
                    uint64 bytes, offset, seg_len;
                    uint8 *data;

                    read_leb_uint32(frame_ip, frame_ip_end, segment);
                    /* skip memory index */
                    frame_ip++;

                    bytes = (uint64)(uint32)POP_I32();
                    offset = (uint64)(uint32)POP_I32();
                    addr = (uint32)POP_I32();

                    CHECK_BULK_MEMORY_OVERFLOW(addr, bytes, maddr);

                    seg_len = (uint64)module->data_segments[segment].data_length;
                    data = module->data_segments[segment].data;
                    if (offset + bytes > seg_len)
                        goto out_of_bounds;

                    memcpy(maddr,
                           data + offset, (uint32)bytes);
                    break;
                }
                case WASM_OP_DATA_DROP:
                {
                    uint32 segment;

                    read_leb_uint32(frame_ip, frame_ip_end, segment);
                    module->data_segments[segment].data_length = 0;
                    break;
                }
                case WASM_OP_MEMORY_COPY:
                {
                    uint32 dst, src, len;
                    uint8 *mdst, *msrc;

                    frame_ip += 2;

                    len = POP_I32();
                    src = POP_I32();
                    dst = POP_I32();

                    CHECK_BULK_MEMORY_OVERFLOW(src, len, msrc);
                    CHECK_BULK_MEMORY_OVERFLOW(dst, len, mdst);

                    /* allowing the destination and source to overlap */
                    memmove(mdst, msrc, len);
                    break;
                }
                case WASM_OP_MEMORY_FILL:
                {
                    uint32 dst, len;
                    uint8 fill_val, *mdst;
                    frame_ip++;

                    len = POP_I32();
                    fill_val = POP_I32();
                    dst = POP_I32();
                    CHECK_BULK_MEMORY_OVERFLOW(dst, len, mdst);

                    memset(mdst, fill_val, len);
                    break;
                }

                default:
                    wasm_set_exception(module, "unsupported opcode");
                    goto got_exception;
                }
                HANDLE_OP_END();
            }

#if WASM_ENABLE_DISPATCH != 0
            HANDLE_OP(WASM_OP_UNUSED_0x06)
            HANDLE_OP(WASM_OP_UNUSED_0x07)
            HANDLE_OP(WASM_OP_UNUSED_0x08)
            HANDLE_OP(WASM_OP_UNUSED_0x09)
            HANDLE_OP(WASM_OP_UNUSED_0x0a)
            HANDLE_OP(WASM_OP_RETURN_CALL)
            HANDLE_OP(WASM_OP_RETURN_CALL_INDIRECT)
            HANDLE_OP(WASM_OP_ATOMIC_PREFIX)
            HANDLE_OP(WASM_OP_SELECT_T)
            HANDLE_OP(WASM_OP_TABLE_GET)
            HANDLE_OP(WASM_OP_TABLE_SET)
            HANDLE_OP(WASM_OP_REF_NULL)
            HANDLE_OP(WASM_OP_REF_IS_NULL)
            HANDLE_OP(WASM_OP_REF_FUNC)
            HANDLE_OP(WASM_OP_UNUSED_0x14)
            HANDLE_OP(WASM_OP_UNUSED_0x15)
            HANDLE_OP(WASM_OP_UNUSED_0x16)
            HANDLE_OP(WASM_OP_UNUSED_0x17)
            HANDLE_OP(WASM_OP_UNUSED_0x18)
            HANDLE_OP(WASM_OP_UNUSED_0x19)
            HANDLE_OP(WASM_OP_UNUSED_0x27)
            HANDLE_OP(EXT_OP_SET_LOCAL_FAST_64)
            HANDLE_OP(EXT_OP_TEE_LOCAL_FAST_64)
            HANDLE_OP(EXT_OP_COPY_STACK_TOP)
            HANDLE_OP(EXT_OP_COPY_STACK_TOP_I64)
            HANDLE_OP(EXT_OP_COPY_STACK_VALUES)
            {
                wasm_set_exception(module, "unsupported opcode");
                goto got_exception;
            }
#endif

#if WASM_ENABLE_DISPATCH == 0
            continue;
#endif

        call_func_from_interp:
        {
            // 保存函数栈帧
            frame->ip = frame_ip;
            frame->sp = frame_sp - cur_func->param_cell_num;
            frame->lp = frame_lp;
            frame->cur_branch_table = cur_branch_table;

            prev_frame = frame;

            if (cur_func->func_kind)
            {
                wasm_interp_call_func_native(exec_env, fidx,
                                             prev_frame);

                // 在该情况下只有这些变量发生变化
                prev_frame = frame->prev_frame;
                cur_func = frame->function;
                frame_sp = frame->sp;

                /* update memory size, no need to update memory ptr as
                   it isn't changed in wasm_enlarge_memory */

                if (memory)
                    linear_mem_size = num_bytes_per_page * memory->cur_page_count;
                if (wasm_get_exception(module))
                    goto got_exception;
            }
            else
            {

                if (!(frame = ALLOC_FRAME(exec_env, prev_frame)))
                {
                    frame = prev_frame;
                    goto got_exception;
                }

                frame_lp = frame->lp = frame_sp - cur_func->param_cell_num;

                frame_ip = (uint8 *)cur_func->func_ptr;
                frame_ip_end = cur_func->code_end;

                frame_sp = frame->sp = frame_sp + cur_func->local_cell_num;

                // 切换跳转表
                cur_branch_table = cur_func->branch_table;
                branch_table = cur_func->branch_table;

                memset(frame_lp + cur_func->param_cell_num, 0,
                       (uint32)(cur_func->local_cell_num * 4));
            }
            HANDLE_OP_END();
        }

        return_func:
        {
            FREE_FRAME(exec_env, frame);

            if (!prev_frame->ip)
                /* Called from native. */
                return;

            // 恢复栈帧
            frame = prev_frame;
            cur_func = frame->function;
            prev_frame = frame->prev_frame;
            frame_ip = frame->ip;
            frame_ip_end = cur_func->code_end;
            frame_lp = frame->lp;
            frame_sp = frame->sp;
            cur_branch_table = frame->cur_branch_table;
            branch_table = cur_func->branch_table;
            HANDLE_OP_END();
        }

        out_of_bounds:
            wasm_set_exception(module, "out of bounds memory access");

        got_exception:
            return;

#if WASM_ENABLE_DISPATCH == 0
        }
    }
}
#else
}
#endif

#if WASM_ENABLE_JIT != 0
static bool
llvm_jit_call_func_bytecode(WASMModule *module_inst,
                            WASMExecEnv *exec_env,
                            WASMFunction *function, uint32 argc,
                            uint32 argv[])
{
    WASMType *func_type = function->func_type;
    uint32 result_count = func_type->result_count;
    uint32 ext_ret_count = result_count > 1 ? result_count - 1 : 0;
    uint32 func_idx = (uint32)(function - module_inst->functions);
    bool ret;

    if (ext_ret_count > 0)
    {
        uint32 cell_num = 0, i;
        uint8 *ext_ret_types = func_type->result + 1;
        uint32 argv1_buf[32], *argv1 = argv1_buf, *ext_rets = NULL;
        uint32 *argv_ret = argv;
        uint32 ext_ret_cell = wasm_get_cell_num(ext_ret_types, ext_ret_count);
        uint64 size;

        /* Allocate memory all arguments */
        size =
            sizeof(uint32) * (uint64)argc            /* original arguments */
            + sizeof(void *) * (uint64)ext_ret_count /* extra result values' addr */
            + sizeof(uint32) * (uint64)ext_ret_cell; /* extra result values */
        if (size > sizeof(argv1_buf))
        {
            if (size > UINT32_MAX || !(argv1 = wasm_runtime_malloc((uint32)size)))
            {
                wasm_set_exception(module_inst, "allocate memory failed");
                return false;
            }
        }

        /* Copy original arguments */
        memcpy(argv1, argv, sizeof(uint32) * argc);

        /* Get the extra result value's address */
        ext_rets =
            argv1 + argc + sizeof(void *) / sizeof(uint32) * ext_ret_count;

        /* Append each extra result value's address to original arguments */
        for (i = 0; i < ext_ret_count; i++)
        {
            *(uintptr_t *)(argv1 + argc + sizeof(void *) / sizeof(uint32) * i) =
                (uintptr_t)(ext_rets + cell_num);
            cell_num += wasm_value_type_cell_num(ext_ret_types[i]);
        }

        ret = wasm_runtime_invoke_native(
            exec_env, func_idx,
            argv1, argv);
        if (!ret)
        {
            if (argv1 != argv1_buf)
                wasm_runtime_free(argv1);
            return ret;
        }

        /* Get extra result values */
        switch (func_type->result[0])
        {
        case VALUE_TYPE_I32:
        case VALUE_TYPE_F32:
            argv_ret++;
            break;
        case VALUE_TYPE_I64:
        case VALUE_TYPE_F64:
            argv_ret += 2;
            break;
        default:
            break;
        }

        ext_rets =
            argv1 + argc + sizeof(void *) / sizeof(uint32) * ext_ret_count;
        memcpy(argv_ret, ext_rets,
               sizeof(uint32) * cell_num);

        if (argv1 != argv1_buf)
            wasm_runtime_free(argv1);
        return true;
    }
    else
    {
        ret = wasm_runtime_invoke_native(
            exec_env, func_idx, argv, argv);

        return ret;
    }
}
#endif /* end of WASM_ENABLE_JIT != 0 */

void wasm_interp_call_wasm(WASMModule *module_inst, WASMExecEnv *exec_env,
                           WASMFunction *function, uint32 argc,
                           uint32 argv[])
{
    WASMFuncFrame *frame;
    unsigned i;

    if (argc < function->param_cell_num)
    {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "invalid argument count %" PRIu32
                 ", must be no smaller than %u",
                 argc, function->param_cell_num);
        wasm_set_exception(module_inst, buf);
        return;
    }
    argc = function->param_cell_num;

    if (!(frame = ALLOC_FRAME(exec_env, NULL)))
        return;

    frame->ip = NULL;
    frame->sp = (uint32 *)exec_env->value_stack.top;

    if (argc > 0)
        word_copy(frame->sp, argv, argc);

    switch (function->func_kind)
    {
    case Wasm_Func:
#if WASM_ENABLE_JIT == 0
        wasm_interp_call_func_bytecode(module_inst, exec_env, function, frame);
#else
        llvm_jit_call_func_bytecode(module_inst, exec_env, function, argc, argv);
#endif
        break;
    case Native_Func:
        uint32 func_idx = (uint32)(function - module_inst->functions);
        wasm_interp_call_func_native(exec_env, func_idx, frame);
        break;
    default:
        break;
    }

    if (!wasm_get_exception(module_inst))
    {
        for (i = 0; i < function->ret_cell_num; i++)
        {
            argv[i] = *(frame->sp + i - function->ret_cell_num);
        }
    }
    FREE_FRAME(exec_env, frame);
}
