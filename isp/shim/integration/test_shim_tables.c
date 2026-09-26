/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * test_shim_tables.c - host test for the on-device runtime table feed.
 *
 * Loads the two real stock `rmm` images, exercises both entry points (path and
 * memory), and verifies that the located vendor tables are mapped onto each
 * clean module's table block with the expected values.  It also checks that
 * every shim setter was handed exactly the block published by
 * freeisp_shim_tables_get() (i.e. the tables are actually installed).
 *
 * The six setters are capture stubs here: the real shims share the
 * `freeisp_get_tables()` symbol and so cannot all be linked into one binary.
 * The setter -> clean-core plumbing is covered by each module's own shim test.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freeisp_shim_tables.h"

/* Pull in the per-module table shapes; rename each clean header's own
 * `freeisp_tables` tag/typedef/provider out of the way (as the provider does). */
#define freeisp_tables      test_shadow_iso_tag
#define freeisp_tables_t    test_shadow_iso_t
#define freeisp_get_tables  test_shadow_iso_get
#include "iso_clean.h"
#undef freeisp_tables
#undef freeisp_tables_t
#undef freeisp_get_tables

#define freeisp_tables      test_shadow_ae_tag
#define freeisp_tables_t    test_shadow_ae_t
#define freeisp_get_tables  test_shadow_ae_get
#include "ae_clean.h"
#undef freeisp_tables
#undef freeisp_tables_t
#undef freeisp_get_tables

#define freeisp_tables      test_shadow_awb_tag
#define freeisp_tables_t    test_shadow_awb_t
#define freeisp_get_tables  test_shadow_awb_get
#include "awb_clean.h"
#undef freeisp_tables
#undef freeisp_tables_t
#undef freeisp_get_tables

#define freeisp_tables      test_shadow_gtm_tag
#define freeisp_tables_t    test_shadow_gtm_t
#define freeisp_get_tables  test_shadow_gtm_get
#include "gtm_clean.h"
#undef freeisp_tables
#undef freeisp_tables_t
#undef freeisp_get_tables

#define freeisp_tables      test_shadow_pltm_tag
#define freeisp_tables_t    test_shadow_pltm_t
#define freeisp_get_tables  test_shadow_pltm_get
#include "pltm_clean.h"
#undef freeisp_tables
#undef freeisp_tables_t
#undef freeisp_get_tables

#include "afs_clean.h"

/* Capture stubs for the six shim knobs. */
static const void *g_captured[FREEISP_SHIM_TABLE_COUNT];

void iso_shim_set_tables(const void *t)      { g_captured[FREEISP_SHIM_TABLE_ISO]  = t; }
void ae_shim_set_tables(const void *t)       { g_captured[FREEISP_SHIM_TABLE_AE]   = t; }
void awb_shim_set_tables(const void *t)      { g_captured[FREEISP_SHIM_TABLE_AWB]  = t; }
void gtm_shim_set_tables(const void *t)      { g_captured[FREEISP_SHIM_TABLE_GTM]  = t; }
void pltm_shim_set_tables(const void *t)     { g_captured[FREEISP_SHIM_TABLE_PLTM] = t; }
void afs_shim_set_trig_tables(const void *t) { g_captured[FREEISP_SHIM_TABLE_AFS]  = t; }

static int g_checks;
static int g_fails;

#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        g_checks++;                                                       \
        if (!(cond)) {                                                    \
            g_fails++;                                                    \
            printf("  FAIL [%s]: %s\n", img, (msg));                      \
        }                                                                 \
    } while (0)

static void check_ae(const char *img)
{
    const ae_clean_tables_t *t =
        (const ae_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_AE);
    uint32_t save;

    CHECK(t != NULL, "AE block is NULL");
    if (!t) return;

    CHECK(t->log2 != NULL && t->log2[5] == 2585, "Ae_Log2[5] != 2585");
    CHECK(t->fno_ladder != NULL && t->fno_ladder[0] == 141 && t->fno_ladder[15] == 3794,
          "aperture ladder endpoints");
    CHECK(t->fno_def != NULL && t->fno_def[0] == 141 && t->fno_def[15] == 3794,
          "injected default aperture ladder endpoints");
    CHECK(t->table_default != NULL && t->table_default->length == 2 &&
          t->table_default->ev_step == 40 && t->table_default->shutter_shift == 0,
          "TABLEDEF header");
    CHECK(t->table_default != NULL && t->table_default->seg[0].min_exp == 8000 &&
          t->table_default->seg[1].max_gain == 6000,
          "TABLEDEF segments");
    CHECK(t->touchprob != NULL && t->touchprob[0] == 32, "TOUCHPROB[0]");
    CHECK(t->conv != NULL && t->conv[127] == 127 && t->conv[128] == 0,
          "CONV rows 0/1");
    CHECK(t->pregamma != NULL && t->pregamma[255] == 4095, "PREGAMMA row endpoint");
    CHECK(t->kernel != NULL && t->kernel[15] == 1, "KERNEL[15]");
    CHECK(t->blmask != NULL && t->blmask[0] == 0, "BLMASK[0]");
    CHECK(t->net_in != NULL && t->net_in[0] == 1820, "IW[0]");
    CHECK(t->net_bias != NULL && t->net_bias[0] == -9395, "b1[0]");
    CHECK(t->net_out != NULL && t->net_out[0] == -217, "LW[0]");
    CHECK(t->auxprob != NULL && t->auxprob[0] == 32, "AUXPROB[0]");

    /* Ae_DeltaLvTbl must be an owned, writable copy (blanked every frame). */
    save = t->evtab[512];
    t->evtab[512] = 0xA5A5A5A5u;
    CHECK(t->evtab[512] == 0xA5A5A5A5u, "evtab not a writable owned copy");
    t->evtab[512] = save;
}

