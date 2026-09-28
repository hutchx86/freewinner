/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* comp_ref.c - per-window radial-distance reference for the MSC table builders:
 * ref[r][c] = sqrt(d(r)^2 + d(c)^2), d(i) = 7 - i for i < 8 else i - 8, rounded to 1e-5.
 * Re-derived from that geometry; nine of the deployed 256 literals differ by 1e-5
 * because they are internally inconsistent (e.g. [0][0] != [0][15]). */

#include <math.h>

#include "base.h"

#define COMP_REF_DIM 16

void freeisp_comp_ref_fill(double out[256])
{
    unsigned r, c;

    for (r = 0; r < COMP_REF_DIM; r++) {
        int dr = (r < 8) ? (int)(7 - r) : (int)(r - 8);

        for (c = 0; c < COMP_REF_DIM; c++) {
            int dc = (c < 8) ? (int)(7 - c) : (int)(c - 8);
            double d = sqrt((double)(dr * dr + dc * dc));

            out[r * COMP_REF_DIM + c] =
                (double)(long long)(d * 100000.0 + 0.5) / 100000.0;
        }
    }
}
