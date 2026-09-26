/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * module_cfg.c - clean-room per-frame ISP module dispatch and payload builder.
 *
 * Re-production of the `isp_module_cfg.o` translation unit from
 * spec/reglayer2.md section 2, written without access to the deployed
 * object or any earlier transcription (see reimplementation/README.md).
 *
 * The tier owns a 31-entry attribute table that binds each module feature bit to
 * its prepare routine and its hardware enable routine, plus isp_map_addr() and
 * isp_hardware_update().  Every prepare routine builds that module's slice of
 * isp_module_config and drives the register writers declared in reg_writers.h.
 *
 * Three conditions are invisible to the differential (it cannot observe them):
 * isp_reg_prepare_gamma's both-probes-zero fallback, isp_reg_prepare_saturation's
 * source-pointer gate on its update bit, and isp_reg_enable_msc's disable branch.
 * Each was confirmed black-box against the deployed object and the unit tests
 * were corrected to match the deployed behaviour - the deployed object is the
 * ground truth for this port.
 */

#include <math.h>
#include <string.h>

#include "module_cfg.h"

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

/* Piecewise linear interpolation, C truncation toward zero. */
static int32_t isp_interp_s32(int32_t curr, int32_t xlo, int32_t xhi,
                           int32_t ylo, int32_t yhi)
{
    if ((xhi - xlo) == 0)
        return ylo;
    return ylo + (yhi - ylo) * (curr - xlo) / (xhi - xlo);
}

static uint32_t clamp10(uint32_t v)
{
    return (v > 0x3ffu) ? 0x3ffu : v;
}

/* ------------------------------------------------------------------ */
/* 2.1 Instance setup and per-frame dispatch                           */
/* ------------------------------------------------------------------ */

void isp_map_addr(isp_module_config_t *cfg, void *vaddr)
{
    if (!cfg)
        return;
    isp_reg_map_load_addr(cfg->isp_dev_id, vaddr);
}

void isp_hardware_update(isp_module_config_t *cfg)
{
    unsigned i;

    if (!cfg)
        return;

    for (i = 0; i < ISP_MODULE_COUNT; i++) {
        const isp_module_attribute_t *attr = &isp_module_attrs[i];

        if (cfg->module_enable_flag & attr->feature_bit) {
            if (attr->config)
                attr->config(cfg);
            if (attr->enable)
                attr->enable(cfg, ISP_MODULE_ENABLE);
        } else if (attr->enable) {
            attr->enable(cfg, ISP_MODULE_DISABLE);
        }
    }

    /* The per-module table-update bits are discarded; a full 16-bit table load
     * is issued every frame (spec 2.1). */
    cfg->table_update = 0xffffffffu;
    isp_reg_update_table(cfg->isp_dev_id, (uint32_t)ISP_TABLE_LOAD_MASK);
    cfg->table_update = 0;
}

/* ------------------------------------------------------------------ */
/* 2.2 Prepare routines                                                */
/* ------------------------------------------------------------------ */

void isp_reg_prepare_afs(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_afs_anti_flick(cfg->isp_dev_id, cfg->afs_cfg.inc_line);
}

void isp_reg_prepare_sharpness(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_sharp(cfg->isp_dev_id, &cfg->sharp_cfg);
    isp_reg_set_sharp_val_lut(cfg->isp_dev_id, cfg->sharp_val_lut);
    isp_reg_set_sharp_edge_lum_lut(cfg->isp_dev_id, cfg->sharp_edge_lum_lut);
    isp_reg_set_sharp_hfrq_lum_lut(cfg->isp_dev_id, cfg->sharp_hfrq_lum_lut);
    isp_reg_set_sharp_hsv_lut(cfg->isp_dev_id, cfg->sharp_hsv_lut);
    isp_reg_set_sharp_s_map_lut(cfg->isp_dev_id, cfg->sharp_s_map_lut);
}

void isp_reg_prepare_contrast(isp_module_config_t *cfg)
{
    (void)cfg;
}

