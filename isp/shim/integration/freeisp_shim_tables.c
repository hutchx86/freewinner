/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* freeisp_shim_tables.c - point each clean module's *_clean_tables_t at the
 * located rmm tables (no tuning bytes compiled in) and install them.
 * Field mapping: docs/shim-mappings.md#integration */
#include "freeisp_shim_tables.h"

#include <stdlib.h>
#include <string.h>

/* The freewinner locator: its `freeisp_tables_t` is the 38-member vendor
 * view and `freeisp_tables_load_rmm`/`_locate`/`_free` do all location work. */
#include "freeisp/rmm_tables.h"
#include "freeisp/pltm_presets.h"
#include "freeisp/ae_out_bias.h"

#include <stdio.h>

/* Every clean header declares its own freeisp_tables_t; rename each so all six
 * headers can coexist in this TU. */
#define freeisp_tables      freeisp_shadow_iso_tag
#define freeisp_tables_t    freeisp_shadow_iso_t
#define freeisp_get_tables  freeisp_shadow_iso_get
#include "iso_clean.h"
#undef freeisp_tables
#undef freeisp_tables_t
#undef freeisp_get_tables

#define freeisp_tables      freeisp_shadow_ae_tag
#define freeisp_tables_t    freeisp_shadow_ae_t
#define freeisp_get_tables  freeisp_shadow_ae_get
#include "ae_clean.h"
#undef freeisp_tables
#undef freeisp_tables_t
#undef freeisp_get_tables

#define freeisp_tables      freeisp_shadow_awb_tag
#define freeisp_tables_t    freeisp_shadow_awb_t
#define freeisp_get_tables  freeisp_shadow_awb_get
#include "awb_clean.h"
#undef freeisp_tables
#undef freeisp_tables_t
#undef freeisp_get_tables

#define freeisp_tables      freeisp_shadow_gtm_tag
#define freeisp_tables_t    freeisp_shadow_gtm_t
#define freeisp_get_tables  freeisp_shadow_gtm_get
#include "gtm_clean.h"
#undef freeisp_tables
#undef freeisp_tables_t
#undef freeisp_get_tables

#define freeisp_tables      freeisp_shadow_pltm_tag
#define freeisp_tables_t    freeisp_shadow_pltm_t
#define freeisp_get_tables  freeisp_shadow_pltm_get
#include "pltm_clean.h"
#undef freeisp_tables
#undef freeisp_tables_t
#undef freeisp_get_tables

#include "afs_clean.h"

/* The shim setters under test; opaque `const void *` knobs. */
#include "../iso_shim.h"
#include "../ae/ae_shim.h"
#include "../awb/awb_shim.h"
#include "../afs/afs_shim.h"
#include "../gtm/gtm_shim.h"
#include "../pltm/pltm_shim.h"

/* Shape guards; AeTblDef (vendor ae_table_info, 252 bytes) is copied whole into
 * ae_desc_t, so the two must stay byte-compatible. */
typedef char freeisp_shim_ae_desc_size_check[(sizeof(ae_desc_t) == 252) ? 1 : -1];
typedef char freeisp_shim_iso_shape_check[(ISO_CURVE_N == 350 && ISO_BP_N == 14) ? 1 : -1];
typedef char freeisp_shim_awb_shape_check[
    (AWB_NTRUST == 96 && AWB_NSPEED == 48 && AWB_NSSC == 64 && AWB_NREF == 10) ? 1 : -1];
typedef char freeisp_shim_gtm_shape_check[
    (GTM_PRE_ROWS * GTM_PRE_COLS == 11 * 256 &&
     GTM_EQ_ROWS * GTM_EQ_TAPS == 20 * 31 &&
     GTM_CONV_ROWS * GTM_CONV_COLS == 32 * 128) ? 1 : -1];
typedef char freeisp_shim_pltm_shape_check[
    (PLTM_STRENGTH_ROWS * PLTM_STRENGTH_COLS == 4 * 256 &&
     PLTM_CONV_ROWS * PLTM_CONV_COLS == 32 * 128) ? 1 : -1];
typedef char freeisp_shim_ae_shape_check[
    (AE_NWIN == 384 && AE_NHIST == 256 && AE_IDX_MAX == 1024 &&
     AE_NSEG == 10) ? 1 : -1];

static freeisp_tables_t    g_vendor;
static int                 g_loaded;

