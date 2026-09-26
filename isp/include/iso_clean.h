/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * iso_clean.h - clean-room ISO gain/luminance switchboard.
 *
 * Behaviour-only reimplementation of the module specified in
 * spec/iso.md.  The gain index table, the two default breakpoint arrays and
 * the AF square seed arrive at runtime through the table-provider contract
 * (freeisp_get_tables()); see docs/provenance.md for the full provenance
 * record.
 *
 * Own names, own decomposition.
 */
#ifndef ISO_CLEAN_H
#define ISO_CLEAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Shapes (spec 5.1, 3.3)                                             */
/* ------------------------------------------------------------------ */

#define ISO_DYN_WORDS     148   /* words per dynamic record             */
#define ISO_CURVE_N       350   /* entries in by_gain / by_lum          */
#define ISO_BP_N           14   /* configured breakpoints per axis      */
#define ISO_CEM_TABLE_N  5888   /* blended bytes of the chroma table    */
#define ISO_CEM_BYTES    6656   /* full chroma table byte span          */
#define ISO_SEARCH_LO      24   /* 0x18: first gain-table index         */
#define ISO_SEARCH_HI     349   /* 0x15d: sentinel / out-of-range index */

/* Per-block trigger selector (spec 2). */
#define ISO_TRIG_LUM    0
#define ISO_TRIG_GAIN   1

/* set-parameters operation selector (spec 3.1). */
#define ISO_PARAM_REBUILD 0

/* ------------------------------------------------------------------ */
/* Dynamic record (spec 5.1)                                          */
/* ------------------------------------------------------------------ */

typedef union iso_dyn {
    int32_t word[ISO_DYN_WORDS];      /* whole-record copy / interp    */
    struct iso_dyn_fields {
        int32_t sharp_cfg[29];        /* 0..28                         */
        int32_t contrast_cfg[11];     /* 29..39                        */
        int32_t denoise_cfg[20];      /* 40..59                        */
        int32_t sensor_offset[4];     /* 60..63                        */
        int32_t black_level[4];       /* 64..67                        */
        int32_t dpc_cfg[6];           /* 68..73                        */
        int32_t pltm_dynamic_cfg[4];  /* 74..77                        */
        int32_t defog_value;          /* 78                            */
        int32_t brightness;           /* 79                            */
        int32_t contrast;             /* 80                            */
        int32_t sat_cb;               /* 81                            */
        int32_t sat_cr;               /* 82                            */
        int32_t sat_cfg[7];           /* 83..89                        */
        int32_t cem_ratio;            /* 90                            */
        int32_t tdf_cfg[22];          /* 91..112                       */
        int32_t color_denoise;        /* 113                           */
        int32_t ae_cfg[14];           /* 114..127                      */
        int32_t gtm_cfg[9];           /* 128..136                      */
        int32_t lca_cfg[11];          /* 137..147                      */
    } f;
} iso_dyn_t;

/* ------------------------------------------------------------------ */
/* Shared ISP context (spec 3.2 inputs + 6 outputs, behind `gen`)     */
/* ------------------------------------------------------------------ */

typedef struct iso_matrix {
    int32_t m[3][3];
    int32_t off[3];
} iso_matrix_t;

