/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/*
 * fcap_abi.h - capture runtime (package A, `fcap_`): the interoperability
 * surface this package implements and consumes.
 *
 * Every enum value, struct offset/size, error code and constant here is a fact
 * recorded in cleanroom/middleware/fcap/SPEC.md sections 2, 3 and 8.  The
 * layouts are the measured 32-bit ARM (EABI5, 4-byte enums, 4-byte pointers)
 * ABI, and the pointer-free records below reproduce it on any host.  This file
 * is the interim stand-in for package D's public `mm_comm_vi.h` / `mpi_vi.h`:
 * when those land they supply the same declarations and this file can defer.
 *
 * The frame descriptors and the pixel-format enum are not restated here; they
 * are the shared media-utils ABI (`media_utils_abi.h`), which the vendor VI
 * glue also consumed.  The V4L2 record types come from the clean device header
 * (`isp_dev_uapi.h`), the same interface facts the capture path applies.
 *
 * Nothing in this file is derived from the vendor implementation; the exported
 * names are the public symbols the media daemon was compiled against.
 *
 * This translation unit must be built with the default 4-byte enum: never add
 * -fshort-enums.
 */

#ifndef FREEWINNER_FCAP_ABI_H
#define FREEWINNER_FCAP_ABI_H

#include <stddef.h>
#include <stdint.h>

#include "media_utils_abi.h"   /* fwm_pixel_format_e, VIDEO_FRAME_S/_INFO_S     */
#include "isp_dev_uapi.h"      /* enum v4l2_buf_type/_memory, pix_mplane    */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Scalar aliases (SPEC 2.1)                                           */
/* ------------------------------------------------------------------ */

typedef int AW_S32;                     /* ERRORTYPE (media_utils_abi.h) */
typedef int VI_DEV;                     /* vipp index, 0..3 */
typedef int VI_CHN;                     /* virtual-channel index, 0..3 */

/* SUCCESS / FAILURE come from media_utils_abi.h (0 / -1). */

/* ------------------------------------------------------------------ */
/* Constants (SPEC 2.3)                                                */
/* ------------------------------------------------------------------ */

#define VI_VIPP_NUM_MAX            4
#define VI_VIRCHN_NUM_MAX          4
#define VI_ISP_NUM_MAX             2
#define MAX_VIPP_DEV_NUM           4
#define VI_HIGH_FRAMERATE_STANDARD 60

/* Per-channel frame FIFO depth (SPEC 3.4.2 / 3.4.1). */
#define VI_FIFO_LEVEL              20

/* Driver-buffer-index occupancy slots (SPEC 3.2.2 / 4.3). */
#define FCAP_OCC_SLOTS             32

/* The one node the capture path opens directly (SPEC 3.2.1). */
#define MEDIA_DEVICE_PATH          "/dev/media0"

/* ------------------------------------------------------------------ */
/* Error encoding (SPEC 8.1)                                           */
/*                                                                     */
/* DEF_ERR(module, level, errid) = 0xA0000000 | (module<<16) |         */
/*     (level<<13) | errid,  level = EN_ERR_LEVEL_ERROR = 4.           */
/* MOD_ID_SYS = 2, MOD_ID_VIU = 16.  The worked values in SPEC 7 are   */
/* the contract; the macros below expand to exactly those.             */
/* ------------------------------------------------------------------ */

#define FCAP_DEF_ERR(module, level, errid)                         \
	((int)(0xA0000000u | (((unsigned)(module)) << 16) |        \
	       (((unsigned)(level)) << 13) | ((unsigned)(errid))))

#define EN_ERR_LEVEL_ERROR 4
#define MOD_ID_SYS        2
#define MOD_ID_VIU        16

/* Generic error ids (SPEC 8.1). */
#define FCAP_ERR_INVALID_DEVID  1
#define FCAP_ERR_INVALID_CHNID  2
#define FCAP_ERR_ILLEGAL_PARAM  3
#define FCAP_ERR_EXIST          4
#define FCAP_ERR_UNEXIST        5
#define FCAP_ERR_NULL_PTR       6
#define FCAP_ERR_NOT_SUPPORT    8
#define FCAP_ERR_NOT_PERM       9
#define FCAP_ERR_NOMEM          12
#define FCAP_ERR_SYS_NOTREADY   16
#define FCAP_ERR_BUSY           18
#define FCAP_ERR_EFUSE_ERROR    25

/* SYS errors. */
#define ERR_SYS_ILLEGAL_PARAM FCAP_DEF_ERR(MOD_ID_SYS, EN_ERR_LEVEL_ERROR, FCAP_ERR_ILLEGAL_PARAM)
#define ERR_SYS_NOT_PERM      FCAP_DEF_ERR(MOD_ID_SYS, EN_ERR_LEVEL_ERROR, FCAP_ERR_NOT_PERM)
#define ERR_SYS_NOTREADY      FCAP_DEF_ERR(MOD_ID_SYS, EN_ERR_LEVEL_ERROR, FCAP_ERR_SYS_NOTREADY)

