/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Helpers for the compile-time layout checks of the binary interfaces
 * (spec r2/04 section 6, spec r2/05 part D). The interfaces are defined for
 * the 32-bit ARM EABI target; the host test build only re-checks what does
 * not depend on the data model:
 *
 *   FC_ABI_CHECK_ALL     records made of 4-byte integers and enums only
 *   FC_ABI_CHECK_PTR32   records holding pointers: any ILP32 target
 *   FC_ABI_CHECK_EABI    records holding 64-bit integers, whose alignment
 *                        (8 bytes) is an ARM EABI property */

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
