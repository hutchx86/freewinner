/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * test_module_cfg.c - host tests for the clean-room module dispatch tier.
 *
 * The fixed mk link line provides only module_cfg.c and this file, so the
 * already-validated writer tier is compiled in here; the config routines are
 * then checked end-to-end against the register block they poke.
 */
#include "module_cfg.h"

#include "../src/reg/reg_writers.c"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_checks, g_failures;

#define CHECK(c, m)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(c)) { g_failures++; printf("FAIL %d: %s\n", __LINE__, (m)); }  \
    } while (0)

#define EQ(a, b, m)                                                          \
    do {                                                                     \
        unsigned long A_ = (unsigned long)(a), B_ = (unsigned long)(b);       \
        g_checks++;                                                          \
        if (A_ != B_) {                                                      \
            g_failures++;                                                    \
            printf("FAIL %d: %s got 0x%lx want 0x%lx\n", __LINE__, (m), A_,  \
                   B_);                                                      \
        }                                                                    \
    } while (0)

static uint8_t  g[0x1000];
static uint8_t  g2[0x1000];
static isp_module_config_t cfg;
static uint16_t msc_words[4 * ISP_MSC_LUT_WORDS];

#define R(o)  (*(uint32_t *)(g + (o)))
#define R2(o) (*(uint32_t *)(g2 + (o)))

static void reset(void)
{
    memset(&cfg, 0, sizeof(cfg));
    memset(g, 0, sizeof(g));
    memset(g2, 0, sizeof(g2));
    isp_reg_map_load_addr(0, g);
    isp_reg_map_load_addr(1, g2);
}

static void fill8(uint8_t *p, unsigned n, unsigned seed)
{
    unsigned i;

    for (i = 0; i < n; i++)
        p[i] = (uint8_t)(seed + i * 7u + 1u);
}

static void fill16(uint16_t *p, unsigned n, unsigned seed)
{
    unsigned i;

    for (i = 0; i < n; i++)
        p[i] = (uint16_t)(seed + i * 3u + 1u);
}

/* ------------------------------------------------------------------ */

static const uint32_t expect_bits[ISP_MODULE_COUNT] = {
    ISP_FEAT_AFS,      ISP_FEAT_SHARP,  ISP_FEAT_CONTRAST, ISP_FEAT_D2D,
    ISP_FEAT_RGB_DRC,  ISP_FEAT_PLTM,   ISP_FEAT_WDR,      ISP_FEAT_CEM,
    ISP_FEAT_LSC,      ISP_FEAT_GAMMA,  ISP_FEAT_RGB2YUV,  ISP_FEAT_RGB2RGB,
    ISP_FEAT_AE,       ISP_FEAT_AF,     ISP_FEAT_AWB,      ISP_FEAT_HIST,
    ISP_FEAT_BLC,      ISP_FEAT_WB,     ISP_FEAT_DPC,      ISP_FEAT_CFA,
    ISP_FEAT_D3D,      ISP_FEAT_CNR,    ISP_FEAT_SATU,     ISP_FEAT_LINEAR,
    ISP_FEAT_SO,       ISP_FEAT_DG,     ISP_FEAT_CTC,      ISP_FEAT_MODE,
    ISP_FEAT_MSC,      ISP_FEAT_LCA,    ISP_FEAT_GCA
};

static const char *const expect_names[ISP_MODULE_COUNT] = {
    "AFS", "SHARP", "CONTRAST", "D2D", "RGB_DRC", "PLTM", "WDR", "CEM",
    "LSC", "GAMMA", "RGB2YUV", "RGB2RGB", "AE", "AF", "AWB", "HIST", "BLC",
    "WB", "DPC", "CFA", "D3D", "CNR", "SATU", "LINEAR", "SO", "DG", "CTC",
    "MODE", "MSC", "LCA", "GCA"
};

static void test_table(void)
{
    unsigned i;

    for (i = 0; i < ISP_MODULE_COUNT; i++) {
        EQ(isp_module_attrs[i].feature_bit, expect_bits[i], "feature bit");
        CHECK(strcmp(isp_module_attrs[i].feature_name, expect_names[i]) == 0,
              "feature name");
        CHECK(isp_module_attrs[i].config != NULL, "config present");
    }
    CHECK(isp_module_attrs[19].enable == NULL, "CFA has no enable");
    CHECK(isp_module_attrs[27].enable == NULL, "MODE has no enable");
    CHECK(isp_module_attrs[0].enable != NULL, "AFS has an enable");
}

