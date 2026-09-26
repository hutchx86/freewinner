/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * test_pixel_format.c - host tests for the clean-room pixel-format maps.
 *
 * Behaviour source: spec/media_utils/pixel_format.md sections 3-6.
 * Both directions are checked against the complete documented tables by
 * symbolic name, the default/unknown behaviour is swept over the low value
 * domain plus boundary bit patterns, and the two required RAW asymmetries and
 * the round-trip non-identity sets are asserted exactly.
 */
#include "utils/pixel_format.h"

#include <stddef.h>
#include <stdio.h>

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

/* Silence the unit's diagnostics from the default/compressed branches. */
void media_utils_log_error(const char *reason, unsigned int detail)
{
	(void)reason;
	(void)detail;
}

/* ------------------------------------------------------------------ */
/* Documented tables                                                   */
/* ------------------------------------------------------------------ */

struct fwd_case {
	int src;
	fwm_pixel_format_e dst;
};

static const struct fwd_case fwd_cases[] = {
	{ V4L2_PIX_FMT_NV21M, FWM_MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420 },
	{ V4L2_PIX_FMT_NV12M, FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420 },
	{ V4L2_PIX_FMT_YUV420M, FWM_MM_PIXEL_FORMAT_YUV_PLANAR_420 },
	{ V4L2_PIX_FMT_YVU420M, FWM_MM_PIXEL_FORMAT_YVU_PLANAR_420 },
	{ V4L2_PIX_FMT_YUYV, FWM_MM_PIXEL_FORMAT_YUYV_PACKAGE_422 },
	{ V4L2_PIX_FMT_NV16M, FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_422 },
	{ V4L2_PIX_FMT_NV61M, FWM_MM_PIXEL_FORMAT_YVU_SEMIPLANAR_422 },
	{ V4L2_PIX_FMT_FBC, FWM_MM_PIXEL_FORMAT_YUV_AW_AFBC },
	{ V4L2_PIX_FMT_LBC_2_0X, FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_2_0X },
	{ V4L2_PIX_FMT_LBC_2_5X, FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_2_5X },
	{ V4L2_PIX_FMT_LBC_1_0X, FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_1_0X },
	{ V4L2_PIX_FMT_SBGGR8, FWM_MM_PIXEL_FORMAT_RAW_SBGGR8 },
	{ V4L2_PIX_FMT_SRGGB8, FWM_MM_PIXEL_FORMAT_RAW_SRGGB8 },
	{ V4L2_PIX_FMT_SGRBG8, FWM_MM_PIXEL_FORMAT_RAW_SGRBG8 },
	{ V4L2_PIX_FMT_SGBRG8, FWM_MM_PIXEL_FORMAT_RAW_SGBRG8 },
	{ V4L2_PIX_FMT_SBGGR10, FWM_MM_PIXEL_FORMAT_RAW_SBGGR10 },
	{ V4L2_PIX_FMT_SRGGB10, FWM_MM_PIXEL_FORMAT_RAW_SRGGB10 },
	{ V4L2_PIX_FMT_SGRBG10, FWM_MM_PIXEL_FORMAT_RAW_SGRBG10 },
	{ V4L2_PIX_FMT_SGBRG10, FWM_MM_PIXEL_FORMAT_RAW_SGBRG10 },
	{ V4L2_PIX_FMT_SBGGR12, FWM_MM_PIXEL_FORMAT_RAW_SBGGR12 },
	{ V4L2_PIX_FMT_SRGGB12, FWM_MM_PIXEL_FORMAT_RAW_SRGGB12 },
	{ V4L2_PIX_FMT_SGRBG12, FWM_MM_PIXEL_FORMAT_RAW_SGRBG12 },
	{ V4L2_PIX_FMT_SGBRG12, FWM_MM_PIXEL_FORMAT_RAW_SGBRG12 },
	{ V4L2_PIX_FMT_MJPEG, FWM_MM_PIXEL_FORMAT_BUTT },
	{ V4L2_PIX_FMT_JPEG, FWM_MM_PIXEL_FORMAT_BUTT },
	{ V4L2_PIX_FMT_H264, FWM_MM_PIXEL_FORMAT_BUTT },
};

