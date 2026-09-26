/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * ae_shim.c - AE (auto-exposure) integration shim.
 *
 * Presents the clean-room AE core (src/ae/ae_clean.c) to the
 * Yi/mediad ISP framework through the SDK isp_ae_core_ops_t contract.
 * Integration glue only: no algorithm is implemented here.
 *
 * The vendor entity embeds its live `ae_param_t config` at offset 0 and hands
 * that pointer to the framework from get_params(); the framework then writes
 * frame id / settings / sensor info into it in place each frame (see
 * libisp/isp_manage/isp_manage.c's AE set-params step) and calls run without a
 * set_params.  The shim reproduces that: the entity's first member is the SDK
 * mirror, get_params() returns it, and run() re-applies the whole mirror to
 * the clean core before running it, so the clean core always sees live
 * values.
 *
 * Field mapping (SDK ae_param_t -> clean ae_params_t):
 *
 *   runtime (ae_setting):
 *     clean ev_bias              <- exp_compensation
 *     clean fixed_exposure       <- exp_absolute         (us)
 *     clean sensor_gain          <- sensor_gain          (Q4)
 *     clean iso_sensitivity      <- iso_sensitivity
 *     clean aperture             <- iris_fno
 *     clean exposure_mode        <- exp_mode
 *     clean iso_control          <- iso_mode
 *     clean light_mode           <- light_mode
 *     clean metering_mode        <- exp_metering_mode
 *     clean hdr_mode             <- ae_mode
 *     clean mains_detected       <- flicker_type  (detected, 0/1/2)
 *     clean scene                <- scene_mode
 *     clean flash_mode           <- flash_mode
 *     clean meter_roi            <- ae_coor
 *     clean exposure_locked      <- exposure_lock
 *     clean flash_open           <- flash_open
 *     clean capture_stage        <- take_pic_start_cnt
 *     clean target_comp          <- ae_target_comp
 *     clean exposure_cfg[14]     <- exposure_cfg[14]
 *
 *   static (ae_ini):
 *     clean table_source         <- define_ae_table
 *     clean max_lv               <- ae_max_lv
 *     clean window_weight_seed   <- ae_win_weight[64]
 *     clean hist_metering_en     <- ae_hist_mod_en
 *     clean kernel_index         <- ae_ConvDataIndex
 *     clean error_delay_frames   <- ae_delay_frame
 *     clean exp_delay_frames     <- exp_delay_frame
 *     clean gain_delay_frames    <- gain_delay_frame
 *     clean ev_comp_step         <- exp_comp_step
 *     clean touch_distance_index <- ae_touch_dist_ind
 *     clean high_fps_handling_en <- ae_handle_high_fps_en
 *     clean iso_to_gain_ratio    <- ae_iso2gain_ratio
 *     clean aperture_ladder[16]  <- ae_fno_step[16]
 *     clean wdr_cfg[4]           <- wdr_cfg[4]
 *     clean total_gain_range[2]  <- ae_total_gain_range[2]
 *     clean analog_gain_range[2] <- ae_analog_gain_range[2]
 *     clean digital_gain_range[2]<- ae_digital_gain_range[2]
 *     clean scene_tables[16]     <- ae_tbl_scene[16]     (byte copy)
 *     clean gain_split_ratio     <- gain_ratio           (double)
 *
 *   sensor (ae_sensor_info): pclk, hts, vts, frame_time, gain_min, gain_max,
 *     sensor_width, sensor_height, hflip, vflip.
 *
 *   test (test_cfg): ae_en -> test_enable; ae_forced -> test_forced;
 *     isp_gain -> test_gain; isp_exp_line -> test_exp_line;
 *     lum_forced, isp_test_exptime, exp_line_start/step/end,
 *     exp_change_interval, isp_test_gain -> test_gain_en, gain_start/step/end,
 *     gain_change_interval, ae_check_delay_en -> delay_en,
 *     ae_delay_type -> delay_type.
 *
 * Parameter kind is a 1:1 mapping of the two enums:
 *   ISP_AE_INI_DATA=0 -> AE_PARAM_INIT, ISP_AE_UPDATE_AE_TABLE=1 ->
 *   AE_PARAM_TABLES, ISP_AE_SET_EXP_IDX=2 -> AE_PARAM_SET_INDEX,
 *   ISP_AE_BUILD_TOUCH_WEIGHT=3 -> AE_PARAM_TOUCH.
 *
 * Stats (SDK isp_ae_stats_s -> clean ae_stats_t):
 *   clean win_avg[384]   <- avg[384]         (saturated to 8-bit)
 *   clean hist[256]      <- hist[256]        (saturated to 8-bit)
 *   clean accum_r/g/b    <- accum_r/g/b      (byte copy of the 16x24 arrays)
 *   clean win_pix_n      <- win_pix_n
 *
 * Result translation (clean ae_result_t -> SDK ae_result_t):
 *   status / setting / setting_last / setting_curr / setting_short, idx_max,
 *   idx_expect, bright_pos, dark_pos, gain_ratio_out -> ae_gain, target,
 *   avg_lum, weight_lum, delta_idx, lv_adj, flash_ev_cumul; wdr_ratio
 *   (sensor/hw_ratio/tmp/last -> sensor/isp_hardware/tmp/last); wdr_hi_th,
 *   wdr_low_th, hist_low/mid/hi, backlight, gain_ratio.
 *
 * Intentionally unmapped:
 *   - SDK ae_ini.ae_win_weight vs clean window_weight_seed is mapped, but the
 *     clean default metering grids come from the injected wght_* tables.
 *   - ae_setting.flicker_mode (power_line_frequency) and
 *     ae_setting.wdr_output_select: no clean counterpart; only the detected
 *     flicker_type is consumed.
 *   - SDK ev_sensor_true_exp_line / ev_av / ev_tv / ev_sv and result
 *     ae_flash_ok / ae_flash_led / ae_wdr_delay: not produced by the clean
 *     core, left zero (the clean output contract, spec section 4, omits them;
 *     ev_av/tv/sv and the flash flags are also never written by the vendor).
 *   - set_params() produces no SDK result: the clean set-param path is a pure
 *     state update, so a result is only produced by the next run().
 *
 * Write-back: the clean core's resolved/mutated configuration is copied back
 * into the SDK mirror at the end of set/run (see sync_mirror): the seeded
 * scene slots 0..2 and aperture ladder (INIT), the analog/digital/total gain
 * ranges (line-table build) and exp_comp_step/sensor_gain/iso_sensitivity
 * (auto and manual-ISO run paths).  Every copy is gated by the same condition
 * the vendor uses (unset array, zero endpoint, zero step); the write-back is a
 * mirror update and does not feed any algorithm input the vendor would not see.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Clean-room side.  The clean entry points share names with the SDK ones
 * (ae_init/ae_exit/get/set/run/isr) and the clean ae_stats_t / ae_result_t
 * collide with the SDK typedefs, so rename the clean spellings for this
 * translation unit only.
 */
#define ae_stats_t     clean_ae_stats_t
#define ae_result_t    clean_ae_result_t
#define ae_init        clean_ae_init
#define ae_exit        clean_ae_exit
#define ae_get_params  clean_ae_get_params
#define ae_set_params  clean_ae_set_params
#define ae_run         clean_ae_run
#define ae_isr         clean_ae_isr
#include "ae_clean.h"
#undef ae_stats_t
#undef ae_result_t
#undef ae_init
#undef ae_exit
#undef ae_get_params
#undef ae_set_params
#undef ae_run
#undef ae_isr

/*
 * Framework side: the generated fwi_* ABI.  The shim exports ae_init/ae_exit
 * and a fwi_ae_core_ops_t vtable; the framework calls it directly.
 */
#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"

#include "ae_shim.h"

/*
 * The clean stats block stores the per-window averages and histogram as 8-bit
 * values and the accumulators as flat int arrays; the SDK exposes
 * HW_U32[16][24] accumulators and HW_U32[384]/[256] average/histogram.  Guard
 * the shapes the byte copies rely on.
 */
typedef char ae_win_size_check[
    (ISP_AE_ROW * ISP_AE_COL == AE_NWIN) ? 1 : -1];
typedef char ae_hist_size_check[
    (ISP_HIST_NUM == AE_NHIST) ? 1 : -1];
typedef char ae_scene_size_check[
    (FWI_SCENE_MODE_MAX == AE_NSCENE) ? 1 : -1];
typedef char ae_desc_size_check[
    (sizeof(struct fwi_ae_table_info) == sizeof(ae_desc_t)) ? 1 : -1];
typedef char ae_seg_size_check[
    (sizeof(struct fwi_ae_table) == sizeof(ae_segment_t)) ? 1 : -1];

/* ------------------------------------------------------------------ */
/* Table provider                                                      */
/* ------------------------------------------------------------------ */

static int32_t  g_log2[256];
static uint32_t g_evtab[AE_IDX_MAX];
static uint8_t  g_conv[32 * 128];
static uint32_t g_touchprob[6 * 12];
static int32_t  g_kernel[20 * 31];
static int16_t  g_pregamma[11 * 256];
static int32_t  g_blmask[3 * AE_NGRID];
static int32_t  g_wmatrix[AE_NGRID];
static int32_t  g_wavg[AE_NGRID];
static int32_t  g_wcenter[AE_NGRID];
static int32_t  g_wover[AE_NGRID];
static int32_t  g_wunder[AE_NGRID];
static int32_t  g_net_in[10 * 64];
static int32_t  g_net_bias[10];
static int32_t  g_net_out[20];
static ae_desc_t g_table_default;
static uint8_t  g_auxprob[96 * 256];
static int      g_defaults_ready;

/*
 * The aperture ladder is NOT compiled in: the clean contract's fno_ladder /
 * fno_def pointers stay NULL until real tables are installed with
 * ae_shim_set_tables().  The vendor default ladder (ae_fno_def) is extracted
 * from the camera's own rmm image at runtime and injected through fno_def.
 */
static const ae_clean_tables_t g_default_ae = {
    g_log2, g_evtab, g_conv, g_touchprob, g_kernel, g_pregamma,
    g_blmask, g_wmatrix, g_wavg, g_wcenter, g_wover, g_wunder,
    g_net_in, g_net_bias, g_net_out, NULL, NULL, &g_table_default,
    g_auxprob
};
static const freeisp_tables_t g_default_tables = { &g_default_ae };

static freeisp_tables_t g_set_tables;
static const freeisp_tables_t *g_tables = &g_default_tables;

static void ensure_defaults(void)
{
    int i, r, k;

    if (g_defaults_ready)
        return;

    /*
     * Pilot placeholder fixtures only (same shapes as the clean unit test):
     * they keep every clean index and denominator in range so the core can be
     * exercised.  Real tuning must be installed with ae_shim_set_tables()
     * before ae_init().  None of this is tuning data.  The aperture ladder is
     * deliberately absent here: it is injected from the camera's own rmm image
     * (ae_fno_def) rather than compiled in.
     */
    for (i = 0; i < 256; i++)
        g_log2[i] = (int32_t)floor(1000.0 * log2((double)(i + 1)) + 0.5);
    for (i = 0; i < AE_IDX_MAX; i++)
        g_evtab[i] = 1000;
    for (r = 0; r < 32; r++)
        for (k = 0; k < 128; k++)
            g_conv[r * 128 + k] = (r == 0) ? (uint8_t)k : (uint8_t)(k / 2);
    for (r = 0; r < 6; r++)
        for (k = 0; k < 12; k++)
            g_touchprob[r * 12 + k] =
                (uint32_t)((32 - k * 2 - r) > 0 ? (32 - k * 2 - r) : 0);
    for (r = 0; r < 20; r++)
        for (k = 0; k < 31; k++)
            g_kernel[r * 31 + k] = 31 - (k > 15 ? k - 15 : 15 - k);
    memset(g_pregamma, 0, sizeof(g_pregamma));
    memset(g_blmask, 0, sizeof(g_blmask));
    for (i = 0; i < AE_NGRID; i++) {
        g_blmask[2 * AE_NGRID + i] = 1;
        g_blmask[1 * AE_NGRID + 27] = 1;
        g_wmatrix[i] = 8;
        g_wavg[i] = 8;
        g_wcenter[i] = 1;
        g_wover[i] = 16;
        g_wunder[i] = 16;
    }
    g_wcenter[27] = 64;
    memset(g_net_in, 0, sizeof(g_net_in));
    memset(g_net_bias, 0, sizeof(g_net_bias));
    for (i = 0; i < 10; i++) {
        g_net_out[i] = 10;
        g_net_out[10 + i] = -10;
    }
    for (r = 0; r < 96; r++)
        for (k = 0; k < 256; k++)
            g_auxprob[r * 256 + k] = (uint8_t)(k < 200 ? 200 - k : 0);

    memset(&g_table_default, 0, sizeof(g_table_default));
    g_table_default.length = 2;
    g_table_default.ev_step = 40;
    g_table_default.shutter_shift = 0;
    g_table_default.seg[0].min_exp = 8000;
    g_table_default.seg[0].max_exp = 30;
    g_table_default.seg[0].min_gain = 256;
    g_table_default.seg[0].max_gain = 256;
    g_table_default.seg[0].min_iris = 266;
    g_table_default.seg[0].max_iris = 266;
    g_table_default.seg[1].min_exp = 30;
    g_table_default.seg[1].max_exp = 30;
    g_table_default.seg[1].min_gain = 256;
    g_table_default.seg[1].max_gain = 6000;
    g_table_default.seg[1].min_iris = 266;
    g_table_default.seg[1].max_iris = 266;

    g_defaults_ready = 1;
}

const freeisp_tables_t *freeisp_get_tables(void)
{
    ensure_defaults();
    return g_tables;
}

void ae_shim_set_tables(const void *tables)
{
    if (tables) {
        g_set_tables.ae = (const ae_clean_tables_t *)tables;
        g_tables = &g_set_tables;
    } else {
        g_tables = &g_default_tables;
    }
}

/* ------------------------------------------------------------------ */
/* Entity                                                              */
/* ------------------------------------------------------------------ */

/*
 * `config` is the first member so the SDK mirror sits at entity offset 0
 * (matching the vendor entity layout); get_params() returns &config.
 */
typedef struct ae_shim_entity {
    fwi_ae_param_t        config;  /* SDK mirror at offset 0              */
    ae_entity_t      *clean;   /* clean-room instance                 */
    clean_ae_result_t result;  /* persistent result across frames     */
} ae_shim_entity_t;

/* ------------------------------------------------------------------ */
/* Parameter mapping: ae_param_t -> ae_params_t                        */
/* ------------------------------------------------------------------ */

static void map_config(ae_params_t *c, const fwi_ae_param_t *s)
{
    const fwi_ae_init_cfg_t      *ini = &s->ae_init;
    const fwi_ae_controls_t *set = &s->ae_setting;
    const fwi_sensor_state_t *si  = &s->ae_sensor_info;
    const fwi_ae_test_cfg_t  *tc  = &s->test_cfg;
    int i;

    memset(c, 0, sizeof(*c));

    /* runtime settings */
    c->ev_bias              = set->exposure_compensation;
    c->fixed_exposure       = (int32_t)set->exposure_absolute;
    c->sensor_gain          = set->sensor_gain;
    c->iso_sensitivity      = set->iso_sensitivity;
    c->aperture             = set->iris_f_number;
    c->exposure_mode        = (int32_t)set->exposure_mode;
    c->iso_control          = (int32_t)set->iso_mode;
    c->light_mode           = (int32_t)set->light_mode;
    c->metering_mode        = (int32_t)set->exposure_metering_mode;
    c->hdr_mode             = (int32_t)set->ae_mode;
    c->mains_detected       = (int32_t)set->flicker_type;
    c->scene                = (int32_t)set->scene_mode;
    c->flash_mode           = (int32_t)set->flash_mode;
    c->meter_roi.x1         = set->ae_coord.x1;
    c->meter_roi.y1         = set->ae_coord.y1;
    c->meter_roi.x2         = set->ae_coord.x2;
    c->meter_roi.y2         = set->ae_coord.y2;
    c->exposure_locked      = set->exposure_lock ? 1 : 0;
    c->flash_open           = set->flash_open;
    c->capture_stage        = set->take_pic_start_count;
    c->target_comp          = s->ae_target_comp;
    for (i = 0; i < 14; i++)
        c->exposure_cfg[i] = set->exposure_cfg[i];

    /* static configuration */
    c->table_source         = ini->define_ae_table;
    c->max_lv               = ini->ae_max_level;
    for (i = 0; i < 64; i++)
        c->window_weight_seed[i] = ini->ae_zone_weight[i];
    c->hist_metering_en     = ini->ae_hist_mode_en;
    c->kernel_index         = ini->ae_conv_data_index;
    c->error_delay_frames   = ini->ae_delay_frame;
    c->exp_delay_frames     = ini->exposure_delay_frame;
    c->gain_delay_frames    = ini->gain_delay_frame;
    c->ev_comp_step         = ini->exposure_comp_step;
    c->touch_distance_index = ini->ae_touch_distance_ind;
    c->high_fps_handling_en = ini->ae_handle_high_fps_en;
    c->iso_to_gain_ratio    = ini->ae_iso2gain_ratio;
    for (i = 0; i < 16; i++)
        c->aperture_ladder[i] = ini->ae_f_number_step[i];
    for (i = 0; i < 4; i++)
        c->wdr_cfg[i] = ini->wdr_cfg[i];
    for (i = 0; i < 2; i++) {
        c->total_gain_range[i]   = ini->ae_total_gain_range[i];
        c->analog_gain_range[i]  = ini->ae_analog_gain_range[i];
        c->digital_gain_range[i] = ini->ae_digital_gain_range[i];
    }
    memcpy(c->scene_tables, ini->ae_tbl_scene, sizeof(c->scene_tables));
    c->gain_split_ratio     = ini->gain_ratio;

    /* sensor descriptor */
    c->pclk                 = (int32_t)si->pclk;
    c->hts                  = (int32_t)si->hts;
    c->vts                  = (int32_t)si->vts;
    c->frame_time           = (int32_t)si->frame_time;
    c->gain_min             = (int32_t)si->gain_min;
    c->gain_max             = (int32_t)si->gain_max;
    c->sensor_width         = si->sensor_width;
    c->sensor_height        = si->sensor_height;
    c->hflip                = si->hflip;
    c->vflip                = si->vflip;

    /* test / diagnostic configuration */
    c->test_enable            = tc->ae_en;
    c->test_forced            = tc->ae_forced;
    c->test_gain              = tc->gain;
    c->test_exp_line          = tc->exposure_line;
    c->lum_forced             = tc->luminance_forced;
    c->test_exptime           = tc->test_exposure_time;
    c->exp_line_start         = tc->exposure_line_start;
    c->exp_line_step          = tc->exposure_line_step;
    c->exp_line_end           = tc->exposure_line_end;
    c->exp_change_interval    = tc->exposure_change_interval;
    c->test_gain_en           = tc->test_gain;
    c->gain_start             = tc->gain_start;
    c->gain_step              = tc->gain_step;
    c->gain_end               = tc->gain_end;
    c->gain_change_interval   = tc->gain_change_interval;
    c->delay_en               = tc->ae_check_delay_en;
    c->delay_type             = (int32_t)tc->ae_delay_type;
}

/* ------------------------------------------------------------------ */
/* SDK write-back: clean params -> SDK ae_param_t mirror               */
/* ------------------------------------------------------------------ */
/*
 * The vendor core does not copy the SDK parameter block; the framework writes
 * the block through the pointer returned by get_params() and the core then
 * mutates parts of that same embedded mirror in place:
 *
 *   - on INIT it seeds scene slots 0..2 and the aperture ladder when the
 *     supplied arrays are unset (ae_ini.ae_tbl_scene[0].max_exp == 0,
 *     ae_ini.ae_fno_step[0] == 0);
 *   - when it builds the exposure line tables it rewrites the analog/digital
 *     gain ranges (when either endpoint is zero) and the total gain range,
 *     and, on the auto path, it defaults exp_comp_step to 4;
 *   - the manual-ISO path rewrites ae_setting.sensor_gain / iso_sensitivity.
 *
 * The clean core performs the same mutations on its own parameter block, so the
 * shim copies those fields back into the SDK mirror after each set/run.  This
 * keeps the two entities' stored ae_param_t byte-identical, which the
 * SDK-boundary differential compares.
 */
static void desc_to_sdk(struct fwi_ae_table_info *d, const ae_desc_t *s)
{
    int j;

    for (j = 0; j < AE_NSEG; j++) {
        d->ae_tbl[j].min_exposure  = s->seg[j].min_exp;
        d->ae_tbl[j].max_exposure  = s->seg[j].max_exp;
        d->ae_tbl[j].min_gain = s->seg[j].min_gain;
        d->ae_tbl[j].max_gain = s->seg[j].max_gain;
        d->ae_tbl[j].min_iris = s->seg[j].min_iris;
        d->ae_tbl[j].max_iris = s->seg[j].max_iris;
    }
    d->length        = (int32_t)s->length;
    d->ev_step       = (int32_t)s->ev_step;
    d->shutter_shift = (int32_t)s->shutter_shift;
}

static void sync_mirror(fwi_ae_param_t *sdk, ae_entity_t *clean)
{
    ae_params_t cp;
    int i;

    memset(&cp, 0, sizeof(cp));
    if (clean_ae_get_params(clean, &cp) != 0)
        return;

    for (i = 0; i < 3; i++)
        desc_to_sdk(&sdk->ae_init.ae_tbl_scene[i], &cp.scene_tables[i]);
    for (i = 0; i < 16; i++)
        sdk->ae_init.ae_f_number_step[i] = cp.aperture_ladder[i];

    for (i = 0; i < 2; i++) {
        sdk->ae_init.ae_analog_gain_range[i]  = cp.analog_gain_range[i];
        sdk->ae_init.ae_digital_gain_range[i] = cp.digital_gain_range[i];
        sdk->ae_init.ae_total_gain_range[i]   = cp.total_gain_range[i];
    }
    sdk->ae_init.exposure_comp_step      = cp.ev_comp_step;
    sdk->ae_setting.sensor_gain    = cp.sensor_gain;
    sdk->ae_setting.iso_sensitivity = cp.iso_sensitivity;
    sdk->ae_target_comp            = cp.target_comp;
}

/* ------------------------------------------------------------------ */
/* Statistics mapping: isp_ae_stats_s -> ae_stats_t                   */
/* ------------------------------------------------------------------ */

static uint8_t sat_u8(uint32_t v)
{
    return (uint8_t)(v > 255u ? 255u : v);
}

static void map_stats(clean_ae_stats_t *c, const fwi_ae_stats_desc_t *s)
{
    const struct fwi_ae_stats *st = s->ae_stats;
    int i;

    memset(c, 0, sizeof(*c));
    for (i = 0; i < AE_NWIN; i++)
        c->win_avg[i] = st->average[i];
    for (i = 0; i < AE_NHIST; i++)
        c->hist[i] = st->hist[i];
    memcpy(c->accum_r, st->accum_r, sizeof(c->accum_r));
    memcpy(c->accum_g, st->accum_g, sizeof(c->accum_g));
    memcpy(c->accum_b, st->accum_b, sizeof(c->accum_b));
    c->win_pix_n = (int32_t)st->window_pixel_n;
}

/* ------------------------------------------------------------------ */
/* Result translation: ae_result_t -> SDK ae_result_t                 */
/* ------------------------------------------------------------------ */

static void map_setting(struct fwi_ev_setting *d, const ae_setting_t *s)
{
    memset(d, 0, sizeof(*d));
    d->ev_exposure_time = (uint32_t)s->exposure_time;
    d->ev_analog_gain   = (uint32_t)s->analog_gain;
    d->ev_digital_gain  = (uint32_t)s->digital_gain;
    d->ev_total_gain    = (uint32_t)s->total_gain;
    d->ev_sensor_exposure_line = (uint32_t)s->sensor_exp_line;
    d->ev_f_number      = (uint32_t)s->f_number;
    d->ev_fno2          = (uint32_t)s->fno2;
    d->ev_level            = (uint32_t)s->lv;
    d->ev               = (uint32_t)s->ev;
    d->ev_index           = s->ev_idx;
}

/* fwi_ae_result.sensor_set is an opaque 176-byte blob in the generated ABI;
 * its layout is fwi_sensor_settings_t (the framework uses the same view). */
static fwi_sensor_settings_t *ev_sets(fwi_sensor_setting_t *s)
{
    return (fwi_sensor_settings_t *)s;
}

static void map_result(fwi_ae_result_t *r, const clean_ae_result_t *c, int write_short)
{
    r->ae_status = (fwi_ae_status_e)c->status;

    map_setting(&ev_sets(&r->sensor_set)->ev_set,        &c->setting);
    map_setting(&ev_sets(&r->sensor_set)->ev_set_last,   &c->setting_last);
    map_setting(&ev_sets(&r->sensor_set)->ev_set_curr,   &c->setting_curr);
    /*
     * The vendor only writes the short companion in WDR mode (and in its
     * default-result path when no statistics are supplied); in linear mode it
     * leaves sensor_set_short untouched.  Mirror that: linear runs do not
     * clobber whatever the framework already holds.
     */
    if (write_short) {
        map_setting(&ev_sets(&r->sensor_set_short)->ev_set, &c->setting_short);
        map_setting(&ev_sets(&r->sensor_set_short)->ev_set_curr, &c->setting_short_curr);
    }
    ev_sets(&r->sensor_set)->ev_index_max    = c->idx_max;
    ev_sets(&r->sensor_set)->ev_index_expect = c->idx_expect;

    r->bright_pixel_value = c->bright_pos;
    r->dark_pixel_value    = c->dark_pos;
    r->ae_gain           = (uint32_t)c->gain_ratio_out;
    r->ae_target         = c->target;
    r->ae_average_luminance        = c->avg_lum;
    r->ae_weight_luminance     = c->weight_lum;
    r->ae_delta_exposure_index  = c->delta_idx;
    r->ev_level_adj         = c->lv_adj;
    r->ae_flash_ev_cumulative = c->flash_ev_cumul;

    r->ae_wdr_ratio.sensor       = c->wdr_ratio.sensor;
    r->ae_wdr_ratio.hardware = c->wdr_ratio.hw_ratio;
    r->ae_wdr_ratio.tmp          = c->wdr_ratio.tmp;
    r->ae_wdr_ratio.last         = c->wdr_ratio.last;

    r->wdr_hi_threshold  = c->wdr_hi_th;
    r->wdr_low_threshold = c->wdr_low_th;

    r->hist_low = (uint16_t)c->hist_low;
    r->hist_mid = (uint16_t)c->hist_mid;
    r->hist_hi  = (uint16_t)c->hist_hi;

    r->backlight  = (uint8_t)c->backlight;
    r->gain_ratio = c->gain_ratio;
}

/* ------------------------------------------------------------------ */
/* Kind mapping                                                        */
/* ------------------------------------------------------------------ */

static int map_kind(fwi_ae_param_type_e t)
{
    switch (t) {
    case FWI_ISP_AE_INIT_DATA:           return AE_PARAM_INIT;
    case FWI_ISP_AE_UPDATE_AE_TABLE:    return AE_PARAM_TABLES;
    case FWI_ISP_AE_SET_EXPOSURE_INDEX:        return AE_PARAM_SET_INDEX;
    case FWI_ISP_AE_BUILD_TOUCH_WEIGHT: return AE_PARAM_TOUCH;
    default:                        return -1;
    }
}

/* ------------------------------------------------------------------ */
/* Ops vtable                                                          */
/* ------------------------------------------------------------------ */

static int32_t shim_get(void *obj, fwi_ae_param_t **param)
{
    ae_shim_entity_t *e = (ae_shim_entity_t *)obj;

    if (!e || !param)
        return -1;
    *param = &e->config;
    return 0;
}

static int32_t shim_set(void *obj, fwi_ae_param_t *param, fwi_ae_result_t *result)
{
    ae_shim_entity_t *e = (ae_shim_entity_t *)obj;
    ae_params_t cp;
    ae_param_req_t req;
    int kind;

    /* The clean set-param path is a state update with no result output; the
     * SDK result is produced by the following run(). */
    (void)result;

    if (!e || !param)
        return -1;

    kind = map_kind(param->type);
    if (kind < 0)
        return -1;

    e->config = *param;
    map_config(&cp, &e->config);
    ae_set_frame_index(e->clean, e->config.ae_frame_id);

    /*
     * The vendor reads its embedded config live, so a TABLES/INDEX/TOUCH
     * command acts on whatever the framework last wrote through the mirror.
     * Refresh the clean config first, exactly like the INIT -> TABLES
     * sequence the framework performs at setup.
     */
    if (kind != AE_PARAM_INIT) {
        memset(&req, 0, sizeof(req));
        req.kind = AE_PARAM_INIT;
        req.params = &cp;
        if (clean_ae_set_params(e->clean, &req, NULL) != 0)
            return -1;
    }

    memset(&req, 0, sizeof(req));
    req.kind = kind;
    req.params = &cp;
    req.line_index = e->config.ae_pline_index;
    if (clean_ae_set_params(e->clean, &req, NULL) != 0)
        return -1;

    sync_mirror(&e->config, e->clean);
    return 0;
}

/*
 * On-camera real-input capture: FREEISP_AE_DUMP=<path> (and FREEISP_AE_TAIL).
 *
 * Inert unless FREEISP_AE_DUMP is set.  The path is opened once, lazily, on the
 * first run(); one fixed-size record is appended per run():
 *
 *   [ae_param_t]              offset 0      sizeof(ae_param_t)             4944
 *   [struct isp_ae_stats_s]   offset 4944   sizeof(struct isp_ae_stats_s)  7172
 *
 * total 12116 bytes (64 records = 775424, the existing capture).  These are the
 * exact inputs the shim's map_config()/map_stats() consume, so the host replay
 * feeds the deployed object and the clean core identical data.  A legacy value
 * of "1" selects /tmp/freeisp_ae_dump.bin (the original capture path).
 *
 * The deployed core's metering grid also flattens past the statistics block: it
 * indexes avg[640..791] (152 u32) on top of avg[384]+hist[256], i.e. it reads
 * real framework memory that follows struct isp_ae_stats_s and is not part of
 * the statistics contract.  So the capture can additionally dump the bytes
 * immediately after the struct it was handed.  FREEISP_AE_TAIL=<nbytes> selects
 * how many (default 1024, 0 = off, capped at 65536); they go to a separate
 * "<dump>.tail" file, keeping the <dump> record layout byte-for-byte unchanged:
 *
 *   header: "AETL", u32 version=1, u32 nbytes, u32 reserved
 *   then one nbytes record per run(), appended in the same order as <dump>.
 *
 * The tail is read from (const uint8_t *)stats->ae_stats +
 * sizeof(struct isp_ae_stats_s), which is exactly the pointer map_stats()
 * dereferences, so the bytes are the real adjacent memory the deployed core
 * over-reads.  Reading past the struct is what the deployed core already does
 * (608 bytes), so the default span is safe; a null stats pointer dumps zeros.
 */
#define AE_TAIL_MAX 65536u

static FILE *g_ae_dump_file;
static FILE *g_ae_tail_file;
static unsigned g_ae_tail_n;
static int g_ae_dump_checked;

static const unsigned char AE_TAIL_MAGIC[4] = { 'A', 'E', 'T', 'L' };

static unsigned ae_tail_size(void)
{
    const char *s = getenv("FREEISP_AE_TAIL");
    char *end;
    unsigned long n;

    if (!s || s[0] == '\0')
        return 1024u;
    n = strtoul(s, &end, 0);
    if (end == s)
        return 1024u;
    if (n > AE_TAIL_MAX)
        n = AE_TAIL_MAX;
    return (unsigned)n;
}

static void ae_dump_zeros(FILE *f, size_t n)
{
    static const unsigned char z[256];

    while (n) {
        size_t k = n > sizeof(z) ? sizeof(z) : n;
        fwrite(z, 1, k, f);
        n -= k;
    }
}

static void ae_tail_open(const char *dump_path)
{
    static const char suffix[] = ".tail";
    size_t len = strlen(dump_path);
    unsigned char hdr[16];
    char *tp = (char *)malloc(len + sizeof(suffix));

    if (!tp)
        return;
    memcpy(tp, dump_path, len);
    memcpy(tp + len, suffix, sizeof(suffix));
    g_ae_tail_file = fopen(tp, "wb");
    free(tp);
    if (!g_ae_tail_file) {
        g_ae_tail_n = 0;
        return;
    }

    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, AE_TAIL_MAGIC, 4);
    hdr[4] = 1;                                   /* version, u32 LE       */
    hdr[8]  = (unsigned char)(g_ae_tail_n & 0xff);
    hdr[9]  = (unsigned char)((g_ae_tail_n >> 8) & 0xff);
    hdr[10] = (unsigned char)((g_ae_tail_n >> 16) & 0xff);
    hdr[11] = (unsigned char)((g_ae_tail_n >> 24) & 0xff);
    fwrite(hdr, 1, sizeof(hdr), g_ae_tail_file);
}

