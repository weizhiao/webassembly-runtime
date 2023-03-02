#ifndef _LOADER_COMMON_H
#define _LOADER_COMMON_H

#include "wasm_type.h"
#include "runtime_utils.h"
#include "wasm_memory.h"

/// @brief 加载初始化表达式
/// @param p_buf 
/// @param buf_end 
/// @param init_expr 
/// @param type 
/// @param error_buf 
/// @param error_buf_size 
/// @return 
bool
load_init_expr(const uint8 **p_buf, const uint8 *buf_end,
               InitializerExpression *init_expr, uint8 type, char *error_buf,
               uint32 error_buf_size);


/// @brief 加载字符串
/// @param p_buf 
/// @param len 
/// @param str 
/// @param error_buf 
/// @param error_buf_size 
/// @return 
bool
load_utf8_str(const uint8 **p_buf, uint32 len, char**str, 
                char *error_buf, uint32 error_buf_size);


#endif