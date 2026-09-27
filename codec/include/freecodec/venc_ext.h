/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* freecodec extensions to the VideoEncSetParameter index set. Values sit far
 * outside the fwm_venc_param_e range; an encoder that does not know one
 * returns "not supported". */
#ifndef FREECODEC_VENC_EXT_H
#define FREECODEC_VENC_EXT_H

/* fwm_venc_display_size_t *: the displayed window, centred inside the encoded
 * picture (sensor modes carry margin columns/rows on every edge). Signalled
 * through the SPS frame-cropping fields (H.264 7.4.2.1.1), so the encoder
 * still codes the full picture. Must be <= the encoded size with even width
 * and height; 0x0 (default) shows the whole picture. */
#define FWM_VENC_PARAM_DISPLAY_SIZE 0x7f000001

typedef struct fwm_venc_display_size {
    int width;
    int height;
} fwm_venc_display_size_t;

/* fwm_venc_display_offset_t *: position of the displayed window inside the
 * encoded picture, in pixels (rounded down to even: 4:2:0 crop units). A
 * negative value keeps that axis centred; an offset that would push the window
 * past the picture edge is ignored. Unset (default): centred on both axes. */
#define FWM_VENC_PARAM_DISPLAY_OFFSET 0x7f000002

typedef struct fwm_venc_display_offset {
    int left;
    int top;
} fwm_venc_display_offset_t;

/* int *: encoder 3D (temporal) filter strength, written directly as the 9-bit
 * threshold field T of the filter's threshold register, 0..511 (values above
 * 511 are clamped). 0 (default) = off: the FWM_VENC_PARAM_FILTER_3D level
 * rules apply unchanged. Non-zero enables the filter as level 3 does (enable,
 * scratch planes and fill) but with T = strength at every QP instead of the
 * level's small QP-banded value (1..12), which is barely visible. A freecodec
 * extension: the hardware field is 9 bits, the vendor levels use only 1..12.
 * Read afresh per picture, so a live change applies to the next picture. */
#define FWM_VENC_PARAM_FILTER_3D_STRENGTH 0x7f000003

#endif
