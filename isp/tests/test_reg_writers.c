/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_reg_writers.c - host tests for the register-writer tier: drives the
 * writers into a flat block and checks fields against spec/reglayer2.md 1. */
#include "reg_writers.h"

#include <stdio.h>
#include <string.h>

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

static uint8_t g[0x1000], g2[0x1000];
#define R(o) (*(uint32_t *)(g + (o)))
#define R2(o) (*(uint32_t *)(g2 + (o)))
#define W(o, v) (*(uint32_t *)(g + (o)) = (uint32_t)(v))
#define F(o, m, s) (((R(o)) >> (s)) & (m))

static void reset(void)
{
    memset(g, 0, sizeof(g));
    memset(g2, 0, sizeof(g2));
}

/* ------------------------------------------------------------------ */

static void test_map_fmt_table_top(void)
{
    reset();
    isp_reg_map_load_addr(0, g);

    W(0, 0xffffff8fu);
    isp_reg_set_input_fmt(0, 5);
    EQ(F(0, 7, 4), 5, "input_fmt field");
    EQ(R(0) & 0x8fu, 0x8fu, "input_fmt keeps other bits");

    isp_reg_map_load_addr(1, g2);
    isp_reg_set_input_fmt(1, 2);
    EQ(F(0, 7, 4), 5, "instance 0 untouched");
    EQ((R2(0) >> 4) & 7u, 2u, "instance 1 independent");
    isp_reg_set_input_fmt(9, 1); /* out of range must not crash */
    CHECK(1, "out of range id survives");

    W(0x20, 0);
    isp_reg_update_table(0, 0x0f0e);   /* first arg = instance id, not a value */
    EQ(R(0x20), 0x2f0eu, "update_table mapping");

    W(0x0fc, 0xffffffffu);
    isp_reg_top_control(0, 3, 0x1234);
    EQ(F(0x0fc, 0x3fff, 0), 0x1234u, "top_control width");
    EQ(F(0x0fc, 1, 16), 0, "top_control (n-1)&1");
    W(0x0fc, 0);
    isp_reg_top_control(0, 4, 0x3fff);
    EQ(F(0x0fc, 1, 16), 1, "top_control (n-1)&1 odd");
}

static void test_module_flags(void)
{
    reset();
    isp_reg_module_enable(0, ISP_FEAT_BLC | ISP_FEAT_LINEAR | ISP_FEAT_MODE);
    EQ(R(0x1a0), 0x4040002u, "module bits set");
    EQ(R(0x100), 7u, "s1 side-effect bits");
    isp_reg_module_disable(0, ISP_FEAT_BLC | ISP_FEAT_LINEAR | ISP_FEAT_MODE);
    EQ(R(0x1a0), 0, "module bits clear");
    EQ(R(0x100), 0, "s1 side-effect clear");

    isp_reg_module_enable(0, ISP_FEAT_WDR);
    EQ(R(0x1a0), ISP_FEAT_WDR, "wdr module bit");
    EQ(F(0x004, 1, 14), 1, "wdr global_cfg1 byte1 bit6");
    isp_reg_module_disable(0, ISP_FEAT_WDR);
    EQ(F(0x004, 1, 14), 0, "wdr side-effect clear");
}

static void test_modes(void)
{
    reset();
    isp_reg_set_wdr_compress_mode(0, 1);
    isp_reg_set_saturation_mode(0, 1);
    isp_reg_set_cfa_mode(0, 1);
    isp_reg_set_dg_mode(0, 1);
    isp_reg_set_ae_mode(0, 3);
    isp_reg_set_awb_mode(0, 1);
    isp_reg_set_hist_src(0, 1);
    isp_reg_set_lsc_mode(0, 2);
    isp_reg_set_msc_mode(0, 1);
    isp_reg_set_hist_mode(0, 3);
    isp_reg_set_dpc_mode(0, 2);
    isp_reg_set_d3d_mode(0, 1);
    isp_reg_set_af_mode(0, 1);

    EQ(F(0x000, 1, 24), 1, "wdr compress mode");
    EQ(F(0x1b0, 1, 1), 1, "saturation mode");
    EQ(F(0x1b0, 1, 2), 1, "cfa mode");
    EQ(F(0x1b0, 1, 3), 1, "dg mode");
    EQ(F(0x1b0, 3, 4), 3, "ae mode");
    EQ(F(0x1b0, 1, 6), 1, "awb mode");
    EQ(F(0x1b0, 1, 7), 1, "hist src");
    EQ(F(0x1b0, 3, 16), 2, "lsc mode");
    EQ(F(0x1b0, 3, 18), 1, "msc mode");
    EQ(F(0x1b0, 3, 8), 3, "hist mode");
    EQ(F(0x1b0, 3, 10), 2, "dpc mode");
    EQ(F(0x1b0, 3, 12), 1, "d3d mode");
    EQ(F(0x1b0, 1, 14), 1, "af mode");
}

