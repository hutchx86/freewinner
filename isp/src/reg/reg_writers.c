/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * reg_writers.c - clean-room ISP register/hardware writer tier.
 *
 * Independent re-production of the register-writer translation unit from the
 * behaviour-only specification in spec/reglayer2.md section 1, written
 * without access to the deployed object or any earlier transcription (see
 * reimplementation/README.md).
 *
 * Each ISP instance owns an isp_reg_map_t descriptor (include/reg_writers.h).
 * isp_reg_map_load_addr() fills every member with instance_base + hardware
 * offset; every writer resolves the instance, checks its inputs, then
 * read-modify-writes the 32-bit register(s).  Two primitives are used:
 *
 *   put(reg, mask, shift, val): *reg = (*reg & ~(mask<<shift)) | ((val&mask)<<shift)
 *   store(reg, fields, val):    *reg = (*reg & ~fields) | (val & fields)
 *
 * Register offsets are interface facts from the spec; every literal field mask
 * below is copied verbatim from that spec.
 */

#include <string.h>

#include "reg_writers.h"

/* ------------------------------------------------------------------ */
/* Instance descriptors                                                */
/* ------------------------------------------------------------------ */

static isp_reg_map_t g_map[ISP_REG_INSTANCES];
static unsigned char  g_map_valid[ISP_REG_INSTANCES];

static isp_reg_map_t *instance(unsigned long id)
{
    if (id >= (unsigned long)ISP_REG_INSTANCES)
        return NULL;
    if (!g_map_valid[id])
        return NULL;
    return &g_map[id];
}

static void put(uint32_t *reg, uint32_t mask, unsigned shift, uint32_t val)
{
    *reg = (*reg & ~(mask << shift)) | ((val & mask) << shift);
}

static void store(uint32_t *reg, uint32_t fields, uint32_t val)
{
    *reg = (*reg & ~fields) | (val & fields);
}

static void setbit(uint32_t *reg, uint32_t bit, int on)
{
    if (on)
        *reg |= bit;
    else
        *reg &= ~bit;
}

/* ------------------------------------------------------------------ */
/* 1.1 Address map / control                                           */
/* ------------------------------------------------------------------ */

static const uint32_t g_af_filter_off[ISP_AF_FILTER_REGS] = {
    0x61c, 0x620, 0x62c, 0x630, 0x634, 0x638, 0x63c, 0x648, 0x64c,
    0x650, 0x654, 0x658, 0x65c, 0x660, 0x664, 0x668, 0x66c, 0x670,
    0x674
};

