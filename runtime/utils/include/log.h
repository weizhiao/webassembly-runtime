#ifndef _LOG_H
#define _LOG_H

#include "platform.h"

typedef enum {
    LOG_LEVEL_FATAL = 0,
    LOG_LEVEL_ERROR = 1,
    LOG_LEVEL_WARNING = 2,
    LOG_LEVEL_DEBUG = 3,
    LOG_LEVEL_VERBOSE = 4
} LogLevel;

void
log_set_verbose_level(uint32 level);

void
runtime_log(LogLevel log_level, const char *file, int line, const char *fmt, ...);

#define LOG_ERROR(...) runtime_log(LOG_LEVEL_ERROR, NULL, 0, __VA_ARGS__)
#define LOG_WARNING(...) runtime_log(LOG_LEVEL_WARNING, NULL, 0, __VA_ARGS__)
#define LOG_VERBOSE(...) runtime_log(LOG_LEVEL_VERBOSE, NULL, 0, __VA_ARGS__)

#if DEBUG != 0
#define LOG_DEBUG(...) \
    runtime_log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#else
#define LOG_DEBUG(...) (void)0
#endif

#endif /* _LOG_H */
