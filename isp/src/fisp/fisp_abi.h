/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* ISP runtime (package B, `fisp_`): the interoperability surface this package
 * implements and consumes. Every symbol, scalar alias, error macro, device
 * count and companion seam here is a fact recorded in
 * cleanroom/middleware/fisp/SPEC.md sections 2, 4, 5 and 6. This header is the
 * interim stand-in for package D's public `media/mpi_isp.h`: when that lands it
 * supplies the same 25 declarations and this file can defer to it.
 *
 * Nothing here is derived from the vendor implementation. The exported names
 * are the public symbols the media daemon was compiled against. */

#ifndef FREEISP_FISP_ABI_H
#define FREEISP_FISP_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Scalar aliases (SPEC 2.1 / 5.1)                                     */
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

/* Negative platform error macros. The exact vendor numerics for the invalid-id
 * and efuse cases are not recorded in the spec (open questions 4); callers test
 * only the sign, so a documented placeholder is used. See NOT-IMPLEMENTED.md. */
#ifndef AW_ERR_VI_INVALID_CHN
#define AW_ERR_VI_INVALID_CHN  (-2)     /* VI invalid id / channel */
#endif
#ifndef EN_ERR_EFUSE_ERROR
#define EN_ERR_EFUSE_ERROR     (-2)     /* framework isp_init() efuse result */
#endif
#ifndef AW_ERR_ISP_EFUSE_ERROR
#define AW_ERR_ISP_EFUSE_ERROR (-2)     /* ISP efuse error macro */
#endif

/* Device counts (SPEC 5.1). */
#define FISP_HW_ISP_DEVICE_NUM  1       /* HW_ISP_DEVICE_NUM */
#define FISP_VI_ISP_NUM_MAX     2       /* VI_ISP_NUM_MAX */

/* ------------------------------------------------------------------ */
/* Exported entry points (SPEC 2.1) -- the 25 AW_MPI_ISP_* symbols      */
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

/*
 * ISP id -> VI video device resolver (SPEC 6.4). The capture runtime (`fcap_`)
 * owns the VI media device; this seam is how the V4L2-control path reaches the
 * device bound to an ISP id. The default implementation scans the clean device
 * layer's `media_params.video_dev[]` and matches `video_to_isp_id()`. `fcap_`
 * may install its own resolver with fisp_set_video_resolver(); NULL restores the
 * default. The resolver must return NULL before any vipp has been started.
 */
struct isp_video_device;
typedef struct isp_video_device *(*fisp_video_resolver_t)(int isp_dev);

extern fisp_video_resolver_t fisp_video_resolver;
void fisp_set_video_resolver(fisp_video_resolver_t resolver);

/*
 * Load-register hook companion (SPEC 4.3). Applies the documented D3D / 3DNR
 * bit edit to the framework's register-program buffer: offset 0x1a0 is the
 * 32-bit module-enable word, bit 5 (mask 0x20) is the D3D feature bit. With
 * `on` false the bit is cleared, but only when it is currently set; with `on`
 * true the buffer is left byte-identical. A buffer shorter than 0x1a4 bytes is
 * left untouched. Returns 1 if the word changed, 0 if it did not, -1 if the
 * buffer was too small or invalid. The `--wrap=isp_set_load_reg` shim itself
 * belongs to the consumer (mediad), not this package.
 */
struct isp_table_reg_map;
int fisp_load_reg_set_3dnr(struct isp_table_reg_map *reg, int on);

#ifdef __cplusplus
}
#endif

#endif /* FREEISP_FISP_ABI_H */
