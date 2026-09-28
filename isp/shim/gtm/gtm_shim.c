/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* gtm_shim.c - glue presenting the clean GTM core (src/gtm/gtm_clean.c) through
 * the fwi_gtm_core_ops_t contract; no algorithm here. See DESIGN.md.
 * Field mapping: docs/shim-mappings.md#gtm */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Clean entry points share the SDK names; rename them for this TU (clean types
 * are already gtm_clean_* prefixed). */
#define gtm_init       clean_gtm_init
#define gtm_exit       clean_gtm_exit
#define gtm_get_params clean_gtm_get_params
#define gtm_set_params clean_gtm_set_params
#define gtm_run        clean_gtm_run
#include "gtm_clean.h"
#undef gtm_init
#undef gtm_exit
#undef gtm_get_params
#undef gtm_set_params
#undef gtm_run

/* Framework side: the generated fwi_* ABI. */
#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"

#include "gtm_shim.h"

/* map_stats() byte-copies: SDK windows (16x24) must equal GTM_NWIN and the
 * histogram length GTM_NCURVE. */
typedef char gtm_win_size_check[
    (ISP_AE_ROW * ISP_AE_COL == GTM_NWIN) ? 1 : -1];
typedef char gtm_hist_size_check[
    (ISP_HIST_NUM == GTM_NCURVE) ? 1 : -1];

/* ------------------------------------------------------------------ */
/* Table provider                                                      */
/* ------------------------------------------------------------------ */

static int16_t  g_guide_linear[GTM_NCURVE];
static int16_t  g_guide_low[GTM_NCURVE];
static int16_t  g_guide_high[GTM_NCURVE];
static int16_t  g_pregamma[GTM_PRE_ROWS * GTM_PRE_COLS];
static int32_t  g_eq_kernel[GTM_EQ_ROWS * GTM_EQ_TAPS];
static uint8_t  g_converge[GTM_CONV_ROWS * GTM_CONV_COLS];
static int      g_defaults_ready;

static const gtm_clean_tables_t g_default_gtm = {
    g_guide_linear, g_guide_low, g_guide_high,
    g_pregamma, g_eq_kernel, g_converge
};
static const freeisp_tables_t g_default_tables = { &g_default_gtm };

static freeisp_tables_t g_set_tables;
static const freeisp_tables_t *g_tables = &g_default_tables;

static void ensure_defaults(void)
{
    int r, c;

    if (g_defaults_ready)
        return;

    /* Placeholder data, shaped so every clean builder stays in range; real
     * tuning comes via gtm_shim_set_tables(). */
    for (r = 0; r < GTM_NCURVE; r++) {
        g_guide_linear[r] = (int16_t)(r * 16);
        g_guide_low[r] = (int16_t)(r * 24);
        g_guide_high[r] = (int16_t)(r * 8);
    }
    for (r = 0; r < GTM_PRE_ROWS; r++)
        for (c = 0; c < GTM_PRE_COLS; c++)
            g_pregamma[r * GTM_PRE_COLS + c] = (int16_t)c;
    for (r = 0; r < GTM_EQ_ROWS; r++)
        for (c = 0; c < GTM_EQ_TAPS; c++)
            g_eq_kernel[r * GTM_EQ_TAPS + c] = 33;
    for (r = 0; r < GTM_CONV_ROWS; r++)
        for (c = 0; c < GTM_CONV_COLS; c++)
            g_converge[r * GTM_CONV_COLS + c] =
                (uint8_t)((r == 26) ? c : 0);

    g_defaults_ready = 1;
}

const freeisp_tables_t *freeisp_get_tables(void)
{
    ensure_defaults();
    return g_tables;
}

void gtm_shim_set_tables(const void *tables)
{
    if (tables) {
        g_set_tables.gtm = (const gtm_clean_tables_t *)tables;
        g_tables = &g_set_tables;
    } else {
        g_tables = &g_default_tables;
    }
}

/* ------------------------------------------------------------------ */
/* Entity                                                              */
/* ------------------------------------------------------------------ */

typedef struct gtm_shim_entity {
    fwi_gtm_param_t   config;   /* SDK mirror at offset 0; get_params returns it */
    gtm_entity_t *clean;    /* clean-room instance                           */
} gtm_shim_entity_t;

/* ------------------------------------------------------------------ */
/* Parameter mapping: gtm_param_t -> gtm_clean_params_t                */
/* ------------------------------------------------------------------ */

/* gtm_cfg[] indices follow the deployed object's behaviour (note the swapped
 * alpha/slope pair). Field mapping: docs/shim-mappings.md#gtm */