#define FWD_CASE_COUNT (sizeof(fwd_cases) / sizeof(fwd_cases[0]))

struct rev_case {
	fwm_pixel_format_e src;
	int dst;
};

static const struct rev_case rev_cases[] = {
	{ FWM_MM_PIXEL_FORMAT_YUV_PLANAR_420, V4L2_PIX_FMT_YUV420M },
	{ FWM_MM_PIXEL_FORMAT_YVU_PLANAR_420, V4L2_PIX_FMT_YVU420M },
	{ FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420, V4L2_PIX_FMT_NV12M },
	{ FWM_MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420, V4L2_PIX_FMT_NV21M },
	{ FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_422, V4L2_PIX_FMT_NV16M },
	{ FWM_MM_PIXEL_FORMAT_YVU_SEMIPLANAR_422, V4L2_PIX_FMT_NV61M },
	{ FWM_MM_PIXEL_FORMAT_YUYV_PACKAGE_422, V4L2_PIX_FMT_YUYV },
	{ FWM_MM_PIXEL_FORMAT_RGB_1555, V4L2_PIX_FMT_RGB555 },
	{ FWM_MM_PIXEL_FORMAT_RGB_8888, V4L2_PIX_FMT_RGB32 },
	{ FWM_MM_PIXEL_FORMAT_YUV_AW_AFBC, V4L2_PIX_FMT_FBC },
	{ FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_2_0X, V4L2_PIX_FMT_LBC_2_0X },
	{ FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_2_5X, V4L2_PIX_FMT_LBC_2_5X },
	{ FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_1_0X, V4L2_PIX_FMT_LBC_1_0X },
	{ FWM_MM_PIXEL_FORMAT_RAW_SBGGR8, V4L2_PIX_FMT_SBGGR8 },
	{ FWM_MM_PIXEL_FORMAT_RAW_SRGGB8, V4L2_PIX_FMT_SRGGB8 },
	{ FWM_MM_PIXEL_FORMAT_RAW_SGBRG8, V4L2_PIX_FMT_SGBRG8 },
	{ FWM_MM_PIXEL_FORMAT_RAW_SGRBG8, V4L2_PIX_FMT_SGRBG8 },
	/* Required vendor asymmetries: 10 -> 8-bit, 12 -> 10-bit siblings. */
	{ FWM_MM_PIXEL_FORMAT_RAW_SBGGR10, V4L2_PIX_FMT_SBGGR8 },
	{ FWM_MM_PIXEL_FORMAT_RAW_SRGGB10, V4L2_PIX_FMT_SRGGB10 },
	{ FWM_MM_PIXEL_FORMAT_RAW_SGBRG10, V4L2_PIX_FMT_SGBRG10 },
	{ FWM_MM_PIXEL_FORMAT_RAW_SGRBG10, V4L2_PIX_FMT_SGRBG10 },
	{ FWM_MM_PIXEL_FORMAT_RAW_SBGGR12, V4L2_PIX_FMT_SBGGR10 },
	{ FWM_MM_PIXEL_FORMAT_RAW_SRGGB12, V4L2_PIX_FMT_SRGGB12 },
	{ FWM_MM_PIXEL_FORMAT_RAW_SGBRG12, V4L2_PIX_FMT_SGBRG12 },
	{ FWM_MM_PIXEL_FORMAT_RAW_SGRBG12, V4L2_PIX_FMT_SGRBG12 },
};

#define REV_CASE_COUNT (sizeof(rev_cases) / sizeof(rev_cases[0]))

static fwm_pixel_format_e expected_fwd(int v)
{
	size_t i;

	for (i = 0; i < FWD_CASE_COUNT; i++)
		if (fwd_cases[i].src == v)
			return fwd_cases[i].dst;
	return FWM_MM_PIXEL_FORMAT_BUTT;
}

