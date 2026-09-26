/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * base.h - clean-room ISP base tier (software table/window/curve generation
 * and DMA statistics parsing).
 *
 * Behaviour-only implementation of spec/reglayer2.md section 3, plus the interfaces.md entry-point/table-injection contract.  Table data
 * is injected at runtime through the table-provider contract
 * (freeisp_get_tables()); see docs/provenance.md for the full provenance
 * record.
 *
 * The struct tag names and layouts below (`isp_lib_context`,
 * `isp_module_config`, `isp_h3a_reg_win`, ...) are reproduced as
 * **interoperability facts**: they must match the SDK ABI header they mirror,
 * `libisp/include/isp_manage.h` (module payload types also in
 * `isp_module_cfg.h`).  The decomposition and behaviour are our own.
 */
#ifndef BASE_CLEAN_H
#define BASE_CLEAN_H

#include <stdint.h>
#include <stddef.h>

#include "reg_writers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Geometry (spec 2, 5)                                                */
/* ------------------------------------------------------------------ */

#define ISP_AE_WIN_H        16
#define ISP_AE_WIN_W        24
#define ISP_AE_WIN_N        (ISP_AE_WIN_H * ISP_AE_WIN_W)   /* 384 */
#define ISP_AWB_WIN_H       32
#define ISP_AWB_WIN_W       32
#define ISP_AWB_WIN_N       (ISP_AWB_WIN_H * ISP_AWB_WIN_W) /* 1024 */
#define ISP_AF_WIN_H        16
#define ISP_AF_WIN_W        24
#define ISP_AF_WIN_N        (ISP_AF_WIN_H * ISP_AF_WIN_W)   /* 384 */
#define ISP_AFS_SUM_N       128
#define ISP_PLTM_WIN_H      24
#define ISP_PLTM_WIN_W      32
#define ISP_PLTM_WIN_N      768   /* the vendor PLTM list is 768 u16 */
#define ISP_HIST_BIN_N      256

#define ISP_GAMMA_TBL_LEN   3072        /* 3 x 1024                       */
#define ISP_GAMMA_TRIG_N    5
#define ISP_MSC_TEMP_NUM    6
#define ISP_LENS_TBL_N      256
#define ISP_MSC_TBL_WORDS   (3 * ISP_LENS_TBL_N)
#define ISP_LSC_TBL_WORDS   (3 * ISP_LENS_TBL_N)     /* 768              */
#define ISP_LSC_TBL_SIZE    1024                     /* lens output words */
#define ISP_MSC_TBL_GROUPS  484
#define ISP_MSC_TBL_LENGTH  (3 * ISP_MSC_TBL_GROUPS) /* 1452             */
#define ISP_MSC_TBL_SIZE    1936                     /* msc output words  */
#define ISP_LINEAR_TBL_N    768         /* 0x600 bytes                    */
#define ISP_WDR_TBL_WORDS   8192        /* WDR_MEM_SIZE 0x4000 bytes      */
#define ISP_WDR_FE_WORDS    4096        /* front-end table 0x2000 bytes   */
#define ISP_ANTI_GAMMA_N    4096
#define ISP_MATRIX_N        9
#define ISP_RGB2YUV_SPACES  6
#define ISP_STAT_HIST_N     256
#define ISP_DYN_MOV_SAVE    7
#define ISP_DYN_MOV_TH      6
#define ISP_STATUS_JUDGE_MAX 4

/* DMA sub-buffer offsets from the aggregate statistics buffer (spec 2.1). */
#define ISP_DMA_HIST_OFF    0x40
#define ISP_DMA_AE_OFF      0x240
#define ISP_DMA_AF_OFF      0x4a40
#define ISP_DMA_AFS_OFF     0x8640
#define ISP_DMA_AWB_OFF     0x8840
#define ISP_DMA_PLTM_OFF    0xdc40
#define ISP_DMA_AWB_CNT_OFF 0x4800

/* AE_HIST / AE histogram output-bit shift (spec 2.1). */
#define ISP_AE_OUT_BITS     16

/* Tuning-settings change flags (spec 5, e3a_settings_flags). */
enum isp_3a_change_bits {
    ISP_3A_SCENE        = 1u << 0,
    ISP_3A_AWB_MODE     = 1u << 1,
    ISP_3A_FLICKER      = 1u << 2,
    ISP_3A_SHARPNESS    = 1u << 3,
    ISP_3A_BRIGHTNESS   = 1u << 4,
    ISP_3A_SATURATION   = 1u << 5,
    ISP_3A_EFFECT       = 1u << 6,
    ISP_3A_AF_METERING  = 1u << 7,
    ISP_3A_AE_METERING  = 1u << 8,
    ISP_3A_CONTRAST     = 1u << 9,
    ISP_3A_HUE          = 1u << 10,
    ISP_3A_GAIN_STR     = 1u << 11
};

