/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* awb_shim.c - glue presenting the clean AWB core (src/awb/awb_clean.c) through
 * the fwi_awb_core_ops_t contract; no algorithm here. See DESIGN.md.
 * Field mapping: docs/shim-mappings.md#awb */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Clean names collide with the SDK ones; rename them here (and in the Makefile)
 * so the shim alone owns the bare awb_* symbols. */
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

/* Framework side: the generated fwi_* ABI. */
#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"

#include "awb_shim.h"

/* SDK stats grid (32x32) must equal AWB_NWIN, and awb_ini reference records
 * (ten s32 each) must match the clean record stride. */
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
/* No vendor table carries the no-statistics fallback gains; default unity. */
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

    /* Placeholder data, shaped so lookups stay in range and the smoother never
     * divides by zero; real tuning comes via awb_shim_set_tables(). */
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

/* Updates the built-in table only; an installed table has its own safe_gain. */
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

    /* Like the vendor, only the init-data tag rebuilds the reference
     * hierarchy; other tags are rejected. */
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

/* Input capture when FREEISP_AWB_DUMP=<path> is set: one param+stats record
 * per run. Record layout: docs/shim-mappings.md#awb */
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

    /* Seed with the caller's result: the vendor's early-outs (start-up, lock,
     * interval, manual) leave it untouched, a zeroed seed would clobber it. */
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
