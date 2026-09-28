/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* module_cfg_shim.c - SDK-ABI adapter for the clean module_cfg tier:
 * SDK -> clean on entry, clean entry points, mutated outputs back to the SDK.
 * Field mapping: docs/shim-mappings.md#module_cfg */
/* SDK and module_cfg.h share ISP_* macros with different meanings (clean
 * ISP_DRC_TBL_SIZE is bytes, SDK elements) and two entry-point names. */
#include <string.h>

#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"

#undef ISP_DRC_TBL_SIZE
#undef ISP_GAMMA_TBL_LENGTH
#undef ISP_LENS_TBL_SIZE

/* clean entry points, renamed apart from the SDK ones */
#define isp_hardware_update clean_isp_hardware_update
#define isp_map_addr        clean_isp_map_addr
#include "module_cfg.h"
#undef isp_hardware_update
#undef isp_map_addr

#include "module_cfg_shim.h"

/* The SDK aggregate must match the vendor ABI: 37040 bytes (0x90b0) on the
 * 32-bit target (host pointers are wider). */
#if __SIZEOF_POINTER__ == 4
typedef char module_cfg_shim_abi_check[
    (sizeof(struct fwi_hw_module_cfg) == 37040) ? 1 : -1];
#endif

/* ------------------------------------------------------------------ */
/* Translation: SDK -> clean                                           */
/* ------------------------------------------------------------------ */

static void win_to_clean(fwi_mod_h3a_reg_win_t *d, const fwi_h3a_reg_window_t *s)
{
    d->hor_num   = s->horizontal_count;
    d->ver_num   = s->vertical_count;
    d->width     = s->width;
    d->height    = s->height;
    d->hor_start = s->horizontal_start;
    d->ver_start = s->vertical_start;
}

static void af_en_to_bits(fwi_mod_af_cfg_t *d, const fwi_af_en_cfg_t *e)
{
    uint32_t en = 0;

    if (e->af_iir0_en)      en |= ISP_AF_IIR0_EN;
    if (e->af_fir0_en)      en |= ISP_AF_FIR0_EN;
    if (e->af_iir0_sec0_en) en |= ISP_AF_IIR0_SEC0_EN;
    if (e->af_iir0_sec1_en) en |= ISP_AF_IIR0_SEC1_EN;
    if (e->af_iir0_sec2_en) en |= ISP_AF_IIR0_SEC2_EN;
    if (e->af_iir0_ldg_en)  en |= ISP_AF_IIR0_LDG_EN;
    if (e->af_fir0_ldg_en)  en |= ISP_AF_FIR0_LDG_EN;
    if (e->af_iir_downsample_en)    en |= ISP_AF_IIR_DS_EN;
    if (e->af_fir_downsample_en)    en |= ISP_AF_FIR_DS_EN;
    if (e->af_offset_en)    en |= ISP_AF_OFFSET_EN;
    if (e->af_peak_en)      en |= ISP_AF_PEAK_EN;
    if (e->af_squ_en)       en |= ISP_AF_SQU_EN;
    d->en_bits = en;
}

