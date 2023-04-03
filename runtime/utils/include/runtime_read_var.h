#ifndef _RUNTIME_READ_VAR_H
#define _RUNTIME_READ_VAR_H

#include"platform.h"

typedef enum LEB{
    Varuint1 = 0,
    Varuint7,
    Varuint32,
    Varint7,
    Varint32,
    Varint64
}LEB;

bool
read_leb(uint8 **p_buf, const uint8 *buf_end, LEB type,
         uint64 *p_result, char *error_buf, uint32 error_buf_size);

#define READ_TYPE_VALUE(Type, p) (p += sizeof(Type), *(Type *)(p - sizeof(Type)))
#define read_uint8(p) READ_TYPE_VALUE(uint8, p)
#define read_uint32(p) READ_TYPE_VALUE(uint32, p)

#define skip_leb(p) while (*p++ & 0x80)
#define skip_leb_int64(p, p_end) skip_leb(p)
#define skip_leb_uint32(p, p_end) skip_leb(p)
#define skip_leb_int32(p, p_end) skip_leb(p)

#define read_leb_int64(p, p_end, res)                                   \
    do {                                                                \
        uint64 res64;                                                   \
        if (!read_leb((uint8 **)&p, p_end, Varint64, &res64, error_buf, \
                      error_buf_size))                                  \
            goto fail;                                                  \
        res = (int64)res64;                                             \
    } while (0)

#define read_leb_uint32(p, p_end, res)                                   \
    do {                                                                 \
        uint64 res64;                                                    \
        if (!read_leb((uint8 **)&p, p_end, Varuint32, &res64, error_buf, \
                      error_buf_size))                                   \
            goto fail;                                                   \
        res = (uint32)res64;                                             \
    } while (0)

#define read_leb_int32(p, p_end, res)                                   \
    do {                                                                \
        uint64 res64;                                                   \
        if (!read_leb((uint8 **)&p, p_end, Varint32, &res64, error_buf, \
                      error_buf_size))                                  \
            goto fail;                                                  \
        res = (int32)res64;                                             \
    } while (0)

#define read_leb_int7(p, p_end, res)                                   \
    do {                                                                \
        uint64 res64;                                                   \
        if (!read_leb((uint8 **)&p, p_end, Varint7, &res64, error_buf, \
                      error_buf_size))                                  \
            goto fail;                                                  \
        res = (int32)res64;                                             \
    } while (0)

#define read_leb_uint7(p, p_end, res)                                   \
    do {                                                                \
        uint64 res64;                                                   \
        if (!read_leb((uint8 **)&p, p_end, Varuint7, &res64, error_buf, \
                      error_buf_size))                                  \
            goto fail;                                                  \
        res = (uint8)res64;                                             \
    } while (0)

#define read_leb_uint1(p, p_end, res)                                   \
    do {                                                                \
        uint64 res64;                                                   \
        if (!read_leb((uint8 **)&p, p_end, Varuint1, &res64, error_buf, \
                      error_buf_size))                                  \
            goto fail;                                                  \
        res = (uint8)res64;                                             \
    } while (0)

#endif