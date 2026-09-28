/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_frame_pool.c - host tests for the frame-header pool (spec
 * media_utils/frame_pool.md 8, cases 1-18), invariants checked after each op. */
#define _GNU_SOURCE

#include "utils/frame_pool.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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
/* Diagnostics and allocation seams                                    */
/* ------------------------------------------------------------------ */

static int g_log_calls;

void media_utils_log_error(const char *reason, unsigned int detail)
{
	(void)reason;
	(void)detail;
	g_log_calls++;
}

/* Strong overrides of the weak heap seam: plain calloc/free until armed, then
 * the nth allocation fails. */
static int g_seam_active;
static int g_seam_fail_at;
static int g_seam_calls;

void *media_utils_pool_alloc(size_t size)
{
	if (g_seam_active) {
		g_seam_calls++;
		if (g_seam_calls == g_seam_fail_at) {
			return NULL;
		}
	}
	return calloc(1, size);
}

void media_utils_pool_free(void *ptr)
{
	free(ptr);
}

/* ------------------------------------------------------------------ */
/* Fixtures and structural probes                                      */
/* ------------------------------------------------------------------ */

static VIDEO_FRAME_INFO_S mkframe(unsigned int id, void *addr)
{
	VIDEO_FRAME_INFO_S f;

	memset(&f, 0, sizeof(f));
	f.VFrame.mWidth = 100u + id;
	f.VFrame.mHeight = 200u + id;
	f.VFrame.mField = FWM_VIDEO_FIELD_NONE;
	f.VFrame.mPixelFormat = FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420;
	f.VFrame.mVideoFormat = FWM_VIDEO_FORMAT_LINEAR;
	f.VFrame.mCompressMode = FWM_COMPRESS_MODE_NONE;
	f.VFrame.mPhyAddr[0] = 0x1000u + id;
	f.VFrame.mpVirAddr[0] = addr;
	f.VFrame.mStride[0] = 0x40u * (id + 1u);
	f.VFrame.mOffsetTop = (short)(id & 0x7fffu);
	f.VFrame.mOffsetBottom = (short)(id + 1u);
	f.VFrame.mpts = 0x1122334455667788ull + id;
	f.VFrame.mExposureTime = 500u + id;
	f.VFrame.mFramecnt = id;
	f.VFrame.mEnvLV = (int)id - 5;
	f.VFrame.mWhoSetFlag = id;
	f.VFrame.mFlagPts = 0x8877665544332211ull + id;
	f.VFrame.mFrmFlag = 0x0F0F0000u | id;
	f.mId = id;
	return f;
}

static int list_len(const struct list_head *head)
{
	const struct list_head *p;
	int n = 0;

	for (p = head->next; p != head; p = p->next) {
		if (++n > 10000) {
			return -1;
		}
	}
	return n;
}

static int list_ok(const struct list_head *head)
{
	const struct list_head *p;
	int guard = 0;

	if (head->next->prev != head || head->prev->next != head) {
		return 0;
	}
	for (p = head->next; p != head; p = p->next) {
		if (++guard > 10000) {
			return 0;
		}
		if (p->next->prev != p || p->prev->next != p) {
			return 0;
		}
	}
	return 1;
}

/* Partition invariant plus the expected per-list occupancy. */
static int pool_check(VideoBufferManager *m, int ef, int ev, int eu)
{
	int nf = list_len(&m->mFreeFrmList);
	int nv = list_len(&m->mValidFrmList);
	int nu = list_len(&m->mUsingFrmList);

	if (nf < 0 || nv < 0 || nu < 0) {
		return 0;
	}
	if (!list_ok(&m->mFreeFrmList) || !list_ok(&m->mValidFrmList) ||
	    !list_ok(&m->mUsingFrmList)) {
		return 0;
	}
	if (nf + nv + nu != m->mFrameNodeNum) {
		return 0;
	}
	return nf == ef && nv == ev && nu == eu;
}

