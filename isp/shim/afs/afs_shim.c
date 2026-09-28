/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* afs_shim.c - glue presenting the clean AFS core (src/afs/afs_clean.c) through
 * the fwi_afs_core_ops_t contract; no algorithm here.
 * Field mapping: docs/shim-mappings.md#afs */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Clean entry points are renamed to clean_afs_* here and in the Makefile, so
 * the shim alone owns the bare afs_* SDK symbols. */
#define afs_clean_init       clean_afs_init
#define afs_clean_exit       clean_afs_exit
#define afs_clean_get_params clean_afs_get_params
#define afs_clean_set_params clean_afs_set_params
#define afs_clean_run        clean_afs_run
#define afs_clean_isr        clean_afs_isr
#include "afs_clean.h"
#undef afs_clean_init
#undef afs_clean_exit
#undef afs_clean_get_params
#undef afs_clean_set_params
#undef afs_clean_run
#undef afs_clean_isr

/* Framework side: the generated fwi_* ABI. */
#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"

#include "afs_shim.h"

/* ------------------------------------------------------------------ */
/* Table provider (rotation kernel)                                    */
/* ------------------------------------------------------------------ */

static int g_sin[AFS_CLEAN_NTRIG];
static int g_cos[AFS_CLEAN_NTRIG];
static int g_trig_ready;

static const afs_clean_trig_t g_default_trig = { g_sin, g_cos };
static afs_clean_trig_t       g_set_trig;
static const afs_clean_trig_t *g_trig = &g_default_trig;

static void ensure_default_trig(void)
{
    int m;

    if (g_trig_ready)
        return;

    /* Placeholder analytic sin/cos, not tuning data; real tables come via
     * afs_shim_set_trig_tables() before afs_init(). */
    for (m = 0; m < AFS_CLEAN_NTRIG; m++) {
        double a = 2.0 * 3.14159265358979323846 * (double)m /
                   (double)AFS_CLEAN_NTRIG;

        g_sin[m] = (int)lround(100.0 * sin(a));
        g_cos[m] = (int)lround(100.0 * cos(a));
    }

    g_trig_ready = 1;
}

static const afs_clean_trig_t *shim_trig_provider(void)
{
    ensure_default_trig();
    return g_trig;
}

void afs_shim_set_trig_tables(const void *trig)
{
    if (trig) {
        g_set_trig = *(const afs_clean_trig_t *)trig;
        g_trig = &g_set_trig;
    } else {
        g_trig = &g_default_trig;
    }
}

/* ------------------------------------------------------------------ */
/* Entity                                                              */
/* ------------------------------------------------------------------ */

/* `config` is first so the SDK mirror sits at entity offset 0, matching the
 * vendor layout; get_params() returns &config. */
typedef struct afs_shim_entity {
    fwi_afs_param_t         config;  /* SDK mirror at offset 0                */
    afs_clean_state_t  *clean;   /* clean-room instance                   */
    afs_clean_params_t *params;  /* clean's live parameter storage        */
    struct fwi_isp_ctx *ctx; /* optional corpus link (writeback)       */
} afs_shim_entity_t;

/* ------------------------------------------------------------------ */
/* Parameter mapping: afs_param_t -> afs_clean_params_t                */
/* ------------------------------------------------------------------ */

static void map_params(afs_clean_params_t *c, const fwi_afs_param_t *s)
{
    c->frame_index     = (int)s->afs_frame_id;
    c->mode            = (int)s->flicker_mode;
    c->seed_type       = (int)s->flicker_type_init;
    c->min_peak_ratio  = (int)s->flicker_ratio;
    c->enable          = (int)s->test_cfg.afs_en;
    c->gain_level      = (int)s->afs_sensor_info.ae_gain;
    c->image_width     = (int)s->afs_sensor_info.sensor_width;
    c->param_kind      = (unsigned char)s->type;
    c->platform_id     = (int)s->platform_id;
    c->auto_flag_param = (int)s->auto_afs_flag;
}

/* Runs on set and on every run: the framework may write the mirror through
 * get_params, and the vendor reads it live each frame. */
static void sync_params(afs_shim_entity_t *e)
{
    if (e->params)
        map_params(e->params, &e->config);
}

/* ------------------------------------------------------------------ */
/* SDK tuning/config corpus -> Afs parameter block                     */
/* ------------------------------------------------------------------ */
/* Mirrors the framework's context -> afs_param_t projection; flicker_mode is
 * never written by the vendor. Field mapping: docs/shim-mappings.md#afs */
static void apply_ctx_cfg(fwi_afs_param_t *p, const struct fwi_isp_ctx *c)
{
    /* Unused by the framework (manage.c has its own adapter); kept compiling. */
    p->platform_id = (int32_t)c->hw_cfg.platform_id;
    p->afs_frame_id = (int32_t)c->af_frame_count;
    /* fwi_sensor_state_t field names differ from afs_sensor_info's; copy by field. */
    memset(&p->afs_sensor_info, 0, sizeof(p->afs_sensor_info));
    p->afs_sensor_info.name = c->sensor.name;
    p->afs_sensor_info.hts = c->sensor.hts;
    p->afs_sensor_info.vts = c->sensor.vts;
    p->afs_sensor_info.pclk = c->sensor.pclk;
    p->afs_sensor_info.fps_fixed = c->sensor.fps_fixed;
    p->afs_sensor_info.wdr_mode = c->sensor.wdr_mode;
    p->afs_sensor_info.sensor_width = c->sensor.sensor_width;
    p->afs_sensor_info.sensor_height = c->sensor.sensor_height;
    p->afs_sensor_info.fps = c->sensor.fps;
    p->afs_sensor_info.ae_gain = (int32_t)c->sensor.ae_gain;

    p->flicker_ratio =
        c->tuning.modules.flicker_ratio;
    p->flicker_type_init =
        c->tuning.modules.flicker_type;
    p->test_cfg.test_mode =
        c->tuning.enables.bench_mode;
    p->test_cfg.afs_en = c->tuning.enables.flicker_detect_en;
}

