/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * test_iso.c - host tests for the clean-room ISO module (spec/iso.md).
 *
 * The tests do not compare against vendor output.  Every expected value is
 * computed independently from the formulas in the specification using small,
 * hand-checkable inputs.
 */
#include "iso_clean.h"

#include <math.h>
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

static uint32_t g_gain_index[ISO_CURVE_N];
static int32_t g_gain_default[ISO_BP_N];
static int32_t g_lum_default[ISO_BP_N];
static uint8_t g_af_square[16];
static const int32_t g_af_iir_s[4] = { 0x0011, 0x0022, 0x0033, 0x0044 };

static const iso_clean_tables_t g_iso = {
    g_gain_index, g_gain_default, g_lum_default, g_af_square, g_af_iir_s
};
static const freeisp_tables_t g_tables = { &g_iso };

const freeisp_tables_t *freeisp_get_tables(void)
{
    return &g_tables;
}

/* ---- fixtures ----------------------------------------------------- */

static iso_ctx_t    ctx;
static iso_params_t params;
static iso_result_t res;
static iso_entity_t *ent;

/* Rebuilding a fresh scene.  Characteristic defaults:
 *   gain_index_table[k] = k
 *   gain_point[i]       = 30 + 2i   -> gain_axis[i] = 30 + 2i
 *   lum_point[i]        = 25(i + 1) -> lum_axis[i]  = 25(i + 1)
 *   cfg[i].word[w]      = 100i + w
 * With ae_pos = 0 the luminance index is 0 and by_lum[0] == cfg[0]. */
static void fresh(void)
{
    int i, w;

    memset(&ctx, 0, sizeof(ctx));
    memset(&params, 0, sizeof(params));
    memset(&res, 0, sizeof(res));

    for (i = 0; i < ISO_CURVE_N; i++)
        g_gain_index[i] = (uint32_t)i;
    for (i = 0; i < ISO_BP_N; i++) {
        g_gain_default[i] = 40 + i;
        g_lum_default[i] = 20 + i;
    }
    for (i = 0; i < 16; i++)
        g_af_square[i] = (uint8_t)(0xa0 + i);

    params.gen = &ctx;
    params.param_kind = ISO_PARAM_REBUILD;
    for (i = 0; i < ISO_BP_N; i++) {
        params.gain_point[i] = 30 + 2 * i;
        params.lum_point[i] = 25 * (i + 1);
        for (w = 0; w < ISO_DYN_WORDS; w++)
            params.cfg[i].word[w] = 100 * i + w;
    }
}

static void apply(void)
{
    CHECK_EQ(iso_set_params(ent, &params, &res), 0);
    memset(&res, 0, sizeof(res));
    CHECK_EQ(iso_run(ent, &res), 0);
}

/* ------------------------------------------------------------------ */
/* Indices (spec 6.1)                                                 */
/* ------------------------------------------------------------------ */

static void test_indices(void)
{
    fresh();
    ctx.ae_pos_max = 100;
    ctx.ae_pos = 50;
    ctx.total_gain = 100;           /* g = (100*100)>>8 = 39 */
    apply();
    CHECK_EQ(iso_run(ent, &res), 0);
    CHECK_EQ(res.lum_i, 174);       /* 349*50/100 */
    CHECK_EQ(res.gain_i, 39);       /* first table[k] >= 39 */

    fresh();
    ctx.ae_pos_max = 0;
    ctx.ae_pos = 123;
    apply();
    CHECK_EQ(iso_run(ent, &res), 0);
    CHECK_EQ(res.lum_i, 0);

    /* scaled gain strictly above 0xc7fff snaps to 0xc8000, then misses. */
    fresh();
    ctx.total_gain = 2100000;
    apply();
    CHECK_EQ(iso_run(ent, &res), 0);
    CHECK_EQ(res.gain_i, ISO_SEARCH_HI);

    /* search starts at 0x18. */
    fresh();
    for (int i = 0; i < ISO_CURVE_N; i++)
        g_gain_index[i] = 100000;
    ctx.total_gain = 100;
    apply();
    CHECK_EQ(iso_run(ent, &res), 0);
    CHECK_EQ(res.gain_i, ISO_SEARCH_LO);
}

/* ------------------------------------------------------------------ */
/* Array construction (spec 6.2)                                      */
/* ------------------------------------------------------------------ */