static int list_ids(const struct list_head *head, unsigned int *out, int max)
{
	const struct list_head *p;
	int n = 0;

	for (p = head->next; p != head && n < max; p = p->next) {
		const VideoFrameListInfo *node =
			container_of(p, VideoFrameListInfo, mList);

		out[n++] = node->mFrame.mId;
	}
	return n;
}

/* ------------------------------------------------------------------ */
/* ABI static facts (target values asserted on a 32-bit host only)     */
/* ------------------------------------------------------------------ */

static void test_abi(void)
{
	CHECK_EQ(sizeof(fwm_pixel_format_e), 4);
	CHECK_EQ(sizeof(fwm_video_field_e), 4);
	CHECK_EQ(sizeof(fwm_video_format_e), 4);
	CHECK_EQ(sizeof(fwm_compress_mode_e), 4);

#if __SIZEOF_POINTER__ == 4
	CHECK_EQ(sizeof(struct list_head), 8);
	CHECK_EQ(sizeof(VIDEO_FRAME_S), 144);
	CHECK_EQ(sizeof(VIDEO_FRAME_INFO_S), 152);
	CHECK_EQ(offsetof(VIDEO_FRAME_INFO_S, mId), 144);
	CHECK_EQ(sizeof(VideoFrameListInfo), 160);
	CHECK_EQ(offsetof(VideoFrameListInfo, mList), 152);
	CHECK_EQ(sizeof(VideoBufferManager), 140);

	CHECK_EQ(offsetof(VideoBufferManager, mFreeFrmList), 0);
	CHECK_EQ(offsetof(VideoBufferManager, mValidFrmList), 8);
	CHECK_EQ(offsetof(VideoBufferManager, mUsingFrmList), 16);
	CHECK_EQ(offsetof(VideoBufferManager, mFrmListLock), 24);
	CHECK_EQ(offsetof(VideoBufferManager, mCondUsingFrmEmpty), 48);
	CHECK_EQ(offsetof(VideoBufferManager, mFrameNodeNum), 96);
	CHECK_EQ(offsetof(VideoBufferManager, mbWaitUsingFrmEmptyFlag), 100);
	CHECK_EQ(offsetof(VideoBufferManager, mOps), 104);

	CHECK_EQ(offsetof(VideoBufferManager, mOps.GetOldestValidFrame), 0);
	CHECK_EQ(offsetof(VideoBufferManager, mOps.GetOldestUsingFrame), 4);
	CHECK_EQ(offsetof(VideoBufferManager, mOps.GetSpecUsingFrameWithAddr), 8);
	CHECK_EQ(offsetof(VideoBufferManager, mOps.GetAllValidUsingFrame), 12);
	CHECK_EQ(offsetof(VideoBufferManager, mOps.getValidFrame), 16);
	CHECK_EQ(offsetof(VideoBufferManager, mOps.releaseFrame), 20);
	CHECK_EQ(offsetof(VideoBufferManager, mOps.pushFrame), 24);
	CHECK_EQ(offsetof(VideoBufferManager, mOps.usingFrmEmpty), 28);
	CHECK_EQ(offsetof(VideoBufferManager, mOps.waitUsingFrmEmpty), 32);
#endif
}

/* ------------------------------------------------------------------ */
/* Case 1: Create(0,0) is a valid empty pool                           */
/* ------------------------------------------------------------------ */