static void test_map_addr(void)
{
    reset();
    cfg.isp_dev_id = 0;
    isp_map_addr(&cfg, g);
    isp_reg_set_input_fmt(0, 5);
    EQ((R(0) >> 4) & 7u, 5u, "isp_map_addr binds the register base");
    isp_map_addr(NULL, g);
    CHECK(1, "isp_map_addr(NULL) is safe");
}

static void test_enables(void)
{
    unsigned i;

    for (i = 0; i < ISP_MODULE_COUNT; i++) {
        uint32_t want;

        if (isp_module_attrs[i].enable == NULL)
            continue;
        memset(g, 0, sizeof(g));
        isp_reg_map_load_addr(0, g);
        isp_module_attrs[i].enable(&cfg, ISP_MODULE_ENABLE);
        want = (i == 2u || i == 10u) ? 0u : isp_module_attrs[i].feature_bit;
        if (i == 25u) {
            EQ(R(0x1a0), ISP_FEAT_DG, "dg enable sets its bypass bit");
            EQ(R(0x100), 0u, "dg mode 0 leaves isp_s1_cfg bit 2 clear");
        } else if (want == ISP_FEAT_MSC) {
            /* The MSC enable reuses the CONTRAST bypass bit (vendor quirk). */
            EQ(R(0x1a0), ISP_FEAT_CONTRAST, "msc enable sets the contrast bit");
        } else {
            EQ(R(0x1a0), want, "enable sets its own bit only");
        }
        isp_module_attrs[i].enable(&cfg, ISP_MODULE_DISABLE);
        /* Deployed: MSC's disable clears the CONTRAST bit too, so every
         * module's bypass bit is clear after disable (differential-blind). */
        EQ(R(0x1a0), 0u, "disable clears the bit");
        if (i == 25u)
            EQ(R(0x100), 0u, "dg disable clears isp_s1_cfg bit 2");
    }
}

static void test_dg_modes(void)
{
    reset();
    cfg.isp_dev_id = 0;

    cfg.mode_cfg.dg_mode = 0u;
    isp_reg_enable_digital_gain(&cfg, ISP_MODULE_ENABLE);
    EQ(R(0x1a0), ISP_FEAT_DG, "dg mode 0 routes before the sensor offset");
    EQ(R(0x100), 0u, "dg mode 0 leaves isp_s1_cfg bit 2 clear");

    reset();
    cfg.isp_dev_id = 0;
    cfg.mode_cfg.dg_mode = 2u;
    isp_reg_enable_digital_gain(&cfg, ISP_MODULE_ENABLE);
    EQ(R(0x1a0), ISP_FEAT_MODE, "dg mode 2 routes after the sensor offset");
    EQ(R(0x100), 0x4u, "dg mode 2 sets isp_s1_cfg bit 2");

    isp_reg_enable_digital_gain(&cfg, ISP_MODULE_DISABLE);
    EQ(R(0x1a0), 0u, "dg disable clears the bypass bits");
    EQ(R(0x100), 0u, "dg disable clears isp_s1_cfg bit 2");
}

static unsigned long gamma_field(unsigned long v)
{
    unsigned long f = (v + 2u) >> 2;

    return f > 0x3ffu ? 0x3ffu : f;
}

