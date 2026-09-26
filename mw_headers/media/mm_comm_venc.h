/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * media/mm_comm_venc.h - thin include-name shim (H "single source",
 * 31-abi-headers.md §5.5/§5.6): VENC_PACK_S is now declared once, in
 * freewinner's generated MPP boundary header (fwm_media_abi.h, as
 * fwm_venc_pack_t).  This file keeps only its name so mediad's #include
 * lines do not change; app code now spells the type directly (Task 2 punch
 * list).
 */
#ifndef FMW_MEDIA_MM_COMM_VENC_H
#define FMW_MEDIA_MM_COMM_VENC_H

#include "mm_comm_video.h"
#include "fwm_media_abi.h"

#endif /* FMW_MEDIA_MM_COMM_VENC_H */
