/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* ISP runtime (package B, `fisp_`): the `AW_MPI_ISP_*` API layer that sits
 * between the media daemon and the clean libisp framework tier. It owns the 25
 * ISP symbols the deployed build references -- the lifecycle wrappers, the AE
 * setters/getters, the luminance and flicker controls, the noise/WDR attribute
 * wrappers and the four picture-control setters. Behaviour is defined by
 * cleanroom/middleware/fisp/SPEC.md; this is original clean-room code.
 *
 * The package boundary is the symbol, not the vendor file: the picture-control
 * and AE setters physically live in the vendor's mpi_vi.c but are owned here.
 *
 * Deliberately not implemented (see NOT-IMPLEMENTED.md): the 42 defined but
 * unreferenced AW_MPI_ISP_* symbols, the compiled-out attribute-form setters,
 * the whole mpi_ae/mpi_awb unit, and the load-reg `--wrap` shim (a consumer
 * concern; the bit-edit helper is provided). */

#define _GNU_SOURCE

#include "fisp_abi.h"

#include "framework_isp.h"
#include "isp_dev_uapi.h"

#include <stdio.h>

#define FISP_LOG(...) fprintf(stderr, "fisp: " __VA_ARGS__)

/* ------------------------------------------------------------------ */
/* ISP id -> VI video device resolver (SPEC 6.4)                       */
/* ------------------------------------------------------------------ */

/*
 * Default resolver: scan the clean device layer's video devices and match the
 * one whose video_to_isp_id() equals the requested ISP id. The array is filled
 * by the capture runtime; before any vipp has started every slot is NULL and
 * the resolver returns NULL.
 */
static struct isp_video_device *fisp_resolver_scan(int isp_dev)
{
	unsigned int i;

	if (isp_dev < 0)
		return NULL;

	for (i = 0; i < HW_VIDEO_DEVICE_NUM; i++) {
		struct isp_video_device *video = media_params.video_dev[i];

		if (video != NULL && video_to_isp_id(video) == isp_dev)
			return video;
	}
	return NULL;
}

fisp_video_resolver_t fisp_video_resolver = fisp_resolver_scan;

void fisp_set_video_resolver(fisp_video_resolver_t resolver)
{
	fisp_video_resolver = resolver ? resolver : fisp_resolver_scan;
}

/* ------------------------------------------------------------------ */
/* Shared validation helpers                                           */
/* ------------------------------------------------------------------ */

/* Lifecycle and the attribute path use the framework's device count. */
static int fisp_isp_id_ok(int isp_dev)
{
	return isp_dev >= 0 && isp_dev < FISP_HW_ISP_DEVICE_NUM;
}

/* The V4L2-control path validates against the VI ISP count first. */
static int fisp_vi_id_ok(int isp_dev)
{
	return isp_dev >= 0 && isp_dev < FISP_VI_ISP_NUM_MAX;
}

/*
 * Resolve the video device for a V4L2-control operation. The caller has already
 * validated the id against the VI range. Returns NULL (and the caller returns
 * FAILURE) when no device is bound to the ISP.
 */
static struct isp_video_device *fisp_video_dev_for(int isp_dev)
{
	struct isp_video_device *video = fisp_video_resolver(isp_dev);

	if (video == NULL)
		FISP_LOG("no VI video device bound to isp %d\n", isp_dev);
	return video;
}

/* Single VIDIOC_S_CTRL write; SUCCESS on success, FAILURE on ioctl failure. */
static AW_S32 fisp_ctrl_write(int isp_dev, int cid, int value)
{
	struct isp_video_device *video = fisp_video_dev_for(isp_dev);

	if (video == NULL)
		return FAILURE;
	return video_set_control(video, cid, value) == 0 ? SUCCESS : FAILURE;
}

