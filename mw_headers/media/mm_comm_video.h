/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * media/mm_comm_video.h - thin include-name shim (H "single source",
 * 31-abi-headers.md §5.5/§5.6): VIDEO_FRAME_INFO_S and BITMAP_S are now
 * declared once, in freewinner's generated MPP boundary header
 * (fwm_media_abi.h, as fwm_video_frame_info_t / fwm_bitmap_t).  This file
 * keeps only its name so mediad's #include lines do not change; app code now
 * spells the types directly (Task 2 punch list).  The pixel-format enum is
 * freewinner's own kept fwm_pixel_format_e (media_utils_abi.h, decision:
 * keep as the public boundary header, not cleanimpl's bare PIXEL_FORMAT_E).
 */
#ifndef FMW_MEDIA_MM_COMM_VIDEO_H
#define FMW_MEDIA_MM_COMM_VIDEO_H

#include <stdint.h>

#include "utils/plat_type.h"
#include "fwm_media_abi.h"
#include "media_utils_abi.h"

#endif /* FMW_MEDIA_MM_COMM_VIDEO_H */
