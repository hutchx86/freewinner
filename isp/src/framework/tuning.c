// SPDX-License-Identifier: AGPL-3.0-only
/* tuning.c - enable mapping, library defaults, module-config derivation,
 * per-module refresh, configuration entry points and the tuning store */
#include "framework_internal.h"
#include <stdlib.h>
#include <string.h>

unsigned int isp_lib_log_param;

/* ------------------------------------------------------------------ */
/* enable mapping                                                      */
/* ------------------------------------------------------------------ */
static void set_bit(uint32_t *f, int en, uint32_t bit)
{
    if (en)
        *f |= bit;
    else
        *f &= ~bit;
}

void isp_enable_mapping(fwi_isp_ctx_t *ctx)
{
    fwi_tuning_enables_t *T = &ctx->tuning.enables;
    fwi_hw_module_cfg_t *M = &ctx->hw_cfg;
    int wdr = ctx->sensor.wdr_mode;

    if (wdr == 0)
        T->wdr_merge_en = 0;

    set_bit(&M->module_enable_flag, T->sharpen_en, FWI_ISP_FEATURES_SHARP);
    set_bit(&M->module_enable_flag, T->local_contrast_en, FWI_ISP_FEATURES_CONTRAST);
    set_bit(&M->module_enable_flag, T->denoise_2d_en, FWI_ISP_FEATURES_D2D);
    set_bit(&M->module_enable_flag, T->drc_en, FWI_ISP_FEATURES_RGB_DRC);
    set_bit(&M->module_enable_flag, T->lens_shading_en, FWI_ISP_FEATURES_LENS_SHADING);
    set_bit(&M->module_enable_flag, T->mesh_shading_en, FWI_ISP_FEATURES_MESH_SHADING);
    set_bit(&M->module_enable_flag, 1, FWI_ISP_FEATURES_RGB2YUV);
    if (wdr != 0) {
        M->module_enable_flag |= FWI_ISP_FEATURES_GAMMA;
        M->module_enable_flag |= FWI_ISP_FEATURES_RGB2RGB;
    } else {
        set_bit(&M->module_enable_flag, T->gamma_en, FWI_ISP_FEATURES_GAMMA);
        set_bit(&M->module_enable_flag, T->colour_matrix_en, FWI_ISP_FEATURES_RGB2RGB);
    }
    set_bit(&M->module_enable_flag, T->black_level_en, FWI_ISP_FEATURES_BLACK_LEVEL);
    set_bit(&M->module_enable_flag, T->wb_gain_en, FWI_ISP_FEATURES_WB);
    set_bit(&M->module_enable_flag, T->defect_pixel_en, FWI_ISP_FEATURES_DEFECT_PIXEL);
    set_bit(&M->module_enable_flag, T->demosaic_en, FWI_ISP_FEATURES_DEMOSAIC);
    set_bit(&M->module_enable_flag, T->denoise_3d_en, FWI_ISP_FEATURES_D3D);
    set_bit(&M->module_enable_flag, T->chroma_denoise_en, FWI_ISP_FEATURES_CHROMA_DENOISE);
    set_bit(&M->module_enable_flag, T->lateral_ca_en, FWI_ISP_FEATURES_LATERAL_CA);
    set_bit(&M->module_enable_flag, T->green_ca_en, FWI_ISP_FEATURES_GREEN_CA);
    set_bit(&M->module_enable_flag, T->saturation_en, FWI_ISP_FEATURES_SATURATION);
    set_bit(&M->module_enable_flag, T->linearize_en, FWI_ISP_FEATURES_LINEARIZE);
    set_bit(&M->module_enable_flag, T->digital_gain_en, FWI_ISP_FEATURES_DG);
    set_bit(&M->module_enable_flag, T->colour_enhance_en, FWI_ISP_FEATURES_COLOUR_ENHANCE);
    set_bit(&M->module_enable_flag, T->local_tone_en, FWI_ISP_FEATURES_PLTM);
    set_bit(&M->module_enable_flag, T->wdr_merge_en, FWI_ISP_FEATURES_WDR);
    set_bit(&M->module_enable_flag, T->sensor_offset_en, FWI_ISP_FEATURES_SENSOR_OFFSET);
    set_bit(&M->module_enable_flag, T->crosstalk_en, FWI_ISP_FEATURES_CROSSTALK);
    set_bit(&M->module_enable_flag, T->flicker_detect_en, FWI_ISP_FEATURES_AFS);
    set_bit(&M->module_enable_flag, T->auto_exposure_en, FWI_ISP_FEATURES_AE);
    set_bit(&M->module_enable_flag, T->auto_wb_en, FWI_ISP_FEATURES_AWB);
    set_bit(&M->module_enable_flag, T->auto_focus_en, FWI_ISP_FEATURES_AF);
    set_bit(&M->module_enable_flag, T->histogram_en, FWI_ISP_FEATURES_HIST);
    M->module_enable_flag |= FWI_ISP_FEATURES_MODE;

    isp_lib_log_param = (unsigned int)T->debug_log_mask;
}

