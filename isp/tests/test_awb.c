/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_awb.c - host tests for the AWB module. This file is the runtime table
 * provider (freeisp_get_tables()); all tables are synthetic fixtures. */
#include "awb_clean.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;
static int g_pass;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); } \
} while (0)

/* ------------------------------------------------------------------ */
/* Runtime tables (test fixtures)                                     */
/* ------------------------------------------------------------------ */

static uint8_t  t_trust[AWB_NTRUST * 256];
static uint16_t t_speed[AWB_NSPEED * AWB_NSSC];
static char     t_label[AWB_NREF][32];
static int32_t  t_std_trust[AWB_NREF];
static int32_t  t_temp_bright[AWB_NREF * AWB_NREF];
static const uint16_t t_safe_gain[4] = { 0x0180, 0x0100, 0x0100, 0x0140 };

static awb_clean_tables_t g_tab;
static freeisp_tables_t   g_ft;

const freeisp_tables_t *freeisp_get_tables(void)
{
    return &g_ft;
}

static void init_tables(void)
{
    int r, k, i;

    for (r = 0; r < AWB_NTRUST; r++) {
        for (k = 0; k < 256; k++)
            t_trust[r * 256 + k] = (k < 160) ? (uint8_t)(32 - k / 8) : 0;
    }
    memset(t_speed, 0, sizeof(t_speed));
    for (k = 0; k < AWB_NGHIST; k++)
        t_speed[0 * AWB_NSSC + k] = (k == 0) ? 64 : 0;
    for (r = 1; r < AWB_NSPEED; r++)
        for (k = 0; k < AWB_NGHIST; k++)
            t_speed[r * AWB_NSSC + k] = 1;

    for (i = 0; i < AWB_NREF; i++) {
        int j;
        for (j = 0; j < 31; j++)
            t_label[i][j] = (j == 0) ? (char)('0' + i) : 0;
        t_label[i][31] = 0;
        t_std_trust[i] = 64;
        for (j = 0; j < AWB_NREF; j++)
            t_temp_bright[i * AWB_NREF + j] = 20;
    }

    g_tab.trust = t_trust;
    g_tab.speed_w = t_speed;
    g_tab.class_label = &t_label[0][0];
    g_tab.std_trust = t_std_trust;
    g_tab.temp_bright = t_temp_bright;
    g_tab.safe_gain = t_safe_gain;
    g_ft.awb = &g_tab;
}

/* ------------------------------------------------------------------ */
/* Parameter fixture                                                  */
/* ------------------------------------------------------------------ */

static void put_ref(int32_t *dst, const int32_t ref[3], int32_t tol,
                    int32_t temp, int32_t prob_ls, int32_t prob_pref)
{
    memset(dst, 0, AWB_REF_INTS * sizeof(int32_t));
    dst[0] = ref[0]; dst[1] = ref[1]; dst[2] = ref[2];
    dst[3] = 256; dst[4] = 256; dst[5] = 256;
    dst[6] = tol;
    dst[7] = temp;
    dst[8] = prob_ls;
    dst[9] = prob_pref;
}

static void base_params(awb_params_t *p)
{
    static const int32_t warm[3] = {300, 280, 180};
    static const int32_t mid[3]  = {280, 280, 220};
    static const int32_t cool[3] = {250, 280, 280};

    memset(p, 0, sizeof(*p));
    p->mode = 1;
    p->interval = 1;
    p->speed = 10;
    p->base_temp = 6500;
    p->green_dist = 1;
    p->blue_dist = 1;
    p->light_num = 3;
    p->skin_num = 0;
    p->special_num = 0;
    p->ae_lv = 800;
    p->ae_index = 5;
    p->ae_index_max = 100;
    p->ae_done = 1;
    p->control_enable = 1;
    p->r_favor = 0;
    p->b_favor = 0;

    put_ref(&p->light_info[0 * AWB_REF_INTS], warm, 1, 2800, 200, 100);
    put_ref(&p->light_info[1 * AWB_REF_INTS], mid,  1, 4000, 200, 100);
    put_ref(&p->light_info[2 * AWB_REF_INTS], cool, 1, 6000, 200, 100);
}

/* Warm class on a long chromaticity segment: a window near an interpolated
 * curve point but far from every anchor exposes the 16-bit distance clamp. */
