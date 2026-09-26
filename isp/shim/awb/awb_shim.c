/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * awb_shim.c - AWB (auto white balance) integration shim.
 *
 * Presents the clean-room AWB core (src/awb/awb_clean.c) to the
 * Yi/mediad ISP framework through the SDK isp_awb_core_ops_t contract.
 * Integration glue only: no algorithm is implemented here.  See DESIGN.md.
 *
 * Entity layout (the vendor entity also embeds its config block at offset 0):
 *
 *   typedef struct awb_shim_entity {
 *       awb_param_t    config;   // SDK mirror at offset 0; get_params -> &config
 *       awb_entity_t  *clean;    // clean-room instance
 *   } awb_shim_entity_t;
 *
 * The clean entity embeds its own awb_params_t, so the shim reaches it through
 * clean_awb_get_params() and rewrites the fields it maps.  The SDK mirror is
 * re-read into the clean config on every set and every run, because the
 * framework writes the mirror in place between calls.
 *
 * Field mapping (SDK awb_param_t -> clean awb_params_t):
 *
 *   clean field            SDK source
 *   ------------------------------------------------------------------
 *   mode                   awb_ctrl.wb_mode
 *   manual_gain[4]         awb_ctrl.wb_gain_manual.{r,gr,gb,b}_gain
 *   lock                   awb_ctrl.white_balance_lock
 *   win_region[4]          awb_ctrl.awb_coor.{x1,y1,x2,y2}
 *   interval               awb_ini.awb_interval
 *   speed                  awb_ini.awb_speed
 *   temp_low               awb_ini.awb_color_temper_low    (unused clean)
 *   temp_high              awb_ini.awb_color_temper_high   (unused clean)
 *   base_temp              awb_ini.awb_base_temper
 *   green_dist             awb_ini.awb_green_zone_dist
 *   blue_dist              awb_ini.awb_blue_sky_dist
 *   light_num              awb_ini.awb_light_num
 *   ext_light_num          awb_ini.awb_ext_light_num
 *   skin_num               awb_ini.awb_skin_color_num
 *   special_num            awb_ini.awb_special_color_num
 *   light_info[320]        awb_ini.awb_light_info
 *   ext_light_info[320]    awb_ini.awb_ext_light_info
 *   skin_info[160]         awb_ini.awb_skin_color_info
 *   special_info[320]      awb_ini.awb_special_color_info
 *   preset_gain[22]        awb_ini.awb_preset_gain
 *   r_favor / b_favor      awb_ini.awb_rgain_favor / awb_ini.awb_bgain_favor
 *   ae_lv                  awb_sensor_info.ae_lv
 *   ae_index / ae_index_max awb_sensor_info.ae_tbl_idx / .ae_tbl_idx_max
 *   ae_done                awb_sensor_info.is_ae_done
 *   test_mode              test_cfg.isp_test_mode         (unused clean)
 *   platform_id            isp_platform_id                (unused clean)
 *   fixed_temp             test_cfg.isp_color_temp
 *   control_enable         test_cfg.awb_en
 *   frame_id               awb_frame_id
 *
 * Statistics mapping: clean win[i].avg[0..2] <- isp_awb_stats_s.awb_avg_r/g/b
 * (row-major 32x32), clean win[i].npix <- isp_awb_stats_s.awb_sum_cnt.
 *
 * Result translation: clean gain_out.{r,gr,gb,b} -> wb_gain_output, and
 * color_temp_out -> color_temp_output.  A null SDK stats handle is passed to
 * the clean core as "no statistics"; the clean run then returns -1 and seeds
 * the unity / 6500 K safe default, which is copied out unchanged.
 *
 * Result write-back is gated exactly like the vendor object's.  The clean
 * result is first seeded with the framework's current result so the clean
 * core's early-outs (start-up frames, lock, interval gate, manual mode,
 * outlier abort) leave the caller's gains and colour temperature untouched;
 * only when the clean core writes them are they copied back.  set-params only
 * rebuilds the corpus->hierarchy mapping for the ISP_AWB_INI_DATA tag, as the
 * vendor does.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Clean-room side.  The clean entry points share names with the SDK ones and
 * the clean awb_stats_t/awb_result_t collide with the SDK typedefs, so rename
 * the clean spellings for this translation unit only.  awb_clean.c is compiled
 * with the same entry-point defines (see Makefile), so the shim owns the bare
 * awb_* SDK symbols and the clean object exports only clean_awb_*.
 */