/* ------------------------------------------------------------------ */
/* library defaults                                                    */
/* ------------------------------------------------------------------ */
void isp_library_defaults(fwi_isp_ctx_t *ctx)
{
    fwi_adjust_setting_t *adj = &ctx->adjust_ctl;
    fwi_tune_setting_t *tune = &ctx->picture_ctl;
    fwi_awb_setting_t *awb = &ctx->awb_ctl;
    fwi_ae_controls_t *ae = &ctx->ae_ctl;
    fwi_af_settings_t *af = &ctx->af_ctl;
    fwi_stats_ctx_t *st = &ctx->stats;
    int i;
    static const int k[6] = { 5, 10, 16, 24, 30, 64 };
    static const int tdnf_comp[4] = { 52, 26, -26, -77 };
    static const int tdnf_diff[4] = { 39, 26, 0, 0 };
    static const int lp_ratio[4] = { 52, 26, -26, -52 };
    static const int sharp_hf[4] = { 52, 26, -26, -52 };
    static const int sharp_edge[4] = { 26, 13, -13, -26 };
    static const int sharp_us[4] = { -77, -26, 13, 26 };

    memset(adj, 0, sizeof(*adj));
    memset(tune, 0, sizeof(*tune));
    tune->sharpness_level = 100;
    tune->saturation_level = 100;
    tune->denoise_3d_level = 50;
    tune->denoise_level = 50;
    tune->effect = FWI_ISP_COLORFX_NONE;

    awb->wb_gain_manual.r_gain = 256;
    awb->wb_gain_manual.gr_gain = 256;
    awb->wb_gain_manual.gb_gain = 256;
    awb->wb_gain_manual.b_gain = 256;
    awb->wb_mode = FWI_WB_AUTO;
    awb->white_balance_lock = 0;
    awb->awb_coord.x1 = H3A_PIC_OFFSET;
    awb->awb_coord.y1 = 0;
    awb->awb_coord.x2 = awb->awb_coord.y2 = H3A_PIC_OFFSET + H3A_PIC_SIZE;

    ae->exposure_mode = FWI_EXPOSURE_AUTO;
    ae->exposure_metering_mode = FWI_AE_METERING_MODE_MATRIX;
    ae->flash_mode = FWI_FLASH_MODE_NONE;
    ae->flash_switch_flag = 0;
    ae->flash_open = 0;
    ae->exposure_lock = 0;
    ae->exposure_compensation = 0;
    ae->flicker_mode = ISP_FREQUENCY_AUTO;
    ae->iso_mode = FWI_ISO_AUTO;
    ae->ae_coord.x1 = ae->ae_coord.y1 = H3A_PIC_OFFSET;
    ae->ae_coord.x2 = ae->ae_coord.y2 = H3A_PIC_OFFSET + H3A_PIC_SIZE;

    af->af_mode = FWI_AUTO_FOCUS_CONTINUOUS;
    af->af_metering_mode = FWI_AUTO_FOCUS_METERING_CENTER_WEIGHTED;
    af->focus_lock = 0;
    af->af_range = FWI_AUTO_FOCUS_RANGE_AUTO;
    af->af_coord.x1 = af->af_coord.y1 = H3A_PIC_OFFSET;
    af->af_coord.x2 = af->af_coord.y2 = H3A_PIC_OFFSET + H3A_PIC_SIZE;

    ctx->drv_stats_ref.min_rgb_saved = 1023;
    ctx->drv_stats_ref.c_noise_saved = 20;

    st->wb_gain_saved.r_gain = 256;
    st->wb_gain_saved.gr_gain = 256;
    st->wb_gain_saved.gb_gain = 256;
    st->wb_gain_saved.b_gain = 256;
    st->dynamic_stats.enable = 0;
    for (i = 0; i < 6; i++)
        st->dynamic_stats.mov_threshold[i] = k[i] * 14 * 14;
    for (i = 0; i < 4; i++) {
        st->dynamic_stats.temporal_denoise_comp[i] = tdnf_comp[i];
        st->dynamic_stats.temporal_denoise_diff_comp[i] = tdnf_diff[i];
        st->dynamic_stats.lp_threshold_ratio_comp[i] = lp_ratio[i];
        st->dynamic_stats.sharp_high_frequency_comp[i] = sharp_hf[i];
        st->dynamic_stats.sharp_edge_comp[i] = sharp_edge[i];
        st->dynamic_stats.sharp_under_shoot_comp[i] = sharp_us[i];
    }
}

/* ------------------------------------------------------------------ */
/* module-config derivation                                            */
/* ------------------------------------------------------------------ */
static int clamp4095(int v)
{
    if (v < 0)
        return 0;
    if (v > 4095)
        return 4095;
    return v;
}

/* f(k, x) = clamp(k / max(x, 1), 0, 4095). */
static uint16_t msc_delta(int k, int32_t x)
{
    if (x < 1)
        x = 1;
    return (uint16_t)clamp4095(k / x);
}

/* 12-entry delta LUT from the copied module-config LUT: msc_mode >= 4 covers i = 1..11 (11 is
 * the unwritten 12th slot, normally 0); otherwise i = 1..7 plus dlt[8] = f(4096, lut[7]). */
static void msc_fill_delta(const uint16_t *lut, uint16_t *dlt, int mode)
{
    int i;
    dlt[0] = msc_delta(4096, lut[0]);
    if (mode >= 4) {
        for (i = 1; i <= 11; i++)
            dlt[i] = msc_delta(8192, (int32_t)lut[i] + lut[i - 1]);
    } else {
        for (i = 1; i <= 7; i++)
            dlt[i] = msc_delta(8192, (int32_t)lut[i] + lut[i - 1]);
        dlt[8] = msc_delta(4096, lut[7]);
    }
}