static ae_clean_tables_t   g_ae;
static awb_clean_tables_t  g_awb;
static afs_clean_trig_t    g_afs;
static iso_clean_tables_t  g_iso;
static gtm_clean_tables_t  g_gtm;
static pltm_clean_tables_t g_pltm;

/* PLTM presets: resolved separately from the table bundle (own text cache),
 * kept across freeisp_shim_tables_free(), NULL-injected when unresolved. */
static freeisp_pltm_presets_t g_presets;
static int                    g_presets_ok;
static char                   g_presets_status[96] = "not resolved";

static void presets_note(const char *src, const char *why)
{
    snprintf(g_presets_status, sizeof(g_presets_status), "%s (%s)", src, why);
}

/* Presets from the firmware image; on success, optionally seed the text cache. */
static void presets_from_image(const void *image, size_t len, const char *path,
                               const char *cache)
{
    const char *why = "no image";
    int ok = image ? freeisp_pltm_presets_extract(image, len, g_presets, &why) == 0
                   : freeisp_pltm_presets_extract_file(path, g_presets, &why) == 0;

    g_presets_ok = ok;
    if (!ok) {
        presets_note("neutral, firmware extraction failed", why);
        return;
    }
    presets_note("extracted from firmware", "ok");
    if (cache && cache[0] != '\0' &&
        freeisp_pltm_presets_save_text(cache,
            (const int32_t (*)[FREEISP_PLTM_PRESET_COLS])g_presets) != 0)
        presets_note("extracted from firmware", "cache write failed");
}

static void presets_from_cache(const char *cache)
{
    const char *why = "no cache file";

    g_presets_ok = freeisp_pltm_presets_load_text(cache, g_presets, &why) == 0;
    presets_note(g_presets_ok ? "from cache" : "neutral, no valid cache", why);
}

/* AE output biases: resolved like the presets, NULL-injected when unresolved. */
static freeisp_ae_out_bias_t  g_bias;
static int                    g_bias_ok;
static char                   g_bias_status[96] = "not resolved";

static void bias_note(const char *src, const char *why)
{
    snprintf(g_bias_status, sizeof(g_bias_status), "%s (%s)", src, why);
}

static void bias_from_image(const void *image, size_t len, const char *path,
                            const char *cache)
{
    const char *why = "no image";
    int ok = image ? freeisp_ae_out_bias_extract(image, len, g_bias, &why) == 0
                   : freeisp_ae_out_bias_extract_file(path, g_bias, &why) == 0;

    g_bias_ok = ok;
    if (!ok) {
        bias_note("not found, backlight network disabled", why);
        return;
    }
    bias_note("extracted from firmware", "ok");
    if (cache && cache[0] != '\0' && freeisp_ae_out_bias_save_text(cache, g_bias) != 0)
        bias_note("extracted from firmware", "cache write failed");
}

static void bias_from_cache(const char *cache)
{
    const char *why = "no cache file";

    g_bias_ok = freeisp_ae_out_bias_load_text(cache, g_bias, &why) == 0;
    bias_note(g_bias_ok ? "from cache" : "not found, backlight network disabled", why);
}

/* Whole-descriptor copy target (the vendor pointer is `const HW_S32 *`). */
static ae_desc_t           g_ae_table_default;

/* Feeds src/reg/base.c five defaults that have no tuning source; weak, so it
 * is inert unless base_shim.o is linked. */
extern void base_shim_set_locator(const void *anti_gamma_table,
                                  const void *rgb2yuv_matrix,
                                  const void *lsc_trig_cfg_def,
                                  const void *msc_trig_cfg_def,
                                  const void *isp_cm_color_temp)
    __attribute__((weak));

