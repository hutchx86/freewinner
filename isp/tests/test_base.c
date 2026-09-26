/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * test_base.c - host tests for the clean-room ISP base tier (spec/reglayer2.md
 * section 3, spec/interfaces.md table contract).
 *
 * No vendor output is compared.  Every expected value is derived independently
 * from the formulas in the specification with small, hand-checkable inputs.
 */
#include "base.h"
#include "freeisp/sdiv.h"

#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            fails++;                                                      \
        }                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        long va = (long)(a);                                              \
        long vb = (long)(b);                                              \
        if (va != vb) {                                                   \
            printf("FAIL %s:%d: %s=%ld expected %s=%ld\n", __FILE__,      \
                   __LINE__, #a, va, #b, vb);                             \
            fails++;                                                      \
        }                                                                 \
    } while (0)

/* ---- runtime table provider -------------------------------------- */

static uint16_t g_gamma_base[ISP_GAMMA_TBL_LEN];
static uint16_t g_gamma_sub[ISP_GAMMA_TRIG_N - 1][ISP_GAMMA_TBL_LEN];
static int32_t  g_gamma_trig[ISP_GAMMA_TRIG_N];
static uint16_t g_lsc[2 * ISP_MSC_TEMP_NUM][ISP_LSC_TBL_WORDS];
static uint16_t g_lsc_trig[ISP_MSC_TEMP_NUM];
static uint16_t g_lsc_trig_def[ISP_MSC_TEMP_NUM];
static uint16_t g_msc[2 * ISP_MSC_TEMP_NUM][ISP_MSC_TBL_LENGTH];
static uint16_t g_msc_trig[ISP_MSC_TEMP_NUM];
static uint16_t g_msc_trig_def[ISP_MSC_TEMP_NUM];
static uint16_t g_linear[ISP_LINEAR_TBL_N];
static uint16_t g_wdr[ISP_WDR_TBL_WORDS];
static uint16_t g_wdr_fe[ISP_WDR_FE_WORDS];
static uint16_t g_anti_gamma[ISP_ANTI_GAMMA_N];
static int16_t  g_yuv[2][ISP_MATRIX_N];   /* rgb2yuv_base is const int16_t * */
static uint16_t g_cm[2 * 12];
static int32_t  g_cm_trig[ISP_MSC_TEMP_NUM];

static const base_tables_t g_base_tables = {
    g_gamma_base,
    { g_gamma_sub[0], g_gamma_sub[1], g_gamma_sub[2], g_gamma_sub[3] },
    g_gamma_trig,
    { g_lsc[0], g_lsc[1], g_lsc[2], g_lsc[3], g_lsc[4], g_lsc[5],
      g_lsc[6], g_lsc[7], g_lsc[8], g_lsc[9], g_lsc[10], g_lsc[11] },
    g_lsc_trig,
    g_lsc_trig_def,
    { g_msc[0], g_msc[1], g_msc[2], g_msc[3], g_msc[4], g_msc[5],
      g_msc[6], g_msc[7], g_msc[8], g_msc[9], g_msc[10], g_msc[11] },
    g_msc_trig,
    g_msc_trig_def,
    g_linear,
    g_wdr,
    g_wdr_fe,
    g_anti_gamma,
    { g_yuv[0], g_yuv[1] },
    g_cm,
    g_cm_trig,
    NULL, /* otp_msc_golden */
    NULL  /* default_reg */
};
static const freeisp_tables_t g_tables = { &g_base_tables };

const freeisp_tables_t *freeisp_get_tables(void)
{
    return &g_tables;
}

/* ---- fixtures ----------------------------------------------------- */

static isp_lib_context_t ctx;
static uint8_t g_buf[0x10000];

/* ---- test doubles for the register-writer collaborators -----------
 * The base tier is linked on its own here; the validated writers live in
 * reg_writers.c (with their own test).  Of the base entry points only
 * config_band_step drives a writer directly; the remaining payloads are
 * produced as module_cfg fields (module_cfg.c owns their writer calls). */

static int      g_lsc_calls, g_dg_calls, g_band_calls, g_wdr_calls;
static uint32_t g_lsc_arg[3];
static uint32_t g_dg_arg[4];
static uint32_t g_band_arg;
static uint32_t g_wdr_arg[7];
static unsigned long g_last_id;

void isp_reg_map_load_addr(unsigned long id, void *base)
{
    (void)base;
    g_last_id = id;
}

/* The AE hook the base tier calls to update the AE core; this standalone test
 * only needs the call to resolve (the SDK-ABI dispatch is covered by the
 * private differential harness). */
