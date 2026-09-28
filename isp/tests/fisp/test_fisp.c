/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* test_fisp.c - black-box tests for the fisp_ ISP runtime: replays the vectors
 * below against the fisp_seam.c recorder. No device, framework or camera. */

#include "fisp_abi.h"
#include "fisp_seam.h"

#include "framework_isp.h"
#include "isp_dev_uapi.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Check harness                                                       */
/* ------------------------------------------------------------------ */

static int g_checks;
static int g_failures;

#define CHECK(cond, msg)                                                     \
	do {                                                                 \
		g_checks++;                                                  \
		if (!(cond)) {                                               \
			g_failures++;                                        \
			printf("FAIL line %d: %s\n", __LINE__, (msg));       \
		}                                                            \
	} while (0)

#define CHECK_EQ(actual, expected, msg)                                      \
	do {                                                                 \
		long a_ = (long)(actual);                                    \
		long e_ = (long)(expected);                                  \
		g_checks++;                                                  \
		if (a_ != e_) {                                              \
			g_failures++;                                        \
			printf("FAIL line %d: %s (got %ld, want %ld)\n",     \
			       __LINE__, (msg), a_, e_);                     \
		}                                                            \
	} while (0)

/* ------------------------------------------------------------------ */
/* Log helpers                                                         */
/* ------------------------------------------------------------------ */

static const struct fisp_seam_call *nth(int op, int n)
{
	int i, k = 0;

	for (i = 0; i < fisp_seam_log_len(); i++) {
		const struct fisp_seam_call *c = fisp_seam_log_at(i);

		if (c->op == op && k++ == n)
			return c;
	}
	return NULL;
}

static void check_sctrl(int n, long id, long value, const char *msg)
{
	const struct fisp_seam_call *c = nth(FISP_OP_S_CTRL, n);

	CHECK(c != NULL, msg);
	if (c != NULL) {
		CHECK_EQ(c->a, id, msg);
		CHECK_EQ(c->b, value, msg);
	}
}

static void check_gctrl(int n, long id, const char *msg)
{
	const struct fisp_seam_call *c = nth(FISP_OP_G_CTRL, n);

	CHECK(c != NULL, msg);
	if (c != NULL)
		CHECK_EQ(c->a, id, msg);
}

static void check_attr_set(int n, long dev, long ctrl, long value,
			   const char *msg)
{
	const struct fisp_seam_call *c = nth(FISP_OP_ATTR_SET, n);

	CHECK(c != NULL, msg);
	if (c != NULL) {
		CHECK_EQ(c->a, dev, msg);
		CHECK_EQ(c->b, ctrl, msg);
		CHECK_EQ(c->c, value, msg);
	}
}

static void check_attr_get(int n, long dev, long ctrl, const char *msg)
{
	const struct fisp_seam_call *c = nth(FISP_OP_ATTR_GET, n);

	CHECK(c != NULL, msg);
	if (c != NULL) {
		CHECK_EQ(c->a, dev, msg);
		CHECK_EQ(c->b, ctrl, msg);
	}
}

/* A resolver that never finds a device (vector 35). */
static struct fwi_video_device *null_resolver(int isp_dev)
{
	(void)isp_dev;
	return NULL;
}

/* ------------------------------------------------------------------ */
/* ABI conformance                                                     */
/* ------------------------------------------------------------------ */

/* Compile-time layout/id asserts, in the model TU (test_fisp_abi.c). */
extern int fisp_abi_model_checks(void);

