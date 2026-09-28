/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* test_fcap.c - black-box tests for the capture runtime: replays the vectors
 * below against the fcap_seam.c recorder. No device, ISP tier or camera. */

#include "fcap_abi.h"
#include "fcap_seam.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Check harness                                                       */
/* ------------------------------------------------------------------ */

static int g_checks;
static int g_failures;

#define CHECK(cond, msg)                                                  \
	do {                                                              \
		g_checks++;                                               \
		if (!(cond)) {                                            \
			g_failures++;                                     \
			printf("FAIL line %d: %s\n", __LINE__, (msg));    \
		}                                                         \
	} while (0)

#define CHECK_EQ(actual, expected, msg)                                   \
	do {                                                              \
		long a_ = (long)(actual);                                 \
		long e_ = (long)(expected);                               \
		g_checks++;                                               \
		if (a_ != e_) {                                           \
			g_failures++;                                     \
			printf("FAIL line %d: %s (got %ld, want %ld)\n",  \
			       __LINE__, (msg), a_, e_);                  \
		}                                                         \
	} while (0)

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void sys_up(void)
{
	MPP_SYS_CONF_S c;

	fcap_seam_reset();
	memset(&c, 0, sizeof c);
	c.nAlignWidth = 32;
	(void)AW_MPI_SYS_SetConf(&c);
	(void)AW_MPI_SYS_Init();
}

static void fill_attr(VI_ATTR_S *a, unsigned int w, unsigned int h,
		      unsigned int fourcc, unsigned int nbufs,
		      unsigned int nplanes, unsigned int wdr, int drop)
{
	memset(a, 0, sizeof *a);
	a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->memtype = V4L2_MEMORY_MMAP;
	a->format.width = w;
	a->format.height = h;
	a->format.pixelformat = fourcc;
	a->format.num_planes = (uint8_t)nplanes;
	a->nbufs = nbufs;
	a->nplanes = nplanes;
	a->fps = 20;
	a->capturemode = 2;
	a->use_current_win = 0;
	a->wdr_mode = wdr;
	a->drop_frame_num = (unsigned int)drop;
}

/* Create vipp, apply the attribute, enable: the daemon's vi_start sequence. */
static int vi_up(int dev, const VI_ATTR_S *a)
{
	if (AW_MPI_VI_CreateVipp(dev) != SUCCESS)
		return -1;
	if (AW_MPI_VI_SetVippAttr(dev, (VI_ATTR_S *)a) != SUCCESS)
		return -1;
	return AW_MPI_VI_EnableVipp(dev) == SUCCESS ? 0 : -1;
}

static int req_index(unsigned long req, int from)
{
	int i;

	for (i = from; i < fcap_seam_log_len(); i++)
		if (fcap_seam_log_at(i)->req == req)
			return i;
	return -1;
}

static void check_sctrl(int n, long id, long value, const char *msg)
{
	const struct fcap_seam_rec *r = fcap_seam_nth(VIDIOC_S_CTRL, n);
	struct v4l2_control c;

	CHECK(r != NULL, msg);
	if (r != NULL) {
		memcpy(&c, r->arg, sizeof c);
		CHECK_EQ(c.id, id, msg);
		CHECK_EQ(c.value, value, msg);
	}
}

static void check_sensor_fps(long want, const char *msg)
{
	const struct fcap_seam_rec *r = fcap_seam_last(VIDIOC_VIN_SENSOR_SET_FPS);
	struct sensor_fps f;

	CHECK(r != NULL, msg);
	if (r != NULL) {
		memcpy(&f, r->arg, sizeof f);
		CHECK_EQ(f.fps, want, msg);
	}
}

static void sleep_ms(unsigned int ms)
{
	struct timespec ts;

	ts.tv_sec = (time_t)(ms / 1000u);
	ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
	nanosleep(&ts, NULL);
}

/* ------------------------------------------------------------------ */
/* Vector 1-3: SYS                                                     */
/* ------------------------------------------------------------------ */