/* VI errors; the six SPEC 7 works the values are INVALID_DEVID (0xA0108001),
 * INVALID_CHNID (0xA0108002), EXIST (0xA0108004), UNEXIST (0xA0108005),
 * NOT_PERM (0xA0108009) and the SYS pair above. */
#define ERR_VI_INVALID_DEVID  FCAP_DEF_ERR(MOD_ID_VIU, EN_ERR_LEVEL_ERROR, FCAP_ERR_INVALID_DEVID)
#define ERR_VI_INVALID_CHNID  FCAP_DEF_ERR(MOD_ID_VIU, EN_ERR_LEVEL_ERROR, FCAP_ERR_INVALID_CHNID)
#define ERR_VI_ILLEGAL_PARAM  FCAP_DEF_ERR(MOD_ID_VIU, EN_ERR_LEVEL_ERROR, FCAP_ERR_ILLEGAL_PARAM)
#define ERR_VI_EXIST          FCAP_DEF_ERR(MOD_ID_VIU, EN_ERR_LEVEL_ERROR, FCAP_ERR_EXIST)
#define ERR_VI_UNEXIST        FCAP_DEF_ERR(MOD_ID_VIU, EN_ERR_LEVEL_ERROR, FCAP_ERR_UNEXIST)
#define ERR_VI_NULL_PTR       FCAP_DEF_ERR(MOD_ID_VIU, EN_ERR_LEVEL_ERROR, FCAP_ERR_NULL_PTR)
#define ERR_VI_NOT_SUPPORT    FCAP_DEF_ERR(MOD_ID_VIU, EN_ERR_LEVEL_ERROR, FCAP_ERR_NOT_SUPPORT)
#define ERR_VI_NOT_PERM       FCAP_DEF_ERR(MOD_ID_VIU, EN_ERR_LEVEL_ERROR, FCAP_ERR_NOT_PERM)
#define ERR_VI_NOMEM          FCAP_DEF_ERR(MOD_ID_VIU, EN_ERR_LEVEL_ERROR, FCAP_ERR_NOMEM)
#define ERR_VI_BUSY           FCAP_DEF_ERR(MOD_ID_VIU, EN_ERR_LEVEL_ERROR, FCAP_ERR_BUSY)

/* The SetVippAttr driver "efuse check" mapping (SPEC 3.2.3). */
#define ERR_VI_EIS_EFUSE_ERR  FCAP_DEF_ERR(MOD_ID_VIU, EN_ERR_LEVEL_ERROR, FCAP_ERR_EFUSE_ERROR)

/* ------------------------------------------------------------------ */
/* Records the caller builds (SPEC 2.2)                                */
/* ------------------------------------------------------------------ */

/* Video-port attribute; sizeof 228 (SPEC 2.2).  Field order is ABI. */
typedef struct VI_ATTR_S {
	enum v4l2_buf_type              type;              /* +0   */
	enum v4l2_memory                memtype;           /* +4   */
	struct v4l2_pix_format_mplane   format;            /* +8   */
	unsigned int                    nbufs;             /* +200 */
	unsigned int                    nplanes;           /* +204 */
	unsigned int                    fps;               /* +208 */
	unsigned int                    capturemode;       /* +212 */
	unsigned int                    use_current_win;   /* +216 */
	unsigned int                    wdr_mode;          /* +220 */
	unsigned int                    drop_frame_num;    /* +224 */
} VI_ATTR_S;                                           /* 228  */

typedef enum VI_SHUTTIME_MODE_E {
	VI_SHUTTIME_MODE_AUTO       = 0,
	VI_SHUTTIME_MODE_PREVIEW    = 1,
	VI_SHUTTIME_MODE_NIGHT_VIEW = 2
} VI_SHUTTIME_MODE_E;

typedef enum VI_SHUTTIME_RESET_E {
	VI_SHUTTIME_RESET_AUTO_DELAY = 0,
	VI_SHUTTIME_RESET_AUTO_NOW   = 1
} VI_SHUTTIME_RESET_E;

/* Long-exposure request; sizeof 20 (SPEC 2.2).  Field order is ABI. */
typedef struct VI_SHUTTIME_CFG_S {
	int                  iTime;         /* +0  1/500s -> 500, 1s -> -1    */
	int                  iExpValue;     /* +4  unused by callers          */
	int                  iGainValue;    /* +8  unused by callers          */
	VI_SHUTTIME_RESET_E  eResetMode;    /* +12 */
	VI_SHUTTIME_MODE_E   eShutterMode;  /* +16 */
} VI_SHUTTIME_CFG_S;                       /* 20  */

/* System configuration; sizeof 260 (SPEC 2.2).  Field order is ABI. */
typedef struct MPP_SYS_CONF_S {
	unsigned int nAlignWidth;      /* +0   stride alignment, 16..1024 */
	char         mkfcTmpDir[256];  /* +4   empty -> "/tmp"            */
} MPP_SYS_CONF_S;                      /* 260 */

