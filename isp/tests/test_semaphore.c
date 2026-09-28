/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_semaphore.c - host tests for cdx_sem_* (spec media_utils/semaphore.md
 * 5); the blocking path uses a helper thread and join, all waits bounded. */
#define _GNU_SOURCE
#include "utils/semaphore.h"

#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
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
		long long va_ = (long long)(a);                           \
		long long vb_ = (long long)(b);                           \
		checks++;                                                 \
		if (va_ != vb_) {                                         \
			printf("FAIL %s:%d: %s=%lld expected %s=%lld\n",  \
			       __FILE__, __LINE__, #a, va_, #b, vb_);     \
			fails++;                                          \
		}                                                         \
	} while (0)

static int64_t mono_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000LL +
	       (int64_t)ts.tv_nsec / 1000LL;
}

/* ------------------------------------------------------------------ */
/* ABI facts (target is 32-bit; host is usually 64-bit)                */
/* ------------------------------------------------------------------ */

static void test_abi(void)
{
	CHECK_EQ(offsetof(cdx_sem_t, condition), 0);
	CHECK(offsetof(cdx_sem_t, semval) > offsetof(cdx_sem_t, condition));

#if __SIZEOF_POINTER__ == 4
	CHECK_EQ(sizeof(cdx_sem_t), 76);
	CHECK_EQ(offsetof(cdx_sem_t, semval), 72);
#endif
}

/* ------------------------------------------------------------------ */
/* Init / up / down                                                    */
/* ------------------------------------------------------------------ */

static void test_init_and_up_down(void)
{
	cdx_sem_t sem;

	CHECK_EQ(cdx_sem_init(&sem, 0), 0);
	CHECK_EQ(sem.semval, 0);

	cdx_sem_up(&sem);
	CHECK_EQ(sem.semval, 1);

	cdx_sem_down(&sem);
	CHECK_EQ(sem.semval, 0);

	cdx_sem_deinit(&sem);
}

static void test_init_value_n(void)
{
	cdx_sem_t sem;

	CHECK_EQ(cdx_sem_init(&sem, 7), 0);
	CHECK_EQ(sem.semval, 7);

	cdx_sem_down(&sem);
	CHECK_EQ(sem.semval, 6);
	cdx_sem_down(&sem);
	CHECK_EQ(sem.semval, 5);

	cdx_sem_up(&sem);
	cdx_sem_up(&sem);
	CHECK_EQ(sem.semval, 7);

	cdx_sem_deinit(&sem);
}

/* ------------------------------------------------------------------ */
/* Timed wait                                                          */
/* ------------------------------------------------------------------ */

static void test_timedwait_empty(void)
{
	cdx_sem_t sem;
	int64_t t0;
	int64_t elapsed;
	int rc;

	CHECK_EQ(cdx_sem_init(&sem, 0), 0);

	t0 = mono_us();
	rc = cdx_sem_down_timedwait(&sem, 50);
	elapsed = mono_us() - t0;

	CHECK_EQ(rc, ETIMEDOUT);
	CHECK(elapsed >= 50000);
	CHECK(elapsed < 5000000);
	/* A timed-out wait must not consume a token. */
	CHECK_EQ(sem.semval, 0);

	cdx_sem_deinit(&sem);
}

static void test_timedwait_ready(void)
{
	cdx_sem_t sem;
	int64_t t0;
	int64_t elapsed;
	int rc;

	CHECK_EQ(cdx_sem_init(&sem, 1), 0);

	t0 = mono_us();
	rc = cdx_sem_down_timedwait(&sem, 2000);
	elapsed = mono_us() - t0;

	/* Non-zero: no wait, immediate success, token consumed. */
	CHECK_EQ(rc, 0);
	CHECK(elapsed < 1000000);
	CHECK_EQ(sem.semval, 0);

	cdx_sem_deinit(&sem);
}

/* A timed wait that loses the race must not go negative. */
static void test_timedwait_after_up(void)
{
	cdx_sem_t sem;
	int rc;

	CHECK_EQ(cdx_sem_init(&sem, 0), 0);
	cdx_sem_up(&sem);
	CHECK_EQ(sem.semval, 1);

	rc = cdx_sem_down_timedwait(&sem, 100);
	CHECK_EQ(rc, 0);
	CHECK_EQ(sem.semval, 0);

	cdx_sem_deinit(&sem);
}

/* ------------------------------------------------------------------ */
/* Blocking down released by a helper thread's up                      */
/* ------------------------------------------------------------------ */

static void *up_after_delay(void *p)
{
	cdx_sem_t *sem = (cdx_sem_t *)p;
	struct timespec sleep_for;

	sleep_for.tv_sec = 0;
	sleep_for.tv_nsec = 50L * 1000000L;
	nanosleep(&sleep_for, NULL);
	cdx_sem_up(sem);
	return NULL;
}

static void test_down_blocks_then_up(void)
{
	cdx_sem_t sem;
	pthread_t helper;
	int64_t t0;
	int64_t elapsed;

	CHECK_EQ(cdx_sem_init(&sem, 0), 0);
	CHECK_EQ(pthread_create(&helper, NULL, up_after_delay, &sem), 0);

	t0 = mono_us();
	cdx_sem_down(&sem);
	elapsed = mono_us() - t0;

	/* It must have been released by the helper's up, not by a spin. */
	CHECK(elapsed >= 40000);
	CHECK(elapsed < 5000000);
	CHECK_EQ(sem.semval, 0);

	CHECK_EQ(pthread_join(helper, NULL), 0);
	cdx_sem_deinit(&sem);
}

int main(void)
{
	test_abi();
	test_init_and_up_down();
	test_init_value_n();
	test_timedwait_empty();
	test_timedwait_ready();
	test_timedwait_after_up();
	test_down_blocks_then_up();

	if (fails) {
		printf("test_semaphore: %d check(s) failed\n", fails);
		return 1;
	}
	printf("test_semaphore: %d checks, 0 failures\n", checks);
	return 0;
}