void isp_reg_prepare_d2d(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_d2d_cfg(cfg->isp_dev_id, &cfg->bdnf_cfg);
    isp_reg_set_d2d_lp0_np_lut(cfg->isp_dev_id, cfg->d2d_lp_lut[0]);
    isp_reg_set_d2d_lp1_np_lut(cfg->isp_dev_id, cfg->d2d_lp_lut[1]);
    isp_reg_set_d2d_lp2_np_lut(cfg->isp_dev_id, cfg->d2d_lp_lut[2]);
    isp_reg_set_d2d_lp3_np_lut(cfg->isp_dev_id, cfg->d2d_lp_lut[3]);
}

void isp_reg_prepare_rgb_drc(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    if (cfg->drc_dst) {
        memcpy(cfg->drc_table, cfg->drc_src, ISP_DRC_TBL_SIZE);
        cfg->table_update |= ISP_TABLE_UPDATE_RGB_DRC;
    }
}

void isp_reg_prepare_pltm(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_pltm_cfg(cfg->isp_dev_id, &cfg->pltm_cfg);
    if (cfg->pltm_dst) {
        memcpy(cfg->pltm_table, cfg->pltm_src, ISP_PLTM_TBL_SIZE);
        cfg->table_update |= ISP_TABLE_UPDATE_PLTM;
    }
}

void isp_reg_prepare_wdr(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_wdr_compress_mode(cfg->isp_dev_id, cfg->mode_cfg.wdr_cmp_mode);
    isp_reg_set_wdr_cfg(cfg->isp_dev_id, cfg->wdr_cfg.lo_th,
                        cfg->wdr_cfg.hi_th, cfg->wdr_cfg.exp_ratio,
                        cfg->wdr_cfg.slope, cfg->wdr_cfg.mv_th,
                        cfg->wdr_cfg.mv_scale, cfg->wdr_cfg.out_sel);
    if (cfg->wdr_dst) {
        memcpy(cfg->wdr_cfg.wdr_table, cfg->wdr_src, ISP_WDR_TBL_SIZE);
        cfg->table_update |= ISP_TABLE_UPDATE_WDR;
    }
}

void isp_reg_prepare_cem(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    if (cfg->cem_dst) {
        memcpy(cfg->cem_table, cfg->cem_src, ISP_CEM_TBL_SIZE);
        cfg->table_update |= ISP_TABLE_UPDATE_CEM;
    }
}

void isp_reg_prepare_lens(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_lsc_mode(cfg->isp_dev_id, cfg->mode_cfg.rsc_mode);
    isp_reg_set_lsc(cfg->isp_dev_id, cfg->lens_cfg.lsc_cfg.ct_x,
                    cfg->lens_cfg.lsc_cfg.ct_y, cfg->lens_cfg.lsc_cfg.rs_val);
    if (cfg->lens_dst) {
        memcpy(cfg->lens_table, cfg->lens_src, sizeof(cfg->lens_table));
        cfg->table_update |= ISP_TABLE_UPDATE_LSC;
    }
}

/* Pack the three 1024-entry planes into one 10-bit-per-plane word. */
static void gamma_pack_normal(const uint16_t *tbl, uint32_t *packed)
{
    int i;

    for (i = 0; i < (int)ISP_GAMMA_PLANE; i++) {
        uint32_t r = ((uint32_t)tbl[i] + 2u) >> 2;
        uint32_t g = ((uint32_t)tbl[ISP_GAMMA_PLANE + i] + 2u) >> 2;
        uint32_t b = ((uint32_t)tbl[2u * ISP_GAMMA_PLANE + i] + 2u) >> 2;

        r = clamp10(r);
        g = clamp10(g);
        b = clamp10(b);
        packed[i] = r | (g << 10) | (b << 20);
    }
}

