/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* freecodec extensions to the VideoEncSetParameter index set. Values sit far
 * outside the vendor VENC_INDEXTYPE range; an encoder that does not know one
 * returns "not supported". */
#ifndef FREECODEC_VENC_EXT_H
#define FREECODEC_VENC_EXT_H

/* FreecodecDisplaySize *: the displayed window, centred inside the encoded
 * picture (sensor modes carry margin columns/rows on every edge). Signalled
 * through the SPS frame-cropping fields (H.264 7.4.2.1.1), so the encoder
 * still codes the full picture. Must be <= the encoded size with even width
 * and height; 0x0 (default) shows the whole picture. */
#define FREECODEC_IndexParamDisplaySize 0x7f000001

typedef struct FreecodecDisplaySize {
    int nWidth;
    int nHeight;
} FreecodecDisplaySize;

#endif