static int expected_rev(fwm_pixel_format_e v)
{
	size_t i;

	for (i = 0; i < REV_CASE_COUNT; i++)
		if (rev_cases[i].src == v)
			return rev_cases[i].dst;
	return V4L2_PIX_FMT_YUV420M;
}

/* ------------------------------------------------------------------ */
/* ABI pinned facts                                                    */
/* ------------------------------------------------------------------ */

static void test_abi(void)
{
	CHECK_EQ(sizeof(fwm_pixel_format_e), 4);
	CHECK_EQ(sizeof(int), 4);

	/* Fourcc macros must expand to the pinned numeric UAPI values. */
	CHECK_EQ(V4L2_PIX_FMT_YUV420M, 0x32314D59L);
	CHECK_EQ(V4L2_PIX_FMT_YVU420M, 0x31324D59L);
	CHECK_EQ(V4L2_PIX_FMT_NV12M, 0x32314D4EL);
	CHECK_EQ(V4L2_PIX_FMT_NV21M, 0x31324D4EL);
	CHECK_EQ(V4L2_PIX_FMT_NV16M, 0x36314D4EL);
	CHECK_EQ(V4L2_PIX_FMT_NV61M, 0x31364D4EL);
	CHECK_EQ(V4L2_PIX_FMT_YUYV, 0x56595559L);
	CHECK_EQ(V4L2_PIX_FMT_RGB555, 0x4F424752L);
	CHECK_EQ(V4L2_PIX_FMT_RGB32, 0x34424752L);
	CHECK_EQ(V4L2_PIX_FMT_MJPEG, 0x47504A4DL);
	CHECK_EQ(V4L2_PIX_FMT_JPEG, 0x4745504AL);
	CHECK_EQ(V4L2_PIX_FMT_H264, 0x34363248L);
	CHECK_EQ(V4L2_PIX_FMT_SBGGR8, 0x31384142L);
	CHECK_EQ(V4L2_PIX_FMT_SRGGB8, 0x42474752L);
	CHECK_EQ(V4L2_PIX_FMT_SGBRG8, 0x47524247L);
	CHECK_EQ(V4L2_PIX_FMT_SGRBG8, 0x47425247L);
	CHECK_EQ(V4L2_PIX_FMT_SBGGR10, 0x30314742L);
	CHECK_EQ(V4L2_PIX_FMT_SRGGB10, 0x30314752L);
	CHECK_EQ(V4L2_PIX_FMT_SGBRG10, 0x30314247L);
	CHECK_EQ(V4L2_PIX_FMT_SGRBG10, 0x30314142L);
	CHECK_EQ(V4L2_PIX_FMT_SBGGR12, 0x32314742L);
	CHECK_EQ(V4L2_PIX_FMT_SRGGB12, 0x32314752L);
	CHECK_EQ(V4L2_PIX_FMT_SGBRG12, 0x32314247L);
	CHECK_EQ(V4L2_PIX_FMT_SGRBG12, 0x32314142L);
	CHECK_EQ(V4L2_PIX_FMT_FBC, 0x31324346L);
	CHECK_EQ(V4L2_PIX_FMT_LBC_2_0X, 0x3132434CL);
	CHECK_EQ(V4L2_PIX_FMT_LBC_2_5X, 0x3232434CL);
	CHECK_EQ(V4L2_PIX_FMT_LBC_1_0X, 0x3332434CL);

	/* Pinned fwm_pixel_format_e numeric ABI values. */
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RGB_1BPP, 0);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RGB_1555, 8);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RGB_8888, 10);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_YUV_PLANAR_420, 20);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_422, 22);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420, 23);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_YUYV_PACKAGE_422, 26);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_YVU_PLANAR_420, 30);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_YVU_SEMIPLANAR_422, 31);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420, 32);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_YUV_AW_AFBC, 33);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_2_0X, 34);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_2_5X, 35);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_1_0X, 36);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RAW_SBGGR8, 37);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RAW_SGBRG8, 38);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RAW_SGRBG8, 39);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RAW_SRGGB8, 40);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RAW_SBGGR10, 41);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RAW_SGBRG10, 42);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RAW_SGRBG10, 43);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RAW_SRGGB10, 44);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RAW_SBGGR12, 45);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RAW_SGBRG12, 46);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RAW_SGRBG12, 47);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_RAW_SRGGB12, 48);
	CHECK_EQ(FWM_MM_PIXEL_FORMAT_BUTT, 49);
}

