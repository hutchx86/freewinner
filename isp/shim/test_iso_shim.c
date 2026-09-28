/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_iso_shim.c - host test for the ISO shim: init/get/set/run with a
 * synthetic param block and context, checking the clean -> SDK writeback. */
#include <stdio.h>
#include <string.h>

#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"
#include "iso_shim.h"

/* Shim entry points (declared by framework_isp.h, not iso_shim.h: TUs seeing
 * iso_clean.h have a clean iso_init of another type). */
void *iso_init(fwi_iso_cfg_core_ops_t **core_ops);
void  iso_exit(void *core_obj);

/* musl atan/e_pow reference ARM EH personality routines; weak stubs close the
 * static link (this test never unwinds; a real libgcc_eh would override). */
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

/* Synthetic shared context: static, not stack (the struct is large). */
static struct fwi_isp_ctx g_gen;

static void fill_scene(fwi_iso_param_t *p)
{
    fwi_dynamic_cfg_t tmpl;
    fwi_tuning_by_iso_t *iso;
    int i;

    memset(&g_gen, 0, sizeof(g_gen));
    memset(p, 0, sizeof(*p));

    p->type = FWI_ISP_ISO_UPDATE_PARAMS;
    p->platform_id = 1;
    p->iso_frame_id = 7;
    p->colour_enhance_color2gray_threshold = 10;
    p->colour_enhance_color2gray_delta_min = 5;
    p->colour_enhance_color2gray_delta_max = 20;

    p->chroma_denoise_adjust = 1;
    p->sharpness_adjust = 1;
    p->saturation_adjust = 1;
    p->contrast_adjust = 1;
    p->brightness_adjust = 1;
    p->colour_enhance_ratio_adjust = 1;
    p->denoise_adjust = 1;
    p->sensor_offset_adjust = 1;
    p->black_level_adjust = 1;
    p->defect_pixel_adjust = 1;
    p->dehaze_value_adjust = 1;
    p->pltm_dynamic_cfg_adjust = 1;
    p->tdnr_adjust = 1;
    p->ae_cfg_adjust = 1;
    p->gtm_cfg_adjust = 1;
    p->lateral_ca_cfg_adjust = 1;
    p->af_cfg_adjust = 1;

    p->denoise_lp0_np_core_ratio = 0x40;
    p->denoise_lp1_np_core_ratio = 0x30;
    p->denoise_lp2_np_core_ratio = 0x20;
    p->denoise_lp3_np_core_ratio = 0x10;

    p->gen = &g_gen;

    /* --- synthetic isp_gen inputs --- */
    g_gen.sensor.total_gain = 100;      /* g = (100*100)>>8 = 39 */
    g_gen.sensor.ae_tbl_index = 50;
    g_gen.sensor.ae_tbl_index_max = 100;  /* lum_i = 349*50/100 = 174 */
    g_gen.ae_ctl.ae_mode = FWI_AE_NORM;

    g_gen.picture_ctl.contrast_level = 8;
    g_gen.picture_ctl.saturation_level = 4;
    g_gen.picture_ctl.sharpness_level = 64;
    g_gen.picture_ctl.brightness_level = 2;
    g_gen.picture_ctl.denoise_level = 32;
    g_gen.picture_ctl.pltmwdr_level = 16;
    g_gen.picture_ctl.denoise_3d_level = 16;
    g_gen.picture_ctl.highlight_level = 0;
    g_gen.picture_ctl.backlight_level = 0;

    g_gen.tuning.enables.denoise_3d_en = 1;
    g_gen.tuning.enables.local_contrast_en = 1;
    g_gen.tuning.enables.wb_gain_en = 1;
    g_gen.tuning.enables.saturation_en = 1;
    g_gen.tuning.enables.local_tone_en = 1;

    /* All triggers select the luminance-indexed record (value 0). */
    iso = &g_gen.tuning.by_iso;
    memset(&tmpl, 0, sizeof(tmpl));
    tmpl.saturation_cfg[6] = 16;   /* nonzero log curve denominator */
    tmpl.contrast = 4;
    tmpl.brightness = 2;
    tmpl.colour_denoise = 0x40;
    for (i = 0; i < 14; i++) {
        iso->gain_mapping_point[i] = 24 + 2 * i;
        iso->luminance_mapping_point[i] = 24 * (i + 1);
        iso->dynamic_cfg[i] = tmpl;
    }

    g_gen.awb_entity.awb_result.wb_gain_output.r_gain = 0x180;
    g_gen.awb_entity.awb_result.wb_gain_output.gb_gain = 0x100;
    g_gen.awb_entity.awb_result.wb_gain_output.b_gain = 0x1a0;
    g_gen.pltm_entity.pltm_result.pltm_next_strength = 0x80;
}

