/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Frame-level H.264 rate control (spec 10). Our own single-term model
 * bits = K * A / Qstep(QP): EWMA estimate of K plus a bounded accumulator that
 * trims P targets toward bit_rate/frame_rate (spec 3 item 8). Qstep is the pure
 * doubling 2^((QP-30)/6), internal only (spec 4 leaves its shape free). */

#include "freecodec/h264_rc.h"

#include <math.h>
#include <string.h>

/* Pictures over which accumulated bit surplus/deficit is repaid (spec 10 3
 * item 7 leaves the transient free). [DES] */
#define RC_REPAY_PICTURES   16.0

/* Accumulator bound in ideal-picture budgets, so an unreachable target (spec 3
 * item 6) cannot leave unbounded debt. [DES] */
#define RC_ACCUM_CAP_PICS   40.0

/* EWMA gain for the K estimator, and the ratio window outside which a sample
 * is rejected as an outlier (spec 10 section 4). [DES] */
#define RC_K_ALPHA          0.35
#define RC_K_OUTLIER_LOW    0.2
#define RC_K_OUTLIER_HIGH   5.0

/* Clamp on the target budget so accumulated debt cannot drive it to zero or
 * an absurd multiple of the ideal rate. [DES] */
#define RC_TARGET_MIN_FRAC  0.15
#define RC_TARGET_MAX_FRAC  4.0

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/* Section 3 item 1's ten-step bpp ladder. */
static int first_qp_ladder(double bpp)
{
    if (bpp < 0.01) return 40;
    if (bpp < 0.03) return 39;
    if (bpp < 0.05) return 37;
    if (bpp < 0.1)  return 35;
    if (bpp < 0.2)  return 33;
    if (bpp < 0.4)  return 32;
    if (bpp < 0.6)  return 29;
    if (bpp < 1.4)  return 27;
    if (bpp < 2.4)  return 25;
    return 15;
}

static double qp_to_qstep(int qp)
{
    return pow(2.0, ((double)qp - 30.0) / 6.0);
}

static double compute_target_bits(const freecodec_rc *rc)
{
    double t = rc->b0;

    if (rc->p_count > 0)
        t -= rc->accum / RC_REPAY_PICTURES;

    if (t < rc->b0 * RC_TARGET_MIN_FRAC)
        t = rc->b0 * RC_TARGET_MIN_FRAC;
    if (t > rc->b0 * RC_TARGET_MAX_FRAC)
        t = rc->b0 * RC_TARGET_MAX_FRAC;
    return t;
}

static int qp_from_target(const freecodec_rc *rc, double target_bits)
{
    double a_est = rc->last_activity;
    double k = rc->have_k ? rc->k_est : 1.0;
    double qstep_desired, qp_real;

    if (a_est <= 0.0)
        a_est = (double)rc->width_mb * (double)rc->height_mb * 8.0;
    if (target_bits <= 0.0)
        target_bits = 1.0;
    if (k <= 0.0)
        k = 1.0;

    qstep_desired = (k * a_est) / target_bits;
    if (qstep_desired < 1e-9)
        qstep_desired = 1e-9;

    qp_real = 30.0 + 6.0 * (log(qstep_desired) / log(2.0));
    return (int)floor(qp_real + 0.5);
}

void freecodec_rc_configure(freecodec_rc *rc, double bit_rate,
                            unsigned int frame_rate, unsigned int width,
                            unsigned int height, int qp_min, int qp_max,
                            int max_step, unsigned int key_interval)
{
    memset(rc, 0, sizeof(*rc));

    rc->bit_rate    = bit_rate;
    rc->frame_rate  = (double)frame_rate;
    rc->width_px    = width;
    rc->height_px   = height;
    rc->width_mb    = width / 16u;
    rc->height_mb   = height / 16u;
    rc->key_interval = key_interval;
    rc->max_step    = max_step;

    /* Bounds (section 3 item 3): clamp into a sane range and never above the
     * coded slice QP ceiling of 51 (STD 7.4.3), regardless of configuration. */
    if (qp_max > 51)
        qp_max = 51;
    if (qp_min < 0)
        qp_min = 0;
    if (qp_min > qp_max)
        qp_min = qp_max;
    rc->qp_min = qp_min;
    rc->qp_max = qp_max;

    rc->b0 = (rc->frame_rate > 0.0) ? bit_rate / rc->frame_rate : bit_rate;

    freecodec_rc_start_sequence(rc);
}

