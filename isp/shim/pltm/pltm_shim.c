/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* pltm_shim.c - glue presenting the clean PLTM core (src/pltm/pltm_clean.c)
 * through the fwi_pltm_core_ops_t contract; no algorithm here.
 * Field mapping: docs/shim-mappings.md#pltm */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Clean entry points share the SDK names; rename them for this TU only. */
#define pltm_init              clean_pltm_init
#define pltm_exit              clean_pltm_exit
#define pltm_get_params        clean_pltm_get_params
#define pltm_set_params        clean_pltm_set_params
#define pltm_run               clean_pltm_run
#define pltm_set_default_result clean_pltm_set_default_result
#include "pltm_clean.h"
#undef pltm_init
#undef pltm_exit
#undef pltm_get_params
#undef pltm_set_params
#undef pltm_run
#undef pltm_set_default_result

/* Framework side: the generated fwi_* ABI. */
#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"

#include "pltm_shim.h"

/* Result table and stats list are 768 u16 on both sides. */
typedef char pltm_tbl_size_check[(PLTM_NTBL == 768) ? 1 : -1];
typedef char pltm_stat_size_check[
    (sizeof(((struct fwi_pltm_stats *)0)->lst) ==
     (size_t)PLTM_NSTAT * sizeof(uint16_t)) ? 1 : -1];

/* ------------------------------------------------------------------ */
/* Table provider                                                      */
/* ------------------------------------------------------------------ */

static int32_t  g_strength_bank[PLTM_STRENGTH_ROWS * PLTM_STRENGTH_COLS];
static uint8_t  g_converge_bank[PLTM_CONV_ROWS * PLTM_CONV_COLS];
static int      g_defaults_ready;

static const pltm_clean_tables_t g_default_pltm = {
    g_strength_bank, g_converge_bank, NULL   /* no presets: neutral */
};
static const freeisp_tables_t g_default_tables = { &g_default_pltm };

static freeisp_tables_t g_set_tables;
static const freeisp_tables_t *g_tables = &g_default_tables;

static void ensure_defaults(void)
{
    int r, k, d;

    if (g_defaults_ready)
        return;

    /* Placeholder: identity strength ramp (auto maps x to ~2*(x>>4)) and raw
     * |delta| steps; real tuning comes via pltm_shim_set_tables(). */
    for (r = 0; r < PLTM_STRENGTH_ROWS; r++)
        for (k = 0; k < PLTM_STRENGTH_COLS; k++)
            g_strength_bank[r * PLTM_STRENGTH_COLS + k] = k;
    for (r = 0; r < PLTM_CONV_ROWS; r++)
        for (d = 0; d < PLTM_CONV_COLS; d++)
            g_converge_bank[r * PLTM_CONV_COLS + d] = (uint8_t)d;

    g_defaults_ready = 1;
}

const freeisp_tables_t *freeisp_get_tables(void)
{
    ensure_defaults();
    return g_tables;
}

void pltm_shim_set_tables(const void *tables)
{
    if (tables) {
        g_set_tables.pltm = (const pltm_clean_tables_t *)tables;
        g_tables = &g_set_tables;
    } else {
        g_tables = &g_default_tables;
    }
}

/* ------------------------------------------------------------------ */
/* Entity                                                              */
/* ------------------------------------------------------------------ */

typedef struct pltm_shim_entity {
    fwi_pltm_param_t   config;   /* SDK mirror at offset 0; get_params returns it */
    pltm_entity_t *clean;    /* clean-room instance (embeds its own params)   */
} pltm_shim_entity_t;

/* ------------------------------------------------------------------ */
/* Parameter mapping: pltm_param_t -> pltm_clean_params_t              */
/* ------------------------------------------------------------------ */

