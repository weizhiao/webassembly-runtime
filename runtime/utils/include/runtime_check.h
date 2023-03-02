#ifndef _RUNTIME_CHECK_H
#define _RUNTIME_CHECK_H

#include "platform.h"
#include "wasm_type.h"

//检查memory的最大大小
bool
check_memory_max_size(uint32 init_size, uint32 max_size, char *error_buf,
                      uint32 error_buf_size);

//检查memory的初始大小
bool
check_memory_init_size(uint32 init_size, char *error_buf, uint32 error_buf_size);

//检查表的最大大小是否正确
bool
check_table_max_size(uint32 init_size, uint32 max_size, char *error_buf,
                     uint32 error_buf_size);

//检查字符串是否是utf-8编码
bool
check_utf8_str(const uint8 *str, uint32 len);

//设置加载时的错误类型
void
set_error_buf(char *error_buf, uint32 error_buf_size, const char *string);

//检查类型是否为value
bool
is_value_type(uint8 type);

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