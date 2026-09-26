/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * test_base_shim.c - host smoke test for the isp_base SDK-ABI shim.
 *
 * Exercises the shim's SDK entry points and the freeisp_get_tables() base
 * provider with a hand-built `struct isp_lib_context`.  The authoritative
 * behavioural verification is the private differential harness; this test
 * only proves the published shim builds and translates the common path
 * (provider wiring, band-step, gamma, digital gain, lens centre) without a
 * vendor object present.
 */
#include <stdio.h>
#include <string.h>

#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"

#undef ISP_LSC_TBL_SIZE
#undef ISP_MSC_TBL_LENGTH
#undef ISP_MSC_TBL_SIZE
#undef ISP_MSC_TEMP_NUM

/* Include the clean base tier for base_tables_t / freeisp_tables_t.  The same
 * SDK/clean tag collisions the shim handles are renamed here for this TU. */
#define isp_ae_param        clean_isp_ae_param
#define isp_ae_result       clean_isp_ae_result
#define isp_ae_settings     clean_isp_ae_settings
#define isp_ae_settings_t   clean_isp_ae_settings_t
#define isp_ae_stats        clean_isp_ae_stats
#define isp_af_param        clean_isp_af_param
#define isp_af_result       clean_isp_af_result
#define isp_af_stats        clean_isp_af_stats
#define isp_afs_param       clean_isp_afs_param
#define isp_afs_stats       clean_isp_afs_stats
#define isp_awb_stats       clean_isp_awb_stats
#define isp_gca_cfg         clean_isp_gca_cfg
#define isp_h3a_reg_win     clean_isp_h3a_reg_win
#define isp_lca_cfg         clean_isp_lca_cfg
#define isp_pltm_stats      clean_isp_pltm_stats
#define isp_sensor_info     clean_isp_sensor_info
#define isp_sensor_info_t   clean_isp_sensor_info_t
#define isp_sharp_cfg       clean_isp_sharp_cfg
#define config_band_step         clean_config_band_step
#define config_lens_center       clean_config_lens_center
#define config_dig_gain          clean_config_dig_gain
#define config_gamma             clean_config_gamma
#define config_wdr               clean_config_wdr
#define isp_apply_colormatrix    clean_isp_apply_colormatrix
#define __isp_stat_dynamic_judge clean___isp_stat_dynamic_judge
#define isp_handle_stats         clean_isp_handle_stats
#define isp_handle_stats_sync    clean_isp_handle_stats_sync
#define isp_apply_settings       clean_isp_apply_settings
#define config_lens_table        clean_config_lens_table
#define config_msc_table         clean_config_msc_table
#include "base.h"
#undef isp_ae_param
#undef isp_ae_result
#undef isp_ae_settings
#undef isp_ae_settings_t
#undef isp_ae_stats
#undef isp_af_param
#undef isp_af_result
#undef isp_af_stats
#undef isp_afs_param
#undef isp_afs_stats
#undef isp_awb_stats
#undef isp_gca_cfg
#undef isp_h3a_reg_win
#undef isp_lca_cfg
#undef isp_pltm_stats
#undef isp_sensor_info
#undef isp_sensor_info_t
#undef isp_sharp_cfg
#undef config_band_step
#undef config_lens_center
#undef config_dig_gain
#undef config_gamma
#undef config_wdr
#undef isp_apply_colormatrix
#undef __isp_stat_dynamic_judge
#undef isp_handle_stats
#undef isp_handle_stats_sync
#undef isp_apply_settings
#undef config_lens_table
#undef config_msc_table

#include "base_shim.h"

static int fails;

/* The shim's freeisp_ae_set_params forwards to the framework AE helper; the
 * host smoke test only needs that symbol to resolve. */
void isp_ae_set_params_helper(fwi_ae_entity_t *ae_ctx,
                              fwi_ae_param_type_e cmd_type)
{
    (void)ae_ctx;
    (void)cmd_type;
}

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

static fwi_isp_ctx_t g;
static uint16_t g_lin[768];
static uint16_t g_lens[1024];
static uint16_t g_msc[1936];

int main(void)
{
    int i;

    memset(&g, 0, sizeof(g));
    g.isp_index = 0;

    /* geometry */
    g.stats.pic_w = 1920;
    g.stats.pic_h = 1080;
    g.drv_stats_ref.pic_size.width = 1920;
    g.drv_stats_ref.pic_size.height = 1080;

    /* tuning */
    g.tuning.modules.lens_shading_center_x = 0x800;
    g.tuning.modules.lens_shading_center_y = 0x800;
    for (i = 0; i < ISP_GAMMA_TBL_LENGTH; i++)
        g.tuning.modules.gamma_tbl_init[0][i] = (uint16_t)i;
    g.tuning.modules.gamma_trig_cfg[0] = 1300;
    g.tuning.modules.gamma_trig_cfg[1] = 1100;
    g.tuning.modules.gamma_trig_cfg[2] = 900;
    g.tuning.modules.gamma_trig_cfg[3] = 600;
    g.tuning.modules.gamma_trig_cfg[4] = 400;
    for (i = 0; i < 4; i++)
        g.tuning.modules.bayer_gain[i] = 1024;
    g.tuning.enables.gamma_en = 1;
    g.tuning.enables.digital_gain_en = 1;

    /* module_cfg pointer targets */
    g.hw_cfg.linearize_table = g_lin;
    g.hw_cfg.lens_table = g_lens;
    g.hw_cfg.mesh_shading_table = g_msc;

    /* NULL context must be a no-op */
    config_band_step(NULL);

    /* band step: pic_h >> 7, clamped to [1, 63] */
    config_band_step(&g);
    CHECK_EQ(g.hw_cfg.afs_cfg.inc_line, 1080 >> 7);

    /* provider wiring */
    {
        const freeisp_tables_t *ft = freeisp_get_tables();
        const base_tables_t *bt = ft ? ft->base : NULL;

        CHECK(bt != NULL);
        if (bt) {
            CHECK(bt->gamma_base ==
                  g.tuning.modules.gamma_tbl_init[0]);
            CHECK(bt->gamma_trig != NULL);
            CHECK(bt->linear ==
                  g.tuning.modules.linearize_tbl);
            CHECK(bt->color_matrix ==
                  (const uint16_t *)g.tuning.modules
                      .colour_matrix_init);
        }
    }

    /* gamma: LV above the top trigger selects the base curve unchanged */
    g.sensor.ae_level = 2000;
    config_gamma(&g);
    CHECK_EQ(g.hw_cfg.gamma_cfg.gamma_tbl[100], 100);

    /* digital gain: bayer 1024 x gain 1024 -> 1024 */
    config_dig_gain(&g, 1024);
    CHECK_EQ(g.hw_cfg.gain_offset_cfg.gain.r_gain, 1024);
    CHECK_EQ(g.sensor.gain_offset.gr_gain, 1024);

    /* lens centre: Q12 centre maps to half the picture */
    config_lens_center(&g);
    CHECK_EQ(g.hw_cfg.lens_cfg.lens_shading_cfg.ct_x, 960);
    CHECK_EQ(g.hw_cfg.lens_cfg.lens_shading_cfg.ct_y, 540);

    /* table builders must not crash with an all-zero tuning block */
    config_lens_table(&g, 512);
    config_msc_table(&g, 512);

    printf("base_shim tests: %d failure(s)\n", fails);
    return fails ? 1 : 0;
}
