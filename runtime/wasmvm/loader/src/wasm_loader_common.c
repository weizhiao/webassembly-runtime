#include "wasm_loader_common.h"

static bool
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

bool
load_init_expr(WASMModule *module, const uint8 **p_buf, const uint8 *buf_end,
               InitializerExpression *init_expr, uint8 type)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint8 flag, end_byte, *p_float;
    uint32 i;

    init_expr->init_expr_type = read_uint8(p);
    flag = init_expr->init_expr_type;

    switch (flag) {
        /* i32.const */
        case INIT_EXPR_TYPE_I32_CONST:
            if (type != VALUE_TYPE_I32)
                goto fail_type_mismatch;
            read_leb_int32(p, p_end, init_expr->u.i32);
            break;
        /* i64.const */
        case INIT_EXPR_TYPE_I64_CONST:
            if (type != VALUE_TYPE_I64)
                goto fail_type_mismatch;
            read_leb_int64(p, p_end, init_expr->u.i64);
            break;
        /* f32.const */
        case INIT_EXPR_TYPE_F32_CONST:
            if (type != VALUE_TYPE_F32)
                goto fail_type_mismatch;
            p_float = (uint8 *)&init_expr->u.f32;
            for (i = 0; i < sizeof(float32); i++)
                *p_float++ = *p++;
            break;
        /* f64.const */
        case INIT_EXPR_TYPE_F64_CONST:
            if (type != VALUE_TYPE_F64)
                goto fail_type_mismatch;
            p_float = (uint8 *)&init_expr->u.f64;
            for (i = 0; i < sizeof(float64); i++)
                *p_float++ = *p++;
            break;

        case INIT_EXPR_TYPE_GET_GLOBAL:
            read_leb_uint32(p, p_end, init_expr->u.global_index);
            break;
        default:
        {
            wasm_set_exception(module, "illegal opcode "
                          "or constant expression required "
                          "or type mismatch");
            goto fail;
        }
    }
    end_byte = read_uint8(p);
    if (end_byte != 0x0b)
        goto fail_type_mismatch;
    *p_buf = p;
    return true;

fail_type_mismatch:
    wasm_set_exception(module, "type mismatch or constant expression required");
fail:
    return false;
}

bool
load_utf8_str(WASMModule *module, const uint8 **p_buf, uint32 len, char**str)
{
    uint8* p = (uint8*)*p_buf;
    if (!check_utf8_str(p, len)) {
        wasm_set_exception(module, "invalid UTF-8 encoding");
        *str = NULL;
        return false;
    }

    if (len == 0) {
        *str = "";
        return true;
    }

    char *c_str = (char *)p - 1;
    memmove(c_str, c_str + 1, len);
    c_str[len] = '\0';

    *str = c_str;

    *p_buf = p + len;

    return true;
}