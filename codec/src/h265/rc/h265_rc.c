/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.265 rate-control model (spec h265/10-rate-control.md sections 2-3). */

#include "freecodec/h265_rc.h"

#include <math.h>
#include <string.h>

/* GOP size over which the model accounts; also the window edge (10 section 2). */
#define H265_RC_WINDOW 80
/* Floor on a GOP budget (10 section 2 item 3). */
#define H265_RC_GOP_FLOOR 0x100

unsigned int freecodec_h265_ratio_i(double bpp, int force_two)
{
    unsigned int r;

    if (bpp <= 0.001)
        r = 25u;
    else if (bpp <= 0.01)
        r = 15u;
    else if (bpp <= 0.05)
        r = 7u;
    else if (bpp <= 0.1)
        r = 5u;
    else if (bpp <= 0.2)
        r = 4u;
    else
        r = 3u;

    if (force_two && r > 2u)
        r = 2u;
    return r + 2u;   /* the +2 of 10 section 2 item 5 */
}

unsigned int freecodec_h265_ratio_p(unsigned int gop_position)
{
    return ((gop_position % 5u) == 4u) ? 4u : 1u;
}

int freecodec_h265_qp_from_lambda(double lambda)
{
    if (lambda <= 0.0)
        return 3;   /* 10 section 3 clamps into [3, 51] */
    return (int)floor(13.7122 + 4.2005 * log(lambda) + 0.5);
}

double freecodec_h265_lambda_from_bits(double bits, double wh)
{
    if (bits <= 0.0 || wh <= 0.0)
        return 0.0;
    return FC_H265_RC_ALPHA * pow(bits / wh, FC_H265_RC_BETA);
}

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void freecodec_h265_rc_configure(freecodec_h265_rc *rc, double bit_rate,
                                 unsigned int frame_rate, unsigned int width,
                                 unsigned int height, int qp_min, int qp_max,
                                 unsigned int gop_size, unsigned int idr_period)
{
    memset(rc, 0, sizeof(*rc));
    rc->bit_rate   = bit_rate;
    rc->frame_rate = frame_rate ? frame_rate : 1u;
    rc->width      = width;
    rc->height     = height;
    rc->gop_size   = gop_size ? gop_size : 1u;
    rc->idr_period = idr_period ? idr_period : 1u;
    if (qp_max > 51)
        qp_max = 51;
    if (qp_min < 0)
        qp_min = 0;
    if (qp_min > qp_max)
        qp_min = qp_max;
    rc->qp_min = qp_min;
    rc->qp_max = qp_max;
    rc->avr_frm_size = rc->bit_rate / (double)rc->frame_rate;
}

void freecodec_h265_rc_set_tracking(freecodec_h265_rc *rc, int on)
{
    rc->track = on ? 1 : 0;
    rc->trk_surplus = 0.0;
}

