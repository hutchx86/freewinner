/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * test_pltm.c - host unit tests for the clean-room PLTM module.
 *
 * The test owns the runtime table provider.  Every table the module consumes
 * is injected through freeisp_get_tables(); the fixtures below are synthetic,
 * not vendor tuning data.
 */
#include "pltm_clean.h"

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

static uint16_t g_source[0x300];
static int32_t  g_strength[PLTM_STRENGTH_ROWS * PLTM_STRENGTH_COLS];
static uint8_t  g_converge[PLTM_CONV_ROWS * PLTM_CONV_COLS];

static int32_t  g_presets[PLTM_NPRESET * PLTM_PRESET_COLS];

static pltm_clean_tables_t g_tab;
static freeisp_tables_t g_ft;
static const freeisp_tables_t *g_ft_ptr = &g_ft;

const freeisp_tables_t *freeisp_get_tables(void)
{
    return g_ft_ptr;
}

static void init_tables(void)
{
    int k, r, d;

    for (k = 0; k < 0x300; k++)
        g_source[k] = (uint16_t)k;

    /* Four monotone candidate curves. */
    for (r = 0; r < PLTM_STRENGTH_ROWS; r++)
        for (k = 0; k < PLTM_STRENGTH_COLS; k++)
            g_strength[r * PLTM_STRENGTH_COLS + k] =
                (int32_t)((r + 1) * 1000 + k);

    /* Convergence step = clamped |delta|. */
    for (r = 0; r < PLTM_CONV_ROWS; r++)
        for (d = 0; d < PLTM_CONV_COLS; d++)
            g_converge[r * PLTM_CONV_COLS + d] = (uint8_t)d;

    /* Synthetic preset bank (made-up, rule-abiding; not device data):
     * blend 255-17r (0 from row 15), order 5+r/2, clip 100+200r,
     * gain 4096-200r. */
    for (r = 0; r < PLTM_NPRESET; r++) {
        int32_t *row = g_presets + r * PLTM_PRESET_COLS;

        row[PLTM_PRESET_BLEND] = r <= 15 ? 255 - 17 * r : 0;
        row[PLTM_PRESET_ORDER] = 5 + r / 2;
        row[PLTM_PRESET_CLIP]  = 100 + 200 * r;
        row[PLTM_PRESET_GAIN]  = 4096 - 200 * r;
    }

    memset(&g_tab, 0, sizeof(g_tab));
    g_tab.strength_bank = g_strength;
    g_tab.converge_bank = g_converge;
    g_tab.presets = g_presets;
    g_ft.pltm = &g_tab;
}

/* ------------------------------------------------------------------ */
/* Parameter / statistics fixtures                                    */
/* ------------------------------------------------------------------ */

static void base_params(pltm_clean_params_t *p)
{
    memset(p, 0, sizeof(*p));
    p->config.mode = 2;                    /* manual/static by default */
    p->config.oripic_ratio_cfg = 0x40;
    p->config.order_cfg = 7;
    p->config.last_order_ratio_cfg = 3;
    p->config.clip_cfg = 16;
    p->config.gain_cfg = PLTM_Q12_UNITY;
    p->config.block_rows = 0;
    p->config.block_cols = 0;
    p->config.contrast = 0;
    p->config.tolerance = 5;
    p->config.speed = 0;
    p->config.step = 2;
    p->config.interval = 1;
    p->config.enable = 1;
    p->config.frame_id = 3;
    p->config.auto_strength = 192;         /* row 3, fraction 0 */
    p->config.manual_strength = 0x10;
    p->config.ae_comp = 0x2A;
    p->config.min_threshold = 0;
    p->config.bit_offset = 0;
    p->config.source_curve = g_source;
    p->sensor.width = 64;
    p->sensor.height = 48;
    p->sensor.wdr_mode = 0;
    p->sensor.ae_settled = 1;
    p->sensor.backlight = 0;
}

static pltm_entity_t *make_entity(pltm_clean_params_t **pp)
{
    pltm_ops_t ops;
    pltm_clean_params_t *p;
    pltm_entity_t *e = pltm_init(&ops);

    if (e == NULL)
        return NULL;
    pltm_get_params(e, &p);
    base_params(p);
    if (pp != NULL)
        *pp = p;
    return e;
}

static void zero_stats(pltm_clean_stats_t *st)
{
    memset(st, 0, sizeof(*st));
}