void isp_module_config_derive(fwi_isp_ctx_t *ctx)
{
    fwi_tuning_enables_t *T = &ctx->tuning.enables;
    fwi_tuning_3a_t *A = &ctx->tuning.a3;
    fwi_tuning_modules_t *U = &ctx->tuning.modules;
    fwi_hw_module_cfg_t *M = &ctx->hw_cfg;
    fwi_ae_param_t *P = ctx->ae_entity.ae_param;
    int i;

    if (T->wb_gain_en == 0 || M->wb_gain_cfg.wb_gain.r_gain == 0 ||
        M->wb_gain_cfg.wb_gain.gr_gain == 0 ||
        M->wb_gain_cfg.wb_gain.gb_gain == 0 ||
        M->wb_gain_cfg.wb_gain.b_gain == 0) {
        M->wb_gain_cfg.wb_gain.r_gain = 256;
        M->wb_gain_cfg.wb_gain.gr_gain = 256;
        M->wb_gain_cfg.wb_gain.gb_gain = 256;
        M->wb_gain_cfg.wb_gain.b_gain = 256;
    }
    M->wb_gain_cfg.clip_val = 4095;
    M->af_cfg.af_sap_limit = ISP_AF_DIR_TH;
    M->denoise_3d_cfg.k3d_increase_mode = FWI_D3D_MAX;
    M->hist_cfg.hist_mode = FWI_MAX_MODE;
    M->mode_cfg.input_fmt = ctx->sensor.input_seq;

    if (ctx->sensor.wdr_mode == 2) {
        M->mode_cfg.wdr_mode = FWI_COMMANDING_WDR;
        ctx->ae_ctl.ae_mode = FWI_AE_NORM;
    } else if (ctx->sensor.wdr_mode == 1) {
        M->mode_cfg.wdr_mode = FWI_DOL_WDR;
        ctx->ae_ctl.ae_mode = FWI_AE_WDR;
    } else {
        ctx->ae_ctl.ae_mode = FWI_AE_NORM;
    }
    M->mode_cfg.wdr_cmp_mode = 0;

    if (P) {
        P->commanding_input_bits = 12;
        P->commanding_output_bits = 15;
        P->nor_cmd_mode = 0;
    }
    config_wdr(ctx, 1);

    M->mode_cfg.saturation_mode = FWI_SATURATION_NORM_MODE;
    M->mode_cfg.hist_select = A->ae_hist_select;
    M->mode_cfg.demosaic_mode = FWI_DEMOSAIC_NORM_MODE;
    M->mode_cfg.ae_mode = A->ae_stat_select;
    M->mode_cfg.awb_mode = A->awb_stat_select;
    M->mode_cfg.dg_mode = (ctx->ae_ctl.ae_mode == FWI_AE_WDR)
                              ? FWI_DG_BEFORE_WDR
                              : FWI_DG_AFTER_SENSOR_OFFSET;
    M->output_speed = 1;

    config_dig_gain(ctx, 1024);

    if (T->wdr_merge_en) {
        int sh = 16 - (P ? P->commanding_output_bits : 15);
        M->awb_cfg.awb_r_saturation_limit = (uint16_t)(AWB_SAT_DEF_LIM >> sh);
        M->awb_cfg.awb_g_saturation_limit = (uint16_t)(AWB_SAT_DEF_LIM >> sh);
        M->awb_cfg.awb_b_saturation_limit = (uint16_t)(AWB_SAT_DEF_LIM >> sh);
    } else {
        M->awb_cfg.awb_r_saturation_limit = AWB_SAT_DEF_LIM;
        M->awb_cfg.awb_g_saturation_limit = AWB_SAT_DEF_LIM;
        M->awb_cfg.awb_b_saturation_limit = AWB_SAT_DEF_LIM;
    }

    if (A->ae_hist_mode_en == 1)
        M->hist_cfg.hist_mode = FWI_MAX_MODE;
    else if (A->ae_hist_mode_en == 2)
        M->hist_cfg.hist_mode = FWI_MIN_MODE;
    else if (A->ae_hist_mode_en == 3)
        M->hist_cfg.hist_mode = FWI_AVERAGE_MODE;

    M->demosaic_cfg.dir_threshold =
        (uint16_t)(U->demosaic_dir_threshold ? U->demosaic_dir_threshold
                                             : ISP_CFA_DIR_TH);
    M->demosaic_cfg.interp_mode =
        (uint8_t)(U->demosaic_interp_mode ? U->demosaic_interp_mode
                                          : ISP_CFA_INTERP_MODE);
    M->demosaic_cfg.zig_zag = (uint8_t)(U->demosaic_zig_zag ? U->demosaic_zig_zag
                                                            : ISP_CFA_ZIG_ZAG);
    M->rgb2rgb_cfg.colour_matrix = U->colour_matrix_init[0];
    M->demosaic_cfg.min_rgb = T->dehaze_en ? 1023 : 0;
    M->crosstalk_cfg.crosstalk_threshold_max = U->crosstalk_threshold_max;
    M->crosstalk_cfg.crosstalk_threshold_min = U->crosstalk_threshold_min;
    M->crosstalk_cfg.crosstalk_threshold_slope = U->crosstalk_threshold_slope;
    M->crosstalk_cfg.crosstalk_dir_wt = U->crosstalk_dir_wt;
    M->crosstalk_cfg.crosstalk_dir_threshold = U->crosstalk_dir_threshold;

    M->pltm_cfg.lss_switch = (uint8_t)U->local_tone_static[FWI_ISP_PLTM_LSS_SWITCH];
    M->pltm_cfg.last_order_ratio =
        (uint8_t)U->local_tone_static[FWI_ISP_PLTM_LAST_ORDER_RATIO];
    M->pltm_cfg.tr_order = (uint8_t)U->local_tone_static[FWI_ISP_PLTM_TR_ORDER];
    M->pltm_cfg.original_picture_ratio =
        (uint8_t)U->local_tone_static[FWI_ISP_PLTM_ORIGINAL_PICTURE_RATIO];
    M->pltm_cfg.intens_asym = (uint8_t)U->local_tone_static[FWI_ISP_PLTM_INTENS_ASYM];
    M->pltm_cfg.spatial_asm = (uint8_t)U->local_tone_static[FWI_ISP_PLTM_SPATIAL_ASM];
    M->pltm_cfg.white_level = (uint8_t)U->local_tone_static[FWI_ISP_PLTM_WHITE_LEVEL];
    M->pltm_cfg.lp_halo_res = (uint8_t)U->local_tone_static[FWI_ISP_PLTM_LP_HALO_RES];
    M->pltm_cfg.luminance_ratio =
        (uint8_t)U->local_tone_static[FWI_ISP_PLTM_LUMINANCE_RATIO];
    M->pltm_cfg.block_v_count =
        (uint8_t)U->local_tone_static[FWI_ISP_PLTM_BLOCK_V_COUNT];
    M->pltm_cfg.block_h_count =
        (uint8_t)U->local_tone_static[FWI_ISP_PLTM_BLOCK_H_COUNT];

    M->green_ca_cfg.green_ca_ct_w =
        (uint16_t)((ctx->drv_stats_ref.pic_size.width - 1) / 2);
    M->green_ca_cfg.green_ca_ct_h =
        (uint16_t)((ctx->drv_stats_ref.pic_size.height - 1) / 2);
    M->green_ca_cfg.green_ca_r_para0 = (uint16_t)U->green_ca[0];
    M->green_ca_cfg.green_ca_r_para1 = (uint16_t)U->green_ca[1];
    M->green_ca_cfg.green_ca_r_para2 = (uint16_t)U->green_ca[2];
    M->green_ca_cfg.green_ca_b_para0 = (uint16_t)U->green_ca[3];
    M->green_ca_cfg.green_ca_b_para1 = (uint16_t)U->green_ca[4];
    M->green_ca_cfg.green_ca_b_para2 = (uint16_t)U->green_ca[5];
    M->green_ca_cfg.green_ca_int_cns = (uint16_t)U->green_ca[6];

    for (i = 0; i < ISP_REG_TBL_LENGTH; i++) {
        M->lateral_ca_cfg.lateral_ca_pf_saturation_lut[i] =
            (uint8_t)U->lateral_ca_pf_saturation_lut[i];
        M->lateral_ca_cfg.lateral_ca_gf_saturation_lut[i] =
            (uint8_t)U->lateral_ca_gf_saturation_lut[i];
    }

    if (T->lens_shading_en)
        M->mode_cfg.rsc_mode = U->lens_shading_mode;

    if (T->mesh_shading_en) {
        int mode = U->mesh_shading_mode;
        M->mode_cfg.mesh_shading_mode = mode;
        for (i = 0; i < 11; i++) {
            M->mesh_shading_cfg.mesh_shading_blw_lut[i] =
                (uint16_t)U->mesh_shading_blw_lut[i];
            M->mesh_shading_cfg.mesh_shading_blh_lut[i] =
                (uint16_t)U->mesh_shading_blh_lut[i];
        }
        msc_fill_delta(M->mesh_shading_cfg.mesh_shading_blw_lut,
                       M->mesh_shading_cfg.mesh_shading_blw_delta_lut, mode);
        msc_fill_delta(M->mesh_shading_cfg.mesh_shading_blh_lut,
                       M->mesh_shading_cfg.mesh_shading_blh_delta_lut, mode);
        for (i = 0; i < ISP_MSC_TBL_LENGTH; i++) {
            ctx->shading_golden[i] = 1.0f;
            ctx->shading_golden_flag[i] = 1;
        }
        for (i = 0; i < 6; i++) {
            ctx->shading_adjust[i] = 0.0f;
            ctx->shading_adjust_low[i] = 0.0f;
        }
    }

    config_band_step(ctx);
    if (T->gamma_en)
        config_gamma(ctx);
    if (T->lens_shading_en) {
        config_lens_table(ctx, 512);
        config_lens_center(ctx);
    }
    if (T->mesh_shading_en)
        config_msc_table(ctx, 512);

    memcpy(M->colour_enhance_cfg.colour_enhance_table, U->colour_enhance_table,
           ISP_CEM_MEM_SIZE);
}