static void test_payload_words(void)
{
    reset();
    isp_reg_set_blc_offset(0, 0x1234, 0x0abc, 0x1def, 0x0fff);
    EQ(R(0x104), 0x0abc1234u, "s1 blc0");
    EQ(R(0x108), 0x0fff1defu, "s1 blc1");
    EQ(R(0x1ec), 0x0abc1234u, "s0 blc0 mirrored");
    EQ(R(0x1f0), 0x0fff1defu, "s0 blc1 mirrored");

    isp_reg_set_wdr_cfg(0, 0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x55, 2);
    EQ(R(0x200), 0x22221111u, "wdr cfg0");
    EQ(R(0x204), 0x44443333u, "wdr cfg1");
    EQ(R(0x208), 0x80555555u, "wdr cfg2");

    isp_reg_set_dpc(0, 1, 2, 3, 4, 0x3ff, 0x1ff);
    EQ(R(0x260), 0x04030201u, "dpc cfg0");
    EQ(R(0x264), 0x01ff03ffu, "dpc cfg1 (cold_abs_th [24:16])");

    isp_reg_set_sensor_offset(0, 0x1234, 0x0abc, 0x1def, 0x0fff);
    EQ(R(0x340), 0x0abc1234u, "sensor offset0");
    EQ(R(0x344), 0x0fff1defu, "sensor offset1");

    isp_reg_set_dg_gain(0, 0x1111, 0x2222, 0x3333, 0x4444);
    EQ(R(0x360), 0x22221111u, "dg gain0");
    EQ(R(0x364), 0x44443333u, "dg gain1");
    EQ(R(0x10c), 0x22221111u, "s1 dg gain0 mirror");
    EQ(R(0x110), 0x44443333u, "s1 dg gain1 mirror");
    EQ(R(0x0f4), 0x22221111u, "s0 dg gain0 mirror");
    EQ(R(0x0f8), 0x44443333u, "s0 dg gain1 mirror");

    isp_reg_set_wb_gain(0, 0x111, 0x222, 0x333, 0x444);
    EQ(R(0x370), 0x02220111u, "wb gain0");
    EQ(R(0x374), 0x04440333u, "wb gain1");
    isp_reg_set_wb_clip(0, 0xabc);     /* first arg = instance id */
    EQ(R(0x378), 0xabcu, "wb clip");

    isp_reg_set_lsc(0, 0x1234, 0x0abc, 0x15);
    EQ(R(0x390), 0x55579234u, "lsc packing");
    EQ(R(0x394), 0x55579234u, "lsc cfg1 identical");
    EQ(R(0x398), 0x55579234u, "lsc cfg2 identical");

    isp_reg_set_cnr(0, 0x123, 0x456, 0x789, 0xabc);
    EQ(R(0x470), 0x04560123u, "cnr cfg0");
    EQ(R(0x474), 0x0abc0789u, "cnr cfg1");

    isp_reg_set_saturation(0, 1, 2, 3);
    EQ(R(0x490), 0x321u, "saturation nibbles");
    isp_reg_set_dehaze(0);
    EQ(R(0x490), 0x321u, "dehaze no-op");

    isp_reg_set_cfa(0, 0x1abc, 1, 0xa);
    EQ(R(0x400), 0x00a11abcu, "cfa packing (interp [16], zig_zag [23:20])");

    isp_reg_set_afs_anti_flick(0, 0x2a); /* first arg = instance id */
    EQ(R(0x6e0), 0x2au, "afs line inc");
}

