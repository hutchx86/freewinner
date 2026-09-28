/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_ae.c - host tests for the AE module. This file is the runtime table
 * provider (freeisp_get_tables()); all tables are synthetic fixtures. */
#include "ae_clean.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_fail;
static int g_pass;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); } \
} while (0)

/* ------------------------------------------------------------------ */
/* Runtime tables (test fixtures)                                     */
/* ------------------------------------------------------------------ */

static int32_t  t_log2[256];
static uint32_t t_evtab[AE_IDX_MAX];
static uint8_t  t_conv[32 * 128];
static uint32_t t_touchprob[6 * 12];
static int32_t  t_kernel[20 * 31];
static int16_t  t_pregamma[11 * 256];
static int32_t  t_blmask[3 * AE_NGRID];
static int32_t  t_wmatrix[AE_NGRID];
static int32_t  t_wavg[AE_NGRID];
static int32_t  t_wcenter[AE_NGRID];
static int32_t  t_wover[AE_NGRID];
static int32_t  t_wunder[AE_NGRID];
static int32_t  t_net_in[10 * 64];
static int32_t  t_net_bias[10];
static int32_t  t_net_out[20];
static int32_t  t_net_out_bias[2] = { 2345, 7777 };   /* synthetic B0 < B1 */
/* Arbitrary monotone fixture (not a tuning table): x1.25 steps from 140. */
static int32_t  t_fno_ladder[16] = {
    140, 175, 219, 273, 342, 427, 534, 668,
    835, 1043, 1304, 1630, 2038, 2547, 3184, 3980
};
static uint8_t  t_auxprob[96 * 256];
static ae_desc_t t_default;

static ae_clean_tables_t g_tab;
static freeisp_tables_t g_ft;

const freeisp_tables_t *freeisp_get_tables(void)
{
    return &g_ft;
}

static void init_tables(void)
{
    int i, r, k;

    for (i = 0; i < 256; i++)
        t_log2[i] = (int32_t)floor(1000.0 * log2((double)(i + 1)) + 0.5);
    for (i = 0; i < AE_IDX_MAX; i++)
        t_evtab[i] = 1000;   /* rewritten by the module */
    for (r = 0; r < 32; r++)
        for (k = 0; k < 128; k++)
            t_conv[r * 128 + k] = (r == 0) ? (uint8_t)k : (uint8_t)(k / 2);
    for (r = 0; r < 6; r++)
        for (k = 0; k < 12; k++)
            t_touchprob[r * 12 + k] = (uint32_t)((32 - k * 2 - r) > 0 ? (32 - k * 2 - r) : 0);
    for (r = 0; r < 20; r++)
        for (k = 0; k < 31; k++)
            t_kernel[r * 31 + k] = 31 - (k > 15 ? k - 15 : 15 - k);
    memset(t_pregamma, 0, sizeof(t_pregamma));
    memset(t_blmask, 0, sizeof(t_blmask));
    for (i = 0; i < AE_NGRID; i++) {
        t_blmask[2 * AE_NGRID + i] = 1;   /* all-one mask      */
        t_wmatrix[i] = 8;
        t_wavg[i] = 8;
        t_wcenter[i] = 1;
        t_wover[i] = 16;
        t_wunder[i] = 16;
    }
    t_blmask[1 * AE_NGRID + 27] = 1;      /* centred-bright mask */
    t_wcenter[27] = 64;
    memset(t_net_in, 0, sizeof(t_net_in));
    memset(t_net_bias, 0, sizeof(t_net_bias));
    for (i = 0; i < 10; i++) {
        t_net_out[i] = 10;
        t_net_out[10 + i] = -10;
    }
    for (r = 0; r < 96; r++)
        for (k = 0; k < 256; k++)
            t_auxprob[r * 256 + k] = (uint8_t)(k < 200 ? 200 - k : 0);

    /* Synthetic descriptor (not a tuning table): 1/4000..1/60 s at 1x, then 1x..8x. */
    memset(&t_default, 0, sizeof(t_default));
    t_default.length = 2;
    t_default.ev_step = 40;
    t_default.shutter_shift = 0;
    t_default.seg[0].min_exp = 4000; t_default.seg[0].max_exp = 60;
    t_default.seg[0].min_gain = 256; t_default.seg[0].max_gain = 256;
    t_default.seg[0].min_iris = 180; t_default.seg[0].max_iris = 180;
    t_default.seg[1].min_exp = 60; t_default.seg[1].max_exp = 60;
    t_default.seg[1].min_gain = 256; t_default.seg[1].max_gain = 2048;
    t_default.seg[1].min_iris = 180; t_default.seg[1].max_iris = 180;

    memset(&g_tab, 0, sizeof(g_tab));
    g_tab.log2 = t_log2;
    g_tab.evtab = t_evtab;
    g_tab.conv = t_conv;
    g_tab.touchprob = t_touchprob;
    g_tab.kernel = t_kernel;
    g_tab.pregamma = t_pregamma;
    g_tab.blmask = t_blmask;
    g_tab.wght_matrix = t_wmatrix;
    g_tab.wght_avg = t_wavg;
    g_tab.wght_center = t_wcenter;
    g_tab.wght_over = t_wover;
    g_tab.wght_under = t_wunder;
    g_tab.net_in = t_net_in;
    g_tab.net_bias = t_net_bias;
    g_tab.net_out = t_net_out;
    g_tab.net_out_bias = t_net_out_bias;
    g_tab.fno_ladder = t_fno_ladder;
    g_tab.fno_def = t_fno_ladder;
    g_tab.table_default = &t_default;
    g_tab.auxprob = t_auxprob;
    g_ft.ae = &g_tab;
}

