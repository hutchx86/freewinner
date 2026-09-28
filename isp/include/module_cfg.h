/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* module_cfg.h - per-frame ISP module dispatch and payload builder (isp/spec/reglayer2.md
 * section 2): 31 prepare/enable routine pairs walked by isp_hardware_update(). Own
 * record layouts (fwi_mod_config, ...); shim/module_cfg translates the SDK records. */
#ifndef MODULE_CFG_H
#define MODULE_CFG_H

#include <stdint.h>

#include "reg_writers.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ISP_MODULE_COUNT      31u

#define ISP_TABLE_UPDATE_ALL  0xffffffffu
#define ISP_TABLE_LOAD_MASK   0xffffu

/* Hardware table-load bits OR'd into fwi_mod_config.table_update (distinct from
 * ISP_FEAT_*); values match the vendor golden output. */
enum isp_table_update_bits {
    ISP_TABLE_UPDATE_LINEAR  = 0x002u,
    ISP_TABLE_UPDATE_LSC     = 0x004u,
    ISP_TABLE_UPDATE_GAMMA   = 0x008u,
    ISP_TABLE_UPDATE_RGB_DRC = 0x010u,
    ISP_TABLE_UPDATE_SATU    = 0x020u,
    ISP_TABLE_UPDATE_WDR     = 0x040u,
    ISP_TABLE_UPDATE_D3D     = 0x080u,
    ISP_TABLE_UPDATE_PLTM    = 0x100u,
    ISP_TABLE_UPDATE_CEM     = 0x200u,
    ISP_TABLE_UPDATE_MSC     = 0x400u
};

#define ISP_GAMMA_PLANE       1024u
#define ISP_GAMMA_TBL_LENGTH  (3u * ISP_GAMMA_PLANE)

#define ISP_CEM_TBL_SIZE      0x1700u
#define ISP_DRC_TBL_SIZE      0x200u
#define ISP_LINEAR_TBL_SIZE   0x600u
#define ISP_PLTM_TBL_SIZE     0x600u
#define ISP_WDR_TBL_SIZE      0x4000u
#define ISP_LENS_TBL_WORDS    256u
#define ISP_LENS_TBL_SIZE     (3u * ISP_LENS_TBL_WORDS)
#define ISP_SATU_TBL_SIZE     0x200u
#define ISP_SATU_SUM_TARGET   0x10u

typedef enum isp_module_enable {
    ISP_MODULE_DISABLE = 0,
    ISP_MODULE_ENABLE  = 1
} isp_module_enable_t;

typedef struct fwi_mod_h3a_reg_win {
    uint8_t  hor_num;
    uint8_t  ver_num;
    uint32_t width;
    uint32_t height;
    uint32_t hor_start;
    uint32_t ver_start;
} fwi_mod_h3a_reg_win_t;

typedef struct fwi_mod_lsc_config {
    uint16_t ct_x;
    uint16_t ct_y;
    uint16_t rs_val;
} fwi_mod_lsc_config_t;

typedef struct fwi_mod_mode_cfg {
    uint32_t input_fmt;
    uint32_t dg_mode;
    uint32_t cfa_mode;
    uint32_t otf_dpc_mode;
    uint32_t hist_sel;
    uint32_t awb_mode;
    uint32_t ae_mode;
    uint32_t wdr_cmp_mode;
    uint32_t rsc_mode;
    uint32_t msc_mode;
    uint32_t d3d_mode;
} fwi_mod_mode_cfg_t;

typedef struct fwi_mod_gain_offset_cfg {
    uint32_t gain[4];
    uint32_t offset[4];
    uint32_t sensor_offset[4];
} fwi_mod_gain_offset_cfg_t;

typedef struct fwi_mod_wb_gain_cfg {
    uint32_t clip_val;
    uint32_t wb_gain[4];
} fwi_mod_wb_gain_cfg_t;