/* ------------------------------------------------------------------ */
/* per-module parameter refresh                                        */
/* ------------------------------------------------------------------ */
void isp_module_refresh(fwi_isp_ctx_t *ctx)
{
    fwi_tuning_enables_t *T = &ctx->tuning.enables;
    fwi_tuning_3a_t *A = &ctx->tuning.a3;
    fwi_tuning_modules_t *U = &ctx->tuning.modules;
    fwi_hw_module_cfg_t *M = &ctx->hw_cfg;

    /* ISO refresh: set the param type and call iso_set_params, raise the 16 *_adjust flags,
     * af_cfg_adjust, test_cfg.test_mode and the four denoise ratios, then run ISO once. */
    if (ctx->iso_entity.ops) {
        fwi_iso_cfg_core_ops_t *ops = fwi_iso_ops(ctx->iso_entity.ops);
        fwi_iso_param_t *p = ctx->iso_entity.iso_param;

        if (p != NULL) {
            p->gen = ctx;
            if (ops->iso_set_params) {
                p->type = (uint8_t)FWI_ISP_ISO_UPDATE_PARAMS;
                ops->iso_set_params(ctx->iso_entity.iso_entity, p,
                                    &ctx->iso_entity.iso_result);
            }
            p->chroma_denoise_adjust = 1;        /* cnr_adjust */
            p->sharpness_adjust = 1;
            p->saturation_adjust = 1;
            p->contrast_adjust = 1;
            p->brightness_adjust = 1;
            p->colour_enhance_ratio_adjust = 1;  /* cem_ratio_adjust */
            p->denoise_adjust = 1;
            p->sensor_offset_adjust = 1;
            p->black_level_adjust = 1;
            p->defect_pixel_adjust = 1;          /* dpc_adjust */
            p->dehaze_value_adjust = 1;          /* defog_value_adjust */
            p->pltm_dynamic_cfg_adjust = 1;
            p->tdnr_adjust = 1;
            p->ae_cfg_adjust = 1;
            p->gtm_cfg_adjust = 1;
            p->lateral_ca_cfg_adjust = 1;        /* lca_cfg_adjust */
            p->af_cfg_adjust =
                (uint32_t)(T->auto_focus_en == 1 || T->sweep_focus_en == 1);
            p->test_cfg.test_mode = T->bench_mode;
            p->denoise_lp0_np_core_ratio = 16;
            p->denoise_lp1_np_core_ratio = 20;
            p->denoise_lp2_np_core_ratio = 24;
            p->denoise_lp3_np_core_ratio = 28;
        }
        if (ops->iso_run)
            ops->iso_run(ctx->iso_entity.iso_entity,
                         &ctx->iso_entity.iso_result);
    }

    /* AE */
    if (ctx->ae_entity.ae_param) {
        fwi_ae_param_t *p = ctx->ae_entity.ae_param;
        p->ae_sensor_info = ctx->sensor;
        p->ae_init.define_ae_table = A->define_ae_table;
        p->ae_init.ae_max_level = A->ae_max_level;
        p->ae_init.ae_hist_mode_en = A->ae_hist_mode_en;
        p->ae_init.ae_ki = A->ae_ki;
        p->ae_init.ae_conv_data_index = A->ae_conv_data_index;
        p->ae_init.ae_highlight_guard_en = A->ae_highlight_guard_en;
        p->ae_init.ae_highlight_guard_level = A->ae_highlight_guard_level;
        p->ae_init.ae_delay_frame = A->ae_delay_frame;
        p->ae_init.exposure_delay_frame = A->exposure_delay_frame;
        p->ae_init.gain_delay_frame = A->gain_delay_frame;
        p->ae_init.exposure_comp_step = A->exposure_comp_step;
        p->ae_init.ae_touch_distance_ind = A->ae_touch_distance_ind;
        p->ae_init.ae_iso2gain_ratio = A->ae_iso2gain_ratio;
        p->ae_setting.flicker_type = (uint8_t)U->flicker_type;
        A->wdr_cfg[FWI_WDR_MODE] = 0; /* zero the source before it is copied */
        p->ae_init.gain_ratio = 0;
        memcpy(p->ae_init.ae_analog_gain_range, A->ae_analog_gain_range,
               sizeof(p->ae_init.ae_analog_gain_range));
        memcpy(p->ae_init.ae_digital_gain_range, A->ae_digital_gain_range,
               sizeof(p->ae_init.ae_digital_gain_range));
        p->ae_init.ae_digital_gain_range[0] = 1024;
        p->ae_init.ae_digital_gain_range[1] = 3072;
        memcpy(p->ae_init.ae_f_number_step, A->ae_f_number_step,
               sizeof(p->ae_init.ae_f_number_step));
        memcpy(p->ae_init.wdr_cfg, A->wdr_cfg, sizeof(p->ae_init.wdr_cfg));
        /* AE scene tables — scene 0 = preview, 1 = capture,
         * 2 = video; copy all 42 s32 of each source table. */
        p->ae_init.ae_tbl_scene[0].length = A->ae_table_preview_length;
        memcpy(p->ae_init.ae_tbl_scene[0].ae_tbl, A->ae_table_preview,
               sizeof(A->ae_table_preview));
        p->ae_init.ae_tbl_scene[1].length = A->ae_table_capture_length;
        memcpy(p->ae_init.ae_tbl_scene[1].ae_tbl, A->ae_table_capture,
               sizeof(A->ae_table_capture));
        p->ae_init.ae_tbl_scene[2].length = A->ae_table_video_length;
        memcpy(p->ae_init.ae_tbl_scene[2].ae_tbl, A->ae_table_video,
               sizeof(A->ae_table_video));
        memcpy(p->ae_init.ae_zone_weight, A->ae_zone_weight,
               sizeof(p->ae_init.ae_zone_weight));
        isp_ae_set_params_helper(&ctx->ae_entity, FWI_ISP_AE_INIT_DATA);
        /* AE test_cfg, written after INIT_DATA (refresh order). */
        p->test_cfg.test_mode = T->bench_mode;
        p->test_cfg.gain = T->fixed_gain;
        p->test_cfg.exposure_line = T->fixed_exposure_lines;
        p->test_cfg.ae_forced = T->ae_hold;
        p->test_cfg.luminance_forced = T->luma_hold;
        p->test_cfg.test_exposure_time = T->sweep_exposure_en;
        p->test_cfg.exposure_line_start = T->sweep_exposure_first;
        p->test_cfg.exposure_line_step = T->sweep_exposure_step;
        p->test_cfg.exposure_line_end = T->sweep_exposure_last;
        p->test_cfg.exposure_change_interval = T->sweep_exposure_period;
        p->test_cfg.test_gain = T->sweep_gain_en;
        p->test_cfg.gain_start = T->sweep_gain_first;
        p->test_cfg.gain_step = T->sweep_gain_step;
        p->test_cfg.gain_end = T->sweep_gain_last;
        p->test_cfg.gain_change_interval = T->sweep_gain_period;
        p->test_cfg.ae_en = T->auto_exposure_en;
        p->test_cfg.ae_check_delay_en = 0;
        p->test_cfg.ae_delay_type = FWI_ISP_AE_DELAY_WDR_AGAIN;
        isp_ae_set_params_helper(&ctx->ae_entity, FWI_ISP_AE_UPDATE_AE_TABLE);
        ctx->picture_ctl.gains.ana_gain_min = p->ae_init.ae_analog_gain_range[0];
        ctx->picture_ctl.gains.ana_gain_max = p->ae_init.ae_analog_gain_range[1];
        ctx->picture_ctl.gains.dig_gain_min = p->ae_init.ae_digital_gain_range[0] / 4;
        ctx->picture_ctl.gains.dig_gain_max = p->ae_init.ae_digital_gain_range[1] / 4;
    }

    /* AWB */
    if (ctx->awb_entity.awb_param) {
        fwi_awb_param_t *p = ctx->awb_entity.awb_param;
        p->awb_init.awb_period_frames = A->awb_period_frames;
        p->awb_init.awb_step_speed = A->awb_step_speed;
        p->awb_init.awb_colour_temper_low = A->awb_colour_temper_low;
        p->awb_init.awb_colour_temper_high = A->awb_colour_temper_high;
        p->awb_init.awb_base_temper = A->awb_base_temper;
        p->awb_init.awb_green_zone_distance = A->awb_green_zone_distance;
        p->awb_init.awb_blue_sky_distance = A->awb_blue_sky_distance;
        p->awb_init.awb_illum_count = A->awb_illum_count;
        p->awb_init.awb_extra_illum_count = A->awb_extra_illum_count;
        p->awb_init.awb_skin_count = A->awb_skin_count;
        p->awb_init.awb_special_count = A->awb_special_count;
        memcpy(p->awb_init.awb_illum, A->awb_illum, sizeof(p->awb_init.awb_illum));
        memcpy(p->awb_init.awb_extra_illum, A->awb_extra_illum,
               sizeof(p->awb_init.awb_extra_illum));
        memcpy(p->awb_init.awb_skin, A->awb_skin, sizeof(p->awb_init.awb_skin));
        memcpy(p->awb_init.awb_special, A->awb_special,
               sizeof(p->awb_init.awb_special));
        memcpy(p->awb_init.awb_preference_gain, A->awb_preference_gain,
               sizeof(p->awb_init.awb_preference_gain));
        p->test_cfg.awb_en = T->auto_wb_en;
        p->test_cfg.colour_temp = T->fixed_cct_k;
        p->test_cfg.test_mode = T->manual_mode_en;
        if (ctx->awb_entity.ops)
            ctx->awb_entity.ops->awb_set_params(ctx->awb_entity.awb_entity, p,
                                                &ctx->awb_entity.awb_result);
    }

    /* GTM */
    if (ctx->gtm_entity.gtm_param) {
        fwi_gtm_param_t *p = ctx->gtm_entity.gtm_param;
        p->gtm_enable = (uint8_t)T->global_tone_en;
        p->gamma_tbl = M->gamma_cfg.gamma_tbl;
        p->drc_table = M->drc_cfg.drc_table;
        p->drc_table_last = M->drc_cfg.drc_table_last;
        p->gtm_init.gtm_type = (uint8_t)U->gtm_type;
        p->gtm_init.gamma_type = (uint8_t)U->gamma_type;
        p->gtm_init.auto_alpha_en = (uint32_t)U->auto_alpha_en;
        memcpy(p->gtm_init.gtm_cfg, ctx->ae_ctl.ae_hist_eq_cfg,
               sizeof(p->gtm_init.gtm_cfg));
        p->test_cfg.test_mode = T->manual_mode_en;
        if (ctx->gtm_entity.ops)
            ctx->gtm_entity.ops->gtm_set_params(ctx->gtm_entity.gtm_entity, p,
                                                &ctx->gtm_entity.gtm_result);
    }

    /* PLTM */
    if (ctx->pltm_entity.pltm_param) {
        fwi_pltm_param_t *p = ctx->pltm_entity.pltm_param;
        p->pltm_enable = (uint8_t)T->local_tone_en;
        p->ae_enable = (uint8_t)T->auto_exposure_en;
        p->sensor_info = ctx->sensor;
        p->pltm_table = (uint16_t *)U->pltm_table;
        memcpy(p->pltm_init.pltm_cfg, U->local_tone_static,
               sizeof(p->pltm_init.pltm_cfg));
        memcpy(p->pltm_init.pltm_dynamic_cfg, ctx->ae_ctl.pltm_dynamic_cfg,
               sizeof(p->pltm_init.pltm_dynamic_cfg));
        if (ctx->pltm_entity.ops)
            ctx->pltm_entity.ops->pltm_set_params(
                ctx->pltm_entity.pltm_entity, p, &ctx->pltm_entity.pltm_result);
    }

    /* AFS */
    if (ctx->afs_entity.afs_param) {
        fwi_afs_param_t *p = ctx->afs_entity.afs_param;
        p->flicker_ratio = U->flicker_ratio;
        p->flicker_type_init = U->flicker_type;
        p->test_cfg.test_mode = T->manual_mode_en;
        p->test_cfg.afs_en = T->flicker_detect_en;
        if (ctx->afs_entity.ops)
            ctx->afs_entity.ops->afs_set_params(ctx->afs_entity.afs_entity, p,
                                                &ctx->afs_entity.afs_result);
    }
}