static void test_sys(void)
{
	fcap_seam_reset();

	/* 1: NULL conf is rejected and leaves the state INVALID. */
	CHECK_EQ(AW_MPI_SYS_SetConf(NULL), ERR_SYS_ILLEGAL_PARAM, "v1 rc");
	CHECK_EQ(ERR_SYS_ILLEGAL_PARAM, (int)0xA0028003u, "v1 value");
	CHECK_EQ(AW_MPI_SYS_Init(), ERR_SYS_NOTREADY, "v1 state unchanged");

	/* 2: an empty temp dir is accepted; a second SetConf after Init is
	 * refused. */
	{
		MPP_SYS_CONF_S c;

		memset(&c, 0, sizeof c);
		c.nAlignWidth = 32;
		CHECK_EQ(AW_MPI_SYS_SetConf(&c), SUCCESS, "v2 rc");
		CHECK_EQ(AW_MPI_SYS_Init(), SUCCESS, "v2 init");
		CHECK_EQ(fcap_seam_isp_init_n, 1, "v2 ISP init called");
		CHECK(fcap_seam_resolver_ptr != NULL, "v2 resolver installed");
		CHECK_EQ(AW_MPI_SYS_SetConf(&c), ERR_SYS_NOT_PERM, "v2 not perm");
		CHECK_EQ(ERR_SYS_NOT_PERM, (int)0xA0028009u, "v2 value");
		CHECK_EQ(AW_MPI_SYS_Init(), SUCCESS, "v2 init idempotent");
		CHECK_EQ(AW_MPI_SYS_Exit(), SUCCESS, "v2 exit");
		CHECK_EQ(fcap_seam_isp_exit_n, 1, "v2 ISP exit called");
	}

	/* 3: media open failure -> FAILURE, state stays CONFIGURED, a later exit
	 * is a no-op. */
	fcap_seam_reset();
	{
		MPP_SYS_CONF_S c;

		memset(&c, 0, sizeof c);
		c.nAlignWidth = 32;
		(void)AW_MPI_SYS_SetConf(&c);
	}
	fcap_seam_fail_open = "/dev/media0";
	CHECK_EQ(AW_MPI_SYS_Init(), FAILURE, "v3 init fails");
	CHECK_EQ(fcap_seam_isp_init_n, 0, "v3 no ISP init");
	fcap_seam_fail_open = NULL;
	CHECK_EQ(AW_MPI_SYS_Exit(), SUCCESS, "v3 exit is a no-op");
}

/* ------------------------------------------------------------------ */
/* Vector 4: CreateVipp                                                */
/* ------------------------------------------------------------------ */

static void test_create_vipp(void)
{
	sys_up();

	CHECK_EQ(AW_MPI_VI_CreateVipp(4), ERR_VI_INVALID_DEVID, "v4 dev 4");
	CHECK_EQ(AW_MPI_VI_CreateVipp(-1), ERR_VI_INVALID_DEVID, "v4 dev -1");
	CHECK_EQ(ERR_VI_INVALID_DEVID, (int)0xA0108001u, "v4 value");

	CHECK_EQ(AW_MPI_VI_CreateVipp(0), SUCCESS, "v4 create");
	CHECK(fcap_seam_resolver_ptr != NULL, "v4 resolver present");
	CHECK_EQ(AW_MPI_VI_CreateVipp(0), SUCCESS, "v4 idempotent");

	/* Count /dev/video0 opens across the seam. */
	{
		int i, n = 0;

		for (i = 0; i < fcap_seam_open_n(); i++)
			if (fcap_seam_open_path(i) != NULL &&
			    strcmp(fcap_seam_open_path(i), "/dev/video0") == 0)
				n++;
		CHECK_EQ(n, 1, "v4 one video0 open");
	}

	(void)AW_MPI_SYS_Exit();
}

/* ------------------------------------------------------------------ */
/* Vector 5-6: attribute round-trip and clamps                         */
/* ------------------------------------------------------------------ */

