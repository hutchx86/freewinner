/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.264 engine work buffers whose size or contents are pure geometry: the
 * MB-RC buffer and its per-row budget table (spec r2/01) and the auxiliary
 * plane size (spec r2/03). Header-only so host tests run the exact code. */

#ifndef FREECODEC_H264_BUFS_H
#define FREECODEC_H264_BUFS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------ MB-RC buffer */

/* Layout of the shared MB-RC buffer (spec r2/01 section 2). */
#define FC_MBRC_ACTIVITY_OFFSET   0u    /* R little-endian u16, engine-written  */
#define FC_MBRC_TABLE_OFFSET      512u  /* R-1 records, software-written        */
#define FC_MBRC_RECORD_WORDS      6u
#define FC_MBRC_RECORD_BYTES      (2u * FC_MBRC_RECORD_WORDS)
#define FC_MBRC_MAX_ROWS          256u  /* activity words must end below 512   */
#define FC_MBRC_SHARE_UNITS       2048u /* a row share is in 1/2048 of a picture */

/* Buffer size for `rows` macroblock rows: 512 + 12 * (rows - 1). */
static inline unsigned int fc_mbrc_buffer_bytes(unsigned int rows)
{
    return 12u * rows + 500u;
}

/* Cache maintenance hook: one clean+invalidate over [mem, mem + size). */
typedef void (*fc_mbrc_sync_fn)(void *opaque, void *mem, int size);

static inline unsigned int fc_mbrc_get16(const uint8_t *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static inline void fc_mbrc_put16(uint8_t *p, unsigned int v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}

/* Rebuild the per-row budget table from the engine's per-row activity (spec r2/01
 * s4); `sync` (may be NULL) runs before reading and after writing. -1 = bad rows. */
static inline int fc_mbrc_build_table(uint8_t *buf, unsigned int rows,
                                      fc_mbrc_sync_fn sync, void *opaque)
{
    /* Tolerance multipliers applied to the cumulative share, in 1/32 units
     * (spec r2/01 section 5): three upper limits, then three lower limits. */
    static const uint8_t weight32[FC_MBRC_RECORD_WORDS] = {
        38, 45, 51,     /* 1 + 6/32, 1 + 13/32, 1 + 19/32 */
        26, 19, 13      /* 1 - 6/32, 1 - 13/32, 1 - 19/32 */
    };
    uint32_t total = 0u;
    uint32_t cum = 0u;
    unsigned int r, k;

    if (buf == 0 || rows == 0u || rows > FC_MBRC_MAX_ROWS)
        return -1;

    if (sync != 0)
        sync(opaque, buf, (int)fc_mbrc_buffer_bytes(rows));

    for (r = 0; r < rows; r++)
        total += fc_mbrc_get16(buf + FC_MBRC_ACTIVITY_OFFSET + 2u * r);

    for (r = 0; r + 1u < rows; r++) {
        uint8_t *rec = buf + FC_MBRC_TABLE_OFFSET + FC_MBRC_RECORD_BYTES * r;
        uint32_t share;

        /* An all-idle picture spreads the budget evenly: one unit per row. */
        if (total == 0u)
            share = 1u;
        else
            share = (FC_MBRC_SHARE_UNITS
                     * fc_mbrc_get16(buf + FC_MBRC_ACTIVITY_OFFSET + 2u * r))
                    / total;
        cum += share;

        /* cum <= 2048, so every limit fits 16 bits (max 2048 * 51 / 32). */
        for (k = 0; k < FC_MBRC_RECORD_WORDS; k++)
            fc_mbrc_put16(rec + 2u * k, (cum * weight32[k]) / 32u);
    }

    if (sync != 0)
        sync(opaque, buf, (int)fc_mbrc_buffer_bytes(rows));
    return 0;
}

/* ------------------------------------------------ auxiliary reference plane */

/* Engines with an IC version above this keep a compressed auxiliary plane
 * beside every reference picture (spec r2/03 section 1). */
#define FC_AUX_PLANE_MIN_IC       0x1667u

/* 48-MB column groups of a picture; also the stride-group field of register
 * 0x08 (bits 10-13). */
static inline unsigned int fc_aux_plane_groups(unsigned int width_mb)
{
    return (width_mb + 47u) / 48u;
}

/* Auxiliary plane bytes (spec r2/03 s2): N = H16 / 8 + 9 rows of (32 B per
 * column group + 2 B per MB column, padded to 32). */
static inline unsigned int fc_aux_plane_bytes(unsigned int width_px,
                                              unsigned int height_px)
{
    unsigned int width_mb = (width_px + 15u) / 16u;
    unsigned int h16      = (height_px + 15u) & ~15u;
    unsigned int cols     = (width_mb + 31u) & ~31u;
    unsigned int rows     = (h16 + 72u) / 8u;

    return rows * (32u * fc_aux_plane_groups(width_mb) + 2u * cols);
}

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_H264_BUFS_H */