/* ------------------------------------------------------------------ */
/* Parameter fixture                                                  */
/* ------------------------------------------------------------------ */

static void base_params(ae_params_t *p)
{
    int i;
    memset(p, 0, sizeof(*p));
    p->exposure_cfg[0] = 256;
    p->exposure_cfg[1] = 256;
    p->exposure_cfg[2] = 256;
    p->exposure_cfg[3] = 256;
    p->exposure_cfg[4] = 0;    /* convergence row, general */
    p->exposure_cfg[5] = 0;    /* capture                  */
    p->exposure_cfg[6] = 0;    /* video                    */
    p->exposure_cfg[7] = 0;    /* spot                     */
    p->exposure_cfg[8] = 3;    /* tolerance                */
    p->exposure_cfg[9] = 128;  /* target                   */
    p->exposure_cfg[10] = 8;
    p->exposure_cfg[11] = 8;
    p->exposure_cfg[12] = 8;
    p->exposure_cfg[13] = 8;

    p->table_source = 0;
    p->max_lv = 1000;
    p->kernel_index = 0;
    p->error_delay_frames = 2;
    p->exp_delay_frames = 1;
    p->gain_delay_frames = 1;
    p->ev_comp_step = 4;
    p->touch_distance_index = 0;
    p->high_fps_handling_en = 0;
    p->iso_to_gain_ratio = 100;
    p->wdr_cfg[0] = 16;
    p->wdr_cfg[1] = 8;
    p->wdr_cfg[2] = 16;
    p->wdr_cfg[3] = 1;
    p->gain_split_ratio = 0.0;

    p->pclk = 432000000;
    p->hts = 2000;
    p->vts = 1250;
    p->frame_time = 0;
    p->gain_min = 16;
    p->gain_max = 4096;
    p->sensor_width = 1920;
    p->sensor_height = 1080;

    p->exposure_mode = 0;
    p->iso_control = 1;
    p->metering_mode = 3;
    p->hdr_mode = 0;
    p->mains_detected = 0;
    p->scene = 0;
    p->flash_mode = 0;
    p->test_enable = 1;
    p->test_forced = 0;
    p->delay_en = 0;
    p->meter_roi.x1 = 800;
    p->meter_roi.y1 = 800;
    p->meter_roi.x2 = 1200;
    p->meter_roi.y2 = 1200;
    (void)i;
}