static void test_empty_pool(void)
{
	VideoBufferManager *m = VideoBufMgrCreate(0, 0);
	VIDEO_FRAME_INFO_S f = mkframe(1, (void *)(uintptr_t)0x1000);

	CHECK(m != NULL);
	CHECK_EQ(m->mFrameNodeNum, 0);
	CHECK_EQ(m->mbWaitUsingFrmEmptyFlag, 0);
	CHECK(pool_check(m, 0, 0, 0));

	CHECK_EQ(VideoBufMgrPushFrame(m, &f), FAILURE);
	CHECK(VideoBufMgrGetValidFrame(m) == NULL);
	CHECK(VideoBufMgrGetAllValidUsingFrame(m) == NULL);
	CHECK(VideoBufMgrGetOldestValidFrame(m) == NULL);
	CHECK(VideoBufMgrGetOldestUsingFrame(m) == NULL);
	CHECK(VideoBufMgrGetSpecUsingFrameWithAddr(m, (void *)&m) == NULL);
	CHECK_EQ(VideoBufMgrUsingEmpty(m), 1);
	CHECK_EQ(VideoBufMgrWaitUsingEmpty(m), SUCCESS);
	CHECK(pool_check(m, 0, 0, 0));

	/* Negative frmNum is likewise a valid empty pool. */
	VideoBufMgrDestroy(m);
	m = VideoBufMgrCreate(-3, 16);
	CHECK(m != NULL);
	CHECK_EQ(m->mFrameNodeNum, 0);
	CHECK(pool_check(m, 0, 0, 0));
	VideoBufMgrDestroy(m);
}

/* ------------------------------------------------------------------ */
/* Cases 2-5: create, push FIFO, exhaustion, get FIFO                  */
/* ------------------------------------------------------------------ */

static void test_push_get_fifo(void)
{
	VideoBufferManager *m = VideoBufMgrCreate(4, 0);
	VIDEO_FRAME_INFO_S in[5];
	VIDEO_FRAME_INFO_S *got;
	unsigned int ids[4];
	int i;

	CHECK(m != NULL);
	CHECK_EQ(m->mFrameNodeNum, 4);
	CHECK(pool_check(m, 4, 0, 0));
	CHECK(m->mOps.getValidFrame != NULL);
	CHECK(m->mOps.releaseFrame != NULL);
	CHECK(m->mOps.pushFrame != NULL);
	CHECK(m->mOps.GetOldestValidFrame != NULL);
	CHECK(m->mOps.GetOldestUsingFrame != NULL);
	CHECK(m->mOps.GetSpecUsingFrameWithAddr != NULL);
	CHECK(m->mOps.GetAllValidUsingFrame != NULL);
	CHECK(m->mOps.usingFrmEmpty != NULL);
	CHECK(m->mOps.waitUsingFrmEmpty != NULL);

	for (i = 0; i < 5; i++) {
		in[i] = mkframe((unsigned int)(i + 1),
				(void *)(uintptr_t)(0xA000 + i));
	}
	for (i = 0; i < 4; i++) {
		CHECK_EQ(VideoBufMgrPushFrame(m, &in[i]), SUCCESS);
	}
	CHECK(pool_check(m, 0, 4, 0));
	CHECK_EQ(list_ids(&m->mValidFrmList, ids, 4), 4);
	for (i = 0; i < 4; i++) {
		CHECK_EQ(ids[i], (unsigned int)(i + 1));
	}

	/* Case 4: the 5th push is refused and the pool is unchanged. */
	CHECK_EQ(VideoBufMgrPushFrame(m, &in[4]), FAILURE);
	CHECK(pool_check(m, 0, 4, 0));

	/* Case 5: four gets are FIFO and copy nothing out (node pointer). */
	for (i = 0; i < 4; i++) {
		got = VideoBufMgrGetValidFrame(m);
		CHECK(got != NULL);
		if (got != NULL) {
			CHECK_EQ(got->mId, (unsigned int)(i + 1));
			CHECK(memcmp(got, &in[i], sizeof(*got)) == 0);
		}
	}
	CHECK(pool_check(m, 0, 0, 4));
	CHECK(VideoBufMgrGetValidFrame(m) == NULL);
	CHECK_EQ(list_ids(&m->mUsingFrmList, ids, 4), 4);
	for (i = 0; i < 4; i++) {
		CHECK_EQ(ids[i], (unsigned int)(i + 1));
	}

	VideoBufMgrDestroy(m);
}

/* ------------------------------------------------------------------ */
/* Case 6: release overwrites the caller's struct with the node image  */
/* ------------------------------------------------------------------ */