#define ISP_3A_CHANGE_MASK 0x0fffu

#define ISP_AE_MODE_NORM    0

/* ------------------------------------------------------------------ */
/* H3A window descriptor (spec 5)                                      */
/* ------------------------------------------------------------------ */

typedef struct isp_h3a_reg_win {
    uint8_t  hor_num;
    uint8_t  ver_num;
    uint32_t width;
    uint32_t height;
    uint32_t hor_start;
    uint32_t ver_start;
} isp_h3a_reg_win_t;

/* ------------------------------------------------------------------ */
/* Parsed statistics (spec 2.1 / 2.2 / "Stats block behaviour")        */
/* ------------------------------------------------------------------ */

typedef struct isp_ae_stats {
    uint32_t win_r[ISP_AE_WIN_N];
    uint32_t win_g[ISP_AE_WIN_N];
    uint32_t win_b[ISP_AE_WIN_N];
    uint16_t luma[ISP_AE_WIN_N];
    uint32_t hist[ISP_HIST_BIN_N];
    uint32_t win_pix_n;
} isp_ae_stats_t;

typedef struct isp_awb_stats {
    uint32_t sum_r[ISP_AWB_WIN_N];
    uint32_t sum_g[ISP_AWB_WIN_N];
    uint32_t sum_b[ISP_AWB_WIN_N];
    uint32_t count[ISP_AWB_WIN_N];
    uint32_t avg_r[ISP_AWB_WIN_N];
    uint32_t avg_g[ISP_AWB_WIN_N];
    uint32_t avg_b[ISP_AWB_WIN_N];
    uint32_t avg[ISP_AWB_WIN_N];
} isp_awb_stats_t;

typedef struct isp_af_stats {
    uint64_t iir[ISP_AF_WIN_N];
    uint64_t fir[ISP_AF_WIN_N];
    uint64_t iir_cnt[ISP_AF_WIN_N];
    uint64_t fir_cnt[ISP_AF_WIN_N];
    uint64_t hlt_cnt[ISP_AF_WIN_N];
    uint64_t count[ISP_AF_WIN_N];
    uint64_t h_d1[ISP_AF_WIN_N];
    uint64_t h_d2[ISP_AF_WIN_N];
    uint64_t v_d1[ISP_AF_WIN_N];
    uint64_t v_d2[ISP_AF_WIN_N];
} isp_af_stats_t;

typedef struct isp_afs_stats {
    uint32_t sum[ISP_AFS_SUM_N];
    uint32_t pic_w;
    uint32_t pic_h;
} isp_afs_stats_t;

typedef struct isp_pltm_stats {
    uint16_t lst[ISP_PLTM_WIN_N];
    uint32_t avg_before;
    uint32_t min_before;
    uint32_t max_before;
    uint32_t avg_after;
    uint32_t min_after;
    uint32_t max_after;
} isp_pltm_stats_t;

typedef struct isp_stats {
    isp_ae_stats_t   ae;
    isp_awb_stats_t  awb;
    isp_af_stats_t   af;
    isp_afs_stats_t  afs;
    isp_pltm_stats_t pltm;
} isp_stats_t;

/* Motion/dynamic-judge state (spec 2.3). */
typedef struct isp_dynamic_stats {
    int      enable;
    uint32_t accum[ISP_AE_WIN_N];
    uint32_t accum_last1[ISP_AE_WIN_N];
    uint32_t accum_last2[ISP_AE_WIN_N];
    uint32_t accum_last3[ISP_AE_WIN_N];

    uint32_t mov_save[ISP_DYN_MOV_SAVE];
    uint32_t mov_th[ISP_DYN_MOV_TH];
    uint32_t mov_save_cnt;

    int32_t  tdnf_comp[ISP_STATUS_JUDGE_MAX];
    int32_t  tdnf_diff_comp[ISP_STATUS_JUDGE_MAX];
    int32_t  lp_th_ratio_comp[ISP_STATUS_JUDGE_MAX];
    int32_t  sharp_hfrq_comp[ISP_STATUS_JUDGE_MAX];
    int32_t  sharp_edge_comp[ISP_STATUS_JUDGE_MAX];
    int32_t  sharp_under_shoot_comp[ISP_STATUS_JUDGE_MAX];

    int32_t  tdnf_comp_target;
    int32_t  tdnf_diff_comp_target;
    int32_t  lp_th_ratio_comp_target;
    int32_t  sharp_hfrq_comp_target;
    int32_t  sharp_edge_comp_target;
    int32_t  sharp_under_shoot_comp_target;

    uint32_t mov;
    uint32_t mov_old;
} isp_dynamic_stats_t;

