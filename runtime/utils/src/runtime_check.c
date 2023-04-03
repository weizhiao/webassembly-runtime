#include "runtime_check.h"

bool
check_table_max_size(uint32 init_size, uint32 max_size, char *error_buf,
                     uint32 error_buf_size)
{
    if (max_size < init_size) {
        set_error_buf(error_buf, error_buf_size,
                      "size minimum must not be greater than maximum");
        return false;
    }
    return true;
}

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

void
set_error_buf(char *error_buf, uint32 error_buf_size, const char *string)
{
    if (error_buf != NULL) {
        snprintf(error_buf, error_buf_size, "WASM module load failed: %s",
                 string);
    }
}

void
set_error_buf_v(char *error_buf, uint32 error_buf_size, const char *format, ...)
{
    va_list args;
    char buf[128];

    if (error_buf != NULL) {
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        snprintf(error_buf, error_buf_size, "WASM module load failed: %s", buf);
    }
}

bool
is_value_type(uint8 type)
{
    if (type == VALUE_TYPE_I32 || type == VALUE_TYPE_I64
        || type == VALUE_TYPE_F32 || type == VALUE_TYPE_F64
    )
        return true;
    return false;
}

bool
check_memory_init_size(uint32 init_size, char *error_buf, uint32 error_buf_size)
{
    if (init_size > DEFAULT_MAX_PAGES) {
        set_error_buf(error_buf, error_buf_size,
                      "memory size must be at most 65536 pages (4GiB)");
        return false;
    }
    return true;
}

bool
check_memory_max_size(uint32 init_size, uint32 max_size, char *error_buf,
                      uint32 error_buf_size)
{
    if (max_size < init_size) {
        set_error_buf(error_buf, error_buf_size,
                      "size minimum must not be greater than maximum");
        return false;
    }

    if (max_size > DEFAULT_MAX_PAGES) {
        set_error_buf(error_buf, error_buf_size,
                      "memory size must be at most 65536 pages (4GiB)");
        return false;
    }
    return true;
}