typedef struct fwi_mod_otf_cfg {
    uint32_t ratio[4];
    uint32_t slope_th;
    uint32_t cold_abs_th;
} fwi_mod_otf_cfg_t;

typedef struct fwi_mod_cfa_cfg {
    uint32_t dir_th;
    uint32_t interp_mode;
    uint32_t zig_zag;
} fwi_mod_cfa_cfg_t;

typedef struct fwi_mod_cnr_cfg {
    uint32_t c_th;
    uint32_t y_th;
    uint32_t st_v_y;
    uint32_t st_h_y;
} fwi_mod_cnr_cfg_t;

typedef struct fwi_mod_rgb2rgb_cfg {
    uint16_t color_matrix[9];
    uint16_t offset[3];
} fwi_mod_rgb2rgb_cfg_t;

typedef struct fwi_mod_rgb2yuv_cfg {
    uint16_t gain[9];
    uint16_t offset[3];
} fwi_mod_rgb2yuv_cfg_t;

typedef struct fwi_mod_satu_cfg {
    uint32_t satu_r;
    uint32_t satu_g;
    uint32_t satu_b;
    uint32_t mode;
    uint8_t  table[ISP_SATU_TBL_SIZE];
} fwi_mod_satu_cfg_t;

typedef struct fwi_mod_afs_cfg {
    uint32_t inc_line;
} fwi_mod_afs_cfg_t;

typedef struct fwi_mod_ae_cfg {
    fwi_mod_h3a_reg_win_t win;
} fwi_mod_ae_cfg_t;

typedef struct fwi_mod_awb_cfg {
    uint32_t sat_r;
    uint32_t sat_g;
    uint32_t sat_b;
    fwi_mod_h3a_reg_win_t win;
} fwi_mod_awb_cfg_t;

typedef struct fwi_mod_hist_cfg {
    uint32_t mode;
    fwi_mod_h3a_reg_win_t win;
} fwi_mod_hist_cfg_t;

typedef struct fwi_mod_af_cfg {
    uint32_t en_bits;
    uint32_t mode;
    fwi_mod_h3a_reg_win_t win;
    fwi_reg_af_filter_t filter;
    uint8_t square_lut[ISP_AF_SQUARE_WORDS * 4u];
} fwi_mod_af_cfg_t;

typedef struct fwi_mod_lens_cfg {
    fwi_mod_lsc_config_t lsc_cfg;
} fwi_mod_lens_cfg_t;

typedef struct fwi_mod_disc_cfg {
    uint16_t disc_ct_x;
    uint16_t disc_ct_y;
    uint16_t disc_rs_val;
} fwi_mod_disc_cfg_t;

typedef struct fwi_mod_gamma_cfg {
    uint16_t gamma_tbl[ISP_GAMMA_TBL_LENGTH];
    uint32_t gamma_packed[ISP_GAMMA_PLANE];
} fwi_mod_gamma_cfg_t;

typedef struct fwi_mod_msc_cfg {
    uint16_t blw[ISP_MSC_LUT_WORDS];
    uint16_t blh[ISP_MSC_LUT_WORDS];
    uint16_t blw_dlt[ISP_MSC_LUT_WORDS];
    uint16_t blh_dlt[ISP_MSC_LUT_WORDS];
} fwi_mod_msc_cfg_t;

typedef struct fwi_mod_wdr_cfg {
    uint32_t lo_th;
    uint32_t hi_th;
    uint32_t exp_ratio;
    uint32_t slope;
    uint32_t mv_th;
    uint32_t mv_scale;
    uint32_t out_sel;
    uint8_t  wdr_table[ISP_WDR_TBL_SIZE];
} fwi_mod_wdr_cfg_t;