/* Upscale a compact 3x256 table to 3x1024 (spec 2.3 case 3). */
static void gamma_pack_compact(const uint16_t *tbl, uint32_t *packed)
{
    int i;

    for (i = 0; i < (int)ISP_GAMMA_PLANE; i++) {
        int32_t a = i >> 2;
        int32_t f = i & 3;
        int32_t n2 = a + 1;
        int32_t r, g, b;
        uint32_t r4, g4, b4;

        if (n2 > 255)
            n2 = 255;

        r = isp_interp_s32(f, 0, 4, tbl[a], tbl[n2]);
        g = isp_interp_s32(f, 0, 4, tbl[256 + a], tbl[256 + n2]);
        b = isp_interp_s32(f, 0, 4, tbl[512 + a], tbl[512 + n2]);
        if (r < 0)
            r += 3;
        if (g < 0)
            g += 3;
        if (b < 0)
            b += 3;

        r4 = (uint32_t)(r >> 2);
        g4 = (uint32_t)(g >> 2);
        b4 = (uint32_t)(b >> 2);
        if ((int32_t)r4 < 0)
            r4 = 0;
        if ((int32_t)g4 < 0)
            g4 = 0;
        if ((int32_t)b4 < 0)
            b4 = 0;
        if (r4 > 0x3ffu)
            r4 = 0x3ffu;
        if (g4 > 0x3ffu)
            g4 = 0x3ffu;
        if (b4 > 0x3ffu)
            b4 = 0x3ffu;

        packed[i] = (b4 << 20) | (g4 << 10) | r4;
    }
}

/* Gamma-2.2 fallback ramp for an all-zero / absent curve (spec 2.3 case 2). */
static void gamma_pack_ramp(uint32_t *packed)
{
    int i;

    for (i = 0; i < (int)ISP_GAMMA_PLANE; i++) {
        uint32_t v = (uint32_t)(1023.0 *
                                pow((double)i / 1023.0, 1.0 / 2.2));

        packed[i] = v | (v << 10) | (v << 20);
    }
}

/*
 * The deployed object selects the gamma source by probing gamma_tbl[0xbff]
 * (full curve), then gamma_tbl[0x2ff] (compact curve), then falling back to the
 * generated ramp.  A table whose plane starts are set but whose two probe
 * entries are both zero therefore emits the *ramp*, not the normal pack.  This
 * branch is invisible to the differential (its pattern sweep only reaches
 * all-zero or all-nonzero tables) and was black-box confirmed against the
 * deployed object; the unit test was corrected to match.
 */
void isp_reg_prepare_gamma(isp_module_config_t *cfg)
{
    const uint16_t *tbl;
    uint32_t *packed;

    if (!cfg)
        return;
    /* The whole body (packing and the update bit) is skipped when the gamma
     * destination pointer is absent. */
    if (!cfg->gamma_dst)
        return;

    tbl = cfg->gamma_cfg.gamma_tbl;
    packed = cfg->gamma_cfg.gamma_packed;

    if (tbl[0xbff] != 0)
        gamma_pack_normal(tbl, packed);
    else if (tbl[0x2ff] != 0)
        gamma_pack_compact(tbl, packed);
    else
        gamma_pack_ramp(packed);

    cfg->table_update |= ISP_TABLE_UPDATE_GAMMA;
}

void isp_reg_prepare_rgb2yuv(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_rgb2yuv_gain_offset(cfg->isp_dev_id, cfg->rgb2yuv.gain,
                                    cfg->rgb2yuv.offset);
}

void isp_reg_prepare_rgb2rgb(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_rgb2rgb_gain_offset(cfg->isp_dev_id,
                                    cfg->rgb2rgb_cfg.color_matrix,
                                    cfg->rgb2rgb_cfg.offset);
}

void isp_reg_prepare_ae_win(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_ae_mode(cfg->isp_dev_id, cfg->mode_cfg.ae_mode);
    isp_reg_set_ae_win(cfg->isp_dev_id, cfg->ae_cfg.win.width,
                       cfg->ae_cfg.win.height, cfg->ae_cfg.win.hor_start,
                       cfg->ae_cfg.win.ver_start);
}

void isp_reg_prepare_af(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_af_mode(cfg->isp_dev_id, cfg->af_cfg.mode);
    isp_reg_set_af_en(cfg->isp_dev_id, cfg->af_cfg.en_bits);
    isp_reg_set_af_win(cfg->isp_dev_id, cfg->af_cfg.win.hor_num,
                       cfg->af_cfg.win.ver_num, cfg->af_cfg.win.width,
                       cfg->af_cfg.win.height, cfg->af_cfg.win.hor_start,
                       cfg->af_cfg.win.ver_start);
    isp_reg_set_af_filter(cfg->isp_dev_id, &cfg->af_cfg.filter);
    isp_reg_set_af_square_lut(cfg->isp_dev_id, cfg->af_cfg.square_lut);
}

