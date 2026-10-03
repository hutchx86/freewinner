/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_awb_shim.c - host test for the AWB shim: init/get/set/run on synthetic
 * data; checks the offset-0 live mirror and clean -> SDK result translation. */
#include <stdio.h>
#include <string.h>

#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"
#include "awb_shim.h"

/* Declared here, not in awb_shim.h: TUs that also see awb_clean.h have a
 * clean-core awb_init of a different type. */
void *awb_init(fwi_awb_core_ops_t **core_ops);
void  awb_exit(void *core_obj);

/* musl libm references the ARM EH personality routines; this test never
 * unwinds, so weak stubs close the static link. */
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

/* Synthetic hardware statistics: 32x32 averages plus a pixel count. */
static struct fwi_awb_stats g_sdk_stats;
static fwi_awb_stats_desc_t g_stats;

static void fill_scene(unsigned int R, unsigned int G, unsigned int B,
                       unsigned int npix)
{
    int r, c;

    for (r = 0; r < ISP_AWB_ROW; r++) {
        for (c = 0; c < ISP_AWB_COL; c++) {
            g_sdk_stats.awb_average_r[r][c] = R;
            g_sdk_stats.awb_average_g[r][c] = G;
            g_sdk_stats.awb_average_b[r][c] = B;
            g_sdk_stats.awb_sum_count[r][c] = npix;
        }
    }
    g_stats.awb_stats = &g_sdk_stats;
}

/* One reference record: ten configured integers (clean spec 3.3). */
static void put_ref(int32_t *dst, const int32_t ref[3], int32_t tol,
                    int32_t temp, int32_t prob_ls, int32_t prob_pref)
{
    memset(dst, 0, 10 * sizeof(int32_t));
    dst[0] = ref[0]; dst[1] = ref[1]; dst[2] = ref[2];
    dst[3] = 256;    dst[4] = 256;    dst[5] = 256;
    dst[6] = tol;
    dst[7] = temp;
    dst[8] = prob_ls;
    dst[9] = prob_pref;
}

static void param_defaults(fwi_awb_param_t *p)
{
    static const int32_t warm[3] = {300, 280, 180};
    static const int32_t mid[3]  = {280, 280, 220};
    static const int32_t cool[3] = {250, 280, 280};

    memset(p, 0, sizeof(*p));
    p->type = FWI_ISP_AWB_INIT_DATA;
    p->platform_id = 1;
    p->awb_frame_id = 0;

    p->awb_ctrl.wb_mode = FWI_WB_AUTO;
    p->awb_ctrl.wb_temperature = 0;
    p->awb_ctrl.white_balance_lock = 0;
    p->awb_ctrl.wb_gain_manual.r_gain = 256;
    p->awb_ctrl.wb_gain_manual.gr_gain = 256;
    p->awb_ctrl.wb_gain_manual.gb_gain = 256;
    p->awb_ctrl.wb_gain_manual.b_gain = 256;

    p->awb_init.awb_period_frames = 1;
    p->awb_init.awb_step_speed = 10;
    p->awb_init.awb_base_temper = 6500;
    p->awb_init.awb_green_zone_distance = 1;
    p->awb_init.awb_blue_sky_distance = 1;
    p->awb_init.awb_illum_count = 3;
    p->awb_init.awb_skin_count = 0;
    p->awb_init.awb_special_count = 0;

    p->awb_sensor_info.ae_level = 800;
    p->awb_sensor_info.ae_tbl_index = 5;
    p->awb_sensor_info.ae_tbl_index_max = 100;
    p->awb_sensor_info.is_ae_done = 1;

    p->test_cfg.test_mode = 0;
    p->test_cfg.colour_temp = 0;
    p->test_cfg.awb_en = 1;

    put_ref(&p->awb_init.awb_illum[0 * 10], warm, 1, 2800, 200, 100);
    put_ref(&p->awb_init.awb_illum[1 * 10], mid,  1, 4000, 200, 100);
    put_ref(&p->awb_init.awb_illum[2 * 10], cool, 1, 6000, 200, 100);
}

