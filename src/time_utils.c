#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include "time_utils.h"
#include <sys/time.h>


long long current_time_millis() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)(tv.tv_sec) * 1000 + (long long)(tv.tv_usec) / 1000;
}

