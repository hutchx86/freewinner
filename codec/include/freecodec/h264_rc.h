/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Frame-level H.264 rate control (spec 10-rate-control.md, r4): a pure
 * computation unit, no device access and no I/O. Before a picture is coded it
 * is asked for a slice QP; after the picture is coded it is told how many
 * bits it cost and how much picture content changed; it can report the bit
 * budget assigned to the picture in progress at any point after the QP
 * request. Call order (spec 10 section 2): configure, then start_sequence,
 * then any number of (start_picture -> target -> finish_picture) triples. */

#ifndef FREECODEC_H264_RC_H
#define FREECODEC_H264_RC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum freecodec_rc_pic_type {
    FC_RC_PIC_I = 0,
    FC_RC_PIC_P = 1
} freecodec_rc_pic_type;

/* Opaque to callers; laid out here only so the unit needs no heap allocation.
 * Every field is reset by freecodec_rc_start_sequence() except the
 * configuration block, which only freecodec_rc_configure() touches. */
typedef struct freecodec_rc {
    /* ---- configuration (freecodec_rc_configure) ---- */
    double       bit_rate;
    double       frame_rate;
    unsigned int width_px, height_px;
    unsigned int width_mb, height_mb;
    int          qp_min, qp_max;
    int          max_step;
    unsigned int key_interval;   /* accepted, not consulted (spec 10 section 2) */
    double       b0;             /* bit_rate / frame_rate */

    /* ---- per-sequence state (freecodec_rc_start_sequence) ---- */
    long         pic_index;      /* pictures since start_sequence, 0-based */
    int          prev_qp_valid;
    int          prev_qp;
    int          cur_qp;
    int          cur_is_i;
    double       cur_target;     /* target() value for the picture in progress */
    double       accum;          /* cumulative (actual - ideal) bits, P only */
    long         p_count;        /* P pictures finished since start_sequence */
    double       last_activity;  /* most recently finished picture's activity */
    int          have_k;
    double       k_est;          /* EWMA bits*qstep/activity estimator, P only */
} freecodec_rc;

/* Configure the unit and arm it (equivalent to configure + start_sequence).
 * width/height are coded picture pixels, multiples of 16. */
void freecodec_rc_configure(freecodec_rc *rc, double bit_rate,
                            unsigned int frame_rate, unsigned int width,
                            unsigned int height, int qp_min, int qp_max,
                            int max_step, unsigned int key_interval);

/* Re-arm the unit to exactly its post-configure state: no picture count, bit
 * history or budget carry-over survives (spec 10 section 2). */
void freecodec_rc_start_sequence(freecodec_rc *rc);

/* Pick the slice QP for the next picture. Returns an integer in
 * [qp_min, qp_max] (and never above 51, spec 10 section 3 item 3). */
int freecodec_rc_start_picture(freecodec_rc *rc, freecodec_rc_pic_type type);

/* The bit budget assigned to the picture in progress. 0 for an I picture
 * (spec 10 section 3 item 8). Valid any time after start_picture. */
long long freecodec_rc_target(const freecodec_rc *rc);

/* Tell the unit how the picture in progress actually came out: bits produced
 * (whole coded-slice payload, in bits) and picture activity A (spec 10
 * section 5 -- the sum of per-macroblock mean absolute differences from
 * encoder register 0x50, or 0 if that path is unavailable/disabled). A zero
 * activity is replaced internally by the neutral fallback
 * width_mb * height_mb * 8. */
void freecodec_rc_finish_picture(freecodec_rc *rc, long long bits,
                                 unsigned int activity);

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_H264_RC_H */