void isp_reg_map_load_addr(unsigned long id, void *base)
{
    isp_reg_map_t *m;
    uint8_t *b = (uint8_t *)base;
    unsigned i;

    if (id >= (unsigned long)ISP_REG_INSTANCES || base == NULL)
        return;

    m = &g_map[id];

    m->isp_global_cfg0          = (uint32_t *)(b + 0x000);
    m->isp_global_cfg1          = (uint32_t *)(b + 0x004);
    m->isp_update_ctrl0         = (uint32_t *)(b + 0x020);
    m->isp_top_ctrl             = (uint32_t *)(b + 0x0fc);
    m->isp_s1_cfg               = (uint32_t *)(b + 0x100);
    m->isp_s1_blc_offset0       = (uint32_t *)(b + 0x104);
    m->isp_s1_blc_offset1       = (uint32_t *)(b + 0x108);
    m->isp_s1_dg_gain0          = (uint32_t *)(b + 0x10c);
    m->isp_s1_dg_gain1          = (uint32_t *)(b + 0x110);
    m->isp_module_bypass0       = (uint32_t *)(b + 0x1a0);
    m->isp_module_mode0         = (uint32_t *)(b + 0x1b0);
    m->isp_s0_blc_offset0       = (uint32_t *)(b + 0x1ec);
    m->isp_s0_blc_offset1       = (uint32_t *)(b + 0x1f0);
    m->isp_s0_dg_gain0          = (uint32_t *)(b + 0x0f4);
    m->isp_s0_dg_gain1          = (uint32_t *)(b + 0x0f8);

    m->isp_wdr_cfg0             = (uint32_t *)(b + 0x200);
    m->isp_wdr_cfg1             = (uint32_t *)(b + 0x204);
    m->isp_wdr_cfg2             = (uint32_t *)(b + 0x208);
    m->isp_dpc_cfg0             = (uint32_t *)(b + 0x260);
    m->isp_dpc_cfg1             = (uint32_t *)(b + 0x264);
    m->isp_ctc_cfg0             = (uint32_t *)(b + 0x270);
    m->isp_ctc_cfg1             = (uint32_t *)(b + 0x274);
    m->isp_ctc_cfg2             = (uint32_t *)(b + 0x278);
    m->isp_gca_center           = (uint32_t *)(b + 0x280);
    m->isp_gca_r_para           = (uint32_t *)(b + 0x284);
    m->isp_gca_b_para           = (uint32_t *)(b + 0x288);
    m->isp_gca_ctrl             = (uint32_t *)(b + 0x28c);
    m->isp_d2d_cfg0             = (uint32_t *)(b + 0x2a0);
    m->isp_d2d_cfg1             = (uint32_t *)(b + 0x2a4);
    m->isp_d2d_cfg2             = (uint32_t *)(b + 0x2a8);
    m->isp_d2d_cfg3             = (uint32_t *)(b + 0x2ac);
    m->isp_d3d_cfg0             = (uint32_t *)(b + 0x2d0);
    m->isp_d3d_cfg1             = (uint32_t *)(b + 0x2d4);
    m->isp_d3d_cfg2             = (uint32_t *)(b + 0x2d8);
    m->isp_d3d_cfg3             = (uint32_t *)(b + 0x2dc);
    m->isp_sensor_offset0       = (uint32_t *)(b + 0x340);
    m->isp_sensor_offset1       = (uint32_t *)(b + 0x344);
    m->isp_dg_gain0             = (uint32_t *)(b + 0x360);
    m->isp_dg_gain1             = (uint32_t *)(b + 0x364);
    m->isp_wb_gain0             = (uint32_t *)(b + 0x370);
    m->isp_wb_gain1             = (uint32_t *)(b + 0x374);
    m->isp_wb_cfg0              = (uint32_t *)(b + 0x378);
    m->isp_rsc_cfg0             = (uint32_t *)(b + 0x390);
    m->isp_rsc_cfg1             = (uint32_t *)(b + 0x394);
    m->isp_rsc_cfg2             = (uint32_t *)(b + 0x398);
    m->isp_pltm_cfg0            = (uint32_t *)(b + 0x3b0);
    m->isp_pltm_cfg1            = (uint32_t *)(b + 0x3b4);
    m->isp_pltm_cfg2            = (uint32_t *)(b + 0x3b8);
    m->isp_pltm_cfg3            = (uint32_t *)(b + 0x3bc);
    m->isp_demosaic_cfg0        = (uint32_t *)(b + 0x400);
    m->isp_lca_cor_ratio        = (uint32_t *)(b + 0x410);
    m->isp_lca_det_ctrl0        = (uint32_t *)(b + 0x414);
    m->isp_lca_det_ctrl1        = (uint32_t *)(b + 0x418);
    m->isp_lca_cor_ctrl         = (uint32_t *)(b + 0x41c);
    m->isp_sharp_edge_stren     = (uint32_t *)(b + 0x420);
    m->isp_sharp_hfrq_stren     = (uint32_t *)(b + 0x424);
    m->isp_sharp_diff_cfg       = (uint32_t *)(b + 0x428);
    m->isp_sharp_ref_noise      = (uint32_t *)(b + 0x42c);
    m->isp_sharp_dir_diff_ctrl  = (uint32_t *)(b + 0x430);
    m->isp_sharp_edge_ctrl      = (uint32_t *)(b + 0x434);
    m->isp_sharp_over_shoot_ctrl = (uint32_t *)(b + 0x438);
    m->isp_sharp_under_shoot_ctrl = (uint32_t *)(b + 0x43c);
    m->isp_rgb2rgb_gain0        = (uint32_t *)(b + 0x440);
    m->isp_rgb2rgb_gain1        = (uint32_t *)(b + 0x444);
    m->isp_rgb2rgb_gain2        = (uint32_t *)(b + 0x448);
    m->isp_rgb2rgb_gain3        = (uint32_t *)(b + 0x44c);
    m->isp_rgb2rgb_gain4        = (uint32_t *)(b + 0x450);
    m->isp_rgb2rgb_offset       = (uint32_t *)(b + 0x454);
    m->isp_cnr_cfg0             = (uint32_t *)(b + 0x470);
    m->isp_cnr_cfg1             = (uint32_t *)(b + 0x474);
    m->isp_satu_cfg0            = (uint32_t *)(b + 0x490);
    m->isp_rgb2yuv_gain0        = (uint32_t *)(b + 0x520);
    m->isp_rgb2yuv_gain1        = (uint32_t *)(b + 0x524);
    m->isp_rgb2yuv_gain2        = (uint32_t *)(b + 0x528);
    m->isp_rgb2yuv_gain3        = (uint32_t *)(b + 0x52c);
    m->isp_rgb2yuv_gain4        = (uint32_t *)(b + 0x530);
    m->isp_rgb2yuv_offset0      = (uint32_t *)(b + 0x534);
    m->isp_rgb2yuv_offset1      = (uint32_t *)(b + 0x538);
    m->isp_ae_size              = (uint32_t *)(b + 0x600);
    m->isp_ae_start             = (uint32_t *)(b + 0x604);
    m->isp_af_cfg               = (uint32_t *)(b + 0x610);
    m->isp_af_size              = (uint32_t *)(b + 0x614);
    m->isp_af_start             = (uint32_t *)(b + 0x618);
    for (i = 0; i < ISP_AF_FILTER_REGS; i++)
        m->isp_af_filter[i] = (uint32_t *)(b + g_af_filter_off[i]);
    m->isp_awb_cfg0             = (uint32_t *)(b + 0x690);
    m->isp_awb_cfg1             = (uint32_t *)(b + 0x694);
    m->isp_awb_cfg2             = (uint32_t *)(b + 0x698);
    m->isp_awb_cfg3             = (uint32_t *)(b + 0x69c);
    m->isp_hist_size            = (uint32_t *)(b + 0x6c0);
    m->isp_hist_start           = (uint32_t *)(b + 0x6c4);
    m->isp_afs_cfg0             = (uint32_t *)(b + 0x6e0);

    m->isp_d3d_lum_th_lut       = (uint32_t *)(b + 0x770);
    m->isp_d3d_bright_th_lut    = (uint32_t *)(b + 0x7b4);
    m->isp_d3d_ref_noise_lut    = (uint32_t *)(b + 0x7f8);
    for (i = 0; i < ISP_D3D_K_REGS; i++) {
        m->isp_d3d_k_lut[i]       = (uint32_t *)(b + 0x83c + 4u * i);
        m->isp_d3d_k_delta_lut[i] = (uint32_t *)(b + 0x854 + 4u * i);
    }
    m->isp_sharp_val_lut        = (uint32_t *)(b + 0x86c);
    m->isp_sharp_edge_lum_lut   = (uint32_t *)(b + 0x8b0);
    m->isp_sharp_hfrq_lum_lut   = (uint32_t *)(b + 0x8f4);
    m->isp_sharp_hsv_lut        = (uint32_t *)(b + 0x938);
    m->isp_sharp_s_map_lut      = (uint32_t *)(b + 0x994);
    m->isp_d2d_lp0_np_lut       = (uint32_t *)(b + 0x9b8);
    m->isp_d2d_lp1_np_lut       = (uint32_t *)(b + 0x9fc);
    m->isp_d2d_lp2_np_lut       = (uint32_t *)(b + 0xa40);
    m->isp_d2d_lp3_np_lut       = (uint32_t *)(b + 0xa84);
    m->isp_af_square_lut        = (uint32_t *)(b + 0xac8);
    m->isp_msc_blw_lut          = (uint32_t *)(b + 0xad8);
    m->isp_msc_blh_lut          = (uint32_t *)(b + 0xae8);
    m->isp_msc_blw_dlt_lut      = (uint32_t *)(b + 0xaf8);
    m->isp_msc_blh_dlt_lut      = (uint32_t *)(b + 0xb08);
    m->isp_lca_pf_satu_lut      = (uint32_t *)(b + 0xb18);
    m->isp_lca_gf_satu_lut      = (uint32_t *)(b + 0xb3c);
    m->isp_f00                  = (uint32_t *)(b + 0xf00);

    g_map_valid[id] = 1;
}

