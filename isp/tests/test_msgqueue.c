/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_msgqueue.c - host tests for msgqueue (spec media_utils/msgqueue.md 3-5);
 * deterministic, own pthread_cond_wait_timeout, heap wrapped for leak checks. */
#define _GNU_SOURCE
#include "utils/msgqueue.h"

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
/* Allocation accounting                                               */
/* ------------------------------------------------------------------ */

static long g_live;
static long g_calls;
static long g_fail_at; /* 1-based malloc call to fail; 0 = disabled */

void *__real_malloc(size_t size);
void *__real_calloc(size_t n, size_t size);
void __real_free(void *ptr);

void *__wrap_malloc(size_t size)
{
	void *p;

	__sync_add_and_fetch(&g_calls, 1);
	if (g_fail_at != 0 && g_calls == g_fail_at)
		return NULL;
	p = __real_malloc(size);
	if (p != NULL)
		__sync_add_and_fetch(&g_live, 1);
	return p;
}

void *__wrap_calloc(size_t n, size_t size)
{
	void *p;

	__sync_add_and_fetch(&g_calls, 1);
	if (g_fail_at != 0 && g_calls == g_fail_at)
		return NULL;
	p = __real_calloc(n, size);
	if (p != NULL)
		__sync_add_and_fetch(&g_live, 1);
	return p;
}

void __wrap_free(void *ptr)
{
	if (ptr != NULL)
		__sync_sub_and_fetch(&g_live, 1);
	__real_free(ptr);
}

/* ------------------------------------------------------------------ */
/* Deterministic systime seam                                          */
/* ------------------------------------------------------------------ */

int pthread_cond_wait_timeout(pthread_cond_t *condition,
			      pthread_mutex_t *mutex, unsigned int msecs)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	ts.tv_sec += (time_t)(msecs / 1000u);
	ts.tv_nsec += (long)(msecs % 1000u) * 1000000L;
	if (ts.tv_nsec >= 1000000000L) {
		ts.tv_sec += 1;
		ts.tv_nsec -= 1000000000L;
	}
	return pthread_cond_timedwait(condition, mutex, &ts);
}

/* Silence the unit's diagnostics. */
void media_utils_log_error(const char *reason, unsigned int detail)
{
	(void)reason;
	(void)detail;
}

/* ------------------------------------------------------------------ */
/* List inspection                                                     */
/* ------------------------------------------------------------------ */

#define WALK_MAX 4096

static int walk_count(const struct list_head *head)
{
	const struct list_head *p = head->next;
	int n = 0;

	while (p != head && n < WALK_MAX) {
		n++;
		p = p->next;
	}
	return n;
}

static int list_links_ok(const struct list_head *head)
{
	const struct list_head *p = head->next;
	int n = 0;

	if (head->prev->next != head || head->next->prev != head)
		return 0;
	while (p != head && n < WALK_MAX) {
		if (p->prev->next != p || p->next->prev != p)
			return 0;
		n++;
		p = p->next;
	}
	return p == head;
}

static int q_idle(const message_queue_t *q)
{
	return walk_count(&q->mIdleMessageList);
}

static int q_ready(const message_queue_t *q)
{
	return walk_count(&q->mReadyMessageList);
}

static int q_links_ok(const message_queue_t *q)
{
	return list_links_ok(&q->mIdleMessageList) &&
	       list_links_ok(&q->mReadyMessageList);
}

static int q_ready_commands(const message_queue_t *q, int *out, int max)
{
	const struct list_head *p = q->mReadyMessageList.next;
	int n = 0;

	while (p != &q->mReadyMessageList && n < max) {
		const message_t *m = container_of(p, message_t, mList);

		out[n++] = m->command;
		p = p->next;
	}
	return n;
}

static void make_msg(message_t *m, int command, int p0, int p1, void *data,
		     int size)
{
	memset(m, 0, sizeof(*m));
	m->command = command;
	m->para0 = p0;
	m->para1 = p1;
	m->mpData = data;
	m->mDataSize = size;
}

/* ------------------------------------------------------------------ */
/* ABI / layout facts (host-independent ordering)                      */
/* ------------------------------------------------------------------ */

