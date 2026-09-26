/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * media/mm_comm_region.h - thin include-name shim (H "single source",
 * 31-abi-headers.md §5.5/§5.6): RGN_ATTR_S and RGN_CHN_ATTR_S are now
 * declared once, in freewinner's generated MPP boundary header
 * (fwm_media_abi.h, as fwm_region_attr_t / fwm_region_chn_attr_t).  This
 * file keeps only its name so mediad's #include lines do not change; app
 * code now spells the types directly (Task 2 punch list).
 */
#ifndef FMW_MEDIA_MM_COMM_REGION_H
#define FMW_MEDIA_MM_COMM_REGION_H

#include "mm_comm_video.h"
#include "fwm_media_abi.h"

/* Plain scalar handle, not an MPP-boundary struct: not part of the
 * fwm_media_abi.h rename, kept local same as before gutting. */
typedef unsigned int RGN_HANDLE;

#endif /* FMW_MEDIA_MM_COMM_REGION_H */