static void ae_dump_record(const ae_shim_entity_t *e, const fwi_ae_stats_desc_t *stats)
{
    if (!g_ae_dump_checked) {
        const char *path = getenv("FREEISP_AE_DUMP");

        g_ae_dump_checked = 1;
        if (path) {
            if (path[0] == '\0' || (path[0] == '1' && path[1] == '\0'))
                path = "/tmp/freeisp_ae_dump.bin";
            g_ae_dump_file = fopen(path, "wb");
            g_ae_tail_n = ae_tail_size();
            if (g_ae_dump_file && g_ae_tail_n)
                ae_tail_open(path);
        }
    }
    if (!g_ae_dump_file)
        return;

    fwrite(&e->config, sizeof(e->config), 1, g_ae_dump_file);
    if (stats && stats->ae_stats) {
        fwrite(stats->ae_stats, 1, sizeof(struct fwi_ae_stats), g_ae_dump_file);
    } else {
        ae_dump_zeros(g_ae_dump_file, sizeof(struct fwi_ae_stats));
    }
    fflush(g_ae_dump_file);

    if (g_ae_tail_file) {
        if (stats && stats->ae_stats) {
            const unsigned char *p = (const unsigned char *)stats->ae_stats +
                                     sizeof(struct fwi_ae_stats);
            fwrite(p, 1, g_ae_tail_n, g_ae_tail_file);
        } else {
            ae_dump_zeros(g_ae_tail_file, g_ae_tail_n);
        }
        fflush(g_ae_tail_file);
    }
}