/* ------------------------------------------------------------------ */
/* Both directions, symbolic                                              */
/* ------------------------------------------------------------------ */

static void test_forward_table(void)
{
	size_t i;

	for (i = 0; i < FWD_CASE_COUNT; i++)
		CHECK_EQ(map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(fwd_cases[i].src),
			 fwd_cases[i].dst);
}

static void test_reverse_table(void)
{
	size_t i;

	for (i = 0; i < REV_CASE_COUNT; i++)
		CHECK_EQ(map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(rev_cases[i].src),
			 rev_cases[i].dst);
}

/* ------------------------------------------------------------------ */
/* Full low-domain sweep + boundary values                               */
/* ------------------------------------------------------------------ */

static void test_forward_sweep(void)
{
	static const int edge[] = {
		0x7FFFFFFF, (int)0x80000000, -1, (int)0xFFFFFFFF,
		0x40000000, 0x12345678, (int)0xDEADBEEF, 0x01020304,
	};
	long v;
	size_t i;

	/* Covers every documented numeric value and the whole default gap. */
	for (v = -512; v <= 8192; v++)
		CHECK_EQ(map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E((int)v),
			 (int)expected_fwd((int)v));

	for (i = 0; i < sizeof(edge) / sizeof(edge[0]); i++)
		CHECK_EQ(map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(edge[i]),
			 (int)expected_fwd(edge[i]));

	/* RGB fourccs have no forward case: they must fall to BUTT. */
	CHECK_EQ(map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(V4L2_PIX_FMT_RGB555),
		 FWM_MM_PIXEL_FORMAT_BUTT);
	CHECK_EQ(map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(V4L2_PIX_FMT_RGB32),
		 FWM_MM_PIXEL_FORMAT_BUTT);
}

static void test_reverse_sweep(void)
{
	long v;

	for (v = -8; v <= 128; v++)
		CHECK_EQ(map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT((fwm_pixel_format_e)v),
			 expected_rev((fwm_pixel_format_e)v));

	/* Any 32-bit value is a valid enum input. */
	CHECK_EQ(map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(FWM_MM_PIXEL_FORMAT_BUTT),
		 V4L2_PIX_FMT_YUV420M);
	CHECK_EQ(map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(0x7FFFFFFF),
		 V4L2_PIX_FMT_YUV420M);
	CHECK_EQ(map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT((fwm_pixel_format_e)-1),
		 V4L2_PIX_FMT_YUV420M);
	CHECK_EQ(map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(
			 (fwm_pixel_format_e)0x80000000),
		 V4L2_PIX_FMT_YUV420M);
	/* Documented no-case enum values (UYVY 25, VYUY 27, ... 422/444). */
	CHECK_EQ(map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(
			 (fwm_pixel_format_e)25),
		 V4L2_PIX_FMT_YUV420M);
	CHECK_EQ(map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(
			 (fwm_pixel_format_e)27),
		 V4L2_PIX_FMT_YUV420M);
}

/* ------------------------------------------------------------------ */
/* Required asymmetries                                                   */
/* ------------------------------------------------------------------ */

static void test_quirk_raw_pairs(void)
{
	/* Reverse emits the 8-/10-bit sibling, not the matching one. */
	CHECK_EQ(map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(
			 FWM_MM_PIXEL_FORMAT_RAW_SBGGR10),
		 V4L2_PIX_FMT_SBGGR8);
	CHECK_EQ(map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(
			 FWM_MM_PIXEL_FORMAT_RAW_SBGGR12),
		 V4L2_PIX_FMT_SBGGR10);

	/* The forward map for the same source formats is the correct one. */
	CHECK_EQ(map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(V4L2_PIX_FMT_SBGGR10),
		 FWM_MM_PIXEL_FORMAT_RAW_SBGGR10);
	CHECK_EQ(map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(V4L2_PIX_FMT_SBGGR12),
		 FWM_MM_PIXEL_FORMAT_RAW_SBGGR12);
}