static void test_vipp_attr(void)
{
	VI_ATTR_S a, out;
	const struct fcap_seam_rec *r;
	struct v4l2_format f;
	struct v4l2_streamparm p;

	sys_up();
	fill_attr(&a, 2304, 1296, V4L2_PIX_FMT_LBC_2_5X, 3, 2, 5, -1);
	fcap_seam_gfmt_w = 2304;
	fcap_seam_gfmt_h = 1296;
	fcap_seam_gfmt_fourcc = V4L2_PIX_FMT_LBC_2_5X;
	CHECK_EQ(AW_MPI_VI_CreateVipp(0), SUCCESS, "v5 create");

	fcap_seam_clear_log();
	CHECK_EQ(AW_MPI_VI_SetVippAttr(0, &a), SUCCESS, "v5 set attr");

	r = fcap_seam_last(VIDIOC_S_FMT);
	CHECK(r != NULL, "v5 S_FMT issued");
	if (r != NULL) {
		memcpy(&f, r->arg, sizeof f);
		CHECK_EQ(f.fmt.pix_mp.width, 2304, "v5 width");
		CHECK_EQ(f.fmt.pix_mp.height, 1296, "v5 height");
		CHECK_EQ(f.fmt.pix_mp.pixelformat, V4L2_PIX_FMT_LBC_2_5X,
			 "v5 fourcc");
	}
	r = fcap_seam_last(VIDIOC_S_PARM);
	CHECK(r != NULL, "v5 S_PARM issued");
	if (r != NULL) {
		memcpy(&p, r->arg, sizeof p);
		CHECK_EQ(p.parm.capture.reserved[1], 0, "v5 wdr clamped to 0");
	}

	/* 6: GetVippAttr returns the driver-negotiated geometry (G_FMT). */
	memset(&out, 0, sizeof out);
	CHECK_EQ(AW_MPI_VI_GetVippAttr(0, &out), SUCCESS, "v6 get attr");
	CHECK_EQ(out.format.width, 2304, "v6 width");
	CHECK_EQ(out.format.height, 1296, "v6 height");
	CHECK_EQ(out.format.pixelformat, V4L2_PIX_FMT_LBC_2_5X, "v6 fourcc");
	CHECK_EQ(out.nbufs, 3, "v6 nbufs");
	CHECK_EQ(out.nplanes, 2, "v6 nplanes");
	CHECK_EQ(out.fps, 20, "v6 fps");
	CHECK_EQ(out.capturemode, 2, "v6 capturemode");
	CHECK_EQ(out.wdr_mode, 0, "v6 wdr clamped");
	CHECK_EQ(out.drop_frame_num, 0, "v6 drop clamped");

	CHECK_EQ(AW_MPI_VI_GetVippAttr(4, &out), ERR_VI_INVALID_DEVID, "v6 bad dev");
	CHECK_EQ(AW_MPI_VI_SetVippAttr(4, &a), ERR_VI_INVALID_DEVID, "v5 bad dev");

	(void)AW_MPI_SYS_Exit();
}

/* ------------------------------------------------------------------ */
/* Vector 8: EnableVipp bring-up order                                 */
/* ------------------------------------------------------------------ */

static void test_enable_vipp(void)
{
	VI_ATTR_S a;
	int i, i_req, i_gfmt, i_qbuf, i_streamon;

	sys_up();
	fill_attr(&a, 640, 360, V4L2_PIX_FMT_LBC_2_5X, 3, 2, 2, 5);
	fcap_seam_plane_len[0] = 138240;
	fcap_seam_plane_len[1] = 1024;
	CHECK_EQ(AW_MPI_VI_CreateVipp(0), SUCCESS, "v8 create");
	CHECK_EQ(AW_MPI_VI_SetVippAttr(0, &a), SUCCESS, "v8 set attr");

	fcap_seam_clear_log();
	CHECK_EQ(AW_MPI_VI_EnableVipp(0), SUCCESS, "v8 enable");

	i_req = req_index(VIDIOC_REQBUFS, 0);
	i_gfmt = req_index(VIDIOC_G_FMT, 0);
	i_qbuf = req_index(VIDIOC_QBUF, 0);
	i_streamon = req_index(VIDIOC_STREAMON, 0);
	CHECK(i_req >= 0, "v8 REQBUFS observed");
	CHECK(i_gfmt >= 0, "v8 G_FMT observed");
	CHECK(i_qbuf >= 0, "v8 QBUF observed");
	CHECK(i_streamon >= 0, "v8 STREAMON observed");
	CHECK(i_req < i_gfmt && i_gfmt < i_qbuf && i_qbuf < i_streamon,
	      "v8 bring-up order");

	{
		struct v4l2_requestbuffers rb;

		memcpy(&rb, fcap_seam_nth(VIDIOC_REQBUFS, 0)->arg, sizeof rb);
		CHECK_EQ(rb.count, 3, "v8 reqbufs count");
	}
	CHECK_EQ(fcap_seam_count(VIDIOC_QBUF), 3, "v8 three QBUF");
	for (i = 0; i < 3; i++) {
		struct v4l2_buffer b;

		memcpy(&b, fcap_seam_nth(VIDIOC_QBUF, i)->arg, sizeof b);
		CHECK_EQ(b.index, i, "v8 qbuf index");
	}
	/* Pool allocation is observable through the plane mmaps: nbufs x
	 * nplanes.  Count them via QUERYBUF (one per buffer). */
	CHECK_EQ(fcap_seam_count(VIDIOC_QUERYBUF), 3, "v8 querybuf per buffer");

	CHECK_EQ(AW_MPI_VI_EnableVipp(0), SUCCESS, "v8 enable idempotent");
	CHECK_EQ(AW_MPI_VI_DisableVipp(0), SUCCESS, "v8 disable");
	(void)AW_MPI_SYS_Exit();
}