void afs_shim_update_cfg(void *obj, struct fwi_isp_ctx *ctx)
{
    afs_shim_entity_t *e = (afs_shim_entity_t *)obj;

    if (!e)
        return;

    e->ctx = ctx;
    if (ctx) {
        apply_ctx_cfg(&e->config, ctx);
        sync_params(e);
    }
}

/* ------------------------------------------------------------------ */
/* Result translation: clean type -> SDK enum detected_flicker_type    */
/* ------------------------------------------------------------------ */

static fwi_detected_flicker_type_e to_sdk_flicker(int clean_type)
{
    switch (clean_type) {
    case AFS_CLEAN_TYPE_HZ50:
        return FWI_FLICKER_50HZ;
    case AFS_CLEAN_TYPE_HZ60:
        return FWI_FLICKER_60HZ;
    default:
        return FWI_FLICKER_NO;
    }
}

/* detected_type -1 (frames < 3, unknown mode) leaves the persistent result as
 * is; a linked ctx also gets ae_ctl.flicker_type, as the vendor run stage does. */
static void map_result_to_sdk(afs_shim_entity_t *e, fwi_afs_result_t *result,
                              const afs_clean_result_t *cr)
{
    if (cr->detected_type >= 0)
        result->flicker_type_output = to_sdk_flicker(cr->detected_type);

    if (e->ctx)
        e->ctx->ae_ctl.flicker_type = result->flicker_type_output;
}

/* ------------------------------------------------------------------ */
/* Ops vtable                                                          */
/* ------------------------------------------------------------------ */

static int32_t shim_get(void *obj, fwi_afs_param_t **param)
{
    afs_shim_entity_t *e = (afs_shim_entity_t *)obj;

    if (!e || !param)
        return -1;
    *param = &e->config;
    return 0;
}

static int32_t shim_set(void *obj, fwi_afs_param_t *param, fwi_afs_result_t *result)
{
    afs_shim_entity_t *e = (afs_shim_entity_t *)obj;

    (void)result;                     /* the vendor set-params is a no-op */

    if (!e || !param)
        return -1;

    e->config = *param;
    sync_params(e);
    return 0;
}

static int32_t shim_run(void *obj, fwi_afs_stats_t *data, fwi_afs_result_t *result)
{
    /* The fwi vtable hands over the stats data itself; keep the descriptor
     * view (null data == "no statistics") for the code below. */
    fwi_afs_stats_desc_t sd = { .afs_stats = data };
    const fwi_afs_stats_desc_t *stats = &sd;
    afs_shim_entity_t *e = (afs_shim_entity_t *)obj;
    afs_clean_stats_t cs;
    afs_clean_result_t cr;
    const afs_clean_stats_t *csp;
    int rc;

    if (!e || !result)
        return -1;

    sync_params(e);

    /* Null stats -> "no statistics" (rc -1, detected_type 50); the vendor
     * would fault on a null inner pointer. */
    memset(&cs, 0, sizeof(cs));
    if (stats && stats->afs_stats) {
        cs.column_sum  = stats->afs_stats->afs_sum;
        cs.stats_width = stats->afs_stats->pic_width;
        cs.stats_height = stats->afs_stats->pic_height;
        csp = &cs;
    } else {
        csp = NULL;
    }

    cr.detected_type = -1;
    rc = clean_afs_run(e->clean, csp, &cr);
    map_result_to_sdk(e, result, &cr);

    return rc;
}

/* ------------------------------------------------------------------ */
/* SDK entry points                                                    */
/* ------------------------------------------------------------------ */

static fwi_afs_core_ops_t g_ops;

void *afs_init(fwi_afs_core_ops_t **ops)
{
    afs_shim_entity_t *e;
    const afs_clean_ops_t *clean_ops = NULL;

    ensure_default_trig();
    afs_clean_set_trig_provider(shim_trig_provider);

    e = (afs_shim_entity_t *)calloc(1, sizeof(*e));
    if (!e)
        return NULL;

    e->clean = clean_afs_init(&clean_ops);
    if (!e->clean) {
        free(e);
        return NULL;
    }

    if (clean_afs_get_params(e->clean, &e->params) != 0 || !e->params) {
        clean_afs_exit(e->clean);
        free(e);
        return NULL;
    }

    sync_params(e);

    g_ops.afs_set_params = shim_set;
    g_ops.afs_get_params = shim_get;
    g_ops.afs_run = shim_run;

    if (ops)
        *ops = &g_ops;

    return e;
}

void afs_exit(void *obj)
{
    afs_shim_entity_t *e = (afs_shim_entity_t *)obj;

    if (!e)
        return;
    if (e->clean)
        clean_afs_exit(e->clean);
    free(e);
}