static void check_awb(const char *img)
{
    const awb_clean_tables_t *t =
        (const awb_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_AWB);

    CHECK(t != NULL, "AWB block is NULL");
    if (!t) return;

    /*
     * These two string literals are data values used to locate the vendor's
     * own table block inside the rmm image (the "Outlier Light" anchor is what
     * the locator searches for; interoperability necessity).  They are not
     * compiled into the shipped clean tier; here they only confirm that the
     * mapped class-label table is the vendor's.
     */
    CHECK(t->class_label != NULL &&
          strcmp(t->class_label, "AH Light") == 0 &&
          strcmp(t->class_label + 9 * 32, "Outlier Light") == 0,
          "class labels");
    CHECK(t->std_trust != NULL && t->std_trust[0] == 64 &&
          t->std_trust[3] == 80 && t->std_trust[8] == 32 && t->std_trust[9] == 16,
          "std trust row");
    CHECK(t->speed_w != NULL && t->speed_w[0] == 216 && t->speed_w[AWB_NSSC] == 484,
          "speed rows");
    CHECK(t->temp_bright != NULL && t->temp_bright[0] == 24 && t->temp_bright[4] == 16,
          "temp x bright matrix");
    CHECK(t->trust != NULL && t->trust[0] == 32 && t->trust[256] == 32,
          "trust table");
}

static void check_afs(const char *img)
{
    const afs_clean_trig_t *t =
        (const afs_clean_trig_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_AFS);

    CHECK(t != NULL, "AFS block is NULL");
    if (!t) return;

    CHECK(t->sine != NULL && t->sine[0] == 0 && t->sine[1] == 20, "sine table");
    CHECK(t->cosine != NULL && t->cosine[0] == 100 && t->cosine[1] == 98, "cosine table");
}

static void check_iso(const char *img)
{
    const iso_clean_tables_t *t =
        (const iso_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_ISO);
    int i;

    CHECK(t != NULL, "ISO block is NULL");
    if (!t) return;

    CHECK(t->gain_point_default != NULL && t->gain_point_default[0] == 100 &&
          t->gain_point_default[13] == 819200, "gain breakpoints");
    CHECK(t->lum_point_default != NULL && t->lum_point_default[0] == 25 &&
          t->lum_point_default[13] == 349, "lum breakpoints");
    CHECK(t->af_square_table != NULL && t->af_square_table[0] == 1 &&
          t->af_square_table[15] == 255, "af_square_lut");
    CHECK(t->gain_index_table != NULL, "gain index table is NULL");
    if (t->gain_index_table) {
        CHECK(t->gain_index_table[0] == 100 && t->gain_index_table[349] == 819200,
              "gain index endpoints");
        for (i = 1; i < ISO_CURVE_N; i++)
            if (t->gain_index_table[i] < t->gain_index_table[i - 1]) break;
        CHECK(i == ISO_CURVE_N, "gain index table not monotone");
    }
}

static void check_gtm(const char *img)
{
    const gtm_clean_tables_t *t =
        (const gtm_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_GTM);

    CHECK(t != NULL, "GTM block is NULL");
    if (!t) return;

    CHECK(t->guide_linear != NULL && t->guide_linear[0] == 0 &&
          t->guide_linear[255] == 4096, "guide linear");
    CHECK(t->guide_low != NULL && t->guide_low[255] == 4096, "guide low");
    CHECK(t->guide_high != NULL && t->guide_high[255] == 4096, "guide high");
    CHECK(t->pre_gamma != NULL && t->pre_gamma[255] == 4095, "pre-gamma endpoint");
    CHECK(t->eq_kernel != NULL && t->eq_kernel[15] == 1, "eq kernel");
    CHECK(t->converge != NULL && t->converge[127] == 127, "converge row 0");
}

