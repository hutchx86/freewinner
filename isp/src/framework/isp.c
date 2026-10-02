// SPDX-License-Identifier: AGPL-3.0-only
/* isp.c - context record, seed, per-frame pipeline and 3A feeds */
#include "framework_internal.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

fwi_isp_ctx_t isp_ctx[1] = {
    [0] = {
    /* seed: only these scalars are non-zero before any call */
    .tuning = {
        .enables = {
            .auto_exposure_en = 1,
            .auto_wb_en = 1,
            .wb_gain_en = 1,
            .histogram_en = 1,
        },
        .a3 = {
            .ae_stat_select = 1,
            .ae_delay_frame = 0,
            .exposure_delay_frame = 1,
            .gain_delay_frame = 1,
            .awb_period_frames = 2,
            .awb_step_speed = 32,
            .awb_stat_select = 1,
            .awb_illum_count = 0,
        },
    },
    },
};

/* Optional consumer hook run just before the per-frame hardware update (mediad's restores
 * the denoise slider); the weak default keeps standalone and host builds linkable. */
__attribute__((weak)) void isp_control_hook(fwi_hw_module_cfg_t *cfg)
{
    (void)cfg;
}

static fwi_ev_setting_t *ev_curr(fwi_ae_result_t *r)
{
    fwi_sensor_settings_t *ss = (fwi_sensor_settings_t *)&r->sensor_set;
    return &ss->ev_set_curr;
}

static void sensor_update_from_ae(fwi_isp_ctx_t *ctx)
{
    fwi_ae_result_t *r = &ctx->ae_entity.ae_result;
    fwi_ev_setting_t *ev = ev_curr(r);
    fwi_sensor_state_t *s = &ctx->sensor;
    uint32_t old = s->exposure_line / 16;
    uint32_t fps;

    s->exposure_line = ev->ev_sensor_exposure_line;
    s->analog_gain = ev->ev_analog_gain;
    s->dig_gain = ev->ev_digital_gain;
    s->total_gain = ev->ev_total_gain;
    s->ae_tbl_index = ev->ev_index;
    s->ae_tbl_index_max = ((fwi_sensor_settings_t *)&r->sensor_set)->ev_index_max;
    s->is_ae_done = (r->ae_status == FWI_AE_STATUS_DONE);
    s->backlight = r->backlight;
    s->ae_gain = r->ae_gain;
    s->ae_level = (int32_t)ev->ev_level;
    if (s->vts && s->hts && s->pclk) {
        uint32_t den = s->vts > old ? s->vts : old;
        if (den == 0)
            fps = 30;
        else
            fps = s->pclk / (den * s->hts);
    } else {
        fps = 30;
    }
    if (fps < 1)
        fps = 1;
    if (fps > 1000)
        fps = 1000;
    s->fps = fps;
    s->frame_time = 1000 / fps;
}

void isp_ae_set_params_helper(fwi_ae_entity_t *ae, fwi_ae_param_type_e cmd)
{
    if (ae == NULL || ae->ae_entity == NULL || ae->ops == NULL ||
        ae->ae_param == NULL)
        return;
    ae->ae_param->type = (uint8_t)cmd;
    ae->ops->ae_set_params(ae->ae_entity, ae->ae_param, &ae->ae_result);
}

void isp_ae_feed(fwi_isp_ctx_t *ctx)
{
    fwi_ae_param_t *p = ctx->ae_entity.ae_param;
    if (p == NULL)
        return;
    p->ae_frame_id = (int32_t)ctx->ae_frame_count;
    p->ae_isp_id = ctx->isp_index;
    p->ae_setting = ctx->ae_ctl;
    p->ae_sensor_info = ctx->sensor;
    p->ae_target_comp = ctx->pltm_entity.pltm_result.pltm_ae_comp;
}

void isp_awb_feed(fwi_isp_ctx_t *ctx)
{
    fwi_awb_param_t *p = ctx->awb_entity.awb_param;
    if (p == NULL)
        return;
    p->awb_frame_id = (int32_t)ctx->awb_frame_count;
    p->awb_ctrl = ctx->awb_ctl;
    p->awb_sensor_info = ctx->sensor;
}

void isp_afs_feed(fwi_isp_ctx_t *ctx)
{
    fwi_afs_param_t *p = ctx->afs_entity.afs_param;
    if (p == NULL)
        return;
    p->platform_id = (int32_t)ctx->hw_cfg.platform_id;
    p->afs_frame_id = (int32_t)ctx->af_frame_count; /* OBS: AF counter */
    p->afs_sensor_info = ctx->sensor;
}

/* ISO per-frame feed: platform id, ISO frame counter and the fixed colour-to-gray
 * thresholds 130/25/30 (reference values); CEM fields use the ABI spellings. */
