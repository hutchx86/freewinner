/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_ae_shim.c - host test for the AE shim: init/get/set/run with synthetic
 * params and stats, checking the clean -> SDK result translation. */
#include <stdio.h>
#include <string.h>

#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"
#include "ae_shim.h"

/* Shim entry points (declared by framework_isp.h, not ae_shim.h: TUs seeing
 * ae_clean.h have a clean ae_init of another type). */
void *ae_init(fwi_ae_core_ops_t **ae_core_ops);
void  ae_exit(void *ae_core_obj);

/* fwi_ae_result.sensor_set is opaque in the generated ABI; view it as
 * fwi_sensor_settings_t (same as the shim and the framework). */
static fwi_sensor_settings_t *ev_sets(fwi_sensor_setting_t *s)
{
    return (fwi_sensor_settings_t *)s;
}

/* musl libm (for the placeholder log2 table) references ARM EH personality
 * routines; weak stubs close the static link, as this test never unwinds. */
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

/* Synthetic hardware statistics; static, not stack (the struct is large). */
static struct fwi_ae_stats g_sdk_stats;
static fwi_ae_stats_desc_t g_stats;

static void base_param(fwi_ae_param_t *p)
{
    int i;

    memset(p, 0, sizeof(*p));
    p->type = FWI_ISP_AE_INIT_DATA;
    p->platform_id = 1;

    p->ae_init.define_ae_table = 0;
    p->ae_init.ae_max_level = 1000;
    p->ae_init.ae_delay_frame = 2;
    p->ae_init.exposure_delay_frame = 1;
    p->ae_init.gain_delay_frame = 1;
    p->ae_init.exposure_comp_step = 4;
    p->ae_init.ae_iso2gain_ratio = 100;
    p->ae_init.wdr_cfg[0] = 16;
    p->ae_init.wdr_cfg[1] = 8;
    p->ae_init.wdr_cfg[2] = 16;
    p->ae_init.wdr_cfg[3] = 1;
    p->ae_init.gain_ratio = 0.0;

    p->ae_setting.exposure_mode = FWI_EXPOSURE_AUTO;
    p->ae_setting.iso_mode = FWI_ISO_AUTO;
    p->ae_setting.light_mode = FWI_NORMAL_LIGHT;
    p->ae_setting.exposure_metering_mode = FWI_AE_METERING_MODE_MATRIX;
    p->ae_setting.ae_mode = FWI_AE_NORM;
    p->ae_setting.flicker_type = FWI_FLICKER_NO;
    p->ae_setting.scene_mode = FWI_SCENE_MODE_PREVIEW;
    p->ae_setting.flash_mode = FWI_FLASH_MODE_OFF;
    p->ae_setting.ae_coord.x1 = 800;
    p->ae_setting.ae_coord.y1 = 800;
    p->ae_setting.ae_coord.x2 = 1200;
    p->ae_setting.ae_coord.y2 = 1200;
    p->ae_setting.exposure_cfg[0] = 256;
    p->ae_setting.exposure_cfg[1] = 256;
    p->ae_setting.exposure_cfg[2] = 256;
    p->ae_setting.exposure_cfg[3] = 256;
    p->ae_setting.exposure_cfg[4] = 0;
    p->ae_setting.exposure_cfg[5] = 0;
    p->ae_setting.exposure_cfg[6] = 0;
    p->ae_setting.exposure_cfg[7] = 0;
    p->ae_setting.exposure_cfg[8] = 3;
    p->ae_setting.exposure_cfg[9] = 128;
    for (i = 10; i < 14; i++)
        p->ae_setting.exposure_cfg[i] = 8;

    p->ae_sensor_info.pclk = 432000000;
    p->ae_sensor_info.hts = 2000;
    p->ae_sensor_info.vts = 1250;
    p->ae_sensor_info.gain_min = 16;
    p->ae_sensor_info.gain_max = 4096;
    p->ae_sensor_info.sensor_width = 1920;
    p->ae_sensor_info.sensor_height = 1080;

    p->test_cfg.ae_en = 1;
    p->test_cfg.ae_forced = 0;
}

/* Match the framework setup sequence: an INI_DATA then an UPDATE_AE_TABLE. */
static void init_and_build(fwi_ae_core_ops_t *ops, void *h, fwi_ae_param_t *p)
{
    p->type = FWI_ISP_AE_INIT_DATA;
    CHECK_EQ(ops->ae_set_params(h, p, NULL), 0);
    p->type = FWI_ISP_AE_UPDATE_AE_TABLE;
    CHECK_EQ(ops->ae_set_params(h, p, NULL), 0);
    p->type = FWI_ISP_AE_INIT_DATA;
}

/* The framework writes the frame id into the stored parameter block each
 * frame; the shim consumes it, so the test must advance it too. */
