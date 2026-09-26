/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * frame_size.c - clean-room getVideoFrameBufferSizeInfo.
 *
 * Behaviour source: spec/media_utils/frame_size.md.  The function
 * reports the Y/U/V plane byte counts selected by the frame's fwm_pixel_format_e.
 * All products are computed in 32-bit unsigned int and stored into signed int
 * fields, matching the deployed target.
 */
#include "utils/frame_size.h"

#include <stdio.h>

/*
 * Default diagnostic sink.  Declared weak in the shared ABI, so a host test or
 * the integrating daemon may provide its own definition; the message text is
 * outside the behavioural contract.
 */
__attribute__((weak)) void media_utils_log_error(const char *reason,
						 unsigned int detail)
{
	(void)reason;
	(void)detail;
	fprintf(stderr, "media_utils: %s (0x%x)\n",
		reason ? reason : "frame_size rejected", detail);
}

ERRORTYPE getVideoFrameBufferSizeInfo(VIDEO_FRAME_INFO_S *pFrame,
				      VideoFrameBufferSizeInfo *pSizeInfo)
{
	unsigned int width;
	unsigned int height;
	unsigned int ySize;
	unsigned int uSize;
	unsigned int vSize;

	if (pFrame == NULL || pSizeInfo == NULL) {
		media_utils_log_error("frame_size null argument", 0);
		return FAILURE;
	}

	width = pFrame->VFrame.mWidth;
	height = pFrame->VFrame.mHeight;

	switch (pFrame->VFrame.mPixelFormat) {
	case FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420:
	case FWM_MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420:
		/* One interleaved chroma plane; reported as U, no V plane. */
		ySize = width * height;
		uSize = width * height / 2;
		vSize = 0;
		break;
	case FWM_MM_PIXEL_FORMAT_YUV_PLANAR_420:
	case FWM_MM_PIXEL_FORMAT_YVU_PLANAR_420:
		/* Two quarter-size chroma planes. */
		ySize = width * height;
		uSize = width * height / 4;
		vSize = width * height / 4;
		break;
	case FWM_MM_PIXEL_FORMAT_YUV_AW_AFBC:
	case FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_2_0X:
	case FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_2_5X:
	case FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_1_0X:
		/*
		 * Compressed AW formats: the caller repurposes mStride[] as the
		 * three plane byte sizes; forward them verbatim, no arithmetic.
		 */
		ySize = pFrame->VFrame.mStride[0];
		uSize = pFrame->VFrame.mStride[1];
		vSize = pFrame->VFrame.mStride[2];
		break;
	case FWM_MM_PIXEL_FORMAT_RAW_SBGGR8:
	case FWM_MM_PIXEL_FORMAT_RAW_SGBRG8:
	case FWM_MM_PIXEL_FORMAT_RAW_SGRBG8:
	case FWM_MM_PIXEL_FORMAT_RAW_SRGGB8:
		ySize = width * height;
		uSize = 0;
		vSize = 0;
		break;
	case FWM_MM_PIXEL_FORMAT_RAW_SBGGR10:
	case FWM_MM_PIXEL_FORMAT_RAW_SGBRG10:
	case FWM_MM_PIXEL_FORMAT_RAW_SGRBG10:
	case FWM_MM_PIXEL_FORMAT_RAW_SRGGB10:
		/* Multiply before divide: (W*H*10)/8 in 32-bit unsigned. */
		ySize = width * height * 10 / 8;
		uSize = 0;
		vSize = 0;
		break;
	case FWM_MM_PIXEL_FORMAT_RAW_SBGGR12:
	case FWM_MM_PIXEL_FORMAT_RAW_SGBRG12:
	case FWM_MM_PIXEL_FORMAT_RAW_SGRBG12:
	case FWM_MM_PIXEL_FORMAT_RAW_SRGGB12:
		ySize = width * height * 12 / 8;
		uSize = 0;
		vSize = 0;
		break;
	default:
		media_utils_log_error("frame_size unsupported format",
				      (unsigned int)pFrame->VFrame.mPixelFormat);
		return FAILURE;
	}

	pSizeInfo->mYSize = (int)ySize;
	pSizeInfo->mUSize = (int)uSize;
	pSizeInfo->mVSize = (int)vSize;
	return SUCCESS;
}