void isp_reg_set_input_fmt(unsigned long id, uint32_t fmt)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_global_cfg0, 0x7u, 4, fmt);
}

void isp_reg_update_table(unsigned long id, uint32_t mask)
{
    isp_reg_map_t *m = instance(id);
    uint32_t v;

    if (!m)
        return;
    /* byte0[7:1] <- mask[7:1], byte1[3:0] <- mask[11:8], bit13 <- mask[1] */
    v = (mask & 0x00000ffeu) | ((mask & 0x2u) << 12);
    store(m->isp_update_ctrl0, 0x00002ffeu, v);
}

void isp_reg_top_control(unsigned long id, uint32_t n, uint32_t w)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_top_ctrl, 0x3fffu, 0, w);
    put(m->isp_top_ctrl, 0x1u, 16, (n - 1u) & 1u);
}

void isp_reg_module_enable(unsigned long id, uint32_t flag)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    *m->isp_module_bypass0 |= flag;
    if (flag & 0x40000u)
        setbit(m->isp_s1_cfg, 1u << 0, 1);
    if (flag & 0x2u)
        setbit(m->isp_s1_cfg, 1u << 1, 1);
    if (flag & 0x4000000u)
        setbit(m->isp_s1_cfg, 1u << 2, 1);
    if (flag & 0x4u)
        setbit(m->isp_global_cfg1, 1u << 14, 1);
}

void isp_reg_module_disable(unsigned long id, uint32_t flag)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    *m->isp_module_bypass0 &= ~flag;
    if (flag & 0x40000u)
        setbit(m->isp_s1_cfg, 1u << 0, 0);
    if (flag & 0x2u)
        setbit(m->isp_s1_cfg, 1u << 1, 0);
    if (flag & 0x4000000u)
        setbit(m->isp_s1_cfg, 1u << 2, 0);
    if (flag & 0x4u)
        setbit(m->isp_global_cfg1, 1u << 14, 0);
}

void isp_reg_set_dg_bypass(unsigned long id, int en, uint32_t dg_mode)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    *m->isp_module_bypass0 &= ~(0x80000u | 0x4000000u);
    *m->isp_s1_cfg &= ~(1u << 2);
    if (en) {
        if (dg_mode == 2u) {
            *m->isp_module_bypass0 |= 0x4000000u;
            *m->isp_s1_cfg |= 1u << 2;
        } else {
            *m->isp_module_bypass0 |= 0x80000u;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 1.2 Mode selectors                                                  */
/* ------------------------------------------------------------------ */

void isp_reg_set_wdr_compress_mode(unsigned long id, uint32_t mode)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_global_cfg0, 0x1u, 24, mode);
}

void isp_reg_set_saturation_mode(unsigned long id, uint32_t mode)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_module_mode0, 0x1u, 1, mode);
}

void isp_reg_set_cfa_mode(unsigned long id, uint32_t mode)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_module_mode0, 0x1u, 2, mode);
}

void isp_reg_set_dg_mode(unsigned long id, uint32_t mode)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_module_mode0, 0x1u, 3, mode);
}

void isp_reg_set_ae_mode(unsigned long id, uint32_t mode)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_module_mode0, 0x3u, 4, mode);
}

void isp_reg_set_lsc_mode(unsigned long id, uint32_t mode)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_module_mode0, 0x3u, 16, mode);
}

void isp_reg_set_msc_mode(unsigned long id, uint32_t mode)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_module_mode0, 0x3u, 18, mode);
}

void isp_reg_set_awb_mode(unsigned long id, uint32_t mode)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_module_mode0, 0x1u, 6, mode);
}

void isp_reg_set_hist_src(unsigned long id, uint32_t mode)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_module_mode0, 0x1u, 7, mode);
}

void isp_reg_set_hist_mode(unsigned long id, uint32_t mode)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_module_mode0, 0x3u, 8, mode);
}

void isp_reg_set_dpc_mode(unsigned long id, uint32_t mode)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_module_mode0, 0x3u, 10, mode);
}

void isp_reg_set_d3d_mode(unsigned long id, uint32_t mode)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_module_mode0, 0x3u, 12, mode);
}

void isp_reg_set_af_mode(unsigned long id, uint32_t mode)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_module_mode0, 0x1u, 14, mode);
}

/* ------------------------------------------------------------------ */
/* 1.3 Module payload writers                                          */
/* ------------------------------------------------------------------ */

