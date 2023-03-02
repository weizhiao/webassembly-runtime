#ifndef _PLATFORM_API_H
#define _PLATFORM_API_H

#include "platform_internal.h"

int
platform_init(void);

void
platform_destroy(void);

char *
platform_read_file(const char *filename, unsigned int *ret_size);

void *
os_malloc(unsigned size);

void *
os_realloc(void *ptr, unsigned size);

void
os_free(void *ptr);

int
os_printf(const char *format, ...);

int
os_vprintf(const char *format, va_list ap);

long
os_time_get_boot_microsecond(void);

#endif /* #ifndef _PLATFORM_API_H */