/* ------------------------------------------------------------------ */
/* full configuration / partial configuration                          */
/* ------------------------------------------------------------------ */
int isp_config_init(fwi_isp_ctx_t *ctx)
{
    if (ctx == NULL)
        return -1;
    pthread_mutex_lock((pthread_mutex_t *)&ctx->lock);
    if (ctx->sensor.sensor_height == 0) {
        pthread_mutex_unlock((pthread_mutex_t *)&ctx->lock);
        return -1;
    }
    isp_enable_mapping(ctx);
    isp_library_defaults(ctx);
    isp_module_config_derive(ctx);
    isp_module_refresh(ctx);
    ctx->pending_3a_changes =
        0xffffffffu & ~(FWI_ISP_SET_BRIGHTNESS | FWI_ISP_SET_CONTRAST |
                        FWI_ISP_SET_GAIN_STR);
    if (ctx->ir_mode == 1) {
        ctx->pending_3a_changes &= ~FWI_ISP_SET_HUE;
        ctx->picture_ctl.effect = FWI_ISP_COLORFX_GRAY;
    }
    isp_apply_settings(ctx);
    isp_hardware_update(&ctx->hw_cfg);
    memset(ctx->dehaze_state.min_rgb_pre, 0,
           sizeof(ctx->dehaze_state.min_rgb_pre));
    pthread_mutex_unlock((pthread_mutex_t *)&ctx->lock);
    return 0;
}

