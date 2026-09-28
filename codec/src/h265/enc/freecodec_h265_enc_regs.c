/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Volatile register access for the H.265 device (spec h265/11 section 5.7):
 * noinline, its own translation unit, so no compiler or LTO can fold an
 * engine access. */

#include "freecodec_h265_enc_priv.h"

__attribute__((noinline))
uint32_t fc_h265_reg_read(volatile uint32_t *base, unsigned int word_off)
{
    return base[word_off];
}

__attribute__((noinline))
void fc_h265_reg_write(volatile uint32_t *base, unsigned int word_off, uint32_t val)
{
    base[word_off] = val;
}
