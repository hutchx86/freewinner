/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.265 rate control (spec h265/10-rate-control.md): the GOP/per-frame model of
 * sections 2 and 3 and the model λ constants. Pure computation, no I/O. The
 * exact per-picture bit budgets the vendor probe realised depend on its internal
 * running state (10 sections 4/7); this unit implements the documented model,
 * and the tests pin the parts the spec states exactly (the ratio tables, the
 * every-fifth-GOP ratio_p rule, the λ/pow constants and the QP estimator). */

#ifndef FREECODEC_H265_RC_H
#define FREECODEC_H265_RC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed λ words written to +0x68 (10 section 3): 64 x 1576 and 64 x 3152. */
#define FC_H265_LAMBDA_I 100864u   /* 0x00018a00 */
#define FC_H265_LAMBDA_P 201728u   /* 0x00031400 */

/* Model coefficients (10 section 2 item 8). */
#define FC_H265_RC_ALPHA 3.2003
#define FC_H265_RC_BETA  (-1.367)

typedef struct freecodec_h265_rc {
    /* configuration */
    double       bit_rate;
    unsigned int frame_rate;
    unsigned int width, height;
    int          qp_min, qp_max;
    unsigned int gop_size;
    unsigned int idr_period;

    /* per-sequence state */
    long         frame_total;      /* pictures since configuration          */
    long         gop_index;        /* position inside the GOP               */
    double       avr_frm_size;     /* running mean frame bits               */
    double       bits_est_gop;     /* current GOP estimate                  */
    double       bits_est_frm;     /* last P estimate, for the blend        */
    double       bits_accu_gops;   /* bits spent over finished GOPs         */
    double       bits_accu_gop;    /* bits spent in the current GOP         */
    int          have_est;
    int          prev_p_qp;
    int          prev_p_valid;
    int          have_i_qp;
    int          i_qp;

    /* picture in progress */
    int          cur_is_i;
    int          cur_qp;
    long long    cur_budget;
} freecodec_h265_rc;

/* Arm the unit (zeroes all state but the configuration). */
void freecodec_h265_rc_configure(freecodec_h265_rc *rc, double bit_rate,
                                 unsigned int frame_rate, unsigned int width,
                                 unsigned int height, int qp_min, int qp_max,
                                 unsigned int gop_size, unsigned int idr_period);

/* Start a picture: picks cur_qp and cur_budget. `frame_index` is the picture's
 * absolute index; `is_i` selects the intra branch. */
void freecodec_h265_rc_start_picture(freecodec_h265_rc *rc, long frame_index,
                                     int is_i);

/* Report the coded picture size and close the picture. */
void freecodec_h265_rc_finish_picture(freecodec_h265_rc *rc, long long bits);

/* ----------------------------------------------------------- pure helpers */

/* I/P ratio from bpp (10 section 2 item 5). `force_two` pre-clamps to 2 before
 * the +2 (intra period not a multiple of the GOP size). */
unsigned int freecodec_h265_ratio_i(double bpp, int force_two);

/* 4 every fifth GOP position, else 1 (10 section 2 item 6). */
unsigned int freecodec_h265_ratio_p(unsigned int gop_position);

/* qp_est = round(13.7122 + 4.2005 * ln(lambda)) (10 section 3). */
int freecodec_h265_qp_from_lambda(double lambda);

/* Model lambda = alpha * (bits / (W * H)) ^ beta (10 section 2 item 8). */
double freecodec_h265_lambda_from_bits(double bits, double wh);

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_H265_RC_H */
