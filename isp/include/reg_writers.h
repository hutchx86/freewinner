/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * reg_writers.h - clean-room ISP register/hardware writer tier.
 *
 * Behaviour-only reimplementation of the vendor register layer described in
 * spec/reglayer2.md section 1.  Only the spec's interface facts (entry-point
 * names, register-map field names and offsets, bitfield layouts) are
 * reproduced; see docs/provenance.md for the full provenance record.
 *
 * Model
 * -----
 * Each ISP instance owns a register-pointer descriptor (one member per named
 * register field).  isp_reg_map_load_addr() fills every member with the
 * absolute address base + hardware offset.  Every per-register writer takes the
 * instance id, looks up its descriptor and read-modify-writes the 32-bit
 * register at that member.
 *
 * The descriptor here is a host-build model: members are plain uint32_t*
 * (the target uses memory-mapped volatile words, same layout).  The struct's
 * byte layout is deliberately not the vendor's 0x270-byte layout - only the
 * pointed-to register addresses and their offsets are the interop facts.
 *
 * Field names are the spec's descriptor names.  Some spec rows give a field
 * shift that overlaps another field in the same register; those rows are
 * resolved to the unique non-overlapping packing that fits the register and
 * are flagged "layout resolved" in reg_writers.c.  Bitfield masks used here
 * are the field widths from the spec.
 */
#ifndef REG_WRITERS_H
#define REG_WRITERS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Instance geometry                                                   */
/* ------------------------------------------------------------------ */

#define ISP_REG_INSTANCES       2u    /* isp521_reg[2]                     */
#define ISP_REG_INSTANCE_BYTES  0x270u

/* ------------------------------------------------------------------ */
/* Table / LUT sizes (spec 4.4, 5)                                     */
/* ------------------------------------------------------------------ */

#define ISP_LUT_TH_BYTES        0x42u  /* d3d lum/bright/ref, sharp LUTs    */
#define ISP_LUT_SHARP_SMAP_WORDS 8u    /* 0x20 bytes then one tail byte     */
#define ISP_LUT_SHARP_SMAP_BYTES 0x21u
#define ISP_D3D_K_REGS          6u     /* d3d k and k-delta banks           */
#define ISP_D3D_K_VALUES        (ISP_D3D_K_REGS * 6u)
#define ISP_MSC_LUT_REGS        4u     /* 3 x 10-bit entries per register   */
#define ISP_MSC_LUT_WORDS       12u
#define ISP_AF_FILTER_REGS      19u    /* isp_af_* mapped banks (see 4.3)    */
#define ISP_AF_SQUARE_WORDS     4u
#define ISP_LCA_SATU_WORDS      8u
#define ISP_LCA_SATU_BYTES      0x21u  /* 8 dwords + one tail byte          */

/* ------------------------------------------------------------------ */
/* Module feature bits (spec 5) - dispatch mask and enable bit         */
/* ------------------------------------------------------------------ */

enum isp_feature_bits {
    ISP_FEAT_AE       = 1u << 0,
    ISP_FEAT_LINEAR   = 1u << 1,
    ISP_FEAT_WDR      = 1u << 2,
    ISP_FEAT_DPC      = 1u << 3,
    ISP_FEAT_D2D      = 1u << 4,
    ISP_FEAT_D3D      = 1u << 5,
    ISP_FEAT_AWB      = 1u << 6,
    ISP_FEAT_WB       = 1u << 7,
    ISP_FEAT_LSC      = 1u << 8,
    ISP_FEAT_GAMMA    = 1u << 9,
    ISP_FEAT_SHARP    = 1u << 10,
    ISP_FEAT_AF       = 1u << 11,
    ISP_FEAT_RGB2RGB  = 1u << 12,
    ISP_FEAT_RGB_DRC  = 1u << 13,
    ISP_FEAT_PLTM     = 1u << 14,
    ISP_FEAT_CEM      = 1u << 15,
    ISP_FEAT_AFS      = 1u << 16,
    ISP_FEAT_HIST     = 1u << 17,
    ISP_FEAT_BLC      = 1u << 18,
    ISP_FEAT_DG       = 1u << 19,
    ISP_FEAT_SO       = 1u << 20,
    ISP_FEAT_CTC      = 1u << 21,
    ISP_FEAT_CONTRAST = 1u << 22,
    ISP_FEAT_CNR      = 1u << 23,
    ISP_FEAT_SATU     = 1u << 24,
    ISP_FEAT_CFA      = 1u << 25,
    ISP_FEAT_MODE     = 1u << 26,
    ISP_FEAT_LCA      = 1u << 27,
    ISP_FEAT_GCA      = 1u << 28,
    ISP_FEAT_MSC      = 1u << 29,
    ISP_FEAT_RGB2YUV  = 1u << 30
};