/* Reference transcription of the merge kernel, used to cross-check. */
static int merge_ref(uint16_t *dst, int n)
{
    int g1[256], g2[256], t[256], u[256];
    int i, half, top, count, eta1, eta2;

    for (i = 0; i < 128; i++)
        dst[i] = 0;
    if (n <= 0)
        return 0;

    half = (n >> 1) + 1;
    top = n + half;
    eta1 = (15 * n) >> 5;
    eta1 = eta1 * eta1;
    eta2 = (18 * n) >> 5;
    eta2 = eta2 * eta2;

    for (i = 0; i <= top; i++) {
        double i2 = (double)i * (double)i;
        double e1 = (eta1 == 0) ? 0.0 : exp(-i2 / (2.0 * (double)eta1));
        double e2 = (eta2 == 0) ? 0.0 : exp(-i2 / (2.0 * (double)eta2));
        g1[i] = (int)(0.45 + 256.0 * e1);
        g2[i] = (int)(0.45 + 256.0 * e2);
    }
    for (i = 0; i <= top; i++) {
        int s = (i * i) >> 8;
        int b = (g1[i] * (256 - s) + g2[i] * s) >> 8;
        t[i] = (i == 0) ? b : ((b < t[i - 1]) ? b : t[i - 1]);
    }
    u[0] = t[0];
    for (i = 1; i < top; i++)
        u[i] = (t[i - 1] + t[i] + t[i + 1]) / 3;
    if (top >= 1)
        u[top] = (t[top - 1] + t[top]) >> 1;

    count = (n >> 1) + 2;
    if (count > 128)
        count = 128;
    for (i = 0; i < count; i++) {
        int a = i + n;
        int b = (i >= n) ? (i - n) : (n - i);
        int sum = u[a] + u[i] + u[b];
        int r0 = 0, r1 = 0;
        if (sum > 0) {
            r0 = (u[a] << 8) / sum;
            r1 = (u[i] << 8) / sum;
            if (r0 > 0xFF) r0 = 0xFF;
            if (r1 > 0xFF) r1 = 0xFF;
        }
        dst[i] = (uint16_t)(r0 | (r1 << 8));
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_init_requires_tables(void)
{
    pltm_ops_t ops;
    pltm_entity_t *e;

    g_ft_ptr = NULL;
    e = pltm_init(&ops);
    CHECK(e == NULL, "init fails with no provider");

    g_ft_ptr = &g_ft;
    g_tab.strength_bank = NULL;
    e = pltm_init(&ops);
    CHECK(e == NULL, "init fails with no strength bank");
    g_tab.strength_bank = g_strength;

    g_tab.converge_bank = NULL;
    e = pltm_init(&ops);
    CHECK(e == NULL, "init fails with no convergence bank");
    g_tab.converge_bank = g_converge;

    e = pltm_init(&ops);
    CHECK(e != NULL, "init succeeds with full tables");
    CHECK(ops.get_params == pltm_get_params, "ops.get_params wired");
    CHECK(ops.set_params == pltm_set_params, "ops.set_params wired");
    CHECK(ops.run == pltm_run, "ops.run wired");
    CHECK(ops.set_default_result == pltm_set_default_result,
          "ops.set_default_result wired");
    pltm_exit(e);
}

static void test_lifecycle_and_params(void)
{
    pltm_entity_t *e;
    pltm_clean_params_t *p = NULL;
    pltm_clean_param_req_t req;
    pltm_clean_result_t res;

    e = pltm_init(NULL);
    CHECK(e != NULL, "init without ops");
    CHECK_EQ(pltm_get_params(NULL, &p), -1, "get_params(NULL)");
    CHECK_EQ(pltm_get_params(e, &p), 0, "get_params");
    CHECK(p != NULL, "params pointer");
    if (p != NULL) {
        CHECK_EQ(p->config.frame_id, 0, "fresh params zeroed");
        CHECK_EQ(p->config.mode, 0, "fresh mode zeroed");
    }

    req.kind = PLTM_PARAM_INIT;
    req.params = NULL;
    CHECK_EQ(pltm_set_params(e, &req, NULL), 0, "set_params is a no-op");
    CHECK_EQ(pltm_set_default_result(e, &res), 0, "set_default_result no-op");

    pltm_exit(e);
}

static void test_null_run(void)
{
    pltm_clean_stats_t st;
    pltm_clean_result_t res;
    pltm_entity_t *e = make_entity(NULL);

    zero_stats(&st);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    CHECK_EQ(pltm_run(NULL, &st, &res), -1, "null entity");
    CHECK_EQ(pltm_run(e, NULL, &res), -1, "null stats");
    CHECK_EQ(pltm_run(e, &st, NULL), -1, "null result");
    pltm_exit(e);
}

static void test_tile_geometry(void)
{
    pltm_clean_stats_t st;
    pltm_clean_result_t res;
    pltm_entity_t *e;
    pltm_clean_params_t *p = NULL;

    zero_stats(&st);
    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->config.block_rows = 3;
    p->config.block_cols = 3;
    p->config.frame_id = 100;

    pltm_run(e, &st, &res);
    CHECK_EQ(res.block_width, 15, "block_width");
    CHECK_EQ(res.block_height, 11, "block_height");
    CHECK_EQ(res.stat_scale, (uint32_t)(0x100000000ULL / 192), "stat_scale");
    CHECK_EQ(pltm_clean_get_start_frame(), 103, "tile change delays start");

    p->config.frame_id = 101;
    pltm_run(e, &st, &res);
    CHECK_EQ(pltm_clean_get_start_frame(), 103, "stable geometry keeps start");
    pltm_exit(e);

    /* nw*nh == 1: 2^32/1 truncates to zero (spec 11). */
    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->sensor.width = 1;
    p->sensor.height = 1;
    p->config.frame_id = 3;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.block_width, 0, "degenerate block_width");
    CHECK_EQ(res.block_height, 0, "degenerate block_height");
    CHECK_EQ(res.stat_scale, 0u, "stat_scale overflow");
    pltm_exit(e);
}

static void test_merge_table(void)
{
    pltm_clean_stats_t st;
    pltm_clean_result_t res;
    pltm_entity_t *e;
    pltm_clean_params_t *p = NULL;
    uint16_t ref[128];
    int i, ok;

    zero_stats(&st);
    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->config.block_cols = 0;
    p->config.block_rows = 0;
    p->sensor.width = 63;
    p->sensor.height = 45;
    p->config.frame_id = 3;
    p->config.clip_cfg = 0;
    p->config.gain_cfg = 0;

    pltm_run(e, &st, &res);

    merge_ref(ref, 63);
    ok = 1;
    for (i = 0; i < 64; i++)
        if (res.tbl[i] != ref[i])
            ok = 0;
    CHECK(ok, "horizontal merge matches reference");

    merge_ref(ref, 45);
    ok = 1;
    for (i = 0; i < 64; i++)
        if (res.tbl[PLTM_MERGE_V_BASE + i] != ref[i])
            ok = 0;
    CHECK(ok, "vertical merge matches reference");

    CHECK_EQ(res.tbl[33], 0, "horizontal tail zero");
    CHECK_EQ(res.tbl[PLTM_MERGE_V_BASE + 24], 0, "vertical tail zero");
    pltm_exit(e);
}

static void test_dense_manual_tables(void)
{
    pltm_clean_stats_t st;
    pltm_clean_result_t res;
    pltm_entity_t *e;
    pltm_clean_params_t *p = NULL;
    int k, ok;

    zero_stats(&st);
    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->config.clip_cfg = 16;
    p->config.gain_cfg = PLTM_Q12_UNITY;
    p->config.oripic_ratio_cfg = 0x40;
    p->config.order_cfg = 7;
    p->config.last_order_ratio_cfg = 3;

    pltm_run(e, &st, &res);

    CHECK_EQ(res.oripic_ratio, 0x40, "manual oripic");
    CHECK_EQ(res.order, 7, "manual order");
    CHECK_EQ(res.last_order_ratio, 3, "manual last order");
    CHECK_EQ(res.cal_en, 1, "manual run qualifies");
    CHECK_EQ(res.frame_smooth_en, 1, "manual run smooth flag");
    CHECK_EQ(res.ae_comp, 0x2A, "ae_comp copy");

    CHECK_EQ(res.tbl[PLTM_TONE_BASE], 0, "tone[0]");
    CHECK_EQ(res.tbl[PLTM_TONE_BASE + 1], 0x101, "tone[1] = source");
    CHECK_EQ(res.tbl[PLTM_TONE_BASE + 254], 0x1FE, "tone[254] = source");
    CHECK_EQ(res.tbl[PLTM_TONE_BASE + 0xFF], 0xFFFF, "tone[255] pinned");

    CHECK_EQ(res.tbl[PLTM_GAIN_BASE], 0xFFFF, "gain[0] pinned");
    ok = 1;
    for (k = 1; k <= 254; k++)
        if (res.tbl[PLTM_GAIN_BASE + k] != PLTM_Q10_UNITY)
            ok = 0;
    CHECK(ok, "gain = unity for gain_cfg = 0x1000");
    CHECK_EQ(res.tbl[PLTM_GAIN_BASE + 0xFF], PLTM_Q10_UNITY, "gain[255]");
    pltm_exit(e);
}

static void test_manual_skips_modulation(void)
{
    pltm_clean_stats_t st;
    pltm_clean_result_t res;
    pltm_entity_t *e;
    pltm_clean_params_t *p = NULL;

    zero_stats(&st);
    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    /* contrast==256 would zero a strength-path clip; manual path ignores it. */
    p->config.contrast = 256;
    p->config.clip_cfg = 0x2000;
    p->config.gain_cfg = 0;

    pltm_run(e, &st, &res);
    CHECK_EQ(res.tbl[PLTM_TONE_BASE + 1], 0x200, "manual clip not modulated");
    CHECK_EQ(res.tbl[PLTM_GAIN_BASE + 1], 0x201, "manual gain passthrough");
    pltm_exit(e);
}

static void test_preset_table(void)
{
    int i, o, t, c, g;

    for (i = 0; i < PLTM_NPRESET; i++) {
        pltm_clean_preset(i, &o, &t, &c, &g);
        CHECK_EQ(o, i <= 15 ? 255 - 17 * i : 0, "preset oripic");
        CHECK_EQ(t, 5 + i / 2, "preset order");
        CHECK_EQ(c, 100 + 200 * i, "preset clip");
        CHECK_EQ(g, 4096 - 200 * i, "preset gain");
    }
    /* Out-of-range indices clamp to the ends. */
    pltm_clean_preset(-3, &o, &t, &c, &g);
    CHECK_EQ(c, 100, "preset clamp low");
    pltm_clean_preset(40, &o, &t, &c, &g);
    CHECK_EQ(c, 100 + 200 * 18, "preset clamp high");

    /* No injected presets: every step is neutral. */
    g_tab.presets = NULL;
    for (i = 0; i < PLTM_NPRESET; i++) {
        pltm_clean_preset(i, &o, &t, &c, &g);
        CHECK_EQ(o, 0xFF, "neutral oripic");
        CHECK_EQ(t, 5, "neutral order");
        CHECK_EQ(c, 0, "neutral clip");
        CHECK_EQ(g, 0x1000, "neutral gain");
    }
    g_tab.presets = g_presets;
}

/* Expected values below are worked by hand from the synthetic bank. */
static void test_strength_map(void)
{
    pltm_clean_params_t p;
    int o, t, l, c, g;

    base_params(&p);

    pltm_clean_strength_map(&p, 0x000, &o, &t, &l, &c, &g);
    CHECK_EQ(o, 255, "map 0x000 oripic");
    CHECK_EQ(t, 5, "map 0x000 order");
    CHECK_EQ(l, 15, "map 0x000 last");
    CHECK_EQ(c, 100, "map 0x000 clip");
    CHECK_EQ(g, 4096, "map 0x000 gain");

    pltm_clean_strength_map(&p, 0x100, &o, &t, &l, &c, &g);
    CHECK_EQ(o, 238, "map 0x100 oripic");
    CHECK_EQ(c, 300, "map 0x100 clip");
    CHECK_EQ(g, 3896, "map 0x100 gain");

    /* Fractional, order-bumped preset 3 (w = 1, low = 255 toward row 4). */
    pltm_clean_strength_map(&p, 0x3FF, &o, &t, &l, &c, &g);
    CHECK_EQ(o, 187, "map 0x3FF oripic");
    CHECK_EQ(t, 7, "map 0x3FF order bump");
    CHECK_EQ(l, 14, "map 0x3FF last");
    CHECK_EQ(c, 899, "map 0x3FF clip");
    CHECK_EQ(g, 3296, "map 0x3FF gain");

    /* high >= 11 forces last_order = 15, no bump (halfway rows 11/12). */
    pltm_clean_strength_map(&p, 0xB80, &o, &t, &l, &c, &g);
    CHECK_EQ(o, 59, "map 0xB80 oripic");
    CHECK_EQ(t, 10, "map 0xB80 order");
    CHECK_EQ(l, 15, "map 0xB80 last");
    CHECK_EQ(c, 2400, "map 0xB80 clip");
    CHECK_EQ(g, 1796, "map 0xB80 gain");

    /* Interpolation reaching preset 16. */
    pltm_clean_strength_map(&p, 0xFFF, &o, &t, &l, &c, &g);
    CHECK_EQ(t, 12, "map 0xFFF order");
    CHECK_EQ(l, 15, "map 0xFFF last");
    CHECK_EQ(c, 3299, "map 0xFFF clip");
    CHECK_EQ(g, 896, "map 0xFFF gain");

    /* high clamps idx to 15 even for out-of-range strengths. */
    pltm_clean_strength_map(&p, 0x2000, &o, &t, &l, &c, &g);
    CHECK_EQ(t, 12, "map 0x2000 order");
    CHECK_EQ(c, 3100, "map 0x2000 clip");
    CHECK_EQ(g, 1096, "map 0x2000 gain");

    /* contrast == 256 zeroes the clip. */
    p.config.contrast = 256;
    pltm_clean_strength_map(&p, 0x000, &o, &t, &l, &c, &g);
    CHECK_EQ(c, 0, "contrast 256 clip");
    p.config.contrast = 0;

    /* auto_strength < 17 scales the clip by /16. */
    p.config.auto_strength = 8;
    pltm_clean_strength_map(&p, 0x000, &o, &t, &l, &c, &g);
    CHECK_EQ(c, 50, "auto scale clip");
    p.config.auto_strength = 192;

    /* wdr_mode == 1 forces the gain blend to zero. */
    p.sensor.wdr_mode = 1;
    pltm_clean_strength_map(&p, 0x000, &o, &t, &l, &c, &g);
    CHECK_EQ(g, 0, "wdr forces gain 0");
}

static void test_strength_targets(void)
{
    pltm_clean_stats_t st;
    pltm_clean_result_t res;
    pltm_entity_t *e;
    pltm_clean_params_t *p = NULL;

    zero_stats(&st);

    /* Semi-manual: cal = manual_strength << 4. */
    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->config.mode = 1;
    p->config.manual_strength = 0x10;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.cal_strength, 0x100, "semi-manual target");
    pltm_exit(e);

    /* Fixed-level: mode 0 with auto_strength < 17. */
    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->config.mode = 0;
    p->config.auto_strength = 8;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.cal_strength, 0x80, "fixed-level target");
    pltm_exit(e);

    /* Automatic: statistics through the strength bank saturate at 0xFFF. */
    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->config.mode = 0;
    p->config.auto_strength = 200;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.cal_strength, 0xFFF, "automatic target saturates");
    pltm_exit(e);
}