static void populate(void)
{
    cfg.isp_dev_id = 0;
    cfg.afs_cfg.inc_line = 7u;
    cfg.mode_cfg.input_fmt = 5u;
    cfg.mode_cfg.wdr_cmp_mode = 1u;
    cfg.mode_cfg.rsc_mode = 2u;
    cfg.mode_cfg.dg_mode = 1u;
    cfg.mode_cfg.cfa_mode = 1u;
    cfg.mode_cfg.otf_dpc_mode = 2u;
    cfg.mode_cfg.hist_sel = 1u;
    cfg.mode_cfg.awb_mode = 1u;
    cfg.mode_cfg.ae_mode = 3u;
    cfg.mode_cfg.msc_mode = 1u;
    cfg.mode_cfg.d3d_mode = 1u;

    cfg.sharp_cfg.edge_black_stren = 0x11u;
    cfg.bdnf_cfg.lf_ratio = 1u;
    cfg.pltm_cfg.lss_switch = 1u;
    cfg.wdr_cfg.lo_th = 0x1111u;
    cfg.wdr_cfg.hi_th = 0x2222u;
    cfg.lens_cfg.lsc_cfg.ct_x = 0x33u;
    cfg.rgb2yuv.gain[0] = 0x101u;
    cfg.rgb2rgb_cfg.color_matrix[0] = 0x102u;
    cfg.ae_cfg.win.width = 0x100u;
    cfg.af_cfg.mode = 1u;
    cfg.af_cfg.win.hor_num = 3u;
    cfg.awb_cfg.sat_r = 0x11u;
    cfg.hist_cfg.win.width = 0x100u;
    cfg.gain_offset_cfg.offset[0] = 1u;
    cfg.wb_gain_cfg.clip_val = 0xabcu;
    cfg.wb_gain_cfg.wb_gain[0] = 0x111u;
    cfg.otf_cfg.ratio[0] = 1u;
    cfg.cfa_cfg.dir_th = 1u;
    cfg.tdf_cfg.bright_diff = 1u;
    cfg.cnr_cfg.c_th = 1u;
    cfg.satu_cfg.satu_r = 5u;
    cfg.satu_cfg.satu_g = 6u;
    cfg.satu_cfg.satu_b = ISP_SATU_SUM_TARGET - 5u - 6u;
    cfg.gain_offset_cfg.sensor_offset[0] = 1u;
    cfg.gain_offset_cfg.gain[0] = 1u;
    cfg.ctc_cfg.th_max = 1u;
    cfg.lca_cfg.gf_cor_ratio = 1u;
    cfg.gca_cfg.ct_h = 1u;

    fill8(cfg.drc_src, ISP_DRC_TBL_SIZE, 1u);
    fill8(cfg.pltm_src, ISP_PLTM_TBL_SIZE, 2u);
    fill8(cfg.cem_src, ISP_CEM_TBL_SIZE, 3u);
    fill8(cfg.fe_table, ISP_LINEAR_TBL_SIZE, 4u);
    memset(cfg.linear_src, 0xeeu, ISP_LINEAR_TBL_SIZE);
    fill8(cfg.wdr_src, ISP_WDR_TBL_SIZE, 5u);
    fill8(cfg.d3d_tdnf_th, ISP_LUT_TH_BYTES, 6u);
    fill8(cfg.lca_pf_satu_lut, ISP_LCA_SATU_BYTES, 0x21u);
    fill16(cfg.lens_src, ISP_LENS_TBL_SIZE, 7u);
    fill16(cfg.gamma_cfg.gamma_tbl, ISP_GAMMA_TBL_LENGTH, 8u);
    /* Destination tables are present (the SDK pointer targets are non-NULL). */
    cfg.cem_dst = cfg.cem_table;
    cfg.drc_dst = cfg.drc_table;
    cfg.pltm_dst = cfg.pltm_table;
    cfg.wdr_dst = cfg.wdr_cfg.wdr_table;
    cfg.lens_dst = cfg.lens_table;
    cfg.gamma_dst = cfg.gamma_cfg.gamma_packed;
    fill16(msc_words, 4u * ISP_MSC_LUT_WORDS, 9u);
    cfg.msc_table = msc_words;
    fill16(cfg.msc_cfg.blw, ISP_MSC_LUT_WORDS, 0x21u);
    fill16(cfg.msc_cfg.blh, ISP_MSC_LUT_WORDS, 0x31u);
    fill16(cfg.msc_cfg.blw_dlt, ISP_MSC_LUT_WORDS, 0x41u);
    fill16(cfg.msc_cfg.blh_dlt, ISP_MSC_LUT_WORDS, 0x51u);
}

