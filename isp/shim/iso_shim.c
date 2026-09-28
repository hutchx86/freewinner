/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* iso_shim.c - presents the clean ISO core to the framework's
 * fwi_iso_cfg_core_ops_t. Field mapping: docs/shim-mappings.md#iso */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Clean entry points and iso_result_t collide with the SDK names; renamed for
 * this TU only. */
#define iso_result_t   clean_iso_result_t
#define iso_init       clean_iso_init
#define iso_exit       clean_iso_exit
#define iso_get_params clean_iso_get_params
#define iso_set_params clean_iso_set_params
#define iso_run        clean_iso_run
#include "iso_clean.h"
#undef iso_result_t
#undef iso_init
#undef iso_exit
#undef iso_get_params
#undef iso_set_params
#undef iso_run

/* Framework side: the generated fwi_* ABI. */
#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"

#include "iso_shim.h"

/* map_params() byte-copies the per-index dynamic config; sizes must match. */
typedef char iso_dyn_size_check[
    (sizeof(iso_dyn_t) == sizeof(struct fwi_dynamic_cfg)) ? 1 : -1];

/* ------------------------------------------------------------------ */
/* Table provider                                                      */
/* ------------------------------------------------------------------ */

static uint32_t g_gain_index[ISO_CURVE_N];
static int32_t  g_gain_default[ISO_BP_N];
static int32_t  g_lum_default[ISO_BP_N];
static uint8_t  g_af_square[16];
/* No vendor table carries the AF IIR feedback coefficients: neutral default,
 * used only when no tables are installed via iso_shim_set_tables(). */
static int32_t  g_af_iir_s[4] = { 0, 0, 0, 0 };
static int      g_defaults_ready;

static const iso_clean_tables_t g_default_iso = {
    g_gain_index, g_gain_default, g_lum_default, g_af_square, g_af_iir_s
};
static const freeisp_tables_t g_default_tables = { &g_default_iso };

static freeisp_tables_t g_set_tables;
static const freeisp_tables_t *g_tables = &g_default_tables;

static void ensure_defaults(void)
{
    int i;

    if (g_defaults_ready)
        return;

    /* Placeholder data (monotone, keeps the clean builders in range); real
     * tuning must be installed with iso_shim_set_tables() before iso_init(). */
    for (i = 0; i < ISO_CURVE_N; i++)
        g_gain_index[i] = (uint32_t)i;
    for (i = 0; i < ISO_BP_N; i++) {
        g_gain_default[i] = 24 + 2 * i;
        g_lum_default[i] = 24 * (i + 1);
    }
    for (i = 0; i < 16; i++)
        g_af_square[i] = (uint8_t)(0xa0 + i);

    g_defaults_ready = 1;
}

const freeisp_tables_t *freeisp_get_tables(void)
{
    ensure_defaults();
    return g_tables;
}

void iso_shim_set_tables(const void *tables)
{
    if (tables) {
        g_set_tables.iso = (const iso_clean_tables_t *)tables;
        g_tables = &g_set_tables;
    } else {
        g_tables = &g_default_tables;
    }
}

/* ------------------------------------------------------------------ */
/* Entity                                                              */
/* ------------------------------------------------------------------ */

typedef struct iso_shim_entity {
    fwi_iso_param_t   config;   /* SDK mirror at offset 0; get_params returns it */
    iso_entity_t *clean;    /* clean-room instance                           */
    iso_ctx_t     gen;      /* clean shared context, pointed to by clean     */
} iso_shim_entity_t;

/* ------------------------------------------------------------------ */
/* Parameter mapping: iso_param_t -> iso_params_t                      */
/* ------------------------------------------------------------------ */

/* Fields the framework mutates in the stored block between set_params calls;
 * refreshed per run without rebuilding the interpolation arrays. */
static void map_live_params(iso_params_t *p, const fwi_iso_param_t *s)
{
    p->frame_id = s->iso_frame_id;
    p->chroma_gray_th = s->colour_enhance_color2gray_threshold;
    p->chroma_gray_span_min = s->colour_enhance_color2gray_delta_min;
    p->chroma_gray_span_max = s->colour_enhance_color2gray_delta_max;

    p->cnr_on = s->chroma_denoise_adjust;
    p->sharp_on = s->sharpness_adjust;
    p->sat_on = s->saturation_adjust;
    p->contrast_on = s->contrast_adjust;
    p->brightness_on = s->brightness_adjust;
    p->cem_on = s->colour_enhance_ratio_adjust;
    p->denoise_on = s->denoise_adjust;
    p->sensor_offset_on = s->sensor_offset_adjust;
    p->black_level_on = s->black_level_adjust;
    p->dpc_on = s->defect_pixel_adjust;
    p->defog_on = s->dehaze_value_adjust;
    p->pltm_dyn_on = s->pltm_dynamic_cfg_adjust;
    p->tdnr_on = s->tdnr_adjust;
    p->ae_cfg_on = s->ae_cfg_adjust;
    p->gtm_cfg_on = s->gtm_cfg_adjust;
    p->lca_cfg_on = s->lateral_ca_cfg_adjust;
    p->af_cfg_on = s->af_cfg_adjust;

    p->dn_core_ratio[0] = s->denoise_lp0_np_core_ratio;
    p->dn_core_ratio[1] = s->denoise_lp1_np_core_ratio;
    p->dn_core_ratio[2] = s->denoise_lp2_np_core_ratio;
    p->dn_core_ratio[3] = s->denoise_lp3_np_core_ratio;
}

