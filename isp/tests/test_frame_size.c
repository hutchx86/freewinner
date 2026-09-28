/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_frame_size.c - host tests for the frame-size unit (spec
 * media_utils/frame_size.md 3-5): full format x size x stride matrix. */
#include "utils/frame_size.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int checks;
static int fails;

#define CHECK(cond)                                                       \
	do {                                                              \
		checks++;                                                 \
		if (!(cond)) {                                            \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__,    \
			       #cond);                                    \
			fails++;                                          \
		}                                                         \
	} while (0)

#define CHECK_EQ(a, b)                                                    \
	do {                                                              \
		long va_ = (long)(a);                                     \
		long vb_ = (long)(b);                                     \
		checks++;                                                 \
		if (va_ != vb_) {                                         \
			printf("FAIL %s:%d: %s=%ld expected %s=%ld\n",    \
			       __FILE__, __LINE__, #a, va_, #b, vb_);     \
			fails++;                                          \
		}                                                         \
	} while (0)

/* Silence the unit's diagnostics for the many deliberate rejections. */
void media_utils_log_error(const char *reason, unsigned int detail)
{
	(void)reason;
	(void)detail;
}

/* ------------------------------------------------------------------ */
/* Fixtures and independent reference                                  */
/* ------------------------------------------------------------------ */

#define SENTINEL 0xA5A5A5A5

static VIDEO_FRAME_INFO_S make_frame(unsigned int fmt, unsigned int w,
				     unsigned int h, const unsigned int st[3])
{
	VIDEO_FRAME_INFO_S f;

	memset(&f, 0, sizeof(f));
	f.VFrame.mWidth = w;
	f.VFrame.mHeight = h;
	f.VFrame.mPixelFormat = (fwm_pixel_format_e)fmt;
	f.VFrame.mStride[0] = st[0];
	f.VFrame.mStride[1] = st[1];
	f.VFrame.mStride[2] = st[2];
	return f;
}

static int is_aw(unsigned int fmt)
{
	return fmt >= FWM_MM_PIXEL_FORMAT_YUV_AW_AFBC &&
	       fmt <= FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_1_0X;
}

/* 32-bit reference: 1 and the plane sizes if the format is handled, else 0;
 * explicit uint32_t so it does not borrow host 64-bit wraparound. */
static int ref_sizes(unsigned int fmt, unsigned int w, unsigned int h,
		     const unsigned int st[3], int *y, int *u, int *v)
{
	uint32_t p = (uint32_t)w * (uint32_t)h;

	if (fmt == FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420 ||
	    fmt == FWM_MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420) {
		*y = (int)p;
		*u = (int)(p / 2u);
		*v = 0;
		return 1;
	}
	if (fmt == FWM_MM_PIXEL_FORMAT_YUV_PLANAR_420 ||
	    fmt == FWM_MM_PIXEL_FORMAT_YVU_PLANAR_420) {
		*y = (int)p;
		*u = (int)(p / 4u);
		*v = (int)(p / 4u);
		return 1;
	}
	if (is_aw(fmt)) {
		*y = (int)st[0];
		*u = (int)st[1];
		*v = (int)st[2];
		return 1;
	}
	if (fmt >= FWM_MM_PIXEL_FORMAT_RAW_SBGGR8 &&
	    fmt <= FWM_MM_PIXEL_FORMAT_RAW_SRGGB8) {
		*y = (int)p;
		*u = 0;
		*v = 0;
		return 1;
	}
	if (fmt >= FWM_MM_PIXEL_FORMAT_RAW_SBGGR10 &&
	    fmt <= FWM_MM_PIXEL_FORMAT_RAW_SRGGB10) {
		*y = (int)((p * 10u) / 8u);
		*u = 0;
		*v = 0;
		return 1;
	}
	if (fmt >= FWM_MM_PIXEL_FORMAT_RAW_SBGGR12 &&
	    fmt <= FWM_MM_PIXEL_FORMAT_RAW_SRGGB12) {
		*y = (int)((p * 12u) / 8u);
		*u = 0;
		*v = 0;
		return 1;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* ABI static facts                                                    */
/* ------------------------------------------------------------------ */

static void test_abi(void)
{
	CHECK_EQ(sizeof(fwm_pixel_format_e), 4);
	CHECK_EQ(sizeof(VideoFrameBufferSizeInfo), 12);
	CHECK_EQ(offsetof(VideoFrameBufferSizeInfo, mYSize), 0);
	CHECK_EQ(offsetof(VideoFrameBufferSizeInfo, mUSize), 4);
	CHECK_EQ(offsetof(VideoFrameBufferSizeInfo, mVSize), 8);
	CHECK_EQ(sizeof(VIDEO_FRAME_BUFFER_SIZE_INFO),
		 sizeof(VideoFrameBufferSizeInfo));

#if __SIZEOF_POINTER__ == 4
	CHECK_EQ(sizeof(struct list_head), 8);
	CHECK_EQ(sizeof(BITMAP_S), 4 * 4);
	CHECK_EQ(sizeof(VIDEO_FRAME_S), 144);
	CHECK_EQ(offsetof(VIDEO_FRAME_S, mPixelFormat), 12);
	CHECK_EQ(offsetof(VIDEO_FRAME_S, mPhyAddr), 24);
	CHECK_EQ(offsetof(VIDEO_FRAME_S, mpVirAddr), 36);
	CHECK_EQ(offsetof(VIDEO_FRAME_S, mStride), 48);
	CHECK_EQ(offsetof(VIDEO_FRAME_S, mpts), 104);
	CHECK_EQ(offsetof(VIDEO_FRAME_S, mFrmFlag), 136);
	CHECK_EQ(sizeof(VIDEO_FRAME_INFO_S), 152);
	CHECK_EQ(offsetof(VIDEO_FRAME_INFO_S, mId), 144);
	CHECK_EQ(sizeof(VideoFrameListInfo), 160);
	CHECK_EQ(offsetof(VideoFrameListInfo, mList), 152);
	CHECK_EQ(sizeof(VideoBufferManager), 140);
	CHECK_EQ(offsetof(VideoBufferManager, mUsingFrmList), 16);
	CHECK_EQ(offsetof(VideoBufferManager, mFrmListLock), 24);
	CHECK_EQ(offsetof(VideoBufferManager, mCondUsingFrmEmpty), 48);
	CHECK_EQ(offsetof(VideoBufferManager, mFrameNodeNum), 96);
	CHECK_EQ(offsetof(VideoBufferManager, mbWaitUsingFrmEmptyFlag), 100);
	CHECK_EQ(offsetof(VideoBufferManager, mOps), 104);
	CHECK_EQ(sizeof(cdx_sem_t), 76);
	CHECK_EQ(offsetof(cdx_sem_t, semval), 72);
#endif
}

/* ------------------------------------------------------------------ */
/* Full matrix                                                         */
/* ------------------------------------------------------------------ */

static const unsigned int g_handled[] = {
	FWM_MM_PIXEL_FORMAT_YUV_PLANAR_420,
	FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420,
	FWM_MM_PIXEL_FORMAT_YVU_PLANAR_420,
	FWM_MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420,
	FWM_MM_PIXEL_FORMAT_YUV_AW_AFBC,
	FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_2_0X,
	FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_2_5X,
	FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_1_0X,
	FWM_MM_PIXEL_FORMAT_RAW_SBGGR8,
	FWM_MM_PIXEL_FORMAT_RAW_SGBRG8,
	FWM_MM_PIXEL_FORMAT_RAW_SGRBG8,
	FWM_MM_PIXEL_FORMAT_RAW_SRGGB8,
	FWM_MM_PIXEL_FORMAT_RAW_SBGGR10,
	FWM_MM_PIXEL_FORMAT_RAW_SGBRG10,
	FWM_MM_PIXEL_FORMAT_RAW_SGRBG10,
	FWM_MM_PIXEL_FORMAT_RAW_SRGGB10,
	FWM_MM_PIXEL_FORMAT_RAW_SBGGR12,
	FWM_MM_PIXEL_FORMAT_RAW_SGBRG12,
	FWM_MM_PIXEL_FORMAT_RAW_SGRBG12,
	FWM_MM_PIXEL_FORMAT_RAW_SRGGB12,
};

static const unsigned int g_rejected[] = {
	FWM_MM_PIXEL_FORMAT_RGB_1BPP,	/* 0  */
	12,				/* RGB bayer family, unhandled   */
	16,				/* RGB bayer family, unhandled   */
	21,				/* unhandled YUV                  */
	FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_422,	/* 22, 422 not 420     */
	24,				/* unhandled YUV                  */
	29,				/* unhandled YUV                  */
	FWM_MM_PIXEL_FORMAT_YVU_SEMIPLANAR_422,	/* 31, 422 not 420     */
	FWM_MM_PIXEL_FORMAT_BUTT,		/* 49                             */
	0x100u,
	0xFFFFFFFFu,
};

struct dim {
	unsigned int w;
	unsigned int h;
};

static const struct dim g_dims[] = {
	{ 0, 0 },	 { 1, 1 },	  { 2, 2 },	    { 13, 7 },
	{ 16, 16 },	 { 640, 480 },	  { 1920, 1080 },   { 2592, 1520 },
	{ 1920, 1 },	 { 1, 1080 },	  { 65536, 65536 }, { 100000, 100000 },
};

static const unsigned int g_aw_strides[3][3] = {
	{ 0x11111111u, 0x22222222u, 0x33333333u },
	{ 0x00000000u, 0x00000000u, 0x00000000u },
	{ 0xFFFFFFFFu, 0x80000000u, 0x00000001u },
};
static const unsigned int g_plain_strides[3] = {
	0xDEADBEEFu, 0xCAFEBABEu, 0x12345678u
};

static void test_matrix(void)
{
	VideoFrameBufferSizeInfo out;
	unsigned int fi;
	unsigned int di;
	unsigned int si;

	for (fi = 0; fi < sizeof(g_handled) / sizeof(g_handled[0]); fi++) {
		unsigned int fmt = g_handled[fi];
		unsigned int stride_count = is_aw(fmt) ? 3u : 1u;

		for (si = 0; si < stride_count; si++) {
			const unsigned int *st = is_aw(fmt)
							 ? g_aw_strides[si]
							 : g_plain_strides;
			for (di = 0; di < sizeof(g_dims) / sizeof(g_dims[0]);
			     di++) {
				VIDEO_FRAME_INFO_S f = make_frame(
					fmt, g_dims[di].w, g_dims[di].h, st);
				int ey = 0;
				int eu = 0;
				int ev = 0;

				ref_sizes(fmt, g_dims[di].w, g_dims[di].h, st,
					  &ey, &eu, &ev);
				out.mYSize = SENTINEL;
				out.mUSize = SENTINEL;
				out.mVSize = SENTINEL;
				CHECK_EQ(getVideoFrameBufferSizeInfo(&f, &out),
					 SUCCESS);
				CHECK_EQ(out.mYSize, ey);
				CHECK_EQ(out.mUSize, eu);
				CHECK_EQ(out.mVSize, ev);
			}
		}
	}

	for (fi = 0; fi < sizeof(g_rejected) / sizeof(g_rejected[0]); fi++) {
		for (di = 0; di < sizeof(g_dims) / sizeof(g_dims[0]); di++) {
			VIDEO_FRAME_INFO_S f = make_frame(g_rejected[fi],
							  g_dims[di].w,
							  g_dims[di].h,
							  g_plain_strides);

			out.mYSize = SENTINEL;
			out.mUSize = SENTINEL;
			out.mVSize = SENTINEL;
			CHECK_EQ(getVideoFrameBufferSizeInfo(&f, &out), FAILURE);
			CHECK_EQ(out.mYSize, (int)SENTINEL);
			CHECK_EQ(out.mUSize, (int)SENTINEL);
			CHECK_EQ(out.mVSize, (int)SENTINEL);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Pinned vectors                                                      */
/* ------------------------------------------------------------------ */

static void expect_vec(unsigned int fmt, unsigned int w, unsigned int h,
		       const unsigned int st[3], int ey, int eu, int ev)
{
	VIDEO_FRAME_INFO_S f = make_frame(fmt, w, h, st);
	VideoFrameBufferSizeInfo out;

	out.mYSize = SENTINEL;
	out.mUSize = SENTINEL;
	out.mVSize = SENTINEL;
	CHECK_EQ(getVideoFrameBufferSizeInfo(&f, &out), SUCCESS);
	CHECK_EQ(out.mYSize, ey);
	CHECK_EQ(out.mUSize, eu);
	CHECK_EQ(out.mVSize, ev);
}

static void test_vectors(void)
{
	const unsigned int st0[3] = { 0, 0, 0 };
	const unsigned int st1[3] = { 0x11111111u, 0x22222222u, 0x33333333u };
	const unsigned int st2[3] = { 0xFFFFFFFFu, 0x80000000u, 0x00000001u };

	/* 640x480 semi-planar / planar. */
	expect_vec(FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420, 640, 480, st1,
		   307200, 153600, 0);
	expect_vec(FWM_MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420, 640, 480, st1,
		   307200, 153600, 0);
	expect_vec(FWM_MM_PIXEL_FORMAT_YUV_PLANAR_420, 640, 480, st1,
		   307200, 76800, 76800);
	expect_vec(FWM_MM_PIXEL_FORMAT_YVU_PLANAR_420, 640, 480, st1,
		   307200, 76800, 76800);

	/* Odd dimensions pin integer-division truncation. */
	expect_vec(FWM_MM_PIXEL_FORMAT_YUV_PLANAR_420, 13, 7, st0, 91, 22, 22);
	expect_vec(FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420, 13, 7, st0, 91, 45, 0);

	/* Zero dimension is not an error for a handled format. */
	expect_vec(FWM_MM_PIXEL_FORMAT_YUV_PLANAR_420, 0, 0, st0, 0, 0, 0);
	expect_vec(FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420, 1, 0, st0, 0, 0, 0);

	/* RAW 8/10/12. */
	expect_vec(FWM_MM_PIXEL_FORMAT_RAW_SRGGB8, 640, 480, st1,
		   307200, 0, 0);
	expect_vec(FWM_MM_PIXEL_FORMAT_RAW_SRGGB10, 13, 7, st0, 113, 0, 0);
	expect_vec(FWM_MM_PIXEL_FORMAT_RAW_SRGGB12, 13, 7, st0, 136, 0, 0);

	/* AW formats forward the stride triple, ignoring W/H. */
	expect_vec(FWM_MM_PIXEL_FORMAT_YUV_AW_AFBC, 640, 480, st1,
		   0x11111111, 0x22222222, 0x33333333);
	expect_vec(FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_2_0X, 0, 0, st0, 0, 0, 0);
	expect_vec(FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_2_5X, 1, 1, st2,
		   (int)0xFFFFFFFF, (int)0x80000000, 1);
	expect_vec(FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_1_0X, 100000, 100000, st2,
		   (int)0xFFFFFFFF, (int)0x80000000, 1);

	/* 32-bit truncation of the products. */
	expect_vec(FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420, 65536, 65536, st0,
		   0, 0, 0);
	expect_vec(FWM_MM_PIXEL_FORMAT_YUV_PLANAR_420, 100000, 100000, st0,
		   1410065408, 352516352, 352516352);
	expect_vec(FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420, 100000, 100000, st0,
		   1410065408, 705032704, 0);
	expect_vec(FWM_MM_PIXEL_FORMAT_RAW_SRGGB10, 100000, 100000, st0,
		   151969024, 0, 0);
	expect_vec(FWM_MM_PIXEL_FORMAT_RAW_SRGGB12, 100000, 100000, st0,
		   504485376, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Null and edge behaviour                                             */
/* ------------------------------------------------------------------ */

static void test_null_and_edges(void)
{
	VIDEO_FRAME_INFO_S f = make_frame(FWM_MM_PIXEL_FORMAT_YUV_PLANAR_420,
					  16, 16, g_plain_strides);
	VideoFrameBufferSizeInfo out;

	out.mYSize = SENTINEL;
	out.mUSize = SENTINEL;
	out.mVSize = SENTINEL;
	CHECK_EQ(getVideoFrameBufferSizeInfo(NULL, &out), FAILURE);
	CHECK_EQ(out.mYSize, (int)SENTINEL);
	CHECK_EQ(out.mUSize, (int)SENTINEL);
	CHECK_EQ(out.mVSize, (int)SENTINEL);

	CHECK_EQ(getVideoFrameBufferSizeInfo(&f, NULL), FAILURE);
	CHECK_EQ(getVideoFrameBufferSizeInfo(NULL, NULL), FAILURE);

	/* mStride is ignored for non-AW groups: a rejected format leaves the output
	 * untouched even with large strides. */
	f = make_frame(FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_422, 16, 16,
		       g_plain_strides);
	out.mYSize = SENTINEL;
	out.mUSize = SENTINEL;
	out.mVSize = SENTINEL;
	CHECK_EQ(getVideoFrameBufferSizeInfo(&f, &out), FAILURE);
	CHECK_EQ(out.mYSize, (int)SENTINEL);
	CHECK_EQ(out.mUSize, (int)SENTINEL);
	CHECK_EQ(out.mVSize, (int)SENTINEL);
}

int main(void)
{
	test_abi();
	test_matrix();
	test_vectors();
	test_null_and_edges();

	if (fails) {
		printf("test_frame_size: %d check(s) failed\n", fails);
		return 1;
	}
	printf("test_frame_size: %d checks, 0 failures\n", checks);
	return 0;
}