void isp_iso_feed(fwi_isp_ctx_t *ctx)
{
    fwi_iso_param_t *p = ctx->iso_entity.iso_param;
    if (p == NULL)
        return;
    p->platform_id = (int32_t)ctx->hw_cfg.platform_id;
    p->iso_frame_id = (int32_t)ctx->iso_frame_count;
    p->colour_enhance_color2gray_threshold = 130;
    p->colour_enhance_color2gray_delta_min = 25;
    p->colour_enhance_color2gray_delta_max = 30;
}

void isp_gtm_feed(fwi_isp_ctx_t *ctx)
{
    fwi_gtm_param_t *p = ctx->gtm_entity.gtm_param;
    fwi_tuning_modules_t *U = &ctx->tuning.modules;
    fwi_ae_param_t *ae = ctx->ae_entity.ae_param;
    if (p == NULL)
        return;
    p->gtm_frame_id = (int32_t)ctx->gtm_frame_count;
    p->contrast = ctx->adjust_ctl.contrast;
    p->brightness = ctx->adjust_ctl.brightness;
    p->gtm_bit_offset = 16 - (ae ? ae->commanding_output_bits : 15);
    p->wdr_en = ctx->tuning.enables.wdr_merge_en != 0;
    p->bright_pixel_value = ctx->ae_entity.ae_result.bright_pixel_value;
    p->dark_pixel_value = ctx->ae_entity.ae_result.dark_pixel_value;
    p->gtm_init.gtm_type = (uint8_t)U->gtm_type;
    p->gtm_init.gamma_type = (uint8_t)U->gamma_type;
    p->gtm_init.auto_alpha_en = (uint32_t)U->auto_alpha_en;
    p->gtm_init.hist_pixel_count = U->hist_pixel_count;
    p->gtm_init.dark_minval = U->dark_minval;
    p->gtm_init.bright_minval = U->bright_minval;
    memcpy(p->gtm_init.plum_var, U->plum_var, sizeof(p->gtm_init.plum_var));
    memcpy(p->gtm_init.gtm_cfg, ctx->ae_ctl.ae_hist_eq_cfg,
           sizeof(p->gtm_init.gtm_cfg));
}

void isp_pltm_feed(fwi_isp_ctx_t *ctx)
{
    fwi_pltm_param_t *p = ctx->pltm_entity.pltm_param;
    fwi_ae_param_t *ae = ctx->ae_entity.ae_param;
    fwi_tuning_modules_t *U = &ctx->tuning.modules;
    if (p == NULL)
        return;
    p->pltm_frame_id = (int32_t)ctx->ae_frame_count;
    p->wdr_bit_offset =
        ctx->tuning.enables.wdr_merge_en
            ? 16 - (ae ? ae->commanding_output_bits : 15)
            : 0;
    p->sensor_info = ctx->sensor;
    memcpy(p->pltm_init.pltm_cfg, U->local_tone_static,
           sizeof(p->pltm_init.pltm_cfg));
    memcpy(p->pltm_init.pltm_dynamic_cfg, ctx->ae_ctl.pltm_dynamic_cfg,
           sizeof(p->pltm_init.pltm_dynamic_cfg));
}

static void pltm_result_to_module(fwi_isp_ctx_t *ctx)
{
    fwi_pltm_result_t *r = &ctx->pltm_entity.pltm_result;
    fwi_pltm_cfg_t *m = &ctx->hw_cfg.pltm_cfg;
    fwi_tuning_modules_t *U = &ctx->tuning.modules;

    memcpy(m->pltm_table, r->pltm_tbl, sizeof(m->pltm_table));
    m->original_picture_ratio = (uint8_t)r->pltm_original_picture_ratio;
    m->tr_order = (uint8_t)r->pltm_tr_order;
    m->last_order_ratio = (uint8_t)r->pltm_last_order_ratio;
    m->cal_en = (uint8_t)r->pltm_cal_en;
    m->frame_smoothing_en = (uint8_t)r->pltm_frame_smoothing_en;
    m->block_height = (uint8_t)r->pltm_block_height;
    m->block_width = (uint8_t)r->pltm_block_width;
    m->statistic_div = r->pltm_statistic_div;
    m->lss_switch = (uint8_t)U->local_tone_static[FWI_ISP_PLTM_LSS_SWITCH];
    m->luminance_ratio =
        (uint8_t)U->local_tone_static[FWI_ISP_PLTM_LUMINANCE_RATIO];
    m->lp_halo_res = (uint8_t)U->local_tone_static[FWI_ISP_PLTM_LP_HALO_RES];
    m->white_level = (uint8_t)U->local_tone_static[FWI_ISP_PLTM_WHITE_LEVEL];
    m->intens_asym = (uint8_t)U->local_tone_static[FWI_ISP_PLTM_INTENS_ASYM];
    m->spatial_asm = (uint8_t)U->local_tone_static[FWI_ISP_PLTM_SPATIAL_ASM];
    m->block_h_count =
        (uint8_t)U->local_tone_static[FWI_ISP_PLTM_BLOCK_H_COUNT];
    m->block_v_count =
        (uint8_t)U->local_tone_static[FWI_ISP_PLTM_BLOCK_V_COUNT];
}