void freeisp_ae_set_params(isp_ae_entity_ctx_t *ae_ctx, int32_t cmd_type)
{
    (void)ae_ctx;
    (void)cmd_type;
}

void isp_reg_set_lsc(unsigned long id, uint32_t ct_x, uint32_t ct_y,
                     uint32_t rs_val)
{
    g_last_id = id;
    g_lsc_arg[0] = ct_x;
    g_lsc_arg[1] = ct_y;
    g_lsc_arg[2] = rs_val;
    g_lsc_calls++;
}

void isp_reg_set_dg_gain(unsigned long id, uint32_t r, uint32_t gr,
                         uint32_t gb, uint32_t b)
{
    g_last_id = id;
    g_dg_arg[0] = r;
    g_dg_arg[1] = gr;
    g_dg_arg[2] = gb;
    g_dg_arg[3] = b;
    g_dg_calls++;
}

void isp_reg_set_afs_anti_flick(unsigned long id, uint32_t line_inc)
{
    g_last_id = id;
    g_band_arg = line_inc;
    g_band_calls++;
}

void isp_reg_set_wdr_cfg(unsigned long id, uint32_t lo_th, uint32_t hi_th,
                         uint32_t exp_ratio, uint32_t slope, uint32_t mv_th,
                         uint32_t mv_scale, uint32_t out_sel)
{
    g_last_id = id;
    g_wdr_arg[0] = lo_th;
    g_wdr_arg[1] = hi_th;
    g_wdr_arg[2] = exp_ratio;
    g_wdr_arg[3] = slope;
    g_wdr_arg[4] = mv_th;
    g_wdr_arg[5] = mv_scale;
    g_wdr_arg[6] = out_sel;
    g_wdr_calls++;
}

static void wr32(uint32_t off, uint32_t v)
{
    memcpy(g_buf + off, &v, sizeof(v));
}

static void fresh(void)
{
    int i, c, t;

    memset(&ctx, 0, sizeof(ctx));
    memset(g_buf, 0, sizeof(g_buf));

    ctx.isp_dev_id = 0;
    g_lsc_calls = g_dg_calls = g_band_calls = g_wdr_calls = 0;
    g_lsc_arg[0] = g_lsc_arg[1] = g_lsc_arg[2] = 0;
    g_dg_arg[0] = g_dg_arg[1] = g_dg_arg[2] = g_dg_arg[3] = 0;
    g_band_arg = 0;
    for (i = 0; i < 7; i++)
        g_wdr_arg[i] = 0;
    ctx.stats_ctx.pic_w = 2000;
    ctx.stats_ctx.pic_h = 1000;
    ctx.ae_settings.colour_space = 0;
    ctx.sensor_info.temperature = 250;

    for (i = 0; i < ISP_GAMMA_TBL_LEN; i++)
        g_gamma_base[i] = (uint16_t)(i & 0xfff);
    for (t = 0; t < ISP_GAMMA_TRIG_N - 1; t++)
        for (i = 0; i < ISP_GAMMA_TBL_LEN; i++)
            g_gamma_sub[t][i] = (uint16_t)((i + 100 * (t + 1)) & 0xfff);
    g_gamma_trig[0] = 0;
    g_gamma_trig[1] = 100;
    g_gamma_trig[2] = 200;
    g_gamma_trig[3] = 300;
    g_gamma_trig[4] = 400;
    for (i = 0; i < ISP_GAMMA_TRIG_N; i++)
        ctx.isp_ini_cfg.gamma_trig_cfg[i] = g_gamma_trig[i];

    for (t = 0; t < ISP_MSC_TEMP_NUM; t++) {
        g_lsc_trig[t] = (uint16_t)(100 + t * 100);
        g_msc_trig[t] = (uint16_t)(100 + t * 100);
        g_lsc_trig_def[t] = (uint16_t)(3000 + t * 100);
        g_msc_trig_def[t] = (uint16_t)(3000 + t * 100);
    }
    for (t = 0; t < 2 * ISP_MSC_TEMP_NUM; t++) {
        for (c = 0; c < 3; c++)
            for (i = 0; i < ISP_LENS_TBL_N; i++)
                g_lsc[t][c * ISP_LENS_TBL_N + i] =
                    (uint16_t)(t * 1000 + c * 100 + i);
        for (i = 0; i < ISP_MSC_TBL_LENGTH; i++)
            g_msc[t][i] = (uint16_t)(t * 2000 + i);
    }
    for (i = 0; i < ISP_MSC_TBL_LENGTH; i++)
        ctx.msc_golden_ratio[i] = 1.0f;
    for (i = 0; i < ISP_LINEAR_TBL_N; i++)
        g_linear[i] = (uint16_t)i;
    for (i = 0; i < ISP_WDR_TBL_WORDS; i++)
        g_wdr[i] = (uint16_t)i;
    for (i = 0; i < ISP_WDR_FE_WORDS; i++)
        g_wdr_fe[i] = (uint16_t)i;
    for (i = 0; i < ISP_ANTI_GAMMA_N; i++)
        g_anti_gamma[i] = (uint16_t)i;
    for (i = 0; i < ISP_MATRIX_N; i++) {
        g_yuv[0][i] = 0;
        g_yuv[1][i] = 0;
    }
    for (i = 0; i < ISP_MSC_TEMP_NUM; i++)
        g_cm_trig[i] = i * 1000;
    for (i = 0; i < 12; i++)
        g_cm[i] = 0;

    isp_reg_map_load_addr(0, NULL);
}

