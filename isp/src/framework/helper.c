// SPDX-License-Identifier: AGPL-3.0-only
/* helper.c - attribute API and V4L2 control-event handling */
#include "framework_internal.h"
#include <stdio.h>
#include <string.h>

extern unsigned int isp_lib_log_param;
extern fwi_isp_ctx_t isp_ctx[1];

static int valid_dev(int id)
{
    return id >= 0 && id < HW_ISP_DEVICE_NUM && media_params.isp_dev[id] != NULL;
}

/* ------------------------------------------------------------------ */
/* attribute path                                                      */
/* ------------------------------------------------------------------ */
int32_t isp_set_attr_cfg(int id, uint32_t ctrl_id, void *value)
{
    fwi_isp_ctx_t *ctx;

    if (id >= HW_ISP_DEVICE_NUM || id < 0)
        return -1;
    if (!valid_dev(id))
        return -1;
    ctx = &isp_ctx[id];
    if (value == NULL)
        return -1;
    switch (ctrl_id) {
    case ISP_CTRL_PLTMWDR_STR:
        ctx->picture_ctl.pltmwdr_level = *(int32_t *)value;
        break;
    case ISP_CTRL_DN_STR:
        ctx->picture_ctl.denoise_level = *(int32_t *)value;
        break;
    case ISP_CTRL_3DN_STR:
        ctx->picture_ctl.denoise_3d_level = *(int32_t *)value;
        break;
    case ISP_CTRL_HIGH_LIGHT:
        ctx->picture_ctl.highlight_level = *(int32_t *)value;
        break;
    case ISP_CTRL_BACK_LIGHT:
        ctx->picture_ctl.backlight_level = *(int32_t *)value;
        break;
    case ISP_CTRL_WB_MGAIN:
        ctx->awb_ctl.wb_gain_manual = *(fwi_wb_gain_t *)value;
        break;
    case ISP_CTRL_AGAIN_DGAIN:
        /* store and flag only when the record differs. */
        if (memcmp(&ctx->picture_ctl.gains, value,
                   sizeof(ctx->picture_ctl.gains)) != 0) {
            memcpy(&ctx->picture_ctl.gains, value,
                   sizeof(ctx->picture_ctl.gains));
            ctx->pending_3a_changes |= FWI_ISP_SET_GAIN_STR;
        }
        break;
    case ISP_CTRL_COLOR_EFFECT:
        if (ctx->picture_ctl.effect != *(int32_t *)value) {
            ctx->picture_ctl.effect = (uint8_t)*(int32_t *)value;
            ctx->pending_3a_changes |= FWI_ISP_SET_EFFECT;
        }
        break;
    case ISP_CTRL_AE_ROI:
        /* AE-ROI setter */
        {
            struct ae_roi_arg { int32_t mode; fwi_h3a_coord_window_t win; };
            struct ae_roi_arg *a = value;
            if (a->mode == FWI_AE_METERING_MODE_SPOT &&
                memcmp(&a->win, &ctx->ae_ctl.ae_coord, sizeof(a->win)) != 0) {
                ctx->ae_ctl.exposure_metering_mode = FWI_AE_METERING_MODE_SPOT;
                ctx->ae_ctl.ae_coord = a->win;
                ctx->ae_ctl.exposure_lock = 0;
                ctx->pending_3a_changes |= FWI_ISP_SET_AE_METERING_MODE;
            }
        }
        break;
    default:
        fprintf(stderr, "isp: unknown attr set %u\n", ctrl_id);
        break;
    }
    return 0;
}

