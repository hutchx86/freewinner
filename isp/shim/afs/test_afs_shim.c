/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * test_afs_shim.c - host test for the AFS integration shim.
 *
 * Includes the SDK ABI header and the shim, drives the full
 * init/get/set/run cycle with synthetic afs_param_t and afs_stats_t, and
 * checks the clean -> SDK result translation (flicker type) and the
 * mode/auto/alternation behaviour end to end.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"
#include "afs_shim.h"

/* The shim's exported 3A entry points (fwi vtable shape; the framework
 * declares them in framework_isp.h).  Not in afs_shim.h: TUs that also see
 * afs_clean.h have a clean-core afs_init of a different type. */
void *afs_init(fwi_afs_core_ops_t **core_ops);
void  afs_exit(void *core_obj);

/*
 * musl's libm objects (pulled in by the shim's analytic default tables)
 * reference the ARM EH personality routines.  This test never unwinds; weak
 * definitions close the static link under the OpenWrt toolchain.
 */
#if defined(__arm__)
#define SHIM_WEAK __attribute__((weak))
SHIM_WEAK int __aeabi_unwind_cpp_pr0(void) { return 0; }
SHIM_WEAK int __aeabi_unwind_cpp_pr1(void) { return 0; }
SHIM_WEAK int __aeabi_unwind_cpp_pr2(void) { return 0; }
#endif

static int fails;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            fails++;                                                      \
        }                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        long va = (long)(a);                                              \
        long vb = (long)(b);                                              \
        if (va != vb) {                                                   \
            printf("FAIL %s:%d: %s=%ld expected %s=%ld\n", __FILE__,      \
                   __LINE__, #a, va, #b, vb);                             \
            fails++;                                                      \
        }                                                                 \
    } while (0)

/* Synthetic hardware statistics.  Static, not stack: afs_sum is 128 words. */
static struct fwi_afs_stats g_afs_stats;
static fwi_afs_stats_desc_t g_stats;

#define BAND_FREQ 6

static void set_flat_columns(void)
{
    int c;

    for (c = 0; c < ISP_AFS_NUM; c++)
        g_afs_stats.afs_sum[c] = 200u;
    g_afs_stats.pic_width = ISP_AFS_NUM;
    g_afs_stats.pic_height = 16;
    g_stats.afs_stats = &g_afs_stats;
}

/* One row of a per-column-phase banding pattern (matches the clean test). */
static void set_band_row(int row)
{
    int c;

    for (c = 0; c < ISP_AFS_NUM; c++) {
        double phi = 2.0 * 3.14159265358979323846 * (double)c /
                     (double)ISP_AFS_NUM;
        double v = 200.0 + 100.0 *
                   cos(2.0 * 3.14159265358979323846 * (double)BAND_FREQ *
                       (double)row / 16.0 + phi);

        g_afs_stats.afs_sum[c] = (unsigned int)lround(v);
    }
    g_afs_stats.pic_width = ISP_AFS_NUM;
    g_afs_stats.pic_height = 16;
    g_stats.afs_stats = &g_afs_stats;
}

static void param_defaults(fwi_afs_param_t *p)
{
    memset(p, 0, sizeof(*p));
    p->type = (fwi_afs_param_type_e)0;
    p->platform_id = 1;
    p->afs_frame_id = 0;
    p->auto_afs_flag = 0;
    p->flicker_ratio = 50;
    p->flicker_type_init = 0;
    p->afs_sensor_info.ae_gain = 256;      /* inside the 255..257 window */
    p->afs_sensor_info.sensor_width = 1;
    p->test_cfg.test_mode = 0;
    p->test_cfg.afs_en = 1;
    p->flicker_mode = FREEISP_FLICKER_AUTO;
}

static void test_lifecycle(void)
{
    fwi_afs_core_ops_t *ops = NULL;
    void *h;

    h = afs_init(&ops);
    CHECK(h != NULL);
    CHECK(ops != NULL);
    CHECK(ops->afs_set_params != NULL);
    CHECK(ops->afs_get_params != NULL);
    CHECK(ops->afs_run != NULL);
    afs_exit(h);
    afs_exit(NULL);
}

