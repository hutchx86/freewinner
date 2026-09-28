// SPDX-License-Identifier: AGPL-3.0-only
/* systime.c - timed condition wait and clock helpers */
#include "utils/systime.h"
#include <errno.h>
#include <time.h>
#include <sys/time.h>

int pthread_cond_wait_timeout(pthread_cond_t *c, pthread_mutex_t *m,
                              unsigned int ms)
{
    struct timespec ts;

    if (c == NULL || m == NULL)
        return EINVAL;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(c, m, &ts);
}

long long CDX_GetTimeUs(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

long long CDX_SetTimeUs(long long us)
{
    (void)us;
    return CDX_GetTimeUs();
}