#define awb_stats_t    clean_awb_stats_t
#define awb_result_t   clean_awb_result_t
#define awb_init       clean_awb_init
#define awb_exit       clean_awb_exit
#define awb_get_params clean_awb_get_params
#define awb_set_params clean_awb_set_params
#define awb_run        clean_awb_run
#define awb_isr        clean_awb_isr
#include "awb_clean.h"
#undef awb_stats_t
#undef awb_result_t
#undef awb_init
#undef awb_exit
#undef awb_get_params
#undef awb_set_params
#undef awb_run
#undef awb_isr

/*
 * Framework side: the generated fwi_* ABI.  The shim exports awb_init/awb_exit
 * and a fwi_awb_core_ops_t vtable; the framework calls it directly.
 */
#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"

#include "awb_shim.h"

/*
 * The SDK statistics grid is ISP_AWB_ROW * ISP_AWB_COL (= 32*32 at ISP_VERSION
 * 521) and the clean win array is AWB_NWIN = 1024; the reference records in
 * awb_ini are ten HW_S32 each, matching the clean record stride.  Guard both at
 * compile time.
 */
typedef char awb_win_size_check[
    (ISP_AWB_ROW * ISP_AWB_COL == AWB_NWIN) ? 1 : -1];
typedef char awb_light_info_size_check[
    (sizeof(((fwi_awb_param_t *)0)->awb_init.awb_illum) ==
     (size_t)(AWB_MAX_LIGHTS * AWB_REF_INTS) * sizeof(int32_t)) ? 1 : -1];
typedef char awb_special_info_size_check[
    (sizeof(((fwi_awb_param_t *)0)->awb_init.awb_special) ==
     (size_t)(AWB_MAX_SPECIAL * AWB_REF_INTS) * sizeof(int32_t)) ? 1 : -1];

/* ------------------------------------------------------------------ */
/* Table provider                                                      */
/* ------------------------------------------------------------------ */

static uint8_t  g_trust[AWB_NTRUST * 256];
static uint16_t g_speed_w[AWB_NSPEED * AWB_NSSC];
static char     g_label[AWB_NREF][32];
static int32_t  g_std_trust[AWB_NREF];
static int32_t  g_temp_bright[AWB_NREF * AWB_NREF];
/*
 * No vendor table carries the no-statistics fallback gains, so the built-in
 * pilot default is unity.  Supply the sensor quadruple with
 * awb_shim_set_safe_gain(), or point the installed table's safe_gain at the
 * real tuning.
 */
static uint16_t g_safe_gain[4] = { 256, 256, 256, 256 };
static int      g_defaults_ready;

static const awb_clean_tables_t g_default_awb = {
    g_trust, g_speed_w, &g_label[0][0], g_std_trust, g_temp_bright,
    g_safe_gain
};
static const freeisp_tables_t g_default_tables = { &g_default_awb };

static freeisp_tables_t g_set_tables;
static const freeisp_tables_t *g_tables = &g_default_tables;

static void ensure_defaults(void)
{
    int r, k, i;

    if (g_defaults_ready)
        return;

    /*
     * Pilot placeholder data only.  Shaped so the clean trust/weight lookups
     * stay in range and the smoother never divides by zero; real tuning must
     * be installed with awb_shim_set_tables() before awb_init().
     */
    for (r = 0; r < AWB_NTRUST; r++)
        for (k = 0; k < 256; k++)
            g_trust[r * 256 + k] = (k < 160) ? (uint8_t)(32 - k / 8) : 0;

    memset(g_speed_w, 0, sizeof(g_speed_w));
    for (k = 0; k < AWB_NGHIST; k++)
        g_speed_w[0 * AWB_NSSC + k] = (k == 0) ? 64 : 0;
    for (r = 1; r < AWB_NSPEED; r++)
        for (k = 0; k < AWB_NGHIST; k++)
            g_speed_w[r * AWB_NSSC + k] = 1;

    for (i = 0; i < AWB_NREF; i++) {
        int j;
        for (j = 0; j < 31; j++)
            g_label[i][j] = (j == 0) ? (char)('0' + i) : 0;
        g_label[i][31] = 0;
        g_std_trust[i] = 64;
        for (j = 0; j < AWB_NREF; j++)
            g_temp_bright[i * AWB_NREF + j] = 20;
    }

    g_defaults_ready = 1;
}

