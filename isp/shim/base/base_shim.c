/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* base_shim.c - SDK-ABI adapter for the clean isp_base tier: per entry point,
 * translate fwi_isp_ctx -> fwi_base_ctx_t, call the clean_* core, copy back.
 * Field mapping: docs/shim-mappings.md#base */
/* SDK and clean headers share ISP_* macros and entry-point names; the clean
 * ones are renamed for this TU only (the clean TU builds with the same renames). */
#include <string.h>

#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"

/* SDK sizes that base.h/reg_writers.h would otherwise redefine. */
enum {
    SDK_ISP_LSC_TBL_SIZE   = ISP_LSC_TBL_SIZE,
    SDK_ISP_MSC_TBL_LENGTH = ISP_MSC_TBL_LENGTH,
    SDK_ISP_MSC_TBL_SIZE   = ISP_MSC_TBL_SIZE,
    SDK_ISP_MSC_TEMP_NUM   = ISP_MSC_TEMP_NUM
};

#undef ISP_LSC_TBL_SIZE
#undef ISP_MSC_TBL_LENGTH
#undef ISP_MSC_TBL_SIZE
#undef ISP_MSC_TEMP_NUM

/* entry points: base.h's prototypes become the clean_* ones this file calls */
#define config_band_step         clean_config_band_step
#define config_lens_center       clean_config_lens_center
#define config_dig_gain          clean_config_dig_gain
#define config_gamma             clean_config_gamma
#define config_wdr               clean_config_wdr
#define isp_apply_colormatrix    clean_isp_apply_colormatrix
#define __isp_stat_dynamic_judge clean___isp_stat_dynamic_judge
#define isp_handle_stats         clean_isp_handle_stats
#define isp_handle_stats_sync    clean_isp_handle_stats_sync
#define isp_apply_settings       clean_isp_apply_settings
#define config_lens_table        clean_config_lens_table
#define config_msc_table         clean_config_msc_table
#include "base.h"
#undef config_band_step
#undef config_lens_center
#undef config_dig_gain
#undef config_gamma
#undef config_wdr
#undef isp_apply_colormatrix
#undef __isp_stat_dynamic_judge
#undef isp_handle_stats
#undef isp_handle_stats_sync
#undef isp_apply_settings
#undef config_lens_table
#undef config_msc_table

#include "base_shim.h"

/* Framework AE helper (isp.c), not in the fwi_* ABI headers; declared here so
 * freeisp_ae_set_params can forward to it with the real entity context. */
void isp_ae_set_params_helper(fwi_ae_entity_t *ae_ctx,
                              fwi_ae_param_type_e cmd_type);

/* SDK context during clean_isp_apply_settings; freeisp_ae_set_params needs it
 * to reach ops/ae_entity (the clean tier has no entity context). */
static struct fwi_isp_ctx *g_cur_sdk;

/* Per-entry write-back selector for store_sdk(); private to this TU. */
enum base_shim_op {
    SHIM_OP_BAND_STEP = 0,
    SHIM_OP_LENS_CENTER,
    SHIM_OP_DIG_GAIN,
    SHIM_OP_GAMMA,
    SHIM_OP_WDR,
    SHIM_OP_COLORMATRIX,
    SHIM_OP_JUDGE,
    SHIM_OP_STATS,
    SHIM_OP_STATS_SYNC,
    SHIM_OP_APPLY_SETTINGS,
    SHIM_OP_LENS_TABLE,
    SHIM_OP_MSC_TABLE
};

/* Table provider: freeisp_get_tables()->base is rebuilt from the SDK tuning on
 * every entry. Table sources: docs/shim-mappings.md#base */
static int32_t  g_gamma_trig[ISP_GAMMA_TRIG_N];
static uint16_t g_wdr_words[ISP_WDR_TBL_WORDS];
static base_tables_t  g_bt;
static freeisp_tables_t g_ft = { &g_bt };

/* The clean context is ~137 KB; keep it off the stack and reuse it (the
 * framework drives one ISP instance at a time). */
static fwi_base_ctx_t g_clean;

static const base_tables_t *g_override;
static const uint16_t *g_loc_anti_gamma;
static const int16_t  *g_loc_rgb2yuv;    /* 6 spaces x 12 entries */
static const uint16_t *g_loc_lsc_trig_def;
static const uint16_t *g_loc_msc_trig_def;
static const int32_t  *g_loc_color_temp;

/* CCM blend knots (warm/neutral/daylight), used when the locator has none; a
 * NULL color_temp zeroes the RGB2RGB matrix (green frame). */
static const int32_t g_cm_color_temp_default[3] = { 2700, 4000, 6500 };

const freeisp_tables_t *freeisp_get_tables(void)
{
    g_ft.base = g_override ? g_override : &g_bt;
    return &g_ft;
}

void base_shim_set_tables(const void *tables)
{
    g_override = (const base_tables_t *)tables;
}

void base_shim_set_locator(const void *anti_gamma_table,
                           const void *rgb2yuv_matrix,
                           const void *lsc_trig_cfg_def,
                           const void *msc_trig_cfg_def,
                           const void *isp_cm_color_temp)
{
    g_loc_anti_gamma   = (const uint16_t *)anti_gamma_table;
    g_loc_rgb2yuv      = (const int16_t *)rgb2yuv_matrix;
    g_loc_lsc_trig_def = (const uint16_t *)lsc_trig_cfg_def;
    g_loc_msc_trig_def = (const uint16_t *)msc_trig_cfg_def;
    g_loc_color_temp   = (const int32_t *)isp_cm_color_temp;
}

/* The clean core calls the provider; a set override wins. */

static void build_tables(const struct fwi_isp_ctx *s)
{
    const fwi_tuning_modules_t *t = &s->tuning.modules;
    unsigned i;

    memset(&g_bt, 0, sizeof(g_bt));

    if (g_override)
        return;

    g_bt.gamma_base = t->gamma_tbl_init[0];
    for (i = 0; i < ISP_GAMMA_TRIG_N - 1; i++)
        g_bt.gamma_sub[i] = t->gamma_tbl_init[i + 1];
    for (i = 0; i < ISP_GAMMA_TRIG_N; i++)
        g_gamma_trig[i] = (int32_t)t->gamma_trig_cfg[i];
    g_bt.gamma_trig = g_gamma_trig;

    for (i = 0; i < 2 * ISP_MSC_TEMP_NUM; i++)
        g_bt.lsc[i] = t->lens_shading_tbl[i];
    g_bt.lsc_trig = t->lens_shading_trig_cfg;
    g_bt.lsc_trig_def = g_loc_lsc_trig_def;

    for (i = 0; i < 2 * ISP_MSC_TEMP_NUM; i++)
        g_bt.msc[i] = t->mesh_shading_tbl[i];
    g_bt.msc_trig = t->mesh_shading_trig_cfg;
    g_bt.msc_trig_def = g_loc_msc_trig_def;
    /* OTP MSC golden reference = first MSC temperature row; NULL disables it. */
    g_bt.otp_msc_golden = t->mesh_shading_tbl[0];

    g_bt.linear = t->linearize_tbl;

    /* The tuning WDR table is a byte image of little-endian u16 words; copy it
     * into an aligned u16 view (the clean core reads it as u16). */
    memcpy(g_wdr_words, t->wdr_table, sizeof(g_wdr_words));
    g_bt.wdr_table = g_wdr_words;

    g_bt.anti_gamma = g_loc_anti_gamma;

    if (g_loc_rgb2yuv) {
        for (i = 0; i < ISP_RGB2YUV_SPACES; i++)
            g_bt.rgb2yuv_base[i] = g_loc_rgb2yuv + i * 12;
    }

    g_bt.color_matrix = (const uint16_t *)t->colour_matrix_init;
    g_bt.color_temp = g_loc_color_temp ? g_loc_color_temp
                                       : g_cm_color_temp_default;
}