void isp_reg_set_blc_offset(unsigned long id, uint32_t r, uint32_t gr,
                            uint32_t gb, uint32_t b)
{
    isp_reg_map_t *m = instance(id);
    uint32_t lo, hi;

    if (!m)
        return;
    lo = (r & 0x1fffu) | ((gr & 0x1fffu) << 16);
    hi = (gb & 0x1fffu) | ((b & 0x1fffu) << 16);
    store(m->isp_s1_blc_offset0, 0x1fff1fffu, lo);
    store(m->isp_s1_blc_offset1, 0x1fff1fffu, hi);
    store(m->isp_s0_blc_offset0, 0x1fff1fffu, lo);
    store(m->isp_s0_blc_offset1, 0x1fff1fffu, hi);
}

void isp_reg_set_wdr_cfg(unsigned long id, uint32_t lo_th, uint32_t hi_th,
                         uint32_t exp_ratio, uint32_t slope, uint32_t mv_th,
                         uint32_t mv_scale, uint32_t out_sel)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    store(m->isp_wdr_cfg0, 0xffffffffu,
          (lo_th & 0xffffu) | ((hi_th & 0xffffu) << 16));
    store(m->isp_wdr_cfg1, 0xffffffffu,
          (exp_ratio & 0xffffu) | ((slope & 0xffffu) << 16));
    store(m->isp_wdr_cfg2, 0xc07fffffu,
          (mv_th & 0xffffu) | ((mv_scale & 0x7fu) << 16) |
          ((out_sel & 0x3u) << 30));
}

void isp_reg_set_dpc(unsigned long id, uint32_t r0, uint32_t r1, uint32_t r2,
                     uint32_t r3, uint32_t slope_th, uint32_t cold_abs_th)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    store(m->isp_dpc_cfg0, 0xffffffffu,
          (r0 & 0xffu) | ((r1 & 0xffu) << 8) | ((r2 & 0xffu) << 16) |
          ((r3 & 0xffu) << 24));
    store(m->isp_dpc_cfg1, 0x01ff03ffu,
          (slope_th & 0x3ffu) | ((cold_abs_th & 0x1ffu) << 16));
}

void isp_reg_set_ctc(unsigned long id, const isp_ctc_cfg_t *cfg)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !cfg)
        return;
    store(m->isp_ctc_cfg0, 0x0fff0fffu,
          (cfg->th_min & 0xfffu) | ((cfg->th_max & 0xfffu) << 16));
    store(m->isp_ctc_cfg1, 0xffffu, cfg->slope & 0xffffu);
    store(m->isp_ctc_cfg2, 0x000fff7fu,
          (cfg->dir_wt & 0x7fu) | ((cfg->dir_th & 0xfffu) << 8));
}

void isp_reg_set_gca(unsigned long id, const isp_gca_cfg_t *cfg)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !cfg)
        return;
    store(m->isp_gca_center, 0x1fff1fffu,
          (cfg->ct_h & 0x1fffu) | ((cfg->ct_w & 0x1fffu) << 16));
    store(m->isp_gca_r_para, 0x0fffffffu,
          (cfg->r.para0 & 0xffu) | ((cfg->r.para1 & 0x3ffu) << 8) |
          ((cfg->r.para2 & 0x3ffu) << 18));
    store(m->isp_gca_b_para, 0x0fffffffu,
          (cfg->b.para0 & 0xffu) | ((cfg->b.para1 & 0x3ffu) << 8) |
          ((cfg->b.para2 & 0x3ffu) << 18));
    put(m->isp_gca_ctrl, 0xffu, 0, cfg->r.int_cns);
}

void isp_reg_set_lca(unsigned long id, const isp_lca_cfg_t *cfg)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !cfg)
        return;
    store(m->isp_lca_cor_ratio, 0x03ff03ffu,
          (cfg->gf_cor_ratio & 0x3ffu) | ((cfg->pf_cor_ratio & 0x3ffu) << 16));
    store(m->isp_lca_det_ctrl0, 0x0fff0fffu,
          (cfg->lum_th & 0xfffu) | ((cfg->grad_th & 0xfffu) << 16));
    store(m->isp_lca_det_ctrl1, 0x01fff3ffu,
          (cfg->clr_gth & 0x3ffu) | ((cfg->pf_rshf & 0xfu) << 12) |
          ((cfg->pf_bslp & 0x1ffu) << 16));
    store(m->isp_lca_cor_ctrl, 0x000fffffu,
          (cfg->clrs_lum_th & 0xffu) | ((cfg->pf_clrc_ratio & 0xfu) << 8) |
          ((cfg->gf_clrc_ratio & 0xfu) << 12) |
          ((cfg->pf_decr_ratio & 0xfu) << 16));
}

void isp_reg_set_d2d_cfg(unsigned long id, const isp_d2d_cfg_t *cfg)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !cfg)
        return;
    store(m->isp_d2d_cfg0, 0x00ffffffu,
          (cfg->lf_ratio & 0xffu) | ((cfg->bf_ratio & 0xffu) << 8) |
          ((cfg->hf_ratio & 0xffu) << 16));
    store(m->isp_d2d_cfg1, 0xffffffffu,
          (cfg->lp_core[0] & 0xffu) | ((cfg->lp_core[1] & 0xffu) << 8) |
          ((cfg->lp_core[2] & 0xffu) << 16) |
          ((cfg->lp_core[3] & 0xffu) << 24));
    store(m->isp_d2d_cfg2, 0x00ffffffu,
          (cfg->lp_side[0] & 0xffu) | ((cfg->lp_side[1] & 0xffu) << 8) |
          ((cfg->lp_side[2] & 0xffu) << 16));
    store(m->isp_d2d_cfg3, 0xffffffffu,
          (cfg->lp_pcnt[0] & 0xffu) | ((cfg->lp_pcnt[1] & 0xffu) << 8) |
          ((cfg->lp_pcnt[2] & 0xffu) << 16) |
          ((cfg->lp_pcnt[3] & 0xffu) << 24));
}