static void test_feedback_and_limiter(void)
{
    pltm_clean_stats_t st;
    pltm_clean_result_t res;
    pltm_entity_t *e;
    pltm_clean_params_t *p = NULL;

    zero_stats(&st);

    /* Deadband: measured minimum equals the target. */
    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->config.mode = 1;
    p->config.manual_strength = 0x20;
    st.measured_min = 43; /* derived target */
    pltm_run(e, &st, &res);
    CHECK_EQ(res.min_threshold, 43, "derived min target");
    CHECK_EQ(res.next_strength, 0, "deadband holds a rising strength");
    pltm_exit(e);

    /* Confirmation gate: first frame suppresses, second frame adapts. */
    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->config.mode = 1;
    p->config.manual_strength = 0x20;
    st.measured_min = 0;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.next_strength, 0, "confirmation suppresses first frame");
    p->config.frame_id = 4;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.next_strength, 127, "adaptive converge step applies");
    CHECK_EQ(res.old_strength, 127, "old strength carries");

    /* Frozen strength while exposure is unsettled. */
    p->sensor.ae_settled = 0;
    p->config.frame_id = 5;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.old_strength, 127, "strength frozen when unsettled");
    CHECK_EQ(res.next_strength, 127, "next strength frozen when unsettled");
    CHECK_EQ(res.cal_en, 1, "cal_en latched");
    pltm_exit(e);
}