#define ISP_ENABLE   1u
#define ISP_DISABLE  0u

/* isp_reg_set_af_en() enable bits (spec 4.3): byte0[4:0], byte1[3:0],
 * byte2[2:0] map to these bit positions directly. */
enum isp_af_enable_bits {
    ISP_AF_IIR0_EN        = 1u << 0,
    ISP_AF_FIR0_EN        = 1u << 1,
    ISP_AF_IIR0_SEC0_EN   = 1u << 2,
    ISP_AF_IIR0_SEC1_EN   = 1u << 3,
    ISP_AF_IIR0_SEC2_EN   = 1u << 4,
    ISP_AF_IIR0_LDG_EN    = 1u << 8,
    ISP_AF_FIR0_LDG_EN    = 1u << 9,
    ISP_AF_IIR_DS_EN      = 1u << 10,
    ISP_AF_FIR_DS_EN      = 1u << 11,
    ISP_AF_OFFSET_EN      = 1u << 16,
    ISP_AF_PEAK_EN        = 1u << 17,
    ISP_AF_SQU_EN         = 1u << 18
};

/* ------------------------------------------------------------------ */
/* Per-instance register-pointer descriptor (spec 4).  One member per  */
/* register field; map_load fills it with base + hardware offset.      */
/* ------------------------------------------------------------------ */