typedef struct iso_ctx {
    /* --- 3A / sensor inputs --- */
    int32_t total_gain;
    int32_t ae_pos;
    int32_t ae_pos_max;
    int32_t ae_mode;                 /* 0 normal, 1 WDR               */

    /* --- feature gates --- */
    int32_t tdf_enable;
    int32_t contrast_enable;
    int32_t wb_enable;
    int32_t sat_enable;
    int32_t pltm_enable;

    /* --- tuning strength knobs --- */
    int32_t contrast_level;
    int32_t sat_level;
    int32_t sharp_level;
    int32_t brightness_level;
    int32_t denoise_level;
    int32_t pltm_level;
    int32_t tdf_level;
    int32_t highlight_level;
    int32_t backlight_level;

    /* --- per-block record selectors (0 = lum, else gain) --- */
    int32_t trig_sharp;
    int32_t trig_contrast;
    int32_t trig_denoise;
    int32_t trig_sensor_offset;
    int32_t trig_black_level;
    int32_t trig_dpc;
    int32_t trig_defog;
    int32_t trig_pltm_dynamic;
    int32_t trig_brightness;
    int32_t trig_saturation;
    int32_t trig_cem_ratio;
    int32_t trig_tdf;
    int32_t trig_color_denoise;
    int32_t trig_ae_cfg;
    int32_t trig_gtm_cfg;            /* present but unused (spec 6.12) */
    int32_t trig_lca_cfg;

    /* --- tuning curves --- */
    int32_t  k3d_incre_curve[256];
    int32_t  tdnf_diff[256];
    uint16_t sharp_edge_lum[33];
    uint16_t sharp_hfrq_lum[33];
    uint16_t sharp_hsv[46];
    uint8_t  sharp_s_map[33];
    int32_t  sharp_val[66];
    int32_t  bdnf_th[33];
    int32_t  tdnf_th[66];
    int32_t  tdnf_k[32];
    uint8_t  cem_table_a[ISO_CEM_TABLE_N];
    uint8_t  cem_table_b[ISO_CEM_TABLE_N];

    /* --- dynamic-judge state / targets --- */
    int32_t dynamic_enable;
    int32_t tdnf_comp_target;
    int32_t tdnf_diff_comp_target;
    int32_t lp_th_ratio_comp_target;
    int32_t sharp_hfrq_comp_target;
    int32_t sharp_edge_comp_target;
    int32_t sharp_under_shoot_comp_target;
    struct {
        int32_t tdnf_comp[4];
        int32_t lp_th_ratio_comp[4];
        int32_t sharp_hfrq_comp[4];
        int32_t sharp_edge_comp[4];
        int32_t sharp_under_shoot_comp[4];
    } comp;

    /* --- misc shared inputs --- */
    int32_t      pltm_strength;      /* Q12                           */
    int32_t      wb_r_gain;
    int32_t      wb_gb_gain;
    int32_t      wb_b_gain;
    iso_matrix_t color_matrix;
    int32_t      gamma_table[3072];
    iso_matrix_t rgb2yuv;

    /* --- shared outputs / persisted state --- */
    int32_t  module_enable_flag;
    uint32_t table_update;
    struct {
        int16_t sensor_offset[4];
        int16_t offset[4];           /* r, gr, gb, b                  */
    } module;
    struct {
        int16_t gain_offset[4];
    } sensor;
    struct {
        int32_t contrast;
        int32_t brightness;
        int32_t defog_value;
    } adjust;
    struct {
        int32_t exposure_cfg[14];
        int32_t pltm_dynamic_cfg[4];
        int32_t ae_hist_eq_cfg[9];
    } ae;
    struct {
        int32_t sharpness_level;
    } tune;
} iso_ctx_t;

/* ------------------------------------------------------------------ */
/* Per-instance configuration (spec 3.1)                              */
/* ------------------------------------------------------------------ */

typedef struct iso_params {
    int32_t  param_kind;             /* 0 = rebuild arrays            */
    int32_t  frame_id;
    uint16_t chroma_gray_th;
    uint16_t chroma_gray_span_min;
    uint16_t chroma_gray_span_max;

    uint32_t cnr_on;
    uint32_t sharp_on;
    uint32_t sat_on;
    uint32_t contrast_on;
    uint32_t brightness_on;
    uint32_t cem_on;
    uint32_t denoise_on;
    uint32_t sensor_offset_on;
    uint32_t black_level_on;
    uint32_t dpc_on;
    uint32_t defog_on;
    uint32_t pltm_dyn_on;
    uint32_t tdnr_on;
    uint32_t ae_cfg_on;
    uint32_t gtm_cfg_on;
    uint32_t lca_cfg_on;
    uint32_t af_cfg_on;

    uint32_t dn_core_ratio[4];

    iso_ctx_t *gen;                  /* shared ISP context            */

    /* configured tuning data for the two interpolation arrays */
    iso_dyn_t cfg[ISO_BP_N];
    int32_t   gain_point[ISO_BP_N];
    int32_t   lum_point[ISO_BP_N];
} iso_params_t;