typedef struct fwi_mod_config {
    unsigned long isp_dev_id;
    uint32_t      module_enable_flag;
    uint32_t      table_update;

    fwi_mod_mode_cfg_t        mode_cfg;
    fwi_mod_gain_offset_cfg_t gain_offset_cfg;
    fwi_mod_wb_gain_cfg_t     wb_gain_cfg;
    fwi_mod_otf_cfg_t         otf_cfg;
    fwi_mod_cfa_cfg_t         cfa_cfg;
    fwi_mod_cnr_cfg_t         cnr_cfg;
    fwi_reg_gca_cfg_t         gca_cfg;
    fwi_reg_lca_cfg_t         lca_cfg;
    fwi_reg_ctc_cfg_t         ctc_cfg;
    fwi_reg_pltm_cfg_t        pltm_cfg;
    fwi_reg_sharp_cfg_t       sharp_cfg;
    fwi_reg_d2d_cfg_t         bdnf_cfg;
    fwi_reg_d3d_cfg_t         tdf_cfg;
    fwi_mod_rgb2rgb_cfg_t     rgb2rgb_cfg;
    fwi_mod_rgb2yuv_cfg_t     rgb2yuv;
    fwi_mod_satu_cfg_t        satu_cfg;
    fwi_mod_afs_cfg_t         afs_cfg;
    fwi_mod_ae_cfg_t          ae_cfg;
    fwi_mod_awb_cfg_t         awb_cfg;
    fwi_mod_hist_cfg_t        hist_cfg;
    fwi_mod_af_cfg_t          af_cfg;
    fwi_mod_lens_cfg_t        lens_cfg;
    fwi_mod_disc_cfg_t        disc_cfg;
    fwi_mod_gamma_cfg_t       gamma_cfg;
    fwi_mod_msc_cfg_t         msc_cfg;
    fwi_mod_wdr_cfg_t         wdr_cfg;

    uint8_t  cem_src[ISP_CEM_TBL_SIZE];
    uint8_t  cem_table[ISP_CEM_TBL_SIZE];
    void    *cem_dst;
    uint8_t  drc_src[ISP_DRC_TBL_SIZE];
    uint8_t  drc_table[ISP_DRC_TBL_SIZE];
    void    *drc_dst;
    uint8_t  linear_src[ISP_LINEAR_TBL_SIZE];
    uint8_t  fe_table[ISP_LINEAR_TBL_SIZE];
    uint8_t  pltm_src[ISP_PLTM_TBL_SIZE];
    uint8_t  pltm_table[ISP_PLTM_TBL_SIZE];
    void    *pltm_dst;
    uint8_t  wdr_src[ISP_WDR_TBL_SIZE];
    void    *wdr_dst;
    void    *gamma_dst;
    uint16_t lens_src[ISP_LENS_TBL_SIZE];
    uint16_t lens_table[ISP_LENS_TBL_SIZE];
    void    *lens_dst;

    /* Presence mirrors of the SDK destination pointers: NULL means absent, so the
     * prepare routine skips the table copy and its table_update bit. */
    const uint16_t *msc_table;
    void           *satu_src;

    uint8_t  sharp_val_lut[ISP_LUT_TH_BYTES];
    uint8_t  sharp_edge_lum_lut[ISP_LUT_TH_BYTES];
    uint8_t  sharp_hfrq_lum_lut[ISP_LUT_TH_BYTES];
    uint8_t  sharp_hsv_lut[ISP_LUT_TH_BYTES];
    uint8_t  sharp_s_map_lut[ISP_LUT_SHARP_SMAP_BYTES];

    uint8_t  d2d_lp_lut[4][ISP_LUT_TH_BYTES];

    uint8_t  d3d_tdnf_th[ISP_LUT_TH_BYTES];
    uint8_t  d3d_lum_th_lut[ISP_LUT_TH_BYTES];
    uint8_t  d3d_bright_th_lut[ISP_LUT_TH_BYTES];
    uint8_t  d3d_ref_noise_lut[ISP_LUT_TH_BYTES];
    uint8_t  d3d_k_lut[ISP_D3D_K_VALUES];
    uint8_t  d3d_k_delta_lut[ISP_D3D_K_VALUES];

    uint8_t  lca_pf_satu_lut[ISP_LCA_SATU_BYTES];
    uint8_t  lca_gf_satu_lut[ISP_LCA_SATU_BYTES];
} fwi_mod_config_t;