static void test_dispatcher_gating(void)
{
    pltm_clean_stats_t st;
    pltm_clean_result_t res;
    pltm_entity_t *e;
    pltm_clean_params_t *p = NULL;

    zero_stats(&st);

    /* Latch: a non-qualifying interval frame does not clear the flags. */
    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->config.interval = 2;
    p->config.frame_id = 3;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.cal_en, 1, "qualifying frame cal_en");
    CHECK_EQ(res.frame_smooth_en, 1, "qualifying frame smooth");
    p->config.frame_id = 4;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.cal_en, 1, "cal_en latched");
    CHECK_EQ(res.frame_smooth_en, 1, "smooth latched");

    p->config.enable = 0;
    p->config.frame_id = 5;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.cal_en, 0, "disabled clears cal_en");
    CHECK_EQ(res.ae_comp, 0, "disabled clears ae_comp");
    CHECK_EQ(res.next_strength, 0, "disabled clears next_strength");
    pltm_exit(e);

    /* Pre-start: only frame start-1 seeds the strength. */
    pltm_clean_set_start_frame(10);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->config.mode = 1;
    p->config.manual_strength = 0x10;
    p->config.frame_id = 9;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.cal_en, 1, "seed frame cal_en");
    CHECK_EQ(res.frame_smooth_en, 0, "seed frame no smooth");
    CHECK_EQ(res.next_strength, 0x10, "seed stores raw manual strength");
    CHECK_EQ(res.old_strength, 0x10, "seed carries to old");
    p->config.frame_id = 8;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.cal_en, 0, "pre-seed frame cal_en clear");
    pltm_exit(e);
}