static void test_arrays(void)
{
    fresh();
    apply();

    /* by_gain: axis 30,32,..,56. */
    CHECK_EQ(iso_debug_gain_word(ent, 0, 0), 0);
    CHECK_EQ(iso_debug_gain_word(ent, 30, 0), 0);    /* cfg[0] */
    CHECK_EQ(iso_debug_gain_word(ent, 31, 0), 50);   /* halfway 0->1 */
    CHECK_EQ(iso_debug_gain_word(ent, 33, 0), 150);  /* halfway 1->2 */
    CHECK_EQ(iso_debug_gain_word(ent, 43, 0), 650);  /* interpolated */
    CHECK_EQ(iso_debug_gain_word(ent, 31, 7), 57);

    /* by_lum: axis 25,50,..,350. */
    CHECK_EQ(iso_debug_lum_word(ent, 0, 0), 0);      /* held at cfg[0] */
    CHECK_EQ(iso_debug_lum_word(ent, 37, 0), 48);    /* 100*12/25 */
    CHECK_EQ(iso_debug_lum_word(ent, 50, 0), 100);   /* cfg[1] */
    CHECK_EQ(iso_debug_lum_word(ent, 75, 0), 200);   /* cfg[2] */
}

static void test_defaults(void)
{
    iso_params_t *p;

    fresh();
    params.gain_point[3] = 0;
    params.lum_point[5] = -1;
    apply();

    CHECK_EQ(iso_get_params(ent, &p), 0);
    CHECK_EQ(p->gain_point[0], 40);
    CHECK_EQ(p->gain_point[3], 43);
    CHECK_EQ(p->gain_point[13], 53);
    CHECK_EQ(p->lum_point[0], 20);
    CHECK_EQ(p->lum_point[5], 25);
    CHECK_EQ(p->lum_point[13], 33);
}

/* ------------------------------------------------------------------ */
/* Simple blocks in one run (spec 6.3 - 6.6, 6.9 - 6.12, 6.14)        */
/* ------------------------------------------------------------------ */

