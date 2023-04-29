#ifndef _RUNTIME_READ_VAR_H
#define _RUNTIME_READ_VAR_H

#include "platform.h"
#include "wasm_exception.h"

typedef enum LEB
{
    Varuint1 = 0,
    Varuint7,
    Varuint32,
    Varint7,
    Varint32,
    Varint64
} LEB;

bool read_leb(uint8 **p_buf, const uint8 *buf_end, LEB type, uint64 *p_result);

#define READ_TYPE_VALUE(Type, p) (p += sizeof(Type), *(Type *)(p - sizeof(Type)))
#define read_uint8(p) READ_TYPE_VALUE(uint8, p)
#define read_uint32(p) READ_TYPE_VALUE(uint32, p)

#define template_read_leb(p, p_end, type, leb_type, res)      \
    do                                                        \
    {                                                         \
        uint64 res64;                                         \
        if (!read_leb((uint8 **)&p, p_end, leb_type, &res64)) \
        {                                                     \
            wasm_set_exception(module, "integer too large");  \
            goto fail;                                        \
        }                                                     \
        res = (type)res64;                                    \
    } while (0)

#define read_leb_int64(p, p_end, res) template_read_leb(p, p_end, int64, Varint64, res)

#define read_leb_uint32(p, p_end, res) template_read_leb(p, p_end, uint32, Varuint32, res)

#define read_leb_int32(p, p_end, res) template_read_leb(p, p_end, int32, Varint32, res)

#define read_leb_int7(p, p_end, res) template_read_leb(p, p_end, int8, Varint7, res)

#define read_leb_uint7(p, p_end, res) template_read_leb(p, p_end, uint8, Varuint7, res)

#define read_leb_uint1(p, p_end, res) template_read_leb(p, p_end, uint8, Varuint1, res)

#endif