typedef struct isp_reg_map {
    /* address map / control                                    off */
    uint32_t *isp_global_cfg0;          /* 0x000 */
    uint32_t *isp_global_cfg1;          /* 0x004 */
    uint32_t *isp_update_ctrl0;         /* 0x020 */
    uint32_t *isp_top_ctrl;             /* 0x0fc */
    uint32_t *isp_s1_cfg;               /* 0x100 */
    uint32_t *isp_s1_blc_offset0;       /* 0x104 */
    uint32_t *isp_s1_blc_offset1;       /* 0x108 */
    uint32_t *isp_s1_dg_gain0;          /* 0x10c */
    uint32_t *isp_s1_dg_gain1;          /* 0x110 */
    uint32_t *isp_module_bypass0;       /* 0x1a0 */
    uint32_t *isp_module_mode0;         /* 0x1b0 */
    uint32_t *isp_s0_blc_offset0;       /* 0x1ec */
    uint32_t *isp_s0_blc_offset1;       /* 0x1f0 */
    uint32_t *isp_s0_dg_gain0;          /* 0x0f4 */
    uint32_t *isp_s0_dg_gain1;          /* 0x0f8 */

    uint32_t *isp_wdr_cfg0;             /* 0x200 */
    uint32_t *isp_wdr_cfg1;             /* 0x204 */
    uint32_t *isp_wdr_cfg2;             /* 0x208 */
    uint32_t *isp_dpc_cfg0;             /* 0x260 */
    uint32_t *isp_dpc_cfg1;             /* 0x264 */
    uint32_t *isp_ctc_cfg0;             /* 0x270 */
    uint32_t *isp_ctc_cfg1;             /* 0x274 */
    uint32_t *isp_ctc_cfg2;             /* 0x278 */
    uint32_t *isp_gca_center;           /* 0x280 */
    uint32_t *isp_gca_r_para;           /* 0x284 */
    uint32_t *isp_gca_b_para;           /* 0x288 */
    uint32_t *isp_gca_ctrl;             /* 0x28c */
    uint32_t *isp_d2d_cfg0;             /* 0x2a0 */
    uint32_t *isp_d2d_cfg1;             /* 0x2a4 */
    uint32_t *isp_d2d_cfg2;             /* 0x2a8 */
    uint32_t *isp_d2d_cfg3;             /* 0x2ac */
    uint32_t *isp_d3d_cfg0;             /* 0x2d0 */
    uint32_t *isp_d3d_cfg1;             /* 0x2d4 */
    uint32_t *isp_d3d_cfg2;             /* 0x2d8 */
    uint32_t *isp_d3d_cfg3;             /* 0x2dc */
    uint32_t *isp_sensor_offset0;       /* 0x340 */
    uint32_t *isp_sensor_offset1;       /* 0x344 */
    uint32_t *isp_dg_gain0;             /* 0x360 */
    uint32_t *isp_dg_gain1;             /* 0x364 */
    uint32_t *isp_wb_gain0;             /* 0x370 */
    uint32_t *isp_wb_gain1;             /* 0x374 */
    uint32_t *isp_wb_cfg0;              /* 0x378 */
    uint32_t *isp_rsc_cfg0;             /* 0x390 */
    uint32_t *isp_rsc_cfg1;             /* 0x394 */
    uint32_t *isp_rsc_cfg2;             /* 0x398 */
    uint32_t *isp_pltm_cfg0;            /* 0x3b0 */
    uint32_t *isp_pltm_cfg1;            /* 0x3b4 */
    uint32_t *isp_pltm_cfg2;            /* 0x3b8 */
    uint32_t *isp_pltm_cfg3;            /* 0x3bc */
    uint32_t *isp_demosaic_cfg0;        /* 0x400 */
    uint32_t *isp_lca_cor_ratio;        /* 0x410 */
    uint32_t *isp_lca_det_ctrl0;        /* 0x414 */
    uint32_t *isp_lca_det_ctrl1;        /* 0x418 */
    uint32_t *isp_lca_cor_ctrl;         /* 0x41c */
    uint32_t *isp_sharp_edge_stren;     /* 0x420 */
    uint32_t *isp_sharp_hfrq_stren;     /* 0x424 */
    uint32_t *isp_sharp_diff_cfg;       /* 0x428 */
    uint32_t *isp_sharp_ref_noise;      /* 0x42c */
    uint32_t *isp_sharp_dir_diff_ctrl;  /* 0x430 */
    uint32_t *isp_sharp_edge_ctrl;      /* 0x434 */
    uint32_t *isp_sharp_over_shoot_ctrl;/* 0x438 */
    uint32_t *isp_sharp_under_shoot_ctrl;/*0x43c */
    uint32_t *isp_rgb2rgb_gain0;        /* 0x440 */
    uint32_t *isp_rgb2rgb_gain1;        /* 0x444 */
    uint32_t *isp_rgb2rgb_gain2;        /* 0x448 */
    uint32_t *isp_rgb2rgb_gain3;        /* 0x44c */
    uint32_t *isp_rgb2rgb_gain4;        /* 0x450 */
    uint32_t *isp_rgb2rgb_offset;       /* 0x454 */
    uint32_t *isp_cnr_cfg0;             /* 0x470 */
    uint32_t *isp_cnr_cfg1;             /* 0x474 */
    uint32_t *isp_satu_cfg0;            /* 0x490 */
    uint32_t *isp_rgb2yuv_gain0;        /* 0x520 */
    uint32_t *isp_rgb2yuv_gain1;        /* 0x524 */
    uint32_t *isp_rgb2yuv_gain2;        /* 0x528 */
    uint32_t *isp_rgb2yuv_gain3;        /* 0x52c */
    uint32_t *isp_rgb2yuv_gain4;        /* 0x530 */
    uint32_t *isp_rgb2yuv_offset0;      /* 0x534 */
    uint32_t *isp_rgb2yuv_offset1;      /* 0x538 */
    uint32_t *isp_ae_size;              /* 0x600 */
    uint32_t *isp_ae_start;             /* 0x604 */
    uint32_t *isp_af_cfg;               /* 0x610 */
    uint32_t *isp_af_size;              /* 0x614 */
    uint32_t *isp_af_start;             /* 0x618 */
    uint32_t *isp_af_filter[ISP_AF_FILTER_REGS]; /* 19 mapped banks */
    uint32_t *isp_awb_cfg0;             /* 0x690 */
    uint32_t *isp_awb_cfg1;             /* 0x694 */
    uint32_t *isp_awb_cfg2;             /* 0x698 */
    uint32_t *isp_awb_cfg3;             /* 0x69c */
    uint32_t *isp_hist_size;            /* 0x6c0 */
    uint32_t *isp_hist_start;           /* 0x6c4 */
    uint32_t *isp_afs_cfg0;             /* 0x6e0 */

    /* LUT pointer banks                                        off */
    uint32_t *isp_d3d_lum_th_lut;       /* 0x770 */
    uint32_t *isp_d3d_bright_th_lut;    /* 0x7b4 */
    uint32_t *isp_d3d_ref_noise_lut;    /* 0x7f8 */
    uint32_t *isp_d3d_k_lut[ISP_D3D_K_REGS];       /* 0x83c */
    uint32_t *isp_d3d_k_delta_lut[ISP_D3D_K_REGS]; /* 0x854 */
    uint32_t *isp_sharp_val_lut;        /* 0x86c */
    uint32_t *isp_sharp_edge_lum_lut;   /* 0x8b0 */
    uint32_t *isp_sharp_hfrq_lum_lut;   /* 0x8f4 */
    uint32_t *isp_sharp_hsv_lut;        /* 0x938 */
    uint32_t *isp_sharp_s_map_lut;      /* 0x994 */
    uint32_t *isp_d2d_lp0_np_lut;       /* 0x9b8 */
    uint32_t *isp_d2d_lp1_np_lut;       /* 0x9fc */
    uint32_t *isp_d2d_lp2_np_lut;       /* 0xa40 */
    uint32_t *isp_d2d_lp3_np_lut;       /* 0xa84 */
    uint32_t *isp_af_square_lut;        /* 0xac8 */
    uint32_t *isp_msc_blw_lut;          /* 0xad8 */
    uint32_t *isp_msc_blh_lut;          /* 0xae8 */
    uint32_t *isp_msc_blw_dlt_lut;      /* 0xaf8 */
    uint32_t *isp_msc_blh_dlt_lut;      /* 0xb08 */
    uint32_t *isp_lca_pf_satu_lut;      /* 0xb18 */
    uint32_t *isp_lca_gf_satu_lut;      /* 0xb3c */
    uint32_t *isp_f00;                  /* 0xf00 */
} isp_reg_map_t;

