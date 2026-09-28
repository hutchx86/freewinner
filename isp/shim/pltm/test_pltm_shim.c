/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_pltm_shim.c - host test for the PLTM shim: init/get/set/run with the
 * built-in tables (strength_bank[r][k] = k, converge_bank[r][d] = d). */
#include <stdio.h>
#include <string.h>

#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"
#include "pltm_shim.h"

/* Declared here, not in pltm_shim.h: TUs that also see pltm_clean.h have a
 * clean-core pltm_init of a different type. */
void *pltm_init(fwi_pltm_core_ops_t **core_ops);
void  pltm_exit(void *core_obj);

/* musl libm references the ARM EH personality routines; this test never
 * unwinds, so weak stubs close the static link. */
#if defined(__arm__)
#define SHIM_WEAK __attribute__((weak))
SHIM_WEAK int __aeabi_unwind_cpp_pr0(void) { return 0; }
SHIM_WEAK int __aeabi_unwind_cpp_pr1(void) { return 0; }
SHIM_WEAK int __aeabi_unwind_cpp_pr2(void) { return 0; }
#endif

/* Result-table layout (clean spec 9.1); local because pltm_clean.h would
 * collide with the SDK pltm_* entry points. */
#define TONE_BASE  0x0100
#define GAIN_BASE  0x0200
#define GAIN_UNITY 0x0400

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

/* Source curve: at least 0x300 entries; identity ramp is enough. */
static uint16_t g_source[0x300];

/* Synthetic SDK parameter block and statistics block. */
static fwi_pltm_param_t          g_param;
static struct fwi_pltm_stats g_stats;
static fwi_pltm_stats_desc_t          g_stats_wrap;

static void zero_param(fwi_pltm_param_t *p)
{
    memset(p, 0, sizeof(*p));
    p->pltm_enable = 1;
    p->pltm_frame_id = 3;
    p->pltm_init.pltm_cfg[FWI_ISP_PLTM_TOLERANCE] = 5;
    p->pltm_init.pltm_cfg[FWI_ISP_PLTM_STEP] = 2;
    p->pltm_init.pltm_cfg[FWI_ISP_PLTM_INTERVAL_FRAME] = 1;
    p->sensor_info.sensor_width = 64;
    p->sensor_info.sensor_height = 48;
    p->sensor_info.wdr_mode = 0;
    p->sensor_info.is_ae_done = 1;
    p->sensor_info.backlight = 0;
    p->pltm_table = g_source;
    p->wdr_bit_offset = 0;
}

static void fill_stats(uint16_t min_after, uint16_t lst_value)
{
    int i;

    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.min_after_pltm = min_after;
    for (i = 0; i < ISP_PLTM_ROW * ISP_PLTM_COL; i++)
        g_stats.lst[i] = lst_value;
    g_stats_wrap.pltm_stats = &g_stats;
}

static void test_lifecycle(void)
{
    fwi_pltm_core_ops_t *ops = NULL;
    void *h;

    h = pltm_init(&ops);
    CHECK(h != NULL);
    CHECK(ops != NULL);
    CHECK(ops->pltm_set_params != NULL);
    CHECK(ops->pltm_get_params != NULL);
    CHECK(ops->pltm_run != NULL);
    pltm_exit(h);
    pltm_exit(NULL);
}