static void test_lifecycle(void)
{
    fwi_awb_core_ops_t *ops = NULL;
    void *h;

    h = awb_init(&ops);
    CHECK(h != NULL);
    CHECK(ops != NULL);
    CHECK(ops->awb_set_params != NULL);
    CHECK(ops->awb_get_params != NULL);
    CHECK(ops->awb_run != NULL);
    awb_exit(h);
    awb_exit(NULL);

    /* A NULL ops out-pointer is legal: the entity is still returned. */
    h = awb_init(NULL);
    CHECK(h != NULL);
    awb_exit(h);
}

static void test_get_set_mirror(void)
{
    fwi_awb_core_ops_t *ops = NULL;
    fwi_awb_param_t param;
    fwi_awb_param_t *gp = NULL;
    void *h;

    h = awb_init(&ops);
    if (!h) {
        printf("FAIL: awb_init returned NULL\n");
        fails++;
        return;
    }

    /* get_params hands back the embedded mirror at entity offset 0. */
    CHECK_EQ(ops->awb_get_params(h, &gp), 0);
    CHECK(gp != NULL);
    CHECK((void *)gp == h);
    CHECK_EQ(gp->awb_frame_id, 0);

    /* set_params copies the framework block into the mirror. */
    param_defaults(&param);
    param.awb_frame_id = 7;
    CHECK_EQ(ops->awb_set_params(h, &param, NULL), 0);
    CHECK_EQ(ops->awb_get_params(h, &gp), 0);
    CHECK_EQ(gp->awb_frame_id, 7);
    CHECK_EQ(gp->awb_init.awb_illum_count, 3);
    CHECK_EQ(gp->awb_sensor_info.ae_level, 800);

    /* The framework writes the mirror in place; the shim must expose that
     * same live storage, not a copy. */
    gp->awb_frame_id = 11;
    {
        fwi_awb_param_t *gp2 = NULL;
        CHECK_EQ(ops->awb_get_params(h, &gp2), 0);
        CHECK(gp2 == gp);
        CHECK_EQ(gp2->awb_frame_id, 11);
    }

    /* argument guards */
    CHECK_EQ(ops->awb_set_params(h, NULL, NULL), -1);
    CHECK_EQ(ops->awb_get_params(h, NULL), -1);
    CHECK_EQ(ops->awb_get_params(NULL, &gp), -1);
    CHECK_EQ(ops->awb_set_params(NULL, &param, NULL), -1);

    awb_exit(h);
}

static void test_fixed_temperature(void)
{
    fwi_awb_core_ops_t *ops = NULL;
    fwi_awb_param_t param;
    fwi_awb_result_t res;
    void *h;

    h = awb_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    param_defaults(&param);
    param.awb_frame_id = 5;
    param.test_cfg.awb_en = 0;      /* adaptive control disabled */
    param.test_cfg.colour_temp = 0;   /* 0 means 6500 K */

    CHECK_EQ(ops->awb_set_params(h, &param, NULL), 0);

    fill_scene(200, 200, 200, 100);
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->awb_run(h, g_stats.awb_stats, &res), 0);
    CHECK_EQ(res.colour_temp_output, 6500);
    CHECK_EQ(res.wb_gain_output.gr_gain, 256);
    CHECK_EQ(res.wb_gain_output.gb_gain, 256);
    CHECK(res.wb_gain_output.r_gain > 0);
    CHECK(res.wb_gain_output.b_gain > 0);

    /* An explicit fixed temperature is reported verbatim. */
    param.test_cfg.colour_temp = 5000;
    CHECK_EQ(ops->awb_set_params(h, &param, NULL), 0);
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->awb_run(h, g_stats.awb_stats, &res), 0);
    CHECK_EQ(res.colour_temp_output, 5000);

    awb_exit(h);
}

static void test_manual_and_mirror_write(void)
{
    fwi_awb_core_ops_t *ops = NULL;
    fwi_awb_param_t param;
    fwi_awb_param_t *gp = NULL;
    fwi_awb_result_t res;
    void *h;

    h = awb_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    param_defaults(&param);
    param.awb_ctrl.wb_mode = FWI_WB_MANUAL;
    CHECK_EQ(ops->awb_set_params(h, &param, NULL), 0);

    /* Drive the run purely through a framework write into the mirror. */
    CHECK_EQ(ops->awb_get_params(h, &gp), 0);
    gp->awb_frame_id = 5;
    gp->awb_ctrl.wb_gain_manual.r_gain = 300;
    gp->awb_ctrl.wb_gain_manual.gr_gain = 256;
    gp->awb_ctrl.wb_gain_manual.gb_gain = 256;
    gp->awb_ctrl.wb_gain_manual.b_gain = 400;

    fill_scene(128, 128, 128, 100);
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->awb_run(h, g_stats.awb_stats, &res), 0);
    CHECK_EQ(res.wb_gain_output.r_gain, 300);
    CHECK_EQ(res.wb_gain_output.gr_gain, 256);
    CHECK_EQ(res.wb_gain_output.gb_gain, 256);
    CHECK_EQ(res.wb_gain_output.b_gain, 400);

    awb_exit(h);
}