typedef struct fwi_mod_attribute {
    uint32_t feature_bit;
    char     feature_name[32];
    void   (*config)(fwi_mod_config_t *);
    void   (*enable)(fwi_mod_config_t *, isp_module_enable_t);
} fwi_mod_attribute_t;

extern const fwi_mod_attribute_t isp_module_attrs[ISP_MODULE_COUNT];

/* Instance setup and per-frame dispatch. */
void isp_map_addr(fwi_mod_config_t *cfg, void *vaddr);
void isp_hardware_update(fwi_mod_config_t *cfg);

/* Per-module prepare routines (table order). */
void isp_reg_prepare_afs(fwi_mod_config_t *cfg);
void isp_reg_prepare_sharpness(fwi_mod_config_t *cfg);
void isp_reg_prepare_contrast(fwi_mod_config_t *cfg);
void isp_reg_prepare_d2d(fwi_mod_config_t *cfg);
void isp_reg_prepare_rgb_drc(fwi_mod_config_t *cfg);
void isp_reg_prepare_pltm(fwi_mod_config_t *cfg);
void isp_reg_prepare_wdr(fwi_mod_config_t *cfg);
void isp_reg_prepare_cem(fwi_mod_config_t *cfg);
void isp_reg_prepare_lens(fwi_mod_config_t *cfg);
void isp_reg_prepare_gamma(fwi_mod_config_t *cfg);
void isp_reg_prepare_rgb2yuv(fwi_mod_config_t *cfg);
void isp_reg_prepare_rgb2rgb(fwi_mod_config_t *cfg);
void isp_reg_prepare_ae_win(fwi_mod_config_t *cfg);
void isp_reg_prepare_af(fwi_mod_config_t *cfg);
void isp_reg_prepare_awb(fwi_mod_config_t *cfg);
void isp_reg_prepare_hist(fwi_mod_config_t *cfg);
void isp_reg_prepare_blc(fwi_mod_config_t *cfg);
void isp_reg_prepare_wb_gain(fwi_mod_config_t *cfg);
void isp_reg_prepare_dpc(fwi_mod_config_t *cfg);
void isp_reg_prepare_cfa(fwi_mod_config_t *cfg);
void isp_reg_prepare_d3d(fwi_mod_config_t *cfg);
void isp_reg_prepare_cnr(fwi_mod_config_t *cfg);
void isp_reg_prepare_saturation(fwi_mod_config_t *cfg);
void isp_reg_prepare_linear(fwi_mod_config_t *cfg);
void isp_reg_prepare_sensor_offset(fwi_mod_config_t *cfg);
void isp_reg_prepare_digital_gain(fwi_mod_config_t *cfg);
void isp_reg_prepare_ctc(fwi_mod_config_t *cfg);
void isp_reg_prepare_mode(fwi_mod_config_t *cfg);
void isp_reg_prepare_msc(fwi_mod_config_t *cfg);
void isp_reg_prepare_lca(fwi_mod_config_t *cfg);
void isp_reg_prepare_gca(fwi_mod_config_t *cfg);

/* Per-module enable routines.  CFA and MODE have none. */
void isp_reg_enable_afs(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_sharpness(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_contrast(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_d2d(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_rgb_drc(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_pltm(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_wdr(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_cem(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_lens(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_gamma(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_rgb2yuv(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_rgb2rgb(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_ae(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_af(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_awb(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_hist(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_blc(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_wb_gain(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_dpc(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_d3d(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_cnr(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_saturation(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_linear(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_sensor_offset(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_digital_gain(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_ctc(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_msc(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_lca(fwi_mod_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_gca(fwi_mod_config_t *cfg, isp_module_enable_t en);

#ifdef __cplusplus
}
#endif

#endif /* MODULE_CFG_H */
