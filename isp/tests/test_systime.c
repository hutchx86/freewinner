/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_systime.c - host tests for the time unit (spec media_utils/systime.md
 * 4): bounded timeout, signal via helper thread, wall-clock helpers. */
#define _GNU_SOURCE
#include "utils/systime.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
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
		long long va_ = (long long)(a);                           \
		long long vb_ = (long long)(b);                           \
		checks++;                                                 \
		if (va_ != vb_) {                                         \
			printf("FAIL %s:%d: %s=%lld expected %s=%lld\n",  \
			       __FILE__, __LINE__, #a, va_, #b, vb_);     \
			fails++;                                          \
		}                                                         \
	} while (0)

/* Monotonic microseconds, used only to bound the waits. */
static int64_t mono_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000LL +
	       (int64_t)ts.tv_nsec / 1000LL;
}

/* Condition variables must share the caller's monotonic clock. */
static void cond_init_monotonic(pthread_cond_t *c)
{
	pthread_condattr_t attr;

	pthread_condattr_init(&attr);
	pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
	pthread_cond_init(c, &attr);
	pthread_condattr_destroy(&attr);
}

/* ------------------------------------------------------------------ */
/* Timeout path                                                        */
/* ------------------------------------------------------------------ */

static void test_timeout_expiry(void)
{
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	int64_t t0;
	int64_t elapsed;
	int rc;

	cond_init_monotonic(&cond);
	pthread_mutex_init(&mutex, NULL);

	pthread_mutex_lock(&mutex);
	t0 = mono_us();
	rc = pthread_cond_wait_timeout(&cond, &mutex, 50);
	elapsed = mono_us() - t0;
	pthread_mutex_unlock(&mutex);

	CHECK_EQ(rc, ETIMEDOUT);
	/* At least the requested interval, but not an unbounded hang. */
	CHECK(elapsed >= 50000);
	CHECK(elapsed < 5000000);

	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cond);
}

/* Zero milliseconds is still a timeout of the current instant. */
static void test_timeout_zero(void)
{
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	int64_t t0;
	int64_t elapsed;
	int rc;

	cond_init_monotonic(&cond);
	pthread_mutex_init(&mutex, NULL);

	pthread_mutex_lock(&mutex);
	t0 = mono_us();
	rc = pthread_cond_wait_timeout(&cond, &mutex, 0);
	elapsed = mono_us() - t0;
	pthread_mutex_unlock(&mutex);

	CHECK_EQ(rc, ETIMEDOUT);
	CHECK(elapsed < 2000000);

	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cond);
}

/* ------------------------------------------------------------------ */
/* Signal path                                                         */
/* ------------------------------------------------------------------ */

struct signal_arg {
	pthread_cond_t *cond;
	unsigned int delay_ms;
};

static void *signal_after_delay(void *p)
{
	struct signal_arg *arg = (struct signal_arg *)p;
	struct timespec sleep_for;

	sleep_for.tv_sec = (time_t)(arg->delay_ms / 1000u);
	sleep_for.tv_nsec =
		(long)(arg->delay_ms % 1000u) * 1000000L;
	nanosleep(&sleep_for, NULL);
	pthread_cond_signal(arg->cond);
	return NULL;
}

static void test_signal_wakes(void)
{
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	pthread_t helper;
	struct signal_arg arg;
	int64_t t0;
	int64_t elapsed;
	int rc;

	cond_init_monotonic(&cond);
	pthread_mutex_init(&mutex, NULL);

	arg.cond = &cond;
	arg.delay_ms = 50;
	CHECK_EQ(pthread_create(&helper, NULL, signal_after_delay, &arg), 0);

	pthread_mutex_lock(&mutex);
	t0 = mono_us();
	rc = pthread_cond_wait_timeout(&cond, &mutex, 5000);
	elapsed = mono_us() - t0;
	pthread_mutex_unlock(&mutex);

	/* A helper wakeup at ~50 ms must return 0 well before the deadline. */
	CHECK_EQ(rc, 0);
	CHECK(elapsed < 4000000);

	CHECK_EQ(pthread_join(helper, NULL), 0);

	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cond);
}

/* ------------------------------------------------------------------ */
/* Wall clock                                                          */
/* ------------------------------------------------------------------ */

static void test_gettime(void)
{
	int64_t a;
	int64_t b;
	struct timespec wall;
	int64_t wall_us;
	int64_t delta;

	a = CDX_GetTimeUs();
	b = CDX_GetTimeUs();
	CHECK(b >= a);

	clock_gettime(CLOCK_REALTIME, &wall);
	wall_us = (int64_t)wall.tv_sec * 1000000LL +
		  (int64_t)wall.tv_nsec / 1000LL;
	delta = wall_us - a;
	if (delta < 0)
		delta = -delta;
	/* Same wall clock, within a generous second. */
	CHECK(delta <= 1000000);
}

static void test_settime(void)
{
	int64_t now;
	int64_t ret;
	int64_t back;

	/* CDX_SetTimeUs never mutates the wall clock and returns the current time,
	 * so a far-past request must leave the clock alone. */
	now = CDX_GetTimeUs();
	ret = CDX_SetTimeUs(1000000);
	back = CDX_GetTimeUs();
	CHECK(ret >= now);
	CHECK(back >= now);
	CHECK(back - now <= 1000000);
}

int main(void)
{
	test_timeout_expiry();
	test_timeout_zero();
	test_signal_wakes();
	test_gettime();
	test_settime();

	if (fails) {
		printf("test_systime: %d check(s) failed\n", fails);
		return 1;
	}
	printf("test_systime: %d checks, 0 failures\n", checks);
	return 0;
}
