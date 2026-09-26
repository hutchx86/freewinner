// SPDX-License-Identifier: AGPL-3.0-only
/* Copyright (C) 2026 freewinner contributors */
/* Host test for the live media-utility entry points (spec 24 §5). */
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "utils/bitmap.h"
#include "utils/frame_size.h"
#include "utils/media_helpers.h"

static int failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			failures++;                                          \
			fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__,        \
				__LINE__, #cond);                            \
		}                                                            \
	} while (0)

#define A5 ((int)0xA5A5A5A5u)

/* Set the 4-byte enum field from a raw number without naming an enumerator. */
static void set_fmt(void *field, unsigned int v)
{
	memcpy(field, &v, sizeof(v));
}

static void test_layout(void)
{
	CHECK(offsetof(BITMAP_S, mWidth) == 4);
	CHECK(offsetof(BITMAP_S, mHeight) == 8);
	CHECK(offsetof(VIDEO_FRAME_S, mPixelFormat) == 12);
	CHECK(offsetof(VIDEO_FRAME_S, mStride) == 48 ||
	      sizeof(void *) != 4);
	CHECK(sizeof(VideoFrameBufferSizeInfo) == 12);
	CHECK(sizeof(MPP_CHN_S) == 12);
	CHECK(sizeof(((BITMAP_S *)0)->mPixelFormat) == 4);
	CHECK(sizeof(((MPP_CHN_S *)0)->mModId) == 4);
	if (sizeof(void *) == 4) {
		CHECK(sizeof(BITMAP_S) == 16);
		CHECK(sizeof(VIDEO_FRAME_S) == 144);
		CHECK(sizeof(VIDEO_FRAME_INFO_S) == 152);
		CHECK(offsetof(VIDEO_FRAME_INFO_S, mId) == 144);
	}
}

static int bmp(unsigned int fmt, unsigned int w, unsigned int h)
{
	BITMAP_S b;
	set_fmt(&b.mPixelFormat, fmt);
	b.mWidth = w;
	b.mHeight = h;
	b.mpData = (void *)(size_t)0x1; /* bogus, must not be dereferenced */
	return BITMAP_S_GetdataSize(&b);
}

static void test_bitmap(void)
{
	static const unsigned int others[] = { 0x100u, 0x7FFFFFFFu,
					       0x80000000u, 0xFFFFFFFFu };
	unsigned int f, i;

	CHECK(BITMAP_S_GetdataSize(NULL) == 0);
	CHECK(bmp(8, 3, 5) == 30);
	CHECK(bmp(10, 3, 5) == 60);
	CHECK(bmp(8, 0, 5) == 0);
	CHECK(bmp(10, 7, 0) == 0);
	CHECK(bmp(8, 0x80000000u, 1) == 0);
	CHECK(bmp(8, 100000, 100000) == -1474836480);
	CHECK(bmp(10, 100000, 100000) == 1345294336);
	CHECK(bmp(8, 0x40000000u, 1) == INT_MIN);
	CHECK(bmp(8, 0xFFFFFFFFu, 2) == -4);
	CHECK(bmp(8, 65536, 65536) == 0);
	CHECK(bmp(10, 1920, 1080) == 1920 * 1080 * 4);
	for (f = 0; f <= 80; f++)
		if (f != 8 && f != 10)
			CHECK(bmp(f, 13, 7) == 0);
	for (i = 0; i < sizeof(others) / sizeof(others[0]); i++)
		CHECK(bmp(others[i], 13, 7) == 0);
}

static int fsz(unsigned int fmt, unsigned int w, unsigned int h,
	       const unsigned int stride[3], VideoFrameBufferSizeInfo *out)
{
	VIDEO_FRAME_INFO_S in, copy;
	int rc;

	memset(&in, 0x3C, sizeof(in));
	set_fmt(&in.VFrame.mPixelFormat, fmt);
	in.VFrame.mWidth = w;
	in.VFrame.mHeight = h;
	in.VFrame.mStride[0] = stride[0];
	in.VFrame.mStride[1] = stride[1];
	in.VFrame.mStride[2] = stride[2];
	copy = in;
	out->mYSize = out->mUSize = out->mVSize = A5;
	rc = getVideoFrameBufferSizeInfo(&in, out);
	CHECK(memcmp(&in, &copy, sizeof(in)) == 0); /* never writes pFrame */
	return rc;
}

#define EXPECT(o, y, u, v) \
	CHECK((o).mYSize == (int)(y) && (o).mUSize == (int)(u) && \
	      (o).mVSize == (int)(v))