static void test_release_overwrite(void)
{
	VideoBufferManager *m = VideoBufMgrCreate(4, 0);
	VIDEO_FRAME_INFO_S pushed[2];
	VIDEO_FRAME_INFO_S got[2];
	VIDEO_FRAME_INFO_S caller;
	int i;

	for (i = 0; i < 2; i++) {
		pushed[i] = mkframe((unsigned int)(i + 1),
				    (void *)(uintptr_t)(0xA000 + i));
		CHECK_EQ(VideoBufMgrPushFrame(m, &pushed[i]), SUCCESS);
	}
	for (i = 0; i < 2; i++) {
		VIDEO_FRAME_INFO_S *p = VideoBufMgrGetValidFrame(m);

		CHECK(p != NULL);
		if (p != NULL) {
			got[i] = *p;
		}
	}
	CHECK(pool_check(m, 2, 0, 2));

	/* Mangle a local copy, release it; the write-back must restore it. */
	caller = got[0];
	caller.mId = 0xDEADBEEFu;
	caller.VFrame.mFramecnt = 0xFFFFu;
	caller.VFrame.mFrmFlag = 0x5A5A5A5Au;
	CHECK_EQ(VideoBufMgrReleaseFrame(m, &caller), SUCCESS);
	CHECK(memcmp(&caller, &pushed[0], sizeof(caller)) == 0);

	/* The second node is still using; release it via the returned pointer. */
	CHECK(pool_check(m, 3, 0, 1));
	CHECK_EQ(VideoBufMgrReleaseFrame(m, got + 1), SUCCESS);
	CHECK(pool_check(m, 4, 0, 0));

	CHECK_EQ(VideoBufMgrUsingEmpty(m), 1);
	VideoBufMgrDestroy(m);
}

/* ------------------------------------------------------------------ */
/* Cases 7-10: matching rules and collisions                           */
/* ------------------------------------------------------------------ */

static void test_release_matching(void)
{
	VideoBufferManager *m;
	VIDEO_FRAME_INFO_S a = mkframe(10, (void *)(uintptr_t)0x1);
	VIDEO_FRAME_INFO_S b = mkframe(10, (void *)(uintptr_t)0x2);
	VIDEO_FRAME_INFO_S q;
	VIDEO_FRAME_INFO_S *p;
	int before;

	/* Case 7: no matching using node -> FAILURE, caller untouched. */
	m = VideoBufMgrCreate(4, 0);
	CHECK_EQ(VideoBufMgrPushFrame(m, &a), SUCCESS);
	p = VideoBufMgrGetValidFrame(m);
	CHECK(p != NULL);
	if (p != NULL) {
		q = *p;
	}
	q.mId = 0x7F000001u;
	q.VFrame.mpVirAddr[0] = (void *)(uintptr_t)0xBAD;
	{
		VIDEO_FRAME_INFO_S keep = q;
		before = g_log_calls;
		CHECK_EQ(VideoBufMgrReleaseFrame(m, &q), FAILURE);
		CHECK(memcmp(&q, &keep, sizeof(q)) == 0);
		CHECK(g_log_calls > before);
	}
	CHECK(pool_check(m, 3, 0, 1));
	VideoBufMgrDestroy(m);

	/* Case 8: match by mId only (virAddr differs). */
	m = VideoBufMgrCreate(2, 0);
	CHECK_EQ(VideoBufMgrPushFrame(m, &a), SUCCESS);
	CHECK(VideoBufMgrGetValidFrame(m) != NULL);
	q = a;
	q.VFrame.mpVirAddr[0] = (void *)(uintptr_t)0x999;
	CHECK_EQ(VideoBufMgrReleaseFrame(m, &q), SUCCESS);
	CHECK(memcmp(&q, &a, sizeof(q)) == 0);
	CHECK(pool_check(m, 2, 0, 0));
	VideoBufMgrDestroy(m);

	/* Case 9: match by mpVirAddr[0] only (mId differs). */
	m = VideoBufMgrCreate(2, 0);
	CHECK_EQ(VideoBufMgrPushFrame(m, &a), SUCCESS);
	CHECK(VideoBufMgrGetValidFrame(m) != NULL);
	q = a;
	q.mId = 0x55AA55AAu;
	CHECK_EQ(VideoBufMgrReleaseFrame(m, &q), SUCCESS);
	CHECK(memcmp(&q, &a, sizeof(q)) == 0);
	CHECK(pool_check(m, 2, 0, 0));
	VideoBufMgrDestroy(m);

	/* Case 10: two using nodes sharing mId -> first using node wins. */
	m = VideoBufMgrCreate(3, 0);
	CHECK_EQ(VideoBufMgrPushFrame(m, &a), SUCCESS);
	CHECK_EQ(VideoBufMgrPushFrame(m, &b), SUCCESS);
	CHECK(VideoBufMgrGetValidFrame(m) != NULL);
	CHECK(VideoBufMgrGetValidFrame(m) != NULL);
	q = mkframe(10, (void *)(uintptr_t)0x99); /* id collides with both */
	CHECK_EQ(VideoBufMgrReleaseFrame(m, &q), SUCCESS);
	CHECK(memcmp(&q, &a, sizeof(q)) == 0);
	p = VideoBufMgrGetOldestUsingFrame(m);
	CHECK(p != NULL);
	if (p != NULL) {
		CHECK(p->VFrame.mpVirAddr[0] == (void *)(uintptr_t)0x2);
	}
	VideoBufMgrDestroy(m);
}