void isp_reg_set_d3d_cfg(unsigned long id, const isp_d3d_cfg_t *cfg)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !cfg)
        return;
    store(m->isp_d3d_cfg0, 0xffffffffu,
          (cfg->bright_diff & 0xffu) | ((cfg->clip_ratio & 0xffu) << 8) |
          ((cfg->lum_diff_clip & 0xffu) << 16) |
          ((cfg->noise_clip & 0xffu) << 24));
    store(m->isp_d3d_cfg1, 0x031f00ffu,
          (cfg->st_2d & 0xffu) | ((cfg->mv_ori & 0x1fu) << 16) |
          ((cfg->ltf_en & 1u) << 24) | ((cfg->rec_en & 1u) << 25));
    store(m->isp_d3d_cfg2, 0x0fff0fffu,
          (cfg->c_weight1 & 0xfffu) | ((cfg->c_weight2 & 0xfffu) << 16));
    store(m->isp_d3d_cfg3, 0x03ff0fffu,
          (cfg->c_weight3 & 0xfffu) | ((cfg->ltf_update_frm & 0x3ffu) << 16));
}

void isp_reg_set_sensor_offset(unsigned long id, uint32_t r, uint32_t gr,
                               uint32_t gb, uint32_t b)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    store(m->isp_sensor_offset0, 0x1fff1fffu,
          (r & 0x1fffu) | ((gr & 0x1fffu) << 16));
    store(m->isp_sensor_offset1, 0x1fff1fffu,
          (gb & 0x1fffu) | ((b & 0x1fffu) << 16));
}

void isp_reg_set_dg_gain(unsigned long id, uint32_t r, uint32_t gr, uint32_t gb,
                         uint32_t b)
{
    isp_reg_map_t *m = instance(id);
    uint32_t g0, g1;

    if (!m)
        return;
    g0 = (r & 0xffffu) | ((gr & 0xffffu) << 16);
    g1 = (gb & 0xffffu) | ((b & 0xffffu) << 16);
    store(m->isp_dg_gain0, 0xffffffffu, g0);
    store(m->isp_dg_gain1, 0xffffffffu, g1);
    store(m->isp_s1_dg_gain0, 0xffffffffu, g0);
    store(m->isp_s1_dg_gain1, 0xffffffffu, g1);
    store(m->isp_s0_dg_gain0, 0xffffffffu, g0);
    store(m->isp_s0_dg_gain1, 0xffffffffu, g1);
}

void isp_reg_set_wb_gain(unsigned long id, uint32_t r, uint32_t gr, uint32_t gb,
                         uint32_t b)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    store(m->isp_wb_gain0, 0x0fff0fffu, (r & 0xfffu) | ((gr & 0xfffu) << 16));
    store(m->isp_wb_gain1, 0x0fff0fffu, (gb & 0xfffu) | ((b & 0xfffu) << 16));
}

void isp_reg_set_wb_clip(unsigned long id, uint32_t clip)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    store(m->isp_wb_cfg0, 0x00000fffu, clip & 0xfffu);
}

void isp_reg_set_lsc(unsigned long id, uint32_t ct_x, uint32_t ct_y,
                     uint32_t rs_val)
{
    isp_reg_map_t *m = instance(id);
    uint32_t v;

    if (!m)
        return;
    v = (ct_x & 0x1fffu) | ((ct_y & 0x1fffu) << 13) |
        ((rs_val & 0x1fu) << 26);
    store(m->isp_rsc_cfg0, 0x7fffffffu, v);
    store(m->isp_rsc_cfg1, 0x7fffffffu, v);
    store(m->isp_rsc_cfg2, 0x7fffffffu, v);
}

void isp_reg_set_pltm_cfg(unsigned long id, const isp_pltm_cfg_t *cfg)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !cfg)
        return;
    store(m->isp_pltm_cfg0, 0xff0f0f07u,
          (cfg->lss_switch & 0x1u) | ((cfg->cal_en & 0x1u) << 1) |
          ((cfg->frm_sm_en & 0x1u) << 2) |
          ((cfg->last_order_ratio & 0xfu) << 8) |
          ((cfg->tr_order & 0xfu) << 16) |
          ((cfg->oripic_ratio & 0xffu) << 24));
    store(m->isp_pltm_cfg1, 0xffffffffu,
          cfg->intens_asym | (cfg->spatial_asm << 8) |
          ((cfg->white_level & 0xffu) << 16) |
          ((cfg->lp_halo_res & 0xfu) << 24) |
          ((cfg->lum_ratio & 0xfu) << 28));
    store(m->isp_pltm_cfg2, 0x1f1fffffu,
          cfg->block_height | (cfg->block_width << 8) |
          ((cfg->block_v_num & 0x1fu) << 16) |
          ((cfg->block_h_num & 0x1fu) << 24));
    store(m->isp_pltm_cfg3, 0xffffffffu, cfg->statistic_div);
}

void isp_reg_set_cfa(unsigned long id, uint32_t dir_th, uint32_t interp_mode,
                     uint32_t zig_zag)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    store(m->isp_demosaic_cfg0, 0x00f11fffu,
          (dir_th & 0x1fffu) | ((interp_mode & 1u) << 16) |
          ((zig_zag & 0xfu) << 20));
}

