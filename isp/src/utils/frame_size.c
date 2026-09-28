// SPDX-License-Identifier: AGPL-3.0-only
/* Copyright (C) 2026 freewinner contributors */
#include "utils/frame_size.h"

/* Numeric pixel-format values; enumerators are not spelled. */
enum {
	FS_YUV420_3PLANE_A = 20,
	FS_YUV420_2PLANE_A = 23,
	FS_YUV420_3PLANE_B = 30,
	FS_YUV420_2PLANE_B = 32,
	FS_COMPRESSED_FIRST = 33,
	FS_COMPRESSED_LAST = 36,
	FS_RAW8_FIRST = 37,
	FS_RAW8_LAST = 40,
	FS_RAW10_FIRST = 41,
	FS_RAW10_LAST = 44,
	FS_RAW12_FIRST = 45,
	FS_RAW12_LAST = 48
};

#define FS_BITS_PER_BYTE 8u
#define FS_RAW10_BITS 10u
#define FS_RAW12_BITS 12u
#define FS_2PLANE_CHROMA_DIV 2u
#define FS_3PLANE_CHROMA_DIV 4u

__attribute__((weak)) void media_utils_log_error(const char *reason,
						 unsigned int detail)
{
	(void)reason;
	(void)detail;
}

ERRORTYPE getVideoFrameBufferSizeInfo(VIDEO_FRAME_INFO_S *pFrame,
				      VideoFrameBufferSizeInfo *pSizeInfo)
{
	const VIDEO_FRAME_S *vf;
	unsigned int fmt, area, y, u, v;

	if (pFrame == NULL || pSizeInfo == NULL) {
		media_utils_log_error("getVideoFrameBufferSizeInfo: NULL argument",
				      0u);
		return FAILURE;
	}

	vf = &pFrame->VFrame;
	fmt = (unsigned int)vf->mPixelFormat;
	/* All arithmetic unsigned 32-bit, wrap before divide. */
	area = vf->mWidth * vf->mHeight;

	if (fmt == FS_YUV420_2PLANE_A || fmt == FS_YUV420_2PLANE_B) {
		y = area;
		u = area / FS_2PLANE_CHROMA_DIV;
		v = 0u;
	} else if (fmt == FS_YUV420_3PLANE_A || fmt == FS_YUV420_3PLANE_B) {
		y = area;
		u = area / FS_3PLANE_CHROMA_DIV;
		v = u;
	} else if (fmt >= FS_COMPRESSED_FIRST && fmt <= FS_COMPRESSED_LAST) {
		y = vf->mStride[0];
		u = vf->mStride[1];
		v = vf->mStride[2];
	} else if (fmt >= FS_RAW8_FIRST && fmt <= FS_RAW8_LAST) {
		y = area;
		u = v = 0u;
	} else if (fmt >= FS_RAW10_FIRST && fmt <= FS_RAW10_LAST) {
		y = (area * FS_RAW10_BITS) / FS_BITS_PER_BYTE;
		u = v = 0u;
	} else if (fmt >= FS_RAW12_FIRST && fmt <= FS_RAW12_LAST) {
		y = (area * FS_RAW12_BITS) / FS_BITS_PER_BYTE;
		u = v = 0u;
	} else {
		/* Output left untouched on rejection. */
		media_utils_log_error("getVideoFrameBufferSizeInfo: unsupported format",
				      fmt);
		return FAILURE;
	}

	pSizeInfo->mYSize = (int)y;
	pSizeInfo->mUSize = (int)u;
	pSizeInfo->mVSize = (int)v;
	return SUCCESS;
}