/* ------------------------------------------------------------------ */
/* Per-frame result block (spec 4 plus the out.* fields of spec 6)    */
/* ------------------------------------------------------------------ */

typedef struct iso_tdf_out {
    int32_t  rec_en;
    uint16_t noise_clip_ratio;
    uint8_t  lum_diff_clip_ratio, bright_diff_clip_ratio;
    uint8_t  bright_diff_ratio, mv_ori_ratio, st_2d_ratio;
    uint16_t c_weight1, c_weight2, c_weight3;
    int32_t  ltf_update_frm;
    uint8_t  ltf_en;
    uint8_t  tdnf_k_delta[32];
    uint16_t tdnf_th[66];
    uint16_t tdnf_k[32];
    uint16_t tdnf_table[512];
} iso_tdf_out_t;

typedef struct iso_af_out {
    int32_t r_offset, g_offset, b_offset, mode;
    int32_t iir0_en, fir0_en, iir0_sec0_en, iir0_sec1_en, iir0_sec2_en;
    int32_t iir0_ldg_en, fir0_ldg_en, offset_en, peak_en;
    int32_t iir_ds_en, fir_ds_en, squ_en;
    int32_t iir_g[6];
    int32_t iir_s[4];
    int32_t fir_g[5];
    int32_t iir0_dilate;
    int32_t iir_ldg_lgain, iir_ldg_hgain, iir_ldg_lth, iir_ldg_hth;
    int32_t iir_ldg_lslope, iir_ldg_hslope;
    int32_t iir_core_th, iir_core_peak, iir_core_slope;
    int32_t fir_ldg_lgain, fir_ldg_hgain, fir_ldg_lth, fir_ldg_hth;
    int32_t fir_ldg_lslope, fir_ldg_hslope;
    int32_t fir_core_th, fir_core_peak, fir_core_slope;
    int32_t hlt_th;
} iso_af_out_t;

typedef struct iso_result {
    int32_t lum_i;
    int32_t gain_i;

    /* CNR / colour denoise */
    uint16_t c_threshold;
    uint16_t y_threshold;
    uint16_t st_v_yth;
    uint16_t st_h_yth;

    /* saturation */
    int16_t  satu_r, satu_g, satu_b;
    int32_t  saturation_mode;
    int16_t  sat_curve[256];

    /* 2D denoise */
    uint8_t  hf_ratio, bf_ratio, lf_ratio;
    uint8_t  lp0_np_side_ratio, lp1_np_side_ratio, lp2_np_side_ratio;
    uint8_t  lp0_np_core_ratio, lp1_np_core_ratio;
    uint8_t  lp2_np_core_ratio, lp3_np_core_ratio;
    uint8_t  lp0_pcnt_ratio, lp1_pcnt_ratio, lp2_pcnt_ratio, lp3_pcnt_ratio;
    uint16_t d2d_lp0_th[33];
    uint16_t d2d_lp1_th[33];
    uint16_t d2d_lp2_th[33];
    uint16_t d2d_lp3_th[33];

    /* DPC */
    uint8_t  hot_ratio, cold_ratio, nbhd_diff_ratio, nearest_diff_ratio;
    uint16_t slope_th, cold_abs_th;

    /* LCA */
    uint16_t lca_gf_cor_ratio, lca_pf_cor_ratio, lca_lum_th, lca_grad_th;
    uint16_t lca_clr_gth, lca_pf_rshf, lca_pf_bslp;
    uint8_t  lca_clrs_lum_th, lca_pf_clrc_ratio;
    uint8_t  lca_gf_clrc_ratio, lca_pf_decr_ratio;

    /* sharpness */
    uint8_t  edge_scale_ratio, hfrq_scale_ratio;
    uint8_t  edge_conv_para, hfrq_conv_para;
    uint16_t dir_eq_ratio, dir_clip_val, ns_lw_th, ns_hi_th;
    uint8_t  edge_th, hv_edge_sm_ratio, aa_edge_sm_ratio;
    int32_t  edge_white_stren, edge_black_stren;
    int32_t  hfrq_white_stren, hfrq_black_stren;
    uint16_t under_area_ctrl, over_area_ctrl, under_val_ctrl, over_val_ctrl;
    uint16_t sharp_edge_lum[33];
    uint16_t sharp_hfrq_lum[33];
    uint16_t sharp_hsv[46];
    uint8_t  sharp_s_map[33];
    uint16_t sharp_val[66];

    /* AF */
    iso_af_out_t af;
    uint8_t      af_square_lut[16];

    /* temporal denoise */
    iso_tdf_out_t tdf;

    /* CEM chroma table */
    uint8_t out_cem[ISO_CEM_BYTES];
} iso_result_t;