static void check_pltm(const char *img)
{
    const pltm_clean_tables_t *t =
        (const pltm_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_PLTM);

    CHECK(t != NULL, "PLTM block is NULL");
    if (!t) return;

    CHECK(t->strength_bank != NULL && t->strength_bank[0] == 256 &&
          t->strength_bank[255] == 0 && t->strength_bank[256] == 256,
          "strength bank");
    CHECK(t->converge_bank != NULL && t->converge_bank[127] == 127, "converge bank");
}

/* The vendor tables shared by several modules must be single-sourced. */
static void check_shared(const char *img)
{
    const ae_clean_tables_t   *ae   =
        (const ae_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_AE);
    const awb_clean_tables_t  *awb  =
        (const awb_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_AWB);
    const gtm_clean_tables_t  *gtm  =
        (const gtm_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_GTM);
    const pltm_clean_tables_t *pltm =
        (const pltm_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_PLTM);

    CHECK(ae && awb && ae->auxprob == awb->trust, "AE auxprob != AWB trust (AwbProbData)");
    CHECK(ae && gtm && ae->pregamma == gtm->pre_gamma, "AE/GTM pre-gamma not shared");
    CHECK(ae && gtm && ae->kernel == gtm->eq_kernel, "AE/GTM kernel not shared");
    CHECK(ae && gtm && ae->conv == gtm->converge, "AE/GTM converge not shared");
    CHECK(ae && pltm && ae->conv == pltm->converge_bank, "AE/PLTM converge not shared");
}

static int run_case(const char *img, int via_memory)
{
    int rc, id;

    printf("== %s (%s) ==\n", img, via_memory ? "memory" : "path");
    memset(g_captured, 0, sizeof(g_captured));

    if (via_memory) {
        FILE *f = fopen(img, "rb");
        unsigned char *buf;
        long n;
        if (!f) { printf("  FAIL: cannot open %s\n", img); g_fails++; return -1; }
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        buf = malloc((size_t)n);
        if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
            free(buf); fclose(f);
            printf("  FAIL: cannot read %s\n", img); g_fails++; return -1;
        }
        fclose(f);
        rc = freeisp_shim_tables_from_memory(buf, (size_t)n);
        free(buf);
    } else {
        rc = freeisp_shim_tables_from_rmm(img);
    }

    CHECK(rc == 0, "load failed");
    if (rc != 0) {
        freeisp_shim_tables_free();
        return -1;
    }

    check_ae(img);
    check_awb(img);
    check_afs(img);
    check_iso(img);
    check_gtm(img);
    check_pltm(img);
    check_shared(img);

    for (id = 0; id < FREEISP_SHIM_TABLE_COUNT; id++) {
        g_checks++;
        if (g_captured[id] != freeisp_shim_tables_get((freeisp_shim_table_id_t)id)) {
            g_fails++;
            printf("  FAIL [%s]: setter %d was not handed the mapped block\n", img, id);
        }
    }

    freeisp_shim_tables_free();
    for (id = 0; id < FREEISP_SHIM_TABLE_COUNT; id++) {
        CHECK(freeisp_shim_tables_get((freeisp_shim_table_id_t)id) == NULL,
              "block not cleared after free");
        CHECK(g_captured[id] == NULL, "setter not reset to defaults after free");
    }
    freeisp_shim_tables_free(); /* idempotent */
    return 0;
}

int main(int argc, char **argv)
{
    const char *imgs[32];
    char *list = NULL;
    int i, n;

    if (argc > 1) {
        n = argc - 1;
        if (n > 32) n = 32;
        for (i = 0; i < n; i++)
            imgs[i] = argv[i + 1];
        for (i = 0; i < n; i++)
            run_case(imgs[i], 0);
    } else {
        const char *env = getenv("FREEWINNER_RMM_IMAGES");
        char *p;
        if (env == NULL || *env == '\0') {
            printf("no rmm images given; pass image paths as arguments or set\n"
                   "FREEWINNER_RMM_IMAGES=path[:path]. Nothing to check.\n");
            return 0;
        }
        list = strdup(env);
        if (list == NULL) {
            printf("out of memory\n");
            return 1;
        }
        n = 0;
        for (p = list; p != NULL && n < 32; ) {
            char *sep = strchr(p, ':');
            if (sep != NULL)
                *sep = '\0';
            if (*p != '\0')
                imgs[n++] = p;
            p = (sep != NULL) ? sep + 1 : NULL;
        }
        /* run the last image through the memory entry point */
        for (i = 0; i < n; i++)
            run_case(imgs[i], i == n - 1);
        free(list);
    }

    printf("\n%d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