static void test_abi(void)
{
	CHECK_EQ(MAX_MESSAGE_ELEMENTS, 8);
	CHECK_EQ(sizeof(((message_t *)0)->id), 4);
	CHECK_EQ(sizeof(((message_t *)0)->command), 4);
	CHECK_EQ(sizeof(((message_t *)0)->para0), 4);
	CHECK_EQ(sizeof(((message_t *)0)->para1), 4);
	CHECK_EQ(sizeof(((message_t *)0)->mDataSize), 4);

	/* Field order is ABI; assert the ordering, not the host size. */
	CHECK(offsetof(message_t, id) < offsetof(message_t, command));
	CHECK(offsetof(message_t, command) < offsetof(message_t, para0));
	CHECK(offsetof(message_t, para0) < offsetof(message_t, para1));
	CHECK(offsetof(message_t, para1) < offsetof(message_t, mpData));
	CHECK(offsetof(message_t, mpData) < offsetof(message_t, mDataSize));
	CHECK(offsetof(message_t, mDataSize) < offsetof(message_t, mList));

	CHECK(offsetof(message_queue_t, mIdleMessageList) <
	      offsetof(message_queue_t, mReadyMessageList));
	CHECK(offsetof(message_queue_t, mReadyMessageList) <
	      offsetof(message_queue_t, message_count));
	CHECK(offsetof(message_queue_t, message_count) <
	      offsetof(message_queue_t, mutex));
	CHECK(offsetof(message_queue_t, mutex) <
	      offsetof(message_queue_t, mCondMessageQueueChanged));
	CHECK(offsetof(message_queue_t, mCondMessageQueueChanged) <
	      offsetof(message_queue_t, mWaitMessageFlag));

#if __SIZEOF_POINTER__ == 4
	CHECK_EQ(sizeof(struct list_head), 8);
	CHECK_EQ(sizeof(message_t), 32);
#endif
}

/* ------------------------------------------------------------------ */
/* Scenario tests                                                      */
/* ------------------------------------------------------------------ */

static void test_create(void)
{
	message_queue_t q;

	memset(&q, 0, sizeof(q));
	CHECK_EQ(message_create(&q), SUCCESS);
	CHECK_EQ(q.message_count, 0);
	CHECK_EQ(q.mWaitMessageFlag, 0);
	CHECK_EQ(q_idle(&q), 8);
	CHECK_EQ(q_ready(&q), 0);
	CHECK(q_links_ok(&q));
	message_destroy(&q);
}

static void test_put_message_drops_payload(void)
{
	message_queue_t q;
	unsigned char src[4] = { 0xde, 0xad, 0xbe, 0xef };
	message_t in;
	message_t out;

	memset(&q, 0, sizeof(q));
	CHECK_EQ(message_create(&q), SUCCESS);

	make_msg(&in, 101, 7, 8, src, 4);
	CHECK_EQ(put_message(&q, &in), SUCCESS);
	CHECK_EQ(q.message_count, 1);
	CHECK_EQ(q_ready(&q), 1);
	CHECK_EQ(q_idle(&q), 7);

	/* The queued copy must not carry the payload. */
	src[0] = 0x00;
	memset(&out, 0, sizeof(out));
	CHECK_EQ(get_message(&q, &out), SUCCESS);
	CHECK_EQ(out.command, 101);
	CHECK_EQ(out.para0, 7);
	CHECK_EQ(out.para1, 8);
	CHECK(out.mpData == NULL);
	CHECK_EQ(out.mDataSize, 0);
	CHECK_EQ(q.message_count, 0);
	CHECK_EQ(q_ready(&q), 0);
	CHECK_EQ(q_idle(&q), 8);

	CHECK_EQ(get_message(&q, &out), FAILURE);
	message_destroy(&q);
}

static void test_put_message_with_data_deep_copy(void)
{
	message_queue_t q;
	unsigned char src[4] = { 0xaa, 0xbb, 0xcc, 0xdd };
	unsigned char original[4] = { 0xaa, 0xbb, 0xcc, 0xdd };
	message_t in;
	message_t out;

	memset(&q, 0, sizeof(q));
	CHECK_EQ(message_create(&q), SUCCESS);

	make_msg(&in, 1, 2, 3, src, 4);
	CHECK_EQ(putMessageWithData(&q, &in), SUCCESS);
	CHECK_EQ(q.message_count, 1);

	/* Mutating the source afterwards must not change the queued copy. */
	memset(src, 0x00, sizeof(src));

	memset(&out, 0, sizeof(out));
	CHECK_EQ(get_message(&q, &out), SUCCESS);
	CHECK_EQ(out.mDataSize, 4);
	CHECK(out.mpData != NULL);
	CHECK(out.mpData != (void *)src);
	CHECK(memcmp(out.mpData, original, 4) == 0);
	free(out.mpData);

	message_destroy(&q);
}

