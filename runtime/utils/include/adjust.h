#ifndef _ADJUST_H
#define _ADJUST_H

#include "platform.h"
#include "wasm_type.h"

void
adjust_table_max_size(uint32 init_size, uint32 max_size_flag, uint32 *max_size);

#endif