static void test_field_preserve(void)
{
    reset();
    isp_reg_map_load_addr(0, g);

    /* wb clip is a 12-bit field in an otherwise untouched word. */
    W(0x378, 0xA5A5A5A5u);
    isp_reg_set_wb_clip(0, 0xabc);     /* first arg = instance id */
    EQ(R(0x378), 0xA5A5AABCu, "wb_clip preserves outside bits");

    /* saturation packs three nibbles in bits [11:0]. */
    W(0x490, 0xA5A5A5A5u);
    isp_reg_set_saturation(0, 1, 2, 3);
    EQ(R(0x490), 0xA5A5A321u, "saturation preserves outside bits");

    /* blc offsets are 13-bit fields with holes at [15:13] and [31:29]. */
    W(0x104, 0xA5A5A5A5u);
    W(0x108, 0xA5A5A5A5u);
    isp_reg_set_blc_offset(0, 0x1234, 0x0abc, 0x1def, 0x0fff);
    EQ(R(0x104), 0xAABCB234u, "blc offset0 preserves holes");
    EQ(R(0x108), 0xAFFFBDEFu, "blc offset1 preserves holes");
}

static void test_struct_payloads(void)
{
    fwi_reg_ctc_cfg_t ctc;
    fwi_reg_gca_cfg_t gca;
    fwi_reg_lca_cfg_t lca;
    fwi_reg_d2d_cfg_t d2d;
    fwi_reg_d3d_cfg_t d3d;
    fwi_reg_pltm_cfg_t p;
    fwi_reg_sharp_cfg_t s;
    static const uint16_t gain[9] = { 0x101, 0x102, 0x103, 0x104, 0x105,
                                      0x106, 0x107, 0x108, 0x109 };
    static const uint16_t off[3] = { 0x111, 0x222, 0x333 };
    unsigned i;

    reset();
    memset(&ctc, 0, sizeof(ctc));
    ctc.th_max = 0xabc; ctc.th_min = 0xdef; ctc.slope = 0x1234;
    ctc.dir_wt = 0x55; ctc.dir_th = 0x678;
    isp_reg_set_ctc(0, &ctc);
    EQ(R(0x270), 0x0abc0defu, "ctc th (th_min [11:0], th_max [27:16])");
    EQ(R(0x274), 0x1234u, "ctc slope");
    EQ(R(0x278), 0x67855u, "ctc dir");

    memset(&gca, 0, sizeof(gca));
    gca.ct_h = 0x111; gca.ct_w = 0x222;
    gca.r.para0 = 0xaa; gca.r.para1 = 0x155; gca.r.para2 = 0x2aa;
    gca.r.int_cns = 0x11;
    gca.b.para0 = 0xbb; gca.b.para1 = 0x1aa; gca.b.para2 = 0x2bb;
    gca.b.int_cns = 0x22;
    isp_reg_set_gca(0, &gca);
    EQ(R(0x280), 0x02220111u, "gca center");
    EQ(F(0x284, 0xff, 0), 0xaa, "gca r para0");
    EQ(F(0x284, 0x3ff, 8), 0x155, "gca r para1");
    EQ(F(0x284, 0x3ff, 18), 0x2aa, "gca r para2");
    EQ(F(0x288, 0xff, 0), 0xbb, "gca b para0");
    EQ(R(0x28c), 0x11u, "gca ctrl int_cns (r only)");
    EQ(F(0x28c, 0xff, 8), 0, "gca b int_cns never written");

    memset(&lca, 0, sizeof(lca));
    lca.gf_cor_ratio = 0x155; lca.pf_cor_ratio = 0x2aa;
    lca.lum_th = 0x123; lca.grad_th = 0x456; lca.clr_gth = 0x1ab;
    lca.pf_rshf = 0x5; lca.pf_bslp = 0x1aa;
    lca.clrs_lum_th = 0x34; lca.pf_clrc_ratio = 0xc;
    lca.gf_clrc_ratio = 0xd; lca.pf_decr_ratio = 0x3;
    isp_reg_set_lca(0, &lca);
    EQ(R(0x410), 0x02aa0155u, "lca cor ratio");
    EQ(R(0x414), 0x04560123u, "lca det ctrl0");
    EQ(F(0x418, 0x3ff, 0), 0x1ab, "lca clr_gth [9:0]");
    EQ(F(0x418, 0xf, 12), 0x5, "lca pf_rshf [15:12]");
    EQ(F(0x418, 0x1ff, 16), 0x1aa, "lca pf_bslp [24:16]");
    EQ(R(0x418), 0x01aa51abu, "lca det ctrl1 whole");
    EQ(F(0x41c, 0xff, 0), 0x34, "lca clrs_lum_th byte [7:0]");
    EQ(F(0x41c, 0xf, 8), 0xc, "lca pf_clrc_ratio [11:8]");
    EQ(F(0x41c, 0xf, 12), 0xd, "lca gf_clrc_ratio [15:12]");
    EQ(F(0x41c, 0xf, 16), 0x3, "lca pf_decr_ratio [19:16]");
    EQ(R(0x41c), 0x0003dc34u, "lca cor ctrl whole");

    memset(&d2d, 0, sizeof(d2d));
    d2d.lf_ratio = 0x11; d2d.bf_ratio = 0x22; d2d.hf_ratio = 0x33;
    for (i = 0; i < 4; i++) {
        d2d.lp_core[i] = (uint8_t)(i + 1);
        d2d.lp_pcnt[i] = (uint8_t)(i + 9);
    }
    for (i = 0; i < 3; i++)
        d2d.lp_side[i] = (uint8_t)(i + 5);
    isp_reg_set_d2d_cfg(0, &d2d);
    EQ(R(0x2a0), 0x00332211u, "d2d ratios");
    EQ(R(0x2a4), 0x04030201u, "d2d lp core");
    EQ(R(0x2a8), 0x00070605u, "d2d lp side (3 bytes, byte3 preserved)");
    EQ(R(0x2ac), 0x0c0b0a09u, "d2d lp pcnt");

    memset(&d3d, 0, sizeof(d3d));
    d3d.bright_diff = 0x11; d3d.clip_ratio = 0x22; d3d.lum_diff_clip = 0x33;
    d3d.noise_clip = 0x3344;
    d3d.st_2d = 1; d3d.mv_ori = 0x15; d3d.ltf_en = 1; d3d.rec_en = 1;
    d3d.c_weight1 = 0x111; d3d.c_weight2 = 0x222; d3d.c_weight3 = 0x333;
    d3d.ltf_update_frm = 0x2aa;
    isp_reg_set_d3d_cfg(0, &d3d);
    EQ(R(0x2d0), 0x44332211u, "d3d cfg0 (lum_diff_clip [23:16], noise_clip byte)");
    EQ(F(0x2d4, 1, 0), 1, "d3d st_2d");
    EQ(F(0x2d4, 0x1f, 16), 0x15, "d3d mv_ori [20:16]");
    EQ(F(0x2d4, 1, 24), 1, "d3d ltf_en [24]");
    EQ(F(0x2d4, 1, 25), 1, "d3d rec_en [25]");
    EQ(R(0x2d4), 0x03150001u, "d3d cfg1 whole");
    EQ(R(0x2d8), 0x02220111u, "d3d cfg2");
    EQ(R(0x2dc), 0x02aa0333u, "d3d cfg3 (ltf_update_frm [25:16])");

    memset(&p, 0, sizeof(p));
    p.lss_switch = 1; p.cal_en = 1; p.last_order_ratio = 0x5;
    p.tr_order = 0x3; p.oripic_ratio = 0xab; p.intens_asym = 0x11;
    p.spatial_asm = 0x22; p.white_level = 0x345; p.lp_halo_res = 0xa;
    p.lum_ratio = 0xb; p.block_height = 0x10; p.block_width = 0x20;
    p.block_v_num = 0x5; p.block_h_num = 0x7;
    p.statistic_div = 0xdeadbeefu;
    isp_reg_set_pltm_cfg(0, &p);
    EQ(R(0x3b0), 0xab030503u, "pltm cfg0 (order [19:16]/[11:8], oripic [31:24])");
    EQ(R(0x3b4), 0xba452211u, "pltm cfg1 (white_level byte, lum_ratio [31:28])");
    EQ(R(0x3b8), 0x07052010u, "pltm cfg2 (h_num [28:24], v_num [20:16])");
    EQ(R(0x3bc), 0xdeadbeefu, "pltm cfg3");

    memset(&s, 0, sizeof(s));
    s.edge_black_stren = 0x111; s.edge_white_stren = 0x222; s.edge_scale = 0x1f;
    s.hfrq_black_stren = 0x333; s.hfrq_white_stren = 0x444; s.hfrq_scale = 0x1e;
    s.scale_ratio = 0x1f; s.conv_ratio = 0x1e; s.ns_lw_th = 0x555;
    s.ns_hi_th = 0x666; s.dir_clip_val = 0x777; s.dir_eq_ratio = 0x888;
    s.edge_th = 0xab; s.hv_edge_sm = 0x1f; s.aa_edge_sm = 0x1e;
    s.over_val = 0x123; s.over_area = 0x234;
    s.under_val = 0x321; s.under_area = 0x232;
    isp_reg_set_sharp(0, &s);
    EQ(R(0x420), 0x02220111u, "sharp edge (white_stren [27:16])");
    EQ(R(0x424), 0x04440333u, "sharp hfrq (white_stren [27:16])");
    EQ(R(0x428), 0x1e1f1e1fu, "sharp diff (scales/conv bytes)");
    EQ(R(0x42c), 0x06660555u, "sharp ref noise");
    EQ(R(0x430), 0x08880777u, "sharp dir diff");
    EQ(R(0x434), 0x1e1f00abu, "sharp edge ctrl (hv [20:16], aa [28:24])");
    EQ(R(0x438), 0x02340123u, "sharp over");
    EQ(R(0x43c), 0x02320321u, "sharp under");

    isp_reg_set_rgb2rgb_gain_offset(0, gain, off);
    EQ(R(0x440), 0x01020101u, "rgb2rgb gain0");
    EQ(F(0x450, 0xfff, 0), 0x109, "rgb2rgb gain8");
    EQ(F(0x450, 0x1fff, 16), 0x111, "rgb2rgb offset0 [28:16]");
    EQ(R(0x450), 0x01110109u, "rgb2rgb gain4 whole");
    EQ(R(0x454), 0x03330222u, "rgb2rgb offset1/2");

    isp_reg_set_rgb2yuv_gain_offset(0, gain, off);
    EQ(R(0x520), 0x01020101u, "rgb2yuv gain pair (high [26:16])");
    EQ(R(0x530), 0x109u, "rgb2yuv gain8");
    EQ(R(0x534), 0x02220111u, "rgb2yuv offset0/1");
    EQ(R(0x538), 0x333u, "rgb2yuv offset2");
}

