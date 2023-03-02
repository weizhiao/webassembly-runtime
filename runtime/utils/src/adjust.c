#include "adjust.h"

void
adjust_table_max_size(uint32 init_size, uint32 max_size_flag, uint32 *max_size)
{
    uint32 default_max_size =
        init_size * 2 > TABLE_MAX_SIZE ? init_size * 2 : TABLE_MAX_SIZE;

    if (max_size_flag) {
        *max_size = *max_size < default_max_size ? *max_size : default_max_size;
    }
    else {
        *max_size = default_max_size;
    }
}