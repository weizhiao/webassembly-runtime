#include "runtime_check.h"

bool
check_utf8_str(const uint8 *str, uint32 len)
{
    const uint8 *p = str, *p_end = str + len;
    uint8 chr;

    while (p < p_end) {
        chr = *p;
        if (chr < 0x80) {
            p++;
        }
        else if (chr >= 0xC2 && chr <= 0xDF && p + 1 < p_end) {
            if (p[1] < 0x80 || p[1] > 0xBF) {
                return false;
            }
            p += 2;
        }
        else if (chr >= 0xE0 && chr <= 0xEF && p + 2 < p_end) {
            if (chr == 0xE0) {
                if (p[1] < 0xA0 || p[1] > 0xBF || p[2] < 0x80 || p[2] > 0xBF) {
                    return false;
                }
            }
            else if (chr == 0xED) {
                if (p[1] < 0x80 || p[1] > 0x9F || p[2] < 0x80 || p[2] > 0xBF) {
                    return false;
                }
            }
            else if (chr >= 0xE1 && chr <= 0xEF) {
                if (p[1] < 0x80 || p[1] > 0xBF || p[2] < 0x80 || p[2] > 0xBF) {
                    return false;
                }
            }
            p += 3;
        }
        else if (chr >= 0xF0 && chr <= 0xF4 && p + 3 < p_end) {
            if (chr == 0xF0) {
                if (p[1] < 0x90 || p[1] > 0xBF || p[2] < 0x80 || p[2] > 0xBF
                    || p[3] < 0x80 || p[3] > 0xBF) {
                    return false;
                }
            }
            else if (chr >= 0xF1 && chr <= 0xF3) {
                if (p[1] < 0x80 || p[1] > 0xBF || p[2] < 0x80 || p[2] > 0xBF
                    || p[3] < 0x80 || p[3] > 0xBF) {
                    return false;
                }
            }
            else if (chr == 0xF4) {
                if (p[1] < 0x80 || p[1] > 0x8F || p[2] < 0x80 || p[2] > 0xBF
                    || p[3] < 0x80 || p[3] > 0xBF) {
                    return false;
                }
            }
            p += 4;
        }
        else {
            return false;
        }
    }
    return (p == p_end);
}