int32_t isp_get_attr_cfg(int id, uint32_t ctrl_id, void *value)
{
    fwi_isp_ctx_t *ctx;

    if (id >= HW_ISP_DEVICE_NUM || id < 0)
        return -1;
    if (!valid_dev(id))
        return -1;
    ctx = &isp_ctx[id];
    switch (ctrl_id) {
    case ISP_CTRL_PLTMWDR_STR:
        *(int32_t *)value = ctx->picture_ctl.pltmwdr_level;
        break;
    case ISP_CTRL_DN_STR:
        *(int32_t *)value = ctx->picture_ctl.denoise_level;
        break;
    case ISP_CTRL_3DN_STR:
        *(int32_t *)value = ctx->picture_ctl.denoise_3d_level;
        break;
    case ISP_CTRL_HIGH_LIGHT:
        *(int32_t *)value = ctx->picture_ctl.highlight_level;
        break;
    case ISP_CTRL_BACK_LIGHT:
        *(int32_t *)value = ctx->picture_ctl.backlight_level;
        break;
    case ISP_CTRL_WB_MGAIN:
        *(fwi_wb_gain_t *)value = ctx->awb_entity.awb_result.wb_gain_output;
        break;
    case ISP_CTRL_DIGITAL_GAIN:
        *(int32_t *)value =
            (int32_t)((fwi_sensor_settings_t *)&ctx->ae_entity.ae_result.sensor_set)
                ->ev_set_curr.ev_digital_gain;
        break;
    case ISP_CTRL_AGAIN_DGAIN:
        *(fwi_gain_cfg_t *)value = ctx->picture_ctl.gains;
        break;
    case ISP_CTRL_COLOR_EFFECT:
        *(int32_t *)value = ctx->picture_ctl.effect;
        break;
    case ISP_CTRL_AE_ROI:
        *(fwi_h3a_coord_window_t *)value = ctx->ae_ctl.ae_coord;
        break;
    case ISP_CTRL_COLOR_TEMP:
        *(int32_t *)value = ctx->awb_entity.awb_result.colour_temp_output;
        break;
    case ISP_CTRL_EV_IDX:
        *(int32_t *)value =
            ((fwi_sensor_settings_t *)&ctx->ae_entity.ae_result.sensor_set)
                ->ev_set.ev_index;
        break;
    case ISP_CTRL_PLTM_HARDWARE_STR:
        *(int32_t *)value = ctx->pltm_entity.pltm_result.pltm_next_strength;
        break;
    case ISP_CTRL_MODULE_EN:
        break;
    default:
        fprintf(stderr, "isp: unknown attr get %u\n", ctrl_id);
        break;
    }
    return 0;
}

int32_t isp_get_lv(int id)
{
    if (id >= HW_ISP_DEVICE_NUM || id < 0 ||
        media_params.isp_dev[id] == NULL)
        return -1;
    return (int32_t)
        ((fwi_sensor_settings_t *)&isp_ctx[id].ae_entity.ae_result.sensor_set)
            ->ev_set_curr.ev_level;
}

/* ------------------------------------------------------------------ */
/* V4L2 control events                                                 */
/* ------------------------------------------------------------------ */
static void gated(int32_t *field, int32_t value, uint32_t flag,
                  fwi_isp_ctx_t *ctx)
{
    if (*field != value) {
        *field = value;
        ctx->pending_3a_changes |= flag;
    }
}

/* Same gated semantics for the ABI's 1-byte enum-ish fields (the reference
 * fields are uint8_t; an int32_t store would corrupt the neighbours). */
static void gated8(uint8_t *field, int32_t value, uint32_t flag,
                   fwi_isp_ctx_t *ctx)
{
    if (*field != (uint8_t)value) {
        *field = (uint8_t)value;
        ctx->pending_3a_changes |= flag;
    }
}