static void test_blocks(void)
{
    iso_ctx_t *c = &ctx;
    int k;

    fresh();
    c->ae_pos = 0;
    c->ae_pos_max = 100;

    params.cfg[0].f.color_denoise = 200;

    params.cfg[0].f.contrast = 10;
    for (k = 0; k < 10; k++)
        params.cfg[0].f.contrast_cfg[k] = 100 + k;

    params.cfg[0].f.brightness = 5;

    params.cfg[0].f.sat_cfg[0] = 100;
    params.cfg[0].f.sat_cfg[1] = 200;
    params.cfg[0].f.sat_cfg[2] = 300;
    params.cfg[0].f.sat_cfg[3] = 2;
    params.cfg[0].f.sat_cfg[4] = 10;
    params.cfg[0].f.sat_cfg[5] = 20;
    params.cfg[0].f.sat_cfg[6] = 16;

    params.cfg[0].f.sensor_offset[0] = -10;
    params.cfg[0].f.sensor_offset[1] = 20;
    params.cfg[0].f.sensor_offset[2] = -30;
    params.cfg[0].f.sensor_offset[3] = 40;

    params.cfg[0].f.black_level[0] = 1;
    params.cfg[0].f.black_level[1] = 2;
    params.cfg[0].f.black_level[2] = 3;
    params.cfg[0].f.black_level[3] = 4;

    for (k = 0; k < 6; k++)
        params.cfg[0].f.dpc_cfg[k] = 10 + k;

    params.cfg[0].f.defog_value = 77;

    params.cfg[0].f.pltm_dynamic_cfg[0] = 10;
    params.cfg[0].f.pltm_dynamic_cfg[1] = 20;
    params.cfg[0].f.pltm_dynamic_cfg[2] = 30;
    params.cfg[0].f.pltm_dynamic_cfg[3] = 40;

    for (k = 0; k < 11; k++)
        params.cfg[0].f.lca_cfg[k] = 1000 + k;

    c->contrast_level = 20;
    c->contrast_enable = 1;
    c->brightness_level = -3;
    c->sat_level = 50;
    c->pltm_level = 5;

    params.cnr_on = 1;
    params.contrast_on = 1;
    params.brightness_on = 1;
    params.sat_on = 1;
    params.sensor_offset_on = 1;
    params.black_level_on = 1;
    params.dpc_on = 1;
    params.defog_on = 1;
    params.pltm_dyn_on = 1;
    params.lca_cfg_on = 1;
    params.af_cfg_on = 1;

    apply();

    /* CNR */
    CHECK_EQ(res.c_threshold, 200);
    CHECK_EQ(res.y_threshold, 156);
    CHECK_EQ(res.st_v_yth, 181);
    CHECK_EQ(res.st_h_yth, 15);

    /* contrast */
    CHECK_EQ(c->adjust.contrast, 30);
    CHECK_EQ(c->dynamic_enable, 1);
    CHECK_EQ(c->comp.tdnf_comp[0], 100);
    CHECK_EQ(c->comp.tdnf_comp[1], 50);
    CHECK_EQ(c->comp.tdnf_comp[2], 52);
    CHECK_EQ(c->comp.tdnf_comp[3], 105);
    CHECK_EQ(c->comp.lp_th_ratio_comp[0], 101);

    /* brightness */
    CHECK_EQ(c->adjust.brightness, 2);

    /* saturation */
    CHECK_EQ(res.satu_r, 100);
    CHECK_EQ(res.satu_g, 200);
    CHECK_EQ(res.satu_b, 300);
    CHECK_EQ(res.saturation_mode, 2);
    CHECK_EQ(res.sat_curve[16], 65);
    CHECK((c->table_update & 0x20u) != 0);

    /* sensor offset + AF */
    CHECK_EQ(c->module.sensor_offset[0], -10);
    CHECK_EQ(c->module.sensor_offset[1], 20);
    CHECK_EQ(c->module.sensor_offset[2], -30);
    CHECK_EQ(c->module.sensor_offset[3], 40);
    CHECK_EQ(c->sensor.gain_offset[1], 20);
    CHECK_EQ(res.af.r_offset, -10);
    CHECK_EQ(res.af.g_offset, -5);
    CHECK_EQ(res.af.b_offset, 40);
    CHECK_EQ(res.af.mode, 0);
    CHECK_EQ(res.af.iir_g[0], 0x01e0);
    CHECK_EQ(res.af.iir_g[1], -233);
    CHECK_EQ(res.af.iir_s[3], 0x0044);
    CHECK_EQ(res.af.fir_g[4], -20);
    CHECK_EQ(res.af.iir0_dilate, 2);
    CHECK_EQ(res.af.iir_ldg_hth, 0xc8);
    CHECK_EQ(res.af.fir_ldg_hslope, 0x0e);
    CHECK_EQ(res.af.iir_core_peak, 0xff);
    CHECK_EQ(res.af.hlt_th, 0xeb);
    CHECK_EQ(res.af_square_lut[0], 0xa0);
    CHECK_EQ(res.af_square_lut[15], 0xaf);

    /* black level */
    CHECK_EQ(c->module.offset[0], 1);
    CHECK_EQ(c->module.offset[1], 2);
    CHECK_EQ(c->module.offset[2], 3);
    CHECK_EQ(c->module.offset[3], 4);

    /* DPC */
    CHECK_EQ(res.hot_ratio, 10);
    CHECK_EQ(res.cold_ratio, 11);
    CHECK_EQ(res.nbhd_diff_ratio, 12);
    CHECK_EQ(res.nearest_diff_ratio, 13);
    CHECK_EQ(res.slope_th, 14);
    CHECK_EQ(res.cold_abs_th, 15);

    /* defog */
    CHECK_EQ(c->adjust.defog_value, 77);

    /* PLTM dynamic */
    CHECK_EQ(c->ae.pltm_dynamic_cfg[0], 15);
    CHECK_EQ(c->ae.pltm_dynamic_cfg[1], 25);
    CHECK_EQ(c->ae.pltm_dynamic_cfg[2], 30);
    CHECK_EQ(c->ae.pltm_dynamic_cfg[3], 40);

    /* LCA */
    CHECK_EQ(res.lca_gf_cor_ratio, 1000);
    CHECK_EQ(res.lca_pf_cor_ratio, 1001);
    CHECK_EQ(res.lca_lum_th, 1002);
    CHECK_EQ(res.lca_clrs_lum_th, (uint8_t)1007);
    CHECK_EQ(res.lca_pf_decr_ratio, (uint8_t)1010);
}

/* ------------------------------------------------------------------ */
/* 2D denoise (spec 6.8)                                              */
/* ------------------------------------------------------------------ */

