/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * bitmap.c - clean-room BITMAP_S_GetdataSize.
 *
 * Behaviour source: spec/media_utils/bitmap.md.  The function
 * returns the payload byte count selected by the bitmap's fwm_pixel_format_e.
 * The product is formed in 32-bit unsigned int and returned as a signed int,
 * so a large image wraps exactly as on the deployed target.
 */
#include "utils/bitmap.h"

#include <stdio.h>

/*
 * Default diagnostic sink.  Declared weak in the shared ABI so a host test or
 * the integrating daemon may provide its own definition; the message text is
 * outside the behavioural contract (bitmap spec section 2).
 */
__attribute__((weak)) void media_utils_log_error(const char *reason,
						 unsigned int detail)
{
	(void)reason;
	(void)detail;
	fprintf(stderr, "media_utils: %s (0x%x)\n",
		reason ? reason : "bitmap rejected", detail);
}

int BITMAP_S_GetdataSize(const BITMAP_S *pBitmap)
{
	unsigned int size;

	if (pBitmap == NULL)
		return 0;

	switch (pBitmap->mPixelFormat) {
	case FWM_MM_PIXEL_FORMAT_RGB_1555:
		size = pBitmap->mWidth * pBitmap->mHeight * 2u;
		break;
	case FWM_MM_PIXEL_FORMAT_RGB_8888:
		size = pBitmap->mWidth * pBitmap->mHeight * 4u;
		break;
	default:
		media_utils_log_error("bitmap unsupported format",
				      (unsigned int)pBitmap->mPixelFormat);
		return 0;
	}

	return (int)size;
}
