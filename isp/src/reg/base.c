/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * base.c - register-tier configuration and statistics layer.
 *
 * Clean-room re-production of the `isp_base.o` translation unit from
 * spec/reglayer2.md.  Written without access to the deployed object
 * (see reimplementation/README.md).
 *
 * Stage 1 implements the configuration/table-builder entry points:
 *   config_band_step, config_lens_center, config_dig_gain, config_gamma,
 *   config_wdr, config_lens_table, config_msc_table, isp_apply_colormatrix and
 *   __isp_stat_dynamic_judge.
 * The statistics handlers (isp_handle_stats / isp_handle_stats_sync) and the
 * settings dispatcher (isp_apply_settings) remain interface-only stubs.
 */

#include <math.h>
#include <string.h>

#include "base.h"
#include "freeisp/sdiv.h"

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

/* Piecewise linear interpolation, C truncation toward zero (spec 3.2). */
static int32_t isp_interp_s32(int32_t curr, int32_t xlo, int32_t xhi,
                           int32_t ylo, int32_t yhi)
{
    if ((xhi - xlo) == 0)
        return ylo;
    return ylo + (yhi - ylo) * (curr - xlo) / (xhi - xlo);
}

/* Median (element 3) of a 7-entry history, values compared as int32. */
static int32_t median7(const uint32_t *v)
{
    int32_t a[ISP_DYN_MOV_SAVE];
    int i, j;

    for (i = 0; i < ISP_DYN_MOV_SAVE; i++)
        a[i] = (int32_t)v[i];
    for (i = 0; i < ISP_DYN_MOV_SAVE - 1; i++) {
        for (j = i + 1; j < ISP_DYN_MOV_SAVE; j++) {
            if (a[j] < a[i]) {
                int32_t t = a[i];
                a[i] = a[j];
                a[j] = t;
            }
        }
    }
    return a[3];
}

/* Sum of a 3x3 minimum filter over the 14x22 interior of a 16x24 plane. */
static int32_t min_filter_sum(const int32_t *p)
{
    int32_t acc = 0;
    int r, c, rr, cc;

    for (r = 0; r + 2 < 16; r++) {
        for (c = 0; c + 2 < 24; c++) {
            int32_t m = p[r * 24 + c];
            for (rr = 0; rr < 3; rr++) {
                for (cc = 0; cc < 3; cc++) {
                    int32_t v = p[(r + rr) * 24 + (c + cc)];
                    if (v < m)
                        m = v;
                }
            }
            acc += m;
        }
    }
    return acc;
}

static const freeisp_tables_t *tables_get(void)
{
    return freeisp_get_tables();
}

/* ------------------------------------------------------------------ */
/* 3.11 config_band_step                                               */
/* ------------------------------------------------------------------ */

void config_band_step(isp_lib_context_t *ctx)
{
    uint32_t inc;

    if (!ctx)
        return;

    inc = ctx->stats_ctx.pic_h >> 7;
    if (inc < 1)
        inc = 1;
    if (inc > 63)
        inc = 63;
    ctx->module_cfg.afs_cfg.inc_line = inc;

    /* The deployment also programs the anti-flicker line increment here; the
     * the differential cannot observe register side effects (see spec 3.11/6). */
    isp_reg_set_afs_anti_flick(ctx->isp_dev_id, inc);
}

/* ------------------------------------------------------------------ */
/* 3.9 config_lens_center                                              */
/* ------------------------------------------------------------------ */

void config_lens_center(isp_lib_context_t *ctx)
{
    uint32_t pw, ph, m;
    int32_t cx, cy, dxw, dyh;
    uint32_t d1, d2, d3, d4;
    uint16_t rs_val;
    int s;

    if (!ctx)
        return;

    pw = ctx->stats_ctx.pic_w;
    ph = ctx->stats_ctx.pic_h;
    if (pw == 0 || ph == 0)
        return;

    cx = (int32_t)(((int32_t)ctx->ae_settings.lsc_center_x * (int32_t)pw) / 4096);
    cy = (int32_t)(((int32_t)ctx->ae_settings.lsc_center_y * (int32_t)ph) / 4096);
    dxw = cx - (int32_t)pw;
    dyh = cy - (int32_t)ph;

    d1 = (uint32_t)(dxw * dxw) + (uint32_t)(dyh * dyh);
    d2 = (uint32_t)(dxw * dxw) + (uint32_t)(cy * cy);
    d3 = (uint32_t)(cx * cx) + (uint32_t)(cy * cy);
    d4 = (uint32_t)(cx * cx) + (uint32_t)(dyh * dyh);

    m = d1;
    if (d2 > m)
        m = d2;
    if (d3 > m)
        m = d3;
    if (d4 > m)
        m = d4;

    ctx->module_cfg.lens_cfg.ct_x = (uint16_t)cx;
    ctx->module_cfg.lens_cfg.ct_y = (uint16_t)cy;

    rs_val = ctx->module_cfg.lens_cfg.rs_val;
    for (s = 0; s < 20; s++) {
        if ((m >> s) < 0x100u) {
            rs_val = (uint16_t)s;
            break;
        }
    }
    ctx->module_cfg.lens_cfg.rs_val = rs_val;

    ctx->module_cfg.disc_cfg.disc_ct_x = (uint16_t)cx;
    ctx->module_cfg.disc_cfg.disc_ct_y = (uint16_t)cy;
    ctx->module_cfg.disc_cfg.disc_rs_val = rs_val;
}

/* ------------------------------------------------------------------ */
/* 3.5 config_dig_gain                                                 */
/* ------------------------------------------------------------------ */

void config_dig_gain(isp_lib_context_t *ctx, int32_t exp_digital_gain)
{
    const freeisp_tables_t *ft;
    const base_tables_t *tb;
    uint16_t g[4];
    int i;

    if (!ctx)
        return;
    if (!ctx->isp_test_settings.dig_gain_en && !ctx->isp_test_settings.wdr_en)
        return;

    ft = tables_get();
    tb = ft ? ft->base : NULL;

    for (i = 0; i < 4; i++) {
        g[i] = (uint16_t)(((uint32_t)ctx->isp_ini_cfg.gains.bayer_gain[i] *
                           (uint32_t)exp_digital_gain + 0x200u) >> 10);
        ctx->sensor_info.gain_offset[i] = g[i];
    }

    if (ctx->isp_test_settings.linear_en && tb && tb->linear)
        memcpy(ctx->module_cfg.linear_table, tb->linear,
               ISP_LINEAR_TBL_N * sizeof(uint16_t));

    if (ctx->isp_test_settings.wdr_en && !ctx->ae_param.nor_cmd_mode) {
        for (i = 0; i < 4; i++)
            g[i] = (uint16_t)(uint32_t)sqrt((double)g[i] * 1024.0);
    }

    if (ctx->sensor_info.so_en) {
        int32_t d =
            (((int16_t)ctx->module_cfg.gain_offset_cfg.sensor_offset[1]) >> 4) +
            0x100;
        for (i = 0; i < 4; i++)
            g[i] = (uint16_t)freeisp_udiv(
                (uint32_t)g[i] * 0x100u + (uint32_t)(d / 2), (uint32_t)d);
    }

    if (ctx->sensor_info.blc_en) {
        int32_t d = (((int16_t)ctx->module_cfg.gain_offset_cfg.offset[1]) >> 4) +
                    0x100;
        for (i = 0; i < 4; i++)
            g[i] = (uint16_t)freeisp_udiv(
                (uint32_t)g[i] * 0x100u + (uint32_t)(d / 2), (uint32_t)d);
    }

    if (ctx->isp_test_settings.linear_en && ctx->isp_test_settings.wdr_en &&
        ctx->isp_test_settings.awb_en && !ctx->isp_test_settings.wb_en) {
        /* Digital-gain white-balance blend uses the AWB result's output gains,
         * not the saved/stats gains held in wb_gain. */
        g[0] = (uint16_t)((g[0] * ctx->awb_gain_output[0] + 0x80) >> 8);
        g[3] = (uint16_t)((g[3] * ctx->awb_gain_output[3] + 0x80) >> 8);
    }

    for (i = 0; i < 4; i++)
        ctx->module_cfg.gain_offset_cfg.gain[i] = g[i];
}

/* ------------------------------------------------------------------ */
/* 3.7 config_gamma                                                    */
/* ------------------------------------------------------------------ */