const freeisp_tables_t *freeisp_get_tables(void)
{
    ensure_defaults();
    return g_tables;
}

void awb_shim_set_tables(const void *tables)
{
    if (tables) {
        g_set_tables.awb = (const awb_clean_tables_t *)tables;
        g_tables = &g_set_tables;
    } else {
        g_tables = &g_default_tables;
    }
}

/*
 * Install the no-statistics fallback gain quadruple (r, gr, gb, b).  The
 * clean core reads it from the table each time the null-statistics path is
 * taken, so this only updates the built-in pilot default; a table installed
 * with awb_shim_set_tables() carries its own safe_gain pointer.
 */
void awb_shim_set_safe_gain(const unsigned short gain[4])
{
    int i;

    if (gain == NULL)
        return;
    for (i = 0; i < 4; i++)
        g_safe_gain[i] = (uint16_t)gain[i];
}

/* ------------------------------------------------------------------ */
/* Entity                                                              */
/* ------------------------------------------------------------------ */

typedef struct awb_shim_entity {
    fwi_awb_param_t    config;   /* SDK mirror at offset 0; get_params returns it */
    awb_entity_t  *clean;    /* clean-room instance (embeds its own params)   */
} awb_shim_entity_t;

/* ------------------------------------------------------------------ */
/* Parameter mapping: awb_param_t -> clean awb_params_t                */
/* ------------------------------------------------------------------ */

static void map_params(awb_params_t *c, const fwi_awb_param_t *s)
{
    memset(c, 0, sizeof(*c));

    c->mode = (int)s->awb_ctrl.wb_mode;
    c->manual_gain[0] = (int)s->awb_ctrl.wb_gain_manual.r_gain;
    c->manual_gain[1] = (int)s->awb_ctrl.wb_gain_manual.gr_gain;
    c->manual_gain[2] = (int)s->awb_ctrl.wb_gain_manual.gb_gain;
    c->manual_gain[3] = (int)s->awb_ctrl.wb_gain_manual.b_gain;
    c->lock = s->awb_ctrl.white_balance_lock ? 1 : 0;
    c->win_region[0] = (int)s->awb_ctrl.awb_coord.x1;
    c->win_region[1] = (int)s->awb_ctrl.awb_coord.y1;
    c->win_region[2] = (int)s->awb_ctrl.awb_coord.x2;
    c->win_region[3] = (int)s->awb_ctrl.awb_coord.y2;

    c->interval = (int)s->awb_init.awb_period_frames;
    c->speed = (int)s->awb_init.awb_step_speed;
    c->temp_low = (int)s->awb_init.awb_colour_temper_low;
    c->temp_high = (int)s->awb_init.awb_colour_temper_high;
    c->base_temp = (int)s->awb_init.awb_base_temper;
    c->green_dist = (int)s->awb_init.awb_green_zone_distance;
    c->blue_dist = (int)s->awb_init.awb_blue_sky_distance;
    c->light_num = (int)s->awb_init.awb_illum_count;
    c->ext_light_num = (int)s->awb_init.awb_extra_illum_count;
    c->skin_num = (int)s->awb_init.awb_skin_count;
    c->special_num = (int)s->awb_init.awb_special_count;

    memcpy(c->light_info, s->awb_init.awb_illum, sizeof(c->light_info));
    memcpy(c->ext_light_info, s->awb_init.awb_extra_illum,
           sizeof(c->ext_light_info));
    memcpy(c->skin_info, s->awb_init.awb_skin,
           sizeof(c->skin_info));
    memcpy(c->special_info, s->awb_init.awb_special,
           sizeof(c->special_info));
    memcpy(c->preset_gain, s->awb_init.awb_preference_gain,
           sizeof(c->preset_gain));

    c->r_favor = (int)s->awb_init.awb_r_bias;
    c->b_favor = (int)s->awb_init.awb_b_bias;

    c->ae_lv = (int)s->awb_sensor_info.ae_level;
    c->ae_index = s->awb_sensor_info.ae_tbl_index;
    c->ae_index_max = s->awb_sensor_info.ae_tbl_index_max;
    c->ae_done = s->awb_sensor_info.is_ae_done;

    c->test_mode = (int)s->test_cfg.test_mode;
    c->platform_id = (int)s->platform_id;
    c->fixed_temp = (int)s->test_cfg.colour_temp;
    c->control_enable = (int)s->test_cfg.awb_en;
    c->frame_id = (int)s->awb_frame_id;
}