static void test_abi(void)
{
	/* fwi_table_reg_map layout is checked at compile time; the pointer-sized
	 * field cannot be checked on a 64-bit host. */
	g_checks += fisp_abi_model_checks();

	CHECK_EQ(ISP_LOAD_DRAM_SIZE, 0x13240, "ISP_LOAD_DRAM_SIZE");
	CHECK_EQ(ISP_REG_TBL_LENGTH, 33, "ISP_REG_TBL_LENGTH");
	CHECK_EQ(HW_ISP_DEVICE_NUM, 1, "HW_ISP_DEVICE_NUM");
	CHECK_EQ(HW_VIDEO_DEVICE_NUM, 4, "HW_VIDEO_DEVICE_NUM");
	CHECK_EQ(FISP_VI_ISP_NUM_MAX, 2, "VI_ISP_NUM_MAX");

	/* V4L2 control ids. */
	CHECK_EQ(V4L2_CID_BRIGHTNESS, 0x00980900, "V4L2_CID_BRIGHTNESS");
	CHECK_EQ(V4L2_CID_CONTRAST, 0x00980901, "V4L2_CID_CONTRAST");
	CHECK_EQ(V4L2_CID_SATURATION, 0x00980902, "V4L2_CID_SATURATION");
	CHECK_EQ(V4L2_CID_EXPOSURE, 0x00980911, "V4L2_CID_EXPOSURE");
	CHECK_EQ(V4L2_CID_GAIN, 0x00980913, "V4L2_CID_GAIN");
	CHECK_EQ(V4L2_CID_POWER_LINE_FREQUENCY, 0x00980918, "PLF");
	CHECK_EQ(V4L2_CID_SHARPNESS, 0x0098091b, "V4L2_CID_SHARPNESS");
	CHECK_EQ(V4L2_CID_EXPOSURE_AUTO, 0x009a0901, "EXPOSURE_AUTO");
	CHECK_EQ(V4L2_CID_EXPOSURE_ABSOLUTE, 0x009a0902, "EXPOSURE_ABSOLUTE");
	CHECK_EQ(V4L2_CID_AUTOGAIN, 0x00980912, "AUTOGAIN");
	CHECK_EQ(V4L2_CID_AUTO_EXPOSURE_BIAS, 0x009a0913, "AUTO_EXPOSURE_BIAS");
	CHECK_EQ(V4L2_CID_EXPOSURE_METERING, 0x009a0919, "EXPOSURE_METERING");

	/* ISP_CTRL_* ids. */
	CHECK_EQ(ISP_CTRL_PLTMWDR_STR, 2, "ISP_CTRL_PLTMWDR_STR");
	CHECK_EQ(ISP_CTRL_DN_STR, 3, "ISP_CTRL_DN_STR");
	CHECK_EQ(ISP_CTRL_3DN_STR, 4, "ISP_CTRL_3DN_STR");
	CHECK_EQ(ISP_CTRL_EV_IDX, 13, "ISP_CTRL_EV_IDX");

	/* Return convention. */
	CHECK_EQ(SUCCESS, 0, "SUCCESS");
	CHECK_EQ(FAILURE, -1, "FAILURE");
}

/* ------------------------------------------------------------------ */
/* Vectors 1-17: setters                                               */
/* ------------------------------------------------------------------ */