void config_gamma(isp_lib_context_t *ctx)
{
    const freeisp_tables_t *ft;
    const base_tables_t *tb;
    const int32_t *trig;
    const uint16_t *a, *b;
    uint16_t out[ISP_GAMMA_TBL_LEN];
    int32_t lv;
    int idx, i;

    if (!ctx)
        return;

    ft = tables_get();
    tb = ft ? ft->base : NULL;
    if (!tb || !tb->gamma_base)
        return;

    trig = (ctx->isp_ini_cfg.gamma_trig_cfg[0] != 0)
               ? ctx->isp_ini_cfg.gamma_trig_cfg
               : tb->gamma_trig;
    if (!trig)
        return;

    lv = ctx->sensor_info.ae_lv;
    for (idx = 0; idx < 4; idx++) {
        if (lv >= trig[idx])
            break;
    }

    if (idx == 0) {
        a = tb->gamma_base;
        b = a;
        for (i = 0; i < ISP_GAMMA_TBL_LEN; i++)
            out[i] = a[i];
    } else {
        int32_t down = trig[idx - 1];
        int32_t up = trig[idx];

        a = tb->gamma_sub[idx - 1];
        b = (idx - 1 == 0) ? tb->gamma_base : tb->gamma_sub[idx - 2];
        if (!a)
            return;
        if (up < down) {
            for (i = 0; i < ISP_GAMMA_TBL_LEN; i++)
                out[i] = (uint16_t)isp_interp_s32(lv, down, up,
                                               b ? b[i] : a[i], a[i]);
        } else {
            for (i = 0; i < ISP_GAMMA_TBL_LEN; i++)
                out[i] = a[i];
        }
    }

    if (ctx->isp_test_settings.wdr_en) {
        uint16_t tmp[1024];

        if (!ctx->isp_test_settings.gamma_en) {
            for (i = 0; i < 1024; i++) {
                double f = (double)i / 1023.0;
                out[i] = (uint16_t)(f * f * 4095.0);
            }
        } else {
            for (i = 0; i < 1024; i++)
                tmp[i] = out[(i * i) / 0x3ff];
            memcpy(out, tmp, 1024 * sizeof(uint16_t));
        }
        memcpy(out + 0x400, out, 0x800);
        memcpy(out + 0x800, out, 0x800);
    }

    memcpy(ctx->module_cfg.gamma_cfg.gamma_tbl, out, sizeof(out));
}

/* ------------------------------------------------------------------ */
/* 3.6 config_wdr                                                      */
/* ------------------------------------------------------------------ */

static int wdr_tuning_is_zero(const base_tables_t *tb)
{
    if (!tb || !tb->wdr_table)
        return 1;
    return (tb->wdr_table[0] == 0 && tb->wdr_table[0x1000] == 0 &&
            tb->wdr_table[0x1ffe] == 0);
}

void config_wdr(isp_lib_context_t *ctx, int32_t flag)
{
    const freeisp_tables_t *ft;
    const base_tables_t *tb;
    int32_t in_bits, out_bits;

    if (!ctx)
        return;

    ft = tables_get();
    tb = ft ? ft->base : NULL;
    in_bits = ctx->ae_param.comanding_input_bits;
    out_bits = ctx->ae_param.comanding_output_bits;

    if (ctx->module_cfg.mode_cfg.wdr_mode == 1 && wdr_tuning_is_zero(tb)) {
        int k;

        ctx->ae_param.nor_cmd_mode = 1;
        if (flag == 0 || ctx->ae_param.frame_cnt > 99)
            return;

        ctx->module_cfg.wdr_cfg.table[0] = 0;
        for (k = 1; k <= 0xffe; k++)
            ctx->module_cfg.wdr_cfg.table[k] =
                (uint16_t)(k >> (12 - in_bits));
        ctx->module_cfg.wdr_cfg.table[0xfff] = (uint16_t)((1 << in_bits) - 1);
        for (k = 0; k <= 0xfff; k++)
            ctx->module_cfg.wdr_cfg.table[0x1000 + k] =
                (uint16_t)(((1 << out_bits) - 1) * sqrt((double)k / 4095.0));

        if (tb && tb->anti_gamma)
            memcpy(ctx->anti_gamma_tbl, tb->anti_gamma,
                   ISP_ANTI_GAMMA_N * sizeof(uint16_t));
        return;
    }

    if (!ctx->isp_test_settings.wdr_en) {
        ctx->ae_settings.ae_mode = 0;
        return;
    }

    {
        int32_t hi, isp_hw, lo;
        uint16_t exp_ratio;
        int k;

        /* The deployed config_wdr guards each wdr_ratio field independently
         * (zero -> 0x100) and divides by ae_wdr_ratio.isp_hardware alone; the
         * earlier clean code folded the sensor/tmp guards onto isp_hw, which
         * clobbered a valid nonzero isp_hardware whenever sensor (COMANDING
         * WDR) or tmp was zero and produced wrong hi/lo thresholds. */
        if (ctx->ae_result.wdr_ratio_sensor == 0)
            ctx->ae_result.wdr_ratio_sensor = 0x100;
        if (ctx->ae_result.wdr_ratio_tmp == 0)
            ctx->ae_result.wdr_ratio_tmp = 0x100;
        if (ctx->ae_result.wdr_ratio_isp_hw == 0)
            ctx->ae_result.wdr_ratio_isp_hw = 0x100;

        hi = ctx->ae_result.wdr_hi_th;
        isp_hw = ctx->ae_result.wdr_ratio_isp_hw;

        hi = hi / isp_hw;
        lo = (int32_t)((uint32_t)ctx->ae_result.wdr_low_th / (uint32_t)isp_hw);
        ctx->ae_result.wdr_hi_th = hi;
        ctx->ae_result.wdr_low_th = lo;

        exp_ratio = (uint16_t)(0x100000 / isp_hw);
        ctx->module_cfg.wdr_cfg.hi_th = (uint16_t)hi;
        ctx->module_cfg.wdr_cfg.exp_ratio = exp_ratio;
        if (ctx->iso_cem_color2gray_th <= ctx->iso_lum_idx)
            ctx->module_cfg.wdr_cfg.hi_th = exp_ratio;
        ctx->module_cfg.wdr_cfg.lo_th = (uint16_t)lo;
        /* The deployed arithmetic is an unguarded sdiv; the divisor is zero
         * exactly when lo_th>>4 == (hi_th>>4)+1.  Keep the vendor result when
         * nonzero, but do not fault: a zero divisor yields slope 0, which is
         * unreachable on the vendor path only because its inputs differ. */
        {
            int32_t den = ((ctx->module_cfg.wdr_cfg.hi_th >> 4) + 1 -
                           (ctx->module_cfg.wdr_cfg.lo_th >> 4));

            ctx->module_cfg.wdr_cfg.slope =
                den ? (uint16_t)(0x10000 / den) : 0;
        }
        ctx->module_cfg.wdr_cfg.mv_th = 0xc00;
        ctx->module_cfg.wdr_cfg.mv_scale = 0x3c;
        ctx->module_cfg.wdr_cfg.out_sel =
            (uint16_t)ctx->ae_settings.wdr_output_select;

        if (flag == 0)
            return;

        if (tb && tb->wdr_table && !wdr_tuning_is_zero(tb)) {
            /* Only the WDR table is copied on this path; the tuning
             * anti-gamma array is not written into anti_gamma_tbl. */
            memcpy(ctx->module_cfg.wdr_cfg.table, tb->wdr_table, 0x4000);
            return;
        }

        if (ctx->module_cfg.mode_cfg.wdr_mode == 0) {
            for (k = 0; k < 0x10000; k += 16)
                ctx->module_cfg.wdr_cfg.table[k >> 4] =
                    (uint16_t)sqrt((double)(k << 8));
        } else {
            for (k = 0; k <= 0xfff; k++) {
                int32_t x = (k < 0x800)
                                ? k
                                : ((k <= 0xbe0) ? (k * 64 - 0x1f800)
                                                : (k * 1024 - 0x2e8000));
                ctx->module_cfg.wdr_cfg.table[k] =
                    (uint16_t)sqrt((double)(x << 4));
            }
        }
        for (k = 0; k < 0xfff0; k += 16)
            ctx->module_cfg.wdr_cfg.table[0x1000 + (k >> 4)] =
                (uint16_t)(k >> (16 - out_bits));
        ctx->module_cfg.wdr_cfg.table[0x1fff] = (uint16_t)((1 << out_bits) - 1);

        if (tb && tb->anti_gamma)
            memcpy(ctx->anti_gamma_tbl, tb->anti_gamma,
                   ISP_ANTI_GAMMA_N * sizeof(uint16_t));
    }
}

/* ------------------------------------------------------------------ */
/* 3.4 isp_apply_colormatrix                                           */
/* ------------------------------------------------------------------ */