/* ------------------------------------------------------------------ */
/* Case 11: GetAllValidUsingFrame drains using before valid            */
/* ------------------------------------------------------------------ */

static void test_drain(void)
{
	VideoBufferManager *m = VideoBufMgrCreate(6, 0);
	VIDEO_FRAME_INFO_S in[5];
	unsigned int expect[5] = { 1, 2, 3, 4, 5 };
	unsigned int got;
	int i;

	for (i = 0; i < 5; i++) {
		in[i] = mkframe((unsigned int)(i + 1),
				(void *)(uintptr_t)(0xB000 + i));
	}

	CHECK_EQ(VideoBufMgrPushFrame(m, &in[0]), SUCCESS);
	CHECK_EQ(VideoBufMgrPushFrame(m, &in[1]), SUCCESS);
	CHECK_EQ(VideoBufMgrPushFrame(m, &in[2]), SUCCESS);
	CHECK(VideoBufMgrGetValidFrame(m) != NULL); /* 1 -> using */
	CHECK(VideoBufMgrGetValidFrame(m) != NULL); /* 2 -> using */
	CHECK_EQ(VideoBufMgrPushFrame(m, &in[3]), SUCCESS);
	CHECK_EQ(VideoBufMgrPushFrame(m, &in[4]), SUCCESS);
	/* free=1 valid=3 using=2 */
	CHECK(pool_check(m, 1, 3, 2));

	for (i = 0; i < 5; i++) {
		VIDEO_FRAME_INFO_S *p = VideoBufMgrGetAllValidUsingFrame(m);

		CHECK(p != NULL);
		if (p != NULL) {
			got = p->mId;
			CHECK_EQ(got, expect[i]);
			CHECK(memcmp(p, &in[expect[i] - 1], sizeof(*p)) == 0);
		}
	}
	CHECK(pool_check(m, 6, 0, 0));
	CHECK(VideoBufMgrGetAllValidUsingFrame(m) == NULL);
	CHECK(pool_check(m, 6, 0, 0));

	VideoBufMgrDestroy(m);
}

/* ------------------------------------------------------------------ */
/* Cases 12-13: peeks, address lookup, NULL address                    */
/* ------------------------------------------------------------------ */

