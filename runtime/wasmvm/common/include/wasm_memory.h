#ifndef _WASM_MEMORY_H
#define _WASM_MEMORY_H

#include "platform.h"

void *
wasm_runtime_malloc(uint64 size);

void
wasm_runtime_free(void *ptr);

#endif