typedef struct isp_stats_ctx {
    isp_stats_t         stats;
    isp_dynamic_stats_t dynamic_stats;
    uint32_t            pic_w;
    uint32_t            pic_h;
} isp_stats_ctx_t;

/* ------------------------------------------------------------------ */
/* Module payload structs (spec 3.4 shapes used by this tier)          */
/* ------------------------------------------------------------------ */

typedef struct isp_rgb2yuv {
    int16_t  gain[3][3];
    int16_t  offset[3];
} isp_rgb2yuv_t;

typedef struct isp_rgb2rgb_cfg {
    uint16_t color_matrix[3][3];
    uint16_t color_offset[3];
} isp_rgb2rgb_cfg_t;

typedef struct isp_gain_offset_cfg {
    uint16_t gain[4];          /* r, gr, gb, b          */
    uint16_t sensor_offset[4]; /* r, gr, gb, b          */
    uint16_t offset[4];        /* blc offsets           */
} isp_gain_offset_cfg_t;

typedef struct isp_wdr_cfg {
    uint16_t lo_th;
    uint16_t hi_th;
    uint16_t exp_ratio;
    uint16_t slope;
    uint16_t mv_th;
    uint16_t mv_scale;
    uint16_t out_sel;
    uint16_t table[ISP_WDR_TBL_WORDS];   /* WDR_MEM_SIZE 0x4000 bytes */
    uint16_t fe_table[ISP_WDR_FE_WORDS]; /* front-end 0x2000 bytes    */
} isp_wdr_cfg_t;

typedef struct isp_gamma_cfg {
    uint16_t gamma_tbl[ISP_GAMMA_TBL_LEN];
} isp_gamma_cfg_t;

typedef struct isp_lens_cfg {
    uint16_t ct_x;
    uint16_t ct_y;
    uint16_t rs_val;
} isp_lens_cfg_t;

typedef struct isp_disc_cfg {
    uint16_t disc_ct_x;
    uint16_t disc_ct_y;
    uint16_t disc_rs_val;
} isp_disc_cfg_t;

typedef struct isp_awb_cfg {
    isp_h3a_reg_win_t awb_reg_win;
    uint8_t lim_r;
    uint8_t lim_g;
    uint8_t lim_b;
} isp_awb_cfg_t;

typedef struct isp_af_cfg {
    isp_h3a_reg_win_t af_reg_win;
} isp_af_cfg_t;

typedef struct isp_ae_cfg {
    isp_h3a_reg_win_t ae_reg_win;
} isp_ae_cfg_t;

typedef struct isp_hist_cfg {
    isp_h3a_reg_win_t hist_reg_win;
} isp_hist_cfg_t;

typedef struct isp_afs_cfg {
    uint32_t inc_line;
} isp_afs_cfg_t;

typedef struct isp_mode_cfg {
    uint32_t wdr_mode;
    uint32_t ae_mode;
    uint32_t dg_mode;
    uint32_t input_fmt;
} isp_mode_cfg_t;

typedef struct clean_isp_module_config {
    uint32_t module_enable_flag;
    uint32_t table_update;

    isp_mode_cfg_t        mode_cfg;
    isp_awb_cfg_t         awb_cfg;
    isp_af_cfg_t          af_cfg;
    isp_ae_cfg_t          ae_cfg;
    isp_hist_cfg_t        hist_cfg;
    isp_afs_cfg_t         afs_cfg;
    isp_rgb2yuv_t         rgb2yuv;
    isp_rgb2rgb_cfg_t     rgb2rgb_cfg;
    isp_gain_offset_cfg_t gain_offset_cfg;
    isp_wdr_cfg_t         wdr_cfg;
    isp_gamma_cfg_t       gamma_cfg;
    isp_lens_cfg_t        lens_cfg;
    isp_disc_cfg_t        disc_cfg;

    uint16_t linear_table[ISP_LINEAR_TBL_N];
    uint16_t lens_table[ISP_LSC_TBL_SIZE];
    uint16_t msc_table[ISP_MSC_TBL_SIZE];
} isp_module_config_t;

