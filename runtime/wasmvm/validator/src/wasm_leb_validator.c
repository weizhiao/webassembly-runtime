#include "wasm_leb_validator.h"

static uint32 maxloopcount[] = {
    0, 0, 4, 0, 4, 9};

static const uint64 mask_leb1 = (uint64)-1 << 1;
static const uint64 mask_lebu32 = (uint64)-1 << 32;
static const uint64 mask_lebi32 = (uint64)-1 << 31;

static const uint64 mask_overbits_lebi32 = (uint64)0b1111 << 31;

static inline bool
check_buf(const uint8 *buf, const uint8 *buf_end, uint32 length)
{
    if ((uintptr_t)buf + length < (uintptr_t)buf || (uintptr_t)buf + length > (uintptr_t)buf_end)
    {
        return false;
    }
    return true;
}

// 读取变长数字
bool read_leb(uint8 **p_buf, const uint8 *buf_end, LEB type,
              uint64 *p_result)
{
    const uint8 *buf = *p_buf;
    uint64 result = 0;
    uint32 shift = 0;
    uint32 offset = 0, bcnt = 0;
    uint8 byte;
    uint64 over_bits;
    uint32 loopcount = maxloopcount[type];

    while (true)
    {
        if (bcnt > loopcount)
        {
            goto fail;
        }
        if (!check_buf(buf, buf_end, offset + 1))
        {
            goto fail;
        }
        byte = buf[offset];
        offset += 1;
        result |= ((byte & (uint64)0x7f) << shift);
        shift += 7;
        bcnt += 1;
        if ((byte & 0x80) == 0)
        {
            break;
        }
    }

    switch (type)
    {
    case Varuint1:
        over_bits = mask_leb1 & result;
        if (over_bits)
        {
            goto fail;
        }
        break;
    case Varuint7:
        break;
    case Varuint32:
        over_bits = mask_lebu32 & result;
        if (over_bits)
        {
            goto fail;
        }
        break;
    case Varint7:
        if (byte & 0x40)
        {
            result |= (~((uint64)0)) << shift;
        }
        break;
    case Varint32:
        over_bits = mask_lebi32 & result;
        if (!over_bits && byte & 0x40)
        {
            result |= (~((uint64)0)) << shift;
        }
        else if (over_bits && !(over_bits ^ mask_overbits_lebi32))
        {
            result |= (~((uint64)0)) << shift;
        }
        else if (over_bits && over_bits ^ mask_overbits_lebi32)
        {
            goto fail;
        }
        break;
    case Varint64:
        if (shift < 64)
        {
            if (byte & 0x40)
            {
                result |= (~((uint64)0)) << shift;
            }
        }
        else
        {
            bool sign_bit_set = byte & 0x1;
            int top_bits = byte & 0xfe;

            if ((sign_bit_set && top_bits != 0x7e) || (!sign_bit_set && top_bits != 0))
                goto fail;
        }
        break;
    }

    *p_buf += offset;
    *p_result = result;
    return true;

fail:
    return false;
}