static void test_fifo_and_count(void)
{
	message_queue_t q;
	message_t in;
	message_t out;
	int i;

	memset(&q, 0, sizeof(q));
	CHECK_EQ(message_create(&q), SUCCESS);

	for (i = 1; i <= 3; i++) {
		make_msg(&in, i, i * 10, i * 100, NULL, 0);
		CHECK_EQ(put_message(&q, &in), SUCCESS);
		CHECK_EQ(q.message_count, i);
	}

	for (i = 1; i <= 3; i++) {
		memset(&out, 0, sizeof(out));
		CHECK_EQ(get_message(&q, &out), SUCCESS);
		CHECK_EQ(out.command, i);
		CHECK_EQ(out.para0, i * 10);
		CHECK_EQ(out.para1, i * 100);
		CHECK_EQ(q.message_count, 3 - i);
	}
	CHECK_EQ(get_message(&q, &out), FAILURE);
	message_destroy(&q);
}

static void test_idle_growth(void)
{
	message_queue_t q;
	message_t in;
	message_t out;
	int i;
	int cmds[16];

	memset(&q, 0, sizeof(q));
	CHECK_EQ(message_create(&q), SUCCESS);

	for (i = 0; i < 8; i++) {
		make_msg(&in, 200 + i, 0, 0, NULL, 0);
		CHECK_EQ(putMessageWithData(&q, &in), SUCCESS);
	}
	CHECK_EQ(q_idle(&q), 0);
	CHECK_EQ(q_ready(&q), 8);

	for (i = 8; i < 12; i++) {
		make_msg(&in, 200 + i, 0, 0, NULL, 0);
		CHECK_EQ(putMessageWithData(&q, &in), SUCCESS);
	}
	CHECK_EQ(q.message_count, 12);
	CHECK_EQ(q_ready(&q), 12);
	CHECK_EQ(q_idle(&q), 4);
	CHECK(q_links_ok(&q));

	CHECK_EQ(q_ready_commands(&q, cmds, 16), 12);
	for (i = 0; i < 12; i++) {
		CHECK_EQ(cmds[i], 200 + i);
		memset(&out, 0, sizeof(out));
		CHECK_EQ(get_message(&q, &out), SUCCESS);
		CHECK_EQ(out.command, 200 + i);
	}
	CHECK_EQ(q.message_count, 0);
	CHECK_EQ(q_idle(&q), 16);
	CHECK_EQ(q_ready(&q), 0);

	message_destroy(&q);
}

static void test_wait_immediate(void)
{
	message_queue_t q;
	message_t in;

	memset(&q, 0, sizeof(q));
	CHECK_EQ(message_create(&q), SUCCESS);

	make_msg(&in, 1, 0, 0, NULL, 0);
	CHECK_EQ(put_message(&q, &in), SUCCESS);
	make_msg(&in, 2, 0, 0, NULL, 0);
	CHECK_EQ(put_message(&q, &in), SUCCESS);

	/* Ready is non-empty: both timeout variants return immediately. */
	CHECK_EQ(TMessage_WaitQueueNotEmpty(&q, 5), 2);
	CHECK_EQ(q.mWaitMessageFlag, 0);
	CHECK_EQ(TMessage_WaitQueueNotEmpty(&q, 0), 2);
	CHECK_EQ(q.mWaitMessageFlag, 0);

	message_destroy(&q);
}

static long elapsed_ms(const struct timespec *a, const struct timespec *b)
{
	return (long)(b->tv_sec - a->tv_sec) * 1000L +
	       (long)(b->tv_nsec - a->tv_nsec) / 1000000L;
}

static void test_wait_timeout_expiry(void)
{
	message_queue_t q;
	struct timespec t0;
	struct timespec t1;
	long ms;

	memset(&q, 0, sizeof(q));
	CHECK_EQ(message_create(&q), SUCCESS);

	clock_gettime(CLOCK_MONOTONIC, &t0);
	CHECK_EQ(TMessage_WaitQueueNotEmpty(&q, 50), 0);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	ms = elapsed_ms(&t0, &t1);

	CHECK(ms >= 30);
	CHECK(ms < 2000);
	CHECK_EQ(q.mWaitMessageFlag, 0);
	message_destroy(&q);
}