void isp_handle_ctrl_event(fwi_isp_ctx_t *ctx, const struct v4l2_event *ev)
{
    uint32_t id = ev->id;
    int32_t value = ev->u.ctrl.value;
    fwi_ae_controls_t *ae = &ctx->ae_ctl;
    fwi_awb_setting_t *awb = &ctx->awb_ctl;
    fwi_af_settings_t *af = &ctx->af_ctl;
    fwi_tune_setting_t *tune = &ctx->picture_ctl;

    switch (id) {
    case V4L2_CID_BRIGHTNESS: gated(&tune->brightness_level, value, FWI_ISP_SET_BRIGHTNESS, ctx); break;
    case V4L2_CID_CONTRAST: gated(&tune->contrast_level, value, FWI_ISP_SET_CONTRAST, ctx); break;
    case V4L2_CID_SATURATION: gated(&tune->saturation_level, value, FWI_ISP_SET_SATURATION, ctx); break;
    case V4L2_CID_HUE: gated(&tune->hue_level, value, FWI_ISP_SET_HUE, ctx); break;
    case V4L2_CID_SHARPNESS: gated(&tune->sharpness_level, value, FWI_ISP_SET_SHARPNESS, ctx); break;
    case V4L2_CID_AUTO_WHITE_BALANCE: gated8(&awb->wb_mode, value, FWI_ISP_SET_AWB_MODE, ctx); break;
    case V4L2_CID_WHITE_BALANCE_PRESET: gated8(&awb->wb_mode, value, 0, ctx); break;
    case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
        awb->wb_mode = FWI_WB_MANUAL;
        awb->wb_temperature = value;
        break;
    case V4L2_CID_EXPOSURE:
        if (ctx->sensor.pclk != 0)
            ae->exposure_absolute =
                (uint32_t)((value / 16.0) * 1e6 * ctx->sensor.hts /
                           ctx->sensor.pclk);
        break;
    case V4L2_CID_EXPOSURE_ABSOLUTE: ae->exposure_absolute = (uint32_t)value; break;
    case V4L2_CID_EXPOSURE_AUTO: gated8(&ae->exposure_mode, value, 0, ctx); break;
    case V4L2_CID_AUTOGAIN: gated8(&ae->iso_mode, value, 0, ctx); break;
    case V4L2_CID_GAIN: gated(&ae->sensor_gain, value, 0, ctx); break;
    case V4L2_CID_ISO_SENSITIVITY: {
        static const int32_t iso[7] = { 100, 200, 400, 800, 1600, 3200, 6400 };
        if (value >= 0 && value < 7)
            gated(&ae->iso_sensitivity, iso[value], 0, ctx);
        break;
    }
    case V4L2_CID_ISO_SENSITIVITY_AUTO: gated8(&ae->iso_mode, value, 0, ctx); break;
    case V4L2_CID_EXPOSURE_BIAS: {
        static const int32_t bias[9] = { -4, -3, -2, -1, 0, 1, 2, 3, 4 };
        if (value >= 0 && value < 9)
            gated(&ae->exposure_compensation, bias[value], 0, ctx);
        break;
    }
    case V4L2_CID_EXPOSURE_METERING:
        if (ae->exposure_metering_mode != value) {
            ae->exposure_metering_mode = (uint8_t)value;
            ae->exposure_lock = 0;
            ctx->pending_3a_changes |= FWI_ISP_SET_AE_METERING_MODE;
        }
        break;
    case V4L2_CID_POWER_LINE_FREQUENCY:
        gated8(&ae->flicker_mode, value, FWI_ISP_SET_FLICKER_MODE, ctx);
        break;
    case V4L2_CID_BAND_STOP_FILTER:
        ae->flicker_mode = value == 1 ? ISP_FREQUENCY_AUTO : ISP_FREQUENCY_DISABLED;
        ctx->pending_3a_changes |= FWI_ISP_SET_FLICKER_MODE;
        break;
    case V4L2_CID_AUTO_BRIGHTNESS:
        if (value == 1)
            tune->brightness_level = 0;
        break;
    case V4L2_CID_SCENE_MODE: gated8(&ae->scene_mode, value, FWI_ISP_SET_SCENE_MODE, ctx); break;
    case V4L2_CID_ILLUMINATORS_1:
        ae->flash_mode = value ? FWI_FLASH_MODE_TORCH : FWI_FLASH_MODE_OFF;
        break;
    case V4L2_CID_FOCUS_ABSOLUTE: gated(&af->focus_absolute, value, 0, ctx); break;
    case V4L2_CID_FOCUS_RELATIVE: gated(&af->focus_relative, value, 0, ctx); break;
    case V4L2_CID_AUTO_FOCUS_RANGE: gated8(&af->af_range, value, 0, ctx); break;
    case V4L2_CID_FOCUS_AUTO:
        if (value != FWI_AUTO_FOCUS_MANUAL) {
            af->af_mode = FWI_AUTO_FOCUS_CONTINUOUS;
            af->focus_lock = 0;
            ctx->pending_3a_changes |= FWI_ISP_SET_AF_METERING_MODE;
        } else {
            af->af_mode = FWI_AUTO_FOCUS_MANUAL;
        }
        break;
    case V4L2_CID_AUTO_FOCUS_START:
        af->focus_lock = 0;
        af->af_mode = FWI_AUTO_FOCUS_TOUCH;
        break;
    case V4L2_CID_AUTO_FOCUS_STOP:
        af->focus_lock = 1;
        af->af_mode = FWI_AUTO_FOCUS_CONTINUOUS;
        break;
    default:
        fprintf(stderr, "isp: ctrl event %#x = %d (logged)\n", id, value);
        break;
    }
}

void isp_handle_frame_sync(fwi_isp_ctx_t *ctx, const uint8_t *d)
{
    struct hw_isp_device *dev = media_params.isp_dev[ctx->isp_index];

    if (dev != NULL)
        dev->load_type = d[0];
    if (ctx->sensor.colour_space != d[1]) {
        ctx->sensor.colour_space = d[1];
        ctx->pending_3a_changes |= FWI_ISP_SET_HUE;
    }
    isp_lib_log_param = (unsigned int)((d[3] << 8) | d[2] |
                                       (uint8_t)ctx->tuning.enables.debug_log_mask);
}
