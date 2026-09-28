/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* Include-name shim plus MPI scalar aliases; the channel struct (fwm_chn_t)
 * is declared in fwm_media_abi.h. */
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
