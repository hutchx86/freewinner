/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
#ifndef FREEISP_SDIV_H
#define FREEISP_SDIV_H

#include <stdint.h>

/*
 * Integer divide semantics for the register/config tier.
 *
 * GCC lowers a data-dependent C division to the ABI helpers
 * `__aeabi_idiv`/`__aeabi_uidiv`, which raise SIGFPE when the divisor is zero
 * and, for the signed form, when the quotient is INT32_MIN / -1.
 *
 * The deployed pipeline does not fault on those inputs: its divisions
 * saturate, which is the behaviour the target architecture specifies for its
 * integer divide instructions --
 *
 *   divisor 0        ->  0
 *   INT32_MIN / -1   ->  INT32_MIN
 *
 * On camera the all-clean `mediad` crashed with SIGFPE on real statistics data
 * because the clean tier used the trapping helpers (2026-09-18): the inputs
 * that fault are reachable, and no caller upstream rejects them.
 *
 * Every division in the clean tier whose divisor is not provably nonzero must
 * go through one of these, so the clean code has the deployed pipeline's
 * arithmetic behaviour rather than the helper's trap.
 */

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