static void map_tuning_scalars(gtm_clean_params_t *p, const fwi_gtm_param_t *s)
{
    p->mode = (int)s->gtm_init.gtm_type;
    p->gamma_mode = (int)s->gtm_init.gamma_type;
    p->frame_index = s->gtm_frame_id;
    p->enable = (s->gtm_enable != 0) ? 1 : 0;

    p->range_max = s->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_GAIN];
    p->eq_gain = s->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_EQ_RATIO];
    p->cover_bias = s->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_EQ_SMOOTH];
    p->black_level = s->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_BLACK];
    p->white_level = s->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_WHITE];
    p->white_slope = s->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_BLACK_ALPHA];
    p->black_slope = s->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_WHITE_ALPHA];
    p->pre_gamma_offset = s->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_GAMMA_IND];

    p->hist_pixel_count = s->gtm_init.hist_pixel_count;
    p->dark_floor = s->gtm_init.dark_minval;
    p->bright_floor = s->gtm_init.bright_minval;

    p->brightness = s->brightness;
    p->contrast = s->contrast;
    p->bit_offset = s->gtm_bit_offset;
    p->wdr_en = s->wdr_en ? 1 : 0;
}

static void map_tuning_tables(gtm_clean_params_t *p, const fwi_gtm_param_t *s)
{
    int r, c;

    for (r = 0; r < GTM_NPEAK; r++)
        for (c = 0; c < GTM_NPEAK; c++)
            p->peak_map[r][c] = (int)s->gtm_init.plum_var[r][c];

    if (s->gamma_tbl)
        memcpy(p->gamma_lut, s->gamma_tbl, sizeof(p->gamma_lut));
    else
        memset(p->gamma_lut, 0, sizeof(p->gamma_lut));
}

/* One-shot mapping used by set_params: seeds the persistent curve state. */
static void map_tuning(gtm_clean_params_t *p, const fwi_gtm_param_t *s)
{
    memset(p, 0, sizeof(*p));
    map_tuning_scalars(p, s);
    map_tuning_tables(p, s);
    if (s->drc_table)
        memcpy(p->curve, s->drc_table, sizeof(p->curve));
    if (s->drc_table_last)
        memcpy(p->curve_prev, s->drc_table_last, sizeof(p->curve_prev));
}

/* Live refresh used by run: reads the mirror again, leaves curve state alone. */
static void apply_tuning_live(gtm_clean_params_t *p, const fwi_gtm_param_t *s)
{
    map_tuning_scalars(p, s);
    map_tuning_tables(p, s);
}

/* ------------------------------------------------------------------ */
/* Statistics mapping: gtm_stats_t -> gtm_clean_stats_t                */
/* ------------------------------------------------------------------ */

static void map_stats(gtm_clean_stats_t *c, const fwi_gtm_stats_desc_t *s)
{
    memset(c, 0, sizeof(*c));
    if (!s || !s->gtm_stats)
        return;
    memcpy(c->hist_raw, s->gtm_stats->hist, sizeof(c->hist_raw));
    memcpy(c->win_avg, s->gtm_stats->average, sizeof(c->win_avg));
}

/* ------------------------------------------------------------------ */
/* Result translation: gtm_clean_result_t -> gtm_result_t              */
/* ------------------------------------------------------------------ */

static void map_result_to_sdk(fwi_gtm_result_t *r, const gtm_clean_result_t *cr)
{
    r->hist_max_val = cr->peak_level;
    r->average_luminance = cr->avg_lum;
    r->average_var = cr->avg_var;
    r->hist_div = cr->div_index;
    r->hratio_last = cr->ratio_hold;
    r->hdr_req = cr->hdr_flag;
}

/* Keep the SDK drc table pointers in sync with the clean curve buffers. */
static void sync_curve_out(gtm_shim_entity_t *e, const gtm_clean_params_t *live)
{
    if (e->config.drc_table && e->config.drc_table != live->curve)
        memmove(e->config.drc_table, live->curve, sizeof(live->curve));
    if (e->config.drc_table_last && e->config.drc_table_last != live->curve_prev)
        memmove(e->config.drc_table_last, live->curve_prev,
                sizeof(live->curve_prev));
}

/* Point any NULL SDK buffer at the clean entity's own storage. */
static void wire_config_buffers(gtm_shim_entity_t *e)
{
    gtm_clean_params_t *live = NULL;

    if (clean_gtm_get_params(e->clean, &live) != 0 || !live)
        return;
    if (!e->config.gamma_tbl)
        e->config.gamma_tbl = live->gamma_lut;
    if (!e->config.drc_table)
        e->config.drc_table = live->curve;
    if (!e->config.drc_table_last)
        e->config.drc_table_last = live->curve_prev;
}

/* ------------------------------------------------------------------ */
/* Ops vtable                                                          */
/* ------------------------------------------------------------------ */

static int32_t shim_get(void *obj, fwi_gtm_param_t **param)
{
    gtm_shim_entity_t *e = (gtm_shim_entity_t *)obj;

    if (!e || !param)
        return -1;
    *param = &e->config;
    return 0;
}