static void test_get_set_mirror(void)
{
    fwi_afs_core_ops_t *ops = NULL;
    fwi_afs_param_t param;
    fwi_afs_param_t *gp = NULL;
    void *h;

    h = afs_init(&ops);
    if (!h) {
        printf("FAIL: afs_init returned NULL\n");
        fails++;
        return;
    }

    /* get_params hands back the embedded mirror at entity offset 0. */
    CHECK_EQ(ops->afs_get_params(h, &gp), 0);
    CHECK(gp != NULL);
    CHECK((void *)gp == h);
    CHECK_EQ(gp->afs_frame_id, 0);

    /* set_params stores the mirror (vendor set-params is a no-op copy-wise,
     * but the framework may hand us a fresh block). */
    param_defaults(&param);
    param.afs_frame_id = 7;
    CHECK_EQ(ops->afs_set_params(h, &param, NULL), 0);
    CHECK_EQ(ops->afs_get_params(h, &gp), 0);
    CHECK_EQ(gp->afs_frame_id, 7);
    CHECK_EQ(gp->platform_id, 1);

    /* argument guards */
    CHECK_EQ(ops->afs_set_params(h, NULL, NULL), -1);
    CHECK_EQ(ops->afs_get_params(h, NULL), -1);
    CHECK_EQ(ops->afs_run(h, NULL, NULL), -1);
    CHECK_EQ(ops->afs_get_params(NULL, &gp), -1);

    afs_exit(h);
}

static void test_mode_dispatch(void)
{
    fwi_afs_core_ops_t *ops = NULL;
    fwi_afs_param_t param;
    fwi_afs_result_t res;
    void *h;

    h = afs_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    set_flat_columns();
    param_defaults(&param);
    param.afs_frame_id = 3;
    param.test_cfg.afs_en = 0;    /* forced modes ignore capture */

    param.flicker_mode = FREEISP_FLICKER_DISABLED;
    CHECK_EQ(ops->afs_set_params(h, &param, NULL), 0);
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->afs_run(h, g_stats.afs_stats, &res), 0);
    CHECK_EQ(res.flicker_type_output, FWI_FLICKER_NO);

    param.flicker_mode = FREEISP_FLICKER_50HZ;
    CHECK_EQ(ops->afs_set_params(h, &param, NULL), 0);
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->afs_run(h, g_stats.afs_stats, &res), 0);
    CHECK_EQ(res.flicker_type_output, FWI_FLICKER_50HZ);

    param.flicker_mode = FREEISP_FLICKER_60HZ;
    CHECK_EQ(ops->afs_set_params(h, &param, NULL), 0);
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->afs_run(h, g_stats.afs_stats, &res), 0);
    CHECK_EQ(res.flicker_type_output, FWI_FLICKER_60HZ);

    afs_exit(h);
}

static void test_auto_seed(void)
{
    fwi_afs_core_ops_t *ops = NULL;
    fwi_afs_param_t param;
    fwi_afs_result_t res;
    void *h;

    h = afs_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    set_flat_columns();
    param_defaults(&param);
    param.afs_frame_id = 3;
    param.test_cfg.afs_en = 0;    /* no capture; just seed selection */

    /* seed_type 0 -> 50, 2 -> 60; flat profiles never re-latch. */
    param.flicker_type_init = 0;
    CHECK_EQ(ops->afs_set_params(h, &param, NULL), 0);
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->afs_run(h, g_stats.afs_stats, &res), 0);
    CHECK_EQ(res.flicker_type_output, FWI_FLICKER_50HZ);

    param.flicker_type_init = 2;
    CHECK_EQ(ops->afs_set_params(h, &param, NULL), 0);
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->afs_run(h, g_stats.afs_stats, &res), 0);
    CHECK_EQ(res.flicker_type_output, FWI_FLICKER_60HZ);

    afs_exit(h);
}

static void test_null_stats(void)
{
    fwi_afs_core_ops_t *ops = NULL;
    fwi_afs_param_t param;
    fwi_afs_result_t res;
    fwi_afs_stats_desc_t empty;
    void *h;

    h = afs_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    param_defaults(&param);
    param.afs_frame_id = 3;
    CHECK_EQ(ops->afs_set_params(h, &param, NULL), 0);

    /* Null SDK stats handle -> clean "no statistics": -1, type 50. */
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->afs_run(h, NULL, &res), -1);
    CHECK_EQ(res.flicker_type_output, FWI_FLICKER_50HZ);

    /* Null inner stats pointer is defended the same way. */
    empty.afs_stats = NULL;
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->afs_run(h, empty.afs_stats, &res), -1);
    CHECK_EQ(res.flicker_type_output, FWI_FLICKER_50HZ);

    afs_exit(h);
}