static void test_speed_index_gate(void)
{
    pltm_clean_stats_t st;
    pltm_clean_result_t res;
    pltm_entity_t *e;
    pltm_clean_params_t *p = NULL;
    int d;

    zero_stats(&st);
    for (d = 0; d < PLTM_CONV_COLS; d++) {
        g_converge[0 * PLTM_CONV_COLS + d] = 1;
        g_converge[1 * PLTM_CONV_COLS + d] = 5;
    }

    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->config.mode = 1;
    p->config.manual_strength = 0x20;
    p->config.frame_id = 0x50;
    p->config.speed = 1; /* row 1 is only reachable at frame >= 0x50 */
    st.measured_min = 0;

    pltm_run(e, &st, &res);
    pltm_run(e, &st, &res);
    CHECK_EQ(res.next_strength, 5, "speed row selects converge bank row");
    pltm_exit(e);

    for (d = 0; d < PLTM_CONV_COLS; d++) {
        g_converge[0 * PLTM_CONV_COLS + d] = (uint8_t)d;
        g_converge[1 * PLTM_CONV_COLS + d] = 0;
    }
}

static void test_sparse_tables(void)
{
    pltm_clean_stats_t st;
    pltm_clean_result_t res;
    pltm_entity_t *e;
    pltm_clean_params_t *p = NULL;

    zero_stats(&st);
    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->sensor.wdr_mode = 1;
    p->config.bit_offset = 2;
    p->config.clip_cfg = 0x100;
    p->config.gain_cfg = 0x800;

    pltm_run(e, &st, &res);

    CHECK_EQ(res.tbl[PLTM_TONE_BASE], 0, "sparse tone[0]");
    CHECK_EQ(res.tbl[PLTM_GAIN_BASE], 0x3FFF, "sparse gain[0] = tbl_max");
    CHECK_EQ(res.tbl[PLTM_TONE_BASE + 1], 0x41, "sparse tone[1]");
    CHECK_EQ(res.tbl[PLTM_TONE_BASE + 62], 0x3E0, "sparse tone[62]");
    CHECK_EQ(res.tbl[PLTM_TONE_BASE + 248], 0x3FFF, "sparse tone tail");
    CHECK_EQ(res.tbl[PLTM_GAIN_BASE + 248], 0x400, "sparse gain tail");
    CHECK_EQ(res.tbl[PLTM_TONE_BASE + 63], 0, "sparse unsampled gap");
    CHECK_EQ(res.tbl[PLTM_GAIN_BASE + 63], 0, "sparse gain unsampled gap");
    CHECK_EQ(res.tbl[PLTM_TONE_BASE + 64], 0x3FFF, "tone tail starts at 0x100>>off");
    CHECK_EQ(res.tbl[PLTM_GAIN_BASE + 64], 0x400, "gain tail starts at 0x100>>off");
    CHECK_EQ(res.tbl[PLTM_GAIN_BASE + 1], 0x302, "sparse gain[1] source unshifted");
    CHECK_EQ(res.tbl[PLTM_GAIN_BASE + 62], 0x37C, "sparse gain[62] source unshifted");
    pltm_exit(e);
}