int isp_config_update(fwi_isp_ctx_t *ctx)
{
    if (ctx == NULL)
        return -1;
    pthread_mutex_lock((pthread_mutex_t *)&ctx->lock);
    isp_enable_mapping(ctx);
    isp_module_config_derive(ctx);
    isp_module_refresh(ctx);
    pthread_mutex_unlock((pthread_mutex_t *)&ctx->lock);
    return 0;
}

int isp_ctx_config_init(fwi_isp_ctx_t *ctx)
{
    return isp_config_init(ctx);
}

int isp_ctx_config_update(fwi_isp_ctx_t *ctx)
{
    return isp_config_update(ctx);
}

/* ------------------------------------------------------------------ */
/* tuning store and set_cfg/get_cfg/update                             */
/* ------------------------------------------------------------------ */
/* HW_ISP_CFG_* group/id values: public, fwi_isp_api.h */

#define AW_ERR_VI_INVALID_PARA       (-1)
#define AW_ERR_VI_INVALID_NULL_PTR   (-4)

struct ccm_record {
    uint16_t temperature;
    fwi_rgb2rgb_gain_offset_t value;
};

struct sharp_record {
    uint16_t value[33];
    uint16_t lum[33];
    uint16_t edge_lum[33];
    uint16_t hfrq_lum[33];
    uint16_t hsv[46];
    uint8_t smap[33];
};

