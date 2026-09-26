/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * test_gtm_shim.c - host test for the GTM shim.
 *
 * Drives the shim through the SDK isp_gtm_core_ops_t contract with synthetic
 * gtm_param_t / gtm_stats_t objects, and checks the translation against the
 * clean-room core driven directly with equivalent gtm_clean_* inputs.
 */
#include <stdio.h>
#include <string.h>

/*
 * Clean-room side: rename its entry points for this TU so both the clean types
 * and the SDK ABI header can be included.  The clean spellings are the ones
 * compiled into gtm_clean.o (see the Makefile).
 */
#define gtm_init       clean_gtm_init
#define gtm_exit       clean_gtm_exit
#define gtm_get_params clean_gtm_get_params
#define gtm_set_params clean_gtm_set_params
#define gtm_run        clean_gtm_run
#include "gtm_clean.h"
#undef gtm_init
#undef gtm_exit
#undef gtm_get_params
#undef gtm_set_params
#undef gtm_run

#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"
#include "gtm_shim.h"

/* The shim's exported 3A entry points (fwi vtable shape; the framework
 * declares them in framework_isp.h).  Not in gtm_shim.h: TUs that also see
 * gtm_clean.h have a clean-core gtm_init of a different type. */
void *gtm_init(fwi_gtm_core_ops_t **core_ops);
void  gtm_exit(void *core_obj);

/*
 * musl's math objects (pulled in by the clean core) reference the ARM EH
 * personality routines.  This test never unwinds; weak definitions close the
 * static link under the OpenWrt toolchain.
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

/* ------------------------------------------------------------------ */
/* Synthetic injected tables (placeholders, not tuning data).          */
/* ------------------------------------------------------------------ */

static int16_t  g_guide_linear[GTM_NCURVE];
static int16_t  g_guide_low[GTM_NCURVE];
static int16_t  g_guide_high[GTM_NCURVE];
static int16_t  g_pregamma[GTM_PRE_ROWS * GTM_PRE_COLS];
static int32_t  g_eq_kernel[GTM_EQ_ROWS * GTM_EQ_TAPS];
static uint8_t  g_converge[GTM_CONV_ROWS * GTM_CONV_COLS];
static gtm_clean_tables_t g_tables;

/* ------------------------------------------------------------------ */
/* Shared constants / buffers.                                         */
/* ------------------------------------------------------------------ */

static unsigned short g_gamma[GTM_NGAMMA];
static unsigned short g_curve[GTM_NCURVE];
static unsigned short g_curve_last[GTM_NCURVE];
static struct fwi_gtm_stats g_stats;

static void init_tables(void)
{
    int r, c, d;

    for (r = 0; r < GTM_NCURVE; r++) {
        int lo = r * 24;
        g_guide_linear[r] = (int16_t)(r * 16);
        g_guide_low[r] = (int16_t)(lo > 4096 ? 4096 : lo);
        g_guide_high[r] = (int16_t)(r * 8);
    }
    for (r = 0; r < GTM_PRE_ROWS; r++)
        for (c = 0; c < GTM_PRE_COLS; c++)
            g_pregamma[r * GTM_PRE_COLS + c] = (int16_t)c;
    for (r = 0; r < GTM_EQ_ROWS; r++)
        for (c = 0; c < GTM_EQ_TAPS; c++)
            g_eq_kernel[r * GTM_EQ_TAPS + c] = 33;
    for (r = 0; r < GTM_CONV_ROWS; r++)
        for (d = 0; d < GTM_CONV_COLS; d++)
            g_converge[r * GTM_CONV_COLS + d] =
                (uint8_t)((r == 26) ? d : 0);

    g_tables.guide_linear = g_guide_linear;
    g_tables.guide_low = g_guide_low;
    g_tables.guide_high = g_guide_high;
    g_tables.pre_gamma = g_pregamma;
    g_tables.eq_kernel = g_eq_kernel;
    g_tables.converge = g_converge;
}

static void reset_curve_buffers(void)
{
    int i;
    for (i = 0; i < GTM_NCURVE; i++) {
        g_curve[i] = 0x200;
        g_curve_last[i] = 0x200;
    }
    for (i = 0; i < GTM_NGAMMA; i++)
        g_gamma[i] = (unsigned short)(i * 4);
}