static void test_peek_and_addr(void)
{
	VideoBufferManager *m = VideoBufMgrCreate(4, 0);
	VIDEO_FRAME_INFO_S a = mkframe(1, (void *)(uintptr_t)0x11);
	VIDEO_FRAME_INFO_S z = mkframe(2, NULL);
	VIDEO_FRAME_INFO_S *p;
	unsigned int ids[4];
	int before;

	CHECK_EQ(VideoBufMgrPushFrame(m, &a), SUCCESS);
	CHECK_EQ(VideoBufMgrPushFrame(m, &z), SUCCESS);

	/* Peek is a no-move. */
	p = VideoBufMgrGetOldestValidFrame(m);
	CHECK(p != NULL);
	if (p != NULL) {
		CHECK_EQ(p->mId, 1u);
	}
	CHECK(pool_check(m, 2, 2, 0));
	CHECK(VideoBufMgrGetOldestUsingFrame(m) == NULL);

	CHECK(VideoBufMgrGetValidFrame(m) != NULL); /* 1 -> using */
	CHECK(VideoBufMgrGetValidFrame(m) != NULL); /* 2 -> using */
	CHECK(pool_check(m, 2, 0, 2));
	CHECK(VideoBufMgrGetOldestValidFrame(m) == NULL);

	p = VideoBufMgrGetOldestUsingFrame(m);
	CHECK(p != NULL);
	if (p != NULL) {
		CHECK_EQ(p->mId, 1u);
	}
	/* peek did not move: still using=2 */
	CHECK(pool_check(m, 2, 0, 2));
	CHECK_EQ(list_ids(&m->mUsingFrmList, ids, 4), 2);
	CHECK_EQ(ids[0], 1u);
	CHECK_EQ(ids[1], 2u);

	p = VideoBufMgrGetSpecUsingFrameWithAddr(m, (void *)(uintptr_t)0x11);
	CHECK(p != NULL);
	if (p != NULL) {
		CHECK_EQ(p->mId, 1u);
	}
	CHECK(pool_check(m, 2, 0, 2));

	/* Not found: NULL and a diagnostic. */
	before = g_log_calls;
	CHECK(VideoBufMgrGetSpecUsingFrameWithAddr(m,
						   (void *)(uintptr_t)0x22) ==
	      NULL);
	CHECK(g_log_calls > before);

	/* Case 13: a NULL lookup matches the NULL-plane node. */
	p = VideoBufMgrGetSpecUsingFrameWithAddr(m, NULL);
	CHECK(p != NULL);
	if (p != NULL) {
		CHECK_EQ(p->mId, 2u);
	}
	CHECK(pool_check(m, 2, 0, 2));

	VideoBufMgrDestroy(m);
}

/* ------------------------------------------------------------------ */
/* Case 14: waitUsingFrmEmpty on an already-empty pool returns         */
/* ------------------------------------------------------------------ */

static void test_wait_immediate(void)
{
	VideoBufferManager *m = VideoBufMgrCreate(2, 0);

	CHECK_EQ(VideoBufMgrWaitUsingEmpty(m), SUCCESS);
	CHECK_EQ(m->mbWaitUsingFrmEmptyFlag, 0);
	CHECK(pool_check(m, 2, 0, 0));

	VideoBufMgrDestroy(m);
}

/* ------------------------------------------------------------------ */
/* Cases 15-16: threaded wait, one signal on the last release          */
/* ------------------------------------------------------------------ */

struct waiter_ctx {
	VideoBufferManager *mgr;
	pthread_mutex_t lock;
	int started;
	int returned;
};

static void *waiter_fn(void *arg)
{
	struct waiter_ctx *c = arg;

	pthread_mutex_lock(&c->lock);
	c->started = 1;
	pthread_mutex_unlock(&c->lock);

	VideoBufMgrWaitUsingEmpty(c->mgr);

	pthread_mutex_lock(&c->lock);
	c->returned = 1;
	pthread_mutex_unlock(&c->lock);
	return NULL;
}

static int waiter_returned(struct waiter_ctx *c)
{
	int r;

	pthread_mutex_lock(&c->lock);
	r = c->returned;
	pthread_mutex_unlock(&c->lock);
	return r;
}

static int join_timeout(pthread_t t)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 5;
	return pthread_timedjoin_np(t, NULL, &ts);
}