static void test_denoise(void)
{
    fresh();
    ctx.ae_pos = 0;
    ctx.ae_pos_max = 100;

    params.cfg[0].f.denoise_cfg[0] = 0;
    params.cfg[0].f.denoise_cfg[1] = 32;
    params.cfg[0].f.denoise_cfg[2] = 0;
    params.cfg[0].f.denoise_cfg[3] = 64;
    params.cfg[0].f.denoise_cfg[4] = 4;
    params.cfg[0].f.denoise_cfg[5] = 5;
    params.cfg[0].f.denoise_cfg[6] = 6;
    params.cfg[0].f.denoise_cfg[7] = 7;
    params.cfg[0].f.denoise_cfg[8] = 8;
    params.cfg[0].f.denoise_cfg[9] = 9;
    for (int i = 0; i < 4; i++)
        params.cfg[0].f.denoise_cfg[0xb + i] = 11 + i;
    params.cfg[0].f.denoise_cfg[0xe] = 14;
    params.cfg[0].f.denoise_cfg[0xf] = 15;
    params.cfg[0].f.denoise_cfg[0x10] = 16;
    params.cfg[0].f.denoise_cfg[0x11] = 17;

    params.dn_core_ratio[0] = 1;
    params.dn_core_ratio[1] = 2;
    params.dn_core_ratio[2] = 3;
    params.dn_core_ratio[3] = 4;
    params.denoise_on = 1;

    apply();

    CHECK_EQ(res.hf_ratio, 4);
    CHECK_EQ(res.bf_ratio, 5);
    CHECK_EQ(res.lf_ratio, 6);
    CHECK_EQ(res.lp0_np_side_ratio, 7);
    CHECK_EQ(res.lp2_np_side_ratio, 9);
    CHECK_EQ(res.lp0_np_core_ratio, 1);
    CHECK_EQ(res.lp3_np_core_ratio, 4);
    CHECK_EQ(res.lp0_pcnt_ratio, 14);
    CHECK_EQ(res.lp3_pcnt_ratio, 17);

    /* v2 = 32 + k; r_b = 11, r_c = 12, r_d = 13 */
    CHECK_EQ(res.d2d_lp0_th[0], 32);
    CHECK_EQ(res.d2d_lp0_th[32], 64);
    CHECK_EQ(res.d2d_lp1_th[0], 1);
    CHECK_EQ(res.d2d_lp1_th[32], 2);
    CHECK_EQ(res.d2d_lp2_th[0], 1);
    CHECK_EQ(res.d2d_lp2_th[32], 3);
    CHECK_EQ(res.d2d_lp3_th[0], 1);
    CHECK_EQ(res.d2d_lp3_th[32], 3);
}

/* ------------------------------------------------------------------ */
/* Sharpness (spec 6.14)                                              */
/* ------------------------------------------------------------------ */

static void test_sharp(void)
{
    fresh();
    ctx.ae_pos = 0;
    ctx.ae_pos_max = 100;
    ctx.sharp_level = 200;
    ctx.sharp_val[0] = 10;
    ctx.sharp_val[0x21] = 20;
    ctx.sharp_val[16] = 8;
    ctx.sharp_val[0x21 + 16] = 33;
    ctx.sharp_edge_lum[0] = 0x1234;

    params.cfg[0].f.sharp_cfg[6] = 0;
    params.cfg[0].f.sharp_cfg[7] = 32;
    params.cfg[0].f.sharp_cfg[8] = 64;
    params.cfg[0].f.sharp_cfg[9] = 96;
    params.cfg[0].f.sharp_cfg[10] = 1;
    params.cfg[0].f.sharp_cfg[11] = 2;
    params.cfg[0].f.sharp_cfg[12] = 3;
    params.cfg[0].f.sharp_cfg[13] = 4;
    params.cfg[0].f.sharp_cfg[14] = 5;
    params.cfg[0].f.sharp_cfg[15] = 6;
    params.cfg[0].f.sharp_cfg[16] = 7;
    params.cfg[0].f.sharp_cfg[17] = 8;
    params.cfg[0].f.sharp_cfg[18] = 9;
    params.cfg[0].f.sharp_cfg[19] = 10;
    params.cfg[0].f.sharp_cfg[20] = 11;
    params.cfg[0].f.sharp_cfg[0x15] = 100;
    params.cfg[0].f.sharp_cfg[0x16] = 101;
    params.cfg[0].f.sharp_cfg[0x17] = 102;
    params.cfg[0].f.sharp_cfg[0x18] = 103;
    params.cfg[0].f.sharp_cfg[0x19] = 106;
    params.cfg[0].f.sharp_cfg[0x1a] = 104;
    params.cfg[0].f.sharp_cfg[0x1b] = 107;
    params.cfg[0].f.sharp_cfg[0x1c] = 105;
    params.sharp_on = 1;

    apply();

    CHECK_EQ(res.edge_scale_ratio, 1);
    CHECK_EQ(res.aa_edge_sm_ratio, 11);
    CHECK_EQ(res.edge_white_stren, 200);
    CHECK_EQ(res.edge_black_stren, 202);
    CHECK_EQ(res.hfrq_white_stren, 204);
    CHECK_EQ(res.hfrq_black_stren, 206);
    CHECK_EQ(res.under_area_ctrl, 104);
    CHECK_EQ(res.over_area_ctrl, 106);
    CHECK_EQ(res.under_val_ctrl, 105);
    CHECK_EQ(res.over_val_ctrl, 107);
    CHECK_EQ(ctx.tune.sharpness_level, 200);
    CHECK_EQ(res.sharp_val[0], 32);
    CHECK_EQ(res.sharp_val[16], 65);
    CHECK_EQ(res.sharp_val[0x21 + 16], 68);
    CHECK_EQ(res.sharp_edge_lum[0], 0x1234);
}

