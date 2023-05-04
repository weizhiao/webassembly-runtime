#ifndef _LOADER_H
#define _LOADER_H

#include "wasm_loader_common.h"

// 初始化loader
bool init_load(const uint8 *buf, const uint8 *buf_end, WASMModule *module);

// 加载type段
bool load_type_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module);

// 加载import段
bool load_import_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module);

// 加载function段
bool load_function_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module);

// 加载table段
bool load_table_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module);

// 加载memory段
bool load_memory_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module);

// 加载global段
bool load_global_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module);

// 加载export段
bool load_export_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module);

// 加载start段
bool load_start_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module);

// 加载element段
bool load_element_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module);

// 加载code段
bool load_code_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module);

// 加载data段
bool load_data_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module);

// 加载datacount段
bool load_datacount_section(const uint8 *buf, const uint8 *buf_end,
                            WASMModule *module);

#endif