/* ------------------------------------------------------------------ */
/* Composite payloads for the multi-field writers                      */
/* ------------------------------------------------------------------ */

typedef struct isp_gca_para {
    uint8_t  para0;
    uint16_t para1;     /* 10-bit */
    uint16_t para2;     /* 10-bit */
    uint8_t  int_cns;
} isp_gca_para_t;

typedef struct isp_gca_cfg {
    uint16_t ct_h;
    uint16_t ct_w;
    isp_gca_para_t r;
    isp_gca_para_t b;
} isp_gca_cfg_t;

typedef struct isp_ctc_cfg {
    uint16_t th_max;    /* 12-bit */
    uint16_t th_min;    /* 12-bit */
    uint16_t slope;
    uint8_t  dir_wt;    /* 7-bit  */
    uint16_t dir_th;    /* 12-bit */
} isp_ctc_cfg_t;

typedef struct isp_lca_cfg {
    uint16_t gf_cor_ratio;   /* 10-bit */
    uint16_t pf_cor_ratio;   /* 10-bit */
    uint16_t lum_th;         /* 12-bit */
    uint16_t grad_th;        /* 12-bit */
    uint16_t clr_gth;        /* 10-bit */
    uint8_t  pf_rshf;        /* nibble */
    uint16_t pf_bslp;        /* 9-bit  */
    uint16_t clrs_lum_th;
    uint16_t pf_clrc_ratio;
    uint16_t gf_clrc_ratio;
    uint16_t pf_decr_ratio;
} isp_lca_cfg_t;