static void fill_stats(int shape)
{
    int i;

    memset(&g_stats, 0, sizeof(g_stats));
    for (i = 0; i < GTM_NWIN; i++)
        g_stats.average[i] = (uint32_t)((i * 7) % 256);
    switch (shape) {
    case 0: /* flat */
        for (i = 0; i < GTM_NCURVE; i++)
            g_stats.hist[i] = 100;
        break;
    case 1: /* shadow-concentrated */
        g_stats.hist[10] = 100000;
        break;
    case 2: /* highlight-concentrated */
        g_stats.hist[250] = 100000;
        break;
    case 3: /* gradient */
        for (i = 0; i < GTM_NCURVE; i++)
            g_stats.hist[i] = (uint32_t)(i + 1);
        break;
    default: /* all-zero */
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Equivalent SDK and clean parameter blocks.                          */
/* ------------------------------------------------------------------ */

static void fill_sdk(fwi_gtm_param_t *sp, int mode, int gamma_mode, int frame_id,
                     int enable)
{
    int r, c;

    memset(sp, 0, sizeof(*sp));
    sp->type = FWI_ISP_GTM_INIT_DATA;
    sp->platform_id = 1;
    sp->gtm_frame_id = frame_id;
    sp->gtm_enable = enable ? 1 : 0;
    sp->gtm_init.gtm_type = (fwi_gtm_type_e)mode;
    sp->gtm_init.gamma_type = (fwi_gamma_type_e)gamma_mode;
    sp->gtm_init.hist_pixel_count = 1;
    sp->gtm_init.bright_minval = 4;
    sp->gtm_init.dark_minval = 4;
    for (r = 0; r < GTM_NPEAK; r++)
        for (c = 0; c < GTM_NPEAK; c++)
            sp->gtm_init.plum_var[r][c] = 0x80;
    sp->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_GAIN] = 0x500;
    sp->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_EQ_RATIO] = 40;
    sp->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_EQ_SMOOTH] = 10;
    sp->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_BLACK] = 16;
    sp->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_WHITE] = 240;
    sp->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_BLACK_ALPHA] = 1;  /* clean white_slope */
    sp->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_WHITE_ALPHA] = 1;  /* clean black_slope */
    sp->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_GAMMA_IND] = 0;    /* pre_gamma_offset  */
    sp->gtm_init.gtm_cfg[FWI_GTM_HIST_EQ_GAMMA_PLUS] = 0;
    sp->contrast = 0;
    sp->brightness = 0;
    sp->gtm_bit_offset = 0;
    sp->wdr_en = false;
    sp->gamma_tbl = g_gamma;
    sp->drc_table = g_curve;
    sp->drc_table_last = g_curve_last;
}

static void fill_clean(gtm_clean_params_t *cp, int mode, int gamma_mode,
                       int frame_id)
{
    int r, c;

    memset(cp, 0, sizeof(*cp));
    cp->mode = mode;
    cp->gamma_mode = gamma_mode;
    cp->frame_index = frame_id;
    cp->enable = 1;
    cp->range_max = 0x500;
    cp->eq_gain = 40;
    cp->cover_bias = 10;
    cp->black_level = 16;
    cp->white_level = 240;
    cp->white_slope = 1;
    cp->black_slope = 1;
    cp->pre_gamma_offset = 0;
    cp->hist_pixel_count = 1;
    cp->dark_floor = 4;
    cp->bright_floor = 4;
    for (r = 0; r < GTM_NPEAK; r++)
        for (c = 0; c < GTM_NPEAK; c++)
            cp->peak_map[r][c] = 0x80;
    cp->brightness = 0;
    cp->contrast = 0;
    cp->bit_offset = 0;
    cp->wdr_en = 0;
    memcpy(cp->gamma_lut, g_gamma, sizeof(cp->gamma_lut));
    memcpy(cp->curve, g_curve, sizeof(cp->curve));
    memcpy(cp->curve_prev, g_curve_last, sizeof(cp->curve_prev));
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_lifecycle(void)
{
    fwi_gtm_core_ops_t *ops = NULL;
    void *h;

    h = gtm_init(&ops);
    CHECK(h != NULL);
    CHECK(ops != NULL);
    CHECK(ops->gtm_set_params != NULL);
    CHECK(ops->gtm_get_params != NULL);
    CHECK(ops->gtm_run != NULL);
    gtm_exit(h);
    gtm_exit(NULL);
}

static void test_init_requires_tables(void)
{
    fwi_gtm_core_ops_t *ops = NULL;
    gtm_clean_tables_t saved = g_tables;
    void *h;

    g_tables.guide_linear = NULL;
    gtm_shim_set_tables(&g_tables);
    h = gtm_init(&ops);
    CHECK(h == NULL);

    g_tables = saved;
    gtm_shim_set_tables(&g_tables);
    h = gtm_init(&ops);
    CHECK(h != NULL);
    gtm_exit(h);
}

static void test_get_set_mirror(void)
{
    fwi_gtm_core_ops_t *ops = NULL;
    fwi_gtm_param_t param;
    fwi_gtm_param_t *gp = NULL;
    fwi_gtm_result_t res;
    void *h;

    fill_sdk(&param, FWI_ISP_GTM_FIXED, FWI_ISP_GTM_GAMMA_DYNAMIC, 7, 1);
    h = gtm_init(&ops);
    if (!h) {
        printf("FAIL: gtm_init returned NULL\n");
        fails++;
        return;
    }

    /* get_params hands back the embedded mirror at entity offset 0. */
    CHECK_EQ(ops->gtm_get_params(h, &gp), 0);
    CHECK(gp != NULL);
    CHECK((void *)gp == h);
    CHECK_EQ(gp->gtm_frame_id, 0);

    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->gtm_set_params(h, &param, &res), 0);
    CHECK_EQ(ops->gtm_get_params(h, &gp), 0);
    CHECK((void *)gp == h);
    CHECK_EQ(gp->gtm_frame_id, 7);
    CHECK_EQ(gp->platform_id, 1);
    CHECK_EQ((int)gp->gtm_init.gtm_type, FWI_ISP_GTM_FIXED);
    CHECK_EQ(gp->contrast, 0);

    /* SDK buffers stay the framework's, and are seeded 0x200. */
    CHECK(gp->drc_table == g_curve);
    CHECK(gp->drc_table_last == g_curve_last);
    CHECK(gp->gamma_tbl == g_gamma);

    /* argument guards */
    CHECK_EQ(ops->gtm_set_params(h, NULL, &res), -1);
    CHECK_EQ(ops->gtm_get_params(h, NULL), -1);
    CHECK_EQ(ops->gtm_get_params(NULL, &gp), -1);
    CHECK_EQ(ops->gtm_set_params(NULL, &param, &res), -1);

    gtm_exit(h);
}