/* Manual/static path: config + all result scalars/tables are deterministic. */
static void test_get_set_run_manual(void)
{
    fwi_pltm_core_ops_t *ops = NULL;
    fwi_pltm_param_t *gp = NULL;
    fwi_pltm_result_t res;
    void *h;

    zero_param(&g_param);
    g_param.platform_id = 1;
    g_param.pltm_init.pltm_cfg[FWI_ISP_PLTM_MODE] = 2;              /* manual/static */
    g_param.pltm_init.pltm_cfg[FWI_ISP_PLTM_ORIGINAL_PICTURE_RATIO] = 0x40;
    g_param.pltm_init.pltm_cfg[FWI_ISP_PLTM_TR_ORDER] = 7;
    g_param.pltm_init.pltm_cfg[FWI_ISP_PLTM_LAST_ORDER_RATIO] = 3;
    g_param.pltm_init.pltm_cfg[FWI_ISP_PLTM_POW_TBL] = 16;          /* clip_cfg     */
    g_param.pltm_init.pltm_cfg[FWI_ISP_PLTM_F_TBL] = 0x1000;        /* gain_cfg     */
    g_param.pltm_init.pltm_cfg[FWI_ISP_PLTM_BLOCK_V_COUNT] = 0;
    g_param.pltm_init.pltm_cfg[FWI_ISP_PLTM_BLOCK_H_COUNT] = 0;
    g_param.pltm_init.pltm_dynamic_cfg[FWI_ISP_PLTM_DYNAMIC_AUTO_STRENGTH] = 192;
    g_param.pltm_init.pltm_dynamic_cfg[FWI_ISP_PLTM_DYNAMIC_MANUAL_STRENGTH] = 0x10;
    g_param.pltm_init.pltm_dynamic_cfg[FWI_ISP_PLTM_DYNAMIC_AE_COMP] = 0x2A;

    h = pltm_init(&ops);
    if (!h) {
        printf("FAIL: pltm_init returned NULL\n");
        fails++;
        return;
    }

    /* get_params hands back the embedded mirror at entity offset 0. */
    CHECK_EQ(ops->pltm_get_params(h, &gp), 0);
    CHECK(gp != NULL);
    CHECK((void *)gp == h);                 /* config is at entity offset 0 */
    CHECK_EQ(gp->pltm_frame_id, 0);

    /* set_params stores the mirror and drives the clean core. */
    CHECK_EQ(ops->pltm_set_params(h, &g_param, &res), 0);
    CHECK_EQ(ops->pltm_get_params(h, &gp), 0);
    CHECK(gp != NULL);
    CHECK_EQ(gp->pltm_frame_id, 3);
    CHECK_EQ(gp->platform_id, 1);
    CHECK_EQ(gp->pltm_init.pltm_cfg[FWI_ISP_PLTM_MODE], 2);

    /* run translates the manual/static clean result. */
    fill_stats(0, 0);
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->pltm_run(h, g_stats_wrap.pltm_stats, &res), 0);

    CHECK_EQ(res.pltm_original_picture_ratio, 0x40);
    CHECK_EQ(res.pltm_tr_order, 7);
    CHECK_EQ(res.pltm_last_order_ratio, 3);
    CHECK_EQ(res.pltm_cal_en, 1);
    CHECK_EQ(res.pltm_frame_smoothing_en, 1);
    CHECK_EQ(res.pltm_ae_comp, 0x2A);

    /* geometry: width/(0+1)-1, height/(0+1)-1, 2^32/(64*48). */
    CHECK_EQ(res.pltm_block_width, 63);
    CHECK_EQ(res.pltm_block_height, 47);
    CHECK_EQ(res.pltm_statistic_div, (unsigned long)(0x100000000ULL / (64u * 48u)));

    /* tone table (dense): tbl[0]=0, tbl[1]=max(source,clip>>4)=0x101, pin. */
    CHECK_EQ(res.pltm_tbl[TONE_BASE], 0);
    CHECK_EQ(res.pltm_tbl[TONE_BASE + 1], 0x101);
    CHECK_EQ(res.pltm_tbl[TONE_BASE + 0xFF], 0xFFFF);

    /* gain table (dense): first 0xFFFF, unity blend, last 0x400. */
    CHECK_EQ(res.pltm_tbl[GAIN_BASE], 0xFFFF);
    CHECK_EQ(res.pltm_tbl[GAIN_BASE + 1], GAIN_UNITY);
    CHECK_EQ(res.pltm_tbl[GAIN_BASE + 0xFF], GAIN_UNITY);

    /* argument guards. */
    CHECK_EQ(ops->pltm_set_params(h, NULL, &res), -1);
    CHECK_EQ(ops->pltm_get_params(h, NULL), -1);
    CHECK_EQ(ops->pltm_run(h, NULL, &res), -1);
    CHECK_EQ(ops->pltm_run(h, g_stats_wrap.pltm_stats, NULL), -1);
    CHECK_EQ(ops->pltm_get_params(NULL, &gp), -1);

    pltm_exit(h);
}

/* Disabled path: enable=0 clears the strength and flags. */
static void test_disabled(void)
{
    fwi_pltm_core_ops_t *ops = NULL;
    fwi_pltm_result_t res;
    void *h;

    zero_param(&g_param);
    g_param.pltm_enable = 0;
    g_param.pltm_init.pltm_cfg[FWI_ISP_PLTM_MODE] = 2;
    g_param.pltm_init.pltm_dynamic_cfg[FWI_ISP_PLTM_DYNAMIC_AE_COMP] = 0x2A;

    h = pltm_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    CHECK_EQ(ops->pltm_set_params(h, &g_param, &res), 0);
    fill_stats(0, 0);
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->pltm_run(h, g_stats_wrap.pltm_stats, &res), 0);
    CHECK_EQ(res.pltm_cal_en, 0);
    CHECK_EQ(res.pltm_frame_smoothing_en, 0);
    CHECK_EQ(res.pltm_ae_comp, 0);
    CHECK_EQ(res.pltm_next_strength, 0);
    pltm_exit(h);
}

