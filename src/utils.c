#include "utils.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

uint64_t get_time_ms(void) {
#ifdef _WIN32
    // Returns the number of milliseconds that have elapsed since the system was started.
    return GetTickCount64();
#else
    struct timespec ts;
    // CLOCK_MONOTONIC is a clock that cannot be set and represents monotonic time since some unspecified starting point.
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
#endif
}
