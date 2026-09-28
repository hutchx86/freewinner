/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
#ifndef FREEISP_SDIV_H
#define FREEISP_SDIV_H

#include <stdint.h>

/* ARM SDIV/UDIV saturating division (x/0 = 0, INT32_MIN/-1 = INT32_MIN) instead of the
 * trapping __aeabi helpers; use for any divisor not provably nonzero (isp/spec/divide-semantics.md). */

static inline int32_t freeisp_sdiv(int32_t n, int32_t d)
{
    if (d == 0)
        return 0;
    if (n == INT32_MIN && d == -1)
        return INT32_MIN;
    return n / d;
}

static inline uint32_t freeisp_udiv(uint32_t n, uint32_t d)
{
    return d ? n / d : 0u;
}

#endif /* FREEISP_SDIV_H */
