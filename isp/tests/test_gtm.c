/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * test_gtm.c - host unit tests for the clean-room GTM module.
 *
 * The test owns the runtime table provider.  Every table the module consumes
 * is injected through freeisp_get_tables(); the fixtures below are synthetic,
 * not vendor tuning data.
 */
#include "gtm_clean.h"

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
/* Injected tables                                                    */
/* ------------------------------------------------------------------ */

static int16_t g_guide_linear[GTM_NCURVE];
static int16_t g_guide_low[GTM_NCURVE];
static int16_t g_guide_high[GTM_NCURVE];
static int16_t g_pregamma[GTM_PRE_ROWS * GTM_PRE_COLS];
static int32_t g_eq_kernel[GTM_EQ_ROWS * GTM_EQ_TAPS];
static uint8_t g_converge[GTM_CONV_ROWS * GTM_CONV_COLS];

static gtm_clean_tables_t g_tab;
static freeisp_tables_t g_ft;
static const freeisp_tables_t *g_ft_ptr = &g_ft;

const freeisp_tables_t *freeisp_get_tables(void)
{
    return g_ft_ptr;
}

static void build_guides(int16_t *dst, int slope)
{
    int i;
    for (i = 0; i < GTM_NCURVE; i++) {
        int v = slope * i;
        if (v > 4096)
            v = 4096;
        dst[i] = (int16_t)v;
    }
}

static void init_tables(void)
{
    int r, j, d;

    build_guides(g_guide_linear, 16);
    build_guides(g_guide_low, 24);
    build_guides(g_guide_high, 8);

    for (r = 0; r < GTM_PRE_ROWS; r++)
        for (j = 0; j < GTM_PRE_COLS; j++)
            g_pregamma[r * GTM_PRE_COLS + j] = (int16_t)j;

    for (r = 0; r < GTM_EQ_ROWS; r++)
        for (j = 0; j < GTM_EQ_TAPS; j++)
            g_eq_kernel[r * GTM_EQ_TAPS + j] = 33;

    for (r = 0; r < GTM_CONV_ROWS; r++)
        for (d = 0; d < GTM_CONV_COLS; d++)
            g_converge[r * GTM_CONV_COLS + d] =
                (uint8_t)((r == 26) ? d : 0);

    memset(&g_tab, 0, sizeof(g_tab));
    g_tab.guide_linear = g_guide_linear;
    g_tab.guide_low = g_guide_low;
    g_tab.guide_high = g_guide_high;
    g_tab.pre_gamma = g_pregamma;
    g_tab.eq_kernel = g_eq_kernel;
    g_tab.converge = g_converge;
    g_ft.gtm = &g_tab;
}

/* ------------------------------------------------------------------ */
/* Parameter / statistics fixtures                                    */
/* ------------------------------------------------------------------ */

static void base_params(gtm_clean_params_t *p)
{
    int r, c, i;

    memset(p, 0, sizeof(*p));
    p->mode = GTM_MODE_FIXED;
    p->gamma_mode = GTM_GAMMA_DYNAMIC;
    p->frame_index = 3;
    p->enable = 1;
    p->range_max = 0x500;
    p->eq_gain = 40;
    p->cover_bias = 10;
    p->black_level = 16;
    p->white_level = 240;
    p->white_slope = 1;
    p->black_slope = 1;
    p->pre_gamma_offset = 0;
    p->hist_pixel_count = 1;
    p->dark_floor = 0;
    p->bright_floor = 0;
    for (r = 0; r < GTM_NPEAK; r++)
        for (c = 0; c < GTM_NPEAK; c++)
            p->peak_map[r][c] = 0x80;
    p->brightness = 0;
    p->contrast = 0;
    p->bit_offset = 0;
    p->wdr_en = 0;
    for (i = 0; i < GTM_NGAMMA; i++)
        p->gamma_lut[i] = (uint16_t)(i * 4);
}