static void fill_flat_stats(ae_stats_t *st, uint8_t luma)
{
    int i;
    memset(st, 0, sizeof(*st));
    for (i = 0; i < AE_NWIN; i++)
        st->win_avg[i] = luma;
    for (i = 0; i < 256; i++)
        st->hist[i] = 0;
    st->hist[luma] = 200;
}

static void fill_bimodal_stats(ae_stats_t *st)
{
    int i;
    memset(st, 0, sizeof(*st));
    for (i = 0; i < AE_NWIN; i++)
        st->win_avg[i] = (uint8_t)((i % 2) ? 40 : 210);
    for (i = 0; i < 256; i++)
        st->hist[i] = 0;
    st->hist[30] = 200;
    st->hist[220] = 200;
}

/* ------------------------------------------------------------------ */

static void test_lifecycle(void)
{
    ae_params_t par;
    ae_ops_t ops;
    ae_entity_t *e;
    ae_param_req_t req;
    ae_result_t res;

    base_params(&par);
    memset(&ops, 0, sizeof(ops));
    e = ae_init(&ops);
    CHECK(e != NULL, "ae_init returns entity");
    CHECK(ops.get_params == ae_get_params, "ops.get_params wired");
    CHECK(ops.set_params == ae_set_params, "ops.set_params wired");
    CHECK(ops.run == ae_run, "ops.run wired");
    CHECK(ops.isr == ae_isr, "ops.isr wired");

    memset(&req, 0, sizeof(req));
    req.kind = AE_PARAM_INIT;
    req.params = &par;
    CHECK(ae_set_params(e, &req, NULL) == 0, "init stage accepts config");

    req.kind = 99;
    CHECK(ae_set_params(e, &req, NULL) == -1, "unknown param kind rejected");

    req.kind = AE_PARAM_TABLES;
    CHECK(ae_set_params(e, &req, NULL) == 0, "line-table build succeeds");

    {
        ae_params_t out;
        CHECK(ae_get_params(e, &out) == 0, "get_params succeeds");
        CHECK(out.aperture_ladder[0] == t_fno_ladder[0], "default ladder copied from table");
        CHECK(out.analog_gain_range[0] == 256, "analog range defaulted from gain_min");
        CHECK(out.digital_gain_range[0] == 1024, "digital range defaulted");
    }

    memset(&res, 0, sizeof(res));
    CHECK(ae_run(NULL, NULL, &res) == -1, "null run returns -1");
    CHECK(res.setting_curr.analog_gain == 256, "default result seeded");

    ae_exit(e);
}

static void test_line_table(void)
{
    ae_params_t par;
    ae_entity_t *e;
    ae_param_req_t req;
    ae_result_t res;
    ae_stats_t st;
    int i, idx_max;

    base_params(&par);
    e = ae_init(NULL);
    req.kind = AE_PARAM_INIT;
    req.params = &par;
    ae_set_params(e, &req, NULL);
    req.kind = AE_PARAM_TABLES;
    ae_set_params(e, &req, NULL);

    fill_flat_stats(&st, 100);
    memset(&res, 0, sizeof(res));
    for (i = 0; i < 5; i++)
        ae_run(e, &st, &res);
    idx_max = res.idx_max;
    CHECK(idx_max > 0 && idx_max <= AE_IDX_MAX, "idx_max in range");

    CHECK(t_evtab[0] == 1000, "EVTAB[0] == 1000");
    for (i = 0; i + 1 < idx_max; i++)
        if (t_evtab[i + 1] < t_evtab[i])
            break;
    CHECK(i + 1 == idx_max, "EVTAB monotonically non-decreasing");

    for (i = 0; i < idx_max; i++)
        if (t_evtab[i] == 0)
            break;
    CHECK(i == idx_max, "EVTAB nonzero through idx_max");

    ae_exit(e);
}