static void test_sparse_off_zero(void)
{
    pltm_clean_stats_t st;
    pltm_clean_result_t res;
    pltm_entity_t *e;
    pltm_clean_params_t *p = NULL;

    zero_stats(&st);
    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->sensor.wdr_mode = 1;
    p->config.bit_offset = 0;
    p->config.clip_cfg = 16;
    p->config.gain_cfg = 0;

    pltm_run(e, &st, &res);
    CHECK_EQ(res.tbl[PLTM_TONE_BASE], 0, "off-0 tone[0]");
    CHECK_EQ(res.tbl[PLTM_TONE_BASE + 254], 0x1FE, "off-0 tone[254]");
    CHECK_EQ(res.tbl[PLTM_TONE_BASE + 0xFF], 0, "off-0 tone[255] unwritten");
    CHECK_EQ(res.tbl[PLTM_GAIN_BASE], 0xFFFF, "off-0 gain[0]");
    CHECK_EQ(res.tbl[PLTM_GAIN_BASE + 0xFE], 0x2FE, "off-0 gain[254] not tailed");
    CHECK_EQ(res.tbl[PLTM_GAIN_BASE + 0xFF], 0, "off-0 gain[255] unwritten");
    pltm_exit(e);
}

static void test_merge_degenerate_eta(void)
{
    pltm_clean_stats_t st;
    pltm_clean_result_t res;
    pltm_entity_t *e;
    pltm_clean_params_t *p = NULL;
    uint16_t ref[128];
    int i, ok;

    zero_stats(&st);
    pltm_clean_set_start_frame(3);

    /* n = 1: eta is zero on both axes, so every gaussian weight is zero. */
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->config.frame_id = 3;
    p->sensor.width = 1;
    p->sensor.height = 1;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.tbl[PLTM_MERGE_H_BASE], 0, "n=1 horizontal entry 0");
    CHECK_EQ(res.tbl[PLTM_MERGE_H_BASE + 1], 0, "n=1 horizontal entry 1");
    CHECK_EQ(res.tbl[PLTM_MERGE_V_BASE], 0, "n=1 vertical entry 0");
    CHECK_EQ(res.tbl[PLTM_MERGE_V_BASE + 1], 0, "n=1 vertical entry 1");
    merge_ref(ref, 1);
    ok = 1;
    for (i = 0; i < 128; i++) {
        if (res.tbl[i] != ref[i] ||
            res.tbl[PLTM_MERGE_V_BASE + i] != ref[i])
            ok = 0;
    }
    CHECK(ok, "n=1 matches degenerate reference");
    pltm_exit(e);

    /* n = 2: eta1 is zero while eta2 is non-zero; the blend still collapses. */
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->config.frame_id = 3;
    p->sensor.width = 2;
    p->sensor.height = 2;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.tbl[PLTM_MERGE_H_BASE], 0, "n=2 horizontal entry 0");
    CHECK_EQ(res.tbl[PLTM_MERGE_H_BASE + 1], 0, "n=2 horizontal entry 1");
    CHECK_EQ(res.tbl[PLTM_MERGE_V_BASE], 0, "n=2 vertical entry 0");
    CHECK_EQ(res.tbl[PLTM_MERGE_V_BASE + 1], 0, "n=2 vertical entry 1");
    merge_ref(ref, 2);
    ok = 1;
    for (i = 0; i < 128; i++) {
        if (res.tbl[i] != ref[i] ||
            res.tbl[PLTM_MERGE_V_BASE + i] != ref[i])
            ok = 0;
    }
    CHECK(ok, "n=2 matches degenerate reference");
    pltm_exit(e);
}