static void test_null_run_guards(void)
{
    fwi_gtm_core_ops_t *ops = NULL;
    fwi_gtm_param_t param;
    fwi_gtm_stats_desc_t ss;
    fwi_gtm_result_t res;
    void *h;

    fill_sdk(&param, FWI_ISP_GTM_FIXED, FWI_ISP_GTM_GAMMA_DYNAMIC, 7, 1);
    ss.gtm_stats = &g_stats;
    h = gtm_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    CHECK_EQ(ops->gtm_run(h, NULL, &res), -1);
    CHECK_EQ(ops->gtm_run(h, ss.gtm_stats, NULL), -1);
    CHECK_EQ(ops->gtm_run(NULL, ss.gtm_stats, &res), -1);
    gtm_exit(h);
}

static void check_run_case(const char *name, int mode, int gamma_mode, int shape)
{
    fwi_gtm_core_ops_t *ops = NULL;
    fwi_gtm_param_t param;
    fwi_gtm_result_t sres;
    fwi_gtm_stats_desc_t ss;
    gtm_ops_t cops;
    gtm_entity_t *ce;
    gtm_clean_params_t *cp = NULL;
    gtm_clean_param_req_t req;
    gtm_clean_stats_t cs;
    gtm_clean_result_t cres;
    unsigned short shim_curve[GTM_NCURVE];
    unsigned short shim_curve_last[GTM_NCURVE];
    void *h;
    int i, bad = 0;

    reset_curve_buffers();
    fill_stats(shape);
    ss.gtm_stats = &g_stats;

    /* --- shim path --- */
    fill_sdk(&param, mode, gamma_mode, 4, 1);
    h = gtm_init(&ops);
    if (!h || !ops) {
        printf("FAIL %s: gtm_init\n", name);
        fails++;
        return;
    }
    memset(&sres, 0, sizeof(sres));
    if (ops->gtm_set_params(h, &param, &sres) != 0 ||
        ops->gtm_run(h, ss.gtm_stats, &sres) != 0) {
        printf("FAIL %s: shim set/run\n", name);
        fails++;
        gtm_exit(h);
        return;
    }

    /* Snapshot the shim's SDK-buffer output, then restore the initial state
     * so the clean core runs the same one-shot transition from 0x200. */
    memcpy(shim_curve, g_curve, sizeof(shim_curve));
    memcpy(shim_curve_last, g_curve_last, sizeof(shim_curve_last));
    reset_curve_buffers();

    /* --- equivalent clean path --- */
    ce = clean_gtm_init(&cops);
    if (!ce) {
        printf("FAIL %s: clean init\n", name);
        fails++;
        gtm_exit(h);
        return;
    }
    clean_gtm_get_params(ce, &cp);
    fill_clean(cp, mode, gamma_mode, 4);
    req.kind = GTM_PARAM_INIT;
    req.params = NULL;
    clean_gtm_set_params(ce, &req, NULL);

    memset(&cs, 0, sizeof(cs));
    memcpy(cs.hist_raw, g_stats.hist, sizeof(cs.hist_raw));
    memcpy(cs.win_avg, g_stats.average, sizeof(cs.win_avg));
    memset(&cres, 0, sizeof(cres));
    if (clean_gtm_run(ce, &cs, &cres) != 0) {
        printf("FAIL %s: clean run\n", name);
        fails++;
    }

    CHECK_EQ(sres.hist_max_val, cres.peak_level);
    CHECK_EQ(sres.average_luminance, cres.avg_lum);
    CHECK_EQ(sres.average_var, cres.avg_var);
    CHECK_EQ(sres.hist_div, cres.div_index);
    CHECK_EQ(sres.hdr_req, cres.hdr_flag);
    if (sres.hratio_last != cres.ratio_hold) {
        printf("FAIL %s: hratio_last=%f expected %f\n", name,
               sres.hratio_last, cres.ratio_hold);
        fails++;
    }
    for (i = 0; i < GTM_NCURVE; i++) {
        if (shim_curve[i] != cres.curve[i] ||
            shim_curve_last[i] != cres.curve_prev[i]) {
            bad = 1;
            break;
        }
    }
    CHECK(!bad);
    for (i = 0; i < GTM_NCURVE; i++)
        if (shim_curve[i] > GTM_CURVE_MAX)
            bad = 2;
    CHECK_EQ(bad, 0);

    clean_gtm_exit(ce);
    gtm_exit(h);
}