static void test_adaptive_translation(void)
{
    fwi_awb_core_ops_t *ops = NULL;
    fwi_awb_param_t param;
    fwi_awb_param_t *gp = NULL;
    fwi_awb_result_t res;
    void *h;
    int fi;

    h = awb_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    param_defaults(&param);
    CHECK_EQ(ops->awb_set_params(h, &param, NULL), 0);
    CHECK_EQ(ops->awb_get_params(h, &gp), 0);

    fill_scene(200, 200, 157, 100);   /* matches the mid illuminant */

    /* The framework advances the frame counter through the mirror; run must
     * see it. */
    for (fi = 3; fi < 63; fi++) {
        gp->awb_frame_id = fi;
        memset(&res, 0, sizeof(res));
        CHECK_EQ(ops->awb_run(h, g_stats.awb_stats, &res), 0);
    }

    CHECK(res.wb_gain_output.r_gain >= 32 &&
          res.wb_gain_output.r_gain <= 2048);
    CHECK(res.wb_gain_output.gr_gain >= 32 &&
          res.wb_gain_output.gr_gain <= 2048);
    CHECK(res.wb_gain_output.gb_gain >= 32 &&
          res.wb_gain_output.gb_gain <= 2048);
    CHECK(res.wb_gain_output.b_gain >= 32 &&
          res.wb_gain_output.b_gain <= 2048);
    CHECK(res.colour_temp_output >= 3000 && res.colour_temp_output <= 5000);

    awb_exit(h);
}

static void test_null_stats(void)
{
    fwi_awb_core_ops_t *ops = NULL;
    fwi_awb_param_t param;
    fwi_awb_result_t res;
    fwi_awb_stats_desc_t empty;
    void *h;

    h = awb_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    param_defaults(&param);
    param.awb_frame_id = 5;
    CHECK_EQ(ops->awb_set_params(h, &param, NULL), 0);

    /* Null stats -> "no statistics": -1, and the installed safe gains (not
     * unity) are written because the supplied gains are 0. */
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->awb_run(h, NULL, &res), -1);
    CHECK_EQ(res.wb_gain_output.r_gain, 0x0180);
    CHECK_EQ(res.wb_gain_output.gr_gain, 0x0100);
    CHECK_EQ(res.wb_gain_output.gb_gain, 0x0100);
    CHECK_EQ(res.wb_gain_output.b_gain, 0x0140);
    CHECK_EQ(res.colour_temp_output, 6500);

    /* Null inner stats pointer is defended the same way. */
    empty.awb_stats = NULL;
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->awb_run(h, empty.awb_stats, &res), -1);
    CHECK_EQ(res.colour_temp_output, 6500);

    /* argument guards */
    CHECK_EQ(ops->awb_run(h, g_stats.awb_stats, NULL), -1);
    CHECK_EQ(ops->awb_run(NULL, g_stats.awb_stats, &res), -1);

    awb_exit(h);
}

int main(void)
{
    /* NULL selects the built-in tables; the no-statistics fallback gains are
     * test values (no vendor table carries them). */
    unsigned short safe_gain[4];

    safe_gain[0] = 0x0180;
    safe_gain[1] = 0x0100;
    safe_gain[2] = 0x0100;
    safe_gain[3] = 0x0140;
    awb_shim_set_safe_gain(safe_gain);
    awb_shim_set_tables(NULL);

    fill_scene(200, 200, 200, 100);

    test_lifecycle();
    test_get_set_mirror();
    test_fixed_temperature();
    test_manual_and_mirror_write();
    test_adaptive_translation();
    test_null_stats();

    if (fails) {
        printf("FAILED: %d check(s)\n", fails);
        return 1;
    }
    printf("ok: AWB shim\n");
    return 0;
}