static void test_metering_error_convergence(void)
{
    ae_params_t par;
    ae_entity_t *e;
    ae_param_req_t req;
    ae_result_t res;
    ae_stats_t st;
    int i;
    int32_t start_idx = -1, end_idx = -1, max_delta = 0;
    int busy_seen = 0, done_seen = 0;

    base_params(&par);
    e = ae_init(NULL);
    req.kind = AE_PARAM_INIT;
    req.params = &par;
    ae_set_params(e, &req, NULL);
    req.kind = AE_PARAM_TABLES;
    ae_set_params(e, &req, NULL);

    fill_flat_stats(&st, 50);   /* darker than the 128 target */
    memset(&res, 0, sizeof(res));
    for (i = 0; i < 60; i++) {
        CHECK(ae_run(e, &st, &res) == 0, "run returns 0");
        if (i == 3) {
            start_idx = res.setting.index;
            CHECK(res.avg_lum == 50, "avg_lum tracks window average");
            CHECK(res.target == 128, "target from exposure_cfg");
            CHECK(res.weight_lum == 50, "weight_lum tracks average");
        }
        if (i > 3) {
            if (res.delta_idx > max_delta)
                max_delta = res.delta_idx;
            if (res.status == 1)
                busy_seen = 1;
            if (res.status == 2)
                done_seen = 1;
            CHECK(res.gain_ratio_out >= 64 && res.gain_ratio_out <= 640,
                  "gain_ratio_out clamped");
        }
    }
    end_idx = res.setting.index;
    CHECK(max_delta > 0, "positive error detected for dark scene");
    CHECK(end_idx > start_idx, "exposure index increased toward target");
    CHECK(busy_seen && done_seen, "status exercised busy and done");
    CHECK(res.setting_short.sensor_exp_line >= 0, "short setting sane");

    ae_exit(e);
}

static void test_backlight(void)
{
    ae_params_t par;
    ae_entity_t *e;
    ae_param_req_t req;
    ae_result_t res;
    ae_stats_t st;
    int i;

    base_params(&par);
    par.hist_metering_en = 1;
    par.kernel_index = 0;
    e = ae_init(NULL);
    req.kind = AE_PARAM_INIT;
    req.params = &par;
    ae_set_params(e, &req, NULL);
    req.kind = AE_PARAM_TABLES;
    ae_set_params(e, &req, NULL);

    fill_bimodal_stats(&st);
    memset(&res, 0, sizeof(res));
    for (i = 0; i < 6; i++) {
        ae_run(e, &st, &res);
    }
    CHECK(res.backlight >= 0 && res.backlight <= 64, "backlight in 0..64");
    CHECK(res.bright_pos >= 0 && res.bright_pos <= 254, "bright_pos in range");
    CHECK(res.dark_pos >= 0 && res.dark_pos <= 254, "dark_pos in range");
    CHECK(res.weight_lum >= 0 && res.weight_lum <= 255, "weight_lum sane");

    ae_exit(e);
}

/* Peak spread >= 101 takes the network; zero weights leave s0 = B0, s1 = B1 (8.5). */
static int32_t network_backlight(const int32_t *bias)
{
    ae_params_t par;
    ae_entity_t *e;
    ae_param_req_t req;
    ae_result_t res;
    ae_stats_t st;
    const int32_t *saved = g_tab.net_out_bias;
    int i;

    g_tab.net_out_bias = bias;
    base_params(&par);
    par.hist_metering_en = 1;
    e = ae_init(NULL);
    req.kind = AE_PARAM_INIT;
    req.params = &par;
    ae_set_params(e, &req, NULL);
    req.kind = AE_PARAM_TABLES;
    ae_set_params(e, &req, NULL);
    /* Two histogram blocks further apart than the kernel: two clean peaks. */
    fill_bimodal_stats(&st);
    memset(st.hist, 0, sizeof(st.hist));
    st.hist[2] = 20000;
    st.hist[250] = 20000;
    memset(&res, 0, sizeof(res));
    for (i = 0; i < 6; i++)
        ae_run(e, &st, &res);
    CHECK(res.bright_pos - res.dark_pos >= 101, "split scene reaches the network");
    ae_exit(e);
    g_tab.net_out_bias = saved;
    return res.backlight;
}