static void test_merge_odd_width_count(void)
{
    /* { sensor_w, sensor_h, one_past_horizontal, one_past_vertical } */
    static const int cases[][4] = {
        { 32, 18, 17, 10 },
        { 64, 64, 33, 33 },
        {  8, 16,  5,  9 },
    };
    pltm_clean_stats_t st;
    pltm_clean_result_t res;
    pltm_entity_t *e;
    pltm_clean_params_t *p = NULL;
    unsigned c;

    zero_stats(&st);
    pltm_clean_set_start_frame(3);
    for (c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        e = make_entity(&p);
        if (e == NULL) {
            CHECK(0, "entity");
            return;
        }
        p->config.frame_id = 3;
        p->sensor.width = cases[c][0];
        p->sensor.height = cases[c][1];
        pltm_run(e, &st, &res);
        CHECK(res.tbl[cases[c][2] - 1] != 0, "merge last emitted entry valid");
        CHECK_EQ(res.tbl[cases[c][2]], 0, "merge one past count is zero");
        CHECK(res.tbl[PLTM_MERGE_V_BASE + cases[c][3] - 1] != 0,
              "merge vertical last emitted entry valid");
        CHECK_EQ(res.tbl[PLTM_MERGE_V_BASE + cases[c][3]], 0,
                 "merge vertical one past count is zero");
        pltm_exit(e);
    }
}

static void test_merge_geometry_golden(void)
{
    /* Expected entries taken from the withheld golden vectors.
     * Cases are named block_width x block_height; the merge builder receives
     * tile counts (block + 1) and derives the entry count from the span, so
     * even spans (31x17, 7x15, 63x63, 119x66) pin the span-1 rule. */
    pltm_clean_result_t res;

    pltm_clean_merge_build(&res, 31, 31); /* 30x30 */
    CHECK_EQ(res.tbl[2], 55309, "30x30 h[2]");
    CHECK_EQ(res.tbl[16], 31232, "30x30 h[16]");
    CHECK_EQ(res.tbl[17], 0, "30x30 h[17] zero");
    CHECK_EQ(res.tbl[PLTM_MERGE_V_BASE + 2], 55309, "30x30 v[2]");
    CHECK_EQ(res.tbl[PLTM_MERGE_V_BASE + 16], 31232, "30x30 v[16]");

    pltm_clean_merge_build(&res, 32, 18); /* 31x17 */
    CHECK_EQ(res.tbl[2], 53776, "31x17 h[2]");
    CHECK_EQ(res.tbl[16], 32512, "31x17 h[16]");
    CHECK_EQ(res.tbl[17], 0, "31x17 h[17] zero");
    CHECK_EQ(res.tbl[PLTM_MERGE_V_BASE + 2], 55049, "31x17 v[2]");
    CHECK_EQ(res.tbl[PLTM_MERGE_V_BASE + 6], 45314, "31x17 v[6]");
    CHECK_EQ(res.tbl[PLTM_MERGE_V_BASE + 10], 0, "31x17 v[10] zero");

    pltm_clean_merge_build(&res, 64, 64); /* 63x63 */
    CHECK_EQ(res.tbl[2], 54035, "63x63 h[2]");
    CHECK_EQ(res.tbl[21], 44036, "63x63 h[21]");
    CHECK_EQ(res.tbl[33], 0, "63x63 h[33] zero");

    pltm_clean_merge_build(&res, 8, 16); /* 7x15 */
    CHECK_EQ(res.tbl[2], 54785, "7x15 h[2]");
    CHECK_EQ(res.tbl[3], 45312, "7x15 h[3]");
    CHECK_EQ(res.tbl[4], 32768, "7x15 h[4]");
    CHECK_EQ(res.tbl[5], 0, "7x15 h[5] zero");
    CHECK_EQ(res.tbl[PLTM_MERGE_V_BASE + 2], 55304, "7x15 v[2]");
    CHECK_EQ(res.tbl[PLTM_MERGE_V_BASE + 8], 32768, "7x15 v[8]");
    CHECK_EQ(res.tbl[PLTM_MERGE_V_BASE + 9], 0, "7x15 v[9] zero");

    pltm_clean_merge_build(&res, 101, 51); /* 100x50 */
    CHECK_EQ(res.tbl[2], 53525, "100x50 h[2]");

    pltm_clean_merge_build(&res, 120, 67); /* 119x66 */
    CHECK_EQ(res.tbl[2], 52759, "119x66 h[2]");
}