struct helper {
	message_queue_t *q;
};

static void *producer(void *arg)
{
	struct helper *h = arg;
	message_t in;
	struct timespec ts;

	ts.tv_sec = 0;
	ts.tv_nsec = 30 * 1000000L;
	nanosleep(&ts, NULL);

	make_msg(&in, 9, 1, 2, NULL, 0);
	put_message(h->q, &in);
	return NULL;
}

static void test_wait_unblocks(void)
{
	message_queue_t q;
	struct helper h;
	pthread_t tid;
	message_t out;

	memset(&q, 0, sizeof(q));
	CHECK_EQ(message_create(&q), SUCCESS);
	h.q = &q;

	CHECK_EQ(pthread_create(&tid, NULL, producer, &h), 0);
	CHECK_EQ(TMessage_WaitQueueNotEmpty(&q, 2000), 1);
	CHECK_EQ(pthread_join(tid, NULL), 0);
	CHECK_EQ(q.mWaitMessageFlag, 0);

	memset(&out, 0, sizeof(out));
	CHECK_EQ(get_message(&q, &out), SUCCESS);
	CHECK_EQ(out.command, 9);
	message_destroy(&q);
}

static void test_wait_blocks_indefinitely(void)
{
	message_queue_t q;
	struct helper h;
	pthread_t tid;
	message_t out;

	memset(&q, 0, sizeof(q));
	CHECK_EQ(message_create(&q), SUCCESS);
	h.q = &q;

	CHECK_EQ(pthread_create(&tid, NULL, producer, &h), 0);
	CHECK_EQ(TMessage_WaitQueueNotEmpty(&q, 0), 1);
	CHECK_EQ(pthread_join(tid, NULL), 0);

	memset(&out, 0, sizeof(out));
	CHECK_EQ(get_message(&q, &out), SUCCESS);
	CHECK_EQ(out.command, 9);
	message_destroy(&q);
}

static void test_destroy_releases(void)
{
	message_queue_t q;
	message_t in;
	unsigned char payloads[3][4] = { { 1, 2, 3, 4 },
					 { 5, 6, 7, 8 },
					 { 9, 10, 11, 12 } };
	long live_before;
	int i;

	live_before = g_live;
	memset(&q, 0, sizeof(q));
	CHECK_EQ(message_create(&q), SUCCESS);

	for (i = 0; i < 3; i++) {
		make_msg(&in, i, 0, 0, payloads[i], 4);
		CHECK_EQ(putMessageWithData(&q, &in), SUCCESS);
	}
	CHECK_EQ(q.message_count, 3);

	message_destroy(&q);
	CHECK_EQ(q.message_count, 0);
	CHECK(list_empty(&q.mIdleMessageList));
	CHECK(list_empty(&q.mReadyMessageList));

	/* All nodes and payloads were freed. */
	CHECK_EQ(g_live, live_before);
}

static void test_create_alloc_failure(void)
{
	message_queue_t q;
	long live_before = g_live;
	long calls_before = g_calls;
	int ret;

	memset(&q, 0, sizeof(q));
	g_fail_at = calls_before + 3; /* third node allocation fails */
	ret = message_create(&q);
	g_fail_at = 0;

	CHECK_EQ(ret, FAILURE);
	/* The two nodes created before the failure were unwound. */
	CHECK_EQ(g_live, live_before);
}

int main(void)
{
	test_abi();
	test_create();
	test_put_message_drops_payload();
	test_put_message_with_data_deep_copy();
	test_fifo_and_count();
	test_idle_growth();
	test_wait_immediate();
	test_wait_timeout_expiry();
	test_wait_unblocks();
	test_wait_blocks_indefinitely();
	test_destroy_releases();
	test_create_alloc_failure();

	/* Every successful allocation in the whole suite was released. */
	CHECK_EQ(g_live, 0);

	if (fails) {
		printf("test_msgqueue: %d check(s) failed\n", fails);
		return 1;
	}
	printf("test_msgqueue: %d checks, 0 failures\n", checks);
	return 0;
}