/* ------------------------------------------------------------------ */
/* Statistics mapping: awb_stats_t -> clean awb_stats_t                */
/* ------------------------------------------------------------------ */

static void map_stats(clean_awb_stats_t *d, const fwi_awb_stats_desc_t *s)
{
    int r, c;

    memset(d, 0, sizeof(*d));
    if (!s || !s->awb_stats)
        return;

    for (r = 0; r < ISP_AWB_ROW; r++) {
        for (c = 0; c < ISP_AWB_COL; c++) {
            int i = r * ISP_AWB_COL + c;
            d->win[i].avg[0] = s->awb_stats->awb_average_r[r][c];
            d->win[i].avg[1] = s->awb_stats->awb_average_g[r][c];
            d->win[i].avg[2] = s->awb_stats->awb_average_b[r][c];
            d->win[i].npix = s->awb_stats->awb_sum_count[r][c];
        }
    }
}

/* ------------------------------------------------------------------ */
/* Result translation: clean awb_result_t -> SDK awb_result_t          */
/* ------------------------------------------------------------------ */

static void map_result_to_sdk(fwi_awb_result_t *d, const clean_awb_result_t *s)
{
    d->wb_gain_output.r_gain = s->gain_out.r;
    d->wb_gain_output.gr_gain = s->gain_out.gr;
    d->wb_gain_output.gb_gain = s->gain_out.gb;
    d->wb_gain_output.b_gain = s->gain_out.b;
    d->colour_temp_output = s->color_temp_out;
}

/* ------------------------------------------------------------------ */
/* Ops vtable                                                          */
/* ------------------------------------------------------------------ */

static int32_t shim_get(void *obj, fwi_awb_param_t **param)
{
    awb_shim_entity_t *e = (awb_shim_entity_t *)obj;

    if (!e || !param)
        return -1;
    *param = &e->config;
    return 0;
}

static int32_t shim_set(void *obj, fwi_awb_param_t *param, fwi_awb_result_t *result)
{
    awb_shim_entity_t *e = (awb_shim_entity_t *)obj;
    awb_params_t cp;
    awb_param_req_t req;

    (void)result;                     /* the vendor set-params is a no-op */

    if (!e || !param)
        return -1;

    e->config = *param;

    /*
     * The vendor set-params only rebuilds the reference hierarchy for the
     * init-data tag; any other tag is rejected without touching the entity.
     * Mirror that gate so the corpus mapping runs exactly when the framework
     * expects the rebuild.
     */
    if (param->type != FWI_ISP_AWB_INIT_DATA)
        return -1;

    map_params(&cp, &e->config);
    memset(&req, 0, sizeof(req));
    req.kind = AWB_PARAM_INIT;
    req.params = &cp;
    if (clean_awb_set_params(e->clean, &req, NULL) != 0)
        return -1;
    return 0;
}

/*
 * On-camera real-input capture: FREEISP_AWB_DUMP=<path>.
 *
 * Inert unless the env var is set.  The path is opened once, lazily, on the
 * first run(); one fixed-size record is appended per run():
 *
 *   [awb_param_t]              offset 0      sizeof(awb_param_t)             4824
 *   [struct isp_awb_stats_s]   offset 4824   sizeof(struct isp_awb_stats_s)  32768
 *
 * total 37592 bytes.  awb_param_t is self-contained (no pointers), so this is
 * the exact input pair the shim's map_params()/map_stats() consume.  A value of
 * "1" selects /tmp/freeisp_awb_dump.bin.
 */