static void sat_params(awb_params_t *p)
{
    static const int32_t warm_a[3] = {0, 256, 0};
    static const int32_t warm_b[3] = {640, 256, 0};
    static const int32_t mid[3]    = {603, 256, 0};
    static const int32_t cool[3]   = {900, 256, 0};

    base_params(p);
    memset(p->light_info, 0, sizeof(p->light_info));
    p->light_num = 4;
    put_ref(&p->light_info[0 * AWB_REF_INTS], warm_a, 1, 1500, 200, 100);
    put_ref(&p->light_info[1 * AWB_REF_INTS], warm_b, 1, 3000, 200, 100);
    put_ref(&p->light_info[2 * AWB_REF_INTS], mid,    1, 4000, 200, 100);
    put_ref(&p->light_info[3 * AWB_REF_INTS], cool,   1, 6000, 200, 100);
}

/* Tie rule (spec 6.3 step 2): classes 1/3/4 tie at 40000, class 0 is nearest;
 * class 4's curve wins only if the tie for second resolves to the later index. */
static const int32_t tie_a0a[3] = {200, 256, 200};
static const int32_t tie_a0b[3] = {600, 256, 300};
static const int32_t tie_a1f[3] = {300, 256, 500};
static const int32_t tie_a1s[3] = {300, 256, 600};
static const int32_t tie_a2[3]  = {300, 256, 100};

static void tie_params(awb_params_t *p)
{
    base_params(p);
    memset(p->light_info, 0, sizeof(p->light_info));
    p->light_num = 5;
    put_ref(&p->light_info[0 * AWB_REF_INTS], tie_a0a, 0, 2500, 64, 100);
    put_ref(&p->light_info[1 * AWB_REF_INTS], tie_a0b, 0, 3000, 64, 100);
    put_ref(&p->light_info[2 * AWB_REF_INTS], tie_a1f, 0, 3600, 64, 100);
    put_ref(&p->light_info[3 * AWB_REF_INTS], tie_a1s, 0, 4000, 64, 100);
    put_ref(&p->light_info[4 * AWB_REF_INTS], tie_a2,  0, 6000, 64, 100);
}

static void fill_scene(awb_stats_t *st, uint32_t R, uint32_t G, uint32_t B)
{
    int i;
    for (i = 0; i < AWB_NWIN; i++) {
        st->win[i].avg[0] = R;
        st->win[i].avg[1] = G;
        st->win[i].avg[2] = B;
        st->win[i].npix = 100;
    }
}

static void run_frames(awb_entity_t *e, const awb_stats_t *st,
                       int first_id, int count, awb_result_t *last)
{
    awb_params_t *pp;
    awb_result_t res;
    int i;

    awb_get_params(e, &pp);
    memset(&res, 0, sizeof(res));
    for (i = 0; i < count; i++) {
        pp->frame_id = first_id + i;
        awb_run(e, st, &res);
    }
    if (last != NULL)
        *last = res;
}

/* ------------------------------------------------------------------ */

static void test_lifecycle(void)
{
    awb_params_t par;
    awb_ops_t ops;
    awb_entity_t *e;
    awb_param_req_t req;
    awb_result_t res;

    base_params(&par);
    memset(&ops, 0, sizeof(ops));
    e = awb_init(&ops);
    CHECK(e != NULL, "awb_init returns entity");
    CHECK(ops.get_params == awb_get_params, "ops.get_params wired");
    CHECK(ops.set_params == awb_set_params, "ops.set_params wired");
    CHECK(ops.run == awb_run, "ops.run wired");
    CHECK(ops.isr == awb_isr, "ops.isr wired");

    memset(&req, 0, sizeof(req));
    req.kind = AWB_PARAM_INIT;
    req.params = &par;
    CHECK(awb_set_params(e, &req, NULL) == 0, "init-data accepted");

    req.kind = 99;
    CHECK(awb_set_params(e, &req, NULL) == -1, "unknown param kind rejected");

    {
        awb_params_t *out = NULL;
        CHECK(awb_get_params(e, &out) == 0, "get_params succeeds");
        CHECK(out != NULL, "get_params returns storage");
        CHECK(awb_get_params(NULL, &out) == -1, "get_params null entity");
    }

    memset(&res, 0, sizeof(res));
    CHECK(awb_run(NULL, NULL, &res) == -1, "null run returns -1");
    CHECK(res.gain_out.r == 0x0180 && res.gain_out.gr == 0x0100 &&
          res.gain_out.gb == 0x0100 && res.gain_out.b == 0x0140,
          "null run seeds the deployed safe gains");
    CHECK(res.color_temp_out == 6500, "null run seeds 6500K");

    awb_exit(e);
}

