/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* ae_shim.c - presents the clean AE core to the framework's fwi_ae_core_ops_t;
 * the SDK ae_param_t mirror sits at entity offset 0 and is re-applied each run.
 * Field mapping: docs/shim-mappings.md#ae */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Clean entry points and ae_stats_t/ae_result_t collide with the SDK names;
 * renamed for this TU only. */
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

/* Framework side: the generated fwi_* ABI. */
#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"

#include "ae_shim.h"

/* Guard the shapes the stats/table copies rely on. */
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

/* fno_ladder/fno_def and the network output biases stay NULL: they are injected
 * at runtime via ae_shim_set_tables(), never compiled in. */
static const ae_clean_tables_t g_default_ae = {
    g_log2, g_evtab, g_conv, g_touchprob, g_kernel, g_pregamma,
    g_blmask, g_wmatrix, g_wavg, g_wcenter, g_wover, g_wunder,
    g_net_in, g_net_bias, g_net_out, NULL, NULL, &g_table_default,
    g_auxprob, NULL
};
static const freeisp_tables_t g_default_tables = { &g_default_ae };

static freeisp_tables_t g_set_tables;
static const freeisp_tables_t *g_tables = &g_default_tables;

static void ensure_defaults(void)
{
    int i, r, k;

    if (g_defaults_ready)
        return;

    /* Placeholder fixtures (not tuning data) keeping every clean index in
     * range; real tables must be installed with ae_shim_set_tables(). */
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

    /* Neutral fallback of our own, used only when no camera table is found: shutter
     * 1/10000..1/50 s at 1x, then 1x..16x gain at 1/50 s, fixed f/2.0 (ae.md §15.11). */
    memset(&g_table_default, 0, sizeof(g_table_default));
    g_table_default.length = 2;
    g_table_default.ev_step = 40;
    g_table_default.shutter_shift = 0;
    g_table_default.seg[0].min_exp = 10000;
    g_table_default.seg[0].max_exp = 50;
    g_table_default.seg[0].min_gain = 256;
    g_table_default.seg[0].max_gain = 256;
    g_table_default.seg[0].min_iris = 200;
    g_table_default.seg[0].max_iris = 200;
    g_table_default.seg[1].min_exp = 50;
    g_table_default.seg[1].max_exp = 50;
    g_table_default.seg[1].min_gain = 256;
    g_table_default.seg[1].max_gain = 4096;
    g_table_default.seg[1].min_iris = 200;
    g_table_default.seg[1].max_iris = 200;

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

/* SDK mirror at offset 0 like the vendor entity; get_params() returns it. */
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
/* The vendor mutates its embedded mirror in place (scene/aperture seeds, gain
 * ranges, manual ISO); copy the clean equivalents back. See shim-mappings.md#ae */
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
/* Statistics mapping: fwi_ae_stats_t -> ae_stats_t                   */
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
    /* Vendor writes the short companion only in WDR mode or on the default
     * (no-stats) path; linear runs leave the framework's copy alone. */
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

    /* The vendor reads its config live, so re-apply INIT from the mirror before
     * a TABLES/INDEX/TOUCH command (as the framework does at setup). */
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

/* Input capture, inert unless FREEISP_AE_DUMP=<path> (FREEISP_AE_TAIL adds the
 * vendor's over-read tail). Format: docs/shim-mappings.md#ae */
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

    /* The framework mutates the mirror between calls; re-apply live values. */
    map_config(&cp, &e->config);
    ae_set_frame_index(e->clean, e->config.ae_frame_id);
    memset(&req, 0, sizeof(req));
    req.kind = AE_PARAM_INIT;
    req.params = &cp;
    if (clean_ae_set_params(e->clean, &req, NULL) != 0)
        return -1;

    /* NULL stats -> "no statistics": clean_run seeds its default result, returns -1. */
    memset(&cs, 0, sizeof(cs));
    if (stats && stats->ae_stats) {
        map_stats(&cs, stats);
        csp = &cs;
    } else {
        csp = NULL;
    }

    /* config_wdr rewrites the result's WDR ratio slots between runs; forward
     * them to the clean core's private state (all zero = not published yet). */
    if (result->ae_wdr_ratio.sensor || result->ae_wdr_ratio.hardware ||
        result->ae_wdr_ratio.tmp || result->ae_wdr_ratio.last)
        ae_apply_wdr_feedback(e->clean,
                              (int32_t)result->ae_wdr_ratio.sensor,
                              (int32_t)result->ae_wdr_ratio.hardware,
                              (int32_t)result->ae_wdr_ratio.tmp,
                              (int32_t)result->ae_wdr_ratio.last);

    /* The core refreshes only the fields its branch produces, so the result
     * block persists in the entity (as in the vendor object). */
    rc = clean_ae_run(e->clean, csp, &e->result);
    /* No stats: the vendor applies its default result only while the framework
     * result is empty (analog gain 0); otherwise it leaves it untouched. */
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