/* Single VIDIOC_G_CTRL read; stores through *value and returns SUCCESS. */
static AW_S32 fisp_ctrl_read(int isp_dev, int cid, int *value)
{
	struct isp_video_device *video;

	if (value == NULL)
		return FAILURE;
	video = fisp_video_dev_for(isp_dev);
	if (video == NULL)
		return FAILURE;
	if (video_get_control(video, cid, value) != 0)
		return FAILURE;
	return SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Lifecycle (SPEC 3.2)                                                */
/* ------------------------------------------------------------------ */

AW_S32 AW_MPI_ISP_Init(void)
{
	return media_dev_init();
}

AW_S32 AW_MPI_ISP_Run(ISP_DEV IspDev)
{
	int ret;

	if (!fisp_isp_id_ok(IspDev))
		return FAILURE;

	ret = isp_init(IspDev);
	if (ret == EN_ERR_EFUSE_ERROR)
		return AW_ERR_ISP_EFUSE_ERROR;

	return isp_run(IspDev);
}

AW_S32 AW_MPI_ISP_Stop(ISP_DEV IspDev)
{
	if (!fisp_isp_id_ok(IspDev))
		return FAILURE;

	/* Order is load-bearing: isp_exit joins the thread, so it must run only
	 * after isp_stop has let the event loop unwind. */
	isp_stop(IspDev);
	isp_pthread_join(IspDev);
	isp_exit(IspDev);
	return SUCCESS;
}

AW_S32 AW_MPI_ISP_Exit(void)
{
	media_dev_exit();
	return SUCCESS;
}

/* ------------------------------------------------------------------ */
/* AE setters (SPEC 3.3)                                               */
/* ------------------------------------------------------------------ */

AW_S32 AW_MPI_ISP_AE_SetMode(ISP_DEV IspDev, int Value)
{
	struct isp_video_device *video;

	if (!fisp_vi_id_ok(IspDev))
		return AW_ERR_VI_INVALID_CHN;
	if (Value != 0 && Value != 1)
		return AW_ERR_VI_INVALID_CHN;

	video = fisp_video_dev_for(IspDev);
	if (video == NULL)
		return FAILURE;

	/* Two writes in one call: the exposure mode, then the paired auto-gain. */
	if (video_set_control(video, V4L2_CID_EXPOSURE_AUTO, Value) != 0)
		return FAILURE;
	if (video_set_control(video, V4L2_CID_AUTOGAIN, Value == 0 ? 1 : 0) != 0)
		return FAILURE;
	return SUCCESS;
}

AW_S32 AW_MPI_ISP_AE_SetMetering(ISP_DEV IspDev, int Value)
{
	if (!fisp_vi_id_ok(IspDev))
		return AW_ERR_VI_INVALID_CHN;
	if (Value < 0 || Value > 3)
		return FAILURE;
	return fisp_ctrl_write(IspDev, V4L2_CID_EXPOSURE_METERING, Value);
}

AW_S32 AW_MPI_ISP_AE_SetExposureBias(ISP_DEV IspDev, int Value)
{
	if (!fisp_vi_id_ok(IspDev))
		return AW_ERR_VI_INVALID_CHN;
	if (Value < 0 || Value > 8)
		return AW_ERR_VI_INVALID_CHN;
	return fisp_ctrl_write(IspDev, V4L2_CID_AUTO_EXPOSURE_BIAS, Value);
}

/* ------------------------------------------------------------------ */
/* AE getters (SPEC 3.5)                                               */
/* ------------------------------------------------------------------ */

AW_S32 AW_MPI_ISP_AE_GetMode(ISP_DEV IspDev, int *Value)
{
	if (!fisp_vi_id_ok(IspDev))
		return AW_ERR_VI_INVALID_CHN;
	return fisp_ctrl_read(IspDev, V4L2_CID_EXPOSURE_AUTO, Value);
}

AW_S32 AW_MPI_ISP_AE_GetMetering(ISP_DEV IspDev, int *Value)
{
	if (!fisp_vi_id_ok(IspDev))
		return AW_ERR_VI_INVALID_CHN;
	return fisp_ctrl_read(IspDev, V4L2_CID_EXPOSURE_METERING, Value);
}

AW_S32 AW_MPI_ISP_AE_GetExposureBias(ISP_DEV IspDev, int *Value)
{
	if (!fisp_vi_id_ok(IspDev))
		return AW_ERR_VI_INVALID_CHN;
	return fisp_ctrl_read(IspDev, V4L2_CID_AUTO_EXPOSURE_BIAS, Value);
}

/* Reads EXPOSURE_ABSOLUTE; distinct from GetExposureLine's EXPOSURE. */
AW_S32 AW_MPI_ISP_AE_GetExposure(ISP_DEV IspDev, int *Value)
{
	if (!fisp_vi_id_ok(IspDev))
		return AW_ERR_VI_INVALID_CHN;
	return fisp_ctrl_read(IspDev, V4L2_CID_EXPOSURE_ABSOLUTE, Value);
}

AW_S32 AW_MPI_ISP_AE_GetExposureLine(ISP_DEV IspDev, int *Value)
{
	if (!fisp_vi_id_ok(IspDev))
		return AW_ERR_VI_INVALID_CHN;
	return fisp_ctrl_read(IspDev, V4L2_CID_EXPOSURE, Value);
}

AW_S32 AW_MPI_ISP_AE_GetGain(ISP_DEV IspDev, int *Value)
{
	if (!fisp_vi_id_ok(IspDev))
		return AW_ERR_VI_INVALID_CHN;
	return fisp_ctrl_read(IspDev, V4L2_CID_GAIN, Value);
}

/* Telemetry, not a V4L2 control (SPEC 3.6). */
AW_S32 AW_MPI_ISP_AE_GetEvIdx(ISP_DEV IspDev, int *Value)
{
	AW_S32 ret;

	if (Value == NULL)
		return FAILURE;
	ret = isp_get_attr_cfg(IspDev, ISP_CTRL_EV_IDX, Value);
	return ret == 0 ? SUCCESS : FAILURE;
}

/* ------------------------------------------------------------------ */
/* Luminance (SPEC 3.6)                                                */
/* ------------------------------------------------------------------ */

/* No id validation of its own; isp_get_lv returns -1 for a null device. The
 * raw framework value is returned unchanged -- no clamp, scale or conversion. */
int AW_MPI_ISP_GetEnvLV(ISP_DEV IspDev)
{
	return (int)isp_get_lv(IspDev);
}

/* ------------------------------------------------------------------ */
/* Flicker (SPEC 3.3)                                                  */
/* ------------------------------------------------------------------ */

AW_S32 AW_MPI_ISP_SetFlicker(ISP_DEV IspDev, int Value)
{
	if (!fisp_vi_id_ok(IspDev))
		return AW_ERR_VI_INVALID_CHN;
	if (Value < 0 || Value > 3)
		return SUCCESS;         /* out of range: accepted, nothing written */
	return fisp_ctrl_write(IspDev, V4L2_CID_POWER_LINE_FREQUENCY, Value);
}

AW_S32 AW_MPI_ISP_GetFlicker(ISP_DEV IspDev, int *Value)
{
	if (!fisp_vi_id_ok(IspDev))
		return AW_ERR_VI_INVALID_CHN;
	return fisp_ctrl_read(IspDev, V4L2_CID_POWER_LINE_FREQUENCY, Value);
}

/* ------------------------------------------------------------------ */
/* ISP_CTRL_* attribute setters/getters (SPEC 3.4)                     */
/* ------------------------------------------------------------------ */

/* The framework's attribute path owns id validation; the wrapper passes its
 * return through. An out-of-range value is accepted with nothing written. */

AW_S32 AW_MPI_ISP_SetNRAttr(ISP_DEV IspDev, int Value)
{
	if (Value < 0 || Value > 1000)
		return SUCCESS;
	return isp_set_attr_cfg(IspDev, ISP_CTRL_DN_STR, &Value);
}

AW_S32 AW_MPI_ISP_Set3NRAttr(ISP_DEV IspDev, int Value)
{
	if (Value < 0 || Value > 100)
		return SUCCESS;
	return isp_set_attr_cfg(IspDev, ISP_CTRL_3DN_STR, &Value);
}

AW_S32 AW_MPI_ISP_SetPltmWDR(ISP_DEV IspDev, int Value)
{
	if (Value < 0 || Value > 255)
		return SUCCESS;
	return isp_set_attr_cfg(IspDev, ISP_CTRL_PLTMWDR_STR, &Value);
}

AW_S32 AW_MPI_ISP_GetPltmWDR(ISP_DEV IspDev, int *Value)
{
	AW_S32 ret;

	if (Value == NULL)
		return FAILURE;
	ret = isp_get_attr_cfg(IspDev, ISP_CTRL_PLTMWDR_STR, Value);
	return ret == 0 ? SUCCESS : FAILURE;
}

/* ------------------------------------------------------------------ */
/* Picture controls (SPEC 3.3 / 3.7)                                   */
/* ------------------------------------------------------------------ */

/* The enforced ranges below are the behaviour; the public header's trailing
 * comments advertise different ones (SPEC 3.3 note). */

AW_S32 AW_MPI_ISP_SetBrightness(ISP_DEV IspDev, int Value)
{
	if (!fisp_vi_id_ok(IspDev))
		return AW_ERR_VI_INVALID_CHN;
	if (Value < -126 || Value > 126)
		return SUCCESS;
	return fisp_ctrl_write(IspDev, V4L2_CID_BRIGHTNESS, Value);
}

AW_S32 AW_MPI_ISP_SetContrast(ISP_DEV IspDev, int Value)
{
	if (!fisp_vi_id_ok(IspDev))
		return AW_ERR_VI_INVALID_CHN;
	if (Value < -64 || Value > 64)
		return SUCCESS;
	return fisp_ctrl_write(IspDev, V4L2_CID_CONTRAST, Value);
}

AW_S32 AW_MPI_ISP_SetSaturation(ISP_DEV IspDev, int Value)
{
	if (!fisp_vi_id_ok(IspDev))
		return AW_ERR_VI_INVALID_CHN;
	if (Value < -256 || Value > 512)
		return FAILURE;
	return fisp_ctrl_write(IspDev, V4L2_CID_SATURATION, Value);
}

AW_S32 AW_MPI_ISP_SetSharpness(ISP_DEV IspDev, int Value)
{
	if (!fisp_vi_id_ok(IspDev))
		return AW_ERR_VI_INVALID_CHN;
	if (Value < -32 || Value > 32)
		return FAILURE;
	return fisp_ctrl_write(IspDev, V4L2_CID_SHARPNESS, Value);
}
