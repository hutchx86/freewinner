/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.264 GOP picture-ring normal-P path (spec/h264-gop.md §1, the mediad default),
 * golden-verified. State machines on is_busy[0], alternating last/current slot. */

#include <string.h>

#include "freecodec/h264_gop.h"

/* IC versions whose sub-ring must not have is_busy[curr] written. */
#define FREECODEC_H264_IC_VERSION_A 0x1639u
#define FREECODEC_H264_IC_VERSION_B 0x1650u

/* Advance one ring from is_busy[0]: the slot holding d (previous rc) becomes
 * the last reference, the other the current (spec h264-gop.md). */
static void ring_step(freecodec_h264_frame_ring *r, unsigned int d)
{
    r->last_frm_idx = (r->is_busy[0] != d) ? 1u : 0u;
    r->curr_frm_idx = (r->is_busy[0] == d) ? 1u : 0u;
}

void freecodec_h264_gop_normal_p(freecodec_h264_frame_ring *main_ring,
                                 freecodec_h264_frame_ring *sub_ring,
                                 freecodec_h264_gop_cfg *cfg)
{
    unsigned int rc;
    unsigned int d;

    rc = (cfg->frame_count - cfg->insert_count) & 0xffu;

    if (cfg->frame_count == 0u) {
        /* Init: reset both rings, then (virtual_period) park 2/2. */
        memset(main_ring, 0, sizeof(*main_ring));
        memset(sub_ring, 0, sizeof(*sub_ring));

        if (cfg->virtual_period != 0u) {
            main_ring->last_frm_idx      = 2u;
            main_ring->curr_frm_idx      = 2u;
            sub_ring->last_frm_idx  = 2u;
            sub_ring->curr_frm_idx  = 2u;
        }
    } else {
        /* Default path. Skip-frame and double/smart/advanced variants (spec
         * §2/§3) fall back to this single-reference ring - a documented deviation. */
        d = rc - 1u;
        ring_step(main_ring, d);
        ring_step(sub_ring, d);
    }

    /* Tail: publish the slot holding rc. Skip the sub-ring store on the two
     * IC versions that do their own bookkeeping; keyframe interval resets. */
    main_ring->is_busy[main_ring->curr_frm_idx] = rc;
    if (cfg->ic_version != FREECODEC_H264_IC_VERSION_A &&
        cfg->ic_version != FREECODEC_H264_IC_VERSION_B)
        sub_ring->is_busy[sub_ring->curr_frm_idx] = rc;

    if (cfg->max_key_itl != 0u && cfg->frame_count >= cfg->max_key_itl) {
        cfg->frame_count  = 0u;
        cfg->insert_count = 0u;
    }
}