static void test_windows_and_af(void)
{
    fwi_reg_af_filter_t f;
    unsigned i;

    reset();
    isp_reg_set_ae_win(0, 0x100, 0x200, 0x40, 0x80);
    EQ(R(0x600), 0x00ff007fu, "ae size");
    EQ(R(0x604), 0x00400020u, "ae start");

    isp_reg_set_af_en(0, 0x00070f1fu);
    EQ(R(0x610), 0x0007d475u, "af en bits (mask 0x0007d475)");
    W(0x610, 0xff000000u);
    isp_reg_set_af_en(0, 0x00070f1fu);
    EQ(R(0x610), 0xff07d475u, "af en preserves other bits");

    W(0x610, 0);
    isp_reg_set_af_win(0, 3, 5, 0x100, 0x200, 0x40, 0x80);
    EQ(R(0x610), 0x05180000u, "af win hor/ver num");
    EQ(R(0x614), 0x00ff007fu, "af size");
    EQ(R(0x618), 0x00400020u, "af start");

    isp_reg_set_awb_satur_lim(0, 0x11, 0x22, 0x33);
    EQ(R(0x690), 0x00330011u, "awb satur cfg0");
    EQ(R(0x694), 0x33u, "awb satur cfg1");
    EQ(F(0x690, 0xff, 8), 0, "lim_g never written");

    isp_reg_set_awb_win(0, 0x100, 0x200, 0x40, 0x80);
    EQ(R(0x698), 0x00ff007fu, "awb size");
    EQ(R(0x69c), 0x00400020u, "awb start");

    isp_reg_set_hist_win(0, 0x100, 0x200, 0x40, 0x80);
    EQ(R(0x6c0), 0x00ff007fu, "hist size");
    EQ(R(0x6c4), 0x00200040u, "hist start swapped");

    memset(&f, 0, sizeof(f));
    for (i = 0; i < 6; i++)
        f.iir0_coef[i] = (uint16_t)(i + 1);
    for (i = 0; i < 4; i++)
        f.iir0_s[i] = (uint16_t)(0x11 + i);
    for (i = 0; i < 5; i++)
        f.fir0_coef[i] = (uint16_t)(i + 1);
    f.iir0_dilate = 2;
    f.iir0_ldg_gain = 0x21; f.iir0_ldg_hgain = 0x22;
    f.iir0_ldg_th = 0x23; f.iir0_ldg_hth = 0x24;
    f.fir0_ldg_gain = 0x31; f.fir0_ldg_hgain = 0x32;
    f.fir0_ldg_th = 0x33; f.fir0_ldg_hth = 0x34;
    f.iir0_ldg_lslope = 0x5; f.iir0_ldg_hslope = 0x6;
    f.fir0_ldg_lslope = 0x7; f.fir0_ldg_hslope = 0x8;
    f.iir0_core_th = 0x41; f.iir0_core_peak = 0x42;
    f.fir0_core_th = 0x43; f.fir0_core_peak = 0x44;
    f.iir0_core_slope = 0x9; f.fir0_core_slope = 0xa;
    f.hlt_th = 0x51;
    f.r_offset = 0x123; f.g_offset = 0x456; f.b_offset = 0x789;
    isp_reg_set_af_filter(0, &f);
    EQ(R(0x61c), 0x00300801u, "af iir0 g0-2");
    EQ(R(0x620), 0x00601404u, "af iir0 g3-5");
    EQ(R(0x62c), 0x011u, "af iir0 s0");
    EQ(R(0x630), 0x012u, "af iir0 s1");
    EQ(R(0x634), 0x013u, "af iir0 s2");
    EQ(R(0x638), 0x014u, "af iir0 s3");
    EQ(R(0x63c), 0x05103081u, "af fir0 g0-4");
    EQ(R(0x648), 0x00000002u, "af iir0 dilate");
    EQ(R(0x64c), 0x00002221u, "af iir0 ldg gain");
    EQ(R(0x650), 0x00002423u, "af iir0 ldg th");
    EQ(R(0x654), 0x00003231u, "af fir0 ldg gain");
    EQ(R(0x658), 0x00003433u, "af fir0 ldg th");
    EQ(R(0x65c), 0x00870065u, "af ldg slopes");
    EQ(R(0x660), 0x00420041u, "af iir0 core th/peak");
    EQ(R(0x664), 0x00440043u, "af fir0 core th/peak");
    EQ(R(0x668), 0x00000a09u, "af core slopes");
    EQ(R(0x66c), 0x00000051u, "af hlt_th");
    EQ(R(0x670), 0x04560123u, "af r/g offset");
    EQ(R(0x674), 0x00000789u, "af b offset");
    EQ(R(0x624), 0, "af 0x624 not in descriptor");
    EQ(R(0x628), 0, "af 0x628 not in descriptor");
    EQ(R(0x644), 0, "af 0x644 not in descriptor");
}