static void config_color_matrix(isp_lib_context_t *ctx)
{
    const freeisp_tables_t *ft;
    const base_tables_t *tb;
    const uint16_t *cm;
    const int32_t *ct;
    uint16_t slot[12];
    int32_t temp, curr, sel, sh;
    int i;

    ft = tables_get();
    tb = ft ? ft->base : NULL;
    cm = tb ? tb->color_matrix : NULL;
    ct = tb ? tb->color_temp : NULL;

    temp = (ctx->isp_test_settings.isp_test_mode == 0)
               ? ctx->awb_color_temp_output
               : ctx->isp_test_settings.isp_color_temp;
    curr = (temp < 2500) ? 2500 : ((temp > 7000) ? 7000 : temp);
    sel = (temp < 4000) ? 1 : 2;

    if (cm && ct) {
        int32_t xlo = ct[sel - 1];
        int32_t xhi = ct[sel];
        const uint16_t *blo = &cm[(sel - 1) * 12];
        const uint16_t *bhi = &cm[sel * 12];

        for (i = 0; i < 12; i++)
            slot[i] = (uint16_t)(int16_t)isp_interp_s32(
                curr, xlo, xhi, (int16_t)blo[i], (int16_t)bhi[i]);
    } else {
        for (i = 0; i < 12; i++)
            slot[i] = 0;
    }

    sh = 16 - ctx->ae_param.comanding_output_bits;

    if (!ctx->isp_test_settings.wdr_en) {
        if (!ctx->isp_test_settings.cm_en) {
            for (i = 0; i < 12; i++)
                slot[i] = 0;
            slot[0] = 0x100;
            slot[4] = 0x100;
            slot[8] = 0x100;
        }
    } else {
        if (ctx->isp_test_settings.cm_en) {
            for (i = 0; i < 12; i++)
                slot[i] = (uint16_t)(int16_t)((int16_t)slot[i] << (sh & 31));
        } else {
            for (i = 0; i < 12; i++)
                slot[i] = 0;
            slot[0] = (uint16_t)(0x100 << (sh & 31));
            slot[4] = (uint16_t)(0x100 << (sh & 31));
            slot[8] = (uint16_t)(0x100 << (sh & 31));
        }
    }

    for (i = 0; i < 9; i++)
        ctx->module_cfg.rgb2rgb_cfg.color_matrix[i / 3][i % 3] = slot[i];
    for (i = 0; i < 3; i++)
        ctx->module_cfg.rgb2rgb_cfg.color_offset[i] = slot[9 + i];
}

static void config_defog(isp_lib_context_t *ctx)
{
    int32_t dv, nz, pre, scale;
    int r, c;

    if (!ctx->isp_test_settings.defog_en || ctx->adjust.defog_value < 4)
        return;

    dv = ctx->adjust.defog_value;
    nz = (int32_t)ctx->stat.min_rgb_saved;
    if (nz < 0)
        nz = 0;
    if (dv <= nz)
        nz = dv;

    pre = ctx->defog_ctx.min_rgb_pre[0];
    if (pre + 0x10 < nz) {
        pre += 6;
        ctx->defog_ctx.defog_changed = 1;
    } else if (nz < pre - 0x10) {
        pre -= 6;
        ctx->defog_ctx.defog_changed = 1;
    } else {
        ctx->defog_ctx.defog_changed = 0;
    }

    if (pre < 1)
        pre = 1;
    if (dv <= pre)
        pre = dv;

    if (ctx->ae_settings.flash_open == 1) {
        nz = (int32_t)ctx->stat.min_rgb_saved;
        if (nz < 1)
            nz = 1;
        pre = nz;
        if (dv * 2 < pre)
            pre = dv * 2;
    }

    if (ctx->ae_result.ae_gain != 0x100)
        pre = ctx->defog_ctx.defog_pre;

    scale = freeisp_sdiv(0x3ff00, 0x3ff - pre);

    for (r = 0; r < 3; r++) {
        for (c = 0; c < 3; c++) {
            int16_t m =
                (int16_t)ctx->module_cfg.rgb2rgb_cfg.color_matrix[r][c];
            ctx->module_cfg.rgb2rgb_cfg.color_matrix[r][c] =
                (uint16_t)(int16_t)((scale * m) >> 8);
        }
        ctx->module_cfg.rgb2rgb_cfg.color_offset[r] =
            (uint16_t)(int16_t)(((int16_t)((scale * pre) >> 8)) * -4);
    }

    ctx->defog_ctx.min_rgb_pre[0] = (uint16_t)pre;
    ctx->defog_ctx.defog_pre = pre;
}

void isp_apply_colormatrix(isp_lib_context_t *ctx)
{
    if (!ctx)
        return;
    config_color_matrix(ctx);
    config_defog(ctx);
}

/* ------------------------------------------------------------------ */
/* 3.2 __isp_stat_dynamic_judge                                        */
/* ------------------------------------------------------------------ */

void __isp_stat_dynamic_judge(isp_lib_context_t *ctx)
{
    isp_dynamic_stats_t *d;
    int32_t p1[ISP_AE_WIN_N], p2[ISP_AE_WIN_N], p3[ISP_AE_WIN_N];
    int32_t th[ISP_DYN_MOV_TH];
    int32_t f1, f2, f3, med, low, high, tm;
    uint32_t minval, sum = 0, t, mov;
    int32_t *ca[6];
    int32_t *tg[6];
    int i;

    if (!ctx)
        return;

    d = &ctx->stats_ctx.dynamic_stats;

    if (!d->enable || !ctx->isp_test_settings.ae_en) {
        d->tdnf_comp_target = 0;
        d->tdnf_diff_comp_target = 0;
        d->lp_th_ratio_comp_target = 0;
        d->sharp_hfrq_comp_target = 0;
        d->sharp_edge_comp_target = 0;
        d->sharp_under_shoot_comp_target = 0;
        return;
    }

    for (i = 0; i < ISP_AE_WIN_N; i++) {
        int32_t a;

        a = (int32_t)(d->accum[i] - d->accum_last1[i]);
        p1[i] = (a < 0) ? -a : a;
        a = (int32_t)(d->accum[i] - d->accum_last2[i]);
        p2[i] = (a < 0) ? -a : a;
        a = (int32_t)(d->accum[i] - d->accum_last3[i]);
        p3[i] = (a < 0) ? -a : a;

        if (ctx->stats_ctx.stats.ae.win_pix_n)
            sum += d->accum[i] / ctx->stats_ctx.stats.ae.win_pix_n;
    }

    f1 = min_filter_sum(p1);
    f2 = min_filter_sum(p2);
    f3 = min_filter_sum(p3);
    minval = (uint32_t)f1;
    if ((uint32_t)f2 < minval)
        minval = (uint32_t)f2;
    if ((uint32_t)f3 < minval)
        minval = (uint32_t)f3;

    for (i = 0; i < ISP_DYN_MOV_SAVE - 1; i++)
        d->mov_save[i] = d->mov_save[i + 1];

    t = (uint32_t)(int32_t)(25u * sum);
    if ((int32_t)t >= 142080) {
        d->mov_save[ISP_DYN_MOV_SAVE - 1] = minval;
    } else {
        int32_t q = (int32_t)t / 3840;
        int32_t tt;

        if (q < 0)
            q = 0;
        tt = (36 * (int32_t)minval) / (q + 1);
        if (tt < (int32_t)minval)
            tt = (int32_t)minval;
        if (4 * (int32_t)minval < tt)
            tt = 4 * (int32_t)minval;
        d->mov_save[ISP_DYN_MOV_SAVE - 1] = (uint32_t)tt;
    }

    med = median7(d->mov_save);
    low = med / 4;
    if (low < 6160)
        low = 6160;
    low = (int32_t)d->mov_old - low;
    high = 2 * med;
    if (high > 2464)
        high = 2464;
    if (high < 308)
        high = 308;
    high += (int32_t)d->mov_old;
    tm = (med > low) ? med : low;
    mov = (uint32_t)((high < tm) ? high : tm);
    d->mov = mov;

    for (i = 0; i < ISP_DYN_MOV_TH; i++)
        th[i] = (int32_t)d->mov_th[i];

    ca[0] = d->tdnf_comp;
    ca[1] = d->tdnf_diff_comp;
    ca[2] = d->lp_th_ratio_comp;
    ca[3] = d->sharp_hfrq_comp;
    ca[4] = d->sharp_edge_comp;
    ca[5] = d->sharp_under_shoot_comp;
    tg[0] = &d->tdnf_comp_target;
    tg[1] = &d->tdnf_diff_comp_target;
    tg[2] = &d->lp_th_ratio_comp_target;
    tg[3] = &d->sharp_hfrq_comp_target;
    tg[4] = &d->sharp_edge_comp_target;
    tg[5] = &d->sharp_under_shoot_comp_target;

    for (i = 0; i < 6; i++) {
        int32_t m = (int32_t)mov;
        int32_t v;

        if (m <= th[0])
            v = ca[i][0];
        else if (m <= th[1])
            v = isp_interp_s32(m, th[0], th[1], ca[i][0], ca[i][1]);
        else if (m <= th[2])
            v = isp_interp_s32(m, th[1], th[2], ca[i][1], 0);
        else if (m <= th[3])
            v = 0;
        else if (m <= th[4])
            v = isp_interp_s32(m, th[3], th[4], 0, ca[i][2]);
        else if (m <= th[5])
            v = isp_interp_s32(m, th[4], th[5], ca[i][2], ca[i][3]);
        else
            v = ca[i][3];
        *tg[i] = v;
    }

    if (ctx->isp_test_settings.af_en)
        ctx->af_param.mov = (int32_t)mov / 308;

    d->mov_old = mov;
}

/* ------------------------------------------------------------------ */
/* 3.8 config_lens_table                                               */
/* ------------------------------------------------------------------ */