void freecodec_h265_rc_start_picture(freecodec_h265_rc *rc, long frame_index,
                                     int is_i)
{
    double remaining, gop_budget, bpp, ratio_frm;
    unsigned int ratio_i, ratio_p;
    int qp;

    rc->cur_is_i = is_i;
    rc->gop_index = frame_index % (long)rc->idr_period;

    if (is_i) {
        rc->frame_total = 0;              /* the RC state resets on an intra */
        rc->bits_accu_gops = 0.0;
        rc->bits_accu_gop = 0.0;
        remaining = (double)rc->idr_period;
        rc->bits_est_gop = rc->avr_frm_size * remaining;
        bpp = rc->bits_est_gop
              / ((double)rc->gop_size * (double)rc->width * (double)rc->height);
        ratio_i = freecodec_h265_ratio_i(bpp,
                    (rc->idr_period % rc->gop_size) != 0u);
        rc->cur_budget = (long long)(ratio_i * rc->avr_frm_size);
        if (rc->cur_budget < H265_RC_GOP_FLOOR)
            rc->cur_budget = H265_RC_GOP_FLOOR;
        /* I QP is kept per IDR; seed it from the first IDR at the QP cap so the
         * anchor is stable, then leave it (10 section 3 I paragraph). */
        if (!rc->have_i_qp) {
            rc->i_qp = clamp_int(rc->qp_max, 10, 40);
            rc->have_i_qp = 1;
        }
        qp = clamp_int(rc->i_qp, rc->qp_min, rc->qp_max);
    } else if (rc->track) {
        /* Integral steering on the running surplus (target - actual), in units
         * of one target frame. Dead zone so it does not chase noise; capped step
         * so the picture does not pump. */
        double tgt = rc->bit_rate / (double)rc->frame_rate;
        double err = tgt > 0.0 ? rc->trk_surplus / tgt : 0.0;
        int q = rc->prev_p_valid ? rc->prev_p_qp : rc->i_qp + 8;

        if (err > 6.0)       q -= 2;
        else if (err > 1.5)  q -= 1;
        else if (err < -6.0) q += 2;
        else if (err < -1.5) q += 1;
        qp = clamp_int(q, rc->qp_min, rc->qp_max);
        rc->prev_p_qp = qp;
        rc->prev_p_valid = 1;
        rc->cur_budget = (long long)tgt;
    } else {
        remaining = (double)rc->idr_period - (double)rc->gop_index;
        if (remaining < 1.0)
            remaining = 1.0;
        gop_budget = (rc->avr_frm_size * (double)rc->frame_total - rc->bits_accu_gops)
                     * ((double)rc->gop_size < remaining ? (double)rc->gop_size : remaining)
                     / remaining;
        if (gop_budget < (double)H265_RC_GOP_FLOOR)
            gop_budget = H265_RC_GOP_FLOOR;

        ratio_i = freecodec_h265_ratio_i(0.0, 0);
        ratio_p = freecodec_h265_ratio_p((unsigned int)(rc->gop_index >= 0 ? rc->gop_index : 0));
        ratio_frm = (double)ratio_p;
        (void)ratio_i;

        {
            /* Measured calibration (spec 10 §3.1): the P budget tracks
             * avr_frm_size at L ramping 0.30 -> 0.54 -> 0.72 -> 0.96 with the
             * ratio_p=4 spike. 0.72 is the steady level; the exact adaptation
             * of L is an open item, so use the steady value. */
            double est = rc->avr_frm_size * 0.72 * ratio_frm;

            if (rc->frame_total > 16 && rc->have_est)
                est = (9.0 * rc->bits_est_frm + est) / 10.0;
            rc->bits_est_frm = est;
            rc->have_est = 1;
            rc->cur_budget = (long long)est;
        }
        if (rc->cur_budget < H265_RC_GOP_FLOOR)
            rc->cur_budget = H265_RC_GOP_FLOOR;

        {
            double lambda = freecodec_h265_lambda_from_bits(
                                (double)rc->cur_budget,
                                (double)rc->width * (double)rc->height);

            qp = freecodec_h265_qp_from_lambda(lambda);
            qp = clamp_int(qp, 3, 51);
            if (rc->prev_p_valid) {
                /* Clamp within +/-5 of the previous P QP (10 section 3). */
                qp = clamp_int(qp, rc->prev_p_qp - 5, rc->prev_p_qp + 5);
            }
            qp = clamp_int(qp, rc->qp_min, rc->qp_max);
            rc->prev_p_qp = qp;
            rc->prev_p_valid = 1;
        }
    }

    rc->cur_qp = qp;
}

void freecodec_h265_rc_finish_picture(freecodec_h265_rc *rc, long long bits)
{
    double b = (double)bits;

    if (rc->track && rc->frame_rate) {
        double tgt = rc->bit_rate / (double)rc->frame_rate;
        rc->trk_surplus += tgt - b;
        if (rc->trk_surplus > 40.0 * tgt)  rc->trk_surplus = 40.0 * tgt;   /* anti-windup, 2 s */
        if (rc->trk_surplus < -40.0 * tgt) rc->trk_surplus = -40.0 * tgt;
    }
    rc->frame_total++;
    rc->bits_accu_gop += b;
    if (rc->frame_total % H265_RC_WINDOW == 0) {
        rc->bits_accu_gops = 0.0;
        rc->bits_accu_gop = 0.0;
    }
    if ((rc->gop_index + 1) % (long)rc->gop_size == 0) {
        rc->bits_accu_gops += rc->bits_accu_gop;
        rc->bits_accu_gop = 0.0;
    }
    /* Running mean frame size drives the next GOP estimate (10 section 2). */
    if (!rc->track)
        rc->avr_frm_size = rc->avr_frm_size * 0.9 + b * 0.1;
}