static void test_luts(void)
{
    uint8_t src[0x42];
    uint8_t k[ISP_D3D_K_VALUES];
    uint16_t msc[ISP_MSC_LUT_WORDS];
    unsigned i;

    reset();
    for (i = 0; i < sizeof(src); i++)
        src[i] = (uint8_t)(i * 3 + 1);

    isp_reg_set_d3d_lum_th_lut(0, src);
    CHECK(memcmp(g + 0x770, src, 0x42) == 0, "d3d lum th");
    isp_reg_set_d3d_bright_th_lut(0, src);
    CHECK(memcmp(g + 0x7b4, src, 0x42) == 0, "d3d bright th");
    isp_reg_set_d3d_ref_noise_lut(0, src);
    CHECK(memcmp(g + 0x7f8, src, 0x42) == 0, "d3d ref noise");
    isp_reg_set_sharp_val_lut(0, src);
    CHECK(memcmp(g + 0x86c, src, 0x42) == 0, "sharp val");
    isp_reg_set_sharp_edge_lum_lut(0, src);
    CHECK(memcmp(g + 0x8b0, src, 0x42) == 0, "sharp edge lum");
    isp_reg_set_sharp_hfrq_lum_lut(0, src);
    CHECK(memcmp(g + 0x8f4, src, 0x42) == 0, "sharp hfrq lum");
    isp_reg_set_sharp_hsv_lut(0, src);
    CHECK(memcmp(g + 0x938, src, 0x42) == 0, "sharp hsv");
    isp_reg_set_sharp_s_map_lut(0, src);
    CHECK(memcmp(g + 0x994, src, ISP_LUT_SHARP_SMAP_BYTES) == 0, "sharp s-map");
    isp_reg_set_d2d_lp0_np_lut(0, src);
    CHECK(memcmp(g + 0x9b8, src, 0x42) == 0, "d2d lp0");
    isp_reg_set_d2d_lp1_np_lut(0, src);
    CHECK(memcmp(g + 0x9fc, src, 0x42) == 0, "d2d lp1");
    isp_reg_set_d2d_lp2_np_lut(0, src);
    CHECK(memcmp(g + 0xa40, src, 0x42) == 0, "d2d lp2");
    isp_reg_set_d2d_lp3_np_lut(0, src);
    CHECK(memcmp(g + 0xa84, src, 0x42) == 0, "d2d lp3");
    isp_reg_set_af_square_lut(0, src);
    CHECK(memcmp(g + 0xac8, src, 16) == 0, "af square");
    isp_reg_set_lca_pf_satu_lut(0, src);
    CHECK(memcmp(g + 0xb18, src, ISP_LCA_SATU_BYTES) == 0, "lca pf satu");
    isp_reg_set_lca_gf_satu_lut(0, src);
    CHECK(memcmp(g + 0xb3c, src, ISP_LCA_SATU_BYTES) == 0, "lca gf satu");

    for (i = 0; i < ISP_D3D_K_VALUES; i++)
        k[i] = (uint8_t)(i & 0x1f);
    isp_reg_set_d3d_k_lut(0, k);
    for (i = 0; i < ISP_D3D_K_REGS - 1u; i++) {
        uint32_t want = 0;
        unsigned j;
        for (j = 0; j < 6; j++)
            want |= (uint32_t)k[i * 6 + j] << (5 * j);
        EQ(R(0x83c + 4 * i), want, "d3d k reg");
    }
    EQ(R(0x850), (uint32_t)(k[30] & 0x1f) | ((uint32_t)(k[31] & 0x1f) << 5),
       "d3d k 6th reg (2 values)");
    isp_reg_set_d3d_k_delta_lut(0, k);
    for (i = 0; i < ISP_D3D_K_REGS - 1u; i++) {
        uint32_t want = 0;
        unsigned j;
        for (j = 0; j < 6; j++)
            want |= (uint32_t)k[i * 6 + j] << (5 * j);
        EQ(R(0x854 + 4 * i), want, "d3d k delta reg");
    }
    EQ(R(0x868), (uint32_t)(k[30] & 0x1f) | ((uint32_t)(k[31] & 0x1f) << 5),
       "d3d k delta 6th reg (2 values)");

    for (i = 0; i < ISP_MSC_LUT_WORDS; i++)
        msc[i] = (uint16_t)(0x100 + i);
    isp_reg_set_msc_blw_lut(0, msc);
    for (i = 0; i < ISP_MSC_LUT_REGS; i++) {
        uint32_t a = msc[3 * i] & 0x3ffu;
        uint32_t b = msc[3 * i + 1] & 0x3ffu;
        uint32_t c = msc[3 * i + 2] & 0x3ffu;
        uint32_t v = a | (b << 10) | (c << 20);
        EQ(R(0xad8 + 4 * i), v, "msc blw reg (c [29:20])");
    }
    isp_reg_set_msc_blh_lut(0, msc);
    CHECK(R(0xae8) != 0, "msc blh written");
    isp_reg_set_msc_blw_dlt_lut(0, msc);
    CHECK(R(0xaf8) != 0, "msc blw dlt written");
    isp_reg_set_msc_blh_dlt_lut(0, msc);
    CHECK(R(0xb08) != 0, "msc blh dlt written");
}

int main(void)
{
    test_map_fmt_table_top();
    test_module_flags();
    test_modes();
    test_payload_words();
    test_field_preserve();
    test_struct_payloads();
    test_windows_and_af();
    test_luts();

    printf("reg_writers tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