/* ------------------------------------------------------------------ */
/* Tuning / settings / 3A context (spec 2)                             */
/* ------------------------------------------------------------------ */

typedef struct isp_gains {
    uint16_t bayer_gain[4];       /* r, gr, gb, b          */
    int32_t  gain_favour;
    int32_t  analog_gain_min;
    int32_t  analog_gain_max;
    int32_t  digital_gain_min;
    int32_t  digital_gain_max;
} isp_gains_t;

typedef struct ae_ini {
    int32_t gain_favour;
    int32_t analog_gain_min;
    int32_t analog_gain_max;
    int32_t digital_gain_min;
    int32_t digital_gain_max;
} ae_ini_t;

typedef struct isp_ini_cfg {
    isp_gains_t gains;
    int32_t     hue_level;            /* signed */
    int32_t     color_effect;         /* 0 none, others per spec 2.4 */
    int32_t     colour_space;         /* sensor colour-space selector */
    int32_t     gamma_trig_cfg[ISP_GAMMA_TRIG_N];
    uint16_t    lsc_trig_cfg[ISP_MSC_TEMP_NUM];
    uint16_t    msc_trig_cfg[ISP_MSC_TEMP_NUM];
    int32_t     cm_en;                /* colour-matrix enable */
    int32_t     cm_break_num;
    int32_t     cm_trig[ISP_MSC_TEMP_NUM];
    int32_t     ae_stat_sel;          /* AE stats gamma-correction select */
} isp_ini_cfg_t;

typedef struct isp_ae_settings {
    int32_t  flicker_mode;
    int32_t  ae_mode;                 /* 0 normal, 1 WDR */
    int32_t  wdr_output_select;       /* WDR output selector (spec 2.7) */
    int32_t  colour_space;
    int32_t  exposure_cfg[16];
    int32_t  awb_coor[4];             /* x0, y0, x1, y1 normalised (-1000..1000) */
    int32_t  ae_spot_coor[4];
    int32_t  hist_coor[4];
    int32_t  lsc_center_x;
    int32_t  lsc_center_y;
    int32_t  ae_gain;
    int32_t  exp_line;
    int32_t  flash_open;
} isp_ae_settings_t;

typedef struct isp_ae_param {
    ae_ini_t ae_ini;
    int      nor_cmd_mode;
    uint32_t frame_cnt;
    int32_t  comanding_input_bits;    /* WDR table input precision   */
    int32_t  comanding_output_bits;   /* WDR table output precision  */
    isp_ae_settings_t ae_setting;     /* settings snapshot (spot AE) */
} isp_ae_param_t;

/* The framework keeps the AE parameter block behind a context handle; the
 * settings-update dispatcher hands this handle to the AE helper. */
typedef struct isp_ae_entity_ctx {
    isp_ae_param_t *ae_param;
} isp_ae_entity_ctx_t;

typedef struct isp_afs_param {
    int32_t flicker_mode;
} isp_afs_param_t;

typedef struct isp_af_param {
    int32_t mov;
} isp_af_param_t;

typedef struct isp_ae_result {
    int32_t  wdr_hi_th;
    int32_t  wdr_low_th;
    int32_t  wdr_exp_ratio;
    int32_t  wdr_slope;
    int32_t  wdr_out_sel;
    int32_t  wdr_ratio_sensor;        /* ae_wdr_ratio (spec 2.7)     */
    int32_t  wdr_ratio_tmp;
    int32_t  wdr_ratio_isp_hw;
    int32_t  ae_gain;                 /* used by the defog path      */
} isp_ae_result_t;

typedef struct isp_af_result {
    int32_t temperature;
} isp_af_result_t;

typedef struct isp_sensor_info {
    int32_t  ae_lv;
    int32_t  colour_space;
    int32_t  temperature;
    uint16_t gain_offset[4];
    uint32_t so_en;
    uint32_t blc_en;
    int32_t  so_offset[4];
    int32_t  blc_offset[4];
} isp_sensor_info_t;

typedef struct isp_adjust {
    int32_t defog_value;
} isp_adjust_t;

typedef struct isp_stat {
    uint16_t min_rgb_saved;
} isp_stat_t;

typedef struct isp_defog_ctx {
    uint16_t min_rgb_pre[3];
    int32_t  defog_pre;
    int32_t  defog_changed;
    int32_t  offset;
} isp_defog_ctx_t;

