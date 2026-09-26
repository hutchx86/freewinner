// SPDX-License-Identifier: AGPL-3.0-only
#ifndef UTILS_SYSTIME_H
#define UTILS_SYSTIME_H
#include "media_utils_abi.h"

int  pthread_cond_wait_timeout(pthread_cond_t *c, pthread_mutex_t *m,
                               unsigned int ms);
long long CDX_GetTimeUs(void);
long long CDX_SetTimeUs(long long us);

#endif