static void test_missing_tables(void)
{
    awb_entity_t *e;
    const awb_clean_tables_t *saved = g_ft.awb;

    g_ft.awb = NULL;
    e = awb_init(NULL);
    CHECK(e == NULL, "init fails without a table provider");
    g_ft.awb = saved;
}

static void test_fixed_temperature(void)
{
    awb_params_t par;
    awb_entity_t *e;
    awb_param_req_t req;
    awb_result_t res;
    awb_stats_t st;

    base_params(&par);
    par.control_enable = 0;
    par.fixed_temp = 0;
    e = awb_init(NULL);
    req.kind = AWB_PARAM_INIT;
    req.params = &par;
    awb_set_params(e, &req, NULL);

    fill_scene(&st, 200, 200, 200);
    memset(&res, 0, sizeof(res));
    awb_run(e, &st, &res);
    CHECK(res.color_temp_out == 6500, "fixed temp 0 means 6500K");
    CHECK(res.gain_out.gr == 256 && res.gain_out.gb == 256, "fixed green unity");
    CHECK(res.gain_out.r > 0 && res.gain_out.b > 0, "fixed gains nonzero");
    awb_exit(e);

    base_params(&par);
    par.control_enable = 0;
    par.fixed_temp = 5000;
    e = awb_init(NULL);
    req.kind = AWB_PARAM_INIT;
    req.params = &par;
    awb_set_params(e, &req, NULL);
    memset(&res, 0, sizeof(res));
    awb_run(e, &st, &res);
    CHECK(res.color_temp_out == 5000, "explicit fixed temp reported");
    awb_exit(e);
}

static void test_manual(void)
{
    awb_params_t par;
    awb_entity_t *e;
    awb_param_req_t req;
    awb_result_t res;
    awb_stats_t st;

    base_params(&par);
    par.mode = 0;
    par.manual_gain[0] = 300;
    par.manual_gain[1] = 256;
    par.manual_gain[2] = 256;
    par.manual_gain[3] = 400;
    e = awb_init(NULL);
    req.kind = AWB_PARAM_INIT;
    req.params = &par;
    awb_set_params(e, &req, NULL);

    fill_scene(&st, 128, 128, 128);
    memset(&res, 0, sizeof(res));
    {
        awb_params_t *pp;
        awb_get_params(e, &pp);
        pp->frame_id = 5;
    }
    awb_run(e, &st, &res);
    CHECK(res.gain_out.r == 300, "manual red passthrough");
    CHECK(res.gain_out.gr == 256, "manual gr passthrough");
    CHECK(res.gain_out.gb == 256, "manual gb passthrough");
    CHECK(res.gain_out.b == 400, "manual blue passthrough");
    awb_exit(e);
}

static void test_adaptive_greyworld(void)
{
    awb_params_t par;
    awb_entity_t *e;
    awb_param_req_t req;
    awb_result_t res, again;
    awb_stats_t st;
    int ok = 1;

    base_params(&par);
    e = awb_init(NULL);
    req.kind = AWB_PARAM_INIT;
    req.params = &par;
    awb_set_params(e, &req, NULL);

    fill_scene(&st, 200, 200, 157);   /* matches the mid illuminant */
    run_frames(e, &st, 3, 60, &res);

    CHECK(res.gain_out.r >= AWB_GAIN_MIN && res.gain_out.r <= AWB_GAIN_MAX,
          "adaptive red in range");
    CHECK(res.gain_out.gr >= AWB_GAIN_MIN && res.gain_out.gr <= AWB_GAIN_MAX,
          "adaptive gr in range");
    CHECK(res.gain_out.gb >= AWB_GAIN_MIN && res.gain_out.gb <= AWB_GAIN_MAX,
          "adaptive gb in range");
    CHECK(res.gain_out.b >= AWB_GAIN_MIN && res.gain_out.b <= AWB_GAIN_MAX,
          "adaptive blue in range");
    CHECK(res.color_temp_out >= 3000 && res.color_temp_out <= 5000,
          "adaptive colour temperature near the mid illuminant");
    CHECK(awb_clean_frame_count(e) > 0, "frame counter advanced");

    /* Constant input must settle: two more frames agree exactly. */
    {
        awb_result_t a, b;
        run_frames(e, &st, 200, 1, &a);
        run_frames(e, &st, 201, 1, &b);
        if (a.gain_out.r != b.gain_out.r || a.gain_out.gr != b.gain_out.gr ||
            a.gain_out.gb != b.gain_out.gb || a.gain_out.b != b.gain_out.b ||
            a.color_temp_out != b.color_temp_out)
            ok = 0;
    }
    CHECK(ok, "steady-state output is stable");

    /* Neutral grey must not be wildly tinted after constraint. */
    fill_scene(&st, 200, 200, 200);
    run_frames(e, &st, 300, 60, &again);
    CHECK(again.gain_out.r >= 32 && again.gain_out.r <= 2048,
          "neutral red bounded");
    CHECK(again.color_temp_out > 0, "neutral colour temperature reported");

    awb_exit(e);
}