void isp_reg_set_sharp(unsigned long id, const isp_sharp_cfg_t *cfg)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !cfg)
        return;
    store(m->isp_sharp_edge_stren, 0x0fff0fffu,
          (cfg->edge_black_stren & 0xfffu) |
          ((cfg->edge_white_stren & 0xfffu) << 16));
    store(m->isp_sharp_hfrq_stren, 0x0fff0fffu,
          (cfg->hfrq_black_stren & 0xfffu) |
          ((cfg->hfrq_white_stren & 0xfffu) << 16));
    store(m->isp_sharp_diff_cfg, 0x1f1f1f1fu,
          (cfg->edge_scale & 0x1fu) | ((cfg->hfrq_scale & 0x1fu) << 8) |
          ((cfg->scale_ratio & 0x1fu) << 16) |
          ((cfg->conv_ratio & 0x1fu) << 24));
    store(m->isp_sharp_ref_noise, 0x0fff0fffu,
          (cfg->ns_lw_th & 0xfffu) | ((cfg->ns_hi_th & 0xfffu) << 16));
    store(m->isp_sharp_dir_diff_ctrl, 0x0fff0fffu,
          (cfg->dir_clip_val & 0xfffu) | ((cfg->dir_eq_ratio & 0xfffu) << 16));
    store(m->isp_sharp_edge_ctrl, 0x1f1f00ffu,
          (cfg->edge_th & 0xffu) | ((cfg->hv_edge_sm & 0x1fu) << 16) |
          ((cfg->aa_edge_sm & 0x1fu) << 24));
    store(m->isp_sharp_over_shoot_ctrl, 0x03ff03ffu,
          (cfg->over_val & 0x3ffu) | ((cfg->over_area & 0x3ffu) << 16));
    store(m->isp_sharp_under_shoot_ctrl, 0x03ff03ffu,
          (cfg->under_val & 0x3ffu) | ((cfg->under_area & 0x3ffu) << 16));
}

void isp_reg_set_rgb2rgb_gain_offset(unsigned long id, const uint16_t gain[9],
                                     const uint16_t offset[3])
{
    isp_reg_map_t *m = instance(id);
    if (!m || !gain || !offset)
        return;
    store(m->isp_rgb2rgb_gain0, 0x0fff0fffu, gain[0] | (gain[1] << 16));
    store(m->isp_rgb2rgb_gain1, 0x0fff0fffu, gain[2] | (gain[3] << 16));
    store(m->isp_rgb2rgb_gain2, 0x0fff0fffu, gain[4] | (gain[5] << 16));
    store(m->isp_rgb2rgb_gain3, 0x0fff0fffu, gain[6] | (gain[7] << 16));
    store(m->isp_rgb2rgb_gain4, 0x1fff0fffu,
          (gain[8] & 0xfffu) | ((offset[0] & 0x1fffu) << 16));
    store(m->isp_rgb2rgb_offset, 0x1fff1fffu,
          (offset[1] & 0x1fffu) | ((offset[2] & 0x1fffu) << 16));
}

void isp_reg_set_cnr(unsigned long id, uint32_t c_th, uint32_t y_th,
                     uint32_t st_v_y, uint32_t st_h_y)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    store(m->isp_cnr_cfg0, 0x0fff0fffu, (c_th & 0xfffu) | ((y_th & 0xfffu) << 16));
    store(m->isp_cnr_cfg1, 0x0fff0fffu,
          (st_v_y & 0xfffu) | ((st_h_y & 0xfffu) << 16));
}

void isp_reg_set_saturation(unsigned long id, uint32_t r, uint32_t g, uint32_t b)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    store(m->isp_satu_cfg0, 0x00000fffu,
          (r & 0xfu) | ((g & 0xfu) << 4) | ((b & 0xfu) << 8));
}

void isp_reg_set_dehaze(unsigned long id)
{
    (void)id;
}

void isp_reg_set_rgb2yuv_gain_offset(unsigned long id, const uint16_t gain[9],
                                     const uint16_t offset[3])
{
    isp_reg_map_t *m = instance(id);
    if (!m || !gain || !offset)
        return;
    store(m->isp_rgb2yuv_gain0, 0x07ff07ffu, gain[0] | (gain[1] << 16));
    store(m->isp_rgb2yuv_gain1, 0x07ff07ffu, gain[2] | (gain[3] << 16));
    store(m->isp_rgb2yuv_gain2, 0x07ff07ffu, gain[4] | (gain[5] << 16));
    store(m->isp_rgb2yuv_gain3, 0x07ff07ffu, gain[6] | (gain[7] << 16));
    store(m->isp_rgb2yuv_gain4, 0x000007ffu, gain[8] & 0x7ffu);
    store(m->isp_rgb2yuv_offset0, 0x07ff07ffu,
          (offset[0] & 0x7ffu) | ((offset[1] & 0x7ffu) << 16));
    store(m->isp_rgb2yuv_offset1, 0x000007ffu, offset[2] & 0x7ffu);
}

void isp_reg_set_ae_win(unsigned long id, uint32_t width, uint32_t height,
                        uint32_t hor_start, uint32_t ver_start)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    store(m->isp_ae_size, 0x0fff0fffu,
          (((width >> 1) - 1u) & 0xfffu) |
          ((((height >> 1) - 1u) & 0xfffu) << 16));
    store(m->isp_ae_start, 0x0fff0fffu,
          ((hor_start >> 1) & 0xfffu) | (((ver_start >> 1) & 0xfffu) << 16));
}

void isp_reg_set_af_en(unsigned long id, uint32_t en_bits)
{
    static const unsigned char src_bit[12] = {
        0, 1, 2, 3, 4, 8, 9, 10, 11, 16, 17, 18
    };
    static const unsigned char hw_bit[12] = {
        0, 2, 4, 5, 6, 10, 12, 14, 15, 16, 17, 18
    };
    isp_reg_map_t *m = instance(id);
    uint32_t hw = 0;
    unsigned i;

    if (!m)
        return;
    for (i = 0; i < 12; i++) {
        if (en_bits & (1u << src_bit[i]))
            hw |= 1u << hw_bit[i];
    }
    *m->isp_af_cfg = (*m->isp_af_cfg & ~0x0007d475u) | (hw & 0x0007d475u);
}