/* Auto path: lst 0x10 -> cal_strength 32, min_threshold 3 + measured_min 15 ->
 * step 32 on the 2nd frame; lst 0x20 doubles it, measured_min 48 holds at 0. */

/* Seeds the result's carried old/next strength; the shim must inject them
 * into the clean core, so a nonzero seed changes the first step. */
static void run_auto_seeded(uint16_t measured_min, uint16_t lst, int frames,
                            uint16_t seed_old, uint16_t seed_next,
                            fwi_pltm_result_t *last)
{
    fwi_pltm_core_ops_t *ops = NULL;
    fwi_pltm_result_t res;
    void *h;
    int i;

    zero_param(&g_param);
    g_param.pltm_enable = 1;
    g_param.pltm_frame_id = 3;
    g_param.pltm_init.pltm_cfg[FWI_ISP_PLTM_MODE] = 0;
    g_param.pltm_init.pltm_cfg[FWI_ISP_PLTM_BLOCK_V_COUNT] = 0;
    g_param.pltm_init.pltm_cfg[FWI_ISP_PLTM_BLOCK_H_COUNT] = 0;
    g_param.pltm_init.pltm_dynamic_cfg[FWI_ISP_PLTM_DYNAMIC_AUTO_STRENGTH] = 64;
    g_param.pltm_init.pltm_dynamic_cfg[FWI_ISP_PLTM_DYNAMIC_MIN_THRESHOLD] = 3;

    h = pltm_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    CHECK_EQ(ops->pltm_set_params(h, &g_param, &res), 0);
    fill_stats(measured_min, lst);
    memset(&res, 0, sizeof(res));
    res.pltm_old_strength = seed_old;
    res.pltm_next_strength = seed_next;
    for (i = 0; i < frames; i++)
        CHECK_EQ(ops->pltm_run(h, g_stats_wrap.pltm_stats, &res), 0);
    *last = res;
    pltm_exit(h);
}

static void run_auto(uint16_t measured_min, uint16_t lst, int frames,
                     fwi_pltm_result_t *last)
{
    run_auto_seeded(measured_min, lst, frames, 0, 0, last);
}

/* Seed old=0 holds at 0 while old=0x400 steps down by 1024-2 on the first
 * frame; without the shim's seed both runs would match. */
static void test_seeded_strength(void)
{
    fwi_pltm_result_t a, b;

    run_auto_seeded(15, 0x10, 1, 0x000, 0x000, &a);
    run_auto_seeded(15, 0x10, 1, 0x400, 0x400, &b);
    CHECK_EQ(a.pltm_next_strength, 0);
    CHECK_EQ(b.pltm_next_strength, 1022);
}

static void test_auto_stats(void)
{
    fwi_pltm_result_t res;

    /* measured 15, list 0x10 -> cal 32, released on frame 2 -> step 32. */
    run_auto(15, 0x10, 2, &res);
    CHECK_EQ(res.pltm_cal_strength, 32);
    CHECK_EQ(res.pltm_min_threshold, 48);
    CHECK_EQ(res.pltm_next_strength, 32);
    CHECK_EQ(res.pltm_old_strength, 32);
    CHECK_EQ(res.pltm_cal_en, 1);
    CHECK_EQ(res.pltm_frame_smoothing_en, 1);
    /* The built-in tables inject no presets, so the neutral row applies:
     * full original blend, order 5, last 15. */
    CHECK_EQ(res.pltm_original_picture_ratio, 0xFF);
    CHECK_EQ(res.pltm_tr_order, 5);
    CHECK_EQ(res.pltm_last_order_ratio, 15);

    /* list 0x20 -> cal 64 -> adaptive step 64. */
    run_auto(15, 0x20, 2, &res);
    CHECK_EQ(res.pltm_cal_strength, 64);
    CHECK_EQ(res.pltm_next_strength, 64);

    /* measured 48 == target -> deadband holds the strength at 0. */
    run_auto(48, 0x10, 2, &res);
    CHECK_EQ(res.pltm_cal_strength, 32);
    CHECK_EQ(res.pltm_next_strength, 0);

    /* first qualifying frame is suppressed (min_change_cnt < 2). */
    run_auto(15, 0x10, 1, &res);
    CHECK_EQ(res.pltm_next_strength, 0);
}

int main(void)
{
    int i;

    /* Table provider: NULL selects the built-in defaults. */
    pltm_shim_set_tables(NULL);

    for (i = 0; i < 0x300; i++)
        g_source[i] = (uint16_t)i;

    test_lifecycle();
    test_get_set_run_manual();
    test_disabled();
    test_auto_stats();
    test_seeded_strength();

    if (fails) {
        printf("FAILED: %d check(s)\n", fails);
        return 1;
    }
    printf("ok: PLTM pilot shim\n");
    return 0;
}