/* ------------------------------------------------------------------ */
/* AE / GTM (spec 6.12)                                               */
/* ------------------------------------------------------------------ */

static void test_ae_gtm(void)
{
    fresh();
    ctx.ae_pos = 0;
    ctx.ae_pos_max = 100;
    ctx.highlight_level = -16;
    ctx.backlight_level = 3;

    params.cfg[0].f.ae_cfg[0] = 10;
    params.cfg[0].f.ae_cfg[1] = 20;
    for (int k = 2; k <= 13; k++)
        params.cfg[0].f.ae_cfg[k] = 100 + k;
    for (int k = 0; k <= 8; k++)
        params.cfg[0].f.gtm_cfg[k] = 200 + k;
    params.ae_cfg_on = 1;
    params.gtm_cfg_on = 1;

    apply();

    CHECK_EQ(ctx.ae.exposure_cfg[0], 5);    /* 10*(-16+32)>>5 */
    CHECK_EQ(ctx.ae.exposure_cfg[1], 80);   /* 20*3 + 20 */
    CHECK_EQ(ctx.ae.exposure_cfg[2], 102);
    CHECK_EQ(ctx.ae.exposure_cfg[13], 113);
    for (int k = 0; k <= 8; k++)
        CHECK_EQ(ctx.ae.ae_hist_eq_cfg[k], 200 + k);
}

/* ------------------------------------------------------------------ */
/* Temporal denoise (spec 6.13)                                       */
/* ------------------------------------------------------------------ */

static void test_tdnr(void)
{
    fresh();
    ctx.ae_pos = 0;
    ctx.ae_pos_max = 100;
    params.tdnr_on = 1;

    /* Warm-up reset with the feature off: start = frame_id + 1 = 11. */
    ctx.tdf_enable = 0;
    params.frame_id = 10;
    apply();
    CHECK_EQ(res.tdf.rec_en, 0);

    params.cfg[0].f.tdf_cfg[4] = 1;
    params.cfg[0].f.tdf_cfg[6] = 1;
    ctx.tdf_enable = 1;

    params.frame_id = 11;
    apply();
    CHECK_EQ(res.tdf.rec_en, 0);            /* frame_id <= start */
    CHECK((ctx.module_enable_flag & 0x20) != 0);

    params.frame_id = 12;
    apply();
    CHECK_EQ(res.tdf.rec_en, 1);            /* one frame past start */

    /* Table composition and diff clamp. */
    fresh();
    ctx.ae_pos = 0;
    ctx.ae_pos_max = 100;
    ctx.tdf_enable = 1;
    for (int i = 0; i < 256; i++) {
        ctx.k3d_incre_curve[i] = i;
        ctx.tdnf_diff[i] = 1000;
    }
    params.cfg[0].f.tdf_cfg[4] = 1;
    params.cfg[0].f.tdf_cfg[6] = 1;
    params.cfg[0].f.tdf_cfg[0x14] = 50;
    params.frame_id = 100;
    params.tdnr_on = 1;
    apply();

    CHECK_EQ(res.tdf.tdnf_table[0], 0);
    CHECK_EQ(res.tdf.tdnf_table[255], 255);
    CHECK_EQ(res.tdf.tdnf_table[256], 50);  /* clamped diff */
    CHECK_EQ(res.tdf.tdnf_table[511], 50);
    CHECK_EQ(res.tdf.tdnf_k_delta[0], 0x1f);
    CHECK_EQ(res.tdf.tdnf_k_delta[31], 0);
    CHECK_EQ(res.tdf.ltf_update_frm, 0x19);
}

