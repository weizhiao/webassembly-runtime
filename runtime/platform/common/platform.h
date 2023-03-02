#ifndef _PLATFORM_H
#define _PLATFORM_H

#include "platform_api.h"

#define BH_MAX_THREAD 32

#define BHT_ERROR (-1)
#define BHT_TIMED_OUT (1)
#define BHT_OK (0)

#define BHT_WAIT_FOREVER ((uint64)-1LL)

#define BH_KB (1024)
#define BH_MB ((BH_KB)*1024)
#define BH_GB ((BH_MB)*1024)

#ifndef BH_MALLOC
#define BH_MALLOC os_malloc
#endif

#ifndef BH_FREE
#define BH_FREE os_free
#endif

#ifndef BH_TIME_T_MAX
#define BH_TIME_T_MAX LONG_MAX
#endif

void *
BH_MALLOC(unsigned int size);
void
BH_FREE(void *ptr);

/* Return the offset of the given field in the given type */
#ifndef offsetof
/* GCC 4.0 and later has the builtin. */
#if defined(__GNUC__) && __GNUC__ >= 4
#define offsetof(Type, field) __builtin_offsetof(Type, field)
#else
#define offsetof(Type, field) ((size_t)(&((Type *)0)->field))
#endif
#endif

typedef uint8_t uint8;
typedef int8_t int8;
typedef uint16_t uint16;
typedef int16_t int16;
typedef uint32_t uint32;
typedef int32_t int32;
typedef float float32;
typedef double float64;
typedef uint64_t uint64;
typedef int64_t int64;

#endif /* #ifndef _PLATFORM_H */
