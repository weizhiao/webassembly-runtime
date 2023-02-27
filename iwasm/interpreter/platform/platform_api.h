#ifndef _PLATFORM_API_H
#define _PLATFORM_API_H

#include "platform_internal.h"

int
bh_platform_init(void);

void
bh_platform_destroy(void);

void *
os_malloc(unsigned size);

void *
os_realloc(void *ptr, unsigned size);

void
os_free(void *ptr);

int
os_printf(const char *format, ...);

#endif /* #ifndef _PLATFORM_API_H */