static void test_run_translation(void)
{
    static const int shapes[] = { 0, 1, 2, 3 };
    int i;

    for (i = 0; i < (int)(sizeof(shapes) / sizeof(shapes[0])); i++) {
        check_run_case("fixed", FWI_ISP_GTM_FIXED, FWI_ISP_GTM_GAMMA_DYNAMIC, shapes[i]);
        check_run_case("dynamic-drc", FWI_ISP_GTM_DYNAMIC_DRC, FWI_ISP_GTM_GAMMA_FIXED,
                       shapes[i]);
        check_run_case("dynamic-gamma", FWI_ISP_GTM_DYNAMIC_GAMMA,
                       FWI_ISP_GTM_GAMMA_DYNAMIC, shapes[i]);
        check_run_case("luma-hold", FWI_ISP_GTM_KEEP_LUMINANCE_DRC, FWI_ISP_GTM_GAMMA_DYNAMIC,
                       shapes[i]);
    }
}

static void test_gating(void)
{
    fwi_gtm_core_ops_t *ops = NULL;
    fwi_gtm_param_t param;
    fwi_gtm_result_t res;
    fwi_gtm_stats_desc_t ss;
    void *h;

    reset_curve_buffers();
    fill_stats(3);
    ss.gtm_stats = &g_stats;

    /* frame_index == 2: clean does not run; result/curve untouched. */
    fill_sdk(&param, FWI_ISP_GTM_FIXED, FWI_ISP_GTM_GAMMA_DYNAMIC, 2, 1);
    h = gtm_init(&ops);
    if (!h) {
        fails++;
        return;
    }
    CHECK_EQ(ops->gtm_set_params(h, &param, &res), 0);
    g_curve[100] = 0x123;
    memset(&res, 0, sizeof(res));
    res.hdr_req = 0x1234;
    CHECK_EQ(ops->gtm_run(h, ss.gtm_stats, &res), 0);
    CHECK_EQ(res.hdr_req, 0x1234);
    CHECK_EQ(g_curve[100], 0x123);

    /* disabled: same gating. */
    param.gtm_frame_id = 7;
    param.gtm_enable = 0;
    CHECK_EQ(ops->gtm_set_params(h, &param, &res), 0);
    res.hdr_req = 0x1234;
    CHECK_EQ(ops->gtm_run(h, ss.gtm_stats, &res), 0);
    CHECK_EQ(res.hdr_req, 0x1234);

    /* enabled and past warmup: hdr_req set. */
    param.gtm_enable = 1;
    CHECK_EQ(ops->gtm_set_params(h, &param, &res), 0);
    res.hdr_req = 0;
    CHECK_EQ(ops->gtm_run(h, ss.gtm_stats, &res), 0);
    CHECK_EQ(res.hdr_req, 1);

    gtm_exit(h);
}

int main(void)
{
    init_tables();
    gtm_shim_set_tables(&g_tables);

    test_lifecycle();
    test_init_requires_tables();
    test_get_set_mirror();
    test_null_run_guards();
    test_run_translation();
    test_gating();

    if (fails) {
        printf("FAILED: %d check(s)\n", fails);
        return 1;
    }
    printf("ok: GTM shim\n");
    return 0;
}