/* ------------------------------------------------------------------ */
/* CEM blend and saturation shaping (spec 6.7)                        */
/* ------------------------------------------------------------------ */

static void test_cem(void)
{
    fresh();
    ctx.ae_pos = 0;
    ctx.ae_pos_max = 100;
    for (int i = 0; i < ISO_CEM_TABLE_N; i++) {
        ctx.cem_table_a[i] = 128;
        ctx.cem_table_b[i] = 128;
    }
    ctx.cem_table_a[0] = 192;           /* ratio = 128 -> exact values */
    ctx.cem_table_b[0] = 192;
    ctx.cem_table_a[1] = 128;
    ctx.cem_table_b[1] = 128;

    params.cfg[0].f.cem_ratio = 128;
    params.cfg[0].f.sat_cfg[0] = 256;   /* g3 = 0 uses this */
    params.cem_on = 1;

    /* sat_enable != 0, ae_mode normal: only the blend runs. */
    ctx.sat_enable = 1;
    apply();
    CHECK_EQ(res.out_cem[0], 192);
    CHECK_EQ(res.out_cem[1], 128);
    CHECK_EQ(res.out_cem[5000], 128);

    /* sat_enable == 0: cell (0,0,0) reshapes bytes 0/1.
     * apply_saturation(192,128,256) -> (97,0) -> (225,128). */
    fresh();
    ctx.ae_pos = 0;
    ctx.ae_pos_max = 100;
    for (int i = 0; i < ISO_CEM_TABLE_N; i++) {
        ctx.cem_table_a[i] = 128;
        ctx.cem_table_b[i] = 128;
    }
    ctx.cem_table_a[0] = 192;
    ctx.cem_table_b[0] = 192;
    params.cfg[0].f.cem_ratio = 128;
    params.cfg[0].f.sat_cfg[0] = 256;
    params.cem_on = 1;
    ctx.sat_enable = 0;
    apply();
    CHECK_EQ(res.out_cem[0], 225);
    CHECK_EQ(res.out_cem[1], 128);
}

/* ------------------------------------------------------------------ */
/* Lifecycle and error behaviour (spec 4, 9)                          */
/* ------------------------------------------------------------------ */

static void test_lifecycle(void)
{
    iso_ops_t ops;
    iso_entity_t *e;
    iso_params_t *p;
    int32_t before;

    e = iso_init(&ops);
    CHECK(e != NULL);
    CHECK(ops.get_params != NULL);
    CHECK(ops.set_params != NULL);
    CHECK(ops.run != NULL);

    CHECK_EQ(iso_run(NULL, &res), -1);
    CHECK_EQ(iso_run(e, NULL), -1);
    iso_exit(e);

    /* A good rebuild, then a bad kind must change nothing. */
    fresh();
    ctx.ae_pos = 0;
    ctx.ae_pos_max = 100;
    params.frame_id = 7;
    apply();
    CHECK_EQ(iso_get_params(ent, &p), 0);
    CHECK_EQ(p->frame_id, 7);
    before = iso_debug_lum_word(ent, 0, 0);

    params.param_kind = 9;
    CHECK_EQ(iso_set_params(ent, &params, &res), -1);
    CHECK_EQ(p->frame_id, 7);
    CHECK_EQ(p->param_kind, ISO_PARAM_REBUILD);
    CHECK_EQ(iso_debug_lum_word(ent, 0, 0), before);

    CHECK_EQ(iso_set_params(ent, NULL, &res), -1);
    CHECK_EQ(iso_get_params(NULL, &p), -1);
}

int main(void)
{
    iso_ops_t ops;

    ent = iso_init(&ops);
    if (!ent) {
        printf("FAIL: iso_init returned NULL\n");
        return 1;
    }

    test_indices();
    test_arrays();
    test_defaults();
    test_blocks();
    test_denoise();
    test_sharp();
    test_ae_gtm();
    test_tdnr();
    test_cem();
    test_lifecycle();

    iso_exit(ent);

    if (fails) {
        printf("test_iso: %d check(s) failed\n", fails);
        return 1;
    }
    printf("test_iso: all checks passed\n");
    return 0;
}