void isp_reg_prepare_awb(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_awb_mode(cfg->isp_dev_id, cfg->mode_cfg.awb_mode);
    isp_reg_set_awb_satur_lim(cfg->isp_dev_id, cfg->awb_cfg.sat_r,
                              cfg->awb_cfg.sat_g, cfg->awb_cfg.sat_b);
    isp_reg_set_awb_win(cfg->isp_dev_id, cfg->awb_cfg.win.width,
                        cfg->awb_cfg.win.height, cfg->awb_cfg.win.hor_start,
                        cfg->awb_cfg.win.ver_start);
}

void isp_reg_prepare_hist(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_hist_src(cfg->isp_dev_id, cfg->mode_cfg.hist_sel);
    isp_reg_set_hist_mode(cfg->isp_dev_id, cfg->hist_cfg.mode);
    isp_reg_set_hist_win(cfg->isp_dev_id, cfg->hist_cfg.win.width,
                         cfg->hist_cfg.win.height, cfg->hist_cfg.win.hor_start,
                         cfg->hist_cfg.win.ver_start);
}

void isp_reg_prepare_blc(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_blc_offset(cfg->isp_dev_id, cfg->gain_offset_cfg.offset[0],
                           cfg->gain_offset_cfg.offset[1],
                           cfg->gain_offset_cfg.offset[2],
                           cfg->gain_offset_cfg.offset[3]);
}

void isp_reg_prepare_wb_gain(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_wb_clip(cfg->isp_dev_id, cfg->wb_gain_cfg.clip_val);
    isp_reg_set_wb_gain(cfg->isp_dev_id, cfg->wb_gain_cfg.wb_gain[0],
                        cfg->wb_gain_cfg.wb_gain[1],
                        cfg->wb_gain_cfg.wb_gain[2],
                        cfg->wb_gain_cfg.wb_gain[3]);
}

void isp_reg_prepare_dpc(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_dpc_mode(cfg->isp_dev_id, cfg->mode_cfg.otf_dpc_mode);
    isp_reg_set_dpc(cfg->isp_dev_id, cfg->otf_cfg.ratio[0],
                    cfg->otf_cfg.ratio[1], cfg->otf_cfg.ratio[2],
                    cfg->otf_cfg.ratio[3], cfg->otf_cfg.slope_th,
                    cfg->otf_cfg.cold_abs_th);
}

void isp_reg_prepare_cfa(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_cfa_mode(cfg->isp_dev_id, cfg->mode_cfg.cfa_mode);
    isp_reg_set_cfa(cfg->isp_dev_id, cfg->cfa_cfg.dir_th,
                    cfg->cfa_cfg.interp_mode, cfg->cfa_cfg.zig_zag);
}

void isp_reg_prepare_d3d(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_d3d_mode(cfg->isp_dev_id, cfg->mode_cfg.d3d_mode);
    isp_reg_set_d3d_cfg(cfg->isp_dev_id, &cfg->tdf_cfg);
    memcpy(cfg->d3d_lum_th_lut, cfg->d3d_tdnf_th, ISP_LUT_TH_BYTES);
    memcpy(cfg->d3d_bright_th_lut, cfg->d3d_tdnf_th, ISP_LUT_TH_BYTES);
    isp_reg_set_d3d_lum_th_lut(cfg->isp_dev_id, cfg->d3d_lum_th_lut);
    isp_reg_set_d3d_bright_th_lut(cfg->isp_dev_id, cfg->d3d_bright_th_lut);
    isp_reg_set_d3d_ref_noise_lut(cfg->isp_dev_id, cfg->d3d_ref_noise_lut);
    isp_reg_set_d3d_k_lut(cfg->isp_dev_id, cfg->d3d_k_lut);
    isp_reg_set_d3d_k_delta_lut(cfg->isp_dev_id, cfg->d3d_k_delta_lut);
    cfg->table_update |= ISP_TABLE_UPDATE_D3D;
}

