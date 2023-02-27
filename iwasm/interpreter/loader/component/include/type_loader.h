#ifndef _TYPE_LOADER_H
#define _TYPE_LOADER_H

#include"platform.h"

bool
load_type_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                  char *error_buf, uint32 error_buf_size);

#endif