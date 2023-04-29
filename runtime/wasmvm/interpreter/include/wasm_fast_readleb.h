#ifndef _WASM_FAST_READLEB_H
#define _WASM_FAST_READLEB_H

#include "wasm_type.h"

uint64
fast_read_leb(const uint8 *buf, uint32 *p_offset, uint32 maxbits, bool sign);

#define read_leb_int64(p, p_end, res)                   \
    do                                                  \
    {                                                   \
        uint8 _val = *p;                                \
        if (!(_val & 0x80))                             \
        {                                               \
            res = (int64)_val;                          \
            if (_val & 0x40)                            \
                /* sign extend */                       \
                res |= 0xFFFFFFFFFFFFFF80LL;            \
            p++;                                        \
            break;                                      \
        }                                               \
        uint32 _off = 0;                                \
        res = (int64)fast_read_leb(p, &_off, 64, true); \
        p += _off;                                      \
    } while (0)

#define read_leb_uint32(p, p_end, res)                    \
    do                                                    \
    {                                                     \
        uint8 _val = *p;                                  \
        if (!(_val & 0x80))                               \
        {                                                 \
            res = _val;                                   \
            p++;                                          \
            break;                                        \
        }                                                 \
        uint32 _off = 0;                                  \
        res = (uint32)fast_read_leb(p, &_off, 32, false); \
        p += _off;                                        \
    } while (0)

#define read_leb_int32(p, p_end, res)                   \
    do                                                  \
    {                                                   \
        uint8 _val = *p;                                \
        if (!(_val & 0x80))                             \
        {                                               \
            res = (int32)_val;                          \
            if (_val & 0x40)                            \
                /* sign extend */                       \
                res |= 0xFFFFFF80;                      \
            p++;                                        \
            break;                                      \
        }                                               \
        uint32 _off = 0;                                \
        res = (int32)fast_read_leb(p, &_off, 32, true); \
        p += _off;                                      \
    } while (0)

#define skip_leb(p) while (*p++ & 0x80)
#define skip_leb_int64(p, p_end) skip_leb(p)
#define skip_leb_uint32(p, p_end) skip_leb(p)
#define skip_leb_int32(p, p_end) skip_leb(p)
#define READ_TYPE_VALUE(Type, p) (p += sizeof(Type), *(Type *)(p - sizeof(Type)))
#define read_uint8(p) READ_TYPE_VALUE(uint8, p)
#define read_uint32(p) READ_TYPE_VALUE(uint32, p)

#endif