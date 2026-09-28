/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_table_bundle.c - a bundle saved from a located rmm image installs the
 * same tables with the image absent; bad bundles install nothing; and the
 * cache-first resolver falls back bundle -> locator -> nothing. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

/* The on-disk format header length; mirrored here on purpose so a format
 * change breaks this test loudly. */
#define TB_HDR 32

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

/* ------------------------------------------------------- snapshots ---- */

#define SNAP_MAX 64
typedef struct {
    int       n;
    long long v[SNAP_MAX];
} snap_t;

static void snap_push(snap_t *s, long long x)
{
    if (s->n < SNAP_MAX) s->v[s->n++] = x;
}

/* Digest of every mapped block; a bundle reproducing it carries byte-equal
 * tuning to the locator path. */
static void snap_blocks(snap_t *s)
{
    const ae_clean_tables_t   *ae   =
        (const ae_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_AE);
    const awb_clean_tables_t  *awb  =
        (const awb_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_AWB);
    const afs_clean_trig_t    *afs  =
        (const afs_clean_trig_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_AFS);
    const iso_clean_tables_t  *iso  =
        (const iso_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_ISO);
    const gtm_clean_tables_t  *gtm  =
        (const gtm_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_GTM);
    const pltm_clean_tables_t *pltm =
        (const pltm_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_PLTM);

    s->n = 0;
    if (ae) {
        snap_push(s, ae->log2[5]);
        snap_push(s, ae->fno_ladder[0]); snap_push(s, ae->fno_ladder[15]);
        snap_push(s, ae->table_default->length);
        snap_push(s, ae->table_default->ev_step);
        snap_push(s, ae->table_default->seg[0].min_exp);
        snap_push(s, ae->touchprob[0]);
        snap_push(s, ae->conv[127]);
        snap_push(s, ae->pregamma[255]);
        snap_push(s, ae->kernel[15]);
        snap_push(s, ae->blmask[0]);
        snap_push(s, ae->net_in[0]);
        snap_push(s, ae->net_bias[0]);
        snap_push(s, ae->net_out[0]);
        snap_push(s, ae->auxprob[0]);
    }
    if (awb) {
        snap_push(s, awb->std_trust[0]); snap_push(s, awb->std_trust[3]);
        snap_push(s, awb->speed_w[0]); snap_push(s, awb->speed_w[AWB_NSSC]);
        snap_push(s, awb->temp_bright[0]);
        snap_push(s, awb->trust[0]); snap_push(s, awb->trust[256]);
    }
    if (afs) {
        snap_push(s, afs->sine[1]);
        snap_push(s, afs->cosine[1]);
    }
    if (iso) {
        snap_push(s, iso->gain_point_default[0]);
        snap_push(s, iso->gain_point_default[13]);
        snap_push(s, iso->lum_point_default[0]);
        snap_push(s, iso->lum_point_default[13]);
        snap_push(s, iso->af_square_table[0]);
        snap_push(s, iso->af_square_table[15]);
        snap_push(s, iso->gain_index_table[0]);
        snap_push(s, iso->gain_index_table[349]);
    }
    if (gtm) {
        snap_push(s, gtm->guide_linear[255]);
        snap_push(s, gtm->guide_low[255]);
        snap_push(s, gtm->guide_high[255]);
        snap_push(s, gtm->pre_gamma[255]);
        snap_push(s, gtm->eq_kernel[15]);
        snap_push(s, gtm->converge[127]);
    }
    if (pltm) {
        snap_push(s, pltm->strength_bank[0]);
        snap_push(s, pltm->strength_bank[255]);
        snap_push(s, pltm->converge_bank[127]);
    }
}

static int snap_eq(const snap_t *a, const snap_t *b)
{
    return a->n == b->n && memcmp(a->v, b->v, (size_t)a->n * sizeof(a->v[0])) == 0;
}

static int labels_ok(void)
{
    const awb_clean_tables_t *awb =
        (const awb_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_AWB);
    return awb && awb->class_label &&
           strcmp(awb->class_label, "AH Light") == 0 &&
           strcmp(awb->class_label + 9 * 32, "Outlier Light") == 0;
}

/* Single-sourced shared tables stay aliased across modules. */
static int shared_ok(void)
{
    const ae_clean_tables_t   *ae   =
        (const ae_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_AE);
    const awb_clean_tables_t  *awb  =
        (const awb_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_AWB);
    const gtm_clean_tables_t  *gtm  =
        (const gtm_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_GTM);
    const pltm_clean_tables_t *pltm =
        (const pltm_clean_tables_t *)freeisp_shim_tables_get(FREEISP_SHIM_TABLE_PLTM);

    return ae && awb && gtm && pltm &&
           ae->auxprob == awb->trust &&
           ae->pregamma == gtm->pre_gamma &&
           ae->kernel == gtm->eq_kernel &&
           ae->conv == gtm->converge &&
           ae->conv == pltm->converge_bank;
}

