/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Compile-time layout checks (spec r2/04 s6, r2/05 part D) for the 32-bit ARM
 * EABI target. FC_ABI_CHECK_ALL: 4-byte integers/enums only; _PTR32: records
 * with pointers (any ILP32); _EABI: records with 64-bit integers (8-byte align). */

#ifndef FREECODEC_ABI_CHECK_H
#define FREECODEC_ABI_CHECK_H

#include <stddef.h>
#include <stdint.h>

#define FC_ABI_CHECK_ALL 1

#if UINTPTR_MAX == 0xffffffffu
#define FC_ABI_CHECK_PTR32 1
#else
#define FC_ABI_CHECK_PTR32 0
#endif

#if defined(__arm__) && defined(__ARM_EABI__)
#define FC_ABI_CHECK_EABI 1
#else
#define FC_ABI_CHECK_EABI 0
#endif

/* Assert that member `m` of type `t` sits at byte offset `off`. */
#define FC_ABI_OFFSET(t, m, off) \
    _Static_assert(offsetof(t, m) == (off), #t "." #m " at offset " #off)

/* Assert that type `t` is `n` bytes. */
#define FC_ABI_SIZE(t, n) \
    _Static_assert(sizeof(t) == (n), #t " is " #n " bytes")

#endif /* FREECODEC_ABI_CHECK_H */