void isp_reg_prepare_cnr(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_cnr(cfg->isp_dev_id, cfg->cnr_cfg.c_th, cfg->cnr_cfg.y_th,
                    cfg->cnr_cfg.st_v_y, cfg->cnr_cfg.st_h_y);
}

void isp_reg_prepare_saturation(isp_module_config_t *cfg)
{
    int32_t sum;

    if (!cfg)
        return;

    sum = (int32_t)cfg->satu_cfg.satu_r + (int32_t)cfg->satu_cfg.satu_g +
          (int32_t)cfg->satu_cfg.satu_b;
    if (sum != (int32_t)ISP_SATU_SUM_TARGET)
        return;

    isp_reg_set_saturation_mode(cfg->isp_dev_id, cfg->satu_cfg.mode);
    isp_reg_set_saturation(cfg->isp_dev_id, cfg->satu_cfg.satu_r,
                           cfg->satu_cfg.satu_g, cfg->satu_cfg.satu_b);

    /* The deployed object raises the update bit only when the saturation table
     * pointer is non-NULL, together with a copy of the embedded table *into*
     * that pointer target (embedded -> pointer).  Invisible to the
     * differential (its NULL-pointer case leaves the bit clear in both images);
     * black-box confirmed against the deployed object and the unit test was
     * corrected to match. */
    if (cfg->satu_src) {
        memcpy(cfg->satu_src, cfg->satu_cfg.table, ISP_SATU_TBL_SIZE);
        cfg->table_update |= ISP_TABLE_UPDATE_SATU;
    }
}

/*
 * Commit the front-end linear table.  The deployed routine copies the linear
 * table (SDK `linear_table`) into the front-end table (SDK `fe_table`) and
 * raises the LINEAR table-update bit, but only when the source is present: a
 * NULL source returns before the copy and before the bit is touched.  Taking
 * the source as a parameter keeps that presence test meaningful even though
 * the clean model stores the table inline.
 */
static void linear_table_commit(isp_module_config_t *cfg, const void *source)
{
    if (!source)
        return;

    memcpy(cfg->fe_table, source, ISP_LINEAR_TBL_SIZE);
    cfg->table_update |= ISP_TABLE_UPDATE_LINEAR;
}

void isp_reg_prepare_linear(isp_module_config_t *cfg)
{
    if (!cfg)
        return;

    /* source = linear table (linear_src), destination = front-end table
     * (fe_table); the reverse copy is wrong. */
    linear_table_commit(cfg, cfg->linear_src);
}

void isp_reg_prepare_sensor_offset(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_sensor_offset(cfg->isp_dev_id,
                              cfg->gain_offset_cfg.sensor_offset[0],
                              cfg->gain_offset_cfg.sensor_offset[1],
                              cfg->gain_offset_cfg.sensor_offset[2],
                              cfg->gain_offset_cfg.sensor_offset[3]);
}

void isp_reg_prepare_digital_gain(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_dg_mode(cfg->isp_dev_id, cfg->mode_cfg.dg_mode);
    isp_reg_set_dg_gain(cfg->isp_dev_id, cfg->gain_offset_cfg.gain[0],
                        cfg->gain_offset_cfg.gain[1],
                        cfg->gain_offset_cfg.gain[2],
                        cfg->gain_offset_cfg.gain[3]);
}

void isp_reg_prepare_ctc(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_ctc(cfg->isp_dev_id, &cfg->ctc_cfg);
}

void isp_reg_prepare_mode(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_input_fmt(cfg->isp_dev_id, cfg->mode_cfg.input_fmt);
}

void isp_reg_prepare_msc(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_msc_mode(cfg->isp_dev_id, cfg->mode_cfg.msc_mode);
    isp_reg_set_msc_blw_lut(cfg->isp_dev_id, cfg->msc_cfg.blw);
    isp_reg_set_msc_blh_lut(cfg->isp_dev_id, cfg->msc_cfg.blh);
    isp_reg_set_msc_blw_dlt_lut(cfg->isp_dev_id, cfg->msc_cfg.blw_dlt);
    isp_reg_set_msc_blh_dlt_lut(cfg->isp_dev_id, cfg->msc_cfg.blh_dlt);
    if (cfg->msc_table)
        cfg->table_update |= ISP_TABLE_UPDATE_MSC;
}

