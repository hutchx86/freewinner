/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * comp_ref: per-window radial-distance reference used by the MSC table
 * builders (the deployment keeps it as a 256-entry double table in its
 * read-only data and memcpy's it to a stack array).
 *
 * The deployment's table is the distance from the centre of a 16x16 window:
 *
 *     ref[r][c] = sqrt(dr^2 + dc^2),   d(i) = 7 - i for i < 8, else i - 8
 *
 * rounded to five decimal places.  We re-derive the values from that geometry
 * instead of reproducing the vendor's bytes.
 *
 * This is deliberately NOT bit-identical to the deployment: nine of its 256
 * literals disagree with the re-derivation by 1e-5, because the deployment's
 * own literals are internally inconsistent (e.g. its [0][0] and [0][15] must
 * be equal and are not).  The private differential harness (config_msc_table)
 * is the arbiter for the difference across the tested envelope.
 */

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