typedef struct isp_test_settings {
    uint32_t ae_en;
    uint32_t awb_en;
    uint32_t af_en;
    uint32_t afs_en;
    uint32_t pltm_en;
    uint32_t defog_en;
    uint32_t lsc_en;
    uint32_t msc_en;
    uint32_t gamma_en;
    uint32_t wdr_en;
    uint32_t dig_gain_en;
    uint32_t linear_en;
    uint32_t cm_en;
    uint32_t satur_en;
    uint32_t wb_en;
    int32_t  isp_test_mode;
    int32_t  isp_color_temp;
    uint32_t isp_test_focus;          /* AF stats read gate (spec 2.9) */
} isp_test_settings_t;

typedef struct isp_stats_attach {
    void *awb_stats;
    void *ae_stats;
    void *af_stats;
    void *afs_stats;
    void *pltm_stats;
    void *md_stats;
    void *gtm_stats;
    void *rolloff_stats;
} isp_stats_attach_t;

/* The framework's register allocation lives outside this tier; the context
 * carries the instance id so the validated writers can be driven. */
struct clean_isp_lib_context {
    unsigned long isp_dev_id;

    uint32_t isp_3a_change_flags;

    isp_module_config_t module_cfg;
    isp_stats_ctx_t     stats_ctx;
    isp_stats_attach_t  stats_attach;

    isp_ini_cfg_t       isp_ini_cfg;
    isp_test_settings_t isp_test_settings;
    isp_ae_settings_t   ae_settings;
    isp_ae_param_t      ae_param;
    isp_ae_entity_ctx_t ae_entity_ctx;
    isp_afs_param_t     afs_param;
    isp_af_param_t      af_param;
    isp_ae_result_t     ae_result;
    isp_af_result_t     af_result;
    int32_t             awb_color_temp_output;   /* AWB result colour temp */
    isp_sensor_info_t   sensor_info;
    isp_adjust_t        adjust;
    isp_stat_t          stat;
    isp_defog_ctx_t     defog_ctx;

    uint16_t anti_gamma_tbl[ISP_ANTI_GAMMA_N];

    /* Saved white-balance gains used to normalise AWB statistics (spec 2.1). */
    uint16_t wb_gain[4];             /* r, gr, gb, b */

    /* AWB result output white-balance gains (the per-frame AWB result). */
    uint16_t awb_gain_output[4];     /* r, gr, gb, b */

    /* Hardware tuning selectors consumed by the table builders. */
    int32_t ff_mod;                  /* lens-table builder select  */
    int32_t lsc_mode;
    int32_t mff_mod;                 /* MSC-table builder select   */
    int32_t msc_mode;
    int32_t rolloff_ratio;           /* ff_mod==1 gain band        */
    int32_t ev_analog_gain;          /* ff_mod==1 analog gain      */

    /* MSC golden-ratio correction fields (populated by isp_manage from OTP
     * data in the deployed runtime; injected here through the scenario). */
    float   msc_golden_ratio[ISP_MSC_TBL_LENGTH];
    float   msc_r_ratio;
    int32_t msc_golden_flag[ISP_MSC_TBL_LENGTH];
    float   msc_adjust_ratio[ISP_MSC_TEMP_NUM];
    float   msc_adjust_ratio_less[ISP_MSC_TEMP_NUM];

    /* Metering selectors and normalised coordinates (spec 2.4). */
    int32_t ae_spot;
    int32_t af_spot;
    int32_t ae_coor[4];              /* x0, y0, x1, y1 normalised  */
    int32_t af_coor[4];

    uint32_t ae_frame_cnt;
    uint32_t af_frame_cnt;
    uint32_t awb_frame_cnt;

    /* ISO entity inputs consumed by the WDR threshold selection (spec 2.7). */
    int32_t  iso_cem_color2gray_th;
    int32_t  iso_lum_idx;
};

typedef struct clean_isp_lib_context isp_lib_context_t;

/* ------------------------------------------------------------------ */
/* Injected tables (interfaces.md table contract)                      */
/* ------------------------------------------------------------------ */