static void test_full_dispatch(void)
{
    uint32_t emask = 0;
    unsigned i;
    unsigned long packed0;

    reset();
    populate();
    for (i = 0; i < ISP_MODULE_COUNT; i++) {
        if (isp_module_attrs[i].enable && i != 2u && i != 10u)
            emask |= isp_module_attrs[i].feature_bit;
    }

    cfg.module_enable_flag = 0xffffffffu;
    isp_hardware_update(&cfg);

    EQ(R(0x1a0), (emask & ~(unsigned long)ISP_FEAT_MSC) |
                     (unsigned long)ISP_FEAT_CONTRAST,
       "every actionable module enabled");
    EQ(cfg.table_update, 0u, "table_update is reset after the forced load");
    EQ(R(0x6e0), 7u, "afs line increment");
    CHECK(R(0x420) != 0u, "sharp config written");
    CHECK(memcmp(g + 0x86c, cfg.sharp_val_lut, ISP_LUT_TH_BYTES) == 0,
          "sharp val lut staged");
    CHECK(R(0x2a0) != 0u, "d2d config written");
    CHECK(memcmp(g + 0x9b8, cfg.d2d_lp_lut[0], ISP_LUT_TH_BYTES) == 0,
          "d2d lp0 lut staged");
    CHECK(memcmp(cfg.drc_table, cfg.drc_src, ISP_DRC_TBL_SIZE) == 0,
          "drc table copied");
    CHECK(R(0x3b0) != 0u, "pltm config written");
    CHECK(memcmp(cfg.pltm_table, cfg.pltm_src, ISP_PLTM_TBL_SIZE) == 0,
          "pltm table copied");
    EQ(R(0x200), 0x22221111u, "wdr thresholds");
    CHECK(memcmp(cfg.wdr_cfg.wdr_table, cfg.wdr_src, ISP_WDR_TBL_SIZE) == 0,
          "wdr table copied");
    CHECK(memcmp(cfg.cem_table, cfg.cem_src, ISP_CEM_TBL_SIZE) == 0,
          "cem table copied");
    EQ(R(0x390), 0x000033u, "lsc centre");
    EQ(memcmp(cfg.lens_table, cfg.lens_src, sizeof(cfg.lens_table)), 0,
       "lens table copied");
    packed0 = ((((unsigned long)cfg.gamma_cfg.gamma_tbl[0] + 2u) >> 2) &
               0x3ffu) |
              (((((unsigned long)cfg.gamma_cfg.gamma_tbl[ISP_GAMMA_PLANE] +
                  2u) >>
                 2) &
                0x3ffu)
               << 10) |
              (((((unsigned long)cfg.gamma_cfg
                      .gamma_tbl[2u * ISP_GAMMA_PLANE] +
                  2u) >>
                 2) &
                0x3ffu)
               << 20);
    packed0 = gamma_field((unsigned long)cfg.gamma_cfg.gamma_tbl[0]) |
              (gamma_field((unsigned long)
                               cfg.gamma_cfg.gamma_tbl[ISP_GAMMA_PLANE])
               << 10) |
              (gamma_field((unsigned long)
                               cfg.gamma_cfg
                                   .gamma_tbl[2u * ISP_GAMMA_PLANE])
               << 20);
    EQ(cfg.gamma_cfg.gamma_packed[0], packed0, "gamma packed word");
    CHECK(R(0x520) != 0u, "rgb2yuv written");
    CHECK(R(0x440) != 0u, "rgb2rgb written");
    CHECK(R(0x600) != 0u, "ae window written");
    CHECK(R(0x610) != 0u, "af window written");
    CHECK(R(0x690) != 0u, "awb satur written");
    CHECK(R(0x6c0) != 0u, "hist window written");
    CHECK(R(0x104) != 0u, "blc offsets written");
    CHECK(R(0x370) != 0u, "wb gain written");
    EQ(R(0x378), 0xabcu, "wb clip written");
    CHECK(R(0x260) != 0u, "dpc written");
    CHECK(R(0x400) != 0u, "cfa written");
    CHECK(R(0x2d0) != 0u, "d3d config written");
    CHECK(memcmp(g + 0x770, cfg.d3d_tdnf_th, ISP_LUT_TH_BYTES) == 0,
          "d3d lum th staged from tdnf");
    CHECK(R(0x470) != 0u, "cnr written");
    EQ(R(0x490), (5u & 0xfu) | (6u << 4) |
                      ((ISP_SATU_SUM_TARGET - 11u) << 8),
       "saturation written when sum matches");
    CHECK(memcmp(cfg.linear_src, cfg.fe_table, ISP_LINEAR_TBL_SIZE) == 0,
          "linear table copied from the front-end table");
    CHECK(R(0x340) != 0u, "sensor offset written");
    CHECK(R(0x360) != 0u, "digital gain written");
    CHECK(R(0x270) != 0u, "ctc written");
    EQ((R(0) >> 4) & 7u, 5u, "input format written");
    CHECK(R(0xad8) != 0u, "msc blw lut written");
    CHECK(R(0xb18) != 0u, "lca pf satu written");
    CHECK(R(0x280) != 0u, "gca written");

    memset(g2, 0, sizeof(g2));
    isp_reg_map_load_addr(1, g2);
    isp_reg_update_table(1, ISP_TABLE_LOAD_MASK);
    EQ(R(0x20), R2(0x20), "update forced with the full 0xffff mask");
}

