/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Register access (spec 12 section 5.7): volatile pointers, noinline, in their
 * own translation unit, so no compiler, flag set or LTO can make an access
 * non-volatile. */

#include "freecodec_enc_priv.h"

__attribute__((noinline))
uint32_t fc_enc_reg_read(volatile uint32_t *base, unsigned int word_off)
{
    return base[word_off];
}

__attribute__((noinline))
void fc_enc_reg_write(volatile uint32_t *base, unsigned int word_off, uint32_t val)
{
    base[word_off] = val;
}

__attribute__((noinline))
void fc_enc_reg_write_block(volatile uint32_t *base, const uint32_t *words,
                            unsigned int n)
{
    unsigned int i;

    for (i = 0; i < n; i++)
        base[i] = words[i];
}