typedef struct isp_d2d_cfg {
    uint8_t lf_ratio;
    uint8_t bf_ratio;
    uint8_t hf_ratio;
    uint8_t lp_core[4];
    uint8_t lp_side[3];
    uint8_t lp_pcnt[4];
} isp_d2d_cfg_t;

typedef struct isp_d3d_cfg {
    uint8_t  bright_diff;
    uint8_t  clip_ratio;
    uint8_t  lum_diff_clip;
    uint16_t noise_clip;
    uint8_t  st_2d;
    uint8_t  ltf_en;
    uint8_t  rec_en;
    uint8_t  mv_ori;         /* 5-bit  */
    uint16_t c_weight1;      /* 12-bit */
    uint16_t c_weight2;      /* 12-bit */
    uint16_t c_weight3;      /* 12-bit */
    uint16_t ltf_update_frm; /* 10-bit */
} isp_d3d_cfg_t;

typedef struct isp_pltm_cfg {
    uint8_t  lss_switch;
    uint8_t  cal_en;
    uint8_t  frm_sm_en;
    uint8_t  last_order_ratio; /* nibble */
    uint8_t  tr_order;         /* nibble */
    uint8_t  oripic_ratio;     /* byte   */
    uint8_t  intens_asym;
    uint8_t  spatial_asm;
    uint16_t white_level;
    uint8_t  lp_halo_res;      /* nibble */
    uint8_t  lum_ratio;        /* nibble */
    uint8_t  block_height;
    uint8_t  block_width;
    uint8_t  block_v_num;      /* 5-bit */
    uint8_t  block_h_num;      /* 5-bit */
    uint32_t statistic_div;
} isp_pltm_cfg_t;

typedef struct isp_sharp_cfg {
    uint16_t edge_black_stren;  /* 12-bit */
    uint16_t edge_white_stren;  /* 12-bit */
    uint8_t  edge_scale;        /* 5-bit  */
    uint16_t hfrq_black_stren;  /* 12-bit */
    uint16_t hfrq_white_stren;  /* 12-bit */
    uint8_t  hfrq_scale;        /* 5-bit  */
    uint8_t  scale_ratio;       /* 5-bit  */
    uint8_t  conv_ratio;        /* 5-bit  */
    uint16_t ns_lw_th;          /* 12-bit */
    uint16_t ns_hi_th;          /* 12-bit */
    uint16_t dir_clip_val;      /* 12-bit */
    uint16_t dir_eq_ratio;      /* 12-bit */
    uint8_t  edge_th;
    uint8_t  hv_edge_sm;        /* 5-bit  */
    uint8_t  aa_edge_sm;        /* 5-bit  */
    uint16_t over_val;          /* 10-bit */
    uint16_t over_area;         /* 10-bit */
    uint16_t under_val;         /* 10-bit */
    uint16_t under_area;        /* 10-bit */
} isp_sharp_cfg_t;

typedef struct isp_af_filter {
    uint16_t iir0_coef[6];   /* iir0_g0..g5, 10-bit */
    uint16_t iir0_s[4];      /* iir0_s0..s3, 10-bit */
    uint16_t fir0_coef[5];   /* fir0_g0..g4, 6-bit  */
    uint8_t  iir0_dilate;    /* 2-bit */
    uint8_t  iir0_ldg_gain;  /* low byte  */
    uint8_t  iir0_ldg_hgain; /* high byte */
    uint8_t  iir0_ldg_th;    /* low byte  */
    uint8_t  iir0_ldg_hth;   /* high byte */
    uint8_t  fir0_ldg_gain;  /* low byte  */
    uint8_t  fir0_ldg_hgain; /* high byte */
    uint8_t  fir0_ldg_th;    /* low byte  */
    uint8_t  fir0_ldg_hth;   /* high byte */
    uint8_t  iir0_ldg_lslope;/* nibble */
    uint8_t  iir0_ldg_hslope;/* nibble */
    uint8_t  fir0_ldg_lslope;/* nibble */
    uint8_t  fir0_ldg_hslope;/* nibble */
    uint8_t  iir0_core_th;
    uint8_t  iir0_core_peak;
    uint8_t  fir0_core_th;
    uint8_t  fir0_core_peak;
    uint8_t  iir0_core_slope;/* nibble */
    uint8_t  fir0_core_slope;/* nibble */
    uint8_t  hlt_th;
    uint16_t r_offset;       /* 13-bit */
    uint16_t g_offset;       /* 13-bit */
    uint16_t b_offset;       /* 13-bit */
} isp_af_filter_t;