static void test_gated_dispatch(void)
{
    reset();
    populate();
    cfg.module_enable_flag = ISP_FEAT_SHARP;
    isp_hardware_update(&cfg);

    CHECK(R(0x420) != 0u, "enabled module is configured");
    EQ(R(0x2a0), 0u, "disabled module is not configured");
    EQ(R(0x1a0), ISP_FEAT_SHARP, "only the enabled module bit is set");
    EQ(cfg.table_update, 0u, "table_update reset");
}

static void test_config_details(void)
{
    isp_module_config_t *c = &cfg;

    reset();
    c->isp_dev_id = 0;

    /* Both source probes (tbl[0xbff], tbl[0x2ff]) are zero, so the deployed
     * object emits the gamma-2.2 ramp, not the normal pack.  Differential-blind
     * branch; the expectation was corrected to the deployed behaviour. */
    c->gamma_cfg.gamma_tbl[0] = 0x3ffu;
    c->gamma_cfg.gamma_tbl[ISP_GAMMA_PLANE] = 1u;
    c->gamma_cfg.gamma_tbl[2u * ISP_GAMMA_PLANE] = 2u;
    c->gamma_dst = c->gamma_cfg.gamma_packed;
    isp_reg_prepare_gamma(c);
    EQ(c->gamma_cfg.gamma_packed[0], 0u, "gamma both probes zero -> ramp");
    {
        uint32_t v = (uint32_t)(1023.0 * pow(1.0 / 1023.0, 1.0 / 2.2));

        EQ(c->gamma_cfg.gamma_packed[1], v | (v << 10) | (v << 20),
           "gamma ramp row 1");
    }
    EQ(c->table_update & ISP_TABLE_UPDATE_GAMMA, ISP_TABLE_UPDATE_GAMMA,
       "gamma update bit");

    memset(c->msc_cfg.blw, 0, sizeof(c->msc_cfg.blw));
    c->msc_cfg.blw[0] = 0x3ffu;
    c->mode_cfg.msc_mode = 3u;
    c->msc_table = NULL;
    c->table_update = 0u;
    isp_reg_prepare_msc(c);
    EQ((R(0x1b0) >> 18) & 3u, 3u, "msc mode written with a NULL table");
    CHECK(R(0xad8) != 0u, "msc blw written with a NULL table");
    EQ(c->table_update & ISP_TABLE_UPDATE_MSC, 0u,
       "msc update bit stays clear with a NULL table");

    c->satu_cfg.satu_r = 1u;
    c->satu_cfg.satu_g = 2u;
    c->satu_cfg.satu_b = 3u;
    isp_reg_prepare_saturation(c);
    EQ(R(0x490), 0u, "saturation skipped unless sum is 0x10");

    c->satu_cfg.satu_r = 0x10u;
    c->satu_cfg.satu_g = 0u;
    c->satu_cfg.satu_b = 0u;
    c->satu_cfg.mode = 1u;
    fill8(c->satu_cfg.table, ISP_SATU_TBL_SIZE, 0x10u);
    isp_reg_prepare_saturation(c);
    EQ(R(0x490), 0u, "saturation nibbles still zero with caller values");
    EQ((R(0x1b0) >> 1) & 1u, 1u, "saturation mode written");
    /* Deployed raises the update bit only when the saturation source pointer is
     * non-NULL; corrected from the previous unconditional expectation
     * (differential-blind). */
    EQ(c->table_update & ISP_TABLE_UPDATE_SATU, 0u,
       "saturation update bit clear with a NULL source");
    {
        uint8_t dst[ISP_SATU_TBL_SIZE];
        uint8_t embed[ISP_SATU_TBL_SIZE];
        unsigned i;
        int dst_ok = 1, embed_ok = 1;

        fill8(dst, ISP_SATU_TBL_SIZE, 0x77u);
        fill8(c->satu_cfg.table, ISP_SATU_TBL_SIZE, 0x10u);
        memcpy(embed, c->satu_cfg.table, ISP_SATU_TBL_SIZE);
        c->satu_src = dst;
        c->table_update = 0u;
        isp_reg_prepare_saturation(c);
        EQ(c->table_update & ISP_TABLE_UPDATE_SATU, ISP_TABLE_UPDATE_SATU,
           "saturation update bit set with a source");
        for (i = 0; i < ISP_SATU_TBL_SIZE; i++) {
            if (dst[i] != embed[i])
                dst_ok = 0;
            if (c->satu_cfg.table[i] != embed[i])
                embed_ok = 0;
        }
        /* Deployed direction: the embedded table is copied into the pointer
         * target and the embedded array is left untouched.  Corrected after
         * black-box probing of the deployed object (the old expectation encoded
         * the inverted direction; differential-blind). */
        CHECK(dst_ok, "saturation copied embedded -> pointer target");
        CHECK(embed_ok, "saturation embedded table untouched");
    }

    /* A6: a NULL destination pointer suppresses both the table copy and its
     * table_update bit, leaving the destination bytes untouched. */
    c->table_update = 0u;
    c->cem_dst = NULL;
    memset(c->cem_table, 0xa5, sizeof(c->cem_table));
    isp_reg_prepare_cem(c);
    EQ(c->table_update & ISP_TABLE_UPDATE_CEM, 0u, "cem bit clear with NULL dst");
    CHECK(c->cem_table[0] == 0xa5, "cem table untouched with NULL dst");
    c->cem_dst = c->cem_table;

    c->table_update = 0u;
    c->drc_dst = NULL;
    memset(c->drc_table, 0xa5, sizeof(c->drc_table));
    isp_reg_prepare_rgb_drc(c);
    EQ(c->table_update & ISP_TABLE_UPDATE_RGB_DRC, 0u,
       "drc bit clear with NULL dst");
    CHECK(c->drc_table[0] == 0xa5, "drc table untouched with NULL dst");
    c->drc_dst = c->drc_table;

    c->table_update = 0u;
    c->pltm_dst = NULL;
    memset(c->pltm_table, 0xa5, sizeof(c->pltm_table));
    isp_reg_prepare_pltm(c);
    EQ(c->table_update & ISP_TABLE_UPDATE_PLTM, 0u,
       "pltm bit clear with NULL dst");
    CHECK(c->pltm_table[0] == 0xa5, "pltm table untouched with NULL dst");
    c->pltm_dst = c->pltm_table;

    c->table_update = 0u;
    c->wdr_dst = NULL;
    memset(c->wdr_cfg.wdr_table, 0xa5, sizeof(c->wdr_cfg.wdr_table));
    isp_reg_prepare_wdr(c);
    EQ(c->table_update & ISP_TABLE_UPDATE_WDR, 0u,
       "wdr bit clear with NULL dst");
    CHECK(c->wdr_cfg.wdr_table[0] == 0xa5, "wdr table untouched with NULL dst");
    c->wdr_dst = c->wdr_cfg.wdr_table;

    c->table_update = 0u;
    c->lens_dst = NULL;
    memset(c->lens_table, 0xa5, sizeof(c->lens_table));
    isp_reg_prepare_lens(c);
    EQ(c->table_update & ISP_TABLE_UPDATE_LSC, 0u,
       "lsc bit clear with NULL dst");
    CHECK(c->lens_table[0] == 0xa5a5, "lens table untouched with NULL dst");
    c->lens_dst = c->lens_table;

    c->table_update = 0u;
    c->gamma_dst = NULL;
    c->gamma_cfg.gamma_packed[0] = 0xdeadbeefu;
    isp_reg_prepare_gamma(c);
    EQ(c->table_update & ISP_TABLE_UPDATE_GAMMA, 0u,
       "gamma bit clear with NULL dst");
    EQ(c->gamma_cfg.gamma_packed[0], 0xdeadbeefu,
       "gamma packing skipped with NULL dst");
    c->gamma_dst = c->gamma_cfg.gamma_packed;
}

int main(void)
{
    test_table();
    test_map_addr();
    test_enables();
    test_dg_modes();
    test_full_dispatch();
    test_gated_dispatch();
    test_config_details();

    printf("module_cfg tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
