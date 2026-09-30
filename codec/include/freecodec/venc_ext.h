/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* freecodec extensions to the VideoEncSetParameter index set. Values sit far
 * outside the fwm_venc_param_e range; an encoder that does not know one
 * returns "not supported". */
#ifndef FREECODEC_VENC_EXT_H
#define FREECODEC_VENC_EXT_H

/* fwm_venc_display_size_t *: displayed window centred in the coded picture via
 * SPS cropping (H.264 7.4.2.1.1); even, <= coded size; 0x0 = whole picture. */
#define FWM_VENC_PARAM_DISPLAY_SIZE 0x7f000001

typedef struct fwm_venc_display_size {
    int width;
    int height;
} fwm_venc_display_size_t;

/* fwm_venc_display_offset_t *: window position in pixels (rounded to even);
 * negative keeps that axis centred; ignored if it would leave the picture. */
#define FWM_VENC_PARAM_DISPLAY_OFFSET 0x7f000002

typedef struct fwm_venc_display_offset {
    int left;
    int top;
} fwm_venc_display_offset_t;

/* int *: 3D-filter threshold T written directly (0..511, clamped); runs the
 * filter as level 3 at every QP, 0 = level rules. Read per picture. */
#define FWM_VENC_PARAM_FILTER_3D_STRENGTH 0x7f000003

/* int *: H.265 rate control target-tracking (1) or the vendor open-loop model
 * (0, default). Set before init; see freecodec_h265_rc_set_tracking(). */
#define FWM_VENC_PARAM_H265_RC_TRACK 0x7f000004

#endif
