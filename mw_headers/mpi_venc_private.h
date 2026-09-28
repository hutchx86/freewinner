/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* VENC and region-hook MPI prototypes; the daemon's stub TU defines these seven
 * symbols, so the signatures must match. Declarations only. */
#ifndef FMW_MPI_VENC_PRIVATE_H
#define FMW_MPI_VENC_PRIVATE_H

#include "component/mm_component.h"
#include "media/mm_common.h"
#include "media/mm_comm_region.h"
#include "media/mm_comm_video.h"
#include "utils/plat_type.h"

#ifdef __cplusplus
extern "C" {
#endif

ERRORTYPE        VENC_Construct(void);
ERRORTYPE        VENC_Destruct(void);
MM_COMPONENTTYPE *VENC_GetChnComp(fwm_chn_t *pMppChn);

ERRORTYPE AW_MPI_VENC_SetRegion(VENC_CHN chn, RGN_HANDLE handle,
				fwm_region_attr_t *rgn,
				const fwm_region_chn_attr_t *chn_attr, fwm_bitmap_t *bmp);
ERRORTYPE AW_MPI_VENC_DeleteRegion(VENC_CHN chn, RGN_HANDLE handle);
ERRORTYPE AW_MPI_VENC_UpdateRegionChnAttr(VENC_CHN chn, RGN_HANDLE handle,
					  const fwm_region_chn_attr_t *chn_attr);
ERRORTYPE AW_MPI_VENC_UpdateOverlayBitmap(VENC_CHN chn, RGN_HANDLE handle,
					  fwm_bitmap_t *bmp);

#ifdef __cplusplus
}
#endif

#endif /* FMW_MPI_VENC_PRIVATE_H */