static void test_setters(void)
{
	int rc;

	/* 1: AE_SetMode(0,0) -> EXPOSURE_AUTO=0 then AUTOGAIN=1. */
	fisp_seam_reset();
	fisp_set_video_resolver(fisp_seam_resolver);
	rc = AW_MPI_ISP_AE_SetMode(0, 0);
	CHECK_EQ(rc, SUCCESS, "v1 rc");
	CHECK_EQ(fisp_seam_count(FISP_OP_S_CTRL), 2, "v1 two writes");
	check_sctrl(0, V4L2_CID_EXPOSURE_AUTO, 0, "v1 first write");
	check_sctrl(1, V4L2_CID_AUTOGAIN, 1, "v1 second write");

	/* 2: AE_SetMode(0,1) -> EXPOSURE_AUTO=1 then AUTOGAIN=0. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_AE_SetMode(0, 1);
	CHECK_EQ(rc, SUCCESS, "v2 rc");
	CHECK_EQ(fisp_seam_count(FISP_OP_S_CTRL), 2, "v2 two writes");
	check_sctrl(0, V4L2_CID_EXPOSURE_AUTO, 1, "v2 first write");
	check_sctrl(1, V4L2_CID_AUTOGAIN, 0, "v2 second write");

	/* 3: AE_SetMode(0,2) -> no ioctl, error. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_AE_SetMode(0, 2);
	CHECK(rc < 0, "v3 negative");
	CHECK_EQ(fisp_seam_count(FISP_OP_S_CTRL), 0, "v3 no ioctl");

	/* 4: AE_SetExposureBias(0,4). */
	fisp_seam_reset();
	rc = AW_MPI_ISP_AE_SetExposureBias(0, 4);
	CHECK_EQ(rc, SUCCESS, "v4 rc");
	check_sctrl(0, V4L2_CID_AUTO_EXPOSURE_BIAS, 4, "v4 write");

	/* 5: AE_SetExposureBias(0,9) -> no ioctl, error. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_AE_SetExposureBias(0, 9);
	CHECK(rc < 0, "v5 negative");
	CHECK_EQ(fisp_seam_count(FISP_OP_S_CTRL), 0, "v5 no ioctl");

	/* 6: AE_SetMetering(0,2). */
	fisp_seam_reset();
	rc = AW_MPI_ISP_AE_SetMetering(0, 2);
	CHECK_EQ(rc, SUCCESS, "v6 rc");
	check_sctrl(0, V4L2_CID_EXPOSURE_METERING, 2, "v6 write");

	/* 7: AE_SetMetering(0,4) -> no ioctl, FAILURE. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_AE_SetMetering(0, 4);
	CHECK_EQ(rc, FAILURE, "v7 rc");
	CHECK_EQ(fisp_seam_count(FISP_OP_S_CTRL), 0, "v7 no ioctl");

	/* 8: SetFlicker(0,1). */
	fisp_seam_reset();
	rc = AW_MPI_ISP_SetFlicker(0, 1);
	CHECK_EQ(rc, SUCCESS, "v8 rc");
	check_sctrl(0, V4L2_CID_POWER_LINE_FREQUENCY, 1, "v8 write");

	/* 9: SetFlicker(0,9) -> no ioctl, SUCCESS. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_SetFlicker(0, 9);
	CHECK_EQ(rc, SUCCESS, "v9 rc");
	CHECK_EQ(fisp_seam_count(FISP_OP_S_CTRL), 0, "v9 no ioctl");

	/* 10: SetBrightness(0,-126). */
	fisp_seam_reset();
	rc = AW_MPI_ISP_SetBrightness(0, -126);
	CHECK_EQ(rc, SUCCESS, "v10 rc");
	check_sctrl(0, V4L2_CID_BRIGHTNESS, -126, "v10 write");

	/* 11: SetBrightness(0,200) -> no ioctl, SUCCESS. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_SetBrightness(0, 200);
	CHECK_EQ(rc, SUCCESS, "v11 rc");
	CHECK_EQ(fisp_seam_count(FISP_OP_S_CTRL), 0, "v11 no ioctl");

	/* 12: SetContrast(0,64). */
	fisp_seam_reset();
	rc = AW_MPI_ISP_SetContrast(0, 64);
	CHECK_EQ(rc, SUCCESS, "v12 rc");
	check_sctrl(0, V4L2_CID_CONTRAST, 64, "v12 write");

	/* 13: SetContrast(0,100) -> no ioctl, SUCCESS. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_SetContrast(0, 100);
	CHECK_EQ(rc, SUCCESS, "v13 rc");
	CHECK_EQ(fisp_seam_count(FISP_OP_S_CTRL), 0, "v13 no ioctl");

	/* 14: SetSaturation(0,512). */
	fisp_seam_reset();
	rc = AW_MPI_ISP_SetSaturation(0, 512);
	CHECK_EQ(rc, SUCCESS, "v14 rc");
	check_sctrl(0, V4L2_CID_SATURATION, 512, "v14 write");

	/* 15: SetSaturation(0,600) -> no ioctl, FAILURE. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_SetSaturation(0, 600);
	CHECK_EQ(rc, FAILURE, "v15 rc");
	CHECK_EQ(fisp_seam_count(FISP_OP_S_CTRL), 0, "v15 no ioctl");

	/* 16: SetSharpness(0,32). */
	fisp_seam_reset();
	rc = AW_MPI_ISP_SetSharpness(0, 32);
	CHECK_EQ(rc, SUCCESS, "v16 rc");
	check_sctrl(0, V4L2_CID_SHARPNESS, 32, "v16 write");

	/* 17: SetSharpness(0,40) -> no ioctl, FAILURE. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_SetSharpness(0, 40);
	CHECK_EQ(rc, FAILURE, "v17 rc");
	CHECK_EQ(fisp_seam_count(FISP_OP_S_CTRL), 0, "v17 no ioctl");
}

/* ------------------------------------------------------------------ */
/* Vectors 18-23: V4L2 getters                                         */
/* ------------------------------------------------------------------ */

static void test_getters(void)
{
	int v, rc;

	/* 18: AE_GetMode. */
	fisp_seam_reset();
	fisp_seam_g_ctrl_value = 1234;
	v = -1;
	rc = AW_MPI_ISP_AE_GetMode(0, &v);
	CHECK_EQ(rc, SUCCESS, "v18 rc");
	check_gctrl(0, V4L2_CID_EXPOSURE_AUTO, "v18 ctrl");
	CHECK_EQ(v, 1234, "v18 value");

	/* 19: AE_GetExposure reads EXPOSURE_ABSOLUTE. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_AE_GetExposure(0, &v);
	CHECK_EQ(rc, SUCCESS, "v19 rc");
	check_gctrl(0, V4L2_CID_EXPOSURE_ABSOLUTE, "v19 ctrl");

	/* 20: AE_GetExposureLine reads EXPOSURE (different control). */
	fisp_seam_reset();
	rc = AW_MPI_ISP_AE_GetExposureLine(0, &v);
	CHECK_EQ(rc, SUCCESS, "v20 rc");
	check_gctrl(0, V4L2_CID_EXPOSURE, "v20 ctrl");

	/* 21: AE_GetGain. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_AE_GetGain(0, &v);
	CHECK_EQ(rc, SUCCESS, "v21 rc");
	check_gctrl(0, V4L2_CID_GAIN, "v21 ctrl");

	/* 22: AE_GetExposureBias. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_AE_GetExposureBias(0, &v);
	CHECK_EQ(rc, SUCCESS, "v22 rc");
	check_gctrl(0, V4L2_CID_AUTO_EXPOSURE_BIAS, "v22 ctrl");

	/* 23: AE_GetMetering. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_AE_GetMetering(0, &v);
	CHECK_EQ(rc, SUCCESS, "v23 rc");
	check_gctrl(0, V4L2_CID_EXPOSURE_METERING, "v23 ctrl");

	/* GetFlicker reads the same control SetFlicker writes. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_GetFlicker(0, &v);
	CHECK_EQ(rc, SUCCESS, "GetFlicker rc");
	check_gctrl(0, V4L2_CID_POWER_LINE_FREQUENCY, "GetFlicker ctrl");

	/* A failing G_CTRL is FAILURE and the value is left unspecified. */
	fisp_seam_reset();
	fisp_seam_g_ctrl_rc = -1;
	rc = AW_MPI_ISP_AE_GetGain(0, &v);
	CHECK_EQ(rc, FAILURE, "getter ioctl failure");
}

/* ------------------------------------------------------------------ */
/* Vectors 24-33: attribute path and luminance                         */
/* ------------------------------------------------------------------ */

static void test_attr_and_lv(void)
{
	int v, rc;

	/* 24: AE_GetEvIdx -> isp_get_attr_cfg(0, EV_IDX, &v). */
	fisp_seam_reset();
	fisp_seam_attr_get_value = 99;
	v = -1;
	rc = AW_MPI_ISP_AE_GetEvIdx(0, &v);
	CHECK_EQ(rc, SUCCESS, "v24 rc");
	check_attr_get(0, 0, ISP_CTRL_EV_IDX, "v24 attr get");
	CHECK_EQ(v, 99, "v24 value");

	/* 25: SetPltmWDR(0,128). */
	fisp_seam_reset();
	rc = AW_MPI_ISP_SetPltmWDR(0, 128);
	CHECK_EQ(rc, SUCCESS, "v25 rc");
	check_attr_set(0, 0, ISP_CTRL_PLTMWDR_STR, 128, "v25 attr set");

	/* 26: SetPltmWDR(0,256) -> no attr call, SUCCESS. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_SetPltmWDR(0, 256);
	CHECK_EQ(rc, SUCCESS, "v26 rc");
	CHECK_EQ(fisp_seam_count(FISP_OP_ATTR_SET), 0, "v26 no attr call");

	/* 27: GetPltmWDR(0,&v). */
	fisp_seam_reset();
	v = -1;
	rc = AW_MPI_ISP_GetPltmWDR(0, &v);
	CHECK_EQ(rc, SUCCESS, "v27 rc");
	check_attr_get(0, 0, ISP_CTRL_PLTMWDR_STR, "v27 attr get");

	/* 28: SetNRAttr(0,1000). */
	fisp_seam_reset();
	rc = AW_MPI_ISP_SetNRAttr(0, 1000);
	CHECK_EQ(rc, SUCCESS, "v28 rc");
	check_attr_set(0, 0, ISP_CTRL_DN_STR, 1000, "v28 attr set");

	/* 29: SetNRAttr(0,1001) -> no attr call, SUCCESS. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_SetNRAttr(0, 1001);
	CHECK_EQ(rc, SUCCESS, "v29 rc");
	CHECK_EQ(fisp_seam_count(FISP_OP_ATTR_SET), 0, "v29 no attr call");

	/* 30: Set3NRAttr(0,100). */
	fisp_seam_reset();
	rc = AW_MPI_ISP_Set3NRAttr(0, 100);
	CHECK_EQ(rc, SUCCESS, "v30 rc");
	check_attr_set(0, 0, ISP_CTRL_3DN_STR, 100, "v30 attr set");

	/* 31: Set3NRAttr(0,101) -> no attr call, SUCCESS. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_Set3NRAttr(0, 101);
	CHECK_EQ(rc, SUCCESS, "v31 rc");
	CHECK_EQ(fisp_seam_count(FISP_OP_ATTR_SET), 0, "v31 no attr call");

	/* The attribute path passes the framework's error through. */
	fisp_seam_reset();
	fisp_seam_attr_set_rc = -1;
	rc = AW_MPI_ISP_SetNRAttr(0, 500);
	CHECK_EQ(rc, -1, "attr set error passthrough");
	fisp_seam_reset();
	fisp_seam_attr_get_rc = -1;
	rc = AW_MPI_ISP_GetPltmWDR(0, &v);
	CHECK_EQ(rc, FAILURE, "attr get error -> FAILURE");

	/* 32: GetEnvLV(0) returns the raw framework value. */
	fisp_seam_reset();
	fisp_seam_lv = 321;
	rc = AW_MPI_ISP_GetEnvLV(0);
	CHECK_EQ(rc, 321, "v32 lv passthrough");
	CHECK_EQ(fisp_seam_count(FISP_OP_ISP_GET_LV), 1, "v32 isp_get_lv call");

	/* 33: GetEnvLV(0) before Run -> -1. */
	fisp_seam_reset();
	fisp_seam_lv = -1;
	rc = AW_MPI_ISP_GetEnvLV(0);
	CHECK_EQ(rc, -1, "v33 lv before run");

	/* GetEnvLV does not validate the id itself. */
	fisp_seam_reset();
	fisp_seam_lv = 5;
	rc = AW_MPI_ISP_GetEnvLV(7);
	CHECK_EQ(rc, 5, "GetEnvLV forwards out-of-range id");
	{
		const struct fisp_seam_call *c = nth(FISP_OP_ISP_GET_LV, 0);

		CHECK(c != NULL, "GetEnvLV isp_get_lv call");
		if (c != NULL)
			CHECK_EQ(c->a, 7, "GetEnvLV id forwarded");
	}
}

/* ------------------------------------------------------------------ */
/* Vectors 34-35: id and resolver failures                             */
/* ------------------------------------------------------------------ */

static void test_id_and_resolver(void)
{
	int rc;

	/* 34: AE_SetMode(2,0) -> no ioctl, no resolver call, error. */
	fisp_seam_reset();
	fisp_set_video_resolver(fisp_seam_resolver);
	rc = AW_MPI_ISP_AE_SetMode(2, 0);
	CHECK(rc < 0, "v34 negative");
	CHECK_EQ(fisp_seam_count(FISP_OP_S_CTRL), 0, "v34 no ioctl");
	CHECK_EQ(fisp_seam_count(FISP_OP_RESOLVE), 0, "v34 no resolver call");

	/* 35: resolver returns NULL -> no ioctl, FAILURE. */
	fisp_seam_reset();
	fisp_set_video_resolver(null_resolver);
	rc = AW_MPI_ISP_SetBrightness(0, 0);
	CHECK_EQ(rc, FAILURE, "v35 rc");
	CHECK_EQ(fisp_seam_count(FISP_OP_S_CTRL), 0, "v35 no ioctl");

	/* Every V4L2 entry point rejects an out-of-VI-range id. */
	fisp_seam_reset();
	fisp_set_video_resolver(fisp_seam_resolver);
	CHECK(AW_MPI_ISP_SetFlicker(2, 1) < 0, "SetFlicker id 2");
	CHECK(AW_MPI_ISP_AE_GetGain(2, &rc) < 0, "GetGain id 2");
	CHECK_EQ(fisp_seam_count(FISP_OP_S_CTRL), 0, "id 2 no ioctl");
	CHECK_EQ(fisp_seam_count(FISP_OP_G_CTRL), 0, "id 2 no g_ioctl");
}

/* ------------------------------------------------------------------ */
/* Vectors 36-41: lifecycle                                            */
/* ------------------------------------------------------------------ */

static void test_lifecycle(void)
{
	int rc;

	/* 36: Run(0) -> isp_init then isp_run; returns isp_run's result. */
	fisp_seam_reset();
	fisp_set_video_resolver(fisp_seam_resolver);
	fisp_seam_isp_run_rc = 0;
	rc = AW_MPI_ISP_Run(0);
	CHECK_EQ(rc, SUCCESS, "v36 rc");
	CHECK_EQ(fisp_seam_count(FISP_OP_ISP_INIT), 1, "v36 isp_init");
	CHECK_EQ(fisp_seam_count(FISP_OP_ISP_RUN), 1, "v36 isp_run");
	{
		const struct fisp_seam_call *c = nth(FISP_OP_ISP_INIT, 0);

		CHECK(c != NULL, "v36 init call");
		if (c != NULL)
			CHECK_EQ(c->a, 0, "v36 init dev");
	}

	/* isp_run's result is returned unchanged. */
	fisp_seam_reset();
	fisp_seam_isp_run_rc = -7;
	rc = AW_MPI_ISP_Run(0);
	CHECK_EQ(rc, -7, "v36 run result passthrough");

	/* 37: efuse error -> no isp_run, ISP efuse macro. */
	fisp_seam_reset();
	fisp_seam_isp_init_rc = EN_ERR_EFUSE_ERROR;
	rc = AW_MPI_ISP_Run(0);
	CHECK_EQ(rc, AW_ERR_ISP_EFUSE_ERROR, "v37 efuse macro");
	CHECK_EQ(fisp_seam_count(FISP_OP_ISP_RUN), 0, "v37 no isp_run");

	/* 38: Run(0) twice -> SUCCESS both times. (The framework's own use-count
	 * makes the second call a no-op; the wrapper forwards faithfully.) */
	fisp_seam_reset();
	CHECK_EQ(AW_MPI_ISP_Run(0), SUCCESS, "v38 first");
	CHECK_EQ(AW_MPI_ISP_Run(0), SUCCESS, "v38 second");
	CHECK_EQ(fisp_seam_count(FISP_OP_ISP_RUN), 2, "v38 two run calls");

	/* 39: Stop(0) order: stop, join, exit; SUCCESS. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_Stop(0);
	CHECK_EQ(rc, SUCCESS, "v39 rc");
	CHECK_EQ(fisp_seam_log_len(), 3, "v39 three calls");
	if (fisp_seam_log_len() == 3) {
		CHECK_EQ(fisp_seam_log_at(0)->op, FISP_OP_ISP_STOP, "v39 stop");
		CHECK_EQ(fisp_seam_log_at(1)->op, FISP_OP_ISP_JOIN, "v39 join");
		CHECK_EQ(fisp_seam_log_at(2)->op, FISP_OP_ISP_EXIT, "v39 exit");
	}

	/* 40: Init() -> media_dev_init. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_Init();
	CHECK_EQ(rc, 0, "v40 rc");
	CHECK_EQ(fisp_seam_count(FISP_OP_MEDIA_DEV_INIT), 1, "v40 media_dev_init");

	/* 41: Exit() -> media_dev_exit; SUCCESS. */
	fisp_seam_reset();
	rc = AW_MPI_ISP_Exit();
	CHECK_EQ(rc, SUCCESS, "v41 rc");
	CHECK_EQ(fisp_seam_count(FISP_OP_MEDIA_DEV_EXIT), 1, "v41 media_dev_exit");

	/* Out-of-range lifecycle ids are rejected without touching anything. */
	fisp_seam_reset();
	CHECK(AW_MPI_ISP_Run(1) < 0, "Run id 1");
	CHECK(AW_MPI_ISP_Stop(1) < 0, "Stop id 1");
	CHECK_EQ(fisp_seam_log_len(), 0, "bad lifecycle id makes no call");
}

/* ------------------------------------------------------------------ */
/* Default resolver scans media_params.video_dev[]                     */
/* ------------------------------------------------------------------ */

static void test_default_resolver(void)
{
	int rc;

	fisp_seam_reset();
	fisp_set_video_resolver(NULL);          /* restore the default scan */

	memset(media_params.video_dev, 0, sizeof media_params.video_dev);
	fisp_seam_video0.isp_id = 0;
	media_params.video_dev[0] = &fisp_seam_video0;

	rc = AW_MPI_ISP_SetBrightness(0, 10);
	CHECK_EQ(rc, SUCCESS, "default resolver match");
	check_sctrl(0, V4L2_CID_BRIGHTNESS, 10, "default resolver write");

	/* A device bound to a different ISP is not matched. */
	fisp_seam_reset();
	fisp_seam_video0.isp_id = 1;
	rc = AW_MPI_ISP_SetBrightness(0, 10);
	CHECK_EQ(rc, FAILURE, "default resolver mismatch -> FAILURE");
	CHECK_EQ(fisp_seam_count(FISP_OP_S_CTRL), 0, "mismatch no ioctl");

	memset(media_params.video_dev, 0, sizeof media_params.video_dev);
	fisp_seam_video0.isp_id = 0;
	fisp_set_video_resolver(fisp_seam_resolver);
}

/* ------------------------------------------------------------------ */
/* Load-register 3DNR hook                                             */
/* ------------------------------------------------------------------ */

static void test_load_reg(void)
{
	struct fwi_table_reg_map reg;
	static unsigned char buf[ISP_LOAD_DRAM_SIZE];
	unsigned int word;
	int rc;

	/* 3DNR off with the bit set: cleared, other bits unchanged. */
	memset(buf, 0xa5, sizeof buf);
	word = 0xffffffffu;
	memcpy(buf + 0x1a0, &word, 4);
	reg.addr = buf;
	reg.size = sizeof buf;
	rc = fisp_load_reg_set_3dnr(&reg, 0);
	CHECK_EQ(rc, 1, "load-reg off changed");
	memcpy(&word, buf + 0x1a0, 4);
	CHECK_EQ(word, 0xffffffdfu, "load-reg bit 5 cleared");
	CHECK_EQ(buf[0x1a4], 0xa5, "load-reg byte after word untouched");
	CHECK_EQ(buf[0x19f], 0xa5, "load-reg byte before word untouched");

	/* 3DNR on: byte-identical. */
	memset(buf, 0xa5, sizeof buf);
	word = 0xffffffffu;
	memcpy(buf + 0x1a0, &word, 4);
	rc = fisp_load_reg_set_3dnr(&reg, 1);
	CHECK_EQ(rc, 0, "load-reg on unchanged");
	memcpy(&word, buf + 0x1a0, 4);
	CHECK_EQ(word, 0xffffffffu, "load-reg on byte-identical");

	/* Already clear: no change reported (no needless store). */
	memset(buf, 0xa5, sizeof buf);
	word = 0xffffffdfu;
	memcpy(buf + 0x1a0, &word, 4);
	rc = fisp_load_reg_set_3dnr(&reg, 0);
	CHECK_EQ(rc, 0, "load-reg already clear");
	memcpy(&word, buf + 0x1a0, 4);
	CHECK_EQ(word, 0xffffffdfu, "load-reg already clear unchanged");

	/* A buffer shorter than 0x1a4 is left untouched and does not fault. */
	memset(buf, 0xa5, sizeof buf);
	word = 0xffffffffu;
	memcpy(buf + 0x1a0, &word, 4);
	reg.size = 0x1a3;
	rc = fisp_load_reg_set_3dnr(&reg, 0);
	CHECK_EQ(rc, -1, "load-reg short buffer skipped");
	memcpy(&word, buf + 0x1a0, 4);
	CHECK_EQ(word, 0xffffffffu, "load-reg short buffer untouched");

	/* Exactly 0x1a4 is enough. */
	reg.size = 0x1a4;
	rc = fisp_load_reg_set_3dnr(&reg, 0);
	CHECK_EQ(rc, 1, "load-reg 0x1a4 accepted");

	/* Invalid inputs. */
	CHECK_EQ(fisp_load_reg_set_3dnr(NULL, 0), -1, "load-reg null reg");
	reg.addr = NULL;
	reg.size = sizeof buf;
	CHECK_EQ(fisp_load_reg_set_3dnr(&reg, 0), -1, "load-reg null addr");
}

int main(void)
{
	test_abi();
	test_setters();
	test_getters();
	test_attr_and_lv();
	test_id_and_resolver();
	test_lifecycle();
	test_default_resolver();
	test_load_reg();

	printf("fisp: %d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