static void sdk_to_clean(fwi_mod_config_t *c,
                         const struct fwi_hw_module_cfg *s)
{
    const fwi_af_filter_cfg_t *f = &s->af_cfg.af_filter_cfg;
    unsigned i;

    memset(c, 0, sizeof(*c));

    c->isp_dev_id         = s->dev_id;
    c->module_enable_flag = s->module_enable_flag;
    c->table_update       = s->table_update;

    /* Only the selectors the clean writers consume; d3d_mode has no SDK
     * mode_cfg field (the vendor d3d writer uses k3d_increase_mode). */
    c->mode_cfg.input_fmt    = s->mode_cfg.input_fmt;
    c->mode_cfg.dg_mode      = s->mode_cfg.dg_mode;
    c->mode_cfg.cfa_mode     = s->mode_cfg.demosaic_mode;
    c->mode_cfg.otf_dpc_mode = s->mode_cfg.on_the_fly_defect_pixel_mode;
    c->mode_cfg.hist_sel     = s->mode_cfg.hist_select;
    c->mode_cfg.awb_mode     = s->mode_cfg.awb_mode;
    c->mode_cfg.ae_mode      = s->mode_cfg.ae_mode;
    c->mode_cfg.wdr_cmp_mode = s->mode_cfg.wdr_cmp_mode;
    c->mode_cfg.rsc_mode     = s->mode_cfg.rsc_mode;
    c->mode_cfg.msc_mode     = s->mode_cfg.mesh_shading_mode;
    c->mode_cfg.d3d_mode     = s->denoise_3d_cfg.k3d_increase_mode;

    c->afs_cfg.inc_line      = s->afs_cfg.inc_line;

    c->cfa_cfg.dir_th        = s->demosaic_cfg.dir_threshold;
    c->cfa_cfg.interp_mode   = s->demosaic_cfg.interp_mode;
    c->cfa_cfg.zig_zag       = s->demosaic_cfg.zig_zag;

    c->otf_cfg.ratio[0]      = s->on_the_fly_cfg.hot_ratio;
    c->otf_cfg.ratio[1]      = s->on_the_fly_cfg.cold_ratio;
    c->otf_cfg.ratio[2]      = s->on_the_fly_cfg.nbhd_diff_ratio;
    c->otf_cfg.ratio[3]      = s->on_the_fly_cfg.nearest_diff_ratio;
    c->otf_cfg.slope_th      = s->on_the_fly_cfg.slope_threshold;
    c->otf_cfg.cold_abs_th   = s->on_the_fly_cfg.cold_abs_threshold;

    c->cnr_cfg.c_th          = s->chroma_denoise_cfg.c_threshold;
    c->cnr_cfg.y_th          = s->chroma_denoise_cfg.y_threshold;
    c->cnr_cfg.st_v_y        = s->chroma_denoise_cfg.st_v_yth;
    c->cnr_cfg.st_h_y        = s->chroma_denoise_cfg.st_h_yth;

    c->gain_offset_cfg.offset[0] = (uint32_t)(int32_t)s->gain_offset_cfg.offset.r_offset;
    c->gain_offset_cfg.offset[1] = (uint32_t)(int32_t)s->gain_offset_cfg.offset.gr_offset;
    c->gain_offset_cfg.offset[2] = (uint32_t)(int32_t)s->gain_offset_cfg.offset.gb_offset;
    c->gain_offset_cfg.offset[3] = (uint32_t)(int32_t)s->gain_offset_cfg.offset.b_offset;
    c->gain_offset_cfg.gain[0] = s->gain_offset_cfg.gain.r_gain;
    c->gain_offset_cfg.gain[1] = s->gain_offset_cfg.gain.gr_gain;
    c->gain_offset_cfg.gain[2] = s->gain_offset_cfg.gain.gb_gain;
    c->gain_offset_cfg.gain[3] = s->gain_offset_cfg.gain.b_gain;
    c->gain_offset_cfg.sensor_offset[0] = (uint32_t)(int32_t)s->gain_offset_cfg.sensor_offset.r_offset;
    c->gain_offset_cfg.sensor_offset[1] = (uint32_t)(int32_t)s->gain_offset_cfg.sensor_offset.gr_offset;
    c->gain_offset_cfg.sensor_offset[2] = (uint32_t)(int32_t)s->gain_offset_cfg.sensor_offset.gb_offset;
    c->gain_offset_cfg.sensor_offset[3] = (uint32_t)(int32_t)s->gain_offset_cfg.sensor_offset.b_offset;

    c->wb_gain_cfg.clip_val  = s->wb_gain_cfg.clip_val;
    c->wb_gain_cfg.wb_gain[0] = s->wb_gain_cfg.wb_gain.r_gain;
    c->wb_gain_cfg.wb_gain[1] = s->wb_gain_cfg.wb_gain.gr_gain;
    c->wb_gain_cfg.wb_gain[2] = s->wb_gain_cfg.wb_gain.gb_gain;
    c->wb_gain_cfg.wb_gain[3] = s->wb_gain_cfg.wb_gain.b_gain;

    c->ctc_cfg.th_max        = s->crosstalk_cfg.crosstalk_threshold_max;
    c->ctc_cfg.th_min        = s->crosstalk_cfg.crosstalk_threshold_min;
    c->ctc_cfg.slope         = s->crosstalk_cfg.crosstalk_threshold_slope;
    c->ctc_cfg.dir_wt        = (uint8_t)s->crosstalk_cfg.crosstalk_dir_wt;
    c->ctc_cfg.dir_th        = s->crosstalk_cfg.crosstalk_dir_threshold;

    c->gca_cfg.ct_h          = s->green_ca_cfg.green_ca_ct_h;
    c->gca_cfg.ct_w          = s->green_ca_cfg.green_ca_ct_w;
    c->gca_cfg.r.para0       = (uint8_t)s->green_ca_cfg.green_ca_r_para0;
    c->gca_cfg.r.para1       = s->green_ca_cfg.green_ca_r_para1;
    c->gca_cfg.r.para2       = s->green_ca_cfg.green_ca_r_para2;
    c->gca_cfg.r.int_cns     = (uint8_t)s->green_ca_cfg.green_ca_int_cns;
    c->gca_cfg.b.para0       = (uint8_t)s->green_ca_cfg.green_ca_b_para0;
    c->gca_cfg.b.para1       = s->green_ca_cfg.green_ca_b_para1;
    c->gca_cfg.b.para2       = s->green_ca_cfg.green_ca_b_para2;

    c->lca_cfg.gf_cor_ratio  = s->lateral_ca_cfg.lateral_ca_gf_cor_ratio;
    c->lca_cfg.pf_cor_ratio  = s->lateral_ca_cfg.lateral_ca_pf_cor_ratio;
    c->lca_cfg.lum_th        = s->lateral_ca_cfg.lateral_ca_luminance_threshold;
    c->lca_cfg.grad_th       = s->lateral_ca_cfg.lateral_ca_grad_threshold;
    c->lca_cfg.clr_gth       = s->lateral_ca_cfg.lateral_ca_clr_gth;
    c->lca_cfg.pf_rshf       = (uint8_t)s->lateral_ca_cfg.lateral_ca_pf_rshf;
    c->lca_cfg.pf_bslp       = s->lateral_ca_cfg.lateral_ca_pf_bslp;
    c->lca_cfg.clrs_lum_th   = s->lateral_ca_cfg.lateral_ca_clrs_luminance_threshold;
    c->lca_cfg.pf_clrc_ratio = s->lateral_ca_cfg.lateral_ca_pf_clrc_ratio;
    c->lca_cfg.gf_clrc_ratio = s->lateral_ca_cfg.lateral_ca_gf_clrc_ratio;
    c->lca_cfg.pf_decr_ratio = s->lateral_ca_cfg.lateral_ca_pf_decr_ratio;
    memcpy(c->lca_pf_satu_lut, s->lateral_ca_cfg.lateral_ca_pf_saturation_lut, ISP_LCA_SATU_BYTES);
    memcpy(c->lca_gf_satu_lut, s->lateral_ca_cfg.lateral_ca_gf_saturation_lut, ISP_LCA_SATU_BYTES);

    /* Clean LUTs are byte images of the SDK u16 LUTs; both writers copy only
     * ISP_LUT_TH_BYTES (0x42), less than the SDK sharp_hsv[46]. */
    c->sharp_cfg.edge_black_stren = s->sharp_cfg.edge_black_strength;
    c->sharp_cfg.edge_white_stren = s->sharp_cfg.edge_white_strength;
    c->sharp_cfg.hfrq_black_stren = s->sharp_cfg.high_frequency_black_strength;
    c->sharp_cfg.hfrq_white_stren = s->sharp_cfg.high_frequency_white_strength;
    c->sharp_cfg.edge_scale       = (uint8_t)s->sharp_cfg.edge_scale_ratio;
    c->sharp_cfg.hfrq_scale       = (uint8_t)s->sharp_cfg.high_frequency_scale_ratio;
    c->sharp_cfg.scale_ratio      = (uint8_t)s->sharp_cfg.edge_conv_para;
    c->sharp_cfg.conv_ratio       = (uint8_t)s->sharp_cfg.high_frequency_conv_para;
    c->sharp_cfg.ns_lw_th         = s->sharp_cfg.ns_lw_threshold;
    c->sharp_cfg.ns_hi_th         = s->sharp_cfg.ns_hi_threshold;
    c->sharp_cfg.dir_clip_val     = s->sharp_cfg.dir_clip_val;
    c->sharp_cfg.dir_eq_ratio     = s->sharp_cfg.dir_eq_ratio;
    c->sharp_cfg.edge_th          = (uint8_t)s->sharp_cfg.edge_threshold;
    c->sharp_cfg.hv_edge_sm       = (uint8_t)s->sharp_cfg.hv_edge_smoothing_ratio;
    c->sharp_cfg.aa_edge_sm       = (uint8_t)s->sharp_cfg.aa_edge_smoothing_ratio;
    c->sharp_cfg.over_val         = s->sharp_cfg.over_val_ctrl;
    c->sharp_cfg.over_area        = s->sharp_cfg.over_area_ctrl;
    c->sharp_cfg.under_val        = s->sharp_cfg.under_val_ctrl;
    c->sharp_cfg.under_area       = s->sharp_cfg.under_area_ctrl;
    memcpy(c->sharp_val_lut,      s->sharp_cfg.sharp_val,      ISP_LUT_TH_BYTES);
    memcpy(c->sharp_edge_lum_lut, s->sharp_cfg.sharp_edge_luminance, ISP_LUT_TH_BYTES);
    memcpy(c->sharp_hfrq_lum_lut, s->sharp_cfg.sharp_high_frequency_luminance, ISP_LUT_TH_BYTES);
    memcpy(c->sharp_hsv_lut,      s->sharp_cfg.sharp_hsv,      ISP_LUT_TH_BYTES);
    memcpy(c->sharp_s_map_lut,    s->sharp_cfg.sharp_s_map,    ISP_LUT_SHARP_SMAP_BYTES);

    /* 2D denoise */
    c->bdnf_cfg.lf_ratio = (uint8_t)s->bayer_denoise_cfg.lf_ratio;
    c->bdnf_cfg.bf_ratio = (uint8_t)s->bayer_denoise_cfg.bf_ratio;
    c->bdnf_cfg.hf_ratio = (uint8_t)s->bayer_denoise_cfg.hf_ratio;
    c->bdnf_cfg.lp_core[0] = (uint8_t)s->bayer_denoise_cfg.lp0_np_core_ratio;
    c->bdnf_cfg.lp_core[1] = (uint8_t)s->bayer_denoise_cfg.lp1_np_core_ratio;
    c->bdnf_cfg.lp_core[2] = (uint8_t)s->bayer_denoise_cfg.lp2_np_core_ratio;
    c->bdnf_cfg.lp_core[3] = (uint8_t)s->bayer_denoise_cfg.lp3_np_core_ratio;
    c->bdnf_cfg.lp_side[0] = (uint8_t)s->bayer_denoise_cfg.lp0_np_side_ratio;
    c->bdnf_cfg.lp_side[1] = (uint8_t)s->bayer_denoise_cfg.lp1_np_side_ratio;
    c->bdnf_cfg.lp_side[2] = (uint8_t)s->bayer_denoise_cfg.lp2_np_side_ratio;
    c->bdnf_cfg.lp_pcnt[0] = (uint8_t)s->bayer_denoise_cfg.lp0_pcnt_ratio;
    c->bdnf_cfg.lp_pcnt[1] = (uint8_t)s->bayer_denoise_cfg.lp1_pcnt_ratio;
    c->bdnf_cfg.lp_pcnt[2] = (uint8_t)s->bayer_denoise_cfg.lp2_pcnt_ratio;
    c->bdnf_cfg.lp_pcnt[3] = (uint8_t)s->bayer_denoise_cfg.lp3_pcnt_ratio;
    memcpy(c->d2d_lp_lut[0], s->bayer_denoise_cfg.d2d_lp0_threshold, ISP_LUT_TH_BYTES);
    memcpy(c->d2d_lp_lut[1], s->bayer_denoise_cfg.d2d_lp1_threshold, ISP_LUT_TH_BYTES);
    memcpy(c->d2d_lp_lut[2], s->bayer_denoise_cfg.d2d_lp2_threshold, ISP_LUT_TH_BYTES);
    memcpy(c->d2d_lp_lut[3], s->bayer_denoise_cfg.d2d_lp3_threshold, ISP_LUT_TH_BYTES);

    /* 3D denoise: clean fwi_reg_d3d_cfg_t; tdnf_lum_th / tdnf_bri_th are outputs. */
    c->tdf_cfg.rec_en         = (uint8_t)s->denoise_3d_cfg.rec_en;
    c->tdf_cfg.noise_clip     = s->denoise_3d_cfg.noise_clip_ratio;
    c->tdf_cfg.bright_diff    = (uint8_t)s->denoise_3d_cfg.bright_diff_ratio;
    c->tdf_cfg.clip_ratio     = (uint8_t)s->denoise_3d_cfg.bright_diff_clip_ratio;
    c->tdf_cfg.lum_diff_clip  = (uint8_t)s->denoise_3d_cfg.luminance_diff_clip_ratio;
    c->tdf_cfg.st_2d          = (uint8_t)s->denoise_3d_cfg.st_2d_ratio;
    c->tdf_cfg.mv_ori         = (uint8_t)s->denoise_3d_cfg.mv_ori_ratio;
    c->tdf_cfg.ltf_en         = (uint8_t)s->denoise_3d_cfg.ltf_en;
    c->tdf_cfg.c_weight1      = s->denoise_3d_cfg.c_weight1;
    c->tdf_cfg.c_weight2      = s->denoise_3d_cfg.c_weight2;
    c->tdf_cfg.c_weight3      = s->denoise_3d_cfg.c_weight3;
    c->tdf_cfg.ltf_update_frm = s->denoise_3d_cfg.ltf_update_frame;
    memcpy(c->d3d_tdnf_th,       s->denoise_3d_cfg.temporal_denoise_threshold,       ISP_LUT_TH_BYTES);
    memcpy(c->d3d_ref_noise_lut, s->denoise_3d_cfg.temporal_denoise_ref_noise, ISP_LUT_TH_BYTES);
    /* The d3d k writers consume 32 entries (6 banks: 5 x 6 + 2); the clean
     * arrays are 36 bytes, so only the 32 useful bytes are loaded. */
    memcpy(c->d3d_k_lut,       s->denoise_3d_cfg.temporal_denoise_k,       sizeof(s->denoise_3d_cfg.temporal_denoise_k));
    memcpy(c->d3d_k_delta_lut, s->denoise_3d_cfg.temporal_denoise_k_delta, sizeof(s->denoise_3d_cfg.temporal_denoise_k_delta));

    /* color matrices */
    for (i = 0; i < 3; i++) {
        unsigned j;
        for (j = 0; j < 3; j++) {
            c->rgb2rgb_cfg.color_matrix[3u * i + j] =
                (uint16_t)s->rgb2rgb_cfg.colour_matrix.matrix[i][j];
            c->rgb2yuv.gain[3u * i + j] =
                (uint16_t)s->rgb2yuv.matrix[i][j];
        }
        c->rgb2rgb_cfg.offset[i] = (uint16_t)s->rgb2rgb_cfg.colour_matrix.offset[i];
        c->rgb2yuv.offset[i]     = (uint16_t)s->rgb2yuv.offset[i];
    }

    /* 3A windows */
    win_to_clean(&c->ae_cfg.win, &s->ae_cfg.ae_reg_window);
    win_to_clean(&c->af_cfg.win, &s->af_cfg.af_reg_window);
    c->af_cfg.mode = s->af_cfg.af_mode;
    af_en_to_bits(&c->af_cfg, &s->af_cfg.af_en_cfg);
    c->af_cfg.filter.iir0_coef[0] = (uint16_t)f->af_iir0_g0;
    c->af_cfg.filter.iir0_coef[1] = (uint16_t)f->af_iir0_g1;
    c->af_cfg.filter.iir0_coef[2] = (uint16_t)f->af_iir0_g2;
    c->af_cfg.filter.iir0_coef[3] = (uint16_t)f->af_iir0_g3;
    c->af_cfg.filter.iir0_coef[4] = (uint16_t)f->af_iir0_g4;
    c->af_cfg.filter.iir0_coef[5] = (uint16_t)f->af_iir0_g5;
    c->af_cfg.filter.iir0_s[0] = f->af_iir0_s0;
    c->af_cfg.filter.iir0_s[1] = f->af_iir0_s1;
    c->af_cfg.filter.iir0_s[2] = f->af_iir0_s2;
    c->af_cfg.filter.iir0_s[3] = f->af_iir0_s3;
    c->af_cfg.filter.fir0_coef[0] = (uint16_t)f->af_fir0_g0;
    c->af_cfg.filter.fir0_coef[1] = (uint16_t)f->af_fir0_g1;
    c->af_cfg.filter.fir0_coef[2] = (uint16_t)f->af_fir0_g2;
    c->af_cfg.filter.fir0_coef[3] = (uint16_t)f->af_fir0_g3;
    c->af_cfg.filter.fir0_coef[4] = (uint16_t)f->af_fir0_g4;
    c->af_cfg.filter.iir0_dilate    = (uint8_t)f->af_iir0_dilate;
    c->af_cfg.filter.iir0_ldg_gain  = (uint8_t)f->af_iir0_ldg_low_gain;
    c->af_cfg.filter.iir0_ldg_hgain = (uint8_t)f->af_iir0_ldg_high_gain;
    c->af_cfg.filter.iir0_ldg_th    = (uint8_t)f->af_iir0_ldg_low_threshold;
    c->af_cfg.filter.iir0_ldg_hth   = (uint8_t)f->af_iir0_ldg_high_threshold;
    c->af_cfg.filter.fir0_ldg_gain  = (uint8_t)f->af_fir0_ldg_low_gain;
    c->af_cfg.filter.fir0_ldg_hgain = (uint8_t)f->af_fir0_ldg_high_gain;
    c->af_cfg.filter.fir0_ldg_th    = (uint8_t)f->af_fir0_ldg_low_threshold;
    c->af_cfg.filter.fir0_ldg_hth   = (uint8_t)f->af_fir0_ldg_high_threshold;
    c->af_cfg.filter.iir0_ldg_lslope = (uint8_t)f->af_iir0_ldg_low_slope;
    c->af_cfg.filter.iir0_ldg_hslope = (uint8_t)f->af_iir0_ldg_high_slope;
    c->af_cfg.filter.fir0_ldg_lslope = (uint8_t)f->af_fir0_ldg_low_slope;
    c->af_cfg.filter.fir0_ldg_hslope = (uint8_t)f->af_fir0_ldg_high_slope;
    c->af_cfg.filter.iir0_core_th    = (uint8_t)f->af_iir0_core_threshold;
    c->af_cfg.filter.iir0_core_peak  = (uint8_t)f->af_iir0_core_peak;
    c->af_cfg.filter.fir0_core_th    = (uint8_t)f->af_fir0_core_threshold;
    c->af_cfg.filter.fir0_core_peak  = (uint8_t)f->af_fir0_core_peak;
    c->af_cfg.filter.iir0_core_slope = (uint8_t)f->af_iir0_core_slope;
    c->af_cfg.filter.fir0_core_slope = (uint8_t)f->af_fir0_core_slope;
    c->af_cfg.filter.hlt_th          = (uint8_t)f->af_hlt_threshold;
    c->af_cfg.filter.r_offset        = (uint16_t)f->af_r_offset;
    c->af_cfg.filter.g_offset        = (uint16_t)f->af_g_offset;
    c->af_cfg.filter.b_offset        = (uint16_t)f->af_b_offset;
    memcpy(c->af_cfg.square_lut, s->af_cfg.af_square_lut,
           sizeof(c->af_cfg.square_lut));

    c->awb_cfg.sat_r = s->awb_cfg.awb_r_saturation_limit;
    c->awb_cfg.sat_g = s->awb_cfg.awb_g_saturation_limit;
    c->awb_cfg.sat_b = s->awb_cfg.awb_b_saturation_limit;
    win_to_clean(&c->awb_cfg.win, &s->awb_cfg.awb_reg_window);

    c->hist_cfg.mode = s->hist_cfg.hist_mode;
    win_to_clean(&c->hist_cfg.win, &s->hist_cfg.hist_reg_window);

    /* lens shading: clean copies its embedded lens_src into lens_table, so
     * lens_src is fed from the SDK's contiguous r/g/b planes. */
    c->lens_cfg.lsc_cfg.ct_x   = s->lens_cfg.lens_shading_cfg.ct_x;
    c->lens_cfg.lsc_cfg.ct_y   = s->lens_cfg.lens_shading_cfg.ct_y;
    c->lens_cfg.lsc_cfg.rs_val = s->lens_cfg.lens_shading_cfg.rs_val;
    memcpy(c->lens_src, s->lens_cfg.lens_r_table, sizeof(c->lens_src));

    /* pltm: clean source array from the SDK embedded table */
    c->pltm_cfg.lss_switch      = (uint8_t)s->pltm_cfg.lss_switch;
    c->pltm_cfg.cal_en          = (uint8_t)s->pltm_cfg.cal_en;
    c->pltm_cfg.frm_sm_en       = (uint8_t)s->pltm_cfg.frame_smoothing_en;
    c->pltm_cfg.last_order_ratio = (uint8_t)s->pltm_cfg.last_order_ratio;
    c->pltm_cfg.tr_order        = (uint8_t)s->pltm_cfg.tr_order;
    c->pltm_cfg.oripic_ratio    = (uint8_t)s->pltm_cfg.original_picture_ratio;
    c->pltm_cfg.intens_asym     = (uint8_t)s->pltm_cfg.intens_asym;
    c->pltm_cfg.spatial_asm     = (uint8_t)s->pltm_cfg.spatial_asm;
    c->pltm_cfg.white_level     = s->pltm_cfg.white_level;
    c->pltm_cfg.lp_halo_res     = (uint8_t)s->pltm_cfg.lp_halo_res;
    c->pltm_cfg.lum_ratio       = (uint8_t)s->pltm_cfg.luminance_ratio;
    c->pltm_cfg.block_height    = (uint8_t)s->pltm_cfg.block_height;
    c->pltm_cfg.block_width     = (uint8_t)s->pltm_cfg.block_width;
    c->pltm_cfg.block_v_num     = (uint8_t)s->pltm_cfg.block_v_count;
    c->pltm_cfg.block_h_num     = (uint8_t)s->pltm_cfg.block_h_count;
    c->pltm_cfg.statistic_div   = s->pltm_cfg.statistic_div;

    /* embedded input tables -> clean source arrays */
    memcpy(c->cem_src,  s->colour_enhance_cfg.colour_enhance_table,  sizeof(c->cem_src));
    memcpy(c->drc_src,  s->drc_cfg.drc_table,  sizeof(c->drc_src));
    memcpy(c->pltm_src, s->pltm_cfg.pltm_table, sizeof(c->pltm_src));
    memcpy(c->wdr_src,  s->wdr_cfg.wdr_table,  sizeof(c->wdr_src));
    c->wdr_cfg.lo_th     = s->wdr_cfg.wdr_low_threshold;
    c->wdr_cfg.hi_th     = s->wdr_cfg.wdr_hi_threshold;
    c->wdr_cfg.exp_ratio = s->wdr_cfg.wdr_exposure_ratio;
    c->wdr_cfg.slope     = s->wdr_cfg.wdr_slope;
    c->wdr_cfg.mv_th     = s->wdr_cfg.wdr_mv_threshold;
    c->wdr_cfg.mv_scale  = s->wdr_cfg.wdr_mv_scale;
    c->wdr_cfg.out_sel   = s->wdr_cfg.wdr_output_select;

    memcpy(c->gamma_cfg.gamma_tbl, s->gamma_cfg.gamma_tbl,
           sizeof(c->gamma_cfg.gamma_tbl));

    /* Destination-pointer presence: the SDK table pointers gate both the
     * clean copy and its table_update bit, so mirror them (NULL == absent). */
    c->cem_dst   = s->colour_enhance_table;
    c->drc_dst   = s->drc_table;
    c->pltm_dst  = s->pltm_table;
    c->wdr_dst   = s->wdr_table;
    c->lens_dst  = s->lens_table;
    c->gamma_dst = s->gamma_table;

    /* msc banks + pointer gate */
    memcpy(c->msc_cfg.blw,     s->mesh_shading_cfg.mesh_shading_blw_lut,     sizeof(c->msc_cfg.blw));
    memcpy(c->msc_cfg.blh,     s->mesh_shading_cfg.mesh_shading_blh_lut,     sizeof(c->msc_cfg.blh));
    memcpy(c->msc_cfg.blw_dlt, s->mesh_shading_cfg.mesh_shading_blw_delta_lut, sizeof(c->msc_cfg.blw_dlt));
    memcpy(c->msc_cfg.blh_dlt, s->mesh_shading_cfg.mesh_shading_blh_delta_lut, sizeof(c->msc_cfg.blh_dlt));
    c->msc_table = (const uint16_t *)s->mesh_shading_table;

    /* satu_src is the clean copy destination: bind it to the SDK target so the
     * table lands in the SDK buffer directly, as in the vendor routine. */
    c->satu_cfg.satu_r = s->saturation_cfg.saturation_r;
    c->satu_cfg.satu_g = s->saturation_cfg.saturation_g;
    c->satu_cfg.satu_b = s->saturation_cfg.saturation_b;
    c->satu_cfg.mode   = s->mode_cfg.saturation_mode;
    memcpy(c->satu_cfg.table, s->saturation_cfg.saturation_table,
           sizeof(c->satu_cfg.table));
    c->satu_src = s->saturation_table;

    /* Vendor direction is linear_table -> fe_table; fe_table is written back. */
    if (s->linearize_table)
        memcpy(c->linear_src, s->linearize_table, ISP_LINEAR_TBL_SIZE);

    /* Pre-seed destinations from the SDK targets so a module that does not run
     * leaves its target unchanged on write-back, as the vendor does. */
    if (s->fe_table)
        memcpy(c->fe_table, s->fe_table, ISP_LINEAR_TBL_SIZE);
    if (s->colour_enhance_table)
        memcpy(c->cem_table, s->colour_enhance_table, ISP_CEM_TBL_SIZE);
    if (s->drc_table)
        memcpy(c->drc_table, s->drc_table, ISP_DRC_TBL_SIZE);
    if (s->pltm_table)
        memcpy(c->pltm_table, s->pltm_table, ISP_PLTM_TBL_SIZE);
    if (s->wdr_table)
        memcpy(c->wdr_cfg.wdr_table, s->wdr_table, ISP_WDR_TBL_SIZE);
    if (s->gamma_table)
        memcpy(c->gamma_cfg.gamma_packed, s->gamma_table, ISP_GAMMA_TBL_SIZE);
    memcpy(c->d3d_lum_th_lut, s->denoise_3d_cfg.temporal_denoise_luminance_threshold, ISP_LUT_TH_BYTES);
    memcpy(c->d3d_bright_th_lut, s->denoise_3d_cfg.temporal_denoise_bri_threshold, ISP_LUT_TH_BYTES);
}