/* ------------------------------------------------------------------ */

static void test_band_step(void)
{
    fresh();
    ctx.stats_ctx.pic_h = 12800;
    config_band_step(&ctx);
    CHECK_EQ(ctx.module_cfg.afs_cfg.inc_line, 63);
    CHECK_EQ(g_band_calls, 1);
    CHECK_EQ(g_band_arg, 63);
    CHECK_EQ(g_last_id, 0);

    fresh();
    ctx.stats_ctx.pic_h = 64;
    config_band_step(&ctx);
    CHECK_EQ(ctx.module_cfg.afs_cfg.inc_line, 1);

    fresh();
    ctx.stats_ctx.pic_h = 128 * 10;
    config_band_step(&ctx);
    CHECK_EQ(ctx.module_cfg.afs_cfg.inc_line, 10);
}

static void test_lens_center(void)
{
    fresh();
    ctx.ae_settings.lsc_center_x = 2048;
    ctx.ae_settings.lsc_center_y = 2048;
    config_lens_center(&ctx);

    CHECK_EQ(ctx.module_cfg.lens_cfg.ct_x, 1000);
    CHECK_EQ(ctx.module_cfg.lens_cfg.ct_y, 500);
    /* Corner distance is kept squared (max 1250000); rs_val is the smallest
     * shift s with (1250000 >> s) < 256, i.e. 13. */
    CHECK_EQ(ctx.module_cfg.lens_cfg.rs_val, 13);
    CHECK_EQ(ctx.module_cfg.disc_cfg.disc_ct_x, 1000);
    CHECK_EQ(ctx.module_cfg.disc_cfg.disc_rs_val, 13);
}

static void test_dig_gain(void)
{
    fresh();
    ctx.isp_test_settings.dig_gain_en = 1;
    ctx.isp_ini_cfg.gains.bayer_gain[0] = 100;
    ctx.isp_ini_cfg.gains.bayer_gain[1] = 200;
    ctx.isp_ini_cfg.gains.bayer_gain[2] = 300;
    ctx.isp_ini_cfg.gains.bayer_gain[3] = 400;

    config_dig_gain(&ctx, 1024);

    CHECK_EQ(ctx.module_cfg.gain_offset_cfg.gain[0], 100);
    CHECK_EQ(ctx.module_cfg.gain_offset_cfg.gain[1], 200);
    CHECK_EQ(ctx.module_cfg.gain_offset_cfg.gain[2], 300);
    CHECK_EQ(ctx.module_cfg.gain_offset_cfg.gain[3], 400);
    CHECK_EQ(ctx.sensor_info.gain_offset[1], 200);
}