/* ------------------------------------------------------------------ */
/* Vector 9-12: GetFrame / ReleaseFrame                                */
/* ------------------------------------------------------------------ */

static void test_frames(void)
{
	VI_ATTR_S a;
	VIDEO_FRAME_INFO_S fi;

	sys_up();
	fill_attr(&a, 640, 360, V4L2_PIX_FMT_LBC_2_5X, 3, 2, 2, 0);
	fcap_seam_gfmt_w = 640;
	fcap_seam_gfmt_h = 360;
	fcap_seam_plane_len[0] = 138240;
	fcap_seam_plane_phy[0] = 0x1234;

	CHECK_EQ(vi_up(0, &a), 0, "v11 vi up");
	CHECK_EQ(AW_MPI_VI_CreateVirChn(0, 0, NULL), SUCCESS, "v11 create chn");
	CHECK_EQ(AW_MPI_VI_EnableVirChn(0, 0), SUCCESS, "v11 enable chn");

	/* 9: nothing queued, timeout 0 -> FAILURE, no block. */
	memset(&fi, 0, sizeof fi);
	CHECK_EQ(AW_MPI_VI_GetFrame(0, 0, &fi, 0), FAILURE, "v9 timeout 0");
	CHECK_EQ(AW_MPI_VI_GetFrame(0, 4, &fi, 0),
		 ERR_VI_INVALID_CHNID, "v9 bad chn");
	CHECK_EQ(AW_MPI_VI_GetFrame(4, 0, &fi, 0),
		 ERR_VI_INVALID_CHNID, "v9 bad dev");

	/* 10: with the clock expired, a 50 ms wait returns FAILURE at once. */
	fcap_seam_set_channel_expire(1);
	CHECK_EQ(AW_MPI_VI_GetFrame(0, 0, &fi, 50), FAILURE, "v10 expired");
	fcap_seam_set_channel_expire(0);

	/* 11: one frame through the capture path. */
	fcap_seam_clear_log();
	fcap_seam_feed(1, 7, 33333, 12, 0);
	memset(&fi, 0, sizeof fi);
	CHECK_EQ(AW_MPI_VI_GetFrame(0, 0, &fi, -1), SUCCESS, "v11 get");
	CHECK_EQ(fi.mId, 1, "v11 mId");
	CHECK_EQ((unsigned long long)fi.VFrame.mpts, 12000000ULL, "v11 mpts");
	CHECK_EQ(fi.VFrame.mFramecnt, 7, "v11 framecnt");
	CHECK_EQ(fi.VFrame.mExposureTime, 33, "v11 exposure");
	CHECK_EQ(fi.VFrame.mStride[0], 138240, "v11 stride is plane length");
	CHECK_EQ(fi.VFrame.mWidth, 640, "v11 width");
	CHECK_EQ(fi.VFrame.mHeight, 360, "v11 height");
	CHECK_EQ(fi.VFrame.mOffsetTop, 0, "v11 off top");
	CHECK_EQ(fi.VFrame.mOffsetBottom, 360, "v11 off bottom");
	CHECK_EQ(fi.VFrame.mOffsetLeft, 0, "v11 off left");
	CHECK_EQ(fi.VFrame.mOffsetRight, 640, "v11 off right");
	CHECK_EQ(fi.VFrame.mPixelFormat, FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_2_5X,
		 "v11 pixel format");

	/* 12: releasing requeues the driver buffer; a second release fails. */
	CHECK_EQ(AW_MPI_VI_ReleaseFrame(0, 0, &fi), SUCCESS, "v12 release");
	{
		const struct fcap_seam_rec *r = fcap_seam_nth(VIDIOC_QBUF, 0);
		struct v4l2_buffer b;

		CHECK(r != NULL, "v12 QBUF observed");
		if (r != NULL) {
			memcpy(&b, r->arg, sizeof b);
			CHECK_EQ(b.index, 1, "v12 QBUF index");
		}
	}
	CHECK_EQ(AW_MPI_VI_ReleaseFrame(0, 0, &fi), FAILURE, "v12 double release");

	/* 13: channel teardown. */
	CHECK_EQ(AW_MPI_VI_DisableVirChn(0, 0), SUCCESS, "v13 disable chn");
	CHECK_EQ(AW_MPI_VI_DestoryVirChn(0, 0), SUCCESS, "v13 destroy chn");
	CHECK_EQ(AW_MPI_VI_DestoryVirChn(0, 0), ERR_VI_UNEXIST, "v13 again");

	(void)AW_MPI_SYS_Exit();
}