static void test_sparse_tail_offset(void)
{
    pltm_clean_stats_t st;
    pltm_clean_result_t res;
    pltm_entity_t *e;
    pltm_clean_params_t *p = NULL;

    zero_stats(&st);
    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->sensor.wdr_mode = 1;
    p->config.bit_offset = 1;
    p->config.clip_cfg = 0x100;
    p->config.gain_cfg = 0x800;

    pltm_run(e, &st, &res);
    CHECK_EQ(res.tbl[PLTM_GAIN_BASE], 0x7FFF, "off-1 gain[0] = tbl_max");
    CHECK_EQ(res.tbl[PLTM_GAIN_BASE + 1], 0x301, "off-1 gain source unshifted");
    CHECK_EQ(res.tbl[PLTM_TONE_BASE + 127], 0, "off-1 tone gap before tail");
    CHECK_EQ(res.tbl[PLTM_GAIN_BASE + 127], 0, "off-1 gain gap before tail");
    CHECK_EQ(res.tbl[PLTM_TONE_BASE + 128], 0x7FFF, "off-1 tone tail start");
    CHECK_EQ(res.tbl[PLTM_GAIN_BASE + 128], 0x400, "off-1 gain tail start");
    pltm_exit(e);
}

static void test_strength_hold(void)
{
    pltm_clean_stats_t st;
    pltm_clean_result_t res;
    pltm_entity_t *e;
    pltm_clean_params_t *p = NULL;

    zero_stats(&st);

    /* Seed old_strength on the frame before start, then request a rise whose
     * minimum-level error is inside the deadband: the strength must hold. */
    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->config.mode = 1;
    p->config.manual_strength = 0x100;
    p->config.frame_id = 2;
    st.measured_min = 0x100;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.next_strength, 0x100, "seed stores raw strength");
    CHECK_EQ(res.old_strength, 0x100, "seed carries to old");

    p->config.manual_strength = 0x101; /* cal = 0x1010, a rise */
    p->config.frame_id = 3;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.min_threshold, 0x100, "hold target clamps");
    CHECK_EQ(res.cal_strength, 0x1010, "hold calibrated strength");
    CHECK_EQ(res.next_strength, 0x100, "deadband holds a rising strength");
    CHECK_EQ(res.old_strength, 0x100, "held strength carries");
    pltm_exit(e);

    /* The same hold applies with WDR enabled. */
    pltm_clean_set_start_frame(3);
    e = make_entity(&p);
    if (e == NULL) {
        CHECK(0, "entity");
        return;
    }
    p->config.mode = 1;
    p->config.manual_strength = 0x100;
    p->config.frame_id = 2;
    p->sensor.wdr_mode = 1;
    p->config.bit_offset = 1;
    st.measured_min = 0x100;
    pltm_run(e, &st, &res);
    p->config.manual_strength = 0x101;
    p->config.frame_id = 3;
    pltm_run(e, &st, &res);
    CHECK_EQ(res.next_strength, 0x100, "wdr deadband holds a rising strength");
    pltm_exit(e);
}

int main(void)
{
    init_tables();

    test_init_requires_tables();
    test_lifecycle_and_params();
    test_null_run();
    test_tile_geometry();
    test_merge_table();
    test_merge_degenerate_eta();
    test_merge_odd_width_count();
    test_merge_geometry_golden();
    test_dense_manual_tables();
    test_manual_skips_modulation();
    test_preset_table();
    test_strength_map();
    test_strength_targets();
    test_feedback_and_limiter();
    test_strength_hold();
    test_dispatcher_gating();
    test_speed_index_gate();
    test_sparse_tables();
    test_sparse_tail_offset();
    test_sparse_off_zero();

    printf("PLTM clean-room tests: %d checks, %d failures\n", g_checks,
           g_failures);
    return g_failures == 0 ? 0 : 1;
}
