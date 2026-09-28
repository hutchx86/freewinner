// SPDX-License-Identifier: AGPL-3.0-only
/* semaphore.c - counting semaphore */
#include "utils/semaphore.h"
#include <errno.h>
#include <time.h>

static void abs_deadline(struct timespec *ts, unsigned int ms)
{
    clock_gettime(CLOCK_MONOTONIC, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

int cdx_sem_init(cdx_sem_t *s, unsigned int val)
{
    pthread_condattr_t ca;
    int r;

    if (s == NULL)
        return FAILURE;
    if (pthread_condattr_init(&ca) != 0)
        return FAILURE;
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    r = pthread_cond_init(&s->condition, &ca);
    pthread_condattr_destroy(&ca);
    if (r != 0)
        return FAILURE;
    if (pthread_mutex_init(&s->mutex, NULL) != 0) {
        pthread_cond_destroy(&s->condition);
        return FAILURE;
    }
    s->semval = val;
    return SUCCESS;
}

void cdx_sem_deinit(cdx_sem_t *s)
{
    if (s == NULL)
        return;
    pthread_cond_destroy(&s->condition);
    pthread_mutex_destroy(&s->mutex);
}

void cdx_sem_down(cdx_sem_t *s)
{
    if (s == NULL)
        return;
    pthread_mutex_lock(&s->mutex);
    while (s->semval == 0)
        pthread_cond_wait(&s->condition, &s->mutex);
    s->semval--;
    pthread_mutex_unlock(&s->mutex);
}

int cdx_sem_down_timedwait(cdx_sem_t *s, unsigned int ms)
{
    int r = 0;

    if (s == NULL)
        return FAILURE;
    pthread_mutex_lock(&s->mutex);
    if (s->semval == 0) {
        struct timespec ts;
        abs_deadline(&ts, ms);
        r = pthread_cond_timedwait(&s->condition, &s->mutex, &ts);
    }
    if (s->semval > 0)
        s->semval--;
    pthread_mutex_unlock(&s->mutex);
    return r;
}

void cdx_sem_up(cdx_sem_t *s)
{
    if (s == NULL)
        return;
    pthread_mutex_lock(&s->mutex);
    s->semval++;
    pthread_cond_signal(&s->condition);
    pthread_mutex_unlock(&s->mutex);
}
