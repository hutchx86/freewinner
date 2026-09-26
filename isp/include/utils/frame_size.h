/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * frame_size.h - per-fwm_pixel_format_e plane byte counts.
 *
 * The exported symbol is called by the VI glue and must keep this exact
 * name/signature (spec/media_utils/frame_size.md section 1).
 */
#ifndef FRAME_SIZE_H
#define FRAME_SIZE_H

#include "media_utils_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

ERRORTYPE getVideoFrameBufferSizeInfo(VIDEO_FRAME_INFO_S *pFrame,
				      VideoFrameBufferSizeInfo *pSizeInfo);

#ifdef __cplusplus
}
#endif

#endif /* FRAME_SIZE_H */
