// SPDX-License-Identifier: AGPL-3.0-only
#ifndef UTILS_SEMAPHORE_H
#define UTILS_SEMAPHORE_H
#include "media_utils_abi.h"

int  cdx_sem_init(cdx_sem_t *s, unsigned int val);
void cdx_sem_deinit(cdx_sem_t *s);
void cdx_sem_down(cdx_sem_t *s);
int  cdx_sem_down_timedwait(cdx_sem_t *s, unsigned int timeout_ms);
void cdx_sem_up(cdx_sem_t *s);

#endif
