#ifndef _WASM_RUNTIME_LOADER_API_H
#define _WASM_RUNTIME_LOADER_API_H

#include "wasm_type.h"

//最终向外界暴露的接口，用于wasm文件的加载
WASMModule *
wasm_loader(uint8 *buf, uint32 size);

#endif