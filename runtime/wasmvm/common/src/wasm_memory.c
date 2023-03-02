#include"wasm_memory.h"

void *
wasm_runtime_malloc(uint64 size)
{
    return os_malloc((uint32)size);
}

void
wasm_runtime_free(void *ptr)
{
    os_free(ptr);
}