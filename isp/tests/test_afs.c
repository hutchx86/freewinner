/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * test_afs.c - host unit tests for the clean-room AFS module.
 */
#include "afs_clean.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
        g_checks++;                                                          \
        if (a_ != e_) {                                                      \
            g_failures++;                                                    \
            printf("FAIL line %d: %s (got %ld, want %ld)\n", __LINE__,       \
                   (msg), a_, e_);                                           \
        }                                                                    \
    } while (0)

/* ------------------------------------------------------------------ */
/* Injected trig tables (built from the documented semantics, not      */
/* hard-coded constants: they are supplied to the module at runtime).  */
/* ------------------------------------------------------------------ */

static int g_sin[AFS_CLEAN_NTRIG];
static int g_cos[AFS_CLEAN_NTRIG];
static const afs_clean_trig_t g_trig = { g_sin, g_cos };

static const afs_clean_trig_t *trig_provider(void)
{
    return &g_trig;
}

static const afs_clean_trig_t *null_provider(void)
{
    return NULL;
}

static void build_trig_tables(void)
{
    int m;

    for (m = 0; m < AFS_CLEAN_NTRIG; m++) {
        double angle = 2.0 * 3.14159265358979323846 * (double)m /
                       (double)AFS_CLEAN_NTRIG;
        g_sin[m] = (int)lround(100.0 * sin(angle));
        g_cos[m] = (int)lround(100.0 * cos(angle));
    }
}

/* ------------------------------------------------------------------ */
/* Synthetic statistics                                               */
/* ------------------------------------------------------------------ */

#define BAND_FREQ 6

static unsigned int g_cols[AFS_CLEAN_NCOL];

static void make_flat_columns(void)
{
    int c;

    for (c = 0; c < AFS_CLEAN_NCOL; c++)
        g_cols[c] = 200u;
}

/* A per-column-phase banding pattern, one row's worth. */
static void make_band_row(int row)
{
    int c;

    for (c = 0; c < AFS_CLEAN_NCOL; c++) {
        double phi = 2.0 * 3.14159265358979323846 * (double)c /
                     (double)AFS_CLEAN_NCOL;
        double v = 200.0 + 100.0 * cos(2.0 * 3.14159265358979323846 *
                                       (double)BAND_FREQ * (double)row / 16.0 +
                                       phi);
        g_cols[c] = (unsigned int)lround(v);
    }
}

