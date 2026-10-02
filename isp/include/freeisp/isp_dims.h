/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* isp_dims.h - V831 ISP statistics-grid and table dimensions used by the shim tier;
 * must match the array extents in the generated fwi_isp_abi.h. */
#ifndef FREEISP_ISP_DIMS_H
#define FREEISP_ISP_DIMS_H

#ifndef ISP_AE_ROW
#define ISP_AE_ROW 16
#endif
#ifndef ISP_AE_COL
#define ISP_AE_COL 24
#endif
#ifndef ISP_HIST_NUM
#define ISP_HIST_NUM 256
#endif
#ifndef ISP_AWB_ROW
#define ISP_AWB_ROW 32
#endif
#ifndef ISP_AWB_COL
#define ISP_AWB_COL 32
#endif
#ifndef ISP_AFS_NUM
#define ISP_AFS_NUM 128
#endif
#ifndef ISP_PLTM_ROW
#define ISP_PLTM_ROW 24
#endif
#ifndef ISP_PLTM_COL
#define ISP_PLTM_COL 32
#endif
#ifndef ISP_DRC_TBL_SIZE
#define ISP_DRC_TBL_SIZE 256
#endif
#ifndef ISP_GAMMA_TBL_SIZE
#define ISP_GAMMA_TBL_SIZE 0x1000
#endif
#ifndef ISP_GAMMA_TBL_LENGTH
#define ISP_GAMMA_TBL_LENGTH (3 * 1024)
#endif
#ifndef ISP_LENS_TBL_SIZE
#define ISP_LENS_TBL_SIZE 256
#endif
#ifndef ISP_LSC_TBL_SIZE
#define ISP_LSC_TBL_SIZE 0x0800
#endif
#ifndef ISP_MSC_TBL_SIZE
#define ISP_MSC_TBL_SIZE 484
#endif
#ifndef ISP_MSC_TBL_LENGTH
#define ISP_MSC_TBL_LENGTH (3 * ISP_MSC_TBL_SIZE)
#endif
#ifndef ISP_MSC_TEMP_NUM
#define ISP_MSC_TEMP_NUM 6
#endif

/* Anti-flicker power-line setting (AFS flicker_mode).  Same 0..3 encoding as
 * the standard V4L2 power-line-frequency control (disabled/50/60/auto). */
enum freeisp_flicker_freq {
    FREEISP_FLICKER_DISABLED = 0,
    FREEISP_FLICKER_50HZ = 1,
    FREEISP_FLICKER_60HZ = 2,
    FREEISP_FLICKER_AUTO = 3,
};

#endif /* FREEISP_ISP_DIMS_H */