static void map_config(pltm_clean_params_t *p, const fwi_pltm_param_t *s)
{
    pltm_clean_config_t *c = &p->config;
    pltm_clean_sensor_t *sn = &p->sensor;
    const fwi_pltm_init_cfg_t *ini = &s->pltm_init;

    c->mode = ini->pltm_cfg[FWI_ISP_PLTM_MODE];
    c->oripic_ratio_cfg = ini->pltm_cfg[FWI_ISP_PLTM_ORIGINAL_PICTURE_RATIO];
    c->order_cfg = ini->pltm_cfg[FWI_ISP_PLTM_TR_ORDER];
    c->last_order_ratio_cfg = ini->pltm_cfg[FWI_ISP_PLTM_LAST_ORDER_RATIO];
    c->clip_cfg = ini->pltm_cfg[FWI_ISP_PLTM_POW_TBL];
    c->gain_cfg = ini->pltm_cfg[FWI_ISP_PLTM_F_TBL];

    c->block_rows = ini->pltm_cfg[FWI_ISP_PLTM_BLOCK_V_COUNT];
    c->block_cols = ini->pltm_cfg[FWI_ISP_PLTM_BLOCK_H_COUNT];
    c->contrast = ini->pltm_cfg[FWI_ISP_PLTM_CONTRAST];
    c->tolerance = ini->pltm_cfg[FWI_ISP_PLTM_TOLERANCE];
    c->speed = ini->pltm_cfg[FWI_ISP_PLTM_SPEED];
    c->step = ini->pltm_cfg[FWI_ISP_PLTM_STEP];
    c->interval = ini->pltm_cfg[FWI_ISP_PLTM_INTERVAL_FRAME];

    c->enable = s->pltm_enable ? 1 : 0;
    c->frame_id = s->pltm_frame_id;

    c->auto_strength = ini->pltm_dynamic_cfg[FWI_ISP_PLTM_DYNAMIC_AUTO_STRENGTH];
    c->manual_strength = ini->pltm_dynamic_cfg[FWI_ISP_PLTM_DYNAMIC_MANUAL_STRENGTH];
    c->ae_comp = ini->pltm_dynamic_cfg[FWI_ISP_PLTM_DYNAMIC_AE_COMP];
    c->min_threshold = ini->pltm_dynamic_cfg[FWI_ISP_PLTM_DYNAMIC_MIN_THRESHOLD];

    c->bit_offset = s->wdr_bit_offset;
    c->source_curve = (const uint16_t *)s->pltm_table;

    sn->width = s->sensor_info.sensor_width;
    sn->height = s->sensor_info.sensor_height;
    sn->wdr_mode = s->sensor_info.wdr_mode;
    sn->ae_settled = s->sensor_info.is_ae_done;
    sn->backlight = s->sensor_info.backlight;
}

/* ------------------------------------------------------------------ */
/* Statistics mapping: pltm_stats_t -> pltm_clean_stats_t              */
/* ------------------------------------------------------------------ */

static void map_stats(pltm_clean_stats_t *d, const fwi_pltm_stats_desc_t *s)
{
    memset(d, 0, sizeof(*d));
    if (!s || !s->pltm_stats)
        return;
    d->measured_min = s->pltm_stats->min_after_pltm;
    memcpy(d->lst, s->pltm_stats->lst, sizeof(d->lst));
}

/* ------------------------------------------------------------------ */
/* Result translation: pltm_clean_result_t -> pltm_result_t            */
/* ------------------------------------------------------------------ */

static void map_result(fwi_pltm_result_t *d, const pltm_clean_result_t *s)
{
    int i;

    d->pltm_last_order_ratio = s->last_order_ratio;
    d->pltm_tr_order = s->order;
    d->pltm_original_picture_ratio = s->oripic_ratio;
    d->pltm_cal_en = s->cal_en;
    d->pltm_frame_smoothing_en = s->frame_smooth_en;
    d->pltm_block_height = s->block_height;
    d->pltm_block_width = s->block_width;
    d->pltm_statistic_div = s->stat_scale;
    for (i = 0; i < PLTM_NTBL; i++)
        d->pltm_tbl[i] = s->tbl[i];
    d->pltm_ae_comp = s->ae_comp;
    d->pltm_old_strength = s->old_strength;
    d->pltm_next_strength = s->next_strength;
    d->pltm_cal_strength = s->cal_strength;
    d->pltm_min_threshold = s->min_threshold;
}

/* ------------------------------------------------------------------ */
/* Ops vtable                                                          */
/* ------------------------------------------------------------------ */

static int32_t shim_get(void *obj, fwi_pltm_param_t **param)
{
    pltm_shim_entity_t *e = (pltm_shim_entity_t *)obj;

    if (!e || !param)
        return -1;
    *param = &e->config;
    return 0;
}

static int32_t shim_set(void *obj, fwi_pltm_param_t *param, fwi_pltm_result_t *result)
{
    pltm_shim_entity_t *e = (pltm_shim_entity_t *)obj;
    pltm_clean_params_t *cp = NULL;
    pltm_clean_param_req_t req;
    int clean_result = 0;

    (void)result;   /* the deployed set_params is a no-op on the result */

    if (!e || !param)
        return -1;

    e->config = *param;

    if (clean_pltm_get_params(e->clean, &cp) != 0 || !cp)
        return -1;
    map_config(cp, &e->config);

    memset(&req, 0, sizeof(req));
    req.kind = PLTM_PARAM_INIT;
    req.params = cp;
    return (clean_pltm_set_params(e->clean, &req, &clean_result) == 0) ? 0 : -1;
}