static void params_defaults(afs_clean_params_t *p)
{
    memset(p, 0, sizeof(*p));
    p->mode = AFS_CLEAN_MODE_AUTO;
    p->seed_type = AFS_CLEAN_SEED_ALTERNATE;
    p->min_peak_ratio = 50;
    p->enable = 1;
    p->gain_level = 256;
    p->image_width = 1;
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_init_requires_tables(void)
{
    afs_clean_ops_t const *ops = (const afs_clean_ops_t *)0x1;
    afs_clean_state_t *s;

    afs_clean_set_trig_provider(null_provider);
    s = afs_clean_init(&ops);
    CHECK(s == NULL, "init fails with no trig tables");
    CHECK(ops == NULL, "failed init clears ops");

    afs_clean_set_trig_provider(trig_provider);
    s = afs_clean_init(&ops);
    CHECK(s != NULL, "init succeeds with trig tables");
    CHECK(ops != NULL, "init returns ops vtable");
    afs_clean_exit(s);
}

static void test_lifecycle_and_params(void)
{
    const afs_clean_ops_t *ops = NULL;
    afs_clean_state_t *s;
    afs_clean_params_t *p = NULL;
    int rc;

    afs_clean_set_trig_provider(trig_provider);
    s = afs_clean_init(&ops);
    CHECK(s != NULL, "init");
    CHECK(ops != NULL, "ops");
    if (s == NULL)
        return;

    CHECK_EQ(afs_clean_rows_filled(s), 0, "fresh rows_filled");
    CHECK_EQ(afs_clean_get_params(NULL, &p), -1, "get_params(NULL)");
    CHECK_EQ(afs_clean_get_params(s, &p), 0, "get_params");
    CHECK(p != NULL, "params pointer");
    if (p != NULL) {
        CHECK_EQ(p->frame_index, 0, "params zeroed frame_index");
        CHECK_EQ(p->mode, 0, "params zeroed mode");
        CHECK_EQ(p->enable, 0, "params zeroed enable");
    }

    rc = afs_clean_set_params(s, NULL, NULL);
    CHECK_EQ(rc, 0, "set_params no-op returns 0");

    afs_clean_exit(s);
}

static void test_warmup_frames_ignored(void)
{
    const afs_clean_ops_t *ops = NULL;
    afs_clean_state_t *s = afs_clean_init(&ops);
    afs_clean_params_t *p = NULL;
    afs_clean_stats_t stats;
    afs_clean_result_t res;
    int fi;

    if (s == NULL) {
        CHECK(0, "init");
        return;
    }
    make_band_row(0);
    stats.column_sum = g_cols;
    stats.stats_width = AFS_CLEAN_NCOL;
    stats.stats_height = AFS_CLEAN_NROW;

    afs_clean_get_params(s, &p);
    params_defaults(p);
    p->mode = AFS_CLEAN_MODE_FORCE60;

    for (fi = 0; fi < 3; fi++) {
        p->frame_index = fi;
        res.detected_type = 0x7FFFFFFF;
        afs_clean_run(s, &stats, &res);
        CHECK_EQ(res.detected_type, 0x7FFFFFFF, "warmup leaves result");
    }
    CHECK_EQ(afs_clean_rows_filled(s), 0, "warmup captures nothing");
    afs_clean_exit(s);
}

static void test_mode_dispatch(void)
{
    const afs_clean_ops_t *ops = NULL;
    afs_clean_state_t *s = afs_clean_init(&ops);
    afs_clean_params_t *p = NULL;
    afs_clean_stats_t stats;
    afs_clean_result_t res;

    if (s == NULL) {
        CHECK(0, "init");
        return;
    }
    make_flat_columns();
    stats.column_sum = g_cols;
    stats.stats_width = AFS_CLEAN_NCOL;
    stats.stats_height = AFS_CLEAN_NROW;

    afs_clean_get_params(s, &p);
    params_defaults(p);
    p->frame_index = 3;
    p->enable = 0;

    p->mode = AFS_CLEAN_MODE_OFF;
    res.detected_type = -1;
    afs_clean_run(s, &stats, &res);
    CHECK_EQ(res.detected_type, AFS_CLEAN_TYPE_NONE, "mode off");

    p->mode = AFS_CLEAN_MODE_FORCE50;
    res.detected_type = -1;
    afs_clean_run(s, &stats, &res);
    CHECK_EQ(res.detected_type, AFS_CLEAN_TYPE_HZ50, "mode force50");

    p->mode = AFS_CLEAN_MODE_FORCE60;
    res.detected_type = -1;
    afs_clean_run(s, &stats, &res);
    CHECK_EQ(res.detected_type, AFS_CLEAN_TYPE_HZ60, "mode force60");

    afs_clean_exit(s);
}

static void test_auto_seed_clamping(void)
{
    static const int seeds[] = { -5, 0, 1, 2, 7 };
    static const int want[] = { AFS_CLEAN_TYPE_HZ50, AFS_CLEAN_TYPE_HZ50,
                                AFS_CLEAN_TYPE_HZ50, AFS_CLEAN_TYPE_HZ60,
                                AFS_CLEAN_TYPE_HZ60 };
    size_t i;

    for (i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        const afs_clean_ops_t *ops = NULL;
        afs_clean_state_t *s = afs_clean_init(&ops);
        afs_clean_params_t *p = NULL;
        afs_clean_stats_t stats;
        afs_clean_result_t res;

        if (s == NULL) {
            CHECK(0, "init");
            return;
        }
        make_flat_columns();
        stats.column_sum = g_cols;
        stats.stats_width = AFS_CLEAN_NCOL;
        stats.stats_height = AFS_CLEAN_NROW;

        afs_clean_get_params(s, &p);
        params_defaults(p);
        p->frame_index = 3;
        p->enable = 0;
        p->seed_type = seeds[i];

        res.detected_type = -1;
        afs_clean_run(s, &stats, &res);
        CHECK_EQ(res.detected_type, want[i], "auto seed clamp");
        afs_clean_exit(s);
    }
}

static void test_gain_window_and_disable(void)
{
    const afs_clean_ops_t *ops = NULL;
    afs_clean_state_t *s = afs_clean_init(&ops);
    afs_clean_params_t *p = NULL;
    afs_clean_stats_t stats;
    afs_clean_result_t res;
    int fi;

    if (s == NULL) {
        CHECK(0, "init");
        return;
    }
    make_flat_columns();
    stats.column_sum = g_cols;
    stats.stats_width = AFS_CLEAN_NCOL;
    stats.stats_height = AFS_CLEAN_NROW;

    afs_clean_get_params(s, &p);
    params_defaults(p);

    /* Disabled: nothing captured even though gain is in-window. */
    p->enable = 0;
    for (fi = 3; fi < 9; fi++) {
        p->frame_index = fi;
        afs_clean_run(s, &stats, &res);
    }
    CHECK_EQ(afs_clean_rows_filled(s), 0, "disabled captures nothing");

    /* Enabled: 5 rows accumulate. */
    p->enable = 1;
    for (fi = 9; fi < 14; fi++) {
        p->frame_index = fi;
        afs_clean_run(s, &stats, &res);
    }
    CHECK_EQ(afs_clean_rows_filled(s), 5, "rows accumulate");

    /* Gain leaves the window: partial set discarded. */
    p->gain_level = 100;
    p->frame_index = 14;
    afs_clean_run(s, &stats, &res);
    CHECK_EQ(afs_clean_rows_filled(s), 0, "gain exit resets set");

    /* Back in window: starts a fresh set. */
    p->gain_level = 257;
    p->frame_index = 15;
    afs_clean_run(s, &stats, &res);
    CHECK_EQ(afs_clean_rows_filled(s), 1, "gain re-entry restarts set");

    afs_clean_exit(s);
}

static void test_null_arguments(void)
{
    const afs_clean_ops_t *ops = NULL;
    afs_clean_state_t *s = afs_clean_init(&ops);
    afs_clean_stats_t stats;
    afs_clean_result_t res;
    int rc;

    if (s == NULL) {
        CHECK(0, "init");
        return;
    }
    stats.column_sum = g_cols;
    stats.stats_width = 0;
    stats.stats_height = 0;

    res.detected_type = -1;
    rc = afs_clean_run(s, NULL, &res);
    CHECK_EQ(rc, -1, "null stats returns -1");
    CHECK_EQ(res.detected_type, AFS_CLEAN_TYPE_HZ50, "null stats sets 50");

    res.detected_type = -1;
    rc = afs_clean_run(NULL, &stats, &res);
    CHECK_EQ(rc, -1, "null state returns -1");
    CHECK_EQ(res.detected_type, AFS_CLEAN_TYPE_HZ50, "null state sets 50");

    afs_clean_exit(s);
}

static void test_banding_detection_and_alternation(void)
{
    const afs_clean_ops_t *ops = NULL;
    afs_clean_state_t *s = afs_clean_init(&ops);
    afs_clean_params_t *p = NULL;
    afs_clean_stats_t stats;
    afs_clean_result_t res;
    int fi;
    int first_cycle = -1;
    int second_cycle = -1;

    if (s == NULL) {
        CHECK(0, "init");
        return;
    }
    stats.column_sum = g_cols;
    stats.stats_width = AFS_CLEAN_NCOL;
    stats.stats_height = AFS_CLEAN_NROW;

    afs_clean_get_params(s, &p);
    params_defaults(p);
    p->mode = AFS_CLEAN_MODE_AUTO;
    p->seed_type = AFS_CLEAN_SEED_ALTERNATE;
    p->min_peak_ratio = 50;

    for (fi = 0; fi <= 40; fi++) {
        if (fi >= 3)
            make_band_row((fi - 3) % AFS_CLEAN_NROW);
        p->frame_index = fi;
        res.detected_type = -1;
        afs_clean_run(s, &stats, &res);

        if (fi == 18)
            first_cycle = res.detected_type;
        if (fi == 34)
            second_cycle = res.detected_type;
    }

    CHECK_EQ(first_cycle, AFS_CLEAN_TYPE_HZ50, "first detection is 50");
    CHECK_EQ(second_cycle, AFS_CLEAN_TYPE_HZ60, "second detection alternates");
    afs_clean_exit(s);
}

static void test_flat_columns_never_detect(void)
{
    const afs_clean_ops_t *ops = NULL;
    afs_clean_state_t *s = afs_clean_init(&ops);
    afs_clean_params_t *p = NULL;
    afs_clean_stats_t stats;
    afs_clean_result_t res;
    int fi;

    if (s == NULL) {
        CHECK(0, "init");
        return;
    }
    make_flat_columns();
    stats.column_sum = g_cols;
    stats.stats_width = AFS_CLEAN_NCOL;
    stats.stats_height = AFS_CLEAN_NROW;

    afs_clean_get_params(s, &p);
    params_defaults(p);
    p->seed_type = AFS_CLEAN_SEED_ALTERNATE;

    for (fi = 0; fi <= 40; fi++) {
        p->frame_index = fi;
        res.detected_type = -1;
        afs_clean_run(s, &stats, &res);
    }
    CHECK_EQ(res.detected_type, AFS_CLEAN_TYPE_HZ50,
             "flat profile keeps provisional type");
    afs_clean_exit(s);
}

static void test_image_width_normalises_columns(void)
{
    const afs_clean_ops_t *ops = NULL;
    afs_clean_state_t *s = afs_clean_init(&ops);
    afs_clean_params_t *p = NULL;
    afs_clean_stats_t stats;
    afs_clean_result_t res;
    int fi;
    int detected = -1;

    if (s == NULL) {
        CHECK(0, "init");
        return;
    }
    stats.column_sum = g_cols;
    stats.stats_width = AFS_CLEAN_NCOL;
    stats.stats_height = AFS_CLEAN_NROW;

    afs_clean_get_params(s, &p);
    params_defaults(p);
    p->image_width = 3;
    p->min_peak_ratio = 50;

    /* Scaling does not change the ratio-based decision. */
    for (fi = 0; fi <= 40; fi++) {
        if (fi >= 3) {
            make_band_row((fi - 3) % AFS_CLEAN_NROW);
        }
        p->frame_index = fi;
        res.detected_type = -1;
        afs_clean_run(s, &stats, &res);
        if (fi == 34)
            detected = res.detected_type;
    }
    CHECK_EQ(detected, AFS_CLEAN_TYPE_HZ60,
             "image_width scaling preserves detection");
    afs_clean_exit(s);
}

static void test_isr_forwards(void)
{
    const afs_clean_ops_t *ops = NULL;
    afs_clean_state_t *s = afs_clean_init(&ops);
    afs_clean_stats_t stats;
    afs_clean_result_t res;
    int rc;

    if (s == NULL) {
        CHECK(0, "init");
        return;
    }
    stats.column_sum = g_cols;
    stats.stats_width = 0;
    stats.stats_height = 0;

    res.detected_type = -1;
    rc = afs_clean_isr(NULL, &stats, &res);
    CHECK_EQ(rc, -1, "isr forwards null-state handling");
    CHECK_EQ(res.detected_type, AFS_CLEAN_TYPE_HZ50, "isr null sets 50");
    afs_clean_exit(s);
}

int main(void)
{
    build_trig_tables();

    test_init_requires_tables();
    test_lifecycle_and_params();
    test_warmup_frames_ignored();
    test_mode_dispatch();
    test_auto_seed_clamping();
    test_gain_window_and_disable();
    test_null_arguments();
    test_banding_detection_and_alternation();
    test_flat_columns_never_detect();
    test_image_width_normalises_columns();
    test_isr_forwards();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