void isp_reg_set_af_win(unsigned long id, uint32_t hor_num, uint32_t ver_num,
                        uint32_t width, uint32_t height, uint32_t hor_start,
                        uint32_t ver_start)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_af_cfg, 0x1fu, 19, hor_num);
    put(m->isp_af_cfg, 0x1fu, 24, ver_num);
    store(m->isp_af_size, 0x0fff0fffu,
          (((width >> 1) - 1u) & 0xfffu) |
          ((((height >> 1) - 1u) & 0xfffu) << 16));
    store(m->isp_af_start, 0x0fff0fffu,
          ((hor_start >> 1) & 0xfffu) | (((ver_start >> 1) & 0xfffu) << 16));
}

void isp_reg_set_af_filter(unsigned long id, const isp_af_filter_t *f)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !f)
        return;
    store(m->isp_af_filter[0], 0x3fffffffu,
          (f->iir0_coef[0] & 0x3ffu) | ((f->iir0_coef[1] & 0x3ffu) << 10) |
          ((f->iir0_coef[2] & 0x3ffu) << 20));
    store(m->isp_af_filter[1], 0x3fffffffu,
          (f->iir0_coef[3] & 0x3ffu) | ((f->iir0_coef[4] & 0x3ffu) << 10) |
          ((f->iir0_coef[5] & 0x3ffu) << 20));
    store(m->isp_af_filter[2], 0x3ffu, f->iir0_s[0] & 0x3ffu);
    store(m->isp_af_filter[3], 0x3ffu, f->iir0_s[1] & 0x3ffu);
    store(m->isp_af_filter[4], 0x3ffu, f->iir0_s[2] & 0x3ffu);
    store(m->isp_af_filter[5], 0x3ffu, f->iir0_s[3] & 0x3ffu);
    store(m->isp_af_filter[6], 0x3fffffffu,
          (f->fir0_coef[0] & 0x3fu) | ((f->fir0_coef[1] & 0x3fu) << 6) |
          ((f->fir0_coef[2] & 0x3fu) << 12) |
          ((f->fir0_coef[3] & 0x3fu) << 18) |
          ((f->fir0_coef[4] & 0x3fu) << 24));
    store(m->isp_af_filter[7], 0x3u, f->iir0_dilate & 0x3u);
    store(m->isp_af_filter[8], 0xffffu,
          (f->iir0_ldg_gain & 0xffu) | ((f->iir0_ldg_hgain & 0xffu) << 8));
    store(m->isp_af_filter[9], 0xffffu,
          (f->iir0_ldg_th & 0xffu) | ((f->iir0_ldg_hth & 0xffu) << 8));
    store(m->isp_af_filter[10], 0xffffu,
          (f->fir0_ldg_gain & 0xffu) | ((f->fir0_ldg_hgain & 0xffu) << 8));
    store(m->isp_af_filter[11], 0xffffu,
          (f->fir0_ldg_th & 0xffu) | ((f->fir0_ldg_hth & 0xffu) << 8));
    store(m->isp_af_filter[12], 0x00ff00ffu,
          (f->iir0_ldg_lslope & 0xfu) | ((f->iir0_ldg_hslope & 0xfu) << 4) |
          ((f->fir0_ldg_lslope & 0xfu) << 16) |
          ((f->fir0_ldg_hslope & 0xfu) << 20));
    store(m->isp_af_filter[13], 0x00ff00ffu,
          (f->iir0_core_th & 0xffu) | ((f->iir0_core_peak & 0xffu) << 16));
    store(m->isp_af_filter[14], 0x00ff00ffu,
          (f->fir0_core_th & 0xffu) | ((f->fir0_core_peak & 0xffu) << 16));
    store(m->isp_af_filter[15], 0x00000f0fu,
          (f->iir0_core_slope & 0xfu) | ((f->fir0_core_slope & 0xfu) << 8));
    store(m->isp_af_filter[16], 0xffu, f->hlt_th & 0xffu);
    store(m->isp_af_filter[17], 0x1fff1fffu,
          (f->r_offset & 0x1fffu) | ((f->g_offset & 0x1fffu) << 16));
    store(m->isp_af_filter[18], 0x1fffu, f->b_offset & 0x1fffu);
}

void isp_reg_set_awb_satur_lim(unsigned long id, uint32_t lim_r, uint32_t lim_g,
                               uint32_t lim_b)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    (void)lim_g;    /* the spec's deployed behaviour never programs lim_g */
    put(m->isp_awb_cfg0, 0xffu, 0, lim_r);
    put(m->isp_awb_cfg0, 0xffu, 16, lim_b);
    put(m->isp_awb_cfg1, 0xffu, 0, lim_b);
}

void isp_reg_set_awb_win(unsigned long id, uint32_t width, uint32_t height,
                         uint32_t hor_start, uint32_t ver_start)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    store(m->isp_awb_cfg2, 0x01ff01ffu,
          (((width >> 1) - 1u) & 0x1ffu) |
          ((((height >> 1) - 1u) & 0x1ffu) << 16));
    store(m->isp_awb_cfg3, 0x07ff07ffu,
          ((hor_start >> 1) & 0x7ffu) | (((ver_start >> 1) & 0x7ffu) << 16));
}

void isp_reg_set_hist_win(unsigned long id, uint32_t width, uint32_t height,
                          uint32_t hor_start, uint32_t ver_start)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    store(m->isp_hist_size, 0x0fff0fffu,
          (((width >> 1) - 1u) & 0xfffu) |
          ((((height >> 1) - 1u) & 0xfffu) << 16));
    /* start halves are swapped relative to the AE/AF windows */
    store(m->isp_hist_start, 0x0fff0fffu,
          ((ver_start >> 1) & 0xfffu) | (((hor_start >> 1) & 0xfffu) << 16));
}

void isp_reg_set_afs_anti_flick(unsigned long id, uint32_t line_inc)
{
    isp_reg_map_t *m = instance(id);
    if (!m)
        return;
    put(m->isp_afs_cfg0, 0x3fu, 0, line_inc);
}

