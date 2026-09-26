/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * module_cfg.h - clean-room per-frame ISP module dispatch and payload builder.
 *
 * Behaviour is defined by spec/reglayer2.md section 2.  The 31 ISP
 * modules each own a prepare routine (isp_reg_prepare_*) that fills that module's
 * slice of struct isp_module_config and drives the matching writers from
 * reg_writers.h, and an enable routine (isp_reg_enable_*) that toggles the module's
 * hardware control bit.  isp_hardware_update() walks the fixed attribute table
 * in table order; isp_map_addr() binds the instance register base.
 *
 * The struct tag names and layouts below (`isp_module_config`, `isp_mode_cfg`,
 * `isp_h3a_reg_win`, ...) are reproduced as **interoperability facts**, not as
 * our own invention: they must match the SDK ABI header they mirror,
 * `libisp/include/isp_module_cfg.h` (the aggregate context lives in
 * `isp_manage.h`).  The decomposition and behaviour are our own.
 */
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

/* Per-table update bits the prepare routines OR into
 * isp_module_config.table_update (spec/reglayer2.md section 2).  These are the
 * hardware table-load mask bits and are distinct from the ISP_FEAT_* dispatch
 * bits.  LINEAR is 0x2; the rest were fixed against the vendor golden. */
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

typedef struct isp_h3a_reg_win {
    uint8_t  hor_num;
    uint8_t  ver_num;
    uint32_t width;
    uint32_t height;
    uint32_t hor_start;
    uint32_t ver_start;
} isp_h3a_reg_win_t;

typedef struct isp_lsc_config {
    uint16_t ct_x;
    uint16_t ct_y;
    uint16_t rs_val;
} isp_lsc_config_t;

typedef struct isp_mode_cfg {
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
} isp_mode_cfg_t;

typedef struct isp_gain_offset_cfg {
    uint32_t gain[4];
    uint32_t offset[4];
    uint32_t sensor_offset[4];
} isp_gain_offset_cfg_t;

typedef struct isp_wb_gain_cfg {
    uint32_t clip_val;
    uint32_t wb_gain[4];
} isp_wb_gain_cfg_t;

typedef struct isp_otf_cfg {
    uint32_t ratio[4];
    uint32_t slope_th;
    uint32_t cold_abs_th;
} isp_otf_cfg_t;

typedef struct isp_cfa_cfg {
    uint32_t dir_th;
    uint32_t interp_mode;
    uint32_t zig_zag;
} isp_cfa_cfg_t;

typedef struct isp_cnr_cfg {
    uint32_t c_th;
    uint32_t y_th;
    uint32_t st_v_y;
    uint32_t st_h_y;
} isp_cnr_cfg_t;

typedef struct isp_rgb2rgb_cfg {
    uint16_t color_matrix[9];
    uint16_t offset[3];
} isp_rgb2rgb_cfg_t;

typedef struct isp_rgb2yuv_cfg {
    uint16_t gain[9];
    uint16_t offset[3];
} isp_rgb2yuv_cfg_t;

typedef struct isp_satu_cfg {
    uint32_t satu_r;
    uint32_t satu_g;
    uint32_t satu_b;
    uint32_t mode;
    uint8_t  table[ISP_SATU_TBL_SIZE];
} isp_satu_cfg_t;

typedef struct isp_afs_cfg {
    uint32_t inc_line;
} isp_afs_cfg_t;

typedef struct isp_ae_cfg {
    isp_h3a_reg_win_t win;
} isp_ae_cfg_t;

typedef struct isp_awb_cfg {
    uint32_t sat_r;
    uint32_t sat_g;
    uint32_t sat_b;
    isp_h3a_reg_win_t win;
} isp_awb_cfg_t;

typedef struct isp_hist_cfg {
    uint32_t mode;
    isp_h3a_reg_win_t win;
} isp_hist_cfg_t;

typedef struct isp_af_cfg {
    uint32_t en_bits;
    uint32_t mode;
    isp_h3a_reg_win_t win;
    isp_af_filter_t filter;
    uint8_t square_lut[ISP_AF_SQUARE_WORDS * 4u];
} isp_af_cfg_t;

typedef struct isp_lens_cfg {
    isp_lsc_config_t lsc_cfg;
} isp_lens_cfg_t;

typedef struct isp_disc_cfg {
    uint16_t disc_ct_x;
    uint16_t disc_ct_y;
    uint16_t disc_rs_val;
} isp_disc_cfg_t;

typedef struct isp_gamma_cfg {
    uint16_t gamma_tbl[ISP_GAMMA_TBL_LENGTH];
    uint32_t gamma_packed[ISP_GAMMA_PLANE];
} isp_gamma_cfg_t;

typedef struct isp_msc_cfg {
    uint16_t blw[ISP_MSC_LUT_WORDS];
    uint16_t blh[ISP_MSC_LUT_WORDS];
    uint16_t blw_dlt[ISP_MSC_LUT_WORDS];
    uint16_t blh_dlt[ISP_MSC_LUT_WORDS];
} isp_msc_cfg_t;

typedef struct isp_wdr_cfg {
    uint32_t lo_th;
    uint32_t hi_th;
    uint32_t exp_ratio;
    uint32_t slope;
    uint32_t mv_th;
    uint32_t mv_scale;
    uint32_t out_sel;
    uint8_t  wdr_table[ISP_WDR_TBL_SIZE];
} isp_wdr_cfg_t;