static void test_backlight_network(void)
{
    static const int32_t lo_hi[2] = { 2345, 7777 };
    static const int32_t hi_lo[2] = { 20000, 1000 };
    static const int32_t deep[2]  = { 1000, 30000 };

    CHECK(network_backlight(lo_hi) == 40 - 7777 / 800, "s0 < s1: 40 - s1/800");
    CHECK(network_backlight(hi_lo) == 20 + 20000 / 800, "s0 >= s1: 20 + s0/800");
    CHECK(network_backlight(deep) == 3, "s0 < s1 branch clamps at 0..32");
    CHECK(network_backlight(NULL) == 32, "absent biases: network off, backlight 32");
}

static void test_touch_grid(void)
{
    ae_params_t par;
    ae_entity_t *e;
    ae_param_req_t req;
    ae_result_t res;
    ae_stats_t st;
    int i;

    base_params(&par);
    par.metering_mode = 2;   /* spot */
    e = ae_init(NULL);
    req.kind = AE_PARAM_INIT;
    req.params = &par;
    ae_set_params(e, &req, NULL);
    req.kind = AE_PARAM_TABLES;
    ae_set_params(e, &req, NULL);
    req.kind = AE_PARAM_TOUCH;
    CHECK(ae_set_params(e, &req, NULL) == 0, "touch grid rebuild succeeds");

    fill_flat_stats(&st, 120);
    memset(&res, 0, sizeof(res));
    for (i = 0; i < 5; i++) {
        ae_run(e, &st, &res);
    }
    CHECK(res.avg_lum == 120, "spot metering reports avg_lum");
    CHECK(res.weight_lum == 120, "spot bypasses shadow/highlight reweight");

    ae_exit(e);
}

static void test_wdr(void)
{
    ae_params_t par;
    ae_entity_t *e;
    ae_param_req_t req;
    ae_result_t res;
    ae_stats_t st;
    int i;

    base_params(&par);
    par.hdr_mode = 1;
    par.wdr_cfg[3] = 1;
    e = ae_init(NULL);
    req.kind = AE_PARAM_INIT;
    req.params = &par;
    ae_set_params(e, &req, NULL);
    req.kind = AE_PARAM_TABLES;
    ae_set_params(e, &req, NULL);

    memset(&st, 0, sizeof(st));
    for (i = 0; i < AE_NWIN; i++) {
        st.win_avg[i] = 128;
        st.accum_r[i] = 1000;
        st.accum_g[i] = 1000;
        st.accum_b[i] = 1000;
    }
    for (i = 0; i < AE_NWIN_WDR; i++)
        st.hist[i] = 1;

    memset(&res, 0, sizeof(res));
    for (i = 0; i < 300; i++) {
        ae_run(e, &st, &res);
    }
    CHECK(res.wdr_ratio.tmp >= 160 && res.wdr_ratio.tmp <= 304,
          "WDR ratio clamped to 160..304");
    CHECK(res.hist_low >= 100 && res.hist_low <= 10000, "hist_low fraction");
    CHECK(res.hist_mid >= 100 && res.hist_mid <= 10000, "hist_mid fraction");
    CHECK(res.hist_hi >= 100 && res.hist_hi <= 10000, "hist_hi fraction");
    CHECK(res.wdr_hi_th == (16 << 8), "wdr_hi_th from wdr_cfg");
    CHECK(res.wdr_low_th == (8 << 8), "wdr_low_th from wdr_cfg");
    CHECK(res.setting_short.exposure_time <= res.setting.exposure_time,
          "short exposure divided");

    ae_exit(e);
}