/* ------------------------------------------------------------------ */
/* V4L2 control ids the capture path writes and reads (SPEC 8.2)       */
/* ------------------------------------------------------------------ */

#ifndef V4L2_CID_EXPOSURE_AUTO
#define V4L2_CID_EXPOSURE_AUTO     0x009a0901
#endif
#ifndef V4L2_CID_AUTOGAIN
#define V4L2_CID_AUTOGAIN          0x00980912
#endif
#ifndef V4L2_CID_EXPOSURE_ABSOLUTE
#define V4L2_CID_EXPOSURE_ABSOLUTE 0x009a0902
#endif
#ifndef V4L2_CID_GAIN
#define V4L2_CID_GAIN              0x00980913
#endif
#ifndef V4L2_CID_HFLIP
#define V4L2_CID_HFLIP             0x00980914
#endif
#ifndef V4L2_CID_VFLIP
#define V4L2_CID_VFLIP             0x00980915
#endif

/* ------------------------------------------------------------------ */
/* Exported entry points (SPEC 2.1) -- the 3 SYS + 16 VI symbols       */
/* ------------------------------------------------------------------ */

AW_S32 AW_MPI_SYS_SetConf(const MPP_SYS_CONF_S *pSysConf);
AW_S32 AW_MPI_SYS_Init(void);
AW_S32 AW_MPI_SYS_Exit(void);

AW_S32 AW_MPI_VI_CreateVipp(VI_DEV ViDev);
AW_S32 AW_MPI_VI_DestoryVipp(VI_DEV ViDev);
AW_S32 AW_MPI_VI_EnableVipp(VI_DEV ViDev);
AW_S32 AW_MPI_VI_DisableVipp(VI_DEV ViDev);

AW_S32 AW_MPI_VI_SetVippAttr(VI_DEV ViDev, VI_ATTR_S *pstAttr);
AW_S32 AW_MPI_VI_GetVippAttr(VI_DEV ViDev, VI_ATTR_S *pstAttr);

AW_S32 AW_MPI_VI_SetVippMirror(VI_DEV ViDev, int Value);
AW_S32 AW_MPI_VI_SetVippFlip(VI_DEV ViDev, int Value);
AW_S32 AW_MPI_VI_SetVippShutterTime(VI_DEV ViDev, VI_SHUTTIME_CFG_S *pTime);

AW_S32 AW_MPI_VI_CreateVirChn(VI_DEV ViDev, VI_CHN ViCh, void *pAttr);
AW_S32 AW_MPI_VI_DestoryVirChn(VI_DEV ViDev, VI_CHN ViCh);
AW_S32 AW_MPI_VI_EnableVirChn(VI_DEV ViDev, VI_CHN ViCh);
AW_S32 AW_MPI_VI_DisableVirChn(VI_DEV ViDev, VI_CHN ViCh);
AW_S32 AW_MPI_VI_SetVirChnAttr(VI_DEV ViDev, VI_CHN ViCh, void *pAttr);

AW_S32 AW_MPI_VI_GetFrame(VI_DEV ViDev, VI_CHN ViCh,
			  VIDEO_FRAME_INFO_S *pstFrameInfo, AW_S32 s32MilliSec);
AW_S32 AW_MPI_VI_ReleaseFrame(VI_DEV ViDev, VI_CHN ViCh,
			      VIDEO_FRAME_INFO_S *pstFrameInfo);

/* ------------------------------------------------------------------ */
/* ABI conformance for the pointer-free records (SPEC 2.2)             */
/* ------------------------------------------------------------------ */

#define FCAP_SA(cond, tag) typedef char fcap_sa_##tag[(cond) ? 1 : -1]

FCAP_SA(sizeof(VI_ATTR_S) == 228, vi_attr_size);
FCAP_SA(offsetof(VI_ATTR_S, nbufs) == 200, vi_attr_nbufs);
FCAP_SA(offsetof(VI_ATTR_S, nplanes) == 204, vi_attr_nplanes);
FCAP_SA(offsetof(VI_ATTR_S, fps) == 208, vi_attr_fps);
FCAP_SA(offsetof(VI_ATTR_S, capturemode) == 212, vi_attr_capturemode);
FCAP_SA(offsetof(VI_ATTR_S, use_current_win) == 216, vi_attr_usecurwin);
FCAP_SA(offsetof(VI_ATTR_S, wdr_mode) == 220, vi_attr_wdrmode);
FCAP_SA(offsetof(VI_ATTR_S, drop_frame_num) == 224, vi_attr_dropnum);
FCAP_SA(sizeof(VI_SHUTTIME_CFG_S) == 20, shuttime_size);
FCAP_SA(sizeof(MPP_SYS_CONF_S) == 260, sys_conf_size);
FCAP_SA(sizeof(struct v4l2_pix_format_mplane) == 192, pix_mplane_size);

#undef FCAP_SA

#ifdef __cplusplus
}
#endif

#endif /* FREEWINNER_FCAP_ABI_H */