static void test_speed_smoothing(void)
{
    awb_params_t par;
    awb_entity_t *e;
    awb_param_req_t req;
    awb_result_t res;
    awb_stats_t st;

    base_params(&par);
    par.speed = 0;
    e = awb_init(NULL);
    req.kind = AWB_PARAM_INIT;
    req.params = &par;
    awb_set_params(e, &req, NULL);

    fill_scene(&st, 200, 200, 157);
    run_frames(e, &st, 3, 60, &res);
    CHECK(awb_clean_frame_count(e) == 60, "one smoothed frame per gate");
    CHECK(res.gain_out.gr >= AWB_GAIN_MIN && res.gain_out.gr <= AWB_GAIN_MAX,
          "speed 0 output bounded");

    awb_exit(e);
}

static void test_smoothing_warmup_taps(void)
{
    awb_params_t par;
    awb_entity_t *e;
    awb_param_req_t req;
    awb_result_t res;
    awb_stats_t st;

    base_params(&par);
    par.speed = 22;                 /* flat 48-tap row in the fixture */
    e = awb_init(NULL);
    req.kind = AWB_PARAM_INIT;
    req.params = &par;
    awb_set_params(e, &req, NULL);

    fill_scene(&st, 200, 200, 157);
    /* frame_count 0..7: the window is one tap short of the full ring. */
    run_frames(e, &st, 3, 8, &res);

    CHECK(awb_clean_frame_count(e) == 8, "warm-up advanced eight frames");
    /* Only taps holding history are averaged; averaging all 48 would blend in
     * the unity-seeded ring and tint a constant scene. */
    CHECK(res.gain_out.r == 256 && res.gain_out.gr == 256 &&
          res.gain_out.gb == 256 && res.gain_out.b == 326,
          "warm-up averages only the available taps");
    CHECK(res.color_temp_out == 4000, "warm-up colour temperature");

    awb_exit(e);
}

static void test_class_distance_saturation(void)
{
    awb_params_t par;
    awb_entity_t *e;
    awb_param_req_t req;
    awb_result_t res;
    awb_stats_t st;

    sat_params(&par);
    e = awb_init(NULL);
    req.kind = AWB_PARAM_INIT;
    req.params = &par;
    awb_set_params(e, &req, NULL);

    /* All anchors are >65535 away (kr=320, kb=3); the 16-bit clamp ties them so
     * class 0 wins (distance 21); 32-bit distances would pick the mid class. */
    fill_scene(&st, 250, 200, 2);
    run_frames(e, &st, 5, 1, &res);

    CHECK(awb_clean_window_class(e, 4) == 0,
          "16-bit clamp selects the first class on a tie");
    CHECK(awb_clean_window_dist(e, 4) == 21,
          "16-bit clamp keeps the curve-refined distance");
    CHECK(awb_clean_window_temp(e, 4) == 2300,
          "16-bit clamp colour temperature");
    /* Segment = first curve entry at-or-above the window temperature
     * (non-strict), so an exact match selects that entry. */
    CHECK(awb_clean_window_seg(e, 4) == 8,
          "16-bit clamp segment");
    CHECK(awb_clean_window_level(e, 4) == 320,
          "16-bit clamp trust level");

    awb_exit(e);
}

