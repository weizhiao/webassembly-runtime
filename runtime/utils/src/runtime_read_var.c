#include "runtime_read_var.h"
#include "runtime_check.h"

//读取变长数字
bool
read_leb(uint8 **p_buf, const uint8 *buf_end, uint32 maxbits, bool sign,
         uint64 *p_result, char *error_buf, uint32 error_buf_size)
{
    const uint8 *buf = *p_buf;
    uint64 result = 0;
    uint32 shift = 0;
    uint32 offset = 0, bcnt = 0;
    uint8 byte;
    uint64 mask;
    uint64 over_bits;

    while (true) {
        if (bcnt + 1 > (maxbits + 6) / 7) {
            goto fail;
        }
        byte = buf[offset];
        offset += 1;
        result |= ((byte & 0x7f) << shift);
        shift += 7;
        bcnt += 1;
        if ((byte & 0x80) == 0) {
            break;
        }
    }
    
    mask = (uint64)-1;
    mask = mask << maxbits;
    over_bits = result & mask;//记录溢出maxbits的比特

    if(!sign){
        if(over_bits) goto fail;
    }
    else{
        if(!over_bits && byte & 0x40) {
            result |= (~((uint64)0)) << shift;
        }
        else if(over_bits){
            byte = (uint8)(result >> (maxbits - 1)) & (uint8)1;//获取maxbits处的字节值
            if(!byte) goto fail;//为0则说明result不是负数，那么溢出的肯定有问题
            mask = mask ^ over_bits;//做异或运算，处于overbits中的位若不为0，则说明不是全1。
            mask = mask <<(sizeof(uint64) * 8 - shift);
            if(mask) goto fail;
            result |= (~((uint64)0)) << shift;
        } 
    }
    *p_buf += offset;
    *p_result = result;
    return true;

fail:
    set_error_buf(error_buf, error_buf_size, "integer too large");
    return false;
}