static void map_params(iso_params_t *p, const fwi_iso_param_t *s,
                       iso_ctx_t *gen, const struct fwi_isp_ctx *c)
{
    int i;

    memset(p, 0, sizeof(*p));

    p->param_kind = ISO_PARAM_REBUILD;
    map_live_params(p, s);

    p->gen = gen;

    if (c) {
        const fwi_tuning_by_iso_t *iso = &c->tuning.by_iso;

        for (i = 0; i < ISO_BP_N; i++) {
            p->gain_point[i] = (int32_t)iso->gain_mapping_point[i];
            p->lum_point[i] = (int32_t)iso->luminance_mapping_point[i];
            /* fwi_dynamic_cfg_t matches iso_dyn_t layout (19 s32 members). */
            memcpy(&p->cfg[i], &iso->dynamic_cfg[i], sizeof(p->cfg[i]));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Live tuning corpus: SDK tuning.modules -> clean iso_ctx_t curves    */
/* ------------------------------------------------------------------ */
/* Shapes differ (u8/u16 -> i32, two-half merges for sharp_val and tdnf_th,
 * spec iso.md 6.13). Table: docs/shim-mappings.md#iso */
static void map_tuning(iso_ctx_t *g, const struct fwi_isp_ctx *c)
{
    const fwi_tuning_modules_t *t = &c->tuning.modules;
    int i;

    for (i = 0; i < 256; i++) {
        g->k3d_incre_curve[i] = (int32_t)t->d3d_k3d_incre_curve[i];
        g->tdnf_diff[i] = (int32_t)t->temporal_denoise_diff[i];
    }
    for (i = 0; i < 33; i++) {
        g->sharp_edge_lum[i] = t->sharp_edge_luminance[i];
        g->sharp_hfrq_lum[i] = t->sharp_high_frequency_luminance[i];
        g->sharp_s_map[i] = t->sharp_s_map[i];
        g->bdnf_th[i] = (int32_t)t->bayer_denoise_threshold[i];
        g->sharp_val[i] = (int32_t)t->sharp_val[i];
        g->sharp_val[33 + i] = (int32_t)t->sharp_luminance[i];
        g->tdnf_th[i] = (int32_t)t->temporal_denoise_threshold[i];
        g->tdnf_th[33 + i] = (int32_t)t->temporal_denoise_ref_noise[i];
    }
    for (i = 0; i < 46; i++)
        g->sharp_hsv[i] = t->sharp_hsv[i];
    for (i = 0; i < 32; i++)
        g->tdnf_k[i] = (int32_t)t->temporal_denoise_k[i];

    memcpy(g->cem_table_a, t->colour_enhance_table, ISO_CEM_TABLE_N);
    memcpy(g->cem_table_b, t->colour_enhance_table1, ISO_CEM_TABLE_N);
}

/* ------------------------------------------------------------------ */
/* Context mapping: fwi_isp_ctx -> iso_ctx_t (inputs only)             */
/* ------------------------------------------------------------------ */

static void map_ctx_inputs(iso_ctx_t *g, const struct fwi_isp_ctx *c)
{
    const fwi_tuning_enables_t *test;
    const fwi_dynamic_judge_stats_t *dyn;
    const uint8_t *trig;
    int i, r, col;

    if (!c)
        return;

    map_tuning(g, c);

    test = &c->tuning.enables;
    dyn = &c->stats.dynamic_stats;
    /* trigger_selectors[17] is a raw u8 array; index order is listed in
     * docs/shim-mappings.md#iso. */
    trig = c->tuning.by_iso.trigger_selectors;

    g->total_gain = (int32_t)c->sensor.total_gain;
    g->ae_pos = (int32_t)c->sensor.ae_tbl_index;
    g->ae_pos_max = (int32_t)c->sensor.ae_tbl_index_max;
    g->ae_mode = (int32_t)c->ae_ctl.ae_mode;

    g->tdf_enable = test->denoise_3d_en;
    g->contrast_enable = test->local_contrast_en;
    g->wb_enable = test->wb_gain_en;
    g->sat_enable = test->saturation_en;
    g->pltm_enable = test->local_tone_en;

    g->contrast_level = c->picture_ctl.contrast_level;
    g->sat_level = c->picture_ctl.saturation_level;
    g->sharp_level = c->picture_ctl.sharpness_level;
    g->brightness_level = c->picture_ctl.brightness_level;
    g->denoise_level = c->picture_ctl.denoise_level;
    g->pltm_level = c->picture_ctl.pltmwdr_level;
    g->tdf_level = c->picture_ctl.denoise_3d_level;
    g->highlight_level = c->picture_ctl.highlight_level;
    g->backlight_level = c->picture_ctl.backlight_level;

    g->trig_sharp = trig[0];
    g->trig_contrast = trig[1];
    g->trig_denoise = trig[2];
    g->trig_sensor_offset = trig[3];
    g->trig_black_level = trig[4];
    g->trig_dpc = trig[5];
    g->trig_defog = trig[6];
    g->trig_pltm_dynamic = trig[7];
    g->trig_brightness = trig[8];
    /* index 9 (global contrast) has no clean consumer, so is not mapped. */
    g->trig_saturation = trig[10];
    g->trig_cem_ratio = trig[11];
    g->trig_tdf = trig[12];
    g->trig_color_denoise = trig[13];
    g->trig_ae_cfg = trig[14];
    g->trig_gtm_cfg = trig[15];
    g->trig_lca_cfg = trig[16];

    g->dynamic_enable = dyn->enable;
    g->tdnf_comp_target = dyn->temporal_denoise_comp_target;
    g->tdnf_diff_comp_target = dyn->temporal_denoise_diff_comp_target;
    g->lp_th_ratio_comp_target = dyn->lp_threshold_ratio_comp_target;
    g->sharp_hfrq_comp_target = dyn->sharp_high_frequency_comp_target;
    g->sharp_edge_comp_target = dyn->sharp_edge_comp_target;
    g->sharp_under_shoot_comp_target = dyn->sharp_under_shoot_comp_target;
    for (i = 0; i < 4; i++) {
        g->comp.tdnf_comp[i] = dyn->temporal_denoise_comp[i];
        g->comp.lp_th_ratio_comp[i] = dyn->lp_threshold_ratio_comp[i];
        g->comp.sharp_hfrq_comp[i] = dyn->sharp_high_frequency_comp[i];
        g->comp.sharp_edge_comp[i] = dyn->sharp_edge_comp[i];
        g->comp.sharp_under_shoot_comp[i] = dyn->sharp_under_shoot_comp[i];
    }

    g->pltm_strength = (int32_t)c->pltm_entity.pltm_result.pltm_next_strength;

    g->wb_r_gain = c->awb_entity.awb_result.wb_gain_output.r_gain;
    g->wb_gb_gain = c->awb_entity.awb_result.wb_gain_output.gb_gain;
    g->wb_b_gain = c->awb_entity.awb_result.wb_gain_output.b_gain;

    for (r = 0; r < 3; r++) {
        for (col = 0; col < 3; col++) {
            g->color_matrix.m[r][col] =
                c->hw_cfg.rgb2rgb_cfg.colour_matrix.matrix[r][col];
            g->rgb2yuv.m[r][col] = c->hw_cfg.rgb2yuv.matrix[r][col];
        }
        g->color_matrix.off[r] =
            c->hw_cfg.rgb2rgb_cfg.colour_matrix.offset[r];
        g->rgb2yuv.off[r] = c->hw_cfg.rgb2yuv.offset[r];
    }

    for (i = 0; i < 3072; i++)
        g->gamma_table[i] = (int32_t)c->hw_cfg.gamma_cfg.gamma_tbl[i];

    /* Persisted shared state the clean core reads and updates across frames. */
    g->module_enable_flag = (int32_t)c->hw_cfg.module_enable_flag;
    g->table_update = c->hw_cfg.table_update;
}

/* ------------------------------------------------------------------ */
/* Result writeback: clean result/ctx -> SDK module_cfg + friends      */
/* ------------------------------------------------------------------ */
/* Each SDK block is written only when its *_adjust gate is set, as the vendor
 * does; clean-owned shared state comes from the clean context. */
static void map_result_to_sdk(struct fwi_isp_ctx *c, const iso_ctx_t *g,
                              const clean_iso_result_t *r,
                              const fwi_iso_param_t *p)
{
    fwi_hw_module_cfg_t *m = &c->hw_cfg;
    int i;

    if (p->chroma_denoise_adjust) {
        m->chroma_denoise_cfg.c_threshold = r->c_threshold;
        m->chroma_denoise_cfg.y_threshold = r->y_threshold;
        m->chroma_denoise_cfg.st_v_yth = r->st_v_yth;
        m->chroma_denoise_cfg.st_h_yth = r->st_h_yth;
    }

    if (p->sharpness_adjust) {
        m->sharp_cfg.edge_scale_ratio = r->edge_scale_ratio;
        m->sharp_cfg.high_frequency_scale_ratio = r->hfrq_scale_ratio;
        m->sharp_cfg.edge_conv_para = r->edge_conv_para;
        m->sharp_cfg.high_frequency_conv_para = r->hfrq_conv_para;
        m->sharp_cfg.dir_eq_ratio = r->dir_eq_ratio;
        m->sharp_cfg.dir_clip_val = r->dir_clip_val;
        m->sharp_cfg.ns_lw_threshold = r->ns_lw_th;
        m->sharp_cfg.ns_hi_threshold = r->ns_hi_th;
        m->sharp_cfg.edge_threshold = r->edge_th;
        m->sharp_cfg.hv_edge_smoothing_ratio = r->hv_edge_sm_ratio;
        m->sharp_cfg.aa_edge_smoothing_ratio = r->aa_edge_sm_ratio;
        m->sharp_cfg.edge_white_strength = (uint16_t)r->edge_white_stren;
        m->sharp_cfg.edge_black_strength = (uint16_t)r->edge_black_stren;
        m->sharp_cfg.high_frequency_white_strength = (uint16_t)r->hfrq_white_stren;
        m->sharp_cfg.high_frequency_black_strength = (uint16_t)r->hfrq_black_stren;
        m->sharp_cfg.under_area_ctrl = r->under_area_ctrl;
        m->sharp_cfg.over_area_ctrl = r->over_area_ctrl;
        m->sharp_cfg.under_val_ctrl = r->under_val_ctrl;
        m->sharp_cfg.over_val_ctrl = r->over_val_ctrl;

        memcpy(m->sharp_cfg.sharp_edge_luminance, r->sharp_edge_lum,
               sizeof(r->sharp_edge_lum));
        memcpy(m->sharp_cfg.sharp_high_frequency_luminance, r->sharp_hfrq_lum,
               sizeof(r->sharp_hfrq_lum));
        memcpy(m->sharp_cfg.sharp_hsv, r->sharp_hsv, sizeof(r->sharp_hsv));
        memcpy(m->sharp_cfg.sharp_s_map, r->sharp_s_map,
               sizeof(r->sharp_s_map));
        for (i = 0; i < 33; i++) {
            m->sharp_cfg.sharp_val[i] = r->sharp_val[i];
            m->sharp_cfg.sharp_luminance[i] = r->sharp_val[33 + i];
        }

        c->picture_ctl.sharpness_level = g->tune.sharpness_level;
    }

    if (p->saturation_adjust) {
        m->saturation_cfg.saturation_r = r->satu_r;
        m->saturation_cfg.saturation_g = r->satu_g;
        m->saturation_cfg.saturation_b = r->satu_b;
        m->mode_cfg.saturation_mode = (unsigned int)r->saturation_mode;
        for (i = 0; i < 256; i++)
            m->saturation_cfg.saturation_table[i] = r->sat_curve[i];
        m->table_update = (uint32_t)g->table_update;
    }

    if (p->denoise_adjust) {
        m->bayer_denoise_cfg.hf_ratio = r->hf_ratio;
        m->bayer_denoise_cfg.bf_ratio = r->bf_ratio;
        m->bayer_denoise_cfg.lf_ratio = r->lf_ratio;
        m->bayer_denoise_cfg.lp0_np_side_ratio = r->lp0_np_side_ratio;
        m->bayer_denoise_cfg.lp1_np_side_ratio = r->lp1_np_side_ratio;
        m->bayer_denoise_cfg.lp2_np_side_ratio = r->lp2_np_side_ratio;
        m->bayer_denoise_cfg.lp0_np_core_ratio = r->lp0_np_core_ratio;
        m->bayer_denoise_cfg.lp1_np_core_ratio = r->lp1_np_core_ratio;
        m->bayer_denoise_cfg.lp2_np_core_ratio = r->lp2_np_core_ratio;
        m->bayer_denoise_cfg.lp3_np_core_ratio = r->lp3_np_core_ratio;
        m->bayer_denoise_cfg.lp0_pcnt_ratio = r->lp0_pcnt_ratio;
        m->bayer_denoise_cfg.lp1_pcnt_ratio = r->lp1_pcnt_ratio;
        m->bayer_denoise_cfg.lp2_pcnt_ratio = r->lp2_pcnt_ratio;
        m->bayer_denoise_cfg.lp3_pcnt_ratio = r->lp3_pcnt_ratio;
        for (i = 0; i < 33; i++) {
            m->bayer_denoise_cfg.d2d_lp0_threshold[i] = r->d2d_lp0_th[i];
            m->bayer_denoise_cfg.d2d_lp1_threshold[i] = r->d2d_lp1_th[i];
            m->bayer_denoise_cfg.d2d_lp2_threshold[i] = r->d2d_lp2_th[i];
            m->bayer_denoise_cfg.d2d_lp3_threshold[i] = r->d2d_lp3_th[i];
        }
    }

    if (p->defect_pixel_adjust) {
        m->on_the_fly_cfg.hot_ratio = r->hot_ratio;
        m->on_the_fly_cfg.cold_ratio = r->cold_ratio;
        m->on_the_fly_cfg.nbhd_diff_ratio = r->nbhd_diff_ratio;
        m->on_the_fly_cfg.nearest_diff_ratio = r->nearest_diff_ratio;
        m->on_the_fly_cfg.slope_threshold = r->slope_th;
        m->on_the_fly_cfg.cold_abs_threshold = r->cold_abs_th;
    }

    if (p->lateral_ca_cfg_adjust) {
        m->lateral_ca_cfg.lateral_ca_gf_cor_ratio = r->lca_gf_cor_ratio;
        m->lateral_ca_cfg.lateral_ca_pf_cor_ratio = r->lca_pf_cor_ratio;
        m->lateral_ca_cfg.lateral_ca_luminance_threshold = r->lca_lum_th;
        m->lateral_ca_cfg.lateral_ca_grad_threshold = r->lca_grad_th;
        m->lateral_ca_cfg.lateral_ca_clr_gth = r->lca_clr_gth;
        m->lateral_ca_cfg.lateral_ca_pf_rshf = r->lca_pf_rshf;
        m->lateral_ca_cfg.lateral_ca_pf_bslp = r->lca_pf_bslp;
        m->lateral_ca_cfg.lateral_ca_clrs_luminance_threshold = r->lca_clrs_lum_th;
        m->lateral_ca_cfg.lateral_ca_pf_clrc_ratio = r->lca_pf_clrc_ratio;
        m->lateral_ca_cfg.lateral_ca_gf_clrc_ratio = r->lca_gf_clrc_ratio;
        m->lateral_ca_cfg.lateral_ca_pf_decr_ratio = r->lca_pf_decr_ratio;
    }

    if (p->af_cfg_adjust) {
        const iso_af_out_t *a = &r->af;

        m->af_cfg.af_mode = (fwi_af_mode_e)a->mode;
        m->af_cfg.af_en_cfg.af_iir0_en = (uint8_t)a->iir0_en;
        m->af_cfg.af_en_cfg.af_fir0_en = (uint8_t)a->fir0_en;
        m->af_cfg.af_en_cfg.af_iir0_sec0_en = (uint8_t)a->iir0_sec0_en;
        m->af_cfg.af_en_cfg.af_iir0_sec1_en = (uint8_t)a->iir0_sec1_en;
        m->af_cfg.af_en_cfg.af_iir0_sec2_en = (uint8_t)a->iir0_sec2_en;
        m->af_cfg.af_en_cfg.af_iir0_ldg_en = (uint8_t)a->iir0_ldg_en;
        m->af_cfg.af_en_cfg.af_fir0_ldg_en = (uint8_t)a->fir0_ldg_en;
        m->af_cfg.af_en_cfg.af_iir_downsample_en = (uint8_t)a->iir_ds_en;
        m->af_cfg.af_en_cfg.af_fir_downsample_en = (uint8_t)a->fir_ds_en;
        m->af_cfg.af_en_cfg.af_offset_en = (uint8_t)a->offset_en;
        m->af_cfg.af_en_cfg.af_peak_en = (uint8_t)a->peak_en;
        m->af_cfg.af_en_cfg.af_squ_en = (uint8_t)a->squ_en;

        m->af_cfg.af_filter_cfg.af_iir0_g0 = (int16_t)a->iir_g[0];
        m->af_cfg.af_filter_cfg.af_iir0_g1 = (int16_t)a->iir_g[1];
        m->af_cfg.af_filter_cfg.af_iir0_g2 = (int16_t)a->iir_g[2];
        m->af_cfg.af_filter_cfg.af_iir0_g3 = (int16_t)a->iir_g[3];
        m->af_cfg.af_filter_cfg.af_iir0_g4 = (int16_t)a->iir_g[4];
        m->af_cfg.af_filter_cfg.af_iir0_g5 = (int16_t)a->iir_g[5];
        m->af_cfg.af_filter_cfg.af_iir0_s0 = (uint16_t)a->iir_s[0];
        m->af_cfg.af_filter_cfg.af_iir0_s1 = (uint16_t)a->iir_s[1];
        m->af_cfg.af_filter_cfg.af_iir0_s2 = (uint16_t)a->iir_s[2];
        m->af_cfg.af_filter_cfg.af_iir0_s3 = (uint16_t)a->iir_s[3];
        m->af_cfg.af_filter_cfg.af_fir0_g0 = (char)a->fir_g[0];
        m->af_cfg.af_filter_cfg.af_fir0_g1 = (char)a->fir_g[1];
        m->af_cfg.af_filter_cfg.af_fir0_g2 = (char)a->fir_g[2];
        m->af_cfg.af_filter_cfg.af_fir0_g3 = (char)a->fir_g[3];
        m->af_cfg.af_filter_cfg.af_fir0_g4 = (char)a->fir_g[4];
        m->af_cfg.af_filter_cfg.af_iir0_dilate = (uint8_t)a->iir0_dilate;
        m->af_cfg.af_filter_cfg.af_iir0_ldg_low_gain = (uint8_t)a->iir_ldg_lgain;
        m->af_cfg.af_filter_cfg.af_iir0_ldg_high_gain = (uint8_t)a->iir_ldg_hgain;
        m->af_cfg.af_filter_cfg.af_iir0_ldg_low_threshold = (uint8_t)a->iir_ldg_lth;
        m->af_cfg.af_filter_cfg.af_iir0_ldg_high_threshold = (uint8_t)a->iir_ldg_hth;
        m->af_cfg.af_filter_cfg.af_fir0_ldg_low_gain = (uint8_t)a->fir_ldg_lgain;
        m->af_cfg.af_filter_cfg.af_fir0_ldg_high_gain = (uint8_t)a->fir_ldg_hgain;
        m->af_cfg.af_filter_cfg.af_fir0_ldg_low_threshold = (uint8_t)a->fir_ldg_lth;
        m->af_cfg.af_filter_cfg.af_fir0_ldg_high_threshold = (uint8_t)a->fir_ldg_hth;
        m->af_cfg.af_filter_cfg.af_iir0_ldg_low_slope = (uint8_t)a->iir_ldg_lslope;
        m->af_cfg.af_filter_cfg.af_iir0_ldg_high_slope = (uint8_t)a->iir_ldg_hslope;
        m->af_cfg.af_filter_cfg.af_fir0_ldg_low_slope = (uint8_t)a->fir_ldg_lslope;
        m->af_cfg.af_filter_cfg.af_fir0_ldg_high_slope = (uint8_t)a->fir_ldg_hslope;
        m->af_cfg.af_filter_cfg.af_iir0_core_threshold = (uint8_t)a->iir_core_th;
        m->af_cfg.af_filter_cfg.af_iir0_core_peak = (uint8_t)a->iir_core_peak;
        m->af_cfg.af_filter_cfg.af_fir0_core_threshold = (uint8_t)a->fir_core_th;
        m->af_cfg.af_filter_cfg.af_fir0_core_peak = (uint8_t)a->fir_core_peak;
        m->af_cfg.af_filter_cfg.af_iir0_core_slope = (uint8_t)a->iir_core_slope;
        m->af_cfg.af_filter_cfg.af_fir0_core_slope = (uint8_t)a->fir_core_slope;
        m->af_cfg.af_filter_cfg.af_hlt_threshold = (uint8_t)a->hlt_th;
        m->af_cfg.af_filter_cfg.af_r_offset = (int16_t)a->r_offset;
        m->af_cfg.af_filter_cfg.af_g_offset = (int16_t)a->g_offset;
        m->af_cfg.af_filter_cfg.af_b_offset = (int16_t)a->b_offset;

        memcpy(m->af_cfg.af_square_lut, r->af_square_lut,
               sizeof(r->af_square_lut));
    }

    /* 6656-byte CEM image into the 5888-byte table: the vendor pass also runs
     * into the following lut_cfg member, so the whole span is copied. */
    if (p->colour_enhance_ratio_adjust)
        memcpy(m->colour_enhance_cfg.colour_enhance_table, r->out_cem, ISO_CEM_BYTES);

    if (p->sensor_offset_adjust) {
        m->gain_offset_cfg.sensor_offset.r_offset = (int16_t)g->module.sensor_offset[0];
        m->gain_offset_cfg.sensor_offset.gr_offset = (int16_t)g->module.sensor_offset[1];
        m->gain_offset_cfg.sensor_offset.gb_offset = (int16_t)g->module.sensor_offset[2];
        m->gain_offset_cfg.sensor_offset.b_offset = (int16_t)g->module.sensor_offset[3];
        c->sensor.gain_offset.r_offset = (int16_t)g->sensor.gain_offset[0];
        c->sensor.gain_offset.gr_offset = (int16_t)g->sensor.gain_offset[1];
        c->sensor.gain_offset.gb_offset = (int16_t)g->sensor.gain_offset[2];
        c->sensor.gain_offset.b_offset = (int16_t)g->sensor.gain_offset[3];
    }

    if (p->black_level_adjust) {
        m->gain_offset_cfg.offset.r_offset = (int16_t)g->module.offset[0];
        m->gain_offset_cfg.offset.gr_offset = (int16_t)g->module.offset[1];
        m->gain_offset_cfg.offset.gb_offset = (int16_t)g->module.offset[2];
        m->gain_offset_cfg.offset.b_offset = (int16_t)g->module.offset[3];
    }

    if (p->dehaze_value_adjust)
        c->adjust_ctl.dehaze_value = g->adjust.defog_value;

    if (p->pltm_dynamic_cfg_adjust) {
        for (i = 0; i < 4; i++)
            c->ae_ctl.pltm_dynamic_cfg[i] =
                (int32_t)g->ae.pltm_dynamic_cfg[i];
    }

    if (p->tdnr_adjust) {
        const iso_tdf_out_t *t = &r->tdf;

        m->denoise_3d_cfg.rec_en = (uint8_t)t->rec_en;
        m->denoise_3d_cfg.noise_clip_ratio = t->noise_clip_ratio;
        m->denoise_3d_cfg.bright_diff_ratio = t->bright_diff_ratio;
        m->denoise_3d_cfg.bright_diff_clip_ratio = t->bright_diff_clip_ratio;
        m->denoise_3d_cfg.luminance_diff_clip_ratio = t->lum_diff_clip_ratio;
        m->denoise_3d_cfg.st_2d_ratio = t->st_2d_ratio;
        m->denoise_3d_cfg.mv_ori_ratio = t->mv_ori_ratio;
        m->denoise_3d_cfg.ltf_en = t->ltf_en;
        m->denoise_3d_cfg.c_weight1 = t->c_weight1;
        m->denoise_3d_cfg.c_weight2 = t->c_weight2;
        m->denoise_3d_cfg.c_weight3 = t->c_weight3;
        m->denoise_3d_cfg.ltf_update_frame = (uint16_t)t->ltf_update_frm;

        for (i = 0; i < 32; i++)
            m->denoise_3d_cfg.temporal_denoise_k_delta[i] = t->tdnf_k_delta[i];
        for (i = 0; i < 33; i++) {
            m->denoise_3d_cfg.temporal_denoise_threshold[i] = t->tdnf_th[i];
            m->denoise_3d_cfg.temporal_denoise_ref_noise[i] = t->tdnf_th[33 + i];
        }
        for (i = 0; i < 32; i++)
            m->denoise_3d_cfg.temporal_denoise_k[i] = (uint8_t)t->tdnf_k[i];

        if (m->temporal_denoise_table) {
            uint8_t *dst = (uint8_t *)m->temporal_denoise_table;

            for (i = 0; i < 256; i++) {
                dst[i] = (uint8_t)t->tdnf_table[i];
                dst[0x100 + i] = (uint8_t)t->tdnf_table[0x100 + i];
            }
        }

        m->module_enable_flag = (uint32_t)g->module_enable_flag;
    }

    if (p->ae_cfg_adjust) {
        for (i = 0; i < 14; i++)
            c->ae_ctl.exposure_cfg[i] = (int32_t)g->ae.exposure_cfg[i];
    }

    if (p->gtm_cfg_adjust) {
        for (i = 0; i < 9; i++)
            c->ae_ctl.ae_hist_eq_cfg[i] = (int32_t)g->ae.ae_hist_eq_cfg[i];
    }

    if (p->contrast_adjust) {
        c->adjust_ctl.contrast = g->adjust.contrast;
        c->stats.dynamic_stats.enable = (g->dynamic_enable != 0);
        for (i = 0; i < 4; i++) {
            c->stats.dynamic_stats.temporal_denoise_comp[i] =
                g->comp.tdnf_comp[i];
            c->stats.dynamic_stats.lp_threshold_ratio_comp[i] =
                g->comp.lp_th_ratio_comp[i];
            c->stats.dynamic_stats.sharp_high_frequency_comp[i] =
                g->comp.sharp_hfrq_comp[i];
            c->stats.dynamic_stats.sharp_edge_comp[i] =
                g->comp.sharp_edge_comp[i];
            c->stats.dynamic_stats.sharp_under_shoot_comp[i] =
                g->comp.sharp_under_shoot_comp[i];
        }
    }

    if (p->brightness_adjust)
        c->adjust_ctl.brightness = g->adjust.brightness;
}

/* ------------------------------------------------------------------ */
/* Ops vtable                                                          */
/* ------------------------------------------------------------------ */

static int32_t shim_get(void *obj, fwi_iso_param_t **param)
{
    iso_shim_entity_t *e = (iso_shim_entity_t *)obj;

    if (!e || !param)
        return -1;
    *param = &e->config;
    return 0;
}

static int32_t shim_set(void *obj, fwi_iso_param_t *param, fwi_iso_result_t *result)
{
    iso_shim_entity_t *e = (iso_shim_entity_t *)obj;
    const struct fwi_isp_ctx *c;
    iso_params_t cp;
    clean_iso_result_t cr;

    if (!e || !param)
        return -1;

    e->config = *param;
    c = e->config.gen;

    map_params(&cp, &e->config, &e->gen, c);
    map_ctx_inputs(&e->gen, c);

    memset(&cr, 0, sizeof(cr));
    if (clean_iso_set_params(e->clean, &cp, &cr) != 0)
        return -1;

    if (result) {
        result->gain_index = (uint32_t)cr.gain_i;
        result->luminance_index = (uint32_t)cr.lum_i;
    }
    return 0;
}

/* Input capture, inert unless FREEISP_ISO_DUMP=<path>: iso_param_t + the whole
 * framework context per run(). Format: docs/shim-mappings.md#iso */
static void iso_fwrite_zeros(FILE *f, size_t n)
{
    static const unsigned char z[256];

    while (n) {
        size_t k = n > sizeof(z) ? sizeof(z) : n;
        fwrite(z, 1, k, f);
        n -= k;
    }
}

static void iso_dump_record(const iso_shim_entity_t *e)
{
    static FILE *dump;
    static int checked;
    const struct fwi_isp_ctx *c = e->config.gen;

    if (!checked) {
        const char *path = getenv("FREEISP_ISO_DUMP");

        checked = 1;
        if (path) {
            if (path[0] == '\0' || (path[0] == '1' && path[1] == '\0'))
                path = "/tmp/freeisp_iso_dump.bin";
            dump = fopen(path, "wb");
        }
    }
    if (!dump)
        return;

    fwrite(&e->config, sizeof(e->config), 1, dump);
    if (c)
        fwrite(c, 1, sizeof(struct fwi_isp_ctx), dump);
    else
        iso_fwrite_zeros(dump, sizeof(struct fwi_isp_ctx));
    fflush(dump);
}

/* Saturation probe, inert unless FREEISP_ISO_TRACE=<path> ("1" = stdout): gate,
 * picked dynamic record, clean result and module_cfg values per run(). */
static void iso_trace_saturation(const iso_shim_entity_t *e,
                                 const clean_iso_result_t *r)
{
    static FILE *trace;
    static int checked;
    int32_t pick[7] = { 0, 0, 0, 0, 0, 0, 0 };
    iso_params_t *cp = NULL;
    const struct fwi_isp_ctx *c = e->config.gen;
    const struct fwi_hw_module_cfg *m = c ? &c->hw_cfg : NULL;

    if (!checked) {
        const char *path = getenv("FREEISP_ISO_TRACE");

        checked = 1;
        if (path) {
            if (path[0] == '\0' || (path[0] == '1' && path[1] == '\0'))
                trace = stdout;
            else
                trace = fopen(path, "w");
        }
    }
    if (!trace)
        return;

    iso_debug_sat_pick(e->clean, pick);
    if (clean_iso_get_params(e->clean, &cp) != 0)
        cp = NULL;

    fprintf(trace,
            "iso_trace frame=%d sat_adjust=%u sat_on=%u "
            "sat_cfg=%d,%d,%d,%d,%d,%d,%d "
            "res=%d,%d,%d mode=%d "
            "module_cfg=%d,%d,%d mode=%u\n",
            e->config.iso_frame_id, (unsigned)e->config.saturation_adjust,
            cp ? (unsigned)cp->sat_on : 0u,
            pick[0], pick[1], pick[2], pick[3], pick[4], pick[5], pick[6],
            r->satu_r, r->satu_g, r->satu_b, r->saturation_mode,
            m ? (int)m->saturation_cfg.saturation_r : -1,
            m ? (int)m->saturation_cfg.saturation_g : -1,
            m ? (int)m->saturation_cfg.saturation_b : -1,
            m ? (unsigned)m->mode_cfg.saturation_mode : 0u);
}

static int32_t shim_run(void *obj, fwi_iso_result_t *result)
{
    iso_shim_entity_t *e = (iso_shim_entity_t *)obj;
    clean_iso_result_t cr;
    iso_params_t live;

    if (!e || !result)
        return -1;

    iso_dump_record(e);

    /* The framework mutates isp_gen in place between calls; refresh the
     * scalar inputs while leaving clean-owned output state untouched. */
    map_ctx_inputs(&e->gen, e->config.gen);

    /* set_params runs before the framework raises the adjust gates; forward the
     * live mirror or every gated block is skipped (green image). */
    map_live_params(&live, &e->config);
    iso_set_live_params(e->clean, &live);

    memset(&cr, 0, sizeof(cr));
    if (clean_iso_run(e->clean, &cr) != 0)
        return -1;

    if (e->config.gen)
        map_result_to_sdk(e->config.gen, &e->gen, &cr, &e->config);

    iso_trace_saturation(e, &cr);

    result->gain_index = (uint32_t)cr.gain_i;
    result->luminance_index = (uint32_t)cr.lum_i;
    return 0;
}

/* ------------------------------------------------------------------ */
/* SDK entry points                                                    */
/* ------------------------------------------------------------------ */

static fwi_iso_cfg_core_ops_t g_ops;

void *iso_init(fwi_iso_cfg_core_ops_t **ops)
{
    iso_shim_entity_t *e;
    iso_ops_t clean_ops;

    ensure_defaults();

    e = (iso_shim_entity_t *)calloc(1, sizeof(*e));
    if (!e)
        return NULL;

    memset(&clean_ops, 0, sizeof(clean_ops));
    e->clean = clean_iso_init(&clean_ops);
    if (!e->clean) {
        free(e);
        return NULL;
    }

    g_ops.iso_set_params = shim_set;
    g_ops.iso_get_params = shim_get;
    g_ops.iso_run = shim_run;

    if (ops)
        *ops = &g_ops;

    return e;
}

void iso_exit(void *obj)
{
    iso_shim_entity_t *e = (iso_shim_entity_t *)obj;

    if (!e)
        return;
    if (e->clean)
        clean_iso_exit(e->clean);
    free(e);
}
