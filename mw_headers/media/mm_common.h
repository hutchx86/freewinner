/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * media/mm_common.h - thin include-name shim (H "single source",
 * 31-abi-headers.md §5.5/§5.6): struct MPP_CHN_S is now declared once, in
 * freewinner's generated MPP boundary header (fwm_media_abi.h, as
 * struct fwm_chn / fwm_chn_t).  This file keeps only its name so mediad's
 * #include lines do not change; app code now spells the type fwm_chn_t
 * directly (Task 2 punch list) rather than through a local MPP_CHN_S alias.
 */
#ifndef FMW_MEDIA_MM_COMMON_H
#define FMW_MEDIA_MM_COMMON_H

#include "utils/plat_type.h"
#include "fwm_media_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 32-bit MPP result / argument alias used by the MPI prototypes. */
#ifndef AW_S32_DEFINED
#define AW_S32_DEFINED
typedef int AW_S32;
#endif

typedef int VI_DEV;
typedef int VI_CHN;
typedef int VENC_CHN;
typedef int ISP_DEV;

#ifdef __cplusplus
}
#endif

#endif /* FMW_MEDIA_MM_COMMON_H */
