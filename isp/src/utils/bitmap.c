// SPDX-License-Identifier: AGPL-3.0-only
/* Copyright (C) 2026 freewinner contributors */
#include "utils/bitmap.h"

/* Numeric pixel-format values handled here; enumerator names are
 * deliberately not spelled. */
#define BMP_FMT_RGB1555 8u
#define BMP_FMT_RGB8888 10u
#define BMP_BYTES_RGB1555 2u
#define BMP_BYTES_RGB8888 4u

__attribute__((weak)) void media_utils_log_error(const char *reason,
						 unsigned int detail)
{
	(void)reason;
	(void)detail;
}

int BITMAP_S_GetdataSize(const BITMAP_S *pBitmap)
{
	unsigned int fmt, bpp, size;

	if (pBitmap == NULL) {
		media_utils_log_error("BITMAP_S_GetdataSize: NULL bitmap", 0u);
		return 0;
	}

	fmt = (unsigned int)pBitmap->mPixelFormat;
	if (fmt == BMP_FMT_RGB1555) {
		bpp = BMP_BYTES_RGB1555;
	} else if (fmt == BMP_FMT_RGB8888) {
		bpp = BMP_BYTES_RGB8888;
	} else {
		media_utils_log_error("BITMAP_S_GetdataSize: unsupported format",
				      fmt);
		return 0;
	}

	/* Unsigned 32-bit, left to right, wrap allowed. */
	size = pBitmap->mWidth * pBitmap->mHeight;
	size = size * bpp;
	return (int)size;
}