/* Temperature-trigger selection.  The deployed object falls back to its
 * compiled-in lsc_trig_cfg_def / msc_trig_cfg_def defaults when the tuning
 * array is all zero; those defaults are injected through the table contract
 * (the runtime feed extracts them from the camera's own rmm image), so no
 * vendor bytes are compiled in.  If the injected default is absent, fall
 * back to the per-temperature tuning triggers so nothing dereferences NULL. */
static const uint16_t *temp_trig_cfg(const uint16_t *cfg, const uint16_t *def,
                                     const uint16_t *fallback)
{
    if (cfg != NULL && cfg[0] != 0)
        return cfg;
    return (def != NULL) ? def : fallback;
}

static void lens_build_shifted(const uint16_t *down, const uint16_t *up,
                               int32_t t, int32_t lo, int32_t hi, int s,
                               uint16_t *lens)
{
    int i, ch;

    if (hi - lo < 1) {
        for (i = 0; i < 256; i++) {
            for (ch = 0; ch < 3; ch++)
                lens[4 * i + ch] = (uint16_t)(down[ch * 256 + i] >> (s & 31));
        }
    } else {
        for (i = 0; i < 256; i++) {
            for (ch = 0; ch < 3; ch++)
                lens[4 * i + ch] = (uint16_t)isp_interp_s32(
                    t, lo, hi, down[ch * 256 + i] >> (s & 31),
                    up[ch * 256 + i] >> (s & 31));
        }
    }
}

static void lens_temp_index(const uint16_t *trig, int32_t temp, int32_t *t_out,
                            int *j_out)
{
    int32_t t = temp;
    int idx;

    if (t < (int32_t)trig[0])
        t = trig[0];
    if (t > (int32_t)trig[ISP_MSC_TEMP_NUM - 1])
        t = trig[ISP_MSC_TEMP_NUM - 1];
    for (idx = 0; idx < ISP_MSC_TEMP_NUM; idx++) {
        if (t < (int32_t)trig[idx])
            break;
    }
    if (idx >= ISP_MSC_TEMP_NUM)
        idx = ISP_MSC_TEMP_NUM - 1;
    *t_out = t;
    *j_out = idx ? idx - 1 : 0;
}

