#include "wasm_fast_readleb.h"

uint64
fast_read_leb(const uint8 *buf, uint32 *p_offset, uint32 maxbits, bool sign)
{
    uint64 result = 0, byte;
    uint32 offset = *p_offset;
    uint32 shift = 0;

    while (true)
    {
        byte = buf[offset++];
        result |= ((byte & 0x7f) << shift);
        shift += 7;
        if ((byte & 0x80) == 0)
        {
            break;
        }
    }
    if (sign && (shift < maxbits) && (byte & 0x40))
    {
        result |= (~((uint64)0)) << shift;
    }
    *p_offset = offset;
    return result;
}