#ifndef _WASM_STACK_VALIDATOR_H
#define _WASM_STACK_VALIDATOR_H

#include "wasm_validator.h"

#define CHECK_STACK_POP(cur_type, expect_type)           \
    do                                                   \
    {                                                    \
        if (cur_type != expect_type)                     \
        {                                                \
            wasm_set_exception(module, "type mismatch"); \
            return false;                                \
        }                                                \
    } while (0)

bool wasm_loader_push_frame_ref(WASMModule *module, WASMLoaderContext *ctx, uint8 type);

bool wasm_loader_pop_frame_ref(WASMModule *module, WASMLoaderContext *ctx, uint8 type);

#define TEMPLATE_PUSH(Type)                                                       \
    do                                                                            \
    {                                                                             \
        if (!(wasm_loader_push_frame_ref(module, loader_ctx, VALUE_TYPE_##Type))) \
            goto fail;                                                            \
    } while (0)

#define TEMPLATE_POP(Type)                                                       \
    do                                                                           \
    {                                                                            \
        if (!(wasm_loader_pop_frame_ref(module, loader_ctx, VALUE_TYPE_##Type))) \
            goto fail;                                                           \
    } while (0)

#define PUSH_I32() TEMPLATE_PUSH(I32)
#define PUSH_F32() TEMPLATE_PUSH(F32)
#define PUSH_I64() TEMPLATE_PUSH(I64)
#define PUSH_F64() TEMPLATE_PUSH(F64)
#define PUSH_V128() TEMPLATE_PUSH(V128)
#define PUSH_FUNCREF() TEMPLATE_PUSH(FUNCREF)
#define PUSH_EXTERNREF() TEMPLATE_PUSH(EXTERNREF)

#define POP_I32() TEMPLATE_POP(I32)
#define POP_F32() TEMPLATE_POP(F32)
#define POP_I64() TEMPLATE_POP(I64)
#define POP_F64() TEMPLATE_POP(F64)
#define POP_V128() TEMPLATE_POP(V128)
#define POP_FUNCREF() TEMPLATE_POP(FUNCREF)
#define POP_EXTERNREF() TEMPLATE_POP(EXTERNREF)

#define PUSH_TYPE(type)                                              \
    do                                                               \
    {                                                                \
        if (!(wasm_loader_push_frame_ref(module, loader_ctx, type))) \
            goto fail;                                               \
    } while (0)

#define POP_TYPE(type)                                              \
    do                                                              \
    {                                                               \
        if (!(wasm_loader_pop_frame_ref(module, loader_ctx, type))) \
            goto fail;                                              \
    } while (0)

#define POP_AND_PUSH(type_pop, type_push) \
    do                                    \
    {                                     \
        POP_TYPE(type_pop);               \
        PUSH_TYPE(type_push);             \
    } while (0)

#define POP2_AND_PUSH(type_pop, type_push) \
    do                                     \
    {                                      \
        POP_TYPE(type_pop);                \
        POP_TYPE(type_pop);                \
        PUSH_TYPE(type_push);              \
    } while (0)

#endif