static void populate(void)
{
    const freeisp_tables_t *v = &g_vendor;

    memset(&g_ae, 0, sizeof(g_ae));
    g_ae.log2        = v->Ae_Log2;
    g_ae.evtab       = v->Ae_DeltaLvTbl;      /* owned, writable copy */
    g_ae.conv        = v->AeConverData;       /* shared with GTM/PLTM */
    g_ae.touchprob   = v->AeProbData;
    g_ae.kernel      = v->AeHistData;         /* shared with GTM */
    g_ae.pregamma    = v->AeGammaPre;         /* shared with GTM */
    g_ae.blmask      = v->AeBackLightWeight;
    g_ae.wght_matrix = v->Ae_LumWeight_win;
    g_ae.wght_avg    = v->Ae_LumWeight_avg;
    g_ae.wght_center = v->Ae_LumWeight_center;
    g_ae.wght_over   = v->Ae_OverExp_LumWeight;
    g_ae.wght_under  = v->Ae_UnderExp_LumWeight;
    g_ae.net_in      = v->IW;
    g_ae.net_bias    = v->b1;
    g_ae.net_out     = v->LW;
    g_ae.fno_ladder  = v->ae_fno_def;
    g_ae.fno_def     = v->ae_fno_def;
    memcpy(&g_ae_table_default, v->AeTblDef, sizeof(g_ae_table_default));
    g_ae.table_default = &g_ae_table_default;
    g_ae.auxprob     = v->AwbProbData;        /* shared with AWB */
    g_ae.net_out_bias = g_bias_ok ? g_bias : NULL;

    memset(&g_awb, 0, sizeof(g_awb));
    g_awb.trust       = v->AwbProbData;
    g_awb.speed_w     = v->AwbSpeedData;
    g_awb.class_label = v->AwbLightClassName;
    g_awb.std_trust   = v->AwbStdTempWeight;
    g_awb.temp_bright = v->AwbTempLvWeightDef;
    /* Not in the rmm image; NULL makes the clean core skip its no-stats
     * fallback (awb_shim_set_safe_gain() only affects the built-in table). */
    g_awb.safe_gain   = NULL;

    memset(&g_afs, 0, sizeof(g_afs));
    g_afs.sine   = (const int *)v->afs_sin;
    g_afs.cosine = (const int *)v->afs_cos;

    memset(&g_iso, 0, sizeof(g_iso));
    g_iso.gain_index_table   = v->TBL2GAIN;
    g_iso.gain_point_default = v->iso_gain_point;
    g_iso.lum_point_default  = v->iso_lum_point;
    g_iso.af_square_table    = v->af_square_lut;
    /* Not in the rmm image; NULL makes the clean core program zero AF IIR
     * feedback coefficients (iso_clean.c blk_af_cfg). */
    g_iso.af_iir_s           = NULL;

    memset(&g_gtm, 0, sizeof(g_gtm));
    g_gtm.guide_linear = v->gd_curve_linear;
    g_gtm.guide_low    = v->gd_curve_low;
    g_gtm.guide_high   = v->gd_curve_high;
    g_gtm.pre_gamma    = v->AeGammaPre;       /* shared with AE */
    g_gtm.eq_kernel    = v->AeHistData;       /* shared with AE */
    g_gtm.converge     = v->AeConverData;     /* shared with AE/PLTM */

    memset(&g_pltm, 0, sizeof(g_pltm));
    g_pltm.strength_bank = v->pltm_stren_tbl_buf;
    g_pltm.converge_bank = v->AeConverData;   /* shared with AE/GTM */
    g_pltm.presets       = g_presets_ok ? &g_presets[0][0] : NULL;

    if (base_shim_set_locator)
        base_shim_set_locator(v->anti_gamma_table, v->rgb2yuv_matrix,
                              v->lsc_trig_cfg_def, v->msc_trig_cfg_def,
                              v->isp_cm_color_temp);
}

static void install(void)
{
    ae_shim_set_tables(&g_ae);
    awb_shim_set_tables(&g_awb);
    afs_shim_set_trig_tables(&g_afs);
    iso_shim_set_tables(&g_iso);
    gtm_shim_set_tables(&g_gtm);
    pltm_shim_set_tables(&g_pltm);
}

static void uninstall(void)
{
    ae_shim_set_tables(NULL);
    awb_shim_set_tables(NULL);
    afs_shim_set_trig_tables(NULL);
    iso_shim_set_tables(NULL);
    gtm_shim_set_tables(NULL);
    pltm_shim_set_tables(NULL);
}

int freeisp_shim_tables_from_memory(const void *image, size_t len)
{
    freeisp_shim_tables_free();
    if (image == NULL || len == 0)
        return -1;

    if (freeisp_tables_locate(image, len, &g_vendor) != 0)
        return -1;
    presets_from_image(image, len, NULL, NULL);
    bias_from_image(image, len, NULL, NULL);

    populate();
    install();
    g_loaded = 1;
    return 0;
}

int freeisp_shim_tables_from_rmm(const char *path)
{
    freeisp_shim_tables_free();
    if (path == NULL)
        return -1;

    if (freeisp_tables_load_rmm(path, &g_vendor) != 0)
        return -1;
    presets_from_image(NULL, 0, path, NULL);
    bias_from_image(NULL, 0, path, NULL);

    populate();
    install();
    g_loaded = 1;
    return 0;
}