static void test_gamma(void)
{
    fresh();
    ctx.sensor_info.ae_lv = 50;         /* idx 0, range 0..100 */
    config_gamma(&ctx);
    /* idx 0 selects the base table verbatim (no interpolation needed). */
    CHECK_EQ(ctx.module_cfg.gamma_cfg.gamma_tbl[0], 0);
    CHECK_EQ(ctx.module_cfg.gamma_cfg.gamma_tbl[1000], 1000);

    /* Degenerate range: all triggers equal, so idx 4 selects gamma_sub[3]
     * verbatim: entry i is (i + 400) & 0xfff. */
    fresh();
    g_gamma_trig[0] = g_gamma_trig[1] = g_gamma_trig[2] =
        g_gamma_trig[3] = g_gamma_trig[4] = 100;
    {
        int i;
        for (i = 0; i < ISP_GAMMA_TRIG_N; i++)
            ctx.isp_ini_cfg.gamma_trig_cfg[i] = g_gamma_trig[i];
    }
    ctx.sensor_info.ae_lv = 50;
    config_gamma(&ctx);
    CHECK_EQ(ctx.module_cfg.gamma_cfg.gamma_tbl[0], 400);
    CHECK_EQ(ctx.module_cfg.gamma_cfg.gamma_tbl[10], 410);

    /* WDR path with gamma disabled: the first 1024 entries become the
     * square curve (i/1023)^2 * 4095, then replicate into the three thirds. */
    fresh();
    ctx.isp_test_settings.wdr_en = 1;
    ctx.isp_test_settings.gamma_en = 0;
    ctx.sensor_info.ae_lv = 50;
    config_gamma(&ctx);
    CHECK_EQ(ctx.module_cfg.gamma_cfg.gamma_tbl[0], 0);
    CHECK_EQ(ctx.module_cfg.gamma_cfg.gamma_tbl[1023],
             4095);
    CHECK_EQ(ctx.module_cfg.gamma_cfg.gamma_tbl[1024], 0);
    CHECK_EQ(ctx.module_cfg.gamma_cfg.gamma_tbl[2048], 0);
}

static void test_lens_table(void)
{
    int i;

    fresh();
    ctx.isp_test_settings.lsc_en = 1;
    ctx.ff_mod = 2;
    ctx.lsc_mode = 0;
    ctx.awb_color_temp_output = 250;        /* shared temp address */
    for (i = 0; i < ISP_MSC_TEMP_NUM; i++)
        ctx.isp_ini_cfg.lsc_trig_cfg[i] = g_lsc_trig[i];
    ctx.module_cfg.lens_table[3] = 0xbeef;  /* lane 3 must stay untouched */
    config_lens_table(&ctx, 0x200);
    /* temp 250 selects rows 1/2 (200..300); rows are r*1000+c*100+i. */
    CHECK_EQ(ctx.module_cfg.lens_table[0], 1500);
    CHECK_EQ(ctx.module_cfg.lens_table[1], 1600);
    CHECK_EQ(ctx.module_cfg.lens_table[2], 1700);
    CHECK_EQ(ctx.module_cfg.lens_table[3], 0xbeef);

    ctx.module_cfg.lens_table[0] = 7777;
    ctx.isp_test_settings.lsc_en = 0;
    config_lens_table(&ctx, 0x200);
    CHECK_EQ(ctx.module_cfg.lens_table[0], 7777);
}

static void test_msc_table(void)
{
    int i;

    fresh();
    ctx.isp_test_settings.msc_en = 1;
    ctx.mff_mod = 2;
    ctx.msc_mode = 0;
    ctx.awb_color_temp_output = 4000;
    for (i = 0; i < ISP_MSC_TEMP_NUM; i++)
        ctx.isp_ini_cfg.msc_trig_cfg[i] = 4000;  /* thi == tlo */
    ctx.module_cfg.msc_table[3] = 0xbeef;
    config_msc_table(&ctx, 0x200);
    /* Scaling-only branch with golden_ratio == 1: row j=4, components
     * 0/484/968 of the 484-word grid. */
    CHECK_EQ(ctx.module_cfg.msc_table[0], 8000);
    CHECK_EQ(ctx.module_cfg.msc_table[1], 8484);
    CHECK_EQ(ctx.module_cfg.msc_table[2], 8968);
    CHECK_EQ(ctx.module_cfg.msc_table[3], 0xbeef);

    ctx.module_cfg.msc_table[0] = 4242;
    ctx.isp_test_settings.msc_en = 0;
    config_msc_table(&ctx, 0x200);
    CHECK_EQ(ctx.module_cfg.msc_table[0], 4242);
}