static void test_frame_size(void)
{
	static const unsigned int strides[4][3] = {
		{ 0x11111111u, 0x22222222u, 0x33333333u },
		{ 0, 0, 0 },
		{ 0xFFFFFFFFu, 0x80000000u, 1 },
		{ 0xDEADBEEFu, 0xCAFEBABEu, 0x12345678u },
	};
	static const struct {
		unsigned int w, h, a, a2, a4, r10, r12;
	} rows[] = {
		{ 0, 0, 0, 0, 0, 0, 0 },
		{ 1, 1, 1, 0, 0, 1, 1 },
		{ 2, 2, 4, 2, 1, 5, 6 },
		{ 13, 7, 91, 45, 22, 113, 136 },
		{ 1920, 1080, 2073600, 1036800, 518400, 2592000, 3110400 },
		{ 2592, 1520, 3939840, 1969920, 984960, 4924800, 5909760 },
		{ 65536, 65536, 0, 0, 0, 0, 0 },
		{ 100000, 100000, 1410065408u, 705032704u, 352516352u,
		  151969024u, 504485376u },
		/* beyond the judges: wrapped area >= 2^31 (24 §5) */
		{ 0x80000000u, 1, 0x80000000u, 0x40000000u, 0x20000000u,
		  0u, 0u },
	};
	static const unsigned int rejected[] = { 0, 8, 10, 12, 16, 21, 22, 24,
						 26, 29, 31, 49, 50, 0x100u,
						 0xFFFFFFFFu };
	VideoFrameBufferSizeInfo o;
	unsigned int r, s, f, i;

	for (r = 0; r < sizeof(rows) / sizeof(rows[0]); r++) {
		for (s = 0; s < 4; s++) {
			const unsigned int *st = strides[s];
			unsigned int w = rows[r].w, h = rows[r].h;

			CHECK(fsz(23, w, h, st, &o) == SUCCESS);
			EXPECT(o, rows[r].a, rows[r].a2, 0);
			CHECK(fsz(32, w, h, st, &o) == SUCCESS);
			EXPECT(o, rows[r].a, rows[r].a2, 0);
			CHECK(fsz(20, w, h, st, &o) == SUCCESS);
			EXPECT(o, rows[r].a, rows[r].a4, rows[r].a4);
			CHECK(fsz(30, w, h, st, &o) == SUCCESS);
			EXPECT(o, rows[r].a, rows[r].a4, rows[r].a4);
			for (f = 33; f <= 36; f++) {
				CHECK(fsz(f, w, h, st, &o) == SUCCESS);
				EXPECT(o, st[0], st[1], st[2]);
			}
			for (f = 37; f <= 40; f++) {
				CHECK(fsz(f, w, h, st, &o) == SUCCESS);
				EXPECT(o, rows[r].a, 0, 0);
			}
			for (f = 41; f <= 44; f++) {
				CHECK(fsz(f, w, h, st, &o) == SUCCESS);
				EXPECT(o, rows[r].r10, 0, 0);
			}
			for (f = 45; f <= 48; f++) {
				CHECK(fsz(f, w, h, st, &o) == SUCCESS);
				EXPECT(o, rows[r].r12, 0, 0);
			}
			for (i = 0; i < sizeof(rejected) / sizeof(rejected[0]);
			     i++) {
				CHECK(fsz(rejected[i], w, h, st, &o) ==
				      FAILURE);
				EXPECT(o, A5, A5, A5);
			}
		}
	}

	/* Explicit sign checks from 24 §3 / §5. */
	CHECK(fsz(33, 5, 5, strides[2], &o) == SUCCESS);
	CHECK(o.mYSize == -1 && o.mUSize == INT_MIN && o.mVSize == 1);
	CHECK(fsz(23, 0x80000000u, 1, strides[0], &o) == SUCCESS);
	CHECK(o.mYSize == INT_MIN && o.mUSize == 1073741824 && o.mVSize == 0);

	/* NULL contracts (F-3). */
	{
		VIDEO_FRAME_INFO_S in;
		memset(&in, 0, sizeof(in));
		set_fmt(&in.VFrame.mPixelFormat, 23);
		o.mYSize = o.mUSize = o.mVSize = A5;
		CHECK(getVideoFrameBufferSizeInfo(NULL, &o) == FAILURE);
		EXPECT(o, A5, A5, A5);
		CHECK(getVideoFrameBufferSizeInfo(&in, NULL) == FAILURE);
		CHECK(getVideoFrameBufferSizeInfo(NULL, NULL) == FAILURE);
	}
}

static void test_copy(void)
{
	static const int src[][3] = {
		{ 0, 0, 0 },
		{ 1, 1, 1 },
		{ 0x11223344, 0x7ABC1234, (int)0x89ABCDEFu },
		{ -1, -1, -1 },
		{ INT_MIN, INT_MIN, INT_MIN },
		{ INT_MAX, INT_MAX, INT_MAX },
		{ (int)0xDEADBEEFu, -123456789, 987654321 },
		{ 15, 100, -100 },
		{ (int)0xA5A5A5A5u, 0x5A5A5A5A, 0x12345678 },
	};
	unsigned int i;

	for (i = 0; i < sizeof(src) / sizeof(src[0]); i++) {
		MPP_CHN_S s, d, s0;
		memcpy(&s, src[i], sizeof(s));
		s0 = s;
		memset(&d, 0x5A, sizeof(d));
		CHECK(copy_MPP_CHN_S(&d, &s) == SUCCESS);
		CHECK(memcmp(&d, src[i], sizeof(d)) == 0);
		CHECK(memcmp(&s, &s0, sizeof(s)) == 0);
		/* pDst == pSrc is a no-op. */
		CHECK(copy_MPP_CHN_S(&s, &s) == SUCCESS);
		CHECK(memcmp(&s, &s0, sizeof(s)) == 0);
	}
}

int main(void)
{
	test_layout();
	test_bitmap();
	test_frame_size();
	test_copy();
	if (failures) {
		printf("test_utils_live: %d FAILURES\n", failures);
		return 1;
	}
	printf("test_utils_live: PASS\n");
	return 0;
}