static void reset_captured(void) { memset(g_captured, 0, sizeof(g_captured)); }

static int all_captured_null(void)
{
    int id;
    for (id = 0; id < FREEISP_SHIM_TABLE_COUNT; id++)
        if (g_captured[id] != NULL) return 0;
    return 1;
}

static int all_blocks_null(void)
{
    int id;
    for (id = 0; id < FREEISP_SHIM_TABLE_COUNT; id++)
        if (freeisp_shim_tables_get((freeisp_shim_table_id_t)id) != NULL) return 0;
    return 1;
}

/* ----------------------------------------------------------- files ---- */

static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

static int copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb"), *out;
    unsigned char buf[4096];
    size_t n;
    if (!in) return -1;
    out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { fclose(in); fclose(out); return -1; }
    fclose(in);
    return fclose(out) == 0 ? 0 : -1;
}

/* Copy `src` to `dst`, keeping only the first `keep` bytes. */
static int copy_prefix(const char *src, const char *dst, long keep)
{
    FILE *in = fopen(src, "rb"), *out;
    unsigned char buf[4096];
    long left = keep;
    if (!in) return -1;
    out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    while (left > 0) {
        size_t want = (size_t)(left < (long)sizeof(buf) ? left : (long)sizeof(buf));
        size_t n = fread(buf, 1, want, in);
        if (n == 0) break;
        if (fwrite(buf, 1, n, out) != n) { fclose(in); fclose(out); return -1; }
        left -= (long)n;
    }
    fclose(in);
    return fclose(out) == 0 ? 0 : -1;
}

static int patch_byte(const char *path, long off, unsigned char mask)
{
    FILE *f = fopen(path, "r+b");
    unsigned char b;
    if (!f) return -1;
    if (fseek(f, off, SEEK_SET) != 0) { fclose(f); return -1; }
    if (fread(&b, 1, 1, f) != 1) { fclose(f); return -1; }
    b ^= mask;
    if (fseek(f, off, SEEK_SET) != 0) { fclose(f); return -1; }
    if (fwrite(&b, 1, 1, f) != 1) { fclose(f); return -1; }
    return fclose(f) == 0 ? 0 : -1;
}

/* A rejected bundle must install nothing and leave free() safe. */
static void expect_reject(const char *img, const char *path, const char *what)
{
    int rc;
    freeisp_shim_tables_free();
    reset_captured();
    rc = freeisp_shim_tables_from_cache(path);
    CHECK(rc != 0, what);
    CHECK(all_captured_null(), "setters touched on rejected bundle");
    CHECK(all_blocks_null(), "blocks installed from rejected bundle");
    freeisp_shim_tables_free();  /* idempotent after a failure */
}