static void test_wdr(void)
{
    int i;

    fresh();
    ctx.isp_test_settings.wdr_en = 0;
    config_wdr(&ctx, 0);
    CHECK_EQ(ctx.ae_settings.ae_mode, ISP_AE_MODE_NORM);

    /* Threshold config only (flag=0 builds no table).  The reported hi/lo
     * thresholds are divided by the ISP hardware ratio, which defaults to
     * 0x100 when the sensor/tmp ratios are zero, so hi becomes 0x30 and the
     * output-select chain picks exp_ratio (iso_c2g_th <= iso_lum_idx). */
    fresh();
    ctx.isp_test_settings.wdr_en = 1;
    ctx.ae_settings.ae_mode = 7;                  /* must stay untouched */
    ctx.ae_settings.wdr_output_select = 2;
    ctx.ae_result.wdr_hi_th = 0x3000;
    ctx.ae_result.wdr_low_th = 0x100;
    config_wdr(&ctx, 0);
    CHECK_EQ(ctx.ae_settings.ae_mode, 7);
    CHECK_EQ(ctx.ae_result.wdr_hi_th, 0x30);
    CHECK_EQ(ctx.ae_result.wdr_low_th, 0x1);
    CHECK_EQ(ctx.module_cfg.wdr_cfg.exp_ratio, 0x1000);
    CHECK_EQ(ctx.module_cfg.wdr_cfg.hi_th, 0x1000);
    CHECK_EQ(ctx.module_cfg.wdr_cfg.lo_th, 0x1);
    CHECK_EQ(ctx.module_cfg.wdr_cfg.slope, 0xff);
    CHECK_EQ(ctx.module_cfg.wdr_cfg.mv_th, 0xc00);
    CHECK_EQ(ctx.module_cfg.wdr_cfg.mv_scale, 0x3c);
    CHECK_EQ(ctx.module_cfg.wdr_cfg.out_sel, 2);
    CHECK_EQ(ctx.module_cfg.wdr_cfg.table[5], 0); /* no table with flag=0 */
    CHECK_EQ(g_wdr_calls, 0);

    /* Commanding path: mode 1 with an all-zero tuning table (all three
     * probe entries must be zero, not just the first few words). */
    fresh();
    for (i = 0; i < ISP_WDR_TBL_WORDS; i++)
        g_wdr[i] = 0;
    ctx.module_cfg.mode_cfg.wdr_mode = 1;
    ctx.ae_param.comanding_input_bits = 12;
    ctx.ae_param.comanding_output_bits = 12;
    ctx.ae_param.frame_cnt = 0;
    config_wdr(&ctx, 1);
    CHECK_EQ(ctx.ae_param.nor_cmd_mode, 1);
    CHECK_EQ(ctx.module_cfg.wdr_cfg.table[100], 100);   /* k >> (12-12) */
    CHECK_EQ(ctx.module_cfg.wdr_cfg.table[0xfff], 0xfff);
    CHECK_EQ(ctx.module_cfg.wdr_cfg.table[0x1000], 0);
    CHECK_EQ(ctx.module_cfg.wdr_cfg.table[0x1000 + 1024], 2047);
    CHECK_EQ(ctx.anti_gamma_tbl[5], 5);           /* anti-gamma copied */
    CHECK_EQ(g_wdr_calls, 0);
    for (i = 0; i < ISP_WDR_TBL_WORDS; i++)
        g_wdr[i] = (uint16_t)i;
}

static void test_apply_settings(void)
{
    fresh();
    ctx.isp_3a_change_flags = ISP_3A_SCENE | ISP_3A_FLICKER |
                              ISP_3A_AF_METERING | ISP_3A_AE_METERING |
                              ISP_3A_GAIN_STR;
    ctx.isp_ini_cfg.gains.gain_favour = 7;
    ctx.isp_ini_cfg.gains.analog_gain_min = 11;
    ctx.ae_settings.flicker_mode = 2;
    ctx.af_frame_cnt = 9;
    ctx.ae_frame_cnt = 9;

    isp_apply_settings(&ctx);

    CHECK_EQ(ctx.ae_param.ae_ini.gain_favour, 7);
    CHECK_EQ(ctx.ae_param.ae_ini.analog_gain_min, 11);
    CHECK_EQ(ctx.afs_param.flicker_mode, 2);
    CHECK_EQ(ctx.af_frame_cnt, 0);
    CHECK_EQ(ctx.ae_frame_cnt, 0);
    CHECK_EQ(ctx.isp_3a_change_flags, 0);

    CHECK_EQ(ctx.module_cfg.af_cfg.af_reg_win.hor_num, ISP_AF_WIN_W);
    CHECK_EQ(ctx.module_cfg.af_cfg.af_reg_win.ver_num, ISP_AF_WIN_H);
    CHECK_EQ(ctx.module_cfg.af_cfg.af_reg_win.width, 2000 / 24);
    CHECK_EQ(ctx.module_cfg.ae_cfg.ae_reg_win.hor_num, ISP_AE_WIN_W);
    CHECK_EQ(ctx.module_cfg.hist_cfg.hist_reg_win.hor_num, 1);

    /* Spot mode takes the raw coordinate path: extents are not clamped. */
    fresh();
    ctx.isp_3a_change_flags = ISP_3A_AF_METERING;
    ctx.af_spot = 1;
    ctx.af_coor[0] = -1000;
    ctx.af_coor[1] = -1000;
    ctx.af_coor[2] = 1000;
    ctx.af_coor[3] = 1000;
    isp_apply_settings(&ctx);
    CHECK_EQ(ctx.module_cfg.af_cfg.af_reg_win.hor_num, 1);
    CHECK_EQ(ctx.module_cfg.af_cfg.af_reg_win.width, 2000);
    CHECK_EQ(ctx.module_cfg.af_cfg.af_reg_win.height, 1000);
}