static void test_class_second_tie(void)
{
    awb_params_t par;
    awb_entity_t *e;
    awb_param_req_t req;
    awb_result_t res;
    awb_stats_t st;

    tie_params(&par);
    e = awb_init(NULL);
    req.kind = AWB_PARAM_INIT;
    req.params = &par;
    awb_set_params(e, &req, NULL);

    /* avg 234/200/234 -> kr = kb = 300. */
    fill_scene(&st, 234, 200, 234);
    run_frames(e, &st, 5, 1, &res);

    CHECK(awb_clean_window_class(e, 0) == 4,
          "tie for second place resolves to the later class");
    CHECK(awb_clean_window_dist(e, 0) == 13, "tie-break distance");
    CHECK(awb_clean_window_temp(e, 0) == 4880, "tie-break colour temperature");
    CHECK(awb_clean_window_seg(e, 0) == 40, "tie-break segment");

    awb_exit(e);
}

static void test_night_detection(void)
{
    awb_params_t par;
    awb_entity_t *e;
    awb_param_req_t req;
    awb_result_t res;
    awb_stats_t st;

    base_params(&par);
    par.ae_lv = 100;
    e = awb_init(NULL);
    req.kind = AWB_PARAM_INIT;
    req.params = &par;
    awb_set_params(e, &req, NULL);
    fill_scene(&st, 10, 10, 10);      /* R+G+B = 30 < 84 */
    run_frames(e, &st, 5, 1, &res);
    CHECK(awb_clean_is_night(e) == 1, "dark scene detected as night");
    awb_exit(e);

    base_params(&par);
    par.ae_lv = 100;
    e = awb_init(NULL);
    req.kind = AWB_PARAM_INIT;
    req.params = &par;
    awb_set_params(e, &req, NULL);
    fill_scene(&st, 200, 200, 200);
    run_frames(e, &st, 5, 1, &res);
    CHECK(awb_clean_is_night(e) == 0, "bright scene is not night");
    awb_exit(e);

    base_params(&par);
    par.ae_lv = 800;                  /* above the LV ceiling */
    e = awb_init(NULL);
    req.kind = AWB_PARAM_INIT;
    req.params = &par;
    awb_set_params(e, &req, NULL);
    fill_scene(&st, 10, 10, 10);
    run_frames(e, &st, 5, 1, &res);
    CHECK(awb_clean_is_night(e) == 0, "high LV overrides dark windows");
    awb_exit(e);
}

static void test_preset_scene(void)
{
    awb_params_t par;
    awb_entity_t *e;
    awb_param_req_t req;
    awb_result_t res;
    awb_stats_t st;

    base_params(&par);
    par.mode = 2;
    par.preset_gain[2 * 2] = 140;
    par.preset_gain[2 * 2 + 1] = 470;
    e = awb_init(NULL);
    req.kind = AWB_PARAM_INIT;
    req.params = &par;
    awb_set_params(e, &req, NULL);

    fill_scene(&st, 200, 200, 157);
    run_frames(e, &st, 3, 60, &res);
    CHECK(res.gain_out.gr == res.gain_out.gb, "preset keeps green pair equal");
    CHECK(res.gain_out.r < res.gain_out.gr, "warm preset pulls red down");
    CHECK(res.gain_out.b > res.gain_out.gr, "warm preset pushes blue up");

    awb_exit(e);
}

static void test_lock(void)
{
    awb_params_t par;
    awb_entity_t *e;
    awb_param_req_t req;
    awb_result_t res, locked;
    awb_stats_t st;

    base_params(&par);
    par.lock = 1;
    e = awb_init(NULL);
    req.kind = AWB_PARAM_INIT;
    req.params = &par;
    awb_set_params(e, &req, NULL);

    fill_scene(&st, 200, 200, 157);
    memset(&res, 0, sizeof(res));
    run_frames(e, &st, 5, 3, &res);
    CHECK(res.gain_out.r == 0 && res.gain_out.b == 0, "lock skips adaptive write");
    locked = res;
    CHECK(locked.color_temp_out == 0, "locked output untouched");
    awb_exit(e);
}

int main(void)
{
    init_tables();

    test_lifecycle();
    test_missing_tables();
    test_fixed_temperature();
    test_manual();
    test_adaptive_greyworld();
    test_speed_smoothing();
    test_smoothing_warmup_taps();
    test_class_distance_saturation();
    test_class_second_tie();
    test_night_detection();
    test_preset_scene();
    test_lock();

    printf("\nAWB clean-room tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