/* ------------------------------------------------------------------ */
/* Vector 14: DisableVipp with a surviving channel                     */
/* ------------------------------------------------------------------ */

static void test_disable_vipp_with_channel(void)
{
	VI_ATTR_S a;

	sys_up();
	fill_attr(&a, 640, 360, V4L2_PIX_FMT_LBC_2_5X, 3, 2, 2, 0);
	CHECK_EQ(vi_up(0, &a), 0, "v14 vi up");
	CHECK_EQ(AW_MPI_VI_CreateVirChn(0, 0, NULL), SUCCESS, "v14 create chn");

	CHECK_EQ(AW_MPI_VI_DisableVipp(0), SUCCESS, "v14 disable vipp");
	CHECK_EQ(AW_MPI_VI_DestoryVipp(0), SUCCESS, "v14 destroy vipp");
	(void)AW_MPI_SYS_Exit();
}

/* ------------------------------------------------------------------ */
/* Vector 15: mirror / flip                                            */
/* ------------------------------------------------------------------ */

static void test_orientation(void)
{
	VI_ATTR_S a;

	sys_up();
	fill_attr(&a, 640, 360, V4L2_PIX_FMT_LBC_2_5X, 3, 2, 2, 0);
	CHECK_EQ(AW_MPI_VI_CreateVipp(0), SUCCESS, "v15 create");

	fcap_seam_clear_log();
	CHECK_EQ(AW_MPI_VI_SetVippMirror(0, 1), SUCCESS, "v15 mirror");
	check_sctrl(0, V4L2_CID_HFLIP, 1, "v15 HFLIP=1");
	CHECK_EQ(AW_MPI_VI_SetVippFlip(0, 1), SUCCESS, "v15 flip");
	check_sctrl(1, V4L2_CID_VFLIP, 1, "v15 VFLIP=1");

	{
		int before = fcap_seam_count(VIDIOC_S_CTRL);

		CHECK_EQ(AW_MPI_VI_SetVippMirror(0, 7), SUCCESS, "v15 odd value");
		CHECK_EQ(fcap_seam_count(VIDIOC_S_CTRL), before, "v15 no write");
	}
	CHECK_EQ(AW_MPI_VI_SetVippMirror(9, 1), ERR_VI_INVALID_DEVID, "v15 dev 9");
	CHECK_EQ(AW_MPI_VI_SetVippFlip(-1, 1), ERR_VI_INVALID_DEVID, "v15 dev -1");

	(void)AW_MPI_SYS_Exit();
}

/* ------------------------------------------------------------------ */
/* Vector 16: shutter time                                             */
/* ------------------------------------------------------------------ */