static void test_colormatrix(void)
{
    fresh();
    ctx.isp_ini_cfg.cm_en = 0;
    isp_apply_colormatrix(&ctx);
    CHECK_EQ(ctx.module_cfg.rgb2rgb_cfg.color_matrix[0][0], 0x100);
    CHECK_EQ(ctx.module_cfg.rgb2rgb_cfg.color_matrix[0][1], 0);
    CHECK_EQ(ctx.module_cfg.rgb2rgb_cfg.color_offset[0], 0);

    fresh();
    ctx.isp_test_settings.cm_en = 1;
    /* Interpolate the adjacent tuning blocks selected by the colour
     * temperature (isp_test_mode 0 -> awb_color_temp_output).  Each block is
     * 12 words: 9 matrix entries then 3 offsets. */
    g_cm_trig[0] = 2500;
    g_cm_trig[1] = 4000;
    g_cm[0] = 256; g_cm[4] = 256; g_cm[8] = 256;
    g_cm[9] = 10; g_cm[10] = 20; g_cm[11] = 30;
    g_cm[12] = 512; g_cm[16] = 512; g_cm[20] = 512;
    g_cm[21] = 110; g_cm[22] = 120; g_cm[23] = 130;
    ctx.awb_color_temp_output = 3250;    /* halfway 2500..4000 */
    isp_apply_colormatrix(&ctx);
    CHECK_EQ(ctx.module_cfg.rgb2rgb_cfg.color_matrix[0][0], 384);
    CHECK_EQ(ctx.module_cfg.rgb2rgb_cfg.color_offset[0], 60);

    /* Defog: step +6, matrix scaled by 0x3ff00/(0x3ff-pre). */
    ctx.isp_test_settings.defog_en = 1;
    ctx.adjust.defog_value = 100;
    ctx.stat.min_rgb_saved = 100;
    ctx.defog_ctx.min_rgb_pre[0] = 0;
    ctx.ae_result.ae_gain = 0x100;       /* do not restore defog_pre */
    isp_apply_colormatrix(&ctx);
    CHECK_EQ(ctx.defog_ctx.min_rgb_pre[0], 6);
    CHECK_EQ(ctx.module_cfg.rgb2rgb_cfg.color_matrix[0][0], 385);
    CHECK_EQ(ctx.module_cfg.rgb2rgb_cfg.color_offset[0],
             (uint16_t)(int16_t)(-24));
}

static void test_dynamic_judge(void)
{
    fresh();
    ctx.isp_test_settings.ae_en = 1;
    ctx.stats_ctx.dynamic_stats.enable = 0;
    ctx.stats_ctx.dynamic_stats.tdnf_comp_target = 99;
    __isp_stat_dynamic_judge(&ctx);
    CHECK_EQ(ctx.stats_ctx.dynamic_stats.tdnf_comp_target, 0);

    fresh();
    ctx.isp_test_settings.ae_en = 1;
    ctx.stats_ctx.dynamic_stats.enable = 1;
    {
        int i;
        static const uint32_t th[ISP_DYN_MOV_TH] = { 10, 20, 30, 40, 50, 60 };
        static const int32_t comp[ISP_STATUS_JUDGE_MAX] = { 1, 2, 3, 4 };
        for (i = 0; i < ISP_DYN_MOV_TH; i++)
            ctx.stats_ctx.dynamic_stats.mov_th[i] = th[i];
        for (i = 0; i < ISP_STATUS_JUDGE_MAX; i++) {
            ctx.stats_ctx.dynamic_stats.tdnf_comp[i] = comp[i];
            ctx.stats_ctx.dynamic_stats.tdnf_diff_comp[i] = comp[i];
            ctx.stats_ctx.dynamic_stats.lp_th_ratio_comp[i] = comp[i];
            ctx.stats_ctx.dynamic_stats.sharp_hfrq_comp[i] = comp[i];
            ctx.stats_ctx.dynamic_stats.sharp_edge_comp[i] = comp[i];
            ctx.stats_ctx.dynamic_stats.sharp_under_shoot_comp[i] = comp[i];
        }
    }
    ctx.stats_ctx.dynamic_stats.accum[0] = 0;
    __isp_stat_dynamic_judge(&ctx);
    CHECK_EQ(ctx.stats_ctx.dynamic_stats.mov, 0);
    CHECK_EQ(ctx.stats_ctx.dynamic_stats.tdnf_comp_target, 1);

    /* Large motion saturates the history clamp and selects level 3.  The
     * 7-deep median must already hold large values, otherwise the current
     * (saturated) estimate is outvoted by the zero history. */
    fresh();
    ctx.isp_test_settings.ae_en = 1;
    ctx.stats_ctx.dynamic_stats.enable = 1;
    {
        int i;
        static const uint32_t th[ISP_DYN_MOV_TH] = { 10, 20, 30, 40, 50, 60 };
        static const int32_t comp[ISP_STATUS_JUDGE_MAX] = { 1, 2, 3, 4 };
        for (i = 0; i < ISP_DYN_MOV_TH; i++)
            ctx.stats_ctx.dynamic_stats.mov_th[i] = th[i];
        for (i = 0; i < ISP_STATUS_JUDGE_MAX; i++)
            ctx.stats_ctx.dynamic_stats.tdnf_comp[i] = comp[i];
        for (i = 0; i < ISP_AE_WIN_N; i++)
            ctx.stats_ctx.dynamic_stats.accum[i] = 0x10000u;
        for (i = 0; i < ISP_DYN_MOV_SAVE; i++)
            ctx.stats_ctx.dynamic_stats.mov_save[i] = 3000;
    }
    __isp_stat_dynamic_judge(&ctx);
    CHECK_EQ(ctx.stats_ctx.dynamic_stats.mov, 0x9a0);
    CHECK_EQ(ctx.stats_ctx.dynamic_stats.tdnf_comp_target, 4);
    CHECK_EQ(ctx.stats_ctx.dynamic_stats.mov_old, 0x9a0);
}

