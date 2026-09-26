/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * test_media_helpers.c - host tests for the clean-room copy_MPP_CHN_S helper.
 *
 * Behaviour source: spec/media_utils/media_helpers.md section 2/4.
 * A fully populated descriptor (including a non-zero sentinel pattern) is
 * copied and every field plus the return value is asserted.
 */
#include "utils/media_helpers.h"

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

/* ------------------------------------------------------------------ */
/* ABI static facts                                                    */
/* ------------------------------------------------------------------ */

static void test_abi(void)
{
	CHECK_EQ(sizeof(fwm_mod_id_e), 4);
	CHECK_EQ(sizeof(MPP_CHN_S), 12);
	CHECK_EQ(offsetof(MPP_CHN_S, mModId), 0);
	CHECK_EQ(offsetof(MPP_CHN_S, mDevId), 4);
	CHECK_EQ(offsetof(MPP_CHN_S, mChnId), 8);
}

/* ------------------------------------------------------------------ */
/* Copy behaviour                                                      */
/* ------------------------------------------------------------------ */

static void expect_copy(fwm_mod_id_e mod, int dev, int chn)
{
	MPP_CHN_S src;
	MPP_CHN_S dst;

	src.mModId = mod;
	src.mDevId = dev;
	src.mChnId = chn;

	memset(&dst, 0xA5, sizeof(dst));

	CHECK_EQ(copy_MPP_CHN_S(&dst, &src), SUCCESS);
	CHECK_EQ((long)dst.mModId, (long)src.mModId);
	CHECK_EQ(dst.mDevId, dev);
	CHECK_EQ(dst.mChnId, chn);
}

static void test_copy(void)
{
	/* Fully populated descriptor with a non-zero sentinel pattern. */
	expect_copy((fwm_mod_id_e)0x11223344, 0x7ABC1234, (int)0x89ABCDEF);
	expect_copy((fwm_mod_id_e)0, 0, 0);
	expect_copy((fwm_mod_id_e)0xFFFFFFFF, -1, -2147483647 - 1);
	expect_copy((fwm_mod_id_e)1, 1, 1);
	expect_copy((fwm_mod_id_e)0x00000001, 100, -100);
	expect_copy((fwm_mod_id_e)0xDEADBEEF, -123456789, 987654321);
}

int main(void)
{
	test_abi();
	test_copy();

	if (fails) {
		printf("test_media_helpers: %d check(s) failed\n", fails);
		return 1;
	}
	printf("test_media_helpers: %d checks, 0 failures\n", checks);
	return 0;
}