static gtm_entity_t *make_entity(gtm_clean_params_t **pp)
{
    gtm_ops_t ops;
    gtm_clean_param_req_t req;
    gtm_clean_params_t *p;
    gtm_entity_t *e = gtm_init(&ops);

    if (e == NULL)
        return NULL;
    gtm_get_params(e, &p);
    base_params(p);
    req.kind = GTM_PARAM_INIT;
    req.params = NULL;
    gtm_set_params(e, &req, NULL);
    if (pp != NULL)
        *pp = p;
    return e;
}

static void fill_stats(gtm_clean_stats_t *st, int mode)
{
    int i;
    memset(st, 0, sizeof(*st));
    for (i = 0; i < GTM_NWIN; i++)
        st->win_avg[i] = (uint32_t)((i * 7) % 256);
    switch (mode) {
    case 0: /* flat */
        for (i = 0; i < GTM_NCURVE; i++)
            st->hist_raw[i] = 100;
        break;
    case 1: /* shadow-concentrated */
        st->hist_raw[10] = 100000;
        break;
    case 2: /* highlight-concentrated */
        st->hist_raw[250] = 100000;
        break;
    case 3: /* gradient */
        for (i = 0; i < GTM_NCURVE; i++)
            st->hist_raw[i] = (uint32_t)(i + 1);
        break;
    default: /* all-zero */
        break;
    }
}

static void run_frames(gtm_entity_t *e, const gtm_clean_stats_t *st,
                       gtm_clean_result_t *res, int n)
{
    int i;
    for (i = 0; i < n; i++)
        gtm_run(e, st, res);
}

static int curve_in_range(const uint16_t *c)
{
    int i;
    for (i = 0; i < GTM_NCURVE; i++)
        if (c[i] > GTM_CURVE_MAX)
            return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_init_requires_tables(void)
{
    gtm_ops_t ops;
    gtm_entity_t *e;

    g_ft_ptr = NULL;
    e = gtm_init(&ops);
    CHECK(e == NULL, "init fails with no provider");

    g_ft_ptr = &g_ft;
    g_tab.guide_linear = NULL;
    e = gtm_init(&ops);
    CHECK(e == NULL, "init fails with missing guide table");
    g_tab.guide_linear = g_guide_linear;

    e = gtm_init(&ops);
    CHECK(e != NULL, "init succeeds with full tables");
    CHECK(ops.get_params == gtm_get_params, "ops.get_params wired");
    CHECK(ops.set_params == gtm_set_params, "ops.set_params wired");
    CHECK(ops.run == gtm_run, "ops.run wired");
    gtm_exit(e);
}

static void test_lifecycle_and_params(void)
{
    gtm_entity_t *e;
    gtm_clean_params_t *p = NULL;
    gtm_clean_param_req_t req;

    e = gtm_init(NULL);
    CHECK(e != NULL, "init without ops");
    CHECK_EQ(gtm_get_params(NULL, &p), -1, "get_params(NULL)");
    CHECK_EQ(gtm_get_params(e, &p), 0, "get_params");
    CHECK(p != NULL, "params pointer");
    if (p != NULL) {
        CHECK_EQ(p->frame_index, 0, "fresh params zeroed");
        CHECK_EQ(p->mode, 0, "fresh mode zeroed");
    }

    req.kind = 99;
    req.params = NULL;
    CHECK_EQ(gtm_set_params(e, &req, NULL), -1, "unknown kind rejected");
    CHECK_EQ(gtm_set_params(NULL, &req, NULL), -1, "null set_params");

    gtm_exit(e);
}

static void test_seeding(void)
{
    gtm_entity_t *e;
    gtm_clean_params_t *p = NULL;
    gtm_clean_param_req_t req;
    int i;

    e = make_entity(&p);
    CHECK(e != NULL, "entity");
    if (e == NULL)
        return;

    for (i = 0; i < GTM_NCURVE; i++) {
        CHECK_EQ(p->curve[i], 0x200, "curve seeded");
        CHECK_EQ(p->curve_prev[i], 0x200, "curve_prev seeded");
    }

    p->curve[100] = 123;
    p->curve[255] = 7; /* non-zero sentinel blocks reseeding */
    req.kind = GTM_PARAM_INIT;
    req.params = NULL;
    gtm_set_params(e, &req, NULL);
    CHECK_EQ(p->curve[100], 123, "seed skipped when curve[255]!=0");

    gtm_exit(e);
}

static void test_null_and_gating(void)
{
    gtm_clean_stats_t st;
    gtm_clean_result_t res;
    gtm_entity_t *e;
    gtm_clean_params_t *p = NULL;

    fill_stats(&st, 0);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }

    CHECK_EQ(gtm_run(NULL, &st, &res), -1, "null entity");
    CHECK_EQ(gtm_run(e, NULL, &res), -1, "null stats");
    CHECK_EQ(gtm_run(e, &st, NULL), -1, "null result");

    p->frame_index = 2;
    res.hdr_flag = 0x1234;
    CHECK_EQ(gtm_run(e, &st, &res), 0, "warmup returns 0");
    CHECK_EQ(res.hdr_flag, 0x1234, "warmup leaves hdr_flag");
    CHECK_EQ(p->curve[100], 0x200, "warmup leaves curve");

    p->frame_index = 3;
    p->enable = 0;
    res.hdr_flag = 0x1234;
    gtm_run(e, &st, &res);
    CHECK_EQ(res.hdr_flag, 0x1234, "disabled leaves hdr_flag");

    p->enable = 1;
    res.hdr_flag = 0;
    gtm_run(e, &st, &res);
    CHECK_EQ(res.hdr_flag, 1, "enabled sets hdr_flag");

    gtm_exit(e);
}

