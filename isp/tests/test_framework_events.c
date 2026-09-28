/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_framework_events.c - host tests for the private isp_loop_* event loop:
 * watch bookkeeping, maxfd, dedup, dispatch order, stop, EINTR and timeout. */
#define _POSIX_C_SOURCE 200809L

#include "framework_internal.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>

/* events.c is linked whole, so its other handlers' symbols need these
 * minimal stand-ins; none are exercised here. */
struct hw_isp_media_dev media_params;

static int test_select(int nfds, fd_set *r, fd_set *w, fd_set *e,
                       struct timeval *tmo)
{
    return select(nfds, r, w, e, tmo);
}

static const struct fwi_uapi_sys g_test_sys = { .select = test_select };
const struct fwi_uapi_sys *isp_uapi_sys = &g_test_sys;

void isp_handle_ctrl_event(fwi_isp_ctx_t *ctx, const struct v4l2_event *ev)
{
    (void)ctx;
    (void)ev;
}

void isp_handle_frame_sync(fwi_isp_ctx_t *ctx, const uint8_t *data)
{
    (void)ctx;
    (void)data;
}

void isp_frame_process(fwi_isp_ctx_t *ctx)
{
    (void)ctx;
}

static int g_checks;
static int g_failures;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            printf("FAIL line %d: %s\n", __LINE__, (msg));                   \
        }                                                                    \
    } while (0)

#define CHECK_EQ(actual, expected, msg)                                      \
    do {                                                                     \
        long a_ = (long)(actual);                                            \
        long e_ = (long)(expected);                                          \
        g_checks++;                                                         \
        if (a_ != e_) {                                                      \
            g_failures++;                                                   \
            printf("FAIL line %d: %s (got %ld, want %ld)\n", __LINE__,       \
                   (msg), a_, e_);                                           \
        }                                                                    \
    } while (0)

/* ------------------------------------------------------------------ */
/* Dispatch logging                                                    */
/* ------------------------------------------------------------------ */

static struct fwi_event_loop *g_loop;
static int g_hits;
static int g_order[16];

static void hit(int id)
{
    if (g_hits < 16)
        g_order[g_hits] = id;
    g_hits++;
}

static void cb_record(void *priv)
{
    hit((int)(long)priv);
}

static void cb_record_stop(void *priv)
{
    hit((int)(long)priv);
    isp_loop_stop(g_loop);
}

static void cb_stop(void *priv)
{
    (void)priv;
    g_hits++;
    isp_loop_stop(g_loop);
}