/* ------------------------------------------------------------------ */
/* Entry points (spec 4) - names are interface facts                   */
/* ------------------------------------------------------------------ */

void isp_reg_map_load_addr(unsigned long id, void *base);

void isp_reg_set_input_fmt(unsigned long id, uint32_t fmt);
void isp_reg_update_table(unsigned long id, uint32_t mask);
void isp_reg_top_control(unsigned long id, uint32_t n, uint32_t w);
void isp_reg_module_enable(unsigned long id, uint32_t flag);
void isp_reg_module_disable(unsigned long id, uint32_t flag);

/* Digital-gain enable side effects: the DG bypass bit plus the DG/SO routing
 * bits that depend on dg_mode. */
void isp_reg_set_dg_bypass(unsigned long id, int en, uint32_t dg_mode);

/* mode selectors (spec 4.2) */
void isp_reg_set_wdr_compress_mode(unsigned long id, uint32_t mode);
void isp_reg_set_saturation_mode(unsigned long id, uint32_t mode);
void isp_reg_set_cfa_mode(unsigned long id, uint32_t mode);
void isp_reg_set_dg_mode(unsigned long id, uint32_t mode);
void isp_reg_set_ae_mode(unsigned long id, uint32_t mode);
void isp_reg_set_lsc_mode(unsigned long id, uint32_t mode);
void isp_reg_set_msc_mode(unsigned long id, uint32_t mode);
void isp_reg_set_awb_mode(unsigned long id, uint32_t mode);
void isp_reg_set_hist_src(unsigned long id, uint32_t mode);
void isp_reg_set_hist_mode(unsigned long id, uint32_t mode);
void isp_reg_set_dpc_mode(unsigned long id, uint32_t mode);
void isp_reg_set_d3d_mode(unsigned long id, uint32_t mode);
void isp_reg_set_af_mode(unsigned long id, uint32_t mode);

/* module payload writers (spec 4.3) */
void isp_reg_set_blc_offset(unsigned long id, uint32_t r, uint32_t gr,
                            uint32_t gb, uint32_t b);
void isp_reg_set_wdr_cfg(unsigned long id, uint32_t lo_th, uint32_t hi_th,
                         uint32_t exp_ratio, uint32_t slope, uint32_t mv_th,
                         uint32_t mv_scale, uint32_t out_sel);
void isp_reg_set_dpc(unsigned long id, uint32_t r0, uint32_t r1, uint32_t r2,
                     uint32_t r3, uint32_t slope_th, uint32_t cold_abs_th);
void isp_reg_set_ctc(unsigned long id, const isp_ctc_cfg_t *cfg);
void isp_reg_set_gca(unsigned long id, const isp_gca_cfg_t *cfg);
void isp_reg_set_lca(unsigned long id, const isp_lca_cfg_t *cfg);
void isp_reg_set_d2d_cfg(unsigned long id, const isp_d2d_cfg_t *cfg);
void isp_reg_set_d3d_cfg(unsigned long id, const isp_d3d_cfg_t *cfg);
void isp_reg_set_sensor_offset(unsigned long id, uint32_t r, uint32_t gr,
                               uint32_t gb, uint32_t b);
void isp_reg_set_dg_gain(unsigned long id, uint32_t r, uint32_t gr,
                         uint32_t gb, uint32_t b);
void isp_reg_set_wb_gain(unsigned long id, uint32_t r, uint32_t gr,
                         uint32_t gb, uint32_t b);
void isp_reg_set_wb_clip(unsigned long id, uint32_t clip);
void isp_reg_set_lsc(unsigned long id, uint32_t ct_x, uint32_t ct_y,
                     uint32_t rs_val);