/* Input capture when FREEISP_PLTM_DUMP=<path> is set: param, stats and source
 * curve per run. Record layout: docs/shim-mappings.md#pltm */
#define PLTM_DUMP_CURVE_N 0x300

static void pltm_dump_zeros(FILE *f, size_t n)
{
    static const unsigned char z[256];

    while (n) {
        size_t k = n > sizeof(z) ? sizeof(z) : n;
        fwrite(z, 1, k, f);
        n -= k;
    }
}

static void pltm_dump_record(const pltm_shim_entity_t *e,
                             const fwi_pltm_stats_desc_t *stats)
{
    static FILE *dump;
    static int checked;
    const void *curve = e->config.pltm_table;

    if (!checked) {
        const char *path = getenv("FREEISP_PLTM_DUMP");

        checked = 1;
        if (path) {
            if (path[0] == '\0' || (path[0] == '1' && path[1] == '\0'))
                path = "/tmp/freeisp_pltm_dump.bin";
            dump = fopen(path, "wb");
        }
    }
    if (!dump)
        return;

    fwrite(&e->config, sizeof(e->config), 1, dump);
    if (stats && stats->pltm_stats)
        fwrite(stats->pltm_stats, 1, sizeof(struct fwi_pltm_stats), dump);
    else
        pltm_dump_zeros(dump, sizeof(struct fwi_pltm_stats));

    if (curve)
        fwrite(curve, 1, PLTM_DUMP_CURVE_N * sizeof(uint16_t), dump);
    else
        pltm_dump_zeros(dump, PLTM_DUMP_CURVE_N * sizeof(uint16_t));
    fflush(dump);
}

static int32_t shim_run(void *obj, fwi_pltm_stats_t *data, fwi_pltm_result_t *result)
{
    /* The fwi vtable hands over the stats data itself; keep the descriptor
     * view (null data == "no statistics") for the code below. */
    fwi_pltm_stats_desc_t sd = { .pltm_stats = data };
    const fwi_pltm_stats_desc_t *stats = &sd;
    pltm_shim_entity_t *e = (pltm_shim_entity_t *)obj;
    pltm_clean_params_t *cp = NULL;
    pltm_clean_stats_t cs;
    pltm_clean_result_t cr;

    if (!e || !result)
        return -1;

    pltm_dump_record(e, stats);

    if (!stats || !stats->pltm_stats)
        return -1;

    /* The framework mutates the mirror in place between calls; re-sync the
     * config so live edits are seen by the clean core, whose run re-reads it. */
    if (clean_pltm_get_params(e->clean, &cp) != 0 || !cp)
        return -1;
    map_config(cp, &e->config);
    map_stats(&cs, stats);

    /* The SDK carries old/next strength in the result across frames, the
     * clean core on the entity: seed it here, map_result() publishes back. */
    pltm_clean_set_strength_state(e->clean, result->pltm_old_strength,
                                  result->pltm_next_strength);

    memset(&cr, 0, sizeof(cr));
    if (clean_pltm_run(e->clean, &cs, &cr) != 0)
        return -1;

    map_result(result, &cr);
    return 0;
}

/* ------------------------------------------------------------------ */
/* SDK entry points                                                    */
/* ------------------------------------------------------------------ */

static fwi_pltm_core_ops_t g_ops;

void *pltm_init(fwi_pltm_core_ops_t **ops)
{
    pltm_shim_entity_t *e;
    pltm_ops_t clean_ops;

    ensure_defaults();

    e = (pltm_shim_entity_t *)calloc(1, sizeof(*e));
    if (!e)
        return NULL;

    memset(&clean_ops, 0, sizeof(clean_ops));
    e->clean = clean_pltm_init(&clean_ops);
    if (!e->clean) {
        free(e);
        return NULL;
    }

    g_ops.pltm_set_params = shim_set;
    g_ops.pltm_get_params = shim_get;
    g_ops.pltm_run = shim_run;

    if (ops)
        *ops = &g_ops;

    return e;
}

void pltm_exit(void *obj)
{
    pltm_shim_entity_t *e = (pltm_shim_entity_t *)obj;

    if (!e)
        return;
    if (e->clean)
        clean_pltm_exit(e->clean);
    free(e);
}
