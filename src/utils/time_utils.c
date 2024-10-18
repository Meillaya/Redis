#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include "time_utils.h"

/**
 * Returns the current time in milliseconds since the epoch.
 */
long long current_time_millis() {
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);

    long long ms = spec.tv_sec * 1000LL + spec.tv_nsec / 1000000LL;    return ms;
}