static void test_handle_stats(void)
{
    int i;

    fresh();
    ctx.isp_test_settings.ae_en = 1;
    ctx.stats_ctx.pic_w = 24;
    ctx.stats_ctx.pic_h = 16;
    /* The AE handler averages over the configured register window; 4x1
     * (>>2 in normal mode) gives one sample per window here. */
    ctx.module_cfg.ae_cfg.ae_reg_win.width = 4;
    ctx.module_cfg.ae_cfg.ae_reg_win.height = 1;
    for (i = 0; i < ISP_AE_WIN_N; i++) {
        wr32(ISP_DMA_AE_OFF + (uint32_t)i * 12 + 0, 100);
        wr32(ISP_DMA_AE_OFF + (uint32_t)i * 12 + 4, 200);
        wr32(ISP_DMA_AE_OFF + (uint32_t)i * 12 + 8, 50);
    }
    for (i = 0; i < ISP_HIST_BIN_N / 2; i++)
        wr32(ISP_DMA_HIST_OFF + (uint32_t)i * 4, 1000);

    isp_handle_stats(&ctx, g_buf);

    CHECK_EQ(ctx.stats_ctx.stats.ae.win_r[0], 100);
    CHECK_EQ(ctx.stats_ctx.stats.ae.luma[0], 156);
    CHECK_EQ(ctx.stats_ctx.stats.ae.hist[0], 500);
    CHECK_EQ(ctx.stats_ctx.stats.ae.hist[1], 500);
    CHECK(ctx.stats_attach.ae_stats == &ctx.stats_ctx.stats.ae);
    CHECK(ctx.stats_attach.awb_stats == &ctx.stats_ctx.stats.awb);

    /* Dual-buffer merge: AE columns 0..11 come from the first capture and
     * columns 12..23 from the second, each window averaging its two 12-byte
     * records within that capture. */
    fresh();
    ctx.isp_test_settings.ae_en = 1;
    ctx.stats_ctx.pic_w = 24;
    ctx.stats_ctx.pic_h = 16;
    /* The AE handler averages over the configured register window; 4x1
     * (>>2 in normal mode) gives one sample per window here. */
    ctx.module_cfg.ae_cfg.ae_reg_win.width = 4;
    ctx.module_cfg.ae_cfg.ae_reg_win.height = 1;
    for (i = 0; i < ISP_AE_WIN_N; i++) {
        wr32(ISP_DMA_AE_OFF + (uint32_t)i * 12 + 0, 100);
        wr32(ISP_DMA_AE_OFF + (uint32_t)i * 12 + 4, 200);
        wr32(ISP_DMA_AE_OFF + (uint32_t)i * 12 + 8, 50);
    }
    {
        uint8_t b1[0x10000];
        memset(b1, 0, sizeof(b1));
        for (i = 0; i < ISP_AE_WIN_N; i++) {
            memcpy(b1 + ISP_DMA_AE_OFF + (uint32_t)i * 12 + 0, &(uint32_t){300}, 4);
            memcpy(b1 + ISP_DMA_AE_OFF + (uint32_t)i * 12 + 4, &(uint32_t){400}, 4);
            memcpy(b1 + ISP_DMA_AE_OFF + (uint32_t)i * 12 + 8, &(uint32_t){150}, 4);
        }
        isp_handle_stats_sync(&ctx, g_buf, b1);
    }
    CHECK_EQ(ctx.stats_ctx.stats.ae.win_r[0], 100);
    CHECK_EQ(ctx.stats_ctx.stats.ae.win_g[0], 200);
    CHECK_EQ(ctx.stats_ctx.stats.ae.luma[0], 156);
    CHECK_EQ(ctx.stats_ctx.stats.ae.win_r[12], 300);
    CHECK_EQ(ctx.stats_ctx.stats.ae.win_g[12], 400);
    CHECK_EQ(ctx.stats_ctx.stats.ae.luma[12], 255);
}