/* Shared SDK -> clean input mapping; unread fields stay zero. Frame counters
 * are u64 in the SDK, u32 in the clean model: low words carried. */
static void bind_sdk(const struct fwi_isp_ctx *s)
{
    const fwi_tuning_modules_t *t = &s->tuning.modules;
    const fwi_tuning_enables_t *ts = &s->tuning.enables;
    const fwi_ae_param_t *p = s->ae_entity.ae_param;
    unsigned i;

    memset(&g_clean, 0, sizeof(g_clean));

    g_clean.isp_dev_id = (unsigned long)s->isp_index;
    g_clean.isp_3a_change_flags = s->pending_3a_changes;
    g_clean.ae_frame_cnt = (uint32_t)s->ae_frame_count;
    g_clean.af_frame_cnt = (uint32_t)s->af_frame_count;
    g_clean.awb_frame_cnt = (uint32_t)s->awb_frame_count;

    /* tune / test enable block */
    g_clean.isp_test_settings.ae_en        = (uint32_t)ts->auto_exposure_en;
    g_clean.isp_test_settings.awb_en       = (uint32_t)ts->auto_wb_en;
    g_clean.isp_test_settings.af_en        = (uint32_t)ts->auto_focus_en;
    g_clean.isp_test_settings.afs_en       = (uint32_t)ts->flicker_detect_en;
    g_clean.isp_test_settings.pltm_en      = (uint32_t)ts->local_tone_en;
    g_clean.isp_test_settings.defog_en     = (uint32_t)ts->dehaze_en;
    g_clean.isp_test_settings.lsc_en       = (uint32_t)ts->lens_shading_en;
    g_clean.isp_test_settings.msc_en       = (uint32_t)ts->mesh_shading_en;
    g_clean.isp_test_settings.gamma_en     = (uint32_t)ts->gamma_en;
    g_clean.isp_test_settings.wdr_en       = (uint32_t)ts->wdr_merge_en;
    g_clean.isp_test_settings.dig_gain_en  = (uint32_t)ts->digital_gain_en;
    g_clean.isp_test_settings.linear_en    = (uint32_t)ts->linearize_en;
    g_clean.isp_test_settings.cm_en        = (uint32_t)ts->colour_matrix_en;
    g_clean.isp_test_settings.satur_en     = (uint32_t)ts->saturation_en;
    g_clean.isp_test_settings.wb_en        = (uint32_t)ts->wb_gain_en;
    g_clean.isp_test_settings.isp_test_mode  = ts->bench_mode;
    g_clean.isp_test_settings.isp_color_temp = ts->fixed_cct_k;
    /* fwi_tuning_enables_t has no isp_test_focus slot (AF stats read gate);
     * 0 matches the vendor behaviour without an AF entity. */
    g_clean.isp_test_settings.isp_test_focus = 0;

    /* tuning corpus the clean model keeps in its own isp_ini_cfg */
    for (i = 0; i < 4; i++)
        g_clean.isp_ini_cfg.gains.bayer_gain[i] = (uint16_t)t->bayer_gain[i];
    g_clean.isp_ini_cfg.hue_level = s->picture_ctl.hue_level;
    g_clean.isp_ini_cfg.color_effect = (int32_t)s->picture_ctl.effect;
    g_clean.isp_ini_cfg.gains.gain_favour     = s->picture_ctl.gains.gain_bias;
    g_clean.isp_ini_cfg.gains.analog_gain_min = s->picture_ctl.gains.ana_gain_min;
    g_clean.isp_ini_cfg.gains.analog_gain_max = s->picture_ctl.gains.ana_gain_max;
    g_clean.isp_ini_cfg.gains.digital_gain_min = s->picture_ctl.gains.dig_gain_min;
    g_clean.isp_ini_cfg.gains.digital_gain_max = s->picture_ctl.gains.dig_gain_max;
    for (i = 0; i < ISP_GAMMA_TRIG_N; i++)
        g_clean.isp_ini_cfg.gamma_trig_cfg[i] = (int32_t)t->gamma_trig_cfg[i];
    for (i = 0; i < ISP_MSC_TEMP_NUM; i++) {
        g_clean.isp_ini_cfg.lsc_trig_cfg[i] = t->lens_shading_trig_cfg[i];
        g_clean.isp_ini_cfg.msc_trig_cfg[i] = t->mesh_shading_trig_cfg[i];
    }
    g_clean.isp_ini_cfg.cm_en = (int32_t)ts->colour_matrix_en;
    g_clean.isp_ini_cfg.ae_stat_sel =
        s->tuning.a3.ae_stat_select;

    /* geometry + lens centre */
    g_clean.stats_ctx.pic_w = s->stats.pic_w;
    g_clean.stats_ctx.pic_h = s->stats.pic_h;
    g_clean.ae_settings.lsc_center_x = t->lens_shading_center_x;
    g_clean.ae_settings.lsc_center_y = t->lens_shading_center_y;

    /* AE settings / coordinates */
    g_clean.ae_settings.ae_mode = (int32_t)s->ae_ctl.ae_mode;
    g_clean.ae_settings.wdr_output_select =
        (int32_t)s->ae_ctl.wdr_output_select;
    g_clean.ae_settings.flicker_mode = (int32_t)s->ae_ctl.flicker_mode;
    g_clean.ae_settings.flash_open = s->ae_ctl.flash_open;
    g_clean.ae_spot = (s->ae_ctl.exposure_metering_mode != 0);
    g_clean.af_spot = (s->af_ctl.af_metering_mode != 0);
    g_clean.ae_coor[0] = s->ae_ctl.ae_coord.x1;
    g_clean.ae_coor[1] = s->ae_ctl.ae_coord.y1;
    g_clean.ae_coor[2] = s->ae_ctl.ae_coord.x2;
    g_clean.ae_coor[3] = s->ae_ctl.ae_coord.y2;
    g_clean.af_coor[0] = s->af_ctl.af_coord.x1;
    g_clean.af_coor[1] = s->af_ctl.af_coord.y1;
    g_clean.af_coor[2] = s->af_ctl.af_coord.x2;
    g_clean.af_coor[3] = s->af_ctl.af_coord.y2;
    g_clean.ae_settings.awb_coor[0] = s->awb_ctl.awb_coord.x1;
    g_clean.ae_settings.awb_coor[1] = s->awb_ctl.awb_coord.y1;
    g_clean.ae_settings.awb_coor[2] = s->awb_ctl.awb_coord.x2;
    g_clean.ae_settings.awb_coor[3] = s->awb_ctl.awb_coord.y2;

    /* The clean tier reads ae_entity_ctx.ae_param as a clean fwi_base_ae_param_t, so
     * point it at the embedded copy; freeisp_ae_set_params maps it back. */
    if (p) {
        g_clean.ae_param.nor_cmd_mode = p->nor_cmd_mode;
        g_clean.ae_param.comanding_input_bits = p->commanding_input_bits;
        g_clean.ae_param.comanding_output_bits = p->commanding_output_bits;
        g_clean.ae_param.ae_ini.gain_favour = p->ae_init.ae_gain_bias;
        g_clean.ae_param.ae_ini.analog_gain_min = p->ae_init.ae_analog_gain_range[0];
        g_clean.ae_param.ae_ini.analog_gain_max = p->ae_init.ae_analog_gain_range[1];
        g_clean.ae_param.ae_ini.digital_gain_min = p->ae_init.ae_digital_gain_range[0];
        g_clean.ae_param.ae_ini.digital_gain_max = p->ae_init.ae_digital_gain_range[1];
        g_clean.ae_entity_ctx.ae_param = &g_clean.ae_param;
    }
    g_clean.ae_param.frame_cnt = (uint32_t)s->ae_frame_count;

    /* AE / AF / AFS / ISO results */
    g_clean.ae_result.wdr_hi_th = s->ae_entity.ae_result.wdr_hi_threshold;
    g_clean.ae_result.wdr_low_th = s->ae_entity.ae_result.wdr_low_threshold;
    g_clean.ae_result.wdr_ratio_sensor =
        s->ae_entity.ae_result.ae_wdr_ratio.sensor;
    g_clean.ae_result.wdr_ratio_tmp =
        s->ae_entity.ae_result.ae_wdr_ratio.tmp;
    g_clean.ae_result.wdr_ratio_isp_hw =
        s->ae_entity.ae_result.ae_wdr_ratio.hardware;
    g_clean.ae_result.ae_gain = (int32_t)s->ae_entity.ae_result.ae_gain;
    g_clean.af_param.mov = s->af_entity.af_param
                               ? s->af_entity.af_param->mov : 0;
    g_clean.afs_param.flicker_mode = s->afs_entity.afs_param
                                         ? s->afs_entity.afs_param->flicker_mode
                                         : 0;
    g_clean.iso_cem_color2gray_th = s->iso_entity.iso_param
                                        ? s->iso_entity.iso_param->colour_enhance_color2gray_threshold
                                        : 0;
    g_clean.iso_lum_idx = (int32_t)s->iso_entity.iso_result.luminance_index;

    /* AWB result / saved gains */
    g_clean.awb_color_temp_output =
        s->awb_entity.awb_result.colour_temp_output;
    g_clean.wb_gain[0] = s->stats.wb_gain_saved.r_gain;
    g_clean.wb_gain[1] = s->stats.wb_gain_saved.gr_gain;
    g_clean.wb_gain[2] = s->stats.wb_gain_saved.gb_gain;
    g_clean.wb_gain[3] = s->stats.wb_gain_saved.b_gain;
    g_clean.awb_gain_output[0] = s->awb_entity.awb_result.wb_gain_output.r_gain;
    g_clean.awb_gain_output[1] = s->awb_entity.awb_result.wb_gain_output.gr_gain;
    g_clean.awb_gain_output[2] = s->awb_entity.awb_result.wb_gain_output.gb_gain;
    g_clean.awb_gain_output[3] = s->awb_entity.awb_result.wb_gain_output.b_gain;

    /* sensor / defog / colour temperature */
    g_clean.sensor_info.ae_lv = s->sensor.ae_level;
    g_clean.sensor_info.colour_space = (int32_t)s->sensor.colour_space;
    g_clean.sensor_info.so_en = (uint32_t)ts->sensor_offset_en;
    g_clean.sensor_info.blc_en = (uint32_t)ts->black_level_en;
    g_clean.sensor_info.gain_offset[0] = s->sensor.gain_offset.r_gain;
    g_clean.sensor_info.gain_offset[1] = s->sensor.gain_offset.gr_gain;
    g_clean.sensor_info.gain_offset[2] = s->sensor.gain_offset.gb_gain;
    g_clean.sensor_info.gain_offset[3] = s->sensor.gain_offset.b_gain;
    g_clean.adjust.defog_value = s->adjust_ctl.dehaze_value;
    g_clean.stat.min_rgb_saved = (uint16_t)s->drv_stats_ref.min_rgb_saved;
    g_clean.defog_ctx.min_rgb_pre[0] = (uint16_t)s->dehaze_state.min_rgb_pre[0];

    /* anti-gamma (AE/AWB WDR statistics correction source) */
    memcpy(g_clean.anti_gamma_tbl, s->inverse_gamma,
           sizeof(g_clean.anti_gamma_tbl));

    /* module_cfg subset the clean tier touches */
    g_clean.module_cfg.mode_cfg.wdr_mode = s->hw_cfg.mode_cfg.wdr_mode;
    g_clean.module_cfg.afs_cfg.inc_line = s->hw_cfg.afs_cfg.inc_line;
    g_clean.module_cfg.gain_offset_cfg.sensor_offset[0] =
        (uint16_t)(int16_t)s->hw_cfg.gain_offset_cfg.sensor_offset.r_offset;
    g_clean.module_cfg.gain_offset_cfg.sensor_offset[1] =
        (uint16_t)(int16_t)s->hw_cfg.gain_offset_cfg.sensor_offset.gr_offset;
    g_clean.module_cfg.gain_offset_cfg.sensor_offset[2] =
        (uint16_t)(int16_t)s->hw_cfg.gain_offset_cfg.sensor_offset.gb_offset;
    g_clean.module_cfg.gain_offset_cfg.sensor_offset[3] =
        (uint16_t)(int16_t)s->hw_cfg.gain_offset_cfg.sensor_offset.b_offset;
    g_clean.module_cfg.gain_offset_cfg.offset[0] =
        (uint16_t)(int16_t)s->hw_cfg.gain_offset_cfg.offset.r_offset;
    g_clean.module_cfg.gain_offset_cfg.offset[1] =
        (uint16_t)(int16_t)s->hw_cfg.gain_offset_cfg.offset.gr_offset;
    g_clean.module_cfg.gain_offset_cfg.offset[2] =
        (uint16_t)(int16_t)s->hw_cfg.gain_offset_cfg.offset.gb_offset;
    g_clean.module_cfg.gain_offset_cfg.offset[3] =
        (uint16_t)(int16_t)s->hw_cfg.gain_offset_cfg.offset.b_offset;
    g_clean.module_cfg.gain_offset_cfg.gain[0] =
        s->hw_cfg.gain_offset_cfg.gain.r_gain;
    g_clean.module_cfg.gain_offset_cfg.gain[1] =
        s->hw_cfg.gain_offset_cfg.gain.gr_gain;
    g_clean.module_cfg.gain_offset_cfg.gain[2] =
        s->hw_cfg.gain_offset_cfg.gain.gb_gain;
    g_clean.module_cfg.gain_offset_cfg.gain[3] =
        s->hw_cfg.gain_offset_cfg.gain.b_gain;
    g_clean.module_cfg.lens_cfg.ct_x = s->hw_cfg.lens_cfg.lens_shading_cfg.ct_x;
    g_clean.module_cfg.lens_cfg.ct_y = s->hw_cfg.lens_cfg.lens_shading_cfg.ct_y;
    g_clean.module_cfg.lens_cfg.rs_val = s->hw_cfg.lens_cfg.lens_shading_cfg.rs_val;
    /* Seed the 3A windows: store_sdk(APPLY_SETTINGS) always copies them back, and
     * zeroed windows mis-place 3A stats (AWB width 0 packs as 0xffffffff). */
    g_clean.module_cfg.ae_cfg.ae_reg_win.hor_num =
        s->hw_cfg.ae_cfg.ae_reg_window.horizontal_count;
    g_clean.module_cfg.ae_cfg.ae_reg_win.ver_num =
        s->hw_cfg.ae_cfg.ae_reg_window.vertical_count;
    g_clean.module_cfg.ae_cfg.ae_reg_win.width =
        s->hw_cfg.ae_cfg.ae_reg_window.width;
    g_clean.module_cfg.ae_cfg.ae_reg_win.height =
        s->hw_cfg.ae_cfg.ae_reg_window.height;
    g_clean.module_cfg.ae_cfg.ae_reg_win.hor_start =
        s->hw_cfg.ae_cfg.ae_reg_window.horizontal_start;
    g_clean.module_cfg.ae_cfg.ae_reg_win.ver_start =
        s->hw_cfg.ae_cfg.ae_reg_window.vertical_start;

    g_clean.module_cfg.af_cfg.af_reg_win.hor_num =
        s->hw_cfg.af_cfg.af_reg_window.horizontal_count;
    g_clean.module_cfg.af_cfg.af_reg_win.ver_num =
        s->hw_cfg.af_cfg.af_reg_window.vertical_count;
    g_clean.module_cfg.af_cfg.af_reg_win.width =
        s->hw_cfg.af_cfg.af_reg_window.width;
    g_clean.module_cfg.af_cfg.af_reg_win.height =
        s->hw_cfg.af_cfg.af_reg_window.height;
    g_clean.module_cfg.af_cfg.af_reg_win.hor_start =
        s->hw_cfg.af_cfg.af_reg_window.horizontal_start;
    g_clean.module_cfg.af_cfg.af_reg_win.ver_start =
        s->hw_cfg.af_cfg.af_reg_window.vertical_start;

    g_clean.module_cfg.hist_cfg.hist_reg_win.hor_num =
        s->hw_cfg.hist_cfg.hist_reg_window.horizontal_count;
    g_clean.module_cfg.hist_cfg.hist_reg_win.ver_num =
        s->hw_cfg.hist_cfg.hist_reg_window.vertical_count;
    g_clean.module_cfg.hist_cfg.hist_reg_win.width =
        s->hw_cfg.hist_cfg.hist_reg_window.width;
    g_clean.module_cfg.hist_cfg.hist_reg_win.height =
        s->hw_cfg.hist_cfg.hist_reg_window.height;
    g_clean.module_cfg.hist_cfg.hist_reg_win.hor_start =
        s->hw_cfg.hist_cfg.hist_reg_window.horizontal_start;
    g_clean.module_cfg.hist_cfg.hist_reg_win.ver_start =
        s->hw_cfg.hist_cfg.hist_reg_window.vertical_start;

    g_clean.module_cfg.awb_cfg.awb_reg_win.hor_num =
        s->hw_cfg.awb_cfg.awb_reg_window.horizontal_count;
    g_clean.module_cfg.awb_cfg.awb_reg_win.ver_num =
        s->hw_cfg.awb_cfg.awb_reg_window.vertical_count;
    g_clean.module_cfg.awb_cfg.awb_reg_win.width =
        s->hw_cfg.awb_cfg.awb_reg_window.width;
    g_clean.module_cfg.awb_cfg.awb_reg_win.height =
        s->hw_cfg.awb_cfg.awb_reg_window.height;
    g_clean.module_cfg.awb_cfg.awb_reg_win.hor_start =
        s->hw_cfg.awb_cfg.awb_reg_window.horizontal_start;
    g_clean.module_cfg.awb_cfg.awb_reg_win.ver_start =
        s->hw_cfg.awb_cfg.awb_reg_window.vertical_start;

    /* lens / MSC table-builder selectors */
    g_clean.ff_mod = t->ff_mod;
    g_clean.lsc_mode = t->lens_shading_mode;
    g_clean.mff_mod = t->mff_mod;
    g_clean.msc_mode = t->mesh_shading_mode;
    g_clean.rolloff_ratio = t->rolloff_ratio;
    g_clean.ev_analog_gain =
        (int32_t)((const fwi_sensor_settings_t *)&s->ae_entity.ae_result.sensor_set)
            ->ev_set_curr.ev_analog_gain;
    /* the shared temperature input is the AWB colour temperature */
    g_clean.af_result.temperature =
        s->awb_entity.awb_result.colour_temp_output;
    for (i = 0; i < ISP_MSC_TEMP_NUM; i++) {
        g_clean.msc_adjust_ratio[i] = s->shading_adjust[i];
        g_clean.msc_adjust_ratio_less[i] = s->shading_adjust_low[i];
    }
    for (i = 0; i < ISP_MSC_TBL_LENGTH; i++) {
        g_clean.msc_golden_ratio[i] = s->shading_golden[i];
        g_clean.msc_golden_flag[i] = s->shading_golden_flag[i];
    }
    g_clean.msc_r_ratio = s->shading_r_ratio;

    /* Seed table buffers so words the builder does not overwrite survive the
     * write-back. */
    if (s->hw_cfg.linearize_table)
        memcpy(g_clean.module_cfg.linear_table, s->hw_cfg.linearize_table,
               ISP_LINEAR_TBL_N * sizeof(uint16_t));
    if (s->hw_cfg.lens_table)
        memcpy(g_clean.module_cfg.lens_table, s->hw_cfg.lens_table,
               ISP_LSC_TBL_SIZE * sizeof(uint16_t));
    if (s->hw_cfg.mesh_shading_table)
        memcpy(g_clean.module_cfg.msc_table, s->hw_cfg.mesh_shading_table,
               ISP_MSC_TBL_SIZE * sizeof(uint16_t));
    /* Seed WDR: config_wdr regenerates only when flag!=0 but store_sdk(WDR)
     * always copies back; unseeded, steady state zeroes the WDR table. */
    memcpy(g_clean.module_cfg.wdr_cfg.table,
           s->hw_cfg.wdr_cfg.wdr_table,
           sizeof(g_clean.module_cfg.wdr_cfg.table));
    /* Seed RGB2YUV: rebuilt only on effect (bit 6)/hue (bit 10) flags but always
     * copied back; unseeded, regs 0x520..0x538 zero (flat green picture). */
    for (i = 0; i < 3; i++) {
        unsigned j;
        for (j = 0; j < 3; j++)
            g_clean.module_cfg.rgb2yuv.gain[i][j] =
                (int16_t)s->hw_cfg.rgb2yuv.matrix[i][j];
        g_clean.module_cfg.rgb2yuv.offset[i] =
            (int16_t)s->hw_cfg.rgb2yuv.offset[i];
    }

    /* dynamic judge / motion state input */
    g_clean.stats_ctx.dynamic_stats.enable = s->stats.dynamic_stats.enable;
    memcpy(g_clean.stats_ctx.dynamic_stats.accum,
           &s->stats.dynamic_stats.accum[0][0],
           sizeof(g_clean.stats_ctx.dynamic_stats.accum));
    memcpy(g_clean.stats_ctx.dynamic_stats.accum_last1,
           &s->stats.dynamic_stats.accum_last1[0][0],
           sizeof(g_clean.stats_ctx.dynamic_stats.accum_last1));
    memcpy(g_clean.stats_ctx.dynamic_stats.accum_last2,
           &s->stats.dynamic_stats.accum_last2[0][0],
           sizeof(g_clean.stats_ctx.dynamic_stats.accum_last2));
    memcpy(g_clean.stats_ctx.dynamic_stats.accum_last3,
           &s->stats.dynamic_stats.accum_last3[0][0],
           sizeof(g_clean.stats_ctx.dynamic_stats.accum_last3));
    for (i = 0; i < ISP_DYN_MOV_TH; i++)
        g_clean.stats_ctx.dynamic_stats.mov_th[i] =
            (uint32_t)s->stats.dynamic_stats.mov_threshold[i];
    for (i = 0; i < ISP_DYN_MOV_SAVE; i++)
        g_clean.stats_ctx.dynamic_stats.mov_save[i] =
            (uint32_t)s->stats.dynamic_stats.mov_save[i];
    memcpy(g_clean.stats_ctx.dynamic_stats.tdnf_comp,
           s->stats.dynamic_stats.temporal_denoise_comp,
           sizeof(g_clean.stats_ctx.dynamic_stats.tdnf_comp));
    memcpy(g_clean.stats_ctx.dynamic_stats.tdnf_diff_comp,
           s->stats.dynamic_stats.temporal_denoise_diff_comp,
           sizeof(g_clean.stats_ctx.dynamic_stats.tdnf_diff_comp));
    memcpy(g_clean.stats_ctx.dynamic_stats.lp_th_ratio_comp,
           s->stats.dynamic_stats.lp_threshold_ratio_comp,
           sizeof(g_clean.stats_ctx.dynamic_stats.lp_th_ratio_comp));
    memcpy(g_clean.stats_ctx.dynamic_stats.sharp_hfrq_comp,
           s->stats.dynamic_stats.sharp_high_frequency_comp,
           sizeof(g_clean.stats_ctx.dynamic_stats.sharp_hfrq_comp));
    memcpy(g_clean.stats_ctx.dynamic_stats.sharp_edge_comp,
           s->stats.dynamic_stats.sharp_edge_comp,
           sizeof(g_clean.stats_ctx.dynamic_stats.sharp_edge_comp));
    memcpy(g_clean.stats_ctx.dynamic_stats.sharp_under_shoot_comp,
           s->stats.dynamic_stats.sharp_under_shoot_comp,
           sizeof(g_clean.stats_ctx.dynamic_stats.sharp_under_shoot_comp));
    g_clean.stats_ctx.dynamic_stats.mov_old =
        (uint32_t)s->stats.dynamic_stats.mov_old;
    g_clean.stats_ctx.stats.ae.win_pix_n =
        s->stats.stats.ae_stats.window_pixel_n;

    build_tables(s);
}