static int run_image(const char *img, const char *dir)
{
    char bundle[512], bad[512], trunc[512], seed[512], empty[512];
    snap_t ref, cache;
    long sz;

    snprintf(bundle, sizeof(bundle), "%s/bundle.bin", dir);
    snprintf(bad,    sizeof(bad),    "%s/bad.bin", dir);
    snprintf(trunc,  sizeof(trunc),  "%s/trunc.bin", dir);
    snprintf(seed,   sizeof(seed),   "%s/seed.bin", dir);
    snprintf(empty,  sizeof(empty),  "%s/empty.bin", dir);
    remove(bundle); remove(bad); remove(trunc); remove(seed); remove(empty);

    printf("== %s ==\n", img);

    /* 1. locator path, then persist the bundle. */
    freeisp_shim_tables_free();
    reset_captured();
    CHECK(freeisp_shim_tables_from_rmm(img) == 0, "locator load failed");
    snap_blocks(&ref);
    CHECK(ref.n > 0 && ref.n < SNAP_MAX, "snapshot incomplete or truncated");
    CHECK(labels_ok(), "locator class labels");
    CHECK(freeisp_shim_tables_save_cache(bundle) == 0, "save bundle failed");
    sz = file_size(bundle);
    CHECK(sz > TB_HDR, "saved bundle is non-trivial");
    freeisp_shim_tables_free();

    /* 2. cache-first load with the vendor image absent: full equality. */
    reset_captured();
    CHECK(freeisp_shim_tables_from_rmm_or_cache("/nonexistent/rmm", bundle) == 0,
          "cache-first load failed");
    snap_blocks(&cache);
    CHECK(snap_eq(&ref, &cache), "cached tables differ from located tables");
    CHECK(labels_ok(), "cached class labels");
    CHECK(shared_ok(), "cached shared tables not single-sourced");
    {
        int id, ok = 1;
        for (id = 0; id < FREEISP_SHIM_TABLE_COUNT; id++)
            if (g_captured[id] != freeisp_shim_tables_get((freeisp_shim_table_id_t)id))
                ok = 0;
        CHECK(ok, "setters not handed the cached blocks");
    }
    freeisp_shim_tables_free();

    /* 3. fail-closed: every corruption installs nothing. */
    expect_reject(img, "/nonexistent/definitely-missing.bin", "missing bundle not rejected");
    CHECK(copy_file(bundle, bad) == 0, "copy bundle");
    CHECK(patch_byte(bad, 0, 0xFF) == 0, "patch magic");
    expect_reject(img, bad, "bad magic not rejected");
    CHECK(copy_file(bundle, bad) == 0, "copy bundle");
    CHECK(patch_byte(bad, 8, 0xFF) == 0, "patch version");
    expect_reject(img, bad, "bad version not rejected");
    CHECK(copy_file(bundle, bad) == 0, "copy bundle");
    CHECK(patch_byte(bad, 12, 0xFF) == 0, "patch count");
    expect_reject(img, bad, "bad count not rejected");
    CHECK(copy_file(bundle, bad) == 0, "copy bundle");
    CHECK(patch_byte(bad, 16, 0x01) == 0, "patch layout_crc");
    expect_reject(img, bad, "bad layout_crc not rejected");
    CHECK(copy_file(bundle, bad) == 0, "copy bundle");
    CHECK(patch_byte(bad, TB_HDR + 8, 0xA5) == 0, "patch payload");
    expect_reject(img, bad, "corrupt payload not rejected");
    CHECK(copy_prefix(bundle, trunc, sz / 2) == 0, "truncate bundle");
    expect_reject(img, trunc, "truncated bundle not rejected");
    CHECK(copy_prefix(bundle, trunc, TB_HDR) == 0, "header-only bundle");
    expect_reject(img, trunc, "header-only bundle not rejected");
    {
        FILE *f = fopen(empty, "wb");
        if (f) fclose(f);
    }
    expect_reject(img, empty, "empty bundle not rejected");
    remove(bad); remove(trunc); remove(empty);

    /* 4. locator fallback: absent bundle + valid rmm re-seeds the cache. */
    reset_captured();
    CHECK(freeisp_shim_tables_from_rmm_or_cache(img, seed) == 0,
          "locator fallback failed");
    CHECK(file_size(seed) == sz, "fallback did not seed the bundle");
    freeisp_shim_tables_free();
    /* A corrupt cache must fall back to rmm and overwrite the bad bundle. */
    CHECK(patch_byte(seed, TB_HDR + 8, 0xA5) == 0, "corrupt seeded bundle");
    CHECK(freeisp_shim_tables_from_rmm_or_cache(img, seed) == 0,
          "corrupt-cache fallback failed");
    snap_blocks(&cache);
    CHECK(snap_eq(&ref, &cache), "fallback tables differ from located tables");
    CHECK(file_size(seed) == sz, "corrupt cache not repaired");
    freeisp_shim_tables_free();
    remove(seed);
    remove(bundle);

    /* 5. both sources absent -> nothing installed. */
    reset_captured();
    CHECK(freeisp_shim_tables_from_rmm_or_cache("/nonexistent/rmm",
                                                "/nonexistent/dir/x.bin") != 0,
          "both-absent not rejected");
    CHECK(all_captured_null(), "setters touched with both sources absent");
    freeisp_shim_tables_free();

    /* 6. save_cache with nothing loaded is a clean failure. */
    CHECK(freeisp_shim_tables_save_cache(bundle) != 0, "save_cache with no tables");
    remove(bundle);

    return 0;
}

int main(int argc, char **argv)
{
    const char *imgs[32];
    const char *base = getenv("TMPDIR");
    char dir[512];
    char *list = NULL;
    int i, n;

    if (base == NULL || *base == '\0') base = "/tmp";
    /* Per-process directory so concurrent test runs (e.g. two checkouts) do
     * not clobber each other's bundle files. */
    snprintf(dir, sizeof(dir), "%s/freeisp_bundle_test.%ld", base, (long)getpid());
    (void)mkdir(dir, 0755);

    if (argc > 1) {
        n = argc - 1;
        if (n > 32) n = 32;
        for (i = 0; i < n; i++)
            imgs[i] = argv[i + 1];
        for (i = 0; i < n; i++)
            run_image(imgs[i], dir);
    } else {
        const char *env = getenv("FREEWINNER_RMM_IMAGES");
        char *p;
        if (env == NULL || *env == '\0') {
            printf("no rmm images given; pass image paths as arguments or set\n"
                   "FREEWINNER_RMM_IMAGES=path[:path]. Nothing to check.\n");
            return 0;
        }
        list = strdup(env);
        if (list == NULL) { printf("out of memory\n"); return 1; }
        n = 0;
        for (p = list; p != NULL && n < 32; ) {
            char *sep = strchr(p, ':');
            if (sep != NULL) *sep = '\0';
            if (*p != '\0') imgs[n++] = p;
            p = (sep != NULL) ? sep + 1 : NULL;
        }
        for (i = 0; i < n; i++)
            run_image(imgs[i], dir);
        free(list);
    }

    (void)rmdir(dir);
    printf("\n%d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