/*
 * Zero-divisor regression (on-camera SIGFPE, 2026-09-20 (7)).
 *
 * A data-dependent divisor of zero reaches __aeabi_idiv/__aeabi_uidiv, which
 * raises SIGFPE; the deployed object uses the ARM divide instructions, which
 * return 0.  Every such site now goes through freeisp_sdiv/freeisp_udiv, so
 * these calls must return normally.  Pre-fix the test process died here.
 */
static void test_zero_divisor_paths(void)
{
    /* 1. The helper contract: ARM SDIV/UDIV saturation, not a trap. */
    CHECK_EQ(freeisp_sdiv(100, 0), 0);
    CHECK_EQ(freeisp_udiv(100u, 0u), 0u);
    CHECK_EQ(freeisp_sdiv(INT32_MIN, -1), INT32_MIN);
    CHECK_EQ(freeisp_sdiv(-7, 2), -3);
    CHECK_EQ(freeisp_udiv(7u, 2u), 3u);

    /* 2. config_dig_gain, sensor-offset path: d = (offset[1] >> 4) + 0x100
     *    is zero when offset[1] is -4096. */
    fresh();
    ctx.isp_ini_cfg.gains.bayer_gain[0] = 100;
    ctx.sensor_info.so_en = 1;
    ctx.module_cfg.gain_offset_cfg.sensor_offset[1] = -4096;
    config_dig_gain(&ctx, 1024);

    /* 3. config_dig_gain, black-level path: same shape. */
    fresh();
    ctx.isp_ini_cfg.gains.bayer_gain[0] = 100;
    ctx.sensor_info.blc_en = 1;
    ctx.module_cfg.gain_offset_cfg.offset[1] = -4096;
    config_dig_gain(&ctx, 1024);

    /* 4. config_defog (reached through isp_apply_colormatrix): a defog_pre of
     *    0x3ff makes (0x3ff - pre) zero. */
    fresh();
    ctx.isp_test_settings.defog_en = 1;
    ctx.adjust.defog_value = 100;
    ctx.ae_result.ae_gain = 0x200;      /* != 0x100, so defog_pre is used */
    ctx.defog_ctx.defog_pre = 0x3ff;
    isp_apply_colormatrix(&ctx);

    /* 5. isp_handle_stats: a 1x1 AE window in non-WDR mode shifts to
     *    win_pix_n == 0, which the four window normalisations divide by. */
    fresh();
    ctx.isp_test_settings.ae_en = 1;
    ctx.stats_ctx.pic_w = 24;
    ctx.stats_ctx.pic_h = 16;
    ctx.ae_settings.ae_mode = 0;
    ctx.module_cfg.ae_cfg.ae_reg_win.width = 1;
    ctx.module_cfg.ae_cfg.ae_reg_win.height = 1;
    isp_handle_stats(&ctx, g_buf);
    CHECK_EQ(ctx.stats_ctx.stats.ae.win_pix_n, 0u);
    CHECK_EQ(ctx.stats_ctx.stats.ae.luma[0], 0);
}

int main(void)
{
    test_band_step();
    test_zero_divisor_paths();
    test_lens_center();
    test_dig_gain();
    test_gamma();
    test_lens_table();
    test_msc_table();
    test_wdr();
    test_apply_settings();
    test_colormatrix();
    test_dynamic_judge();
    test_handle_stats();

    if (fails) {
        printf("test_base: %d check(s) failed\n", fails);
        return 1;
    }
    printf("test_base: all checks passed\n");
    return 0;
}