/* Clean -> SDK write-back: only fields the op can mutate, since other SDK
 * fields were not carried losslessly (table shapes, stat widths differ). */
static void store_sdk(int op, struct fwi_isp_ctx *s)
{
    const fwi_ae_param_t *p = s->ae_entity.ae_param;
    unsigned i;

    switch (op) {
    case SHIM_OP_BAND_STEP:
        s->hw_cfg.afs_cfg.inc_line = g_clean.module_cfg.afs_cfg.inc_line;
        break;

    case SHIM_OP_LENS_CENTER:
        s->hw_cfg.lens_cfg.lens_shading_cfg.ct_x = g_clean.module_cfg.lens_cfg.ct_x;
        s->hw_cfg.lens_cfg.lens_shading_cfg.ct_y = g_clean.module_cfg.lens_cfg.ct_y;
        s->hw_cfg.lens_cfg.lens_shading_cfg.rs_val =
            g_clean.module_cfg.lens_cfg.rs_val;
        s->hw_cfg.disc_cfg.disc_ct_x = g_clean.module_cfg.disc_cfg.disc_ct_x;
        s->hw_cfg.disc_cfg.disc_ct_y = g_clean.module_cfg.disc_cfg.disc_ct_y;
        s->hw_cfg.disc_cfg.disc_rs_val =
            g_clean.module_cfg.disc_cfg.disc_rs_val;
        break;

    case SHIM_OP_DIG_GAIN:
        s->sensor.gain_offset.r_gain = g_clean.sensor_info.gain_offset[0];
        s->sensor.gain_offset.gr_gain = g_clean.sensor_info.gain_offset[1];
        s->sensor.gain_offset.gb_gain = g_clean.sensor_info.gain_offset[2];
        s->sensor.gain_offset.b_gain = g_clean.sensor_info.gain_offset[3];
        s->hw_cfg.gain_offset_cfg.gain.r_gain =
            g_clean.module_cfg.gain_offset_cfg.gain[0];
        s->hw_cfg.gain_offset_cfg.gain.gr_gain =
            g_clean.module_cfg.gain_offset_cfg.gain[1];
        s->hw_cfg.gain_offset_cfg.gain.gb_gain =
            g_clean.module_cfg.gain_offset_cfg.gain[2];
        s->hw_cfg.gain_offset_cfg.gain.b_gain =
            g_clean.module_cfg.gain_offset_cfg.gain[3];
        if (s->hw_cfg.linearize_table)
            memcpy(s->hw_cfg.linearize_table, g_clean.module_cfg.linear_table,
                   ISP_LINEAR_TBL_N * sizeof(uint16_t));
        break;

    case SHIM_OP_GAMMA:
        memcpy(s->hw_cfg.gamma_cfg.gamma_tbl,
               g_clean.module_cfg.gamma_cfg.gamma_tbl,
               sizeof(s->hw_cfg.gamma_cfg.gamma_tbl));
        break;

    case SHIM_OP_WDR:
        s->hw_cfg.wdr_cfg.wdr_low_threshold = g_clean.module_cfg.wdr_cfg.lo_th;
        s->hw_cfg.wdr_cfg.wdr_hi_threshold = g_clean.module_cfg.wdr_cfg.hi_th;
        s->hw_cfg.wdr_cfg.wdr_exposure_ratio =
            g_clean.module_cfg.wdr_cfg.exp_ratio;
        s->hw_cfg.wdr_cfg.wdr_slope = g_clean.module_cfg.wdr_cfg.slope;
        s->hw_cfg.wdr_cfg.wdr_mv_threshold = g_clean.module_cfg.wdr_cfg.mv_th;
        s->hw_cfg.wdr_cfg.wdr_mv_scale = g_clean.module_cfg.wdr_cfg.mv_scale;
        s->hw_cfg.wdr_cfg.wdr_output_select = g_clean.module_cfg.wdr_cfg.out_sel;
        memcpy(s->hw_cfg.wdr_cfg.wdr_table, g_clean.module_cfg.wdr_cfg.table,
               sizeof(s->hw_cfg.wdr_cfg.wdr_table));
        s->ae_ctl.ae_mode = (fwi_ae_mode_e)g_clean.ae_settings.ae_mode;
        s->ae_entity.ae_result.wdr_hi_threshold = g_clean.ae_result.wdr_hi_th;
        s->ae_entity.ae_result.wdr_low_threshold = g_clean.ae_result.wdr_low_th;
        if (p)
            ((fwi_ae_param_t *)(void *)p)->nor_cmd_mode =
                (bool)g_clean.ae_param.nor_cmd_mode;
        memcpy(s->inverse_gamma, g_clean.anti_gamma_tbl,
               sizeof(s->inverse_gamma));
        break;

    case SHIM_OP_COLORMATRIX:
        for (i = 0; i < 3; i++) {
            unsigned j;
            for (j = 0; j < 3; j++)
                s->hw_cfg.rgb2rgb_cfg.colour_matrix.matrix[i][j] =
                    (int16_t)g_clean.module_cfg.rgb2rgb_cfg.color_matrix[i][j];
            s->hw_cfg.rgb2rgb_cfg.colour_matrix.offset[i] =
                (int16_t)g_clean.module_cfg.rgb2rgb_cfg.color_offset[i];
        }
        s->dehaze_state.dehaze_changed = g_clean.defog_ctx.defog_changed;
        s->dehaze_state.min_rgb_pre[0] = (int32_t)g_clean.defog_ctx.min_rgb_pre[0];
        s->dehaze_state.dehaze_pre = g_clean.defog_ctx.defog_pre;
        break;

    case SHIM_OP_JUDGE:
        memcpy(&s->stats.dynamic_stats.accum[0][0],
               g_clean.stats_ctx.dynamic_stats.accum,
               sizeof(s->stats.dynamic_stats.accum));
        memcpy(s->stats.dynamic_stats.temporal_denoise_comp,
               &g_clean.stats_ctx.dynamic_stats.tdnf_comp[0],
               sizeof(s->stats.dynamic_stats.temporal_denoise_comp));
        memcpy(s->stats.dynamic_stats.temporal_denoise_diff_comp,
               &g_clean.stats_ctx.dynamic_stats.tdnf_diff_comp[0],
               sizeof(s->stats.dynamic_stats.temporal_denoise_diff_comp));
        memcpy(s->stats.dynamic_stats.lp_threshold_ratio_comp,
               &g_clean.stats_ctx.dynamic_stats.lp_th_ratio_comp[0],
               sizeof(s->stats.dynamic_stats.lp_threshold_ratio_comp));
        memcpy(s->stats.dynamic_stats.sharp_high_frequency_comp,
               &g_clean.stats_ctx.dynamic_stats.sharp_hfrq_comp[0],
               sizeof(s->stats.dynamic_stats.sharp_high_frequency_comp));
        memcpy(s->stats.dynamic_stats.sharp_edge_comp,
               &g_clean.stats_ctx.dynamic_stats.sharp_edge_comp[0],
               sizeof(s->stats.dynamic_stats.sharp_edge_comp));
        memcpy(s->stats.dynamic_stats.sharp_under_shoot_comp,
               &g_clean.stats_ctx.dynamic_stats.sharp_under_shoot_comp[0],
               sizeof(s->stats.dynamic_stats.sharp_under_shoot_comp));
        s->stats.dynamic_stats.temporal_denoise_comp_target =
            g_clean.stats_ctx.dynamic_stats.tdnf_comp_target;
        s->stats.dynamic_stats.temporal_denoise_diff_comp_target =
            g_clean.stats_ctx.dynamic_stats.tdnf_diff_comp_target;
        s->stats.dynamic_stats.lp_threshold_ratio_comp_target =
            g_clean.stats_ctx.dynamic_stats.lp_th_ratio_comp_target;
        s->stats.dynamic_stats.sharp_high_frequency_comp_target =
            g_clean.stats_ctx.dynamic_stats.sharp_hfrq_comp_target;
        s->stats.dynamic_stats.sharp_edge_comp_target =
            g_clean.stats_ctx.dynamic_stats.sharp_edge_comp_target;
        s->stats.dynamic_stats.sharp_under_shoot_comp_target =
            g_clean.stats_ctx.dynamic_stats.sharp_under_shoot_comp_target;
        s->stats.dynamic_stats.mov =
            (int32_t)g_clean.stats_ctx.dynamic_stats.mov;
        s->stats.dynamic_stats.mov_old =
            (int32_t)g_clean.stats_ctx.dynamic_stats.mov_old;
        for (i = 0; i < ISP_DYN_MOV_SAVE; i++)
            s->stats.dynamic_stats.mov_save[i] =
                (int32_t)g_clean.stats_ctx.dynamic_stats.mov_save[i];
        if (s->af_entity.af_param)
            s->af_entity.af_param->mov = g_clean.af_param.mov;
        break;

    case SHIM_OP_STATS:
    case SHIM_OP_STATS_SYNC:
        /* AE */
        memcpy(&s->stats.stats.ae_stats.accum_r[0][0],
               g_clean.stats_ctx.stats.ae.win_r,
               sizeof(s->stats.stats.ae_stats.accum_r));
        memcpy(&s->stats.stats.ae_stats.accum_g[0][0],
               g_clean.stats_ctx.stats.ae.win_g,
               sizeof(s->stats.stats.ae_stats.accum_g));
        memcpy(&s->stats.stats.ae_stats.accum_b[0][0],
               g_clean.stats_ctx.stats.ae.win_b,
               sizeof(s->stats.stats.ae_stats.accum_b));
        for (i = 0; i < ISP_AE_WIN_N; i++)
            s->stats.stats.ae_stats.average[i] =
                g_clean.stats_ctx.stats.ae.luma[i];
        memcpy(s->stats.stats.ae_stats.hist,
               g_clean.stats_ctx.stats.ae.hist,
               sizeof(s->stats.stats.ae_stats.hist));
        s->stats.stats.ae_stats.window_pixel_n =
            g_clean.stats_ctx.stats.ae.win_pix_n;
        /* AWB */
        memcpy(&s->stats.stats.awb_stats.awb_sum_r[0][0],
               g_clean.stats_ctx.stats.awb.sum_r,
               sizeof(s->stats.stats.awb_stats.awb_sum_r));
        memcpy(&s->stats.stats.awb_stats.awb_sum_g[0][0],
               g_clean.stats_ctx.stats.awb.sum_g,
               sizeof(s->stats.stats.awb_stats.awb_sum_g));
        memcpy(&s->stats.stats.awb_stats.awb_sum_b[0][0],
               g_clean.stats_ctx.stats.awb.sum_b,
               sizeof(s->stats.stats.awb_stats.awb_sum_b));
        memcpy(&s->stats.stats.awb_stats.awb_sum_count[0][0],
               g_clean.stats_ctx.stats.awb.count,
               sizeof(s->stats.stats.awb_stats.awb_sum_count));
        memcpy(&s->stats.stats.awb_stats.awb_average_r[0][0],
               g_clean.stats_ctx.stats.awb.avg_r,
               sizeof(s->stats.stats.awb_stats.awb_average_r));
        memcpy(&s->stats.stats.awb_stats.awb_average_g[0][0],
               g_clean.stats_ctx.stats.awb.avg_g,
               sizeof(s->stats.stats.awb_stats.awb_average_g));
        memcpy(&s->stats.stats.awb_stats.awb_average_b[0][0],
               g_clean.stats_ctx.stats.awb.avg_b,
               sizeof(s->stats.stats.awb_stats.awb_average_b));
        memcpy(&s->stats.stats.awb_stats.average[0][0],
               g_clean.stats_ctx.stats.awb.avg,
               sizeof(s->stats.stats.awb_stats.average));
        /* AF */
        memcpy(&s->stats.stats.af_stats.af_iir[0][0],
               g_clean.stats_ctx.stats.af.iir,
               sizeof(s->stats.stats.af_stats.af_iir));
        memcpy(&s->stats.stats.af_stats.af_fir[0][0],
               g_clean.stats_ctx.stats.af.fir,
               sizeof(s->stats.stats.af_stats.af_fir));
        memcpy(&s->stats.stats.af_stats.af_iir_count[0][0],
               g_clean.stats_ctx.stats.af.iir_cnt,
               sizeof(s->stats.stats.af_stats.af_iir_count));
        memcpy(&s->stats.stats.af_stats.af_fir_count[0][0],
               g_clean.stats_ctx.stats.af.fir_cnt,
               sizeof(s->stats.stats.af_stats.af_fir_count));
        memcpy(&s->stats.stats.af_stats.af_hlt_count[0][0],
               g_clean.stats_ctx.stats.af.hlt_cnt,
               sizeof(s->stats.stats.af_stats.af_hlt_count));
        memcpy(&s->stats.stats.af_stats.af_count[0][0],
               g_clean.stats_ctx.stats.af.count,
               sizeof(s->stats.stats.af_stats.af_count));
        memcpy(&s->stats.stats.af_stats.af_h_d1[0][0],
               g_clean.stats_ctx.stats.af.h_d1,
               sizeof(s->stats.stats.af_stats.af_h_d1));
        memcpy(&s->stats.stats.af_stats.af_h_d2[0][0],
               g_clean.stats_ctx.stats.af.h_d2,
               sizeof(s->stats.stats.af_stats.af_h_d2));
        memcpy(&s->stats.stats.af_stats.af_v_d1[0][0],
               g_clean.stats_ctx.stats.af.v_d1,
               sizeof(s->stats.stats.af_stats.af_v_d1));
        memcpy(&s->stats.stats.af_stats.af_v_d2[0][0],
               g_clean.stats_ctx.stats.af.v_d2,
               sizeof(s->stats.stats.af_stats.af_v_d2));
        /* AFS */
        memcpy(s->stats.stats.afs_stats.afs_sum,
               g_clean.stats_ctx.stats.afs.sum,
               sizeof(s->stats.stats.afs_stats.afs_sum));
        s->stats.stats.afs_stats.pic_width =
            g_clean.stats_ctx.stats.afs.pic_w;
        s->stats.stats.afs_stats.pic_height =
            g_clean.stats_ctx.stats.afs.pic_h;
        /* PLTM */
        memcpy(s->stats.stats.pltm_stats.lst,
               g_clean.stats_ctx.stats.pltm.lst,
               sizeof(s->stats.stats.pltm_stats.lst));
        s->stats.stats.pltm_stats.average_before_pltm =
            (uint16_t)g_clean.stats_ctx.stats.pltm.avg_before;
        s->stats.stats.pltm_stats.min_before_pltm =
            (uint16_t)g_clean.stats_ctx.stats.pltm.min_before;
        s->stats.stats.pltm_stats.max_before_pltm =
            (uint16_t)g_clean.stats_ctx.stats.pltm.max_before;
        s->stats.stats.pltm_stats.average_after_pltm =
            (uint16_t)g_clean.stats_ctx.stats.pltm.avg_after;
        s->stats.stats.pltm_stats.min_after_pltm =
            (uint16_t)g_clean.stats_ctx.stats.pltm.min_after;
        s->stats.stats.pltm_stats.max_after_pltm =
            (uint16_t)g_clean.stats_ctx.stats.pltm.max_after;
        /* dynamic accumulation produced by the AE handler */
        memcpy(&s->stats.dynamic_stats.accum[0][0],
               g_clean.stats_ctx.dynamic_stats.accum,
               sizeof(s->stats.dynamic_stats.accum));
        /* Point every entity's stats handle at the parsed SDK block (as the vendor
         * does); left NULL, the 3A cores see no stats and the image goes green. */
        s->awb_entity.awb_stats.awb_stats = &s->stats.stats.awb_stats;
        s->ae_entity.ae_stats.ae_stats = &s->stats.stats.ae_stats;
        s->af_entity.af_stats.af_stats = &s->stats.stats.af_stats;
        s->md_entity.md_stats.md_stats =
            (fwi_md_stats_t *)(void *)&s->stats.stats;
        s->afs_entity.afs_stats.afs_stats = &s->stats.stats.afs_stats;
        s->gtm_entity.gtm_stats.gtm_stats =
            (fwi_gtm_stats_t *)(void *)&s->stats.stats;
        s->pltm_entity.pltm_stats.pltm_stats =
            &s->stats.stats.pltm_stats;
        s->rolloff_entity.rolloff_stats.rolloff_stats =
            (fwi_stats_t *)(void *)&s->stats.stats;
        break;

    case SHIM_OP_APPLY_SETTINGS:
        for (i = 0; i < 3; i++) {
            unsigned j;
            for (j = 0; j < 3; j++)
                s->hw_cfg.rgb2yuv.matrix[i][j] =
                    (int16_t)g_clean.module_cfg.rgb2yuv.gain[i][j];
            s->hw_cfg.rgb2yuv.offset[i] =
                (int16_t)g_clean.module_cfg.rgb2yuv.offset[i];
        }
        s->hw_cfg.ae_cfg.ae_reg_window.horizontal_count =
            g_clean.module_cfg.ae_cfg.ae_reg_win.hor_num;
        s->hw_cfg.ae_cfg.ae_reg_window.vertical_count =
            g_clean.module_cfg.ae_cfg.ae_reg_win.ver_num;
        s->hw_cfg.ae_cfg.ae_reg_window.width =
            g_clean.module_cfg.ae_cfg.ae_reg_win.width;
        s->hw_cfg.ae_cfg.ae_reg_window.height =
            g_clean.module_cfg.ae_cfg.ae_reg_win.height;
        s->hw_cfg.ae_cfg.ae_reg_window.horizontal_start =
            g_clean.module_cfg.ae_cfg.ae_reg_win.hor_start;
        s->hw_cfg.ae_cfg.ae_reg_window.vertical_start =
            g_clean.module_cfg.ae_cfg.ae_reg_win.ver_start;
        s->hw_cfg.af_cfg.af_reg_window.horizontal_count =
            g_clean.module_cfg.af_cfg.af_reg_win.hor_num;
        s->hw_cfg.af_cfg.af_reg_window.vertical_count =
            g_clean.module_cfg.af_cfg.af_reg_win.ver_num;
        s->hw_cfg.af_cfg.af_reg_window.width =
            g_clean.module_cfg.af_cfg.af_reg_win.width;
        s->hw_cfg.af_cfg.af_reg_window.height =
            g_clean.module_cfg.af_cfg.af_reg_win.height;
        s->hw_cfg.af_cfg.af_reg_window.horizontal_start =
            g_clean.module_cfg.af_cfg.af_reg_win.hor_start;
        s->hw_cfg.af_cfg.af_reg_window.vertical_start =
            g_clean.module_cfg.af_cfg.af_reg_win.ver_start;
        s->hw_cfg.hist_cfg.hist_reg_window.horizontal_count =
            g_clean.module_cfg.hist_cfg.hist_reg_win.hor_num;
        s->hw_cfg.hist_cfg.hist_reg_window.vertical_count =
            g_clean.module_cfg.hist_cfg.hist_reg_win.ver_num;
        s->hw_cfg.hist_cfg.hist_reg_window.width =
            g_clean.module_cfg.hist_cfg.hist_reg_win.width;
        s->hw_cfg.hist_cfg.hist_reg_window.height =
            g_clean.module_cfg.hist_cfg.hist_reg_win.height;
        s->hw_cfg.hist_cfg.hist_reg_window.horizontal_start =
            g_clean.module_cfg.hist_cfg.hist_reg_win.hor_start;
        s->hw_cfg.hist_cfg.hist_reg_window.vertical_start =
            g_clean.module_cfg.hist_cfg.hist_reg_win.ver_start;
        s->hw_cfg.awb_cfg.awb_reg_window.horizontal_count =
            g_clean.module_cfg.awb_cfg.awb_reg_win.hor_num;
        s->hw_cfg.awb_cfg.awb_reg_window.vertical_count =
            g_clean.module_cfg.awb_cfg.awb_reg_win.ver_num;
        s->hw_cfg.awb_cfg.awb_reg_window.width =
            g_clean.module_cfg.awb_cfg.awb_reg_win.width;
        s->hw_cfg.awb_cfg.awb_reg_window.height =
            g_clean.module_cfg.awb_cfg.awb_reg_win.height;
        s->hw_cfg.awb_cfg.awb_reg_window.horizontal_start =
            g_clean.module_cfg.awb_cfg.awb_reg_win.hor_start;
        s->hw_cfg.awb_cfg.awb_reg_window.vertical_start =
            g_clean.module_cfg.awb_cfg.awb_reg_win.ver_start;
        if (p) {
            fwi_ae_param_t *ap = (fwi_ae_param_t *)(void *)p;
            ap->ae_init.ae_gain_bias = g_clean.ae_param.ae_ini.gain_favour;
            ap->ae_init.ae_analog_gain_range[0] =
                g_clean.ae_param.ae_ini.analog_gain_min;
            ap->ae_init.ae_analog_gain_range[1] =
                g_clean.ae_param.ae_ini.analog_gain_max;
            ap->ae_init.ae_digital_gain_range[0] =
                g_clean.ae_param.ae_ini.digital_gain_min;
            ap->ae_init.ae_digital_gain_range[1] =
                g_clean.ae_param.ae_ini.digital_gain_max;
            if ((s->pending_3a_changes & (1u << 8)) && g_clean.ae_spot)
                ap->ae_setting = s->ae_ctl;
        }
        if (s->afs_entity.afs_param)
            s->afs_entity.afs_param->flicker_mode =
                (uint8_t)g_clean.afs_param.flicker_mode;
        s->ae_frame_count = g_clean.ae_frame_cnt;
        s->af_frame_count = g_clean.af_frame_cnt;
        s->awb_frame_count = g_clean.awb_frame_cnt;
        s->pending_3a_changes = g_clean.isp_3a_change_flags;
        break;

    case SHIM_OP_LENS_TABLE:
        if (s->hw_cfg.lens_table)
            memcpy(s->hw_cfg.lens_table, g_clean.module_cfg.lens_table,
                   ISP_LSC_TBL_SIZE * sizeof(uint16_t));
        break;

    case SHIM_OP_MSC_TABLE:
        if (s->hw_cfg.mesh_shading_table)
            memcpy(s->hw_cfg.mesh_shading_table, g_clean.module_cfg.msc_table,
                   ISP_MSC_TBL_SIZE * sizeof(uint16_t));
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Entry points                                                        */
/* ------------------------------------------------------------------ */

void config_band_step(struct fwi_isp_ctx *isp_gen)
{
    if (!isp_gen)
        return;
    bind_sdk(isp_gen);
    clean_config_band_step(&g_clean);
    store_sdk(SHIM_OP_BAND_STEP, isp_gen);
}

void config_lens_center(struct fwi_isp_ctx *isp_gen)
{
    if (!isp_gen)
        return;
    bind_sdk(isp_gen);
    clean_config_lens_center(&g_clean);
    store_sdk(SHIM_OP_LENS_CENTER, isp_gen);
}

void config_dig_gain(struct fwi_isp_ctx *isp_gen,
                     unsigned int exp_digital_gain)
{
    if (!isp_gen)
        return;
    bind_sdk(isp_gen);
    clean_config_dig_gain(&g_clean, (int32_t)exp_digital_gain);
    store_sdk(SHIM_OP_DIG_GAIN, isp_gen);
}

void config_gamma(struct fwi_isp_ctx *isp_gen)
{
    if (!isp_gen)
        return;
    bind_sdk(isp_gen);
    clean_config_gamma(&g_clean);
    store_sdk(SHIM_OP_GAMMA, isp_gen);
}

void config_wdr(struct fwi_isp_ctx *isp_gen, int flag)
{
    if (!isp_gen)
        return;
    bind_sdk(isp_gen);
    clean_config_wdr(&g_clean, flag);
    store_sdk(SHIM_OP_WDR, isp_gen);
}

void isp_apply_colormatrix(struct fwi_isp_ctx *isp_gen)
{
    if (!isp_gen)
        return;
    bind_sdk(isp_gen);
    clean_isp_apply_colormatrix(&g_clean);
    store_sdk(SHIM_OP_COLORMATRIX, isp_gen);
}

void __isp_stat_dynamic_judge(struct fwi_isp_ctx *isp_gen)
{
    if (!isp_gen)
        return;
    bind_sdk(isp_gen);
    clean___isp_stat_dynamic_judge(&g_clean);
    store_sdk(SHIM_OP_JUDGE, isp_gen);
}

void isp_handle_stats(struct fwi_isp_ctx *isp_gen, const void *buffer)
{
    if (!isp_gen || !buffer)
        return;
    bind_sdk(isp_gen);
    clean_isp_handle_stats(&g_clean, buffer);
    store_sdk(SHIM_OP_STATS, isp_gen);
}

void isp_handle_stats_sync(struct fwi_isp_ctx *isp_gen, const void *buffer0,
                           const void *buffer1)
{
    if (!isp_gen || !buffer0 || !buffer1)
        return;
    bind_sdk(isp_gen);
    clean_isp_handle_stats_sync(&g_clean, buffer0, buffer1);
    store_sdk(SHIM_OP_STATS_SYNC, isp_gen);
}

/* SDK-side provider of base.h's freeisp_ae_set_params: map the clean ae_param
 * update onto the SDK ae_param and dispatch via the framework helper. */
void freeisp_ae_set_params(fwi_base_ae_entity_ctx_t *ae_ctx, int32_t cmd_type)
{
    struct fwi_isp_ctx *s = g_cur_sdk;
    const fwi_base_ae_param_t *c;
    fwi_ae_param_t *p;

    if (!s || !ae_ctx || !(c = ae_ctx->ae_param))
        return;
    p = s->ae_entity.ae_param;
    if (!p)
        return;

    if (cmd_type == FWI_ISP_AE_UPDATE_AE_TABLE) {
        /* set_ae_ini_from_gains already applied the vendor digital gain
         * shift (<<2), so the clean values copy verbatim. */
        p->ae_init.ae_gain_bias = c->ae_ini.gain_favour;
        p->ae_init.ae_analog_gain_range[0] = c->ae_ini.analog_gain_min;
        p->ae_init.ae_analog_gain_range[1] = c->ae_ini.analog_gain_max;
        p->ae_init.ae_digital_gain_range[0] = c->ae_ini.digital_gain_min;
        p->ae_init.ae_digital_gain_range[1] = c->ae_ini.digital_gain_max;
    } else if (cmd_type == FWI_ISP_AE_BUILD_TOUCH_WEIGHT) {
        /* The vendor base snapshot is the SDK settings block itself. */
        memcpy(&p->ae_setting, &s->ae_ctl, sizeof(s->ae_ctl));
    }

    isp_ae_set_params_helper(&s->ae_entity, (fwi_ae_param_type_e)cmd_type);
}

void isp_apply_settings(struct fwi_isp_ctx *isp_gen)
{
    if (!isp_gen)
        return;
    g_cur_sdk = isp_gen;
    bind_sdk(isp_gen);
    clean_isp_apply_settings(&g_clean);
    g_cur_sdk = NULL;
    store_sdk(SHIM_OP_APPLY_SETTINGS, isp_gen);
}

void config_lens_table(struct fwi_isp_ctx *isp_gen, int vcm_std_pos)
{
    if (!isp_gen)
        return;
    bind_sdk(isp_gen);
    clean_config_lens_table(&g_clean, vcm_std_pos);
    store_sdk(SHIM_OP_LENS_TABLE, isp_gen);
}

void config_msc_table(struct fwi_isp_ctx *isp_gen, int vcm_std_pos)
{
    if (!isp_gen)
        return;
    bind_sdk(isp_gen);
    clean_config_msc_table(&g_clean, vcm_std_pos);
    store_sdk(SHIM_OP_MSC_TABLE, isp_gen);
}

/* config_blc: declared by the SDK but absent from the vendor object and never
 * called; no-op so stray references link. */
void config_blc(struct fwi_isp_ctx *isp_gen)
{
    (void)isp_gen;
}
