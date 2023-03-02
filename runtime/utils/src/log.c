#include "log.h"

static uint32 log_verbose_level = LOG_LEVEL_WARNING;

void
log_set_verbose_level(uint32 level)
{
    log_verbose_level = level;
}

void
runtime_log(LogLevel log_level, const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    char buf[32] = { 0 };
    uint64 usec;
    uint32 t, h, m, s, mills;

    if ((uint32)log_level > log_verbose_level)
        return;

    usec = os_time_get_boot_microsecond();
    t = (uint32)(usec / 1000000) % (24 * 60 * 60);
    h = t / (60 * 60);
    t = t % (60 * 60);
    m = t / 60;
    s = t % 60;
    mills = (uint32)(usec % 1000);

    snprintf(buf, sizeof(buf),
             "%02" PRIu32 ":%02" PRIu32 ":%02" PRIu32 ":%03" PRIu32, h, m, s,
             mills);

    os_printf("[%s]: ", buf);

    if (file)
        os_printf("%s, line %d, ", file, line);

    va_start(ap, fmt);
    os_vprintf(fmt, ap);
    va_end(ap);

    os_printf("\n");
}