static int32_t shim_set(void *obj, fwi_gtm_param_t *param, fwi_gtm_result_t *result)
{
    gtm_shim_entity_t *e = (gtm_shim_entity_t *)obj;
    gtm_clean_param_req_t req;
    gtm_clean_params_t cp;

    (void)result;

    if (!e || !param)
        return -1;

    e->config = *param;

    map_tuning(&cp, &e->config);
    req.kind = GTM_PARAM_INIT;
    req.params = &cp;
    if (clean_gtm_set_params(e->clean, &req, NULL) != 0)
        return -1;

    wire_config_buffers(e);
    {
        gtm_clean_params_t *live = NULL;
        if (clean_gtm_get_params(e->clean, &live) == 0 && live)
            sync_curve_out(e, live);
    }
    return 0;
}

/* Input capture when FREEISP_GTM_DUMP=<path> is set: param, stats and the three
 * pointed-to tables per run. Record layout: docs/shim-mappings.md#gtm */
static void gtm_dump_zeros(FILE *f, size_t n)
{
    static const unsigned char z[256];

    while (n) {
        size_t k = n > sizeof(z) ? sizeof(z) : n;
        fwrite(z, 1, k, f);
        n -= k;
    }
}

static void gtm_dump_record(const gtm_shim_entity_t *e, const fwi_gtm_stats_desc_t *stats)
{
    static FILE *dump;
    static int checked;
    const void *gamma = e->config.gamma_tbl;
    const void *drc = e->config.drc_table;
    const void *drc_last = e->config.drc_table_last;

    if (!checked) {
        const char *path = getenv("FREEISP_GTM_DUMP");

        checked = 1;
        if (path) {
            if (path[0] == '\0' || (path[0] == '1' && path[1] == '\0'))
                path = "/tmp/freeisp_gtm_dump.bin";
            dump = fopen(path, "wb");
        }
    }
    if (!dump)
        return;

    fwrite(&e->config, sizeof(e->config), 1, dump);
    if (stats && stats->gtm_stats)
        fwrite(stats->gtm_stats, 1, sizeof(struct fwi_gtm_stats), dump);
    else
        gtm_dump_zeros(dump, sizeof(struct fwi_gtm_stats));

    if (gamma)
        fwrite(gamma, 1, ISP_GAMMA_TBL_LENGTH * sizeof(uint16_t), dump);
    else
        gtm_dump_zeros(dump, ISP_GAMMA_TBL_LENGTH * sizeof(uint16_t));
    if (drc)
        fwrite(drc, 1, ISP_DRC_TBL_SIZE * sizeof(uint16_t), dump);
    else
        gtm_dump_zeros(dump, ISP_DRC_TBL_SIZE * sizeof(uint16_t));
    if (drc_last)
        fwrite(drc_last, 1, ISP_DRC_TBL_SIZE * sizeof(uint16_t), dump);
    else
        gtm_dump_zeros(dump, ISP_DRC_TBL_SIZE * sizeof(uint16_t));
    fflush(dump);
}

static int32_t shim_run(void *obj, fwi_gtm_stats_t *data, fwi_gtm_result_t *result)
{
    /* GTM rejects null stats (-1), so null data maps to a null descriptor,
     * not an empty one. */
    fwi_gtm_stats_desc_t sd = { .gtm_stats = data };
    const fwi_gtm_stats_desc_t *stats = data ? &sd : NULL;
    gtm_shim_entity_t *e = (gtm_shim_entity_t *)obj;
    gtm_clean_params_t *live = NULL;
    gtm_clean_stats_t cs;
    gtm_clean_result_t cr;

    if (!e || !stats || !result)
        return -1;

    gtm_dump_record(e, stats);

    if (clean_gtm_get_params(e->clean, &live) != 0 || !live)
        return -1;

    apply_tuning_live(live, &e->config);
    map_stats(&cs, stats);

    memset(&cr, 0, sizeof(cr));
    if (clean_gtm_run(e->clean, &cs, &cr) != 0)
        return -1;

    /* Gated frames (frame_index <= 2 / disabled) leave the result and the
     * curve untouched, exactly like the framework object. */
    if (cr.curve) {
        sync_curve_out(e, live);
        map_result_to_sdk(result, &cr);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* SDK entry points                                                    */
/* ------------------------------------------------------------------ */

static fwi_gtm_core_ops_t g_ops;

void *gtm_init(fwi_gtm_core_ops_t **gtm_core_ops)
{
    gtm_shim_entity_t *e;
    gtm_ops_t clean_ops;

    ensure_defaults();

    e = (gtm_shim_entity_t *)calloc(1, sizeof(*e));
    if (!e)
        return NULL;

    memset(&clean_ops, 0, sizeof(clean_ops));
    e->clean = clean_gtm_init(&clean_ops);
    if (!e->clean) {
        free(e);
        return NULL;
    }

    wire_config_buffers(e);

    g_ops.gtm_set_params = shim_set;
    g_ops.gtm_get_params = shim_get;
    g_ops.gtm_run = shim_run;

    if (gtm_core_ops)
        *gtm_core_ops = &g_ops;

    return e;
}

void gtm_exit(void *obj)
{
    gtm_shim_entity_t *e = (gtm_shim_entity_t *)obj;

    if (!e)
        return;
    if (e->clean)
        clean_gtm_exit(e->clean);
    free(e);
}