static void awb_result_to_module(fwi_isp_ctx_t *ctx)
{
    fwi_awb_result_t *r = &ctx->awb_entity.awb_result;
    fwi_wb_gain_cfg_t *m = &ctx->hw_cfg.wb_gain_cfg;
    fwi_ae_param_t *ae = ctx->ae_entity.ae_param;
    int wdr = ctx->tuning.enables.wdr_merge_en != 0;

    if (!wdr || (ae && ae->nor_cmd_mode != 0)) {
        m->wb_gain = r->wb_gain_output;
    } else {
        m->wb_gain.r_gain =
            (uint16_t)sqrt((double)r->wb_gain_output.r_gain * 256.0);
        m->wb_gain.gr_gain =
            (uint16_t)sqrt((double)r->wb_gain_output.gr_gain * 256.0);
        m->wb_gain.gb_gain =
            (uint16_t)sqrt((double)r->wb_gain_output.gb_gain * 256.0);
        m->wb_gain.b_gain =
            (uint16_t)sqrt((double)r->wb_gain_output.b_gain * 256.0);
    }
    ctx->stats.wb_gain_saved = r->wb_gain_output;
}

/* last exposure/gain successfully sent */
static struct {
    int32_t exp_val;
    int32_t gain_val;
    int32_t r_gain;
    int32_t b_gain;
} last_eg;

