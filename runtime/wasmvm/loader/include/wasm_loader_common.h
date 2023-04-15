#ifndef _LOADER_COMMON_H
#define _LOADER_COMMON_H

#include "wasm_type.h"
#include "runtime_utils.h"
#include "wasm_leb_validator.h"
#include "wasm_memory.h"
#include "wasm_exception.h"

/// @brief 加载初始化表达式
/// @param p_buf 
/// @param buf_end 
/// @param init_expr 
/// @param type 
/// @param error_buf 
/// @param error_buf_size 
/// @return 
bool
load_init_expr(WASMModule *module, const uint8 **p_buf, const uint8 *buf_end,
               InitializerExpression *init_expr, uint8 type);


/// @brief 加载字符串
/// @param p_buf 
/// @param len 
/// @param str 
/// @param error_buf 
/// @param error_buf_size 
/// @return 
bool
load_utf8_str(WASMModule *module, const uint8 **p_buf, uint32 len, char**str);


#endif