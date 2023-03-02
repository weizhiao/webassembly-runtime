#include "platform_api.h"

long
os_time_get_boot_microsecond()
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return ts.tv_sec * 1000 * 1000 + ts.tv_nsec / 1000;
}