static void set_frame(fwi_ae_core_ops_t *ops, void *h, int frame)
{
    fwi_ae_param_t *stored = NULL;

    if (ops->ae_get_params(h, &stored) == 0 && stored)
        stored->ae_frame_id = frame;
}

static void fill_flat_stats(int luma)
{
    int i;

    memset(&g_sdk_stats, 0, sizeof(g_sdk_stats));
    for (i = 0; i < ISP_AE_ROW * ISP_AE_COL; i++)
        g_sdk_stats.average[i] = (uint32_t)luma;
    g_sdk_stats.hist[luma] = 200;
    g_stats.ae_stats = &g_sdk_stats;
}

static void fill_wdr_stats(void)
{
    int i;

    memset(&g_sdk_stats, 0, sizeof(g_sdk_stats));
    for (i = 0; i < ISP_AE_ROW * ISP_AE_COL; i++) {
        g_sdk_stats.average[i] = 128;
        g_sdk_stats.accum_r[i / ISP_AE_COL][i % ISP_AE_COL] = 1000;
        g_sdk_stats.accum_g[i / ISP_AE_COL][i % ISP_AE_COL] = 1000;
        g_sdk_stats.accum_b[i / ISP_AE_COL][i % ISP_AE_COL] = 1000;
    }
    for (i = 0; i < 192; i++)
        g_sdk_stats.hist[i] = 1;
    g_stats.ae_stats = &g_sdk_stats;
}

static void test_lifecycle(void)
{
    fwi_ae_core_ops_t *ops = NULL;
    void *h;

    h = ae_init(&ops);
    CHECK(h != NULL);
    CHECK(ops != NULL);
    CHECK(ops->ae_set_params != NULL);
    CHECK(ops->ae_get_params != NULL);
    CHECK(ops->ae_run != NULL);
    ae_exit(h);
    ae_exit(NULL);
}

static void test_get_set_mirror(void)
{
    fwi_ae_core_ops_t *ops = NULL;
    fwi_ae_param_t param;
    fwi_ae_param_t *gp = NULL;
    void *h;

    h = ae_init(&ops);
    if (!h) {
        printf("FAIL: ae_init returned NULL\n");
        fails++;
        return;
    }

    /* get_params hands back the embedded mirror at entity offset 0. */
    CHECK_EQ(ops->ae_get_params(h, &gp), 0);
    CHECK(gp != NULL);
    CHECK((void *)gp == h);
    CHECK_EQ(gp->ae_frame_id, 0);

    base_param(&param);
    param.ae_frame_id = 7;
    CHECK_EQ(ops->ae_set_params(h, &param, NULL), 0);
    CHECK_EQ(ops->ae_get_params(h, &gp), 0);
    CHECK_EQ(gp->ae_frame_id, 7);
    CHECK_EQ(gp->platform_id, 1);

    /* unknown param kind is rejected */
    param.type = FWI_ISP_AE_PARAM_TYPE_MAX;
    CHECK_EQ(ops->ae_set_params(h, &param, NULL), -1);

    /* argument guards */
    CHECK_EQ(ops->ae_set_params(h, NULL, NULL), -1);
    CHECK_EQ(ops->ae_get_params(h, NULL), -1);
    CHECK_EQ(ops->ae_run(h, NULL, NULL), -1);
    CHECK_EQ(ops->ae_get_params(NULL, &gp), -1);

    ae_exit(h);
}

static void test_kind_dispatch(void)
{
    fwi_ae_core_ops_t *ops = NULL;
    fwi_ae_param_t param;
    void *h;

    h = ae_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    base_param(&param);

    CHECK_EQ(ops->ae_set_params(h, &param, NULL), 0);      /* INIT   */

    param.type = FWI_ISP_AE_UPDATE_AE_TABLE;
    CHECK_EQ(ops->ae_set_params(h, &param, NULL), 0);      /* TABLES */

    param.type = FWI_ISP_AE_SET_EXPOSURE_INDEX;
    param.ae_pline_index = 4;
    CHECK_EQ(ops->ae_set_params(h, &param, NULL), 0);      /* INDEX  */

    param.type = FWI_ISP_AE_BUILD_TOUCH_WEIGHT;
    CHECK_EQ(ops->ae_set_params(h, &param, NULL), 0);      /* TOUCH  */

    ae_exit(h);
}