static void test_wait_threaded(void)
{
	int iter;

	for (iter = 0; iter < 50; iter++) {
		VideoBufferManager *m = VideoBufMgrCreate(4, 0);
		VIDEO_FRAME_INFO_S in[3];
		VIDEO_FRAME_INFO_S ret[3];
		struct waiter_ctx c;
		pthread_t tid;
		int i;

		for (i = 0; i < 3; i++) {
			in[i] = mkframe((unsigned int)(i + 1),
					(void *)(uintptr_t)(0xC000 + i));
			CHECK_EQ(VideoBufMgrPushFrame(m, &in[i]), SUCCESS);
		}
		for (i = 0; i < 3; i++) {
			VIDEO_FRAME_INFO_S *p = VideoBufMgrGetValidFrame(m);

			CHECK(p != NULL);
			if (p != NULL) {
				ret[i] = *p;
			}
		}
		CHECK(pool_check(m, 1, 0, 3));

		memset(&c, 0, sizeof(c));
		c.mgr = m;
		pthread_mutex_init(&c.lock, NULL);
		pthread_create(&tid, NULL, waiter_fn, &c);

		while (!c.started) {
			usleep(1000);
		}
		usleep(2000);
		CHECK_EQ(waiter_returned(&c), 0);

		/* Interior releases must not wake the waiter. */
		CHECK_EQ(VideoBufMgrReleaseFrame(m, &ret[0]), SUCCESS);
		usleep(2000);
		CHECK_EQ(waiter_returned(&c), 0);
		CHECK_EQ(VideoBufMgrReleaseFrame(m, &ret[1]), SUCCESS);
		usleep(2000);
		CHECK_EQ(waiter_returned(&c), 0);

		/* The last release clears using and signals exactly then. */
		CHECK_EQ(VideoBufMgrReleaseFrame(m, &ret[2]), SUCCESS);
		CHECK_EQ(join_timeout(tid), 0);
		CHECK_EQ(waiter_returned(&c), 1);
		CHECK_EQ(m->mbWaitUsingFrmEmptyFlag, 0);
		CHECK(pool_check(m, 4, 0, 0));

		pthread_mutex_destroy(&c.lock);
		VideoBufMgrDestroy(m);
	}

	/* Case 16: pushes must not end the wait; only the using list does. */
	for (iter = 0; iter < 20; iter++) {
		VideoBufferManager *m = VideoBufMgrCreate(4, 0);
		VIDEO_FRAME_INFO_S f = mkframe(1, (void *)(uintptr_t)0xD000);
		VIDEO_FRAME_INFO_S more[3];
		VIDEO_FRAME_INFO_S ret;
		struct waiter_ctx c;
		pthread_t tid;
		int i;

		CHECK_EQ(VideoBufMgrPushFrame(m, &f), SUCCESS);
		{
			VIDEO_FRAME_INFO_S *p = VideoBufMgrGetValidFrame(m);

			CHECK(p != NULL);
			if (p != NULL) {
				ret = *p;
			}
		}

		memset(&c, 0, sizeof(c));
		c.mgr = m;
		pthread_mutex_init(&c.lock, NULL);
		pthread_create(&tid, NULL, waiter_fn, &c);
		while (!c.started) {
			usleep(1000);
		}
		usleep(2000);
		CHECK_EQ(waiter_returned(&c), 0);

		for (i = 0; i < 3; i++) {
			more[i] = mkframe((unsigned int)(i + 10),
					  (void *)(uintptr_t)(0xD100 + i));
			CHECK_EQ(VideoBufMgrPushFrame(m, &more[i]), SUCCESS);
		}
		usleep(2000);
		CHECK_EQ(waiter_returned(&c), 0);

		CHECK_EQ(VideoBufMgrReleaseFrame(m, &ret), SUCCESS);
		CHECK_EQ(join_timeout(tid), 0);
		CHECK_EQ(waiter_returned(&c), 1);
		/* The three pushed frames stay valid; only the using node moved. */
		CHECK(pool_check(m, 1, 3, 0));

		pthread_mutex_destroy(&c.lock);
		VideoBufMgrDestroy(m);
	}
}

/* ------------------------------------------------------------------ */
/* Case 17: Destroy paths, including leaks and NULL                    */
/* ------------------------------------------------------------------ */