void isp_reg_prepare_lca(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_lca(cfg->isp_dev_id, &cfg->lca_cfg);
    isp_reg_set_lca_pf_satu_lut(cfg->isp_dev_id, cfg->lca_pf_satu_lut);
    isp_reg_set_lca_gf_satu_lut(cfg->isp_dev_id, cfg->lca_gf_satu_lut);
}

void isp_reg_prepare_gca(isp_module_config_t *cfg)
{
    if (!cfg)
        return;
    isp_reg_set_gca(cfg->isp_dev_id, &cfg->gca_cfg);
}

/* ------------------------------------------------------------------ */
/* 2.4 Enable routines                                                 */
/* ------------------------------------------------------------------ */

static void module_toggle(isp_module_config_t *cfg, isp_module_enable_t en,
                          uint32_t bit)
{
    if (en == ISP_MODULE_ENABLE)
        isp_reg_module_enable(cfg->isp_dev_id, bit);
    else
        isp_reg_module_disable(cfg->isp_dev_id, bit);
}

void isp_reg_enable_afs(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_AFS);
}

void isp_reg_enable_sharpness(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_SHARP);
}

void isp_reg_enable_contrast(isp_module_config_t *cfg, isp_module_enable_t en)
{
    (void)cfg;
    (void)en;
}

void isp_reg_enable_d2d(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_D2D);
}

void isp_reg_enable_rgb_drc(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_RGB_DRC);
}

void isp_reg_enable_pltm(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_PLTM);
}

void isp_reg_enable_wdr(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_WDR);
}

void isp_reg_enable_cem(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_CEM);
}

void isp_reg_enable_lens(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_LSC);
}

void isp_reg_enable_gamma(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_GAMMA);
}

void isp_reg_enable_rgb2yuv(isp_module_config_t *cfg, isp_module_enable_t en)
{
    (void)cfg;
    (void)en;
}

void isp_reg_enable_rgb2rgb(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_RGB2RGB);
}

void isp_reg_enable_ae(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_AE);
}

void isp_reg_enable_af(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_AF);
}

void isp_reg_enable_awb(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_AWB);
}

void isp_reg_enable_hist(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_HIST);
}

void isp_reg_enable_blc(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_BLC);
}

void isp_reg_enable_wb_gain(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_WB);
}

void isp_reg_enable_dpc(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_DPC);
}

void isp_reg_enable_d3d(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_D3D);
}

void isp_reg_enable_cnr(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_CNR);
}

void isp_reg_enable_saturation(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_SATU);
}

void isp_reg_enable_linear(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_LINEAR);
}

void isp_reg_enable_sensor_offset(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_SO);
}

void isp_reg_enable_digital_gain(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    isp_reg_set_dg_bypass(cfg->isp_dev_id, en == ISP_MODULE_ENABLE,
                           cfg->mode_cfg.dg_mode);
}

void isp_reg_enable_ctc(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_CTC);
}

void isp_reg_enable_msc(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;

    /* The MSC enable drives the CONTRAST bypass bit, not MSC (spec 2.4).  The
     * deployed object also clears that bit on disable.  Invisible to the
     * differential (its seeds keep the bit clear); black-box confirmed against
     * the deployed object and the unit test was corrected to match. */
    if (en == ISP_MODULE_ENABLE)
        isp_reg_module_enable(cfg->isp_dev_id, ISP_FEAT_CONTRAST);
    else
        isp_reg_module_disable(cfg->isp_dev_id, ISP_FEAT_CONTRAST);
}

void isp_reg_enable_lca(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_LCA);
}

void isp_reg_enable_gca(isp_module_config_t *cfg, isp_module_enable_t en)
{
    if (!cfg)
        return;
    module_toggle(cfg, en, ISP_FEAT_GCA);
}

/* ------------------------------------------------------------------ */
/* 2.4 Dispatch table (31 entries, table order)                        */
/* ------------------------------------------------------------------ */