typedef struct isp_module_config {
    unsigned long isp_dev_id;
    uint32_t      module_enable_flag;
    uint32_t      table_update;

    isp_mode_cfg_t        mode_cfg;
    isp_gain_offset_cfg_t gain_offset_cfg;
    isp_wb_gain_cfg_t     wb_gain_cfg;
    isp_otf_cfg_t         otf_cfg;
    isp_cfa_cfg_t         cfa_cfg;
    isp_cnr_cfg_t         cnr_cfg;
    isp_gca_cfg_t         gca_cfg;
    isp_lca_cfg_t         lca_cfg;
    isp_ctc_cfg_t         ctc_cfg;
    isp_pltm_cfg_t        pltm_cfg;
    isp_sharp_cfg_t       sharp_cfg;
    isp_d2d_cfg_t         bdnf_cfg;
    isp_d3d_cfg_t         tdf_cfg;
    isp_rgb2rgb_cfg_t     rgb2rgb_cfg;
    isp_rgb2yuv_cfg_t     rgb2yuv;
    isp_satu_cfg_t        satu_cfg;
    isp_afs_cfg_t         afs_cfg;
    isp_ae_cfg_t          ae_cfg;
    isp_awb_cfg_t         awb_cfg;
    isp_hist_cfg_t        hist_cfg;
    isp_af_cfg_t          af_cfg;
    isp_lens_cfg_t        lens_cfg;
    isp_disc_cfg_t        disc_cfg;
    isp_gamma_cfg_t       gamma_cfg;
    isp_msc_cfg_t         msc_cfg;
    isp_wdr_cfg_t         wdr_cfg;

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

    /* Destination table pointers.  The clean model embeds each destination
     * table as an array, so presence is carried by these SDK-pointer mirrors:
     * NULL means the SDK destination pointer was absent, and the prepare
     * routine must skip both the table copy and its table_update bit. */
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
} isp_module_config_t;

typedef struct isp_module_attribute {
    uint32_t feature_bit;
    char     feature_name[32];
    void   (*config)(isp_module_config_t *);
    void   (*enable)(isp_module_config_t *, isp_module_enable_t);
} isp_module_attribute_t;

extern const isp_module_attribute_t isp_module_attrs[ISP_MODULE_COUNT];

/* Instance setup and per-frame dispatch. */
void isp_map_addr(isp_module_config_t *cfg, void *vaddr);
void isp_hardware_update(isp_module_config_t *cfg);

/* Per-module prepare routines (table order). */
void isp_reg_prepare_afs(isp_module_config_t *cfg);
void isp_reg_prepare_sharpness(isp_module_config_t *cfg);
void isp_reg_prepare_contrast(isp_module_config_t *cfg);
void isp_reg_prepare_d2d(isp_module_config_t *cfg);
void isp_reg_prepare_rgb_drc(isp_module_config_t *cfg);
void isp_reg_prepare_pltm(isp_module_config_t *cfg);
void isp_reg_prepare_wdr(isp_module_config_t *cfg);
void isp_reg_prepare_cem(isp_module_config_t *cfg);
void isp_reg_prepare_lens(isp_module_config_t *cfg);
void isp_reg_prepare_gamma(isp_module_config_t *cfg);
void isp_reg_prepare_rgb2yuv(isp_module_config_t *cfg);
void isp_reg_prepare_rgb2rgb(isp_module_config_t *cfg);
void isp_reg_prepare_ae_win(isp_module_config_t *cfg);
void isp_reg_prepare_af(isp_module_config_t *cfg);
void isp_reg_prepare_awb(isp_module_config_t *cfg);
void isp_reg_prepare_hist(isp_module_config_t *cfg);
void isp_reg_prepare_blc(isp_module_config_t *cfg);
void isp_reg_prepare_wb_gain(isp_module_config_t *cfg);
void isp_reg_prepare_dpc(isp_module_config_t *cfg);
void isp_reg_prepare_cfa(isp_module_config_t *cfg);
void isp_reg_prepare_d3d(isp_module_config_t *cfg);
void isp_reg_prepare_cnr(isp_module_config_t *cfg);
void isp_reg_prepare_saturation(isp_module_config_t *cfg);
void isp_reg_prepare_linear(isp_module_config_t *cfg);
void isp_reg_prepare_sensor_offset(isp_module_config_t *cfg);
void isp_reg_prepare_digital_gain(isp_module_config_t *cfg);
void isp_reg_prepare_ctc(isp_module_config_t *cfg);
void isp_reg_prepare_mode(isp_module_config_t *cfg);
void isp_reg_prepare_msc(isp_module_config_t *cfg);
void isp_reg_prepare_lca(isp_module_config_t *cfg);
void isp_reg_prepare_gca(isp_module_config_t *cfg);

/* Per-module enable routines.  CFA and MODE have none. */
void isp_reg_enable_afs(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_sharpness(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_contrast(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_d2d(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_rgb_drc(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_pltm(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_wdr(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_cem(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_lens(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_gamma(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_rgb2yuv(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_rgb2rgb(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_ae(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_af(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_awb(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_hist(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_blc(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_wb_gain(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_dpc(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_d3d(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_cnr(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_saturation(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_linear(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_sensor_offset(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_digital_gain(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_ctc(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_msc(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_lca(isp_module_config_t *cfg, isp_module_enable_t en);
void isp_reg_enable_gca(isp_module_config_t *cfg, isp_module_enable_t en);

#ifdef __cplusplus
}
#endif

#endif /* MODULE_CFG_H */