static void test_destroy_paths(void)
{
	VideoBufferManager *m;
	VIDEO_FRAME_INFO_S f = mkframe(1, (void *)(uintptr_t)0x1);
	VIDEO_FRAME_INFO_S g = mkframe(2, (void *)(uintptr_t)0x2);
	VIDEO_FRAME_INFO_S *p;
	int before;

	/* Clean destroy: free only. */
	m = VideoBufMgrCreate(3, 0);
	VideoBufMgrDestroy(m);

	/* Nodes in free + valid. */
	m = VideoBufMgrCreate(4, 0);
	CHECK_EQ(VideoBufMgrPushFrame(m, &f), SUCCESS);
	CHECK_EQ(VideoBufMgrPushFrame(m, &g), SUCCESS);
	VideoBufMgrDestroy(m);

	/* Nodes in free + valid + using: using is leaked and a mismatch logs. */
	m = VideoBufMgrCreate(4, 0);
	CHECK_EQ(VideoBufMgrPushFrame(m, &f), SUCCESS);
	CHECK_EQ(VideoBufMgrPushFrame(m, &g), SUCCESS);
	p = VideoBufMgrGetValidFrame(m);
	CHECK(p != NULL);
	before = g_log_calls;
	VideoBufMgrDestroy(m);
	CHECK(g_log_calls > before);

	/* Destroy(NULL) is a no-op diagnostic. */
	before = g_log_calls;
	VideoBufMgrDestroy(NULL);
	CHECK(g_log_calls > before);
}

/* ------------------------------------------------------------------ */
/* Case 18: partial allocation and manager allocation failure          */
/* ------------------------------------------------------------------ */

static void test_partial_alloc(void)
{
	VideoBufferManager *m;
	VIDEO_FRAME_INFO_S in[3];
	VIDEO_FRAME_INFO_S *p;

	/* Fail the manager allocation -> NULL. */
	g_seam_calls = 0;
	g_seam_fail_at = 1;
	g_seam_active = 1;
	m = VideoBufMgrCreate(1, 0);
	g_seam_active = 0;
	CHECK(m == NULL);

	/* Fail the third node allocation -> two-node partial pool, no rollback. */
	in[0] = mkframe(1, (void *)(uintptr_t)0x1);
	in[1] = mkframe(2, (void *)(uintptr_t)0x2);
	in[2] = mkframe(3, (void *)(uintptr_t)0x3);

	g_seam_calls = 0;
	g_seam_fail_at = 4; /* 1 manager + node1 + node2 + failing node3 */
	g_seam_active = 1;
	m = VideoBufMgrCreate(3, 0);
	g_seam_active = 0;

	CHECK(m != NULL);
	if (m != NULL) {
		CHECK_EQ(m->mFrameNodeNum, 2);
		CHECK(pool_check(m, 2, 0, 0));
		CHECK_EQ(VideoBufMgrPushFrame(m, &in[0]), SUCCESS);
		CHECK_EQ(VideoBufMgrPushFrame(m, &in[1]), SUCCESS);
		CHECK_EQ(VideoBufMgrPushFrame(m, &in[2]), FAILURE);
		CHECK(pool_check(m, 0, 2, 0));
		p = VideoBufMgrGetValidFrame(m);
		CHECK(p != NULL);
		if (p != NULL) {
			CHECK_EQ(p->mId, 1u);
		}
		CHECK_EQ(VideoBufMgrReleaseFrame(m, p), SUCCESS);
		CHECK(pool_check(m, 1, 1, 0));
		VideoBufMgrDestroy(m);
	}
}

int main(void)
{
	test_abi();
	test_empty_pool();
	test_push_get_fifo();
	test_release_overwrite();
	test_release_matching();
	test_drain();
	test_peek_and_addr();
	test_wait_immediate();
	test_wait_threaded();
	test_destroy_paths();
	test_partial_alloc();

	if (fails) {
		printf("test_frame_pool: %d check(s) failed\n", fails);
		return 1;
	}
	printf("test_frame_pool: %d checks, 0 failures\n", checks);
	return 0;
}