static int32_t shim_run(void *obj, fwi_ae_stats_t *data, fwi_ae_result_t *result)
{
    /* The fwi vtable hands over the stats data itself; keep the descriptor
     * view (null data == "no statistics") for the code below. */
    fwi_ae_stats_desc_t sd = { .ae_stats = data };
    const fwi_ae_stats_desc_t *stats = &sd;
    ae_shim_entity_t *e = (ae_shim_entity_t *)obj;
    ae_params_t cp;
    ae_param_req_t req;
    clean_ae_stats_t cs;
    const clean_ae_stats_t *csp;
    int rc;

    if (!e || !result)
        return -1;

    ae_dump_record(e, stats);

    /*
     * The framework mutates the mirror in place between calls (frame id,
     * ae_setting, ae_sensor_info, target compensation); re-apply it so the
     * clean core always runs on live values.
     */
    map_config(&cp, &e->config);
    ae_set_frame_index(e->clean, e->config.ae_frame_id);
    memset(&req, 0, sizeof(req));
    req.kind = AE_PARAM_INIT;
    req.params = &cp;
    if (clean_ae_set_params(e->clean, &req, NULL) != 0)
        return -1;

    /*
     * A null SDK stats handle, or a null inner stats pointer, is fed to the
     * clean core as "no statistics": clean_run seeds its default result and
     * returns -1.
     */
    memset(&cs, 0, sizeof(cs));
    if (stats && stats->ae_stats) {
        map_stats(&cs, stats);
        csp = &cs;
    } else {
        csp = NULL;
    }

    /*
     * The framework's WDR configuration step (config_wdr) rewrites the shared
     * result block's blend-ratio slots between runs, and the deployed core sees
     * those writes on its next run (the exposure path arms the sensor delay
     * when last != tmp).  The clean core owns a private result block, so
     * forward the framework's values into its WDR state before running.  A
     * fully-zero block means the framework has not published a WDR ratio yet
     * (e.g. a synthetic harness that does not run config_wdr), so the clean
     * core keeps its own seed and the standalone harnesses are unaffected.
     */
    if (result->ae_wdr_ratio.sensor || result->ae_wdr_ratio.hardware ||
        result->ae_wdr_ratio.tmp || result->ae_wdr_ratio.last)
        ae_apply_wdr_feedback(e->clean,
                              (int32_t)result->ae_wdr_ratio.sensor,
                              (int32_t)result->ae_wdr_ratio.hardware,
                              (int32_t)result->ae_wdr_ratio.tmp,
                              (int32_t)result->ae_wdr_ratio.last);

    /*
     * The clean core only refreshes the fields its current branch produces
     * (gain_ratio_out, avg_lum, histogram fractions, ...); like the vendor
     * object it expects a result block that persists across frames, so the
     * entity owns it rather than a per-call stack block.
     */
    rc = clean_ae_run(e->clean, csp, &e->result);
    /*
     * With no statistics the vendor core only applies its default-result
     * initialiser when the framework result is still empty (analog gain 0);
     * otherwise it leaves the result untouched and returns -1.  Mirror that
     * instead of clobbering a populated result.
     */
    if (csp == NULL && ev_sets(&result->sensor_set)->ev_set_curr.ev_analog_gain != 0) {
        /* leave the SDK result as the framework supplied it */
    } else {
        map_result(result, &e->result,
                   (e->config.ae_setting.ae_mode == FWI_AE_WDR) || (csp == NULL));
    }
    sync_mirror(&e->config, e->clean);
    return rc;
}

/* ------------------------------------------------------------------ */
/* SDK entry points                                                    */
/* ------------------------------------------------------------------ */

static fwi_ae_core_ops_t g_ops;

void *ae_init(fwi_ae_core_ops_t **ops)
{
    ae_shim_entity_t *e;
    ae_ops_t clean_ops;

    ensure_defaults();

    e = (ae_shim_entity_t *)calloc(1, sizeof(*e));
    if (!e)
        return NULL;

    memset(&clean_ops, 0, sizeof(clean_ops));
    e->clean = clean_ae_init(&clean_ops);
    if (!e->clean) {
        free(e);
        return NULL;
    }

    g_ops.ae_set_params = shim_set;
    g_ops.ae_get_params = shim_get;
    g_ops.ae_run = shim_run;

    if (ops)
        *ops = &g_ops;

    return e;
}

void ae_exit(void *obj)
{
    ae_shim_entity_t *e = (ae_shim_entity_t *)obj;

    if (!e)
        return;
    if (e->clean)
        clean_ae_exit(e->clean);
    free(e);
}
