/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* fisp_abi.h - ISP runtime (fisp_) interoperability surface: exported symbols, scalar
 * aliases, error macros, device count and companion seams; an interim stand-in for
 * media/mpi_isp.h. The names are the public symbols the media daemon was built against. */

#ifndef FREEISP_FISP_ABI_H
#define FREEISP_FISP_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Scalar aliases                                                      */
/* ------------------------------------------------------------------ */

typedef int AW_S32;
typedef int ISP_DEV;                    /* mm_common.h */

/* Return convention: SUCCESS is 0, FAILURE is -1 (plat_type.h). Callers only
 * test != 0; they never branch on a specific error value. */
#ifndef SUCCESS
#define SUCCESS 0
#endif
#ifndef FAILURE
#define FAILURE (-1)
#endif

/* Negative platform errors. The vendor numerics for the invalid-id and efuse cases are
 * unknown; callers test only the sign, so placeholders are used (NOT-IMPLEMENTED.md). */
#ifndef AW_ERR_VI_INVALID_CHN
#define AW_ERR_VI_INVALID_CHN  (-2)     /* VI invalid id / channel */
#endif
#ifndef EN_ERR_EFUSE_ERROR
#define EN_ERR_EFUSE_ERROR     (-2)     /* framework isp_init() efuse result */
#endif
#ifndef AW_ERR_ISP_EFUSE_ERROR
#define AW_ERR_ISP_EFUSE_ERROR (-2)     /* ISP efuse error macro */
#endif

/* Device counts. */
#define FISP_HW_ISP_DEVICE_NUM  1       /* HW_ISP_DEVICE_NUM */
#define FISP_VI_ISP_NUM_MAX     2       /* VI_ISP_NUM_MAX */

/* ------------------------------------------------------------------ */
/* Exported entry points -- the 25 AW_MPI_ISP_* symbols                 */
/* ------------------------------------------------------------------ */

/* Lifecycle (4). */
AW_S32 AW_MPI_ISP_Init(void);
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

/* ------------------------------------------------------------------ */
/* Documented companions                                               */
/* ------------------------------------------------------------------ */

/* ISP id -> VI video device resolver. The default scans media_params.video_dev[] by
 * video_to_isp_id() and returns NULL before any vipp starts; NULL restores the default. */
struct fwi_video_device;
typedef struct fwi_video_device *(*fisp_video_resolver_t)(int isp_dev);

extern fisp_video_resolver_t fisp_video_resolver;
void fisp_set_video_resolver(fisp_video_resolver_t resolver);

/* Clear the D3D/3DNR bit (0x20 in the module-enable word at 0x1a0) when `on` is false;
 * `on` true leaves it. Returns 1 changed, 0 unchanged, -1 if NULL or under 0x1a4 bytes. */
struct fwi_table_reg_map;
int fisp_load_reg_set_3dnr(struct fwi_table_reg_map *reg, int on);

#ifdef __cplusplus
}
#endif

#endif /* FREEISP_FISP_ABI_H */
