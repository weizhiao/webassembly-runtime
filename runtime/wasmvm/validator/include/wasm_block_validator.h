#ifndef _WASM_BLOCK_VALIDATOR_H
#define _WASM_BLOCK_VALIDATOR_H

#include "wasm_validator.h"

bool wasm_validator_push_block(WASMValidator *ctx, uint8 label_type,
                               BlockType block_type, uint8 *start_addr);

bool wasm_validator_pop_block(WASMModule *module, WASMValidator *ctx);

#define PUSH_BLOCK(loader_ctx, label_type, block_type, _start_addr)        \
    do                                                                     \
    {                                                                      \
        if (!wasm_validator_push_block(loader_ctx, label_type, block_type, \
                                       _start_addr))                       \
            goto fail;                                                     \
    } while (0)

#define POP_BLOCK()                                        \
    do                                                     \
    {                                                      \
        if (!wasm_validator_pop_block(module, loader_ctx)) \
            goto fail;                                     \
    } while (0)

#endif