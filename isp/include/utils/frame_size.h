/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
#ifndef FREEWINNER_UTILS_FRAME_SIZE_H
#define FREEWINNER_UTILS_FRAME_SIZE_H

#include "media_utils_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

ERRORTYPE getVideoFrameBufferSizeInfo(VIDEO_FRAME_INFO_S *pFrame,
				      VideoFrameBufferSizeInfo *pSizeInfo);

#ifdef __cplusplus
}
#endif

#endif /* FREEWINNER_UTILS_FRAME_SIZE_H */
