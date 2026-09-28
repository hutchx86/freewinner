/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_bitmap.c - host tests for the bitmap size helper (spec
 * media_utils/bitmap.md 2/4), checked against a 32-bit unsigned reference. */
#include "utils/bitmap.h"

#include <stddef.h>
#include <stdint.h>
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

/* Silence the unit's diagnostic on the deliberate unsupported formats. */
void media_utils_log_error(const char *reason, unsigned int detail)
{
	(void)reason;
	(void)detail;
}

/* ------------------------------------------------------------------ */
/* Fixtures and independent reference                                  */
/* ------------------------------------------------------------------ */

static BITMAP_S make_bitmap(unsigned int fmt, unsigned int w, unsigned int h)
{
	BITMAP_S b;

	memset(&b, 0, sizeof(b));
	b.mPixelFormat = (fwm_pixel_format_e)fmt;
	b.mWidth = w;
	b.mHeight = h;
	b.mpData = (void *)0xDEADBEEFu;
	return b;
}

/* 32-bit reference: explicit uint32_t so it wraps like the target instead of
 * borrowing host 64-bit arithmetic. */
static int ref_size(unsigned int fmt, unsigned int w, unsigned int h)
{
	uint32_t p = (uint32_t)w * (uint32_t)h;

	switch (fmt) {
	case FWM_MM_PIXEL_FORMAT_RGB_1555:
		return (int)(p * 2u);
	case FWM_MM_PIXEL_FORMAT_RGB_8888:
		return (int)(p * 4u);
	default:
		return 0;
	}
}

/* ------------------------------------------------------------------ */
/* ABI static facts                                                    */
/* ------------------------------------------------------------------ */

static void test_abi(void)
{
	CHECK_EQ(sizeof(fwm_pixel_format_e), 4);
	CHECK_EQ(offsetof(BITMAP_S, mPixelFormat), 0);
	CHECK_EQ(offsetof(BITMAP_S, mWidth), 4);
	CHECK_EQ(offsetof(BITMAP_S, mHeight), 8);

#if __SIZEOF_POINTER__ == 4
	CHECK_EQ(sizeof(BITMAP_S), 16);
	CHECK_EQ(offsetof(BITMAP_S, mpData), 12);
#endif

	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RGB_1555, 8);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RGB_8888, 10);
}

/* ------------------------------------------------------------------ */
/* Behaviour                                                           */
/* ------------------------------------------------------------------ */

static void test_null(void)
{
	CHECK_EQ(BITMAP_S_GetdataSize(NULL), 0);
}

static void test_formats(void)
{
	static const unsigned int w[] = { 0u,  1u,      2u,      13u,   16u,
					  640u, 1920u,  1920u,   1u,    100000u,
					  65536u, 0x80000000u, 0x40000000u };
	static const unsigned int h[] = { 0u,  1u,   2u,      7u,    16u,
					  480u, 1080u, 1u,      1080u, 100000u,
					  65536u, 1u,   1u };
	static const unsigned int fmt[] = { FWM_MM_PIXEL_FORMAT_RGB_1555,
					    FWM_MM_PIXEL_FORMAT_RGB_8888 };
	unsigned int fi;
	unsigned int di;

	for (fi = 0; fi < sizeof(fmt) / sizeof(fmt[0]); fi++) {
		for (di = 0; di < sizeof(w) / sizeof(w[0]); di++) {
			BITMAP_S b = make_bitmap(fmt[fi], w[di], h[di]);

			CHECK_EQ(BITMAP_S_GetdataSize(&b),
				 ref_size(fmt[fi], w[di], h[di]));
		}
	}
}

static void test_unsupported(void)
{
	static const unsigned int bad[] = {
		FWM_MM_PIXEL_FORMAT_RGB_1BPP,
		FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420,
		FWM_MM_PIXEL_FORMAT_YUV_PLANAR_420,
		FWM_MM_PIXEL_FORMAT_YUYV_PACKAGE_422,
		FWM_MM_PIXEL_FORMAT_RAW_SRGGB8,
		FWM_MM_PIXEL_FORMAT_BUTT,
		0x100u,
		0xFFFFFFFFu,
	};
	unsigned int i;

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		BITMAP_S b = make_bitmap(bad[i], 640, 480);

		CHECK_EQ(BITMAP_S_GetdataSize(&b), 0);
	}

	/* Zero dimensions are not an error for a supported format. */
	{
		BITMAP_S z0 = make_bitmap(FWM_MM_PIXEL_FORMAT_RGB_1555, 0, 0);
		BITMAP_S z1 = make_bitmap(FWM_MM_PIXEL_FORMAT_RGB_8888, 1, 0);
		BITMAP_S z2 = make_bitmap(FWM_MM_PIXEL_FORMAT_RGB_8888, 0, 1080);

		CHECK_EQ(BITMAP_S_GetdataSize(&z0), 0);
		CHECK_EQ(BITMAP_S_GetdataSize(&z1), 0);
		CHECK_EQ(BITMAP_S_GetdataSize(&z2), 0);
	}
}

static void test_vectors(void)
{
	BITMAP_S b;

	/* Hand-computed pins. */
	b = make_bitmap(FWM_MM_PIXEL_FORMAT_RGB_1555, 640, 480);
	CHECK_EQ(BITMAP_S_GetdataSize(&b), 614400);
	b = make_bitmap(FWM_MM_PIXEL_FORMAT_RGB_8888, 640, 480);
	CHECK_EQ(BITMAP_S_GetdataSize(&b), 1228800);
	b = make_bitmap(FWM_MM_PIXEL_FORMAT_RGB_1555, 1920, 1080);
	CHECK_EQ(BITMAP_S_GetdataSize(&b), 4147200);
	b = make_bitmap(FWM_MM_PIXEL_FORMAT_RGB_8888, 1920, 1080);
	CHECK_EQ(BITMAP_S_GetdataSize(&b), 8294400);

	/* Overflow-width cases: the product wraps in 32-bit unsigned. */
	b = make_bitmap(FWM_MM_PIXEL_FORMAT_RGB_8888, 0x40000000u, 1);
	CHECK_EQ(BITMAP_S_GetdataSize(&b), 0);
	b = make_bitmap(FWM_MM_PIXEL_FORMAT_RGB_1555, 0x80000000u, 1);
	CHECK_EQ(BITMAP_S_GetdataSize(&b), 0);
	b = make_bitmap(FWM_MM_PIXEL_FORMAT_RGB_8888, 0xFFFFFFFFu, 2);
	CHECK_EQ(BITMAP_S_GetdataSize(&b),
		 ref_size(FWM_MM_PIXEL_FORMAT_RGB_8888, 0xFFFFFFFFu, 2));
	b = make_bitmap(FWM_MM_PIXEL_FORMAT_RGB_1555, 100000, 100000);
	CHECK_EQ(BITMAP_S_GetdataSize(&b),
		 ref_size(FWM_MM_PIXEL_FORMAT_RGB_1555, 100000, 100000));
	b = make_bitmap(FWM_MM_PIXEL_FORMAT_RGB_8888, 100000, 100000);
	CHECK_EQ(BITMAP_S_GetdataSize(&b),
		 ref_size(FWM_MM_PIXEL_FORMAT_RGB_8888, 100000, 100000));
}

int main(void)
{
	test_abi();
	test_null();
	test_formats();
	test_unsupported();
	test_vectors();

	if (fails) {
		printf("test_bitmap: %d check(s) failed\n", fails);
		return 1;
	}
	printf("test_bitmap: %d checks, 0 failures\n", checks);
	return 0;
}