static void test_lifecycle(void)
{
    fwi_iso_cfg_core_ops_t *ops = NULL;
    void *h;

    h = iso_init(&ops);
    CHECK(h != NULL);
    CHECK(ops != NULL);
    CHECK(ops->iso_set_params != NULL);
    CHECK(ops->iso_get_params != NULL);
    CHECK(ops->iso_run != NULL);
    iso_exit(h);
    iso_exit(NULL);
}

static void test_get_set_run(void)
{
    fwi_iso_cfg_core_ops_t *ops = NULL;
    fwi_iso_param_t param;
    fwi_iso_param_t *gp = NULL;
    fwi_iso_result_t res;
    void *h;

    fill_scene(&param);
    h = iso_init(&ops);
    if (!h) {
        printf("FAIL: iso_init returned NULL\n");
        fails++;
        return;
    }

    /* get_params hands back the embedded mirror. */
    CHECK_EQ(ops->iso_get_params(h, &gp), 0);
    CHECK(gp != NULL);
    CHECK((void *)gp == h);            /* config is at entity offset 0 */
    CHECK_EQ(gp->iso_frame_id, 0);

    /* set_params stores the mirror and drives the clean core. */
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->iso_set_params(h, &param, &res), 0);
    CHECK_EQ(ops->iso_get_params(h, &gp), 0);
    CHECK(gp != NULL);
    CHECK_EQ(gp->iso_frame_id, 7);
    CHECK_EQ(gp->platform_id, 1);

    /* run translates index 39 / 174 (see fill_scene). */
    memset(&res, 0, sizeof(res));
    CHECK_EQ(ops->iso_run(h, &res), 0);
    CHECK_EQ(res.gain_index, 39);
    CHECK_EQ(res.luminance_index, 174);
    CHECK_EQ(res.gain_index, res.gain_index);   /* silence unused warnings */

    /* argument guards */
    CHECK_EQ(ops->iso_set_params(h, NULL, &res), -1);
    CHECK_EQ(ops->iso_get_params(h, NULL), -1);
    CHECK_EQ(ops->iso_run(h, NULL), -1);
    CHECK_EQ(ops->iso_get_params(NULL, &gp), -1);

    iso_exit(h);
}

/* Tuning corpus + full writeback into module_cfg. The CEM check pins a/b
 * orientation: cem_ratio == 0 selects cem_table1 (complement-weighted). */
static unsigned char g_tdnf_table[512];

