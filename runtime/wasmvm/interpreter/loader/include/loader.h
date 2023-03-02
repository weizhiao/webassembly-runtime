#ifndef _LOADER_H
#define _LOADER_H

#include "loader_common.h"

//加载type段
bool
load_type_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                char *error_buf, uint32 error_buf_size);

//加载import段
bool
load_import_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                char *error_buf, uint32 error_buf_size);

//加载function段
bool
load_function_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module, 
                char *error_buf, uint32 error_buf_size);

//加载table段
bool
load_table_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                   char *error_buf, uint32 error_buf_size);

//加载memory段
bool
load_memory_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    char *error_buf, uint32 error_buf_size);

//加载global段
bool
load_global_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    char *error_buf, uint32 error_buf_size);
            
//加载export段
bool
load_export_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    char *error_buf, uint32 error_buf_size);

//加载start段
bool
load_start_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    char *error_buf, uint32 error_buf_size);

//加载element段
bool
load_element_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    char *error_buf, uint32 error_buf_size);

//加载code段
bool
load_code_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    char *error_buf, uint32 error_buf_size);

//加载data段
bool
load_data_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    char *error_buf, uint32 error_buf_size);

#endif