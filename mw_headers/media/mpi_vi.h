/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* The 16 AW_MPI_VI_* entry points the daemon calls; implemented by the
 * capture runtime package. Declarations only. */
#ifndef FMW_MEDIA_MPI_VI_H
#define FMW_MEDIA_MPI_VI_H

#include "mm_common.h"
#include "mm_comm_vi.h"
#include "mm_comm_video.h"

#ifdef __cplusplus
extern "C" {
#endif

AW_S32 AW_MPI_VI_CreateVipp(VI_DEV ViDev);
AW_S32 AW_MPI_VI_DestoryVipp(VI_DEV ViDev);
AW_S32 AW_MPI_VI_EnableVipp(VI_DEV ViDev);
AW_S32 AW_MPI_VI_DisableVipp(VI_DEV ViDev);

AW_S32 AW_MPI_VI_SetVippAttr(VI_DEV ViDev, fwm_vi_attr_t *pstAttr);
AW_S32 AW_MPI_VI_GetVippAttr(VI_DEV ViDev, fwm_vi_attr_t *pstAttr);

AW_S32 AW_MPI_VI_SetVippMirror(VI_DEV ViDev, int Value);
AW_S32 AW_MPI_VI_SetVippFlip(VI_DEV ViDev, int Value);
AW_S32 AW_MPI_VI_SetVippShutterTime(VI_DEV ViDev, fwm_vi_shutter_cfg_t *pTime);

AW_S32 AW_MPI_VI_CreateVirChn(VI_DEV ViDev, VI_CHN ViCh, void *pAttr);
AW_S32 AW_MPI_VI_DestoryVirChn(VI_DEV ViDev, VI_CHN ViCh);
AW_S32 AW_MPI_VI_EnableVirChn(VI_DEV ViDev, VI_CHN ViCh);
AW_S32 AW_MPI_VI_DisableVirChn(VI_DEV ViDev, VI_CHN ViCh);
AW_S32 AW_MPI_VI_SetVirChnAttr(VI_DEV ViDev, VI_CHN ViCh, void *pAttr);

AW_S32 AW_MPI_VI_GetFrame(VI_DEV ViDev, VI_CHN ViCh,
			  fwm_video_frame_info_t *pstFrameInfo, AW_S32 s32MilliSec);
AW_S32 AW_MPI_VI_ReleaseFrame(VI_DEV ViDev, VI_CHN ViCh,
			      fwm_video_frame_info_t *pstFrameInfo);

#ifdef __cplusplus
}
#endif

#endif /* FMW_MEDIA_MPI_VI_H */
