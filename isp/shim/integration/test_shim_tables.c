/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_shim_tables.c - host test for the table feed: loads two stock rmm images
 * by path and from memory, checks each mapped block and that it was installed.
 * Setters are capture stubs: the real shims all define freeisp_get_tables(). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freeisp_shim_tables.h"
#include "freeisp/ae_out_bias.h"

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

/* Spec ae.md 15.10: 16 strictly increasing positive codes. */
static int ladder_ok(const int32_t *l)
{
    int i;

    if (l == NULL || l[0] <= 0)
        return 0;
    for (i = 1; i < 16; i++)
        if (l[i] <= l[i - 1])
            return 0;
    return 1;
}

/* Spec ae.md 15.11: 1..AE_NSEG valid segments, zero beyond `length`. */
static int desc_ok(const ae_desc_t *d)
{
    static const ae_segment_t zero;
    unsigned int i;

    if (d == NULL || d->length < 1u || d->length > AE_NSEG)
        return 0;
    for (i = 0; i < AE_NSEG; i++) {
        const ae_segment_t *s = &d->seg[i];

        if (i >= d->length) {
            if (memcmp(s, &zero, sizeof(zero)) != 0)
                return 0;
        } else if (s->max_exp == 0u || s->min_exp < s->max_exp ||
                   s->min_gain > s->max_gain || s->min_iris == 0u) {
            return 0;
        }
    }
    return 1;
}

static void check_ae(const char *img)
{
    const ae_clean_tables_t *t =
        (const ae_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_AE);
    uint32_t save;

    CHECK(t != NULL, "AE block is NULL");
    if (!t) return;

    CHECK(t->log2 != NULL && t->log2[5] == 2585, "Ae_Log2[5] != 2585");
    CHECK(t->fno_ladder != NULL && t->fno_def == t->fno_ladder,
          "default aperture ladder not mapped to both slots");
    CHECK(ladder_ok(t->fno_ladder), "aperture ladder not 16 increasing positive codes");
    CHECK(desc_ok(t->table_default), "TABLEDEF structure");
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

    /* Label strings are the locator's interoperability anchor, not shipped
     * data; here they confirm the mapped class-label table is the vendor's. */
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

/* AE output biases: found once, role/validity rules hold, mapped into the AE block.
 * Values are printed for the log only; the source holds none. */
static void check_ae_out_bias(const char *img, int mapped)
{
    const ae_clean_tables_t *t =
        (const ae_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_AE);
    freeisp_ae_out_bias_t b;
    const char *why = "?";
    size_t at[2] = { 0, 0 };
    FILE *f = fopen(img, "rb");
    unsigned char *buf = NULL;
    long n = 0;
    int rc = -1;

    if (f && fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 &&
        fseek(f, 0, SEEK_SET) == 0 && (buf = malloc((size_t)n)) != NULL &&
        fread(buf, 1, (size_t)n, f) == (size_t)n)
        rc = freeisp_ae_out_bias_extract_at(buf, (size_t)n, b, at, &why);
    if (f)
        fclose(f);
    free(buf);

    CHECK(rc == 0, "AE output biases not extracted");
    if (rc != 0) {
        printf("  ae out bias: %s\n", why);
        return;
    }
    printf("  ae out bias: B0=%d @0x%zx  B1=%d @0x%zx\n", (int)b[0], at[0], (int)b[1], at[1]);
    CHECK(freeisp_ae_out_bias_validate(b, &why) == 0, "AE output biases invalid");
    CHECK(b[0] < b[1], "AE output biases not ordered B0 < B1");
    CHECK(at[0] > at[1] && at[0] - at[1] < 4096, "B0 site not after B1 within 4 KiB");
    if (!mapped)
        return;
    CHECK(strncmp(freeisp_shim_ae_out_bias_status(), "extracted from firmware", 23) == 0,
          "AE output-bias status");
    CHECK(t != NULL && t->net_out_bias != NULL &&
          t->net_out_bias[0] == b[0] && t->net_out_bias[1] == b[1],
          "AE block net_out_bias not the extracted pair");
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
        check_ae_out_bias(img, 0);
        freeisp_shim_tables_free();
        return -1;
    }

    check_ae(img);
    check_awb(img);
    check_afs(img);
    check_iso(img);
    check_gtm(img);
    check_pltm(img);
    check_ae_out_bias(img, 1);
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