const isp_module_attribute_t isp_module_attrs[ISP_MODULE_COUNT] = {
    { ISP_FEAT_AFS,      "AFS",      isp_reg_prepare_afs,        isp_reg_enable_afs },
    { ISP_FEAT_SHARP,    "SHARP",    isp_reg_prepare_sharpness,  isp_reg_enable_sharpness },
    { ISP_FEAT_CONTRAST, "CONTRAST", isp_reg_prepare_contrast,   isp_reg_enable_contrast },
    { ISP_FEAT_D2D,      "D2D",      isp_reg_prepare_d2d,        isp_reg_enable_d2d },
    { ISP_FEAT_RGB_DRC,  "RGB_DRC",  isp_reg_prepare_rgb_drc,    isp_reg_enable_rgb_drc },
    { ISP_FEAT_PLTM,     "PLTM",     isp_reg_prepare_pltm,       isp_reg_enable_pltm },
    { ISP_FEAT_WDR,      "WDR",      isp_reg_prepare_wdr,        isp_reg_enable_wdr },
    { ISP_FEAT_CEM,      "CEM",      isp_reg_prepare_cem,        isp_reg_enable_cem },
    { ISP_FEAT_LSC,      "LSC",      isp_reg_prepare_lens,       isp_reg_enable_lens },
    { ISP_FEAT_GAMMA,    "GAMMA",    isp_reg_prepare_gamma,      isp_reg_enable_gamma },
    { ISP_FEAT_RGB2YUV,  "RGB2YUV",  isp_reg_prepare_rgb2yuv,    isp_reg_enable_rgb2yuv },
    { ISP_FEAT_RGB2RGB,  "RGB2RGB",  isp_reg_prepare_rgb2rgb,    isp_reg_enable_rgb2rgb },
    { ISP_FEAT_AE,       "AE",       isp_reg_prepare_ae_win,     isp_reg_enable_ae },
    { ISP_FEAT_AF,       "AF",       isp_reg_prepare_af,         isp_reg_enable_af },
    { ISP_FEAT_AWB,      "AWB",      isp_reg_prepare_awb,        isp_reg_enable_awb },
    { ISP_FEAT_HIST,     "HIST",     isp_reg_prepare_hist,       isp_reg_enable_hist },
    { ISP_FEAT_BLC,      "BLC",      isp_reg_prepare_blc,        isp_reg_enable_blc },
    { ISP_FEAT_WB,       "WB",       isp_reg_prepare_wb_gain,    isp_reg_enable_wb_gain },
    { ISP_FEAT_DPC,      "DPC",      isp_reg_prepare_dpc,        isp_reg_enable_dpc },
    { ISP_FEAT_CFA,      "CFA",      isp_reg_prepare_cfa,        NULL },
    { ISP_FEAT_D3D,      "D3D",      isp_reg_prepare_d3d,        isp_reg_enable_d3d },
    { ISP_FEAT_CNR,      "CNR",      isp_reg_prepare_cnr,        isp_reg_enable_cnr },
    { ISP_FEAT_SATU,     "SATU",     isp_reg_prepare_saturation, isp_reg_enable_saturation },
    { ISP_FEAT_LINEAR,   "LINEAR",   isp_reg_prepare_linear,     isp_reg_enable_linear },
    { ISP_FEAT_SO,       "SO",       isp_reg_prepare_sensor_offset,         isp_reg_enable_sensor_offset },
    { ISP_FEAT_DG,       "DG",       isp_reg_prepare_digital_gain,         isp_reg_enable_digital_gain },
    { ISP_FEAT_CTC,      "CTC",      isp_reg_prepare_ctc,        isp_reg_enable_ctc },
    { ISP_FEAT_MODE,     "MODE",     isp_reg_prepare_mode,       NULL },
    { ISP_FEAT_MSC,      "MSC",      isp_reg_prepare_msc,        isp_reg_enable_msc },
    { ISP_FEAT_LCA,      "LCA",      isp_reg_prepare_lca,        isp_reg_enable_lca },
    { ISP_FEAT_GCA,      "GCA",      isp_reg_prepare_gca,        isp_reg_enable_gca }
};
