#ifndef _LOADER_EXPORT_H
#define _LOADER_EXPORT_H

#include "wasm_type.h"

//最终向外界暴露的接口，用于wasm文件的加载
WASMModule *
wasm_loader(uint8 *buf, uint32 size, char *error_buf, uint32 error_buf_size);

#endif