typedef struct base_tables {
    const uint16_t *gamma_base;            /* ISP_GAMMA_TBL_LEN           */
    const uint16_t *gamma_sub[ISP_GAMMA_TRIG_N - 1]; /* 4 sub-tables      */
    const int32_t  *gamma_trig;            /* ISP_GAMMA_TRIG_N            */
    const uint16_t *lsc[2 * ISP_MSC_TEMP_NUM]; /* ISP_LSC_TBL_WORDS each   */
    const uint16_t *lsc_trig;              /* ISP_MSC_TEMP_NUM (tuning)   */
    const uint16_t *lsc_trig_def;          /* ISP_MSC_TEMP_NUM, injected:  */
                                           /* vendor lsc_trig_cfg_def      */
    const uint16_t *msc[2 * ISP_MSC_TEMP_NUM]; /* ISP_MSC_TBL_LENGTH each  */
    const uint16_t *msc_trig;              /* ISP_MSC_TEMP_NUM (tuning)   */
    const uint16_t *msc_trig_def;          /* ISP_MSC_TEMP_NUM, injected:  */
                                           /* vendor msc_trig_cfg_def      */
    const uint16_t *linear;                /* ISP_LINEAR_TBL_N            */
    const uint16_t *wdr_table;             /* ISP_WDR_TBL_WORDS           */
    const uint16_t *wdr_front;             /* ISP_WDR_FE_WORDS            */
    const uint16_t *anti_gamma;            /* ISP_ANTI_GAMMA_N            */
    const int16_t  *rgb2yuv_base[ISP_RGB2YUV_SPACES]; /* 9 matrix + 3 offset */
    const uint16_t *color_matrix;          /* cm_break_num * (9+3) entries */
    const int32_t  *color_temp;            /* ISP_MSC_TEMP_NUM            */
    const uint16_t *otp_msc_golden;        /* ISP_MSC_TBL_LENGTH (1452);
                                            * injected OTP MSC golden
                                            * reference; NULL disables the
                                            * OTP MSC path */
    const uint32_t *default_reg;           /* ISP_LOAD_REG_SIZE/4 (1024)
                                            * injected ISP reset image;
                                            * NULL == seed zeros */
} base_tables_t;

#ifndef FREEISP_TABLES_T_DEFINED
#define FREEISP_TABLES_T_DEFINED
typedef struct freeisp_tables {
    const base_tables_t *base;
} freeisp_tables_t;
#endif

/* Provided by the framework; must be non-NULL before any entry point. */
const freeisp_tables_t *freeisp_get_tables(void);

/* Radial-distance reference over a 16x16 window, used as a per-window
 * correction factor by the MSC table builders.  The deployment keeps this as a
 * 256-entry double read-only table; here the same geometry is re-derived (see
 * src/reg/comp_ref.c). */
void freeisp_comp_ref_fill(double out[256]);

/* Provided by the integration shim (base_shim.c): applies AE parameters for a
 * cmd by handing the entity handle to the framework AE core.  cmd_type:
 * 1 = refresh the AE table (ISP_AE_UPDATE_AE_TABLE), 3 = rebuild the touch
 * weighting (ISP_AE_BUILD_TOUCH_WEIGHT).
 *
 * The framework helper `isp_ae_set_params_helper` dereferences the SDK
 * `isp_ae_entity_context` (ae_param at +0, ops at +0x1c8, ae_entity at +0x1cc)
 * and dispatches through `ops->isp_ae_set_params`.  The clean context models
 * only `ae_param`, so this tier cannot call that helper directly; the SDK-side
 * shim implements this hook, translates the parameter update and owns the
 * dispatch. */
void freeisp_ae_set_params(isp_ae_entity_ctx_t *ae_ctx, int32_t cmd_type);

/* ------------------------------------------------------------------ */
/* Entry points (interfaces.md / reglayer2.md section 3)               */
/* ------------------------------------------------------------------ */

void isp_handle_stats(isp_lib_context_t *ctx, const void *buffer);
void isp_handle_stats_sync(isp_lib_context_t *ctx, const void *buf0,
                           const void *buf1);
void isp_apply_settings(isp_lib_context_t *ctx);
void __isp_stat_dynamic_judge(isp_lib_context_t *ctx);
void isp_apply_colormatrix(isp_lib_context_t *ctx);
void config_band_step(isp_lib_context_t *ctx);
void config_dig_gain(isp_lib_context_t *ctx, int32_t exp_digital_gain);
void config_gamma(isp_lib_context_t *ctx);
void config_lens_center(isp_lib_context_t *ctx);
void config_lens_table(isp_lib_context_t *ctx, int32_t vcm_std_pos);
void config_msc_table(isp_lib_context_t *ctx, int32_t vcm_std_pos);
void config_wdr(isp_lib_context_t *ctx, int32_t flag);

/* Diagnostics for tests: the module-cfg payloads are the observable output
 * of most entry points, so no extra getters are required. */

#ifdef __cplusplus
}
#endif

#endif /* BASE_CLEAN_H */