static void test_flat_convergence(void)
{
    fwi_ae_core_ops_t *ops = NULL;
    fwi_ae_param_t param;
    fwi_ae_result_t res;
    void *h;
    int i;

    h = ae_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    base_param(&param);
    init_and_build(ops, h, &param);

    fill_flat_stats(50);   /* darker than the 128 target */

    for (i = 0; i < 30; i++) {
        set_frame(ops, h, i);
        memset(&res, 0, sizeof(res));
        CHECK_EQ(ops->ae_run(h, g_stats.ae_stats, &res), 0);
        CHECK(res.ae_status == FWI_AE_STATUS_IDLE ||
              res.ae_status == FWI_AE_STATUS_BUSY ||
              res.ae_status == FWI_AE_STATUS_DONE);
        CHECK(ev_sets(&res.sensor_set)->ev_index_max > 0);
        CHECK(ev_sets(&res.sensor_set)->ev_index_max <= 1024);   /* clean AE_IDX_MAX */
        if (i >= 3)   /* first compute happens on frame index 3 */
            CHECK(res.ae_gain >= 64 && res.ae_gain <= 640);
        if (i == 3) {
            CHECK_EQ(res.ae_average_luminance, 50);
            CHECK_EQ(res.ae_target, 128);
            CHECK_EQ(res.ae_weight_luminance, 50);
        }
    }

    ae_exit(h);
}

static void test_manual(void)
{
    fwi_ae_core_ops_t *ops = NULL;
    fwi_ae_param_t param;
    fwi_ae_result_t res;
    void *h;
    int i;

    h = ae_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    base_param(&param);
    param.ae_setting.exposure_mode = FWI_EXPOSURE_MANUAL;
    param.ae_setting.exposure_absolute = 10000;
    param.ae_setting.sensor_gain = 256;   /* Q4: 4096 after <<4 */
    param.test_cfg.gain = 512;
    init_and_build(ops, h, &param);

    for (i = 0; i < 8; i++) {
        set_frame(ops, h, i);
        memset(&res, 0, sizeof(res));
        CHECK_EQ(ops->ae_run(h, g_stats.ae_stats, &res), 0);
    }
    CHECK_EQ(ev_sets(&res.sensor_set)->ev_set.ev_exposure_time, 10000);
    /* Manual exposure with automatic ISO forces the base analog gain. */
    CHECK_EQ(ev_sets(&res.sensor_set)->ev_set.ev_analog_gain, 256);

    ae_exit(h);
}

static void test_wdr(void)
{
    fwi_ae_core_ops_t *ops = NULL;
    fwi_ae_param_t param;
    fwi_ae_result_t res;
    void *h;
    int i;

    h = ae_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    base_param(&param);
    param.ae_setting.ae_mode = FWI_AE_WDR;
    init_and_build(ops, h, &param);

    fill_wdr_stats();
    for (i = 0; i < 300; i++) {
        set_frame(ops, h, i);
        memset(&res, 0, sizeof(res));
        CHECK_EQ(ops->ae_run(h, g_stats.ae_stats, &res), 0);
    }

    CHECK(res.ae_wdr_ratio.tmp >= 160 && res.ae_wdr_ratio.tmp <= 304);
    CHECK(res.hist_low >= 100 && res.hist_low <= 10000);
    CHECK(res.hist_mid >= 100 && res.hist_mid <= 10000);
    CHECK(res.hist_hi >= 100 && res.hist_hi <= 10000);
    CHECK_EQ(res.wdr_hi_threshold, (16 << 8));
    CHECK_EQ(res.wdr_low_threshold, (8 << 8));
    CHECK(ev_sets(&res.sensor_set_short)->ev_set.ev_exposure_time <=
          ev_sets(&res.sensor_set)->ev_set.ev_exposure_time);

    ae_exit(h);
}

static void test_null_stats(void)
{
    fwi_ae_core_ops_t *ops = NULL;
    fwi_ae_param_t param;
    fwi_ae_result_t res;
    fwi_ae_stats_desc_t empty;
    void *h;

    h = ae_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    base_param(&param);
    CHECK_EQ(ops->ae_set_params(h, &param, NULL), 0);

    /* Null SDK stats handle -> clean default result. */
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->ae_run(h, NULL, &res), -1);
    CHECK_EQ(ev_sets(&res.sensor_set)->ev_set_curr.ev_analog_gain, 256);
    CHECK_EQ(ev_sets(&res.sensor_set)->ev_set_curr.ev_digital_gain, 1024);

    /* Null stats data (the fwi vtable passes the data pointer itself). */
    empty.ae_stats = NULL;
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->ae_run(h, empty.ae_stats, &res), -1);
    CHECK_EQ(ev_sets(&res.sensor_set)->ev_set_curr.ev_analog_gain, 256);

    ae_exit(h);
}

int main(void)
{
    /* Table provider: NULL selects the built-in placeholder defaults. */
    ae_shim_set_tables(NULL);

    test_lifecycle();
    test_get_set_mirror();
    test_kind_dispatch();
    test_flat_convergence();
    test_manual();
    test_wdr();
    test_null_stats();

    if (fails) {
        printf("FAILED: %d check(s)\n", fails);
        return 1;
    }
    printf("ok: AE shim\n");
    return 0;
}