/* ------------------------------------------------------------------ */
/* Round-trip / non-identity census                                       */
/* ------------------------------------------------------------------ */

/* Forward-then-reverse is non-identity for exactly these five inputs. */
static int is_fwd_nonidentity(int fourcc)
{
	return fourcc == V4L2_PIX_FMT_SBGGR10 ||
	       fourcc == V4L2_PIX_FMT_SBGGR12 ||
	       fourcc == V4L2_PIX_FMT_MJPEG ||
	       fourcc == V4L2_PIX_FMT_JPEG ||
	       fourcc == V4L2_PIX_FMT_H264;
}

static void test_roundtrip(void)
{
	size_t i;

	/* reverse(forward(x)) == x for every documented fourcc but the five. */
	for (i = 0; i < FWD_CASE_COUNT; i++) {
		int src = fwd_cases[i].src;
		int rt = map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(
			map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(src));

		if (is_fwd_nonidentity(src))
			CHECK(rt != src);
		else
			CHECK_EQ(rt, src);
	}

	/* The compressed forms collapse to BUTT and reverse to YUV420M. */
	CHECK_EQ(map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(
			 map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(
				 V4L2_PIX_FMT_MJPEG)),
		 V4L2_PIX_FMT_YUV420M);
	CHECK_EQ(map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(
			 map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(V4L2_PIX_FMT_JPEG)),
		 V4L2_PIX_FMT_YUV420M);
	CHECK_EQ(map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(
			 map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(V4L2_PIX_FMT_H264)),
		 V4L2_PIX_FMT_YUV420M);

	/* Unknown fourcc -> BUTT -> YUV420M, never the original. */
	CHECK_EQ(map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(0x12345678),
		 FWM_MM_PIXEL_FORMAT_BUTT);
	CHECK_EQ(map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(
			 map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(0x12345678)),
		 V4L2_PIX_FMT_YUV420M);

	/* Reverse-then-forward is non-identity for the two RAW quirks; the
	 * RGB and compressed asymmetry is asserted separately. */
	CHECK_EQ(map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(
			 map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(
				 FWM_MM_PIXEL_FORMAT_RAW_SBGGR10)),
		 FWM_MM_PIXEL_FORMAT_RAW_SBGGR8);
	CHECK_EQ(map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(
			 map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(
				 FWM_MM_PIXEL_FORMAT_RAW_SBGGR12)),
		 FWM_MM_PIXEL_FORMAT_RAW_SBGGR10);

	/* BUTT has no reverse case: it defaults to YUV420M, which forwards to
	 * the 420 planar enum. */
	CHECK_EQ(map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(
			 map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(
				 FWM_MM_PIXEL_FORMAT_BUTT)),
		 FWM_MM_PIXEL_FORMAT_YUV_PLANAR_420);

	/* RGB is one-way: it reverses to RGB fourccs, which have no forward
	 * case, so the round trip returns BUTT. */
	CHECK_EQ(map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(
			 map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(
				 FWM_MM_PIXEL_FORMAT_RGB_1555)),
		 FWM_MM_PIXEL_FORMAT_BUTT);
	CHECK_EQ(map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(
			 map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(
				 FWM_MM_PIXEL_FORMAT_RGB_8888)),
		 FWM_MM_PIXEL_FORMAT_BUTT);
}

int main(void)
{
	test_abi();
	test_forward_table();
	test_reverse_table();
	test_forward_sweep();
	test_reverse_sweep();
	test_quirk_raw_pairs();
	test_roundtrip();

	if (fails) {
		printf("test_pixel_format: %d check(s) failed\n", fails);
		return 1;
	}
	printf("test_pixel_format: %d checks, 0 failures\n", checks);
	return 0;
}