/* WDR-ratio step holds the incoming ratio (256) on no-adjust paths (counter
 * still 0); only the adjust path steps it toward the target (160). */
static void test_wdr_ratio_step(void)
{
    ae_params_t par;
    ae_entity_t *e;
    ae_param_req_t req;
    ae_result_t res;
    ae_stats_t st;
    int i;

    base_params(&par);
    par.hdr_mode = 1;
    par.wdr_cfg[0] = 16;   /* base ratio 256, shutter multiple 256 lines */
    par.wdr_cfg[3] = 1;    /* ratio tracking enabled                     */
    e = ae_init(NULL);
    req.kind = AE_PARAM_INIT;
    req.params = &par;
    ae_set_params(e, &req, NULL);
    req.kind = AE_PARAM_TABLES;
    ae_set_params(e, &req, NULL);

    /* Zero long/short accumulators -> all weight in the low histogram bins
       -> the computed target ratio is the 160 clamp floor. */
    memset(&st, 0, sizeof(st));
    for (i = 0; i < AE_NWIN; i++)
        st.win_avg[i] = 100;

    memset(&res, 0, sizeof(res));
    for (i = 0; i < 2000 && res.wdr_ratio.last == 0; i++)
        ae_run(e, &st, &res);

    CHECK(res.wdr_ratio.last == 256, "WDR step sees incoming ratio 256");
    CHECK(res.wdr_ratio.tmp == res.wdr_ratio.last,
          "no-adjust path holds the incoming ratio");

    for (i = 0; i < 8000; i++)
        ae_run(e, &st, &res);
    CHECK(res.wdr_ratio.tmp > 160 && res.wdr_ratio.tmp < 256,
          "adjust path steps tmp toward the target");

    ae_exit(e);
}

/* Bright flat scene at the table floor: the expected index clamps unsigned
 * (reports the top index) and the out-of-table LV probe reads zero. */
static void test_auto_bright_low_saturation(void)
{
    ae_params_t par;
    ae_entity_t *e;
    ae_param_req_t req;
    ae_result_t res;
    ae_stats_t st;
    int i;

    base_params(&par);
    par.frame_time = 33333;
    e = ae_init(NULL);
    req.kind = AE_PARAM_INIT;
    req.params = &par;
    ae_set_params(e, &req, NULL);
    req.kind = AE_PARAM_TABLES;
    ae_set_params(e, &req, NULL);

    fill_flat_stats(&st, 220);   /* much brighter than the 128 target */
    memset(&res, 0, sizeof(res));
    for (i = 0; i < 40; i++) {
        ae_set_frame_index(e, 80 + i);
        ae_run(e, &st, &res);
    }
    CHECK(res.setting.index == 0, "bright scene saturates at the low index");
    CHECK(res.idx_expect == res.idx_max - 1,
          "negative ideal index clamps to the table top");
    CHECK(res.lv_adj == 0, "sub-table LV probe reads zero");

    ae_exit(e);
}

/* WDR quantises the long shutter to wdr_cfg[0]*16 lines but not the latency
 * delay, so the HDR ratio must converge and the committed ratios follow it. */