static void test_banding_detection_and_alternation(void)
{
    fwi_afs_core_ops_t *ops = NULL;
    fwi_afs_param_t param;
    fwi_afs_result_t res;
    void *h;
    int fi;
    int first_cycle = -1;
    int second_cycle = -1;

    h = afs_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    param_defaults(&param);
    param.flicker_mode = FREEISP_FLICKER_AUTO;
    param.flicker_type_init = 0;
    param.flicker_ratio = 50;

    for (fi = 0; fi <= 40; fi++) {
        if (fi >= 3)
            set_band_row((fi - 3) % 16);
        param.afs_frame_id = fi;
        CHECK_EQ(ops->afs_set_params(h, &param, NULL), 0);
        memset(&res, 0, sizeof(res));
        CHECK_EQ(ops->afs_run(h, g_stats.afs_stats, &res), 0);

        if (fi == 18)
            first_cycle = res.flicker_type_output;
        if (fi == 34)
            second_cycle = res.flicker_type_output;
    }

    CHECK_EQ(first_cycle, FWI_FLICKER_50HZ);
    CHECK_EQ(second_cycle, FWI_FLICKER_60HZ);

    afs_exit(h);
}

static void test_ctx_mapping(void)
{
    fwi_afs_core_ops_t *ops = NULL;
    struct fwi_isp_ctx *gen;
    fwi_afs_param_t *gp = NULL;
    fwi_afs_result_t res;
    void *h;

    gen = (struct fwi_isp_ctx *)calloc(1, sizeof(*gen));
    if (!gen) {
        printf("FAIL: context allocation\n");
        fails++;
        return;
    }

    /* Synthetic tuning/config corpus. */
    gen->hw_cfg.platform_id = 0x521;
    gen->af_frame_count = 9;
    gen->sensor.sensor_width = 1;
    gen->sensor.ae_gain = 256;
    gen->tuning.modules.flicker_ratio = 42;
    gen->tuning.modules.flicker_type = 2;
    gen->tuning.enables.bench_mode = 1;
    gen->tuning.enables.flicker_detect_en = 0;
    gen->ae_ctl.flicker_mode = FREEISP_FLICKER_60HZ;
    gen->ae_ctl.flicker_type = FWI_FLICKER_NO;

    set_flat_columns();

    h = afs_init(&ops);
    if (!h) {
        fails++;
        free(gen);
        return;
    }
    CHECK_EQ(ops->afs_get_params(h, &gp), 0);

    afs_shim_update_cfg(h, gen);

    /* Corpus fields land in the mirror... */
    CHECK_EQ(gp->platform_id, 0x521);
    CHECK_EQ(gp->afs_frame_id, 9);
    CHECK_EQ(gp->flicker_ratio, 42);
    CHECK_EQ(gp->flicker_type_init, 2);
    CHECK_EQ(gp->test_cfg.test_mode, 1);
    CHECK_EQ(gp->test_cfg.afs_en, 0);
    CHECK_EQ(gp->afs_sensor_info.ae_gain, 256);
    /* ...but flicker_mode is not projected (the framework never sets it). */
    CHECK_EQ(gp->flicker_mode, FREEISP_FLICKER_DISABLED);

    /* Force-60: result is written to the SDK result and, through the linked
     * context, to ae_settings.flicker_type as the vendor AFS run stage does. */
    gp->flicker_mode = FREEISP_FLICKER_60HZ;
    CHECK_EQ(ops->afs_set_params(h, gp, NULL), 0);
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->afs_run(h, g_stats.afs_stats, &res), 0);
    CHECK_EQ(res.flicker_type_output, FWI_FLICKER_60HZ);
    CHECK_EQ(gen->ae_ctl.flicker_type, FWI_FLICKER_60HZ);

    afs_shim_update_cfg(h, NULL);
    afs_exit(h);
    free(gen);
}

static void test_custom_trig(void)
{
    struct test_trig {
        const int *sine;
        const int *cosine;
    };
    static int t_sin[32];
    static int t_cos[32];
    struct test_trig tt;
    fwi_afs_core_ops_t *ops = NULL;
    void *h;
    int m;

    for (m = 0; m < 32; m++) {
        t_sin[m] = (m % 2) ? 100 : 0;
        t_cos[m] = (m % 2) ? 0 : 100;
    }
    tt.sine = t_sin;
    tt.cosine = t_cos;

    afs_shim_set_trig_tables(&tt);
    h = afs_init(&ops);
    CHECK(h != NULL);
    afs_exit(h);

    afs_shim_set_trig_tables(NULL);
}

int main(void)
{
    /* Table provider: NULL selects the built-in pilot defaults. */
    afs_shim_set_trig_tables(NULL);

    test_lifecycle();
    test_get_set_mirror();
    test_mode_dispatch();
    test_auto_seed();
    test_null_stats();
    test_banding_detection_and_alternation();
    test_ctx_mapping();
    test_custom_trig();

    if (fails) {
        printf("FAILED: %d check(s)\n", fails);
        return 1;
    }
    printf("ok: AFS shim\n");
    return 0;
}