static void test_shutter(void)
{
	VI_ATTR_S a;
	VI_SHUTTIME_CFG_S t;

	sys_up();
	fill_attr(&a, 640, 360, V4L2_PIX_FMT_LBC_2_5X, 3, 2, 2, 0);
	CHECK_EQ(AW_MPI_VI_CreateVipp(0), SUCCESS, "v16 create");
	fcap_seam_sensor_fps = 20;

	/* AUTO: exposure auto on, auto gain on, sensor fps restored. */
	memset(&t, 0, sizeof t);
	t.iTime = 0;
	t.eResetMode = VI_SHUTTIME_RESET_AUTO_DELAY;
	t.eShutterMode = VI_SHUTTIME_MODE_AUTO;
	fcap_seam_clear_log();
	CHECK_EQ(AW_MPI_VI_SetVippShutterTime(0, &t), SUCCESS, "v16 auto rc");
	check_sctrl(0, V4L2_CID_EXPOSURE_AUTO, 0, "v16 auto exposure");
	check_sctrl(1, V4L2_CID_AUTOGAIN, 1, "v16 auto gain");
	check_sensor_fps(20, "v16 fps restored");

	/* NIGHT_VIEW, iTime -5, sensor fps 20: manual exp/gain + fps -5. */
	fcap_seam_ctrl_value = 1000;   /* gain == exposure == 1000 */
	memset(&t, 0, sizeof t);
	t.iTime = -5;
	t.eResetMode = VI_SHUTTIME_RESET_AUTO_DELAY;
	t.eShutterMode = VI_SHUTTIME_MODE_NIGHT_VIEW;
	fcap_seam_clear_log();
	CHECK_EQ(AW_MPI_VI_SetVippShutterTime(0, &t), SUCCESS, "v16 night rc");
	check_sctrl(0, V4L2_CID_EXPOSURE_AUTO, 1, "v16 night manual exposure");
	check_sctrl(1, V4L2_CID_AUTOGAIN, 0, "v16 night manual gain");
	check_sctrl(2, V4L2_CID_EXPOSURE_ABSOLUTE, 5000000, "v16 night exp us");
	check_sctrl(3, V4L2_CID_GAIN, 48, "v16 night gain");
	check_sensor_fps(-5, "v16 fps set to -5");

	/* Already in long exposure and NIGHT_VIEW again -> busy, no writes. */
	{
		int before = fcap_seam_count(VIDIOC_S_CTRL);

		CHECK_EQ(AW_MPI_VI_SetVippShutterTime(0, &t), ERR_VI_BUSY,
			 "v16 busy");
		CHECK_EQ(fcap_seam_count(VIDIOC_S_CTRL), before, "v16 no write");
	}

	/* PREVIEW with iTime 0 -> FAILURE (iTime==0 outside AUTO). */
	t.iTime = 0;
	t.eShutterMode = VI_SHUTTIME_MODE_PREVIEW;
	CHECK_EQ(AW_MPI_VI_SetVippShutterTime(0, &t), FAILURE, "v16 preview 0");

	CHECK_EQ(AW_MPI_VI_SetVippShutterTime(9, &t), ERR_VI_INVALID_DEVID,
		 "v16 bad dev");
	(void)AW_MPI_SYS_Exit();
}

/* ------------------------------------------------------------------ */
/* Vector 7 / 17: virtual-channel state machine                        */
/* ------------------------------------------------------------------ */

static void test_virchn_states(void)
{
	VI_ATTR_S a;

	sys_up();
	fill_attr(&a, 640, 360, V4L2_PIX_FMT_LBC_2_5X, 3, 2, 2, 0);
	CHECK_EQ(AW_MPI_VI_CreateVipp(0), SUCCESS, "v7 create");
	CHECK_EQ(AW_MPI_VI_SetVippAttr(0, &a), SUCCESS, "v7 set attr");

	/* 7: a virchn on a non-enabled vipp is UNEXIST. */
	CHECK_EQ(AW_MPI_VI_CreateVirChn(0, 0, NULL), ERR_VI_UNEXIST, "v7 not enabled");
	CHECK_EQ(ERR_VI_UNEXIST, (int)0xA0108005u, "v7 value");

	CHECK_EQ(AW_MPI_VI_EnableVipp(0), SUCCESS, "v7 enable");
	CHECK_EQ(AW_MPI_VI_CreateVirChn(0, 0, NULL), SUCCESS, "v7 create chn");
	CHECK_EQ(AW_MPI_VI_CreateVirChn(0, 0, NULL), ERR_VI_EXIST, "v7 exist");
	CHECK_EQ(ERR_VI_EXIST, (int)0xA0108004u, "v7 exist value");
	CHECK_EQ(AW_MPI_VI_CreateVirChn(0, 4, NULL), ERR_VI_INVALID_CHNID,
		 "v7 bad chn");

	/* 17: SetVirChnAttr is a no-op returning 0. */
	{
		int before = fcap_seam_log_len();

		CHECK_EQ(AW_MPI_VI_SetVirChnAttr(0, 0, NULL), 0, "v17 no-op");
		CHECK_EQ(fcap_seam_log_len(), before, "v17 no side effect");
		CHECK_EQ(AW_MPI_VI_SetVirChnAttr(0, 4, NULL),
			 ERR_VI_INVALID_CHNID, "v17 bad chn");
	}

	(void)AW_MPI_SYS_Exit();
}