/* group-4 gamma shape */
struct gamma_record {
    int32_t number;
    uint16_t value[5][3072];
    uint16_t lv_triggers[5];
};

int32_t isp_set_cfg(int dev_id, uint8_t group_id, uint32_t cfg_ids,
                    void *cfg_data)
{
    struct fwi_tuning_store *st;
    uint8_t *p = cfg_data;
    int32_t total = 0;

    if (dev_id >= HW_ISP_DEVICE_NUM || dev_id < 0)
        return -1;
    if (media_params.isp_dev[dev_id] == NULL)
        return -1;
    if (cfg_data == NULL)
        return AW_ERR_VI_INVALID_PARA;
    st = isp_tuning_store_get(dev_id);
    if (st == NULL || st->ctx == NULL)
        return AW_ERR_VI_INVALID_NULL_PTR;

    pthread_mutex_lock(&st->lock);
    switch (group_id) {
    case HW_ISP_CFG_TEST:
        if (cfg_ids & HW_ISP_CFG_TEST_ENABLE) {
            memcpy(&st->params.enables.manual_mode_en, p, 128);
            p += 128;
            total += 128;
        }
        break;
    case HW_ISP_CFG_TUNING:
        if (cfg_ids & HW_ISP_CFG_TUNING_FLICKER) {
            st->params.modules.flicker_type = ((int32_t *)p)[0];
            st->params.modules.flicker_ratio = ((int32_t *)p)[1];
            p += 8;
            total += 8;
        }
        {
            static const uint32_t bits[3] = { HW_ISP_CFG_TUNING_CCM_LOW,
                                              HW_ISP_CFG_TUNING_CCM_MID,
                                              HW_ISP_CFG_TUNING_CCM_HIGH };
            int k;
            for (k = 0; k < 3; k++) {
                if (cfg_ids & bits[k]) {
                    struct ccm_record *r = (struct ccm_record *)p;
                    st->params.modules.colour_matrix_trigger[k] = r->temperature;
                    st->params.modules.colour_matrix_init[k] = r->value;
                    p += sizeof(*r);
                    total += sizeof(*r);
                }
            }
        }
        if (cfg_ids & HW_ISP_CFG_TUNING_PLTM) {
            memcpy(st->params.modules.local_tone_static, p,
                   sizeof(st->params.modules.local_tone_static));
            p += sizeof(st->params.modules.local_tone_static);
            total += (int32_t)sizeof(st->params.modules.local_tone_static);
        }
        break;
    case HW_ISP_CFG_TUNING_TABLES:
        if (cfg_ids & HW_ISP_CFG_TUNING_GAMMA) {
            struct gamma_record *r = (struct gamma_record *)p;
            st->params.modules.gamma_count = r->number;
            memcpy(st->params.modules.gamma_tbl_init, r->value,
                   sizeof(r->value));
            memcpy(st->params.modules.gamma_trig_cfg, r->lv_triggers,
                   sizeof(r->lv_triggers));
            p += sizeof(*r);
            total += (int32_t)sizeof(*r);
        }
        if (cfg_ids & HW_ISP_CFG_TUNING_SHARP) {
            struct sharp_record *r = (struct sharp_record *)p;
            memcpy(st->params.modules.sharp_val, r->value, sizeof(r->value));
            memcpy(st->params.modules.sharp_luminance, r->lum, sizeof(r->lum));
            memcpy(st->params.modules.sharp_edge_luminance, r->edge_lum,
                   sizeof(r->edge_lum));
            memcpy(st->params.modules.sharp_high_frequency_luminance,
                   r->hfrq_lum, sizeof(r->hfrq_lum));
            memcpy(st->params.modules.sharp_hsv, r->hsv, sizeof(r->hsv));
            memcpy(st->params.modules.sharp_s_map, r->smap, sizeof(r->smap));
            p += sizeof(*r);
            total += (int32_t)sizeof(*r);
        }
        break;
    case HW_ISP_CFG_3A:
    case HW_ISP_CFG_DYNAMIC:
        total = 0;
        break;
    default:
        total = -1;
        break;
    }
    pthread_mutex_unlock(&st->lock);
    return total;
}

