#ifndef _RUNTIME_CHECK_H
#define _RUNTIME_CHECK_H

#include "platform.h"
#include "wasm_type.h"

//检查字符串是否是utf-8编码
bool
check_utf8_str(const uint8 *str, uint32 len);

//检查类型是否为value
static inline bool
is_value_type(uint8 type)
{
    if (type == VALUE_TYPE_I32 || type == VALUE_TYPE_I64
        || type == VALUE_TYPE_F32 || type == VALUE_TYPE_F64
#if WASM_ENABLE_REF_TYPES != 0
        || type == VALUE_TYPE_FUNCREF || type == VALUE_TYPE_EXTERNREF
#endif
    )
        return true;
    return false;
}

//检查两个type是否相同
inline static bool
wasm_type_equal(const WASMType *type1, const WASMType *type2)
{
    if (type1 == type2) {
        return true;
    }
    return (type1->param_count == type2->param_count
            && type1->result_count == type2->result_count
            && memcmp(type1->param, type2->param,(uint32)type1->param_count) == 0
            && memcmp(type1->result, type2->result, (uint32)type1->result_count) == 0
            )
               ? true
               : false;
}

#endif