void config_lens_table(isp_lib_context_t *ctx, int32_t vcm_std_pos)
{
    const freeisp_tables_t *ft;
    const base_tables_t *tb;
    const uint16_t *trig;
    uint16_t *lens;
    int32_t temp, t, lo, hi;
    int j;

    if (!ctx)
        return;
    if (!ctx->isp_test_settings.lsc_en)
        return;

    ft = tables_get();
    tb = ft ? ft->base : NULL;
    if (!tb)
        return;

    trig = temp_trig_cfg(ctx->isp_ini_cfg.lsc_trig_cfg, tb->lsc_trig_def,
                         tb->lsc_trig);
    if (!trig)
        return;

    temp = (ctx->isp_test_settings.isp_test_mode == 0)
               ? ctx->awb_color_temp_output
               : ctx->isp_test_settings.isp_color_temp;
    lens_temp_index(trig, temp, &t, &j);
    lo = trig[j];
    hi = trig[j + 1];
    lens = ctx->module_cfg.lens_table;

    if (ctx->ff_mod == 2) {
        lens_build_shifted(tb->lsc[j], tb->lsc[j + 1], t, lo, hi,
                           ctx->lsc_mode & 31, lens);
    } else if (ctx->ff_mod == 1) {
        int s = ctx->lsc_mode;
        int32_t roll, low_gain, high_gain, anagain;
        int i, ch;

        lens_build_shifted(tb->lsc[j], tb->lsc[j + 1], t, lo, hi, s, lens);

        roll = ctx->rolloff_ratio;
        low_gain = (roll & 0xfff) << 4;
        high_gain = ((roll & 0xffffff) >> 12) << 4;
        anagain = ctx->ev_analog_gain;
        if (high_gain >= low_gain) {
            uint16_t g = (uint16_t)(0x400 >> (s & 31));
            for (i = 0; i < 256; i++) {
                for (ch = 0; ch < 3; ch++) {
                    if (high_gain < anagain)
                        lens[4 * i + ch] = g;
                    else if (low_gain < anagain)
                        lens[4 * i + ch] = (uint16_t)isp_interp_s32(
                            anagain, low_gain, high_gain, lens[4 * i + ch], g);
                }
            }
        }
    } else {
        const uint16_t *ad = tb->lsc[j];
        const uint16_t *au = tb->lsc[j + 1];
        const uint16_t *bd = tb->lsc[j + 6];
        const uint16_t *bu = tb->lsc[j + 7];
        int32_t vcm = vcm_std_pos;
        int i;

        if (vcm < 0)
            vcm = 0;
        if (vcm > 0x3ff)
            vcm = 0x3ff;

        if (hi - lo < 1) {
            for (i = 0; i < 768; i++)
                lens[i] = (uint16_t)isp_interp_s32(vcm, 0, 0x3ff, ad[i], bd[i]);
        } else {
            uint16_t pos1[768], pos2[768];

            for (i = 0; i < 768; i++) {
                pos1[i] = (uint16_t)isp_interp_s32(t, lo, hi, ad[i], au[i]);
                pos2[i] = (uint16_t)isp_interp_s32(t, lo, hi, bd[i], bu[i]);
            }
            for (i = 0; i < 256; i++) {
                lens[4 * i + 0] =
                    (uint16_t)isp_interp_s32(vcm, 0, 0x3ff, pos1[i], pos2[i]);
                lens[4 * i + 1] =
                    (uint16_t)isp_interp_s32(vcm, 0, 0x3ff, pos1[i + 256],
                                          pos2[i + 256]);
                lens[4 * i + 2] =
                    (uint16_t)isp_interp_s32(vcm, 0, 0x3ff, pos1[i + 512],
                                          pos2[i + 512]);
            }
        }
    }

    if (ctx->isp_test_settings.wdr_en && ctx->ae_entity_ctx.ae_param &&
        !ctx->ae_entity_ctx.ae_param->nor_cmd_mode) {
        int i, ch;

        for (i = 0; i < 256; i++) {
            for (ch = 0; ch < 3; ch++)
                lens[4 * i + ch] =
                    (uint16_t)(uint32_t)sqrt((double)lens[4 * i + ch] * 1024.0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 3.10 config_msc_table                                               */
/* ------------------------------------------------------------------ */

#define MSC_SRC_STRIDE 484

static void msc_emit_corr(isp_lib_context_t *ctx, const double *cref, int i,
                          int L, float adj_or_less, int flag, int32_t v0,
                          int32_t v1, int32_t v2, uint16_t *out)
{
    float g0 = ctx->msc_golden_ratio[i];
    double s, d;

    if (flag == 1) {
        s = (double)(ctx->msc_r_ratio - 1.0f);
        d = (i < 256) ? cref[i] * (double)(adj_or_less / 100.0f) : 0.0;
    } else {
        s = (double)(1.0f - ctx->msc_r_ratio);
        d = (i < 256) ? cref[i] * (double)(adj_or_less / 100.0f) : 0.0;
    }

    out[4 * i + 0] =
        (uint16_t)(uint32_t)((double)((float)v0 * g0) * (1.0 - d * s));
    out[4 * i + 1] =
        (uint16_t)(uint32_t)((float)v1 * ctx->msc_golden_ratio[L + i]);
    out[4 * i + 2] =
        (uint16_t)(uint32_t)((float)v2 * ctx->msc_golden_ratio[2 * L + i]);
}

static void msc_build_af(isp_lib_context_t *ctx, const base_tables_t *tb,
                         int32_t t, int32_t tlo, int32_t thi, int j, int L,
                         int32_t vcm, uint16_t *out)
{
    const uint16_t *rlo = tb->msc[j];
    const uint16_t *rhi = tb->msc[j + 1];
    const uint16_t *r2lo = tb->msc[j + 6];
    const uint16_t *r2hi = tb->msc[j + 7];
    double cref[256];
    float adj, c100;
    int stride = L;
    int i;

    freeisp_comp_ref_fill(cref);
    adj = (float)isp_interp_s32(t, tlo, thi, (int)ctx->msc_adjust_ratio[j],
                             (int)ctx->msc_adjust_ratio[j + 1]);
    c100 = (float)isp_interp_s32(t, tlo, thi, 100, 100);

    if (thi - tlo < 1) {
        for (i = 0; i < L; i++) {
            int src = (L < 484) ? i + 6 * (i >> 4) : i;
            int32_t v0 = isp_interp_s32(vcm, 0, 0x3ff, rlo[src], r2lo[src]);
            int32_t v1 = isp_interp_s32(vcm, 0, 0x3ff, rlo[src + stride],
                                     r2lo[src + stride]);
            int32_t v2 = isp_interp_s32(vcm, 0, 0x3ff, rlo[src + 2 * stride],
                                     r2lo[src + 2 * stride]);
            float a = (ctx->msc_golden_flag[i] == 1) ? adj : c100;
            msc_emit_corr(ctx, cref, i, L, a,
                          ctx->msc_golden_flag[i] == 1, v0, v1, v2, out);
        }
    } else {
        /*
         * The deployed helper fills only the first 3*L entries of a
         * translation-unit-scope scratch and reads stale entries past that
         * (the spec's "scratch persists across calls"); reproduce the same
         * storage so the out-of-range reads match.  Both sides start zeroed.
         */
        static uint16_t pos1[3 * MSC_SRC_STRIDE];
        static uint16_t pos2[3 * MSC_SRC_STRIDE];
        int p;

        for (p = 0; p < 3 * L; p++) {
            pos1[p] = (uint16_t)isp_interp_s32(t, tlo, thi, rlo[p], rhi[p]);
            pos2[p] = (uint16_t)isp_interp_s32(t, tlo, thi, r2lo[p], r2hi[p]);
        }
        for (i = 0; i < L; i++) {
            int src = (L < 484) ? i + 6 * (i >> 4) : i;
            int32_t v0 = isp_interp_s32(vcm, 0, 0x3ff, pos1[src], pos2[src]);
            int32_t v1 = isp_interp_s32(vcm, 0, 0x3ff, pos1[src + stride],
                                     pos2[src + stride]);
            int32_t v2 = isp_interp_s32(vcm, 0, 0x3ff, pos1[src + 2 * stride],
                                     pos2[src + 2 * stride]);
            float a = (ctx->msc_golden_flag[i] == 1) ? adj : c100;
            msc_emit_corr(ctx, cref, i, L, a,
                          ctx->msc_golden_flag[i] == 1, v0, v1, v2, out);
        }
    }
}

static void msc_build_ff(isp_lib_context_t *ctx, const base_tables_t *tb,
                         int32_t t, int32_t tlo, int32_t thi, int j, int L,
                         uint16_t *out)
{
    const uint16_t *rlo = tb->msc[j];
    const uint16_t *rhi = tb->msc[j + 1];
    double cref[256];
    float adj, less;
    int i, c;

    freeisp_comp_ref_fill(cref);
    adj = (float)isp_interp_s32(t, tlo, thi, (int)ctx->msc_adjust_ratio[j],
                             (int)ctx->msc_adjust_ratio[j + 1]);
    less = (float)isp_interp_s32(t, tlo, thi,
                              (int)ctx->msc_adjust_ratio_less[j],
                              (int)ctx->msc_adjust_ratio_less[j + 1]);

    if (thi - tlo < 1) {
        for (i = 0; i < L; i++) {
            int src = (L < 484) ? i + 6 * (i >> 4) : i;
            for (c = 0; c < 3; c++)
                out[4 * i + c] = (uint16_t)((float)rlo[src + c * MSC_SRC_STRIDE] *
                                            ctx->msc_golden_ratio[c * L + i]);
        }
    } else {
        for (i = 0; i < L; i++) {
            int src = (L < 484) ? i + 6 * (i >> 4) : i;
            int32_t v0 = isp_interp_s32(t, tlo, thi, rlo[src], rhi[src]);
            int32_t v1 = isp_interp_s32(t, tlo, thi, rlo[src + MSC_SRC_STRIDE],
                                     rhi[src + MSC_SRC_STRIDE]);
            int32_t v2 = isp_interp_s32(t, tlo, thi, rlo[src + 2 * MSC_SRC_STRIDE],
                                     rhi[src + 2 * MSC_SRC_STRIDE]);
            float a = (ctx->msc_golden_flag[i] == 1) ? adj : less;
            msc_emit_corr(ctx, cref, i, L, a,
                          ctx->msc_golden_flag[i] == 1, v0, v1, v2, out);
        }
    }
}

void config_msc_table(isp_lib_context_t *ctx, int32_t vcm_std_pos)
{
    const freeisp_tables_t *ft;
    const base_tables_t *tb;
    const uint16_t *trig;
    uint16_t *out;
    int32_t temp, t, tlo, thi;
    int32_t vcm;
    int j, L;

    if (!ctx)
        return;
    if (!ctx->isp_test_settings.msc_en)
        return;

    ft = tables_get();
    tb = ft ? ft->base : NULL;
    if (!tb)
        return;

    trig = temp_trig_cfg(ctx->isp_ini_cfg.msc_trig_cfg, tb->msc_trig_def,
                         tb->msc_trig);
    if (!trig)
        return;

    temp = (ctx->isp_test_settings.isp_test_mode == 0)
               ? ctx->awb_color_temp_output
               : ctx->isp_test_settings.isp_color_temp;
    lens_temp_index(trig, temp, &t, &j);
    tlo = trig[j];
    thi = trig[j + 1];

    L = (ctx->msc_mode < 4) ? 256 : 484;
    out = ctx->module_cfg.msc_table;

    vcm = vcm_std_pos;
    if (vcm < 0)
        vcm = 0;
    if (vcm > 0x3ff)
        vcm = 0x3ff;

    if (ctx->mff_mod == 2)
        msc_build_ff(ctx, tb, t, tlo, thi, j, L, out);
    else
        msc_build_af(ctx, tb, t, tlo, thi, j, L, vcm, out);

    if (ctx->isp_test_settings.wdr_en && ctx->ae_entity_ctx.ae_param &&
        !ctx->ae_entity_ctx.ae_param->nor_cmd_mode) {
        int i, c;

        for (i = 0; i < 256; i++) {
            for (c = 0; c < 3; c++)
                out[4 * i + c] =
                    (uint16_t)(uint32_t)sqrt((double)out[4 * i + c] * 1024.0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 3.1 Statistics capture                                              */
/* ------------------------------------------------------------------ */

static uint16_t load_u16(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static uint32_t load_u32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static uint64_t load_u64(const uint8_t *p)
{
    uint64_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static uint8_t clamp_u8(uint32_t v)
{
    return (v > 0xffu) ? (uint8_t)0xff : (uint8_t)v;
}

static void stats_rotate_accum(isp_dynamic_stats_t *d)
{
    memcpy(d->accum_last3, d->accum_last2, sizeof(d->accum_last3));
    memcpy(d->accum_last2, d->accum_last1, sizeof(d->accum_last2));
    memcpy(d->accum_last1, d->accum, sizeof(d->accum_last1));
}

static uint32_t stats_ae_win_pix_n(isp_lib_context_t *ctx)
{
    uint32_t n = ctx->module_cfg.ae_cfg.ae_reg_win.height *
                 ctx->module_cfg.ae_cfg.ae_reg_win.width;
    uint32_t sh = (ctx->ae_settings.ae_mode == 0) ? 2u : 1u;

    return (n == 0) ? 0u : (n >> sh);
}

static void handle_ae(isp_lib_context_t *ctx, const uint8_t *buf)
{
    isp_ae_stats_t *ae = &ctx->stats_ctx.stats.ae;
    const uint8_t *win = buf + ISP_DMA_AE_OFF;
    const uint8_t *hist = buf + ISP_DMA_HIST_OFF;
    int32_t cmdout = ctx->ae_param.comanding_output_bits;
    uint32_t win_pix_n;
    int i;

    if (!ctx->isp_test_settings.ae_en)
        return;

    stats_rotate_accum(&ctx->stats_ctx.dynamic_stats);
    win_pix_n = stats_ae_win_pix_n(ctx);
    ae->win_pix_n = win_pix_n;

    if (ctx->isp_ini_cfg.ae_stat_sel != 0 && ctx->isp_test_settings.wdr_en != 0) {
        for (i = 0; i < ISP_AE_WIN_N; i++) {
            uint32_t raw[3], corr[3];
            int c;

            raw[0] = load_u32(win + (uint32_t)i * 12 + 0);
            raw[1] = load_u32(win + (uint32_t)i * 12 + 4);
            raw[2] = load_u32(win + (uint32_t)i * 12 + 8);
            for (c = 0; c < 3; c++) {
                uint32_t idx = freeisp_udiv((raw[c] << 4) << (16 - cmdout),
                                            win_pix_n);
                if (idx > 0xfffu)
                    idx = 0xfffu;
                corr[c] = (win_pix_n * ctx->anti_gamma_tbl[idx]) >> 4;
            }
            ae->win_r[i] = corr[0];
            ae->win_g[i] = corr[1];
            ae->win_b[i] = corr[2];
            ae->luma[i] = clamp_u8(
                freeisp_udiv((10u * raw[1] + 4u * raw[0] + 2u * raw[2]) >> 4,
                             win_pix_n));
            ctx->stats_ctx.dynamic_stats.accum[i] =
                (corr[0] + corr[2] + 2u * corr[1]) >> 2;
        }
    } else {
        for (i = 0; i < ISP_AE_WIN_N; i++) {
            uint32_t r = load_u32(win + (uint32_t)i * 12 + 0);
            uint32_t g = load_u32(win + (uint32_t)i * 12 + 4);
            uint32_t b = load_u32(win + (uint32_t)i * 12 + 8);

            ae->win_r[i] = r;
            ae->win_g[i] = g;
            ae->win_b[i] = b;
            ae->luma[i] = clamp_u8(freeisp_udiv(
                (10u * g + 4u * r + 2u * b) >> 4, win_pix_n));
            ctx->stats_ctx.dynamic_stats.accum[i] = (r + b + 2u * g) >> 2;
        }
    }

    for (i = 0; i < ISP_HIST_BIN_N / 2; i++) {
        uint32_t v = load_u32(hist + (uint32_t)i * 4) >> 1;

        ae->hist[2 * i] = v;
        ae->hist[2 * i + 1] = v;
    }
}

static void awb_wb_ratios(isp_lib_context_t *ctx, uint32_t *ratio_r,
                          uint32_t *ratio_b)
{
    uint32_t gr = ctx->wb_gain[1];

    *ratio_r = gr ? ((((uint32_t)ctx->wb_gain[0] << 8) + gr / 2) / gr) : 0u;
    *ratio_b = gr ? ((((uint32_t)ctx->wb_gain[3] << 8) + gr / 2) / gr) : 0u;
}

static void handle_awb(isp_lib_context_t *ctx, const uint8_t *buf)
{
    isp_awb_stats_t *awb = &ctx->stats_ctx.stats.awb;
    const uint8_t *base = buf + ISP_DMA_AWB_OFF;
    const uint8_t *cnt = base + ISP_DMA_AWB_CNT_OFF;
    uint32_t ratio_r, ratio_b;
    int32_t cmdout = ctx->ae_param.comanding_output_bits;
    int i;

    if (!ctx->isp_test_settings.awb_en)
        return;

    awb_wb_ratios(ctx, &ratio_r, &ratio_b);

    for (i = 0; i < ISP_AWB_WIN_N; i++) {
        uint32_t r = load_u32(base + (uint32_t)i * 12 + 0);
        uint32_t g = load_u32(base + (uint32_t)i * 12 + 4);
        uint32_t b = load_u32(base + (uint32_t)i * 12 + 8);
        uint32_t c = load_u16(cnt + (uint32_t)i * 2);

        awb->count[i] = c;
        if (c == 0) {
            awb->sum_r[i] = 0;
            awb->sum_g[i] = 0;
            awb->sum_b[i] = 0;
            awb->avg_r[i] = 0;
            awb->avg_g[i] = 0;
            awb->avg_b[i] = 0;
            continue;
        }

        if (ctx->isp_test_settings.wdr_en == 0) {
            uint32_t sr = ratio_r ? ((ratio_r >> 1) + r * 256u) / ratio_r
                                  : r * 256u;
            uint32_t sb = ratio_b ? ((ratio_b >> 1) + b * 256u) / ratio_b
                                  : b * 256u;

            awb->sum_r[i] = sr;
            awb->sum_g[i] = g;
            awb->sum_b[i] = sb;
            awb->avg_r[i] = (sr + c / 2) / c;
            awb->avg_g[i] = (g + c / 2) / c;
            awb->avg_b[i] = (sb + c / 2) / c;
        } else {
            uint32_t gam[3];
            uint32_t idx;
            int ch;
            const uint32_t raw[3] = { r, g, b };

            for (ch = 0; ch < 3; ch++) {
                idx = (c / 2 + ((raw[ch] << 4) << (16 - cmdout))) / c;
                if (idx > 0xfffu)
                    idx = 0xfffu;
                gam[ch] = ctx->anti_gamma_tbl[idx] >> 4;
            }
            awb->sum_r[i] = r;
            awb->sum_g[i] = g;
            awb->sum_b[i] = b;
            awb->avg_g[i] = gam[1];
            awb->avg_r[i] = ratio_r ? ((ratio_r >> 1) + gam[0] * 256u) / ratio_r
                                    : gam[0] * 256u;
            awb->avg_b[i] = ratio_b ? ((ratio_b >> 1) + gam[2] * 256u) / ratio_b
                                    : gam[2] * 256u;
        }
    }
}

static void handle_af(isp_lib_context_t *ctx, const uint8_t *buf)
{
    isp_af_stats_t *af = &ctx->stats_ctx.stats.af;
    const uint8_t *base = buf + ISP_DMA_AF_OFF;
    int i;

    if (!ctx->isp_test_settings.af_en && !ctx->isp_test_settings.isp_test_focus)
        return;

    for (i = 0; i < ISP_AF_WIN_N; i++) {
        uint64_t iir = load_u64(base + 0x0000u + (uint32_t)i * 8);
        uint64_t fir = load_u64(base + 0x0c00u + (uint32_t)i * 8);
        uint64_t iir_cnt = load_u64(base + 0x6c00u + (uint32_t)i * 8);
        uint64_t fir_cnt = load_u64(base + 0xcc00u + (uint32_t)i * 8);
        uint64_t hlt_cnt = load_u64(base + 0x12c00u + (uint32_t)i * 8);

        af->iir[i] = iir;
        af->fir[i] = fir;
        af->iir_cnt[i] = iir_cnt;
        af->fir_cnt[i] = fir_cnt;
        af->hlt_cnt[i] = hlt_cnt;
        af->count[i] = iir_cnt + fir_cnt + hlt_cnt;
        af->h_d1[i] = iir;
        af->h_d2[i] = fir;
        af->v_d1[i] = 0;
        af->v_d2[i] = 0;
    }
}

static void handle_afs(isp_lib_context_t *ctx, const uint8_t *buf)
{
    isp_afs_stats_t *afs = &ctx->stats_ctx.stats.afs;
    const uint8_t *base = buf + ISP_DMA_AFS_OFF;
    int i;

    if (!ctx->isp_test_settings.afs_en)
        return;

    for (i = 0; i < ISP_AFS_SUM_N; i++)
        afs->sum[i] = load_u32(base + (uint32_t)i * 4);
    afs->pic_w = ctx->stats_ctx.pic_w;
    afs->pic_h = ctx->stats_ctx.pic_h;
}

static void handle_pltm(isp_lib_context_t *ctx, const uint8_t *buf)
{
    isp_pltm_stats_t *pltm = &ctx->stats_ctx.stats.pltm;
    isp_awb_stats_t *awb = &ctx->stats_ctx.stats.awb;
    const uint8_t *base = buf + ISP_DMA_PLTM_OFF;
    uint64_t sum_before = 0, sum_after = 0;
    uint32_t mn_before = 0xfff, mx_before = 0;
    uint32_t mn_after = 0xfff, mx_after = 0;
    int i;

    if (!ctx->isp_test_settings.pltm_en)
        return;

    for (i = 0; i < ISP_PLTM_WIN_N; i++) {
        uint16_t v = load_u16(base + (uint32_t)i * 2);

        pltm->lst[i] = v;
        sum_before += v;
        if (v < mn_before)
            mn_before = v;
        if (v > mx_before)
            mx_before = v;
    }

    for (i = 0; i < ISP_AWB_WIN_N; i++) {
        uint32_t w = 10u * awb->avg_g[i] + 4u * awb->avg_r[i] +
                     2u * awb->avg_b[i];

        awb->avg[i] = w;
        sum_after += w;
        if (w != 0 && w < mn_after)
            mn_after = w;
        if (w > mx_after)
            mx_after = w;
    }

    pltm->avg_before = (uint32_t)(sum_before / ISP_PLTM_WIN_N);
    pltm->min_before = mn_before;
    pltm->max_before = mx_before;
    pltm->avg_after = (uint32_t)(sum_after >> 10);
    pltm->min_after = mn_after;
    pltm->max_after = mx_after;
}

static void stats_attach(isp_lib_context_t *ctx)
{
    ctx->stats_attach.awb_stats = &ctx->stats_ctx.stats.awb;
    ctx->stats_attach.ae_stats = &ctx->stats_ctx.stats.ae;
    ctx->stats_attach.af_stats = &ctx->stats_ctx.stats.af;
    ctx->stats_attach.afs_stats = &ctx->stats_ctx.stats.afs;
    ctx->stats_attach.pltm_stats = &ctx->stats_ctx.stats.pltm;
    ctx->stats_attach.md_stats = &ctx->stats_ctx.stats;
    ctx->stats_attach.gtm_stats = &ctx->stats_ctx.stats;
    ctx->stats_attach.rolloff_stats = &ctx->stats_ctx.stats;
}

void isp_handle_stats(isp_lib_context_t *ctx, const void *buffer)
{
    const uint8_t *buf = (const uint8_t *)buffer;

    if (!ctx || !buffer)
        return;

    handle_ae(ctx, buf);
    handle_awb(ctx, buf);
    handle_af(ctx, buf);
    handle_afs(ctx, buf);
    handle_pltm(ctx, buf);
    stats_attach(ctx);
}

static void merge_ae(isp_lib_context_t *ctx, const uint8_t *b0,
                     const uint8_t *b1)
{
    isp_ae_stats_t *ae = &ctx->stats_ctx.stats.ae;
    const uint8_t *s0 = b0 + ISP_DMA_AE_OFF;
    const uint8_t *s1 = b1 + ISP_DMA_AE_OFF;
    const uint8_t *h0 = b0 + ISP_DMA_HIST_OFF;
    const uint8_t *h1 = b1 + ISP_DMA_HIST_OFF;
    uint32_t win_pix_n;
    int i;

    if (!ctx->isp_test_settings.ae_en)
        return;

    win_pix_n = stats_ae_win_pix_n(ctx);
    ae->win_pix_n = win_pix_n;

    for (i = 0; i < ISP_AE_WIN_N; i++) {
        int row = i / ISP_AE_WIN_W;
        int col = i % ISP_AE_WIN_W;
        const uint8_t *src;
        uint32_t r, g, b;

        if (col <= 11)
            src = s0 + (uint32_t)((row * 12 + col) * 24);
        else
            src = s1 + (uint32_t)((row * 12 + (col - 12)) * 24);

        r = (load_u32(src + 0) + load_u32(src + 12)) >> 1;
        g = (load_u32(src + 4) + load_u32(src + 16)) >> 1;
        b = (load_u32(src + 8) + load_u32(src + 20)) >> 1;
        ae->win_r[i] = r;
        ae->win_g[i] = g;
        ae->win_b[i] = b;
        ae->luma[i] = clamp_u8(freeisp_udiv(
            (10u * g + 4u * r + 2u * b) >> 4, win_pix_n));
    }

    for (i = 0; i < ISP_HIST_BIN_N; i++) {
        uint32_t lo = (uint32_t)(i >> 1) * 4;

        ae->hist[i] = (load_u32(h0 + lo) + load_u32(h1 + lo)) >> 1;
    }
}

static void merge_awb(isp_lib_context_t *ctx, const uint8_t *b0,
                      const uint8_t *b1)
{
    isp_awb_stats_t *awb = &ctx->stats_ctx.stats.awb;
    const uint8_t *p0 = b0 + ISP_DMA_AWB_OFF;
    const uint8_t *p1 = b1 + ISP_DMA_AWB_OFF;
    const uint8_t *c0 = p0 + ISP_DMA_AWB_CNT_OFF;
    const uint8_t *c1 = p1 + ISP_DMA_AWB_CNT_OFF;
    uint32_t ratio_r, ratio_b;
    int i;

    if (!ctx->isp_test_settings.awb_en)
        return;

    awb_wb_ratios(ctx, &ratio_r, &ratio_b);

    for (i = 0; i < ISP_AWB_WIN_N; i++) {
        int use1 = (i & 16) != 0;
        int idx = (i / 32) * 16 + (i & 15);
        const uint8_t *src = (use1 ? p1 : p0) + (uint32_t)idx * 24;
        const uint8_t *cntp = (use1 ? c1 : c0) + (uint32_t)idx * 4;
        uint32_t r = (load_u32(src + 0) + load_u32(src + 12)) >> 1;
        uint32_t g = (load_u32(src + 4) + load_u32(src + 16)) >> 1;
        uint32_t b = (load_u32(src + 8) + load_u32(src + 20)) >> 1;
        uint32_t c = (load_u16(cntp + 2) + load_u16(cntp + 0)) >> 1;
        uint32_t sr, sg, sb;

        awb->count[i] = c;
        if (c == 0) {
            awb->sum_r[i] = 0;
            awb->sum_g[i] = 0;
            awb->sum_b[i] = 0;
            awb->avg_r[i] = 0;
            awb->avg_g[i] = 0;
            awb->avg_b[i] = 0;
            continue;
        }

        sr = ratio_r ? ((ratio_r >> 1) + r * 256u) / ratio_r : r * 256u;
        sg = g;
        sb = ratio_b ? ((ratio_b >> 1) + b * 256u) / ratio_b : b * 256u;

        awb->sum_r[i] = sr;
        awb->sum_g[i] = sg;
        awb->sum_b[i] = sb;
        awb->avg_r[i] = (sr + c / 2) / c;
        awb->avg_g[i] = (sg + c / 2) / c;
        awb->avg_b[i] = (sb + c / 2) / c;
    }
}

static void merge_afs(isp_lib_context_t *ctx, const uint8_t *b0,
                      const uint8_t *b1)
{
    isp_afs_stats_t *afs = &ctx->stats_ctx.stats.afs;
    const uint8_t *s0 = b0 + ISP_DMA_AFS_OFF;
    const uint8_t *s1 = b1 + ISP_DMA_AFS_OFF;
    int i;

    if (!ctx->isp_test_settings.afs_en)
        return;

    for (i = 0; i < ISP_AFS_SUM_N; i++)
        afs->sum[i] = (load_u32(s0 + (uint32_t)i * 4) +
                       load_u32(s1 + (uint32_t)i * 4)) >> 1;
    afs->pic_w = ctx->stats_ctx.pic_w;
    afs->pic_h = ctx->stats_ctx.pic_h;
}

static void merge_pltm(isp_lib_context_t *ctx, const uint8_t *b0,
                       const uint8_t *b1)
{
    isp_pltm_stats_t *pltm = &ctx->stats_ctx.stats.pltm;
    const uint8_t *p0 = b0 + ISP_DMA_PLTM_OFF;
    const uint8_t *p1 = b1 + ISP_DMA_PLTM_OFF;
    int i;

    if (!ctx->isp_test_settings.pltm_en)
        return;

    for (i = 0; i < ISP_PLTM_WIN_N; i++) {
        uint16_t v0 = load_u16(p0 + (uint32_t)i * 2);
        uint16_t v1 = load_u16(p1 + (uint32_t)i * 2);

        pltm->lst[i] = (uint16_t)((v0 + v1) >> 1);
    }
}

void isp_handle_stats_sync(isp_lib_context_t *ctx, const void *buf0,
                           const void *buf1)
{
    const uint8_t *b0 = (const uint8_t *)buf0;
    const uint8_t *b1 = (const uint8_t *)buf1;

    if (!ctx || !buf0 || !buf1)
        return;

    merge_ae(ctx, b0, b1);
    merge_awb(ctx, b0, b1);
    merge_afs(ctx, b0, b1);
    merge_pltm(ctx, b0, b1);
    stats_attach(ctx);
}

/* ------------------------------------------------------------------ */
/* 3.3 isp_apply_settings                                              */
/* ------------------------------------------------------------------ */

static void set_awb_win(isp_lib_context_t *ctx)
{
    isp_h3a_reg_win_t *w = &ctx->module_cfg.awb_cfg.awb_reg_win;
    int32_t pw = (int32_t)ctx->stats_ctx.pic_w;
    int32_t ph = (int32_t)ctx->stats_ctx.pic_h;
    int32_t x1, y1, x2, y2, nw, nh;

    w->hor_num = 32;
    w->ver_num = 32;

    x1 = pw * (ctx->ae_settings.awb_coor[0] + 1000) / 2000;
    y1 = ph * (ctx->ae_settings.awb_coor[1] + 1000) / 2000;
    x2 = pw * (ctx->ae_settings.awb_coor[2] + 1000) / 2000 - x1;
    y2 = ph * (ctx->ae_settings.awb_coor[3] + 1000) / 2000 - y1;

    nw = x2 >> 5;
    if (nw < 4)
        nw = 4;
    if (nw > 0x70)
        nw = 0x70;
    nh = y2 >> 5;
    if (nh < 4)
        nh = 4;
    if (nh > 0xa8)
        nh = 0xa8;

    x1 += (x2 - nw * 32) >> 1;
    y1 += (y2 - nh * 32) >> 1;

    w->width = (uint32_t)nw;
    w->height = (uint32_t)nh;
    w->hor_start = (uint32_t)((x1 < pw) ? x1 : pw);
    w->ver_start = (uint32_t)((y1 < ph) ? y1 : ph);
}

static void set_ae_win(isp_lib_context_t *ctx)
{
    isp_h3a_reg_win_t *ae = &ctx->module_cfg.ae_cfg.ae_reg_win;
    isp_h3a_reg_win_t *hist = &ctx->module_cfg.hist_cfg.hist_reg_win;
    int32_t pw = (int32_t)ctx->stats_ctx.pic_w;
    int32_t ph = (int32_t)ctx->stats_ctx.pic_h;

    ae->hor_num = 24;
    ae->ver_num = 16;

    if (ctx->ae_spot) {
        int32_t x1 = pw * (ctx->ae_coor[0] + 1000) / 2000;
        int32_t y1 = ph * (ctx->ae_coor[1] + 1000) / 2000;
        int32_t x2 = pw * (ctx->ae_coor[2] + 1000) / 2000;
        int32_t y2 = ph * (ctx->ae_coor[3] + 1000) / 2000;
        int32_t nw = (x2 - x1) / 24;
        int32_t nh = (y2 - y1) >> 4;

        if (nw < 4)
            nw = 4;
        if (nw > 0x70)
            nw = 0x70;
        if (nh < 4)
            nh = 4;
        if (nh > 0xa8)
            nh = 0xa8;

        ae->width = (uint32_t)nw;
        ae->height = (uint32_t)nh;
        x2 -= nw * 24;
        y2 -= nh * 16;
        ae->hor_start = (uint32_t)((x2 < pw) ? x2 : pw);
        ae->ver_start = (uint32_t)((y2 < ph) ? y2 : ph);
    } else {
        ae->width = (uint32_t)(pw / 24);
        ae->height = (uint32_t)(ph >> 4);
        ae->hor_start = (uint32_t)((pw % 24) >> 1);
        ae->ver_start = (uint32_t)((ph & 0xf) >> 1);
    }

    hist->hor_num = 1;
    hist->ver_num = 1;
    hist->width = (uint32_t)pw;
    hist->height = (uint32_t)ph;
    hist->hor_start = 0;
    hist->ver_start = 0;
}

static void set_af_win(isp_lib_context_t *ctx)
{
    isp_h3a_reg_win_t *af = &ctx->module_cfg.af_cfg.af_reg_win;
    int32_t pw = (int32_t)ctx->stats_ctx.pic_w;
    int32_t ph = (int32_t)ctx->stats_ctx.pic_h;

    if (ctx->af_spot) {
        int32_t x1 = pw * (ctx->af_coor[0] + 1000) / 2000;
        int32_t y1 = ph * (ctx->af_coor[1] + 1000) / 2000;
        int32_t x2 = pw * (ctx->af_coor[2] + 1000) / 2000;
        int32_t y2 = ph * (ctx->af_coor[3] + 1000) / 2000;

        af->hor_num = 1;
        af->ver_num = 1;
        af->width = (uint32_t)(x2 - x1);
        af->height = (uint32_t)(y2 - y1);
        af->hor_start = (uint32_t)x1;
        af->ver_start = (uint32_t)y1;
    } else {
        af->hor_num = 24;
        af->ver_num = 16;
        af->width = (uint32_t)(pw / 24);
        af->height = (uint32_t)(ph >> 4);
        af->hor_start = 0;
        af->ver_start = 0;
    }
}

static int16_t enc_matrix(double v)
{
    int32_t t = (int32_t)(v * 1024.0);

    if (t < -1024)
        t = -1024;
    if (t > 1023)
        t = 1023;
    return (int16_t)t;
}

static int16_t enc_offset(double v)
{
    int32_t t = (int32_t)(v * 512.0);

    if (t < -1024)
        t = -1024;
    if (t > 1023)
        t = 1023;
    return (int16_t)t;
}

static void load_rgb2yuv_base(isp_lib_context_t *ctx, double a[3][3],
                              double b[3])
{
    const freeisp_tables_t *ft = tables_get();
    const base_tables_t *tb = ft ? ft->base : NULL;
    const int16_t *tbl = NULL;
    int cs = ctx->sensor_info.colour_space;
    int i, j;

    if (cs < 0)
        cs = 0;
    if (cs > ISP_RGB2YUV_SPACES - 1)
        cs = ISP_RGB2YUV_SPACES - 1;
    if (tb)
        tbl = tb->rgb2yuv_base[cs];

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++)
            a[i][j] = tbl ? ((double)tbl[3 * i + j] / 1024.0) : 0.0;
        b[i] = tbl ? ((double)tbl[9 + i] / 512.0) : 0.0;
    }
}

static void build_rgb2yuv_effect(isp_lib_context_t *ctx)
{
    static const double c_none[3][3] = {
        { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 }
    };
    static const double c_gray[3][3] = {
        { 0.3, 0.6, 0.1 }, { 0.3, 0.6, 0.1 }, { 0.3, 0.6, 0.1 }
    };
    static const double c_neg[3][3] = {
        { -1.0, 0.0, 0.0 }, { 0.0, -1.0, 0.0 }, { 0.0, 0.0, -1.0 }
    };
    static const double c_antique[3][3] = {
        { 0.393, 0.769, 0.189 },
        { 0.349, 0.686, 0.168 },
        { 0.272, 0.534, 0.131 }
    };
    static const double c_r[3][3] = {
        { 1.5, 0.0, 0.0 }, { 0.0, 2.0 / 3.0, 0.0 }, { 0.0, 0.0, 2.0 / 3.0 }
    };
    static const double c_g[3][3] = {
        { 2.0 / 3.0, 0.0, 0.0 }, { 0.0, 1.5, 0.0 }, { 0.0, 0.0, 2.0 / 3.0 }
    };
    static const double c_b[3][3] = {
        { 2.0 / 3.0, 0.0, 0.0 }, { 0.0, 2.0 / 3.0, 0.0 }, { 0.0, 0.0, 1.5 }
    };
    static const double (*const c_tbl[7])[3] = {
        c_none, c_gray, c_neg, c_antique, c_r, c_g, c_b
    };
    static const double d_neg[3] = { 2.0, 2.0, 2.0 };
    static const double d_zero[3] = { 0.0, 0.0, 0.0 };
    static const double *const d_tbl[7] = {
        d_zero, d_zero, d_neg, d_zero, d_zero, d_zero, d_zero
    };
    double a[3][3], b[3], m[3][3];
    const double (*c)[3];
    const double *d;
    int e = ctx->isp_ini_cfg.color_effect;
    int i, j, k;

    if (e < 0)
        e = 0;
    if (e > 6)
        e = 6;
    c = c_tbl[e];
    d = d_tbl[e];

    load_rgb2yuv_base(ctx, a, b);

    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            double s = 0.0;

            for (k = 0; k < 3; k++)
                s += a[i][k] * c[k][j];
            m[i][j] = s;
        }

    for (i = 0; i < 3; i++) {
        double o = b[i];

        for (j = 0; j < 3; j++)
            o += a[i][j] * d[j];
        ctx->module_cfg.rgb2yuv.offset[i] = enc_offset(o);
    }
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            ctx->module_cfg.rgb2yuv.gain[i][j] = enc_matrix(m[i][j]);
}

static void build_rgb2yuv_hue(isp_lib_context_t *ctx)
{
    double a[3][3], b[3], m[3][3];
    double ang = (double)ctx->isp_ini_cfg.hue_level * 3.1415926 / 180.0;
    double cs = cos(ang);
    double sn = sin(ang);
    double j[3][3] = {
        { 1.0, 0.0, 0.0 },
        { 0.0, cs, sn },
        { 0.0, -sn, cs }
    };
    double hoff[3] = { 0.0, (1.0 - cs) - sn, sn + (1.0 - cs) };
    int i, k, l;

    load_rgb2yuv_base(ctx, a, b);

    for (i = 0; i < 3; i++)
        for (k = 0; k < 3; k++) {
            double s = 0.0;

            for (l = 0; l < 3; l++)
                s += j[i][l] * a[l][k];
            m[i][k] = s;
        }

    for (i = 0; i < 3; i++) {
        double o = hoff[i];

        for (k = 0; k < 3; k++)
            o += j[i][k] * b[k];
        ctx->module_cfg.rgb2yuv.offset[i] = enc_offset(o);
    }
    for (i = 0; i < 3; i++)
        for (k = 0; k < 3; k++)
            ctx->module_cfg.rgb2yuv.gain[i][k] = enc_matrix(m[i][k]);
}

static void set_ae_ini_from_gains(isp_lib_context_t *ctx)
{
    ctx->ae_param.ae_ini.gain_favour = ctx->isp_ini_cfg.gains.gain_favour;
    ctx->ae_param.ae_ini.analog_gain_min = ctx->isp_ini_cfg.gains.analog_gain_min;
    ctx->ae_param.ae_ini.analog_gain_max = ctx->isp_ini_cfg.gains.analog_gain_max;
    ctx->ae_param.ae_ini.digital_gain_min =
        ctx->isp_ini_cfg.gains.digital_gain_min << 2;
    ctx->ae_param.ae_ini.digital_gain_max =
        ctx->isp_ini_cfg.gains.digital_gain_max << 2;
}

void isp_apply_settings(isp_lib_context_t *ctx)
{
    uint32_t flags;
    int bit;

    if (!ctx)
        return;

    ctx->ae_entity_ctx.ae_param = &ctx->ae_param;
    flags = ctx->isp_3a_change_flags;

    for (bit = 0; bit < 12; bit++) {
        uint32_t mask = 1u << bit;

        if (!(flags & mask))
            continue;

        switch (bit) {
        case 0:
            freeisp_ae_set_params(&ctx->ae_entity_ctx, 1);
            break;
        case 1:
            set_awb_win(ctx);
            ctx->awb_frame_cnt = 0;
            break;
        case 2:
            ctx->afs_param.flicker_mode = ctx->ae_settings.flicker_mode;
            break;
        case 3:
        case 4:
        case 5:
        case 9:
            break;
        case 6:
            build_rgb2yuv_effect(ctx);
            break;
        case 7:
            set_af_win(ctx);
            ctx->af_frame_cnt = 0;
            break;
        case 8:
            if (ctx->ae_entity_ctx.ae_param && ctx->ae_spot) {
                ctx->ae_param.ae_setting = ctx->ae_settings;
                freeisp_ae_set_params(&ctx->ae_entity_ctx, 3);
            }
            set_ae_win(ctx);
            ctx->ae_frame_cnt = 0;
            break;
        case 10:
            build_rgb2yuv_hue(ctx);
            break;
        case 11:
            set_ae_ini_from_gains(ctx);
            freeisp_ae_set_params(&ctx->ae_entity_ctx, 1);
            break;
        default:
            break;
        }

        ctx->isp_3a_change_flags &= ~mask;
    }
}
