#ifndef _PLATFORM_API_H
#define _PLATFORM_API_H

#include "platform_common.h"

enum {
    MMAP_PROT_NONE = 0,
    MMAP_PROT_READ = 1,
    MMAP_PROT_WRITE = 2,
    MMAP_PROT_EXEC = 4
};

/* Memory map flags */
enum {
    MMAP_MAP_NONE = 0,
    /* Put the mapping into 0 to 2 G, supported only on x86_64 */
    MMAP_MAP_32BIT = 1,
    /* Don't interpret addr as a hint: place the mapping at exactly
       that address. */
    MMAP_MAP_FIXED = 2
};

int
platform_init(void);

void
platform_destroy(void);

unsigned char *
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

uint64
os_time_get_boot_microsecond();


korp_tid
os_self_thread(void);

#endif /* #ifndef _PLATFORM_API_H */