static void awb_dump_zeros(FILE *f, size_t n)
{
    static const unsigned char z[256];

    while (n) {
        size_t k = n > sizeof(z) ? sizeof(z) : n;
        fwrite(z, 1, k, f);
        n -= k;
    }
}

static void awb_dump_record(const awb_shim_entity_t *e, const fwi_awb_stats_desc_t *stats)
{
    static FILE *dump;
    static int checked;

    if (!checked) {
        const char *path = getenv("FREEISP_AWB_DUMP");

        checked = 1;
        if (path) {
            if (path[0] == '\0' || (path[0] == '1' && path[1] == '\0'))
                path = "/tmp/freeisp_awb_dump.bin";
            dump = fopen(path, "wb");
        }
    }
    if (!dump)
        return;

    fwrite(&e->config, sizeof(e->config), 1, dump);
    if (stats && stats->awb_stats) {
        fwrite(stats->awb_stats, 1, sizeof(struct fwi_awb_stats), dump);
    } else {
        awb_dump_zeros(dump, sizeof(struct fwi_awb_stats));
    }
    fflush(dump);
}

static int32_t shim_run(void *obj, fwi_awb_stats_t *data, fwi_awb_result_t *result)
{
    /* The fwi vtable hands over the stats data itself; keep the descriptor
     * view (null data == "no statistics") for the code below. */
    fwi_awb_stats_desc_t sd = { .awb_stats = data };
    const fwi_awb_stats_desc_t *stats = &sd;
    awb_shim_entity_t *e = (awb_shim_entity_t *)obj;
    awb_params_t *live = NULL;
    clean_awb_stats_t cs;
    clean_awb_result_t cr;
    const clean_awb_stats_t *csp;
    int rc;

    if (!e || !result)
        return -1;

    awb_dump_record(e, stats);

    /* The framework mutates the mirror in place between calls; re-sync the
     * config so live edits are seen by the clean core. */
    if (clean_awb_get_params(e->clean, &live) == 0 && live)
        map_params(live, &e->config);

    if (stats && stats->awb_stats) {
        map_stats(&cs, stats);
        csp = &cs;
    } else {
        csp = NULL;   /* fed to the clean core as "no statistics" */
    }

    /*
     * Seed the clean result with the framework's current white-balance result
     * before running.  The vendor object mutates the result in place and has
     * gated early-outs (start-up frames, lock, interval gate, manual mode)
     * that leave the caller's values untouched; handing the clean core a
     * zero-initialised result would clobber them on those paths.  The clean
     * run then either rewrites the fields or leaves the seed in place.
     */
    memset(&cr, 0, sizeof(cr));
    cr.gain_out.r  = result->wb_gain_output.r_gain;
    cr.gain_out.gr = result->wb_gain_output.gr_gain;
    cr.gain_out.gb = result->wb_gain_output.gb_gain;
    cr.gain_out.b  = result->wb_gain_output.b_gain;
    cr.color_temp_out = result->colour_temp_output;

    rc = clean_awb_run(e->clean, csp, &cr);

    /* A null stats handle leaves the clean result at its safe default (unity
     * gains, 6500 K) and returns -1; copy it out unchanged. */
    map_result_to_sdk(result, &cr);
    return rc;
}

/* ------------------------------------------------------------------ */
/* SDK entry points                                                    */
/* ------------------------------------------------------------------ */

static fwi_awb_core_ops_t g_ops;

void *awb_init(fwi_awb_core_ops_t **awb_core_ops)
{
    awb_shim_entity_t *e;

    ensure_defaults();

    e = (awb_shim_entity_t *)calloc(1, sizeof(*e));
    if (!e)
        return NULL;

    e->clean = clean_awb_init(NULL);
    if (!e->clean) {
        free(e);
        return NULL;
    }

    g_ops.awb_set_params = shim_set;
    g_ops.awb_get_params = shim_get;
    g_ops.awb_run = shim_run;

    if (awb_core_ops)
        *awb_core_ops = &g_ops;

    return e;
}

void awb_exit(void *obj)
{
    awb_shim_entity_t *e = (awb_shim_entity_t *)obj;

    if (!e)
        return;
    if (e->clean)
        clean_awb_exit(e->clean);
    free(e);
}