void isp_reg_set_pltm_cfg(unsigned long id, const isp_pltm_cfg_t *cfg);
void isp_reg_set_cfa(unsigned long id, uint32_t dir_th, uint32_t interp_mode,
                     uint32_t zig_zag);
void isp_reg_set_sharp(unsigned long id, const isp_sharp_cfg_t *cfg);
void isp_reg_set_rgb2rgb_gain_offset(unsigned long id, const uint16_t gain[9],
                                     const uint16_t offset[3]);
void isp_reg_set_cnr(unsigned long id, uint32_t c_th, uint32_t y_th,
                     uint32_t st_v_y, uint32_t st_h_y);
void isp_reg_set_saturation(unsigned long id, uint32_t r, uint32_t g,
                            uint32_t b);
void isp_reg_set_dehaze(unsigned long id);
void isp_reg_set_rgb2yuv_gain_offset(unsigned long id, const uint16_t gain[9],
                                     const uint16_t offset[3]);
void isp_reg_set_ae_win(unsigned long id, uint32_t width, uint32_t height,
                        uint32_t hor_start, uint32_t ver_start);
void isp_reg_set_af_en(unsigned long id, uint32_t en_bits);
void isp_reg_set_af_win(unsigned long id, uint32_t hor_num, uint32_t ver_num,
                        uint32_t width, uint32_t height, uint32_t hor_start,
                        uint32_t ver_start);
void isp_reg_set_af_filter(unsigned long id, const isp_af_filter_t *f);
void isp_reg_set_awb_satur_lim(unsigned long id, uint32_t lim_r, uint32_t lim_g,
                               uint32_t lim_b);
void isp_reg_set_awb_win(unsigned long id, uint32_t width, uint32_t height,
                         uint32_t hor_start, uint32_t ver_start);
void isp_reg_set_hist_win(unsigned long id, uint32_t width, uint32_t height,
                          uint32_t hor_start, uint32_t ver_start);
void isp_reg_set_afs_anti_flick(unsigned long id, uint32_t line_inc);

/* LUT writers (spec 4.4) */
void isp_reg_set_d3d_lum_th_lut(unsigned long id, const void *src);
void isp_reg_set_d3d_bright_th_lut(unsigned long id, const void *src);
void isp_reg_set_d3d_ref_noise_lut(unsigned long id, const void *src);
void isp_reg_set_d3d_k_lut(unsigned long id, const uint8_t *vals);
void isp_reg_set_d3d_k_delta_lut(unsigned long id, const uint8_t *vals);
void isp_reg_set_sharp_val_lut(unsigned long id, const void *src);
void isp_reg_set_sharp_edge_lum_lut(unsigned long id, const void *src);
void isp_reg_set_sharp_hfrq_lum_lut(unsigned long id, const void *src);
void isp_reg_set_sharp_hsv_lut(unsigned long id, const void *src);
void isp_reg_set_sharp_s_map_lut(unsigned long id, const void *src);
void isp_reg_set_d2d_lp0_np_lut(unsigned long id, const void *src);
void isp_reg_set_d2d_lp1_np_lut(unsigned long id, const void *src);
void isp_reg_set_d2d_lp2_np_lut(unsigned long id, const void *src);
void isp_reg_set_d2d_lp3_np_lut(unsigned long id, const void *src);
void isp_reg_set_af_square_lut(unsigned long id, const void *src);
void isp_reg_set_msc_blw_lut(unsigned long id, const uint16_t *vals);
void isp_reg_set_msc_blh_lut(unsigned long id, const uint16_t *vals);
void isp_reg_set_msc_blw_dlt_lut(unsigned long id, const uint16_t *vals);
void isp_reg_set_msc_blh_dlt_lut(unsigned long id, const uint16_t *vals);
void isp_reg_set_lca_pf_satu_lut(unsigned long id, const void *src);
void isp_reg_set_lca_gf_satu_lut(unsigned long id, const void *src);

#ifdef __cplusplus
}
#endif

#endif /* REG_WRITERS_H */