/* ------------------------------------------------------------------ */
/* Injected tables (spec 3.3) - supplied at runtime, never built in   */
/* ------------------------------------------------------------------ */

typedef struct iso_clean_tables {
    const uint32_t *gain_index_table;   /* 350, monotone non-decreasing */
    const int32_t  *gain_point_default; /* 14                            */
    const int32_t  *lum_point_default;  /* 14                            */
    const uint8_t  *af_square_table;    /* 16-byte seed                  */
    const int32_t  *af_iir_s;           /* 4, AF IIR feedback coefs      */
} iso_clean_tables_t;

typedef struct freeisp_tables {
    const iso_clean_tables_t *iso;
} freeisp_tables_t;

/* Provided by the framework; must be non-NULL before iso_init(). */
const freeisp_tables_t *freeisp_get_tables(void);

/* ------------------------------------------------------------------ */
/* Entry points (interfaces.md)                                       */
/* ------------------------------------------------------------------ */

typedef struct iso_entity iso_entity_t;

typedef struct iso_ops {
    int (*get_params)(iso_entity_t *e, iso_params_t **out);
    int (*set_params)(iso_entity_t *e, const iso_params_t *in,
                      iso_result_t *result);
    int (*run)(iso_entity_t *e, iso_result_t *result);
} iso_ops_t;

iso_entity_t *iso_init(iso_ops_t *out_ops);
void          iso_exit(iso_entity_t *e);
int           iso_get_params(iso_entity_t *e, iso_params_t **out);
int           iso_set_params(iso_entity_t *e, const iso_params_t *in,
                             iso_result_t *result);
int           iso_run(iso_entity_t *e, iso_result_t *result);

/*
 * Override the frame counter the temporal-denoise block measures its warm-up
 * window against.  The framework mutates the frame id in its stored parameter
 * block in place every frame without re-entering set-parameters, so the shim
 * forwards the live value here before each run.
 */
void          iso_set_frame_id(iso_entity_t *e, int32_t frame_id);

/*
 * Refresh the per-frame inputs the framework rewrites in its stored parameter
 * block in place without re-entering set-parameters: the per-block enable
 * gates, the 2D-denoise core ratios, the frame counter and the colour-to-gray
 * thresholds.  The interpolation arrays built by set-parameters are left
 * untouched (the framework rebuilds them only when it re-enters
 * set-parameters).  The shim calls this before each run so the core reads the
 * live mirror exactly as the deployed core does; without it the core keeps
 * whatever gates were latched at set-parameters time and skips every gated
 * block (spec 11).
 */
void          iso_set_live_params(iso_entity_t *e, const iso_params_t *in);

/* Diagnostics for tests: expose the built interpolation arrays. */
int32_t iso_debug_gain_word(const iso_entity_t *e, int index, int word);
int32_t iso_debug_lum_word(const iso_entity_t *e, int index, int word);

/*
 * Diagnostics: copy the saturation block's selected dynamic-record words
 * (sat_cfg[0..6]) for the current saturation trigger into out[0..6].  Lets a
 * boundary probe compare the picked record with the value written back.
 */
void    iso_debug_sat_pick(const iso_entity_t *e, int32_t out[7]);

#ifdef __cplusplus
}
#endif

#endif /* ISO_CLEAN_H */
