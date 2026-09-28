/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* The AW_MPI_ISP_* entry points the daemon calls; implemented by the ISP runtime.
 * AW_MPI_ISP_Init is not on the daemon's call surface (see NOT-IMPLEMENTED.md). */
#ifndef FMW_MEDIA_MPI_ISP_H
#define FMW_MEDIA_MPI_ISP_H

#include "mm_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Lifecycle (3). */
AW_S32 AW_MPI_ISP_Run(ISP_DEV IspDev);
AW_S32 AW_MPI_ISP_Stop(ISP_DEV IspDev);
AW_S32 AW_MPI_ISP_Exit(void);

/* Luminance (1); note the int return, not AW_S32. */
int    AW_MPI_ISP_GetEnvLV(ISP_DEV IspDev);

/* AE setters (3). */
AW_S32 AW_MPI_ISP_AE_SetMode(ISP_DEV IspDev, int Value);
AW_S32 AW_MPI_ISP_AE_SetMetering(ISP_DEV IspDev, int Value);
AW_S32 AW_MPI_ISP_AE_SetExposureBias(ISP_DEV IspDev, int Value);

/* AE getters (7). */
AW_S32 AW_MPI_ISP_AE_GetMode(ISP_DEV IspDev, int *Value);
AW_S32 AW_MPI_ISP_AE_GetMetering(ISP_DEV IspDev, int *Value);
AW_S32 AW_MPI_ISP_AE_GetExposureBias(ISP_DEV IspDev, int *Value);
AW_S32 AW_MPI_ISP_AE_GetExposure(ISP_DEV IspDev, int *Value);
AW_S32 AW_MPI_ISP_AE_GetExposureLine(ISP_DEV IspDev, int *Value);
AW_S32 AW_MPI_ISP_AE_GetGain(ISP_DEV IspDev, int *Value);
AW_S32 AW_MPI_ISP_AE_GetEvIdx(ISP_DEV IspDev, int *Value);

/* Flicker (2). */
AW_S32 AW_MPI_ISP_SetFlicker(ISP_DEV IspDev, int Value);
AW_S32 AW_MPI_ISP_GetFlicker(ISP_DEV IspDev, int *Value);

/* Noise (2). */
AW_S32 AW_MPI_ISP_SetNRAttr(ISP_DEV IspDev, int Value);
AW_S32 AW_MPI_ISP_Set3NRAttr(ISP_DEV IspDev, int Value);

/* WDR strength (2). */
AW_S32 AW_MPI_ISP_SetPltmWDR(ISP_DEV IspDev, int Value);
AW_S32 AW_MPI_ISP_GetPltmWDR(ISP_DEV IspDev, int *Value);

/* Picture controls (4). */
AW_S32 AW_MPI_ISP_SetBrightness(ISP_DEV IspDev, int Value);
AW_S32 AW_MPI_ISP_SetContrast(ISP_DEV IspDev, int Value);
AW_S32 AW_MPI_ISP_SetSaturation(ISP_DEV IspDev, int Value);
AW_S32 AW_MPI_ISP_SetSharpness(ISP_DEV IspDev, int Value);

#ifdef __cplusplus
}
#endif

#endif /* FMW_MEDIA_MPI_ISP_H */