/* Never touches a vendor image; fail-closed via freeisp_tables_load_bundle(). */
int freeisp_shim_tables_from_cache(const char *path)
{
    freeisp_shim_tables_free();
    if (path == NULL || path[0] == '\0')
        return -1;

    if (freeisp_tables_load_bundle(path, &g_vendor) != 0)
        return -1;
    presets_from_cache(FREEISP_PLTM_PRESETS_PATH);
    bias_from_cache(FREEISP_AE_OUT_BIAS_PATH);

    populate();
    install();
    g_loaded = 1;
    return 0;
}

int freeisp_shim_tables_save_cache(const char *path)
{
    if (!g_loaded || path == NULL || path[0] == '\0')
        return -1;
    return freeisp_tables_save(path, &g_vendor);
}

int freeisp_shim_tables_from_rmm_or_cache(const char *rmm_path,
                                          const char *bundle_path)
{
    const char *bp = (bundle_path != NULL) ? bundle_path
                                           : FREEISP_TABLE_BUNDLE_PATH;

    /* 1. cache first; the image is read only for missing presets/biases (the
     *    shims hold &g_pltm / &g_ae, so updating the field is enough). */
    if (bp[0] != '\0' && freeisp_shim_tables_from_cache(bp) == 0) {
        if (!g_presets_ok && rmm_path != NULL && rmm_path[0] != '\0') {
            presets_from_image(NULL, 0, rmm_path, FREEISP_PLTM_PRESETS_PATH);
            g_pltm.presets = g_presets_ok ? &g_presets[0][0] : NULL;
        }
        if (!g_bias_ok && rmm_path != NULL && rmm_path[0] != '\0') {
            bias_from_image(NULL, 0, rmm_path, FREEISP_AE_OUT_BIAS_PATH);
            g_ae.net_out_bias = g_bias_ok ? g_bias : NULL;
        }
        return 0;
    }

    /* 2. locator fallback; seed the cache for the next boot.  A failed cache
     *    write must not fail the boot -- the tables are already installed. */
    if (rmm_path == NULL || rmm_path[0] == '\0')
        return -1;
    if (freeisp_shim_tables_from_rmm(rmm_path) != 0)
        return -1;
    if (bp[0] != '\0') {
        (void)freeisp_shim_tables_save_cache(bp);
        if (g_presets_ok &&
            freeisp_pltm_presets_save_text(FREEISP_PLTM_PRESETS_PATH,
                (const int32_t (*)[FREEISP_PLTM_PRESET_COLS])g_presets) != 0)
            presets_note("extracted from firmware", "cache write failed");
        if (g_bias_ok && freeisp_ae_out_bias_save_text(FREEISP_AE_OUT_BIAS_PATH, g_bias) != 0)
            bias_note("extracted from firmware", "cache write failed");
    }
    return 0;
}

void freeisp_shim_tables_free(void)
{
    /* Reset the shim knobs before the buffers they point at go away. */
    if (g_loaded)
        uninstall();

    freeisp_tables_free(&g_vendor);

    memset(&g_ae, 0, sizeof(g_ae));
    memset(&g_awb, 0, sizeof(g_awb));
    memset(&g_afs, 0, sizeof(g_afs));
    memset(&g_iso, 0, sizeof(g_iso));
    memset(&g_gtm, 0, sizeof(g_gtm));
    memset(&g_pltm, 0, sizeof(g_pltm));
    memset(&g_ae_table_default, 0, sizeof(g_ae_table_default));
    memset(&g_vendor, 0, sizeof(g_vendor));
    g_loaded = 0;
}

const void *freeisp_shim_tables_get(freeisp_shim_table_id_t id)
{
    if (!g_loaded)
        return NULL;

    switch (id) {
    case FREEISP_SHIM_TABLE_AE:   return &g_ae;
    case FREEISP_SHIM_TABLE_AWB:  return &g_awb;
    case FREEISP_SHIM_TABLE_AFS:  return &g_afs;
    case FREEISP_SHIM_TABLE_ISO:  return &g_iso;
    case FREEISP_SHIM_TABLE_GTM:  return &g_gtm;
    case FREEISP_SHIM_TABLE_PLTM: return &g_pltm;
    default:                      return NULL;
    }
}

const char *freeisp_shim_pltm_presets_status(void)
{
    return g_presets_status;
}

const char *freeisp_shim_ae_out_bias_status(void)
{
    return g_bias_status;
}