/* index of fd in the watch array, or -1 */
static int watch_index(const struct fwi_event_loop *l, int fd)
{
    int i;

    for (i = 0; i < l->nwatch; i++)
        if (l->watch[i].fd == fd)
            return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Signals (EINTR)                                                     */
/* ------------------------------------------------------------------ */

static volatile sig_atomic_t g_alarm_count;
static int g_intr_write_fd;

static void on_alarm(int sig)
{
    char c = 'x';
    ssize_t r;

    (void)sig;
    g_alarm_count++;
    r = write(g_intr_write_fd, &c, 1);
    (void)r;
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_init(void)
{
    struct fwi_event_loop l;

    memset(&l, 0xAA, sizeof(l));
    isp_loop_init(&l);

    CHECK_EQ(l.done, 0, "init done false");
    CHECK_EQ(l.started, 0, "init started false");
    CHECK_EQ(l.nwatch, 0, "init nwatch zero");
    CHECK_EQ(l.maxfd, -1, "init maxfd -1 (no watched fd)");
}

static void test_watch_unwatch(void)
{
    struct fwi_event_loop l;
    int p1[2];
    int p2[2];
    int expect_max;
    int max_before;

    if (pipe(p1) != 0 || pipe(p2) != 0) {
        CHECK(0, "pipe");
        return;
    }

    isp_loop_init(&l);
    isp_loop_watch(&l, p1[0], ISP_WATCH_READ, cb_record, (void *)1L);
    CHECK_EQ(l.maxfd, p1[0], "maxfd after read watch");
    CHECK_EQ(l.nwatch, 1, "one entry after read watch");
    CHECK_EQ(l.watch[watch_index(&l, p1[0])].kind, ISP_WATCH_READ,
             "read fd recorded as read kind");

    isp_loop_watch(&l, p2[0], ISP_WATCH_WRITE, cb_record, (void *)2L);
    expect_max = p1[0] > p2[0] ? p1[0] : p2[0];
    CHECK_EQ(l.maxfd, expect_max, "maxfd after write watch");
    CHECK_EQ(l.watch[watch_index(&l, p2[0])].kind, ISP_WATCH_WRITE,
             "write fd recorded as write kind");

    isp_loop_watch(&l, p2[1], ISP_WATCH_EXCEPT, cb_record, (void *)3L);
    expect_max = expect_max > p2[1] ? expect_max : p2[1];
    CHECK_EQ(l.maxfd, expect_max, "maxfd after except watch");
    CHECK_EQ(l.watch[watch_index(&l, p2[1])].kind, ISP_WATCH_EXCEPT,
             "except fd recorded as except kind");
    CHECK_EQ(l.nwatch, 3, "three entries");

    isp_loop_unwatch(&l, p1[0]);
    CHECK_EQ(l.maxfd, p2[1] > p2[0] ? p2[1] : p2[0], "maxfd after unwatch");
    CHECK(watch_index(&l, p1[0]) < 0, "unwatched fd removed");
    CHECK_EQ(l.nwatch, 2, "two entries after unwatch");

    max_before = l.maxfd;
    isp_loop_unwatch(&l, 9999);
    CHECK_EQ(l.maxfd, max_before, "unknown unwatch leaves maxfd");
    CHECK_EQ(l.nwatch, 2, "unknown unwatch leaves list");

    isp_loop_unwatch(&l, p2[0]);
    isp_loop_unwatch(&l, p2[1]);
    CHECK_EQ(l.maxfd, -1, "maxfd -1 when emptied");
    CHECK_EQ(l.nwatch, 0, "list empty when all removed");

    close(p1[0]);
    close(p1[1]);
    close(p2[0]);
    close(p2[1]);
}

static void test_duplicate_watch_updates_in_place(void)
{
    struct fwi_event_loop l;
    int p[2];
    int q[2];

    if (pipe(p) != 0 || pipe(q) != 0) {
        CHECK(0, "pipe");
        return;
    }

    isp_loop_init(&l);
    isp_loop_watch(&l, p[0], ISP_WATCH_READ, cb_record, (void *)1L);
    /* re-watching the same fd updates its entry in place rather than
     * appending a duplicate (isp_loop_watch's own dedup, events.c). */
    isp_loop_watch(&l, p[0], ISP_WATCH_WRITE, cb_record, (void *)2L);
    isp_loop_watch(&l, q[0], ISP_WATCH_READ, cb_record, (void *)3L);
    CHECK_EQ(l.nwatch, 2, "re-watch updates in place, does not grow");
    CHECK_EQ(l.watch[watch_index(&l, p[0])].kind, ISP_WATCH_WRITE,
             "re-watch replaced the kind");
    CHECK_EQ(l.maxfd, p[0] > q[0] ? p[0] : q[0], "duplicate maxfd all");

    isp_loop_unwatch(&l, p[0]);
    CHECK_EQ(l.maxfd, q[0], "unwatch after re-watch leaves other fd");
    CHECK_EQ(l.nwatch, 1, "one entry left");

    isp_loop_unwatch(&l, q[0]);
    CHECK_EQ(l.maxfd, -1, "maxfd -1 when emptied");
    CHECK_EQ(l.nwatch, 0, "list empty");

    close(p[0]);
    close(p[1]);
    close(q[0]);
    close(q[1]);
}

static void test_dispatch_order(void)
{
    struct fwi_event_loop l;
    int pa[2];
    int pb[2];
    int sv[2];
    char c = 'a';

    if (pipe(pa) != 0 || pipe(pb) != 0) {
        CHECK(0, "pipe");
        return;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        CHECK(0, "socketpair");
        return;
    }
    if (write(pa[1], &c, 1) != 1) {
        CHECK(0, "prime readable pipe");
        return;
    }
    if (send(sv[0], "o", 1, MSG_OOB) != 1) {
        CHECK(0, "send OOB");
        return;
    }

    isp_loop_init(&l);
    isp_loop_start(&l);
    g_loop = &l;
    g_hits = 0;

    isp_loop_watch(&l, pa[0], ISP_WATCH_READ, cb_record, (void *)1L);
    isp_loop_watch(&l, pb[1], ISP_WATCH_WRITE, cb_record, (void *)2L);
    isp_loop_watch(&l, sv[1], ISP_WATCH_EXCEPT, cb_record_stop, (void *)3L);

    CHECK_EQ(isp_loop_run(&l), false, "run false after dispatch stop");
    CHECK_EQ(g_hits, 3, "all three callbacks dispatch");
    CHECK_EQ(g_order[0], 1, "read dispatched first");
    CHECK_EQ(g_order[1], 2, "write dispatched second");
    CHECK_EQ(g_order[2], 3, "except dispatched third");
    CHECK_EQ(l.done, true, "done set by callback stop");

    close(pa[0]);
    close(pa[1]);
    close(pb[0]);
    close(pb[1]);
    close(sv[0]);
    close(sv[1]);
}

static void test_mixed_readiness(void)
{
    struct fwi_event_loop l;
    int pa[2];
    int pb[2];
    int pw[2];
    char c = 'a';

    if (pipe(pa) != 0 || pipe(pb) != 0 || pipe(pw) != 0) {
        CHECK(0, "pipe");
        return;
    }
    if (write(pa[1], &c, 1) != 1) {
        CHECK(0, "prime readable pipe");
        return;
    }

    isp_loop_init(&l);
    isp_loop_start(&l);
    g_loop = &l;
    g_hits = 0;

    isp_loop_watch(&l, pa[0], ISP_WATCH_READ, cb_record, (void *)1L);
    isp_loop_watch(&l, pb[0], ISP_WATCH_READ, cb_record, (void *)2L);
    isp_loop_watch(&l, pw[1], ISP_WATCH_WRITE, cb_record_stop, (void *)3L);

    CHECK_EQ(isp_loop_run(&l), false, "run false after mixed stop");
    CHECK_EQ(g_hits, 2, "only ready descriptors dispatched");
    CHECK_EQ(g_order[0], 1, "ready read dispatched");
    CHECK_EQ(g_order[1], 3, "ready write dispatched");
    CHECK(watch_index(&l, pb[0]) >= 0, "unready fd still watched");

    close(pa[0]);
    close(pa[1]);
    close(pb[0]);
    close(pb[1]);
    close(pw[0]);
    close(pw[1]);
}

static void test_stop_halts_dispatch(void)
{
    struct fwi_event_loop l;
    int pa[2];
    int pb[2];
    char c = 'a';

    if (pipe(pa) != 0 || pipe(pb) != 0) {
        CHECK(0, "pipe");
        return;
    }
    if (write(pa[1], &c, 1) != 1) {
        CHECK(0, "prime readable pipe");
        return;
    }

    isp_loop_init(&l);
    isp_loop_start(&l);
    g_loop = &l;
    g_hits = 0;

    isp_loop_watch(&l, pa[0], ISP_WATCH_READ, cb_stop, NULL);
    isp_loop_watch(&l, pb[1], ISP_WATCH_WRITE, cb_record, (void *)2L);

    CHECK_EQ(isp_loop_run(&l), false, "run false after early stop");
    CHECK_EQ(g_hits, 1, "stop halts remaining dispatch");

    close(pa[0]);
    close(pa[1]);
    close(pb[0]);
    close(pb[1]);
}

static void test_eintr_retry(void)
{
    struct fwi_event_loop l;
    int p[2];
    struct sigaction sa;
    struct sigaction old;
    char drain;

    if (pipe(p) != 0) {
        CHECK(0, "pipe");
        return;
    }

    isp_loop_init(&l);
    isp_loop_start(&l);
    g_loop = &l;
    g_hits = 0;
    g_alarm_count = 0;
    g_intr_write_fd = p[1];

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_alarm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    CHECK_EQ(sigaction(SIGALRM, &sa, &old), 0, "sigaction install");

    isp_loop_watch(&l, p[0], ISP_WATCH_READ, cb_record_stop, (void *)7L);

    alarm(1);
    CHECK_EQ(isp_loop_run(&l), false, "run false after EINTR then stop");
    alarm(0);
    sigaction(SIGALRM, &old, NULL);

    CHECK(g_alarm_count >= 1, "alarm fired");
    CHECK_EQ(g_hits, 1, "EINTR retried then dispatched");
    CHECK_EQ(g_order[0], 7, "EINTR dispatch id");

    if (read(p[0], &drain, 1) < 0) {
        /* nothing to drain */
    }

    close(p[0]);
    close(p[1]);
}

static void test_timeout_leaves_done(void)
{
    struct fwi_event_loop l;

    isp_loop_init(&l);
    isp_loop_start(&l);
    CHECK_EQ(isp_loop_run(&l), true, "timeout returns true without stop");
    CHECK_EQ(l.done, false, "timeout leaves done false");
}

static void test_select_error_leaves_done(void)
{
    struct fwi_event_loop l;
    int p[2];

    if (pipe(p) != 0) {
        CHECK(0, "pipe");
        return;
    }

    isp_loop_init(&l);
    isp_loop_start(&l);
    isp_loop_watch(&l, p[0], ISP_WATCH_READ, cb_record, (void *)1L);
    close(p[0]);
    close(p[1]);

    CHECK_EQ(isp_loop_run(&l), true, "select error returns true without stop");
    CHECK_EQ(l.done, false, "select error leaves done false");
}

static void test_start_stop(void)
{
    struct fwi_event_loop l;

    isp_loop_init(&l);
    isp_loop_stop(&l);
    CHECK_EQ(l.done, true, "stop sets done");
    isp_loop_start(&l);
    CHECK_EQ(l.done, false, "start clears done");
    CHECK_EQ(l.started, true, "start sets started");
}

int main(void)
{
    test_init();
    test_watch_unwatch();
    test_duplicate_watch_updates_in_place();
    test_dispatch_order();
    test_mixed_readiness();
    test_stop_halts_dispatch();
    test_eintr_retry();
    test_timeout_leaves_done();
    test_select_error_leaves_done();
    test_start_stop();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