void isp_frame_process(fwi_isp_ctx_t *ctx)
{
    fwi_tuning_enables_t *T = &ctx->tuning.enables;
    fwi_hw_module_cfg_t *M = &ctx->hw_cfg;
    fwi_ae_param_t *ae = ctx->ae_entity.ae_param;
    fwi_ae_result_t *ar = &ctx->ae_entity.ae_result;

    if (ctx == NULL || ctx->ae_entity.ae_entity == NULL)
        return;

    pthread_mutex_lock((pthread_mutex_t *)&ctx->lock);

    /* decode the statistics buffer before the algorithm pass.
     * The buffer pointer was stored in ctx->stats_buf by the stats handler. */
    isp_handle_stats(ctx, ctx->stats_buf);

    if (getenv("FREEISP_STATS_TRACE")) {
        static uint32_t n;
        if ((n++ % 30) == 0) {
            fwi_ae_stats_t *st = &ctx->stats.stats.ae_stats;
            fprintf(stderr,
                "freeisp_stats_trace: stats_buf=%p win_pix_n=%u "
                "accum_r[0][0]=%u accum_g[0][0]=%u accum_b[0][0]=%u "
                "avg[0]=%u avg[100]=%u hist[0]=%u hist[128]=%u\n",
                ctx->stats_buf, st->window_pixel_n,
                st->accum_r[0][0], st->accum_g[0][0], st->accum_b[0][0],
                st->average[0], st->average[100], st->hist[0], st->hist[128]);
        }
    }

    isp_apply_settings(ctx);

    isp_ae_feed(ctx);
    if (ctx->ae_entity.ops)
        ctx->ae_entity.ops->ae_run(ctx->ae_entity.ae_entity,
                                    ctx->ae_entity.ae_stats.ae_stats,
                                    ar);
    sensor_update_from_ae(ctx);

    if (getenv("FREEISP_STATS_TRACE")) {
        static uint32_t m;
        if ((m++ % 30) == 0) {
            fwi_ev_setting_t *ev = ev_curr(ar);
            fprintf(stderr,
                "freeisp_ae_trace: target_comp=%d ae_target=%d avg_lum=%d "
                "weight_lum=%d again=%d dgain=%d tgain=%d line=%d\n",
                (int)(ae ? ae->ae_target_comp : -1), (int)ar->ae_target,
                (int)ar->ae_average_luminance, (int)ar->ae_weight_luminance,
                (int)ev->ev_analog_gain, (int)ev->ev_digital_gain,
                (int)ev->ev_total_gain, (int)ev->ev_sensor_exposure_line);
        }
    }
    config_gamma(ctx);
    config_dig_gain(ctx, (int)ev_curr(ar)->ev_digital_gain);
    config_wdr(ctx, 0);

    __isp_stat_dynamic_judge(ctx);

    if (ctx->iso_entity.ops) {
        fwi_iso_cfg_core_ops_t *ops = fwi_iso_ops(ctx->iso_entity.ops);
        isp_iso_feed(ctx);
        ops->iso_run(ctx->iso_entity.iso_entity, &ctx->iso_entity.iso_result);
    }

    if (T->global_tone_en && ctx->gtm_entity.ops) {
        isp_gtm_feed(ctx);
        ctx->gtm_entity.ops->gtm_run(ctx->gtm_entity.gtm_entity,
                                      ctx->gtm_entity.gtm_stats.gtm_stats,
                                      &ctx->gtm_entity.gtm_result);
    }

    if (T->local_tone_en && ctx->pltm_entity.ops) {
        isp_pltm_feed(ctx);
        ctx->pltm_entity.ops->pltm_run(ctx->pltm_entity.pltm_entity,
                                       ctx->pltm_entity.pltm_stats.pltm_stats,
                                       &ctx->pltm_entity.pltm_result);
        pltm_result_to_module(ctx);
    }

    if (T->flicker_detect_en && ctx->afs_entity.ops) {
        isp_afs_feed(ctx);
        ctx->afs_entity.ops->afs_run(ctx->afs_entity.afs_entity,
                                     ctx->afs_entity.afs_stats.afs_stats,
                                     &ctx->afs_entity.afs_result);
        ctx->ae_ctl.flicker_type = ctx->afs_entity.afs_result.flicker_type_output;
    }

    if (ctx->awb_entity.ops) {
        isp_awb_feed(ctx);
        ctx->awb_entity.ops->awb_run(ctx->awb_entity.awb_entity,
                                     ctx->awb_entity.awb_stats.awb_stats,
                                     &ctx->awb_entity.awb_result);
        awb_result_to_module(ctx);
    }

    if (T->lens_shading_en)
        config_lens_table(ctx, (int)ctx->af_entity.af_result.std_code_output);
    if (T->mesh_shading_en)
        config_msc_table(ctx, (int)ctx->af_entity.af_result.std_code_output);

    isp_apply_colormatrix(ctx);

    M->demosaic_cfg.min_rgb = T->dehaze_en ? 1023 : 0;

    /* mediad's pre-hardware-update hook (restores the denoise slider); a weak no-op
     * default covers host builds (tests/fake_tiers.c). */
    isp_control_hook(M);

    isp_hardware_update(M);

    ctx->awb_frame_count++;
    ctx->ae_frame_count++;
    ctx->af_frame_count++;
    ctx->all_frame_count++;
    ctx->gtm_frame_count++;
    ctx->md_frame_count++;
    ctx->afs_frame_count++;
    ctx->iso_frame_count++;
    ctx->rolloff_frame_count++;

    pthread_mutex_unlock((pthread_mutex_t *)&ctx->lock);

    /* Steps 6 and 7 run outside the ctx lock: the exposure/gain ioctl
     * on change, then the register program load. */
    {
        struct hw_isp_device *dev = media_params.isp_dev[ctx->isp_index];
        struct {
            int32_t exp_val, gain_val, r_gain, b_gain;
        } eg;
        uint32_t gr = ctx->awb_entity.awb_result.wb_gain_output.gr_gain;
        uint32_t gb = ctx->awb_entity.awb_result.wb_gain_output.gb_gain;
        eg.exp_val = (int32_t)ev_curr(ar)->ev_sensor_exposure_line;
        eg.gain_val = (int32_t)ev_curr(ar)->ev_analog_gain >> 4;
        eg.r_gain = (int32_t)(ctx->awb_entity.awb_result.wb_gain_output.r_gain *
                              256 / (gr ? gr : 256));
        eg.b_gain = (int32_t)(ctx->awb_entity.awb_result.wb_gain_output.b_gain *
                              256 / (gb ? gb : 256));
        if (eg.exp_val != last_eg.exp_val || eg.gain_val != last_eg.gain_val ||
            eg.r_gain != last_eg.r_gain || eg.b_gain != last_eg.b_gain) {
            struct sensor_exp_gain s;
            s.exp_val = eg.exp_val;
            s.gain_val = eg.gain_val;
            s.r_gain = eg.r_gain;
            s.b_gain = eg.b_gain;
            if (dev != NULL &&
                isp_uapi_sys->ioctl(dev->sensor.fd,
                                    VIDIOC_VIN_SENSOR_EXP_GAIN, &s) == 0) {
                last_eg.exp_val = eg.exp_val;
                last_eg.gain_val = eg.gain_val;
                last_eg.r_gain = eg.r_gain;
                last_eg.b_gain = eg.b_gain;
            }
        }

        if (dev != NULL) {
            struct fwi_table_reg_map reg;
            reg.addr = ctx->reg_image;
            reg.size = ISP_LOAD_DRAM_SIZE;
            isp_set_load_reg(dev, &reg);
        }
    }

    (void)M;
    (void)ae;
}