static void test_writeback(void)
{
    fwi_iso_cfg_core_ops_t *ops = NULL;
    fwi_iso_param_t param;
    fwi_iso_result_t res;
    fwi_tuning_modules_t *t;
    void *h;
    int i;

    fill_scene(&param);

    t = &g_gen.tuning.modules;
    t->sharp_edge_luminance[0] = 0x123;
    t->sharp_high_frequency_luminance[0] = 0x456;
    t->sharp_s_map[0] = 0x7a;
    t->sharp_hsv[0] = 0x0abc;
    for (i = 0; i < 256; i++) {
        t->d3d_k3d_incre_curve[i] = (uint8_t)(i + 1);
        t->temporal_denoise_diff[i] = (uint8_t)(0xff - i);
    }
    for (i = 0; i < 33; i++) {
        t->sharp_val[i] = (uint16_t)(i + 10);
        t->sharp_luminance[i] = (uint16_t)(i + 20);
        t->bayer_denoise_threshold[i] = (uint16_t)(i + 30);
        t->temporal_denoise_threshold[i] = (uint16_t)(i + 40);
        t->temporal_denoise_ref_noise[i] = (uint16_t)(i + 50);
    }
    for (i = 0; i < 32; i++)
        t->temporal_denoise_k[i] = (uint8_t)(i + 60);
    t->colour_enhance_table[0] = 0x11;
    t->colour_enhance_table1[0] = 0x22;

    g_gen.hw_cfg.temporal_denoise_table = g_tdnf_table;
    memset(g_tdnf_table, 0, sizeof(g_tdnf_table));

    h = iso_init(&ops);
    if (!h) {
        fails++;
        return;
    }

    CHECK_EQ(ops->iso_set_params(h, &param, &res), 0);
    CHECK_EQ(ops->iso_run(h, &res), 0);

    /* tuning corpus -> clean ctx -> SDK module_cfg */
    CHECK_EQ(g_gen.hw_cfg.sharp_cfg.sharp_edge_luminance[0], 0x123);
    CHECK_EQ(g_gen.hw_cfg.sharp_cfg.sharp_high_frequency_luminance[0], 0x456);
    CHECK_EQ(g_gen.hw_cfg.sharp_cfg.sharp_s_map[0], 0x7a);
    CHECK_EQ(g_gen.hw_cfg.sharp_cfg.sharp_hsv[0], 0x0abc);
    CHECK_EQ(g_gen.hw_cfg.chroma_denoise_cfg.c_threshold, 0x40);
    CHECK_EQ(g_gen.hw_cfg.colour_enhance_cfg.colour_enhance_table[0], 0x22);
    CHECK_EQ(g_gen.hw_cfg.temporal_denoise_table != NULL, 1);
    CHECK_EQ(((unsigned char *)g_gen.hw_cfg.temporal_denoise_table)[0], 1);
    CHECK_EQ(g_gen.picture_ctl.sharpness_level, 64);

    iso_exit(h);
}

static void test_index_edges(void)
{
    fwi_iso_cfg_core_ops_t *ops = NULL;
    fwi_iso_param_t param;
    fwi_iso_result_t res;
    void *h;

    h = iso_init(&ops);
    if (!h) {
        fails++;
        return;
    }

    /* ae_pos_max == 0 forces lum_idx 0. */
    fill_scene(&param);
    g_gen.sensor.ae_tbl_index_max = 0;
    g_gen.sensor.ae_tbl_index = 999;
    CHECK_EQ(ops->iso_set_params(h, &param, &res), 0);
    CHECK_EQ(ops->iso_run(h, &res), 0);
    CHECK_EQ(res.luminance_index, 0);

    /* gain beyond the table snaps to the search sentinel 349. */
    fill_scene(&param);
    g_gen.sensor.total_gain = 2100000;   /* g > 0xc8000 */
    CHECK_EQ(ops->iso_set_params(h, &param, &res), 0);
    CHECK_EQ(ops->iso_run(h, &res), 0);
    CHECK_EQ(res.gain_index, 349);

    iso_exit(h);
}

int main(void)
{
    /* Table provider: NULL selects the built-in placeholder defaults. */
    iso_shim_set_tables(NULL);

    test_lifecycle();
    test_get_set_run();
    test_writeback();
    test_index_edges();

    if (fails) {
        printf("FAILED: %d check(s)\n", fails);
        return 1;
    }
    printf("ok: ISO pilot shim\n");
    return 0;
}