/* ------------------------------------------------------------------ */
/* Translation: clean -> SDK                                           */
/* ------------------------------------------------------------------ */
/* Only table_update, table targets and the two d3d outputs are written back;
 * lens_table is not (the vendor isp_reg_prepare_lens copies no table). */
static void clean_to_sdk(struct fwi_hw_module_cfg *s,
                         const fwi_mod_config_t *c)
{
    s->table_update = c->table_update;

    if (s->linearize_table && s->fe_table)
        memcpy(s->fe_table, c->fe_table, ISP_LINEAR_TBL_SIZE);
    if (s->colour_enhance_table)
        memcpy(s->colour_enhance_table, c->cem_table, ISP_CEM_TBL_SIZE);
    if (s->drc_table)
        memcpy(s->drc_table, c->drc_table, ISP_DRC_TBL_SIZE);
    if (s->pltm_table)
        memcpy(s->pltm_table, c->pltm_table, ISP_PLTM_TBL_SIZE);
    if (s->wdr_table)
        memcpy(s->wdr_table, c->wdr_cfg.wdr_table, ISP_WDR_TBL_SIZE);
    if (s->gamma_table)
        memcpy(s->gamma_table, c->gamma_cfg.gamma_packed, ISP_GAMMA_TBL_SIZE);
    /* saturation_table is written in place through c->satu_src. */
    memcpy(s->denoise_3d_cfg.temporal_denoise_luminance_threshold, c->d3d_lum_th_lut,  ISP_LUT_TH_BYTES);
    memcpy(s->denoise_3d_cfg.temporal_denoise_bri_threshold, c->d3d_bright_th_lut, ISP_LUT_TH_BYTES);
}

/* ------------------------------------------------------------------ */
/* SDK entry points                                                    */
/* ------------------------------------------------------------------ */
/* ~67 KB context, kept off the stack (one ISP instance at a time). */
static fwi_mod_config_t g_clean;

void isp_map_addr(struct fwi_hw_module_cfg *cfg, unsigned long vaddr)
{
    if (!cfg)
        return;

    memset(&g_clean, 0, sizeof(g_clean));
    g_clean.isp_dev_id = cfg->dev_id;
    clean_isp_map_addr(&g_clean, (void *)vaddr);
}

void isp_hardware_update(struct fwi_hw_module_cfg *cfg)
{
    if (!cfg)
        return;

    sdk_to_clean(&g_clean, cfg);
    clean_isp_hardware_update(&g_clean);
    clean_to_sdk(cfg, &g_clean);
}