int32_t isp_get_cfg(int dev_id, uint8_t group_id, uint32_t cfg_ids,
                    void *cfg_data)
{
    struct fwi_tuning_store *st;
    uint8_t *p = cfg_data;
    int32_t total = 0;

    if (dev_id >= HW_ISP_DEVICE_NUM || dev_id < 0)
        return -1;
    if (media_params.isp_dev[dev_id] == NULL)
        return -1;
    if (cfg_data == NULL)
        return AW_ERR_VI_INVALID_PARA;
    st = isp_tuning_store_get(dev_id);
    if (st == NULL || st->ctx == NULL)
        return AW_ERR_VI_INVALID_NULL_PTR;

    pthread_mutex_lock(&st->lock);
    switch (group_id) {
    case HW_ISP_CFG_TEST:
        if (cfg_ids & HW_ISP_CFG_TEST_ENABLE) {
            memcpy(p, &st->params.enables.manual_mode_en, 128);
            p += 128;
            total += 128;
        }
        break;
    case HW_ISP_CFG_TUNING:
        if (cfg_ids & HW_ISP_CFG_TUNING_FLICKER) {
            ((int32_t *)p)[0] = st->params.modules.flicker_type;
            ((int32_t *)p)[1] = st->params.modules.flicker_ratio;
            p += 8;
            total += 8;
        }
        {
            static const uint32_t bits[3] = { HW_ISP_CFG_TUNING_CCM_LOW,
                                              HW_ISP_CFG_TUNING_CCM_MID,
                                              HW_ISP_CFG_TUNING_CCM_HIGH };
            int k;
            for (k = 0; k < 3; k++) {
                if (cfg_ids & bits[k]) {
                    struct ccm_record *r = (struct ccm_record *)p;
                    r->temperature = st->params.modules.colour_matrix_trigger[k];
                    r->value = st->params.modules.colour_matrix_init[k];
                    p += sizeof(*r);
                    total += (int32_t)sizeof(*r);
                }
            }
        }
        if (cfg_ids & HW_ISP_CFG_TUNING_PLTM) {
            memcpy(p, st->params.modules.local_tone_static,
                   sizeof(st->params.modules.local_tone_static));
            p += sizeof(st->params.modules.local_tone_static);
            total += (int32_t)sizeof(st->params.modules.local_tone_static);
        }
        break;
    case HW_ISP_CFG_TUNING_TABLES:
        if (cfg_ids & HW_ISP_CFG_TUNING_GAMMA) {
            struct gamma_record *r = (struct gamma_record *)p;
            r->number = st->params.modules.gamma_count;
            memcpy(r->value, st->params.modules.gamma_tbl_init,
                   sizeof(r->value));
            memcpy(r->lv_triggers, st->params.modules.gamma_trig_cfg,
                   sizeof(r->lv_triggers));
            p += sizeof(*r);
            total += (int32_t)sizeof(*r);
        }
        if (cfg_ids & HW_ISP_CFG_TUNING_SHARP) {
            struct sharp_record *r = (struct sharp_record *)p;
            memcpy(r->value, st->params.modules.sharp_val, sizeof(r->value));
            memcpy(r->lum, st->params.modules.sharp_luminance, sizeof(r->lum));
            memcpy(r->edge_lum, st->params.modules.sharp_edge_luminance,
                   sizeof(r->edge_lum));
            memcpy(r->hfrq_lum,
                   st->params.modules.sharp_high_frequency_luminance,
                   sizeof(r->hfrq_lum));
            memcpy(r->hsv, st->params.modules.sharp_hsv, sizeof(r->hsv));
            memcpy(r->smap, st->params.modules.sharp_s_map, sizeof(r->smap));
            p += sizeof(*r);
            total += (int32_t)sizeof(*r);
        }
        break;
    case HW_ISP_CFG_3A:
    case HW_ISP_CFG_DYNAMIC:
        total = 0;
        break;
    default:
        total = -1;
        break;
    }
    pthread_mutex_unlock(&st->lock);
    return total;
}

int isp_update(int dev_id)
{
    struct fwi_tuning_store *st;

    if (dev_id >= HW_ISP_DEVICE_NUM || dev_id < 0)
        return -1;
    if (media_params.isp_dev[dev_id] == NULL)
        return -1;
    st = isp_tuning_store_get(dev_id);
    if (st == NULL || st->ctx == NULL)
        return -1;
    pthread_mutex_lock(&st->lock);
    st->ctx->tuning = st->params;
    pthread_mutex_unlock(&st->lock);
    isp_config_update(st->ctx);
    return 0;
}

int isp_reset(int dev_id, int mode_flag)
{
    struct fwi_tuning_store *st;

    if (dev_id >= HW_ISP_DEVICE_NUM || dev_id < 0)
        return -1;
    if (media_params.isp_dev[dev_id] == NULL)
        return -1;
    st = isp_tuning_store_get(dev_id);
    if (st == NULL || st->ctx == NULL)
        return -1;
    if (mode_flag & 1)
        media_params.wdr_flag = 1;
    if (mode_flag & 2)
        media_params.ir_flag = 1;
    parser_ini_info(&st->ctx->tuning, st->ctx->sensor.name,
                    st->ctx->sensor.sensor_width, st->ctx->sensor.sensor_height,
                    (int)st->ctx->sensor.fps_fixed, media_params.wdr_flag,
                    media_params.ir_flag, 0, dev_id);
    st->params = st->ctx->tuning;
    return isp_config_init(st->ctx);
}

int isp_set_sync(int mode)
{
    media_params.isp_sync_mode = 0;
    (void)mode;
    return 0;
}