void freecodec_rc_start_sequence(freecodec_rc *rc)
{
    rc->pic_index      = 0;
    rc->prev_qp_valid  = 0;
    rc->prev_qp        = 0;
    rc->cur_qp         = 0;
    rc->cur_is_i       = 0;
    rc->cur_target     = 0.0;
    rc->accum          = 0.0;
    rc->p_count        = 0;
    rc->last_activity  = 0.0;
    rc->have_k         = 0;
    rc->k_est          = 0.0;
}

int freecodec_rc_start_picture(freecodec_rc *rc, freecodec_rc_pic_type type)
{
    long idx = rc->pic_index++;
    int qp;

    rc->cur_is_i = (type == FC_RC_PIC_I);

    if (idx == 0) {
        /* Section 3 item 1: the very first picture takes its QP from the bpp
         * ladder, independent of picture type. */
        double denom = (double)rc->width_px * (double)rc->height_px * rc->frame_rate;
        double bpp = (denom > 0.0) ? rc->bit_rate / denom : 0.0;

        qp = first_qp_ladder(bpp);
        qp = clamp_int(qp, rc->qp_min, rc->qp_max);
        rc->cur_target = 0.0;
    } else if (idx == 1) {
        /* Section 3 item 2: first-picture QP + 1, exactly, clamped into
         * bounds. No step-limit applies to this fixed rule. */
        qp = clamp_int(rc->prev_qp + 1, rc->qp_min, rc->qp_max);
        rc->cur_target = rc->b0;
    } else {
        double t = compute_target_bits(rc);

        rc->cur_target = t;
        qp = qp_from_target(rc, t);
        qp = clamp_int(qp, rc->qp_min, rc->qp_max);

        if (rc->prev_qp_valid) {
            /* Section 3 item 4: step limit between consecutive pictures. */
            int lo = rc->prev_qp - rc->max_step;
            int hi = rc->prev_qp + rc->max_step;

            if (qp < lo)
                qp = lo;
            if (qp > hi)
                qp = hi;
            qp = clamp_int(qp, rc->qp_min, rc->qp_max);
        }
    }

    qp = clamp_int(qp, 0, 51);

    rc->cur_qp        = qp;
    rc->prev_qp       = qp;
    rc->prev_qp_valid = 1;
    return qp;
}

long long freecodec_rc_target(const freecodec_rc *rc)
{
    long long v;

    if (rc->cur_is_i)
        return 0;

    v = (long long)(rc->cur_target + 0.5);
    return (v < 0) ? 0 : v;
}

void freecodec_rc_finish_picture(freecodec_rc *rc, long long bits,
                                 unsigned int activity)
{
    double qstep = qp_to_qstep(rc->cur_qp);
    double a = (double)activity;

    /* Section 5: neutral stand-in when the activity reads 0 (no figure yet, or
     * statistics disabled). */
    if (a <= 0.0)
        a = (double)rc->width_mb * (double)rc->height_mb * 8.0;

    rc->last_activity = a;

    if (!rc->cur_is_i) {
        double k_sample = ((double)bits * qstep) / a;

        if (!rc->have_k) {
            rc->k_est = k_sample;
            rc->have_k = 1;
        } else if (rc->k_est > 0.0) {
            double ratio = k_sample / rc->k_est;

            if (ratio > RC_K_OUTLIER_LOW && ratio < RC_K_OUTLIER_HIGH)
                rc->k_est = rc->k_est * (1.0 - RC_K_ALPHA) + k_sample * RC_K_ALPHA;
        }

        rc->accum += (double)bits - rc->b0;
        if (rc->accum > RC_ACCUM_CAP_PICS * rc->b0)
            rc->accum = RC_ACCUM_CAP_PICS * rc->b0;
        if (rc->accum < -RC_ACCUM_CAP_PICS * rc->b0)
            rc->accum = -RC_ACCUM_CAP_PICS * rc->b0;

        rc->p_count++;
    }
}