static void test_fixed_blend(void)
{
    gtm_clean_stats_t st;
    gtm_clean_result_t res;
    gtm_entity_t *e;
    gtm_clean_params_t *p = NULL;

    fill_stats(&st, 3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->mode = GTM_MODE_FIXED;
    p->brightness = 0;
    p->contrast = 0;

    memset(&res, 0, sizeof(res));
    gtm_run(e, &st, &res);
    CHECK_EQ(res.hdr_flag, 1, "fixed hdr_flag");
    CHECK(res.curve == p->curve, "result curve points at instance curve");
    CHECK(curve_in_range(p->curve), "fixed curve in range");
    CHECK_EQ(p->curve[0], 0, "blend forces curve[0]=0");
    CHECK(p->curve[255] > 0, "blend produces a top level");

    /* Contrast must not crash and must stay bounded. */
    p->contrast = 60;
    p->brightness = 10;
    gtm_run(e, &st, &res);
    CHECK(curve_in_range(p->curve), "contrast curve in range");

    gtm_exit(e);
}

static void test_dynamic_range(void)
{
    gtm_clean_stats_t st;
    gtm_clean_result_t res;
    gtm_entity_t *e;
    gtm_clean_params_t *p = NULL;
    int changed = 0, i;

    fill_stats(&st, 3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->mode = GTM_MODE_DYNAMIC_RANGE;
    p->gamma_mode = GTM_GAMMA_FIXED;
    p->pre_gamma_offset = 0;

    memset(&res, 0, sizeof(res));
    gtm_run(e, &st, &res);
    CHECK_EQ(res.hdr_flag, 1, "dr hdr_flag");
    CHECK(curve_in_range(p->curve), "dr curve in range");
    for (i = 0; i < GTM_NCURVE; i++)
        if (p->curve[i] != 0x200)
            changed = 1;
    CHECK(changed, "dr writes a curve");

    gtm_exit(e);

    /* All-zero histogram: equalisation builds no curve (fix_sum == 0), but
     * the run must still fall through to the curve refresh and the
     * brightness/contrast blend (audit A5a: a blank frame still refreshes
     * the curve). */
    fill_stats(&st, 4);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->mode = GTM_MODE_DYNAMIC_RANGE;
    memset(&res, 0, sizeof(res));
    gtm_run(e, &st, &res);
    CHECK_EQ(res.hdr_flag, 1, "empty histogram still marks hdr_flag");
    CHECK(p->curve[100] != 0x200, "empty histogram still refreshes the curve");
    gtm_exit(e);
}

static void test_luma_hold(void)
{
    gtm_clean_stats_t st;
    gtm_clean_result_t res;
    gtm_entity_t *e;
    gtm_clean_params_t *p = NULL;
    int i;

    fill_stats(&st, 1);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->mode = GTM_MODE_LUMA_HOLD;

    memset(&res, 0, sizeof(res));
    run_frames(e, &st, &res, 6);

    CHECK_EQ(res.hdr_flag, 1, "lh hdr_flag");
    CHECK(res.avg_lum <= 255, "avg_lum range");
    CHECK(res.avg_var <= 255, "avg_var range");
    CHECK(res.peak_level >= 0x80, "peak floor applied");
    CHECK(res.div_index <= 255, "div_index range");
    CHECK(res.ratio_hold == res.ratio_hold, "ratio_hold is not NaN");
    CHECK(curve_in_range(p->curve), "lh curve in range");
    for (i = 0; i < GTM_NCURVE; i++) {
        if (p->curve[i] != p->curve_prev[i]) {
            CHECK(0, "lh keeps curve_prev equal to curve");
            break;
        }
    }

    gtm_exit(e);
}

static void test_guide_hysteresis(void)
{
    gtm_clean_stats_t st;
    gtm_clean_result_t res;
    gtm_entity_t *e;
    gtm_clean_params_t *p = NULL;
    int i;

    fill_stats(&st, 1);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->mode = GTM_MODE_LUMA_HOLD;

    /* Ten consistent frames drive the polarity to +1. */
    memset(&res, 0, sizeof(res));
    run_frames(e, &st, &res, 10);
    CHECK_EQ(gtm_clean_guide_state(e), 1, "positive frames select +1");

    /* Nine contradictory frames are absorbed by hysteresis. */
    build_guides(g_guide_linear, 32);
    run_frames(e, &st, &res, 9);
    CHECK_EQ(gtm_clean_guide_state(e), 1, "first nine contradictions held");

    /* The tenth contradiction reclassifies. */
    gtm_run(e, &st, &res);
    CHECK_EQ(gtm_clean_guide_state(e), -1, "tenth contradiction flips");

    for (i = 0; i < GTM_NCURVE; i++)
        g_guide_linear[i] = (int16_t)(16 * i);
    gtm_exit(e);
}

static void test_step_limiter(void)
{
    gtm_clean_stats_t st;
    gtm_clean_result_t res;
    gtm_entity_t *e;
    gtm_clean_params_t *p = NULL;
    int i, ok = 1;

    fill_stats(&st, 1);
    for (i = 0; i < GTM_CONV_COLS; i++)
        g_converge[26 * GTM_CONV_COLS + i] = 1;

    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->mode = GTM_MODE_LUMA_HOLD;

    memset(&res, 0, sizeof(res));
    gtm_run(e, &st, &res);
    for (i = 0; i < GTM_NCURVE; i++) {
        if (p->curve[i] < 0x200 - 1 || p->curve[i] > 0x200 + 1)
            ok = 0;
    }
    CHECK(ok, "step limiter bounds movement to 1");

    for (i = 0; i < GTM_CONV_COLS; i++)
        g_converge[26 * GTM_CONV_COLS + i] = (uint8_t)i;
    gtm_exit(e);
}

static void test_luma_hold_ignores_contrast(void)
{
    gtm_clean_stats_t st;
    gtm_clean_result_t res_a, res_b;
    gtm_entity_t *ea, *eb;
    gtm_clean_params_t *pa = NULL, *pb = NULL;
    int i, same = 1;

    fill_stats(&st, 1);

    ea = make_entity(&pa);
    eb = make_entity(&pb);
    if (ea == NULL || eb == NULL) {
        CHECK(0, "entities");
        gtm_exit(ea);
        gtm_exit(eb);
        return;
    }
    pa->mode = GTM_MODE_LUMA_HOLD;
    pb->mode = GTM_MODE_LUMA_HOLD;
    pa->contrast = 0;
    pb->contrast = 100;

    memset(&res_a, 0, sizeof(res_a));
    memset(&res_b, 0, sizeof(res_b));
    run_frames(ea, &st, &res_a, 8);
    run_frames(eb, &st, &res_b, 8);

    for (i = 0; i < GTM_NCURVE; i++)
        if (pa->curve[i] != pb->curve[i])
            same = 0;
    CHECK(same, "luma-hold ignores contrast");

    gtm_exit(ea);
    gtm_exit(eb);
}

static void test_blend_normalises_index_255(void)
{
    gtm_clean_stats_t st;
    gtm_clean_result_t res;
    gtm_entity_t *e;
    gtm_clean_params_t *p = NULL;
    static const int combos[][2] = {
        { 0, 0 }, { 10, 60 }, { -20, -40 }
    };
    int n = (int)(sizeof(combos) / sizeof(combos[0]));
    int i;

    fill_stats(&st, 3);

    /* In fixed mode the blend overwrites curve[255] with the fully
     * normalised projection (blend_curve[255] -> 0x1000), so the top of
     * the curve must be the same for every brightness/contrast pair. */
    for (i = 0; i < n; i++) {
        e = make_entity(&p);
        if (e == NULL) {
            CHECK(0, "blend entity");
            return;
        }
        p->mode = GTM_MODE_FIXED;
        p->brightness = combos[i][0];
        p->contrast = combos[i][1];
        memset(&res, 0, sizeof(res));
        gtm_run(e, &st, &res);
        CHECK_EQ(p->curve[255], 514,
                 "blend normalises index 255 (fixed)");
        gtm_exit(e);
    }

    /* Non-fixed modes multiply the projection into the seeded curve; from a
     * fresh 0x200 top entry the same normalised top is produced. */
    e = make_entity(&p);
    if (e != NULL) {
        p->mode = GTM_MODE_DYNAMIC_GAMMA;
        p->brightness = 0;
        p->contrast = 0;
        memset(&res, 0, sizeof(res));
        gtm_run(e, &st, &res);
        CHECK_EQ(p->curve[255], 514, "blend normalises index 255 (dyn)");
        gtm_exit(e);
    }
}


/*
 * Zero-divisor regression (on-camera SIGFPE, 2026-09-20 (7)): the GTM curve
 * interpolator and the otsu histogram pass both divide by a value that can be
 * zero on real data -- a flat gamma LUT (hi == lo) and an all-zero histogram
 * (cnt stays 0).  Pre-fix these trapped; the deployed object's hardware sdiv
 * yields 0.
 */
static void test_zero_divisor_paths(void)
{
    gtm_clean_stats_t st;
    gtm_clean_result_t res;
    gtm_clean_params_t *p;
    gtm_entity_t *e = make_entity(&p);
    int i;

    CHECK(e != NULL, "gtm entity created");

    /* Flat gamma LUT: the first matching pair has hi - lo == 0. */
    for (i = 0; i < GTM_NGAMMA; i++)
        p->gamma_lut[i] = 0x80;
    fill_stats(&st, 3);
    run_frames(e, &st, &res, 1);

    /* All-zero histogram: otsu's cnt never increments. */
    for (i = 0; i < GTM_NGAMMA; i++)
        p->gamma_lut[i] = (uint16_t)(i * 4);
    fill_stats(&st, 4);
    run_frames(e, &st, &res, 1);

    gtm_exit(e);
}

int main(void)
{
    init_tables();

    test_init_requires_tables();
    test_lifecycle_and_params();
    test_seeding();
    test_null_and_gating();
    test_fixed_blend();
    test_blend_normalises_index_255();
    test_dynamic_range();
    test_luma_hold();
    test_guide_hysteresis();
    test_step_limiter();
    test_luma_hold_ignores_contrast();
    test_zero_divisor_paths();

    printf("GTM clean-room tests: %d checks, %d failures\n", g_checks,
           g_failures);
    return g_failures == 0 ? 0 : 1;
}