/* ------------------------------------------------------------------ */
/* 1.4 LUT writers                                                     */
/* ------------------------------------------------------------------ */

void isp_reg_set_d3d_lum_th_lut(unsigned long id, const void *src)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !src)
        return;
    memcpy(m->isp_d3d_lum_th_lut, src, ISP_LUT_TH_BYTES);
}

void isp_reg_set_d3d_bright_th_lut(unsigned long id, const void *src)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !src)
        return;
    memcpy(m->isp_d3d_bright_th_lut, src, ISP_LUT_TH_BYTES);
}

void isp_reg_set_d3d_ref_noise_lut(unsigned long id, const void *src)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !src)
        return;
    memcpy(m->isp_d3d_ref_noise_lut, src, ISP_LUT_TH_BYTES);
}

static void d3d_k_write(uint32_t *reg[ISP_D3D_K_REGS], const uint8_t *vals)
{
    unsigned i, k;

    for (i = 0; i < ISP_D3D_K_REGS - 1u; i++) {
        uint32_t w = 0;
        for (k = 0; k < 6u; k++)
            w |= (uint32_t)(vals[6u * i + k] & 0x1fu) << (5u * k);
        store(reg[i], 0x3fffffffu, w);
    }
    store(reg[ISP_D3D_K_REGS - 1u], 0x3ffu,
          (uint32_t)(vals[30] & 0x1fu) | ((uint32_t)(vals[31] & 0x1fu) << 5));
}

void isp_reg_set_d3d_k_lut(unsigned long id, const uint8_t *vals)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !vals)
        return;
    d3d_k_write(m->isp_d3d_k_lut, vals);
}

void isp_reg_set_d3d_k_delta_lut(unsigned long id, const uint8_t *vals)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !vals)
        return;
    d3d_k_write(m->isp_d3d_k_delta_lut, vals);
}

void isp_reg_set_sharp_val_lut(unsigned long id, const void *src)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !src)
        return;
    memcpy(m->isp_sharp_val_lut, src, ISP_LUT_TH_BYTES);
}

void isp_reg_set_sharp_edge_lum_lut(unsigned long id, const void *src)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !src)
        return;
    memcpy(m->isp_sharp_edge_lum_lut, src, ISP_LUT_TH_BYTES);
}

void isp_reg_set_sharp_hfrq_lum_lut(unsigned long id, const void *src)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !src)
        return;
    memcpy(m->isp_sharp_hfrq_lum_lut, src, ISP_LUT_TH_BYTES);
}

void isp_reg_set_sharp_hsv_lut(unsigned long id, const void *src)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !src)
        return;
    memcpy(m->isp_sharp_hsv_lut, src, ISP_LUT_TH_BYTES);
}

void isp_reg_set_sharp_s_map_lut(unsigned long id, const void *src)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !src)
        return;
    memcpy(m->isp_sharp_s_map_lut, src, ISP_LUT_SHARP_SMAP_BYTES);
}

void isp_reg_set_d2d_lp0_np_lut(unsigned long id, const void *src)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !src)
        return;
    memcpy(m->isp_d2d_lp0_np_lut, src, ISP_LUT_TH_BYTES);
}

void isp_reg_set_d2d_lp1_np_lut(unsigned long id, const void *src)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !src)
        return;
    memcpy(m->isp_d2d_lp1_np_lut, src, ISP_LUT_TH_BYTES);
}

void isp_reg_set_d2d_lp2_np_lut(unsigned long id, const void *src)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !src)
        return;
    memcpy(m->isp_d2d_lp2_np_lut, src, ISP_LUT_TH_BYTES);
}

void isp_reg_set_d2d_lp3_np_lut(unsigned long id, const void *src)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !src)
        return;
    memcpy(m->isp_d2d_lp3_np_lut, src, ISP_LUT_TH_BYTES);
}

void isp_reg_set_af_square_lut(unsigned long id, const void *src)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !src)
        return;
    memcpy(m->isp_af_square_lut, src, ISP_AF_SQUARE_WORDS * 4u);
}

static void msc_write(uint32_t *reg, const uint16_t *vals)
{
    unsigned j;

    for (j = 0; j < ISP_MSC_LUT_REGS; j++) {
        uint32_t w = (vals[3u * j] & 0x3ffu) |
                     ((vals[3u * j + 1u] & 0x3ffu) << 10) |
                     ((vals[3u * j + 2u] & 0x3ffu) << 20);
        store(reg + j, 0x3fffffffu, w);
    }
}

void isp_reg_set_msc_blw_lut(unsigned long id, const uint16_t *vals)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !vals)
        return;
    msc_write(m->isp_msc_blw_lut, vals);
}

void isp_reg_set_msc_blh_lut(unsigned long id, const uint16_t *vals)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !vals)
        return;
    msc_write(m->isp_msc_blh_lut, vals);
}

void isp_reg_set_msc_blw_dlt_lut(unsigned long id, const uint16_t *vals)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !vals)
        return;
    msc_write(m->isp_msc_blw_dlt_lut, vals);
}

void isp_reg_set_msc_blh_dlt_lut(unsigned long id, const uint16_t *vals)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !vals)
        return;
    msc_write(m->isp_msc_blh_dlt_lut, vals);
}

void isp_reg_set_lca_pf_satu_lut(unsigned long id, const void *src)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !src)
        return;
    memcpy(m->isp_lca_pf_satu_lut, src, ISP_LCA_SATU_BYTES);
}

void isp_reg_set_lca_gf_satu_lut(unsigned long id, const void *src)
{
    isp_reg_map_t *m = instance(id);
    if (!m || !src)
        return;
    memcpy(m->isp_lca_gf_satu_lut, src, ISP_LCA_SATU_BYTES);
}