/* ------------------------------------------------------------------ */
/* Vector 18: FIFO depth and order                                     */
/* ------------------------------------------------------------------ */

static void test_fifo_depth(void)
{
	VI_ATTR_S a;
	VIDEO_FRAME_INFO_S fi;
	int i;

	sys_up();
	/* The vector "push 21 frames without releasing" needs 21 driver buffers
	 * so the capture path can hold them all in flight. */
	fill_attr(&a, 640, 360, V4L2_PIX_FMT_LBC_2_5X, 21, 2, 2, 0);
	fcap_seam_gfmt_w = 640;
	fcap_seam_gfmt_h = 360;
	fcap_seam_plane_len[0] = 4096;

	CHECK_EQ(vi_up(0, &a), 0, "v18 vi up");
	CHECK_EQ(AW_MPI_VI_CreateVirChn(0, 0, NULL), SUCCESS, "v18 create chn");
	CHECK_EQ(AW_MPI_VI_EnableVirChn(0, 0), SUCCESS, "v18 enable chn");

	for (i = 0; i < 21; i++)
		fcap_seam_feed((unsigned int)i, (unsigned int)i, 1000, 100, i);

	/* Wait for the worker to drain the feed, then let it settle. */
	for (i = 0; i < 200 && fcap_seam_pending() > 0; i++)
		sleep_ms(1);
	sleep_ms(50);

	/* The FIFO holds 20; the oldest came first and the 21st was dropped. */
	for (i = 0; i < 20; i++) {
		memset(&fi, 0, sizeof fi);
		CHECK_EQ(AW_MPI_VI_GetFrame(0, 0, &fi, 0), SUCCESS, "v18 get");
		CHECK_EQ(fi.VFrame.mFramecnt, i, "v18 FIFO order");
		CHECK_EQ(fi.mId, i, "v18 FIFO index");
	}
	memset(&fi, 0, sizeof fi);
	CHECK_EQ(AW_MPI_VI_GetFrame(0, 0, &fi, 0), FAILURE, "v18 empty after 20");

	(void)AW_MPI_SYS_Exit();
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

extern int fcap_abi_model_checks(void);

static void test_abi(void)
{
	g_checks += fcap_abi_model_checks();

	CHECK_EQ(SUCCESS, 0, "SUCCESS");
	CHECK_EQ(FAILURE, -1, "FAILURE");
	CHECK_EQ(VI_VIPP_NUM_MAX, 4, "VI_VIPP_NUM_MAX");
	CHECK_EQ(VI_VIRCHN_NUM_MAX, 4, "VI_VIRCHN_NUM_MAX");
	CHECK_EQ(VI_FIFO_LEVEL, 20, "VI_FIFO_LEVEL");
	CHECK_EQ(V4L2_CID_HFLIP, 0x00980914, "V4L2_CID_HFLIP");
	CHECK_EQ(V4L2_CID_VFLIP, 0x00980915, "V4L2_CID_VFLIP");
	CHECK_EQ(V4L2_CID_EXPOSURE_AUTO, 0x009a0901, "V4L2_CID_EXPOSURE_AUTO");
	CHECK_EQ(V4L2_CID_AUTOGAIN, 0x00980912, "V4L2_CID_AUTOGAIN");
	CHECK_EQ(V4L2_CID_EXPOSURE_ABSOLUTE, 0x009a0902, "EXPOSURE_ABSOLUTE");
	CHECK_EQ(V4L2_CID_GAIN, 0x00980913, "V4L2_CID_GAIN");
}

int main(void)
{
	test_abi();
	test_sys();
	test_create_vipp();
	test_vipp_attr();
	test_enable_vipp();
	test_frames();
	test_disable_vipp_with_channel();
	test_orientation();
	test_shutter();
	test_virchn_states();
	test_fifo_depth();

	printf("fcap: %d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