static void test_wdr_shutter_quantisation(void)
{
    ae_params_t par;
    ae_entity_t *e;
    ae_param_req_t req;
    ae_result_t res;
    ae_stats_t st;
    int i, w, div;

    base_params(&par);
    par.hdr_mode = 1;
    par.wdr_cfg[0] = 4;
    par.wdr_cfg[1] = 8;
    par.wdr_cfg[2] = 40;
    par.wdr_cfg[3] = 1;
    par.frame_time = 33333;
    e = ae_init(NULL);
    req.kind = AE_PARAM_INIT;
    req.params = &par;
    ae_set_params(e, &req, NULL);
    req.kind = AE_PARAM_TABLES;
    ae_set_params(e, &req, NULL);

    /* Dark long/short accumulators: the HDR target is the low clamp, so the
       ratio must step away from its initial 256. */
    memset(&st, 0, sizeof(st));
    for (i = 0; i < AE_NWIN; i++) {
        st.win_avg[i] = 128;
        st.accum_r[i] = 1000;
        st.accum_g[i] = 1000;
        st.accum_b[i] = 1000;
    }
    for (i = 0; i < AE_NWIN_WDR; i++)
        st.hist[i] = 1;

    memset(&res, 0, sizeof(res));
    for (i = 0; i < 30; i++) {
        ae_set_frame_index(e, 80 + i);
        ae_run(e, &st, &res);
    }

    w = par.wdr_cfg[0] * 16;
    CHECK(res.wdr_ratio.tmp >= 160 && res.wdr_ratio.tmp <= 304,
          "WDR ratio stays in range");
    CHECK(res.wdr_ratio.tmp != 256, "WDR ratio converges within the run");
    CHECK(res.wdr_ratio.tmp == res.wdr_ratio.last,
          "WDR ratio holds once converged");
    CHECK(res.wdr_ratio.sensor == res.wdr_ratio.tmp,
          "committed sensor ratio tracks the tracked ratio");
    CHECK(res.wdr_ratio.hw_ratio == res.wdr_ratio.tmp,
          "hardware ratio tracks the tracked ratio");
    CHECK(res.setting.sensor_exp_line > 0 &&
          res.setting.sensor_exp_line % w == 0,
          "long shutter quantised to the WDR multiple");
    div = res.wdr_ratio.sensor >> 4;
    if (div < 1)
        div = 1;
    CHECK(res.setting_short.sensor_exp_line ==
          res.setting.sensor_exp_line / div,
          "short shutter is the long shutter divided by the ratio");
    CHECK(res.setting_short.exposure_time ==
          res.setting.exposure_time / div,
          "short exposure is the long exposure divided by the ratio");

    ae_exit(e);
}

static void test_manual_and_priority(void)
{
    ae_params_t par;
    ae_entity_t *e;
    ae_param_req_t req;
    ae_result_t res;
    ae_stats_t st;
    int i;

    base_params(&par);
    par.exposure_mode = 2;   /* shutter priority */
    par.fixed_exposure = 20000;
    e = ae_init(NULL);
    req.kind = AE_PARAM_INIT;
    req.params = &par;
    ae_set_params(e, &req, NULL);
    req.kind = AE_PARAM_TABLES;
    ae_set_params(e, &req, NULL);

    fill_flat_stats(&st, 80);
    for (i = 0; i < 8; i++) {
        memset(&res, 0, sizeof(res));
        ae_run(e, &st, &res);
    }
    CHECK(res.setting.exposure_time <= par.fixed_exposure,
          "shutter priority respects manual cap");
    ae_exit(e);

    base_params(&par);
    par.exposure_mode = 1;   /* manual */
    par.iso_control = 0;     /* manual ISO so the gain is not overridden */
    par.fixed_exposure = 10000;
    par.sensor_gain = 256;
    par.test_gain = 512;
    e = ae_init(NULL);
    req.kind = AE_PARAM_INIT;
    req.params = &par;
    ae_set_params(e, &req, NULL);
    req.kind = AE_PARAM_TABLES;
    ae_set_params(e, &req, NULL);
    for (i = 0; i < 8; i++) {
        memset(&res, 0, sizeof(res));
        ae_run(e, &st, &res);
    }
    CHECK(res.setting.exposure_time == par.fixed_exposure,
          "manual exposure uses fixed exposure");
    CHECK(res.setting.analog_gain == 4096,
          "manual gain uses sensor_gain<<4");
    ae_exit(e);
}

int main(void)
{
    init_tables();

    test_lifecycle();
    test_line_table();
    test_metering_error_convergence();
    test_backlight();
    test_backlight_network();
    test_touch_grid();
    test_wdr();
    test_wdr_ratio_step();
    test_auto_bright_low_saturation();
    test_wdr_shutter_quantisation();
    test_manual_and_priority();

    printf("\nAE clean-room tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
