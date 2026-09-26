/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * ae_clean.c - clean-room auto-exposure module.
 *
 * Implements the behaviour specified in spec/ae.md.  All tuning
 * tables are injected at runtime; this file contains no table data.
 */
#include "ae_clean.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MIN(a, b)    ((a) < (b) ? (a) : (b))
#define MAX(a, b)    ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

/* Arithmetic right shift (spec 2: signed right shifts are arithmetic). */
#define SAR(x, n) ((x) >> (n))

/* ------------------------------------------------------------------ */
/* Internal state (spec 5)                                            */
/* ------------------------------------------------------------------ */

struct ae_entity {
    ae_params_t     p;                  /* parameter block             */
    const ae_clean_tables_t *tab;

    ae_setting_t    line_base[AE_IDX_MAX];
    ae_setting_t    line_50[AE_IDX_MAX];
    ae_setting_t    line_60[AE_IDX_MAX];
    ae_setting_t   *line_active;

    ae_desc_t       tbl_preview;
    ae_desc_t       tbl_capture;
    ae_desc_t       tbl_video;

    int32_t         interval_frame;
    int32_t         latency_count;
    int32_t         lum_change_cnt;
    int32_t         wdr_change_cnt;

    int32_t         hist_count[32];
    int32_t         hist_mean[32];
    int32_t         hist_conv[62];
    int32_t         peak_pos[8];
    int32_t         peak_val[8];
    int32_t         bin_weight[3];

    int32_t         wgrid_matrix[AE_NGRID];
    int32_t         wgrid_avg[AE_NGRID];
    int32_t         wgrid_center[AE_NGRID];
    int32_t         wgrid_touch[AE_NGRID];

    int32_t         flash_before_lum;
    int32_t         flash_after_lum;
    int32_t         flash_expect_lum;
    int32_t         flash_ev_accum;

    int32_t         frame_rate_max;
    int32_t         busy_flag;

    int32_t         frame_index;
    int32_t         target_comp;
    int32_t         cur_delta;
    int32_t         lv_adj;

    /* working settings (mirrored into the result block) */
    ae_setting_t    set_cur;
    ae_setting_t    set_last;
    ae_setting_t    set_curr;
    ae_setting_t    set_short;
    ae_setting_t    set_short_curr;

    ae_wdr_ratio_t  wdr_ratio;

    /* line-table build scratch */
    int32_t         idx_max;
    int32_t         analog_lo, analog_hi;
    int32_t         digital_lo, digital_hi;
    int32_t         total_lo, total_hi;
    int32_t         min_digital_gain;

    /* diagnostic sweep state (spec 6.4) */
    int32_t         diag_frame_cnt;
    int32_t         diag_value;
    int32_t         diag_dir;
    int32_t         diag_luma_hist[10];
    int32_t         diag_hist_pos;
};

static const ae_clean_tables_t *TAB(const ae_entity_t *e)
{
    return e->tab;
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static int32_t interp(int32_t x, int32_t x0, int32_t x1, int32_t y0, int32_t y1)
{
    if (x1 == x0)
        return y0;
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
}

static uint64_t isqrt_u64(uint64_t v)
{
    uint64_t x;
    if (v == 0)
        return 0;
    x = (uint64_t)sqrt((double)v);
    while ((x + 1) * (x + 1) <= v)
        x++;
    while (x * x > v)
        x--;
    return x;
}

static int32_t isqrt_i64(int64_t v)
{
    if (v <= 0)
        return 0;
    return (int32_t)isqrt_u64((uint64_t)v);
}

/* Section 13.8 */
static int32_t ae_time_to_line(ae_entity_t *e, int64_t time, int round_up)
{
    int64_t l, b;
    if (e->p.hts == 0)
        return 0;
    l = ((time * e->p.pclk) << 4) / 1000000;
    b = (l + e->p.hts / 2) / e->p.hts;
    if (b == 0)
        b = 1;
    if (b > 160000000)
        b = 160000000;
    if (b > 16) {
        if (round_up)
            b += 8;
        return (int32_t)(b & 0xfffffff0LL);
    }
    return (int32_t)b;
}

static int32_t ae_line_to_time(ae_entity_t *e, int64_t line)
{
    double v;
    if (e->p.pclk == 0)
        return 0;
    v = ((double)line / 16.0) * 1000000.0 * (double)e->p.hts / (double)e->p.pclk;
    return (int32_t)floor(v + 0.5);
}

/* Section 7.1 grid index, honouring mirror flags. */
static int32_t grid_index(const ae_entity_t *e, int row, int col)
{
    int r = (e->p.vflip == 1) ? (7 - row) : row;
    int c = (e->p.hflip == 1) ? (7 - col) : col;
    return r * 8 + c;
}

/* ------------------------------------------------------------------ */
/* Result helpers                                                     */
/* ------------------------------------------------------------------ */

void ae_default_result(ae_result_t *result)
{
    if (!result)
        return;
    memset(result, 0, sizeof(*result));
    result->setting_curr.analog_gain = 256;
    result->setting_curr.sensor_exp_line = 100;
    result->setting_curr.digital_gain = 1024;
    result->wdr_ratio.sensor = 96;
    result->wdr_ratio.hw_ratio = 96;
    /* The object seeds the short companion from the emitted setting, then
     * divides its exposure/line by the fixed 96 ratio, and copies the current
     * setting unchanged into the short-current slot. */
    result->setting_short = result->setting;
    result->setting_short.exposure_time /= 96;
    result->setting_short.sensor_exp_line /= 96;
    result->setting_short_curr = result->setting_curr;
    result->status = 0;
}

static void sync_result(ae_entity_t *e, ae_result_t *r)
{
    r->setting = e->set_cur;
    r->setting_last = e->set_last;
    r->setting_curr = e->set_curr;
    r->setting_short = e->set_short;
    r->setting_short_curr = e->set_short_curr;
    r->idx_max = e->idx_max;
    r->lv_adj = e->lv_adj;
    r->delta_idx = e->cur_delta;
    r->wdr_ratio = e->wdr_ratio;
    r->gain_ratio = e->p.gain_split_ratio;
    r->flash_ev_cumul = e->flash_ev_accum;
}

/* ------------------------------------------------------------------ */
/* Section 7: luminance acquisition and metering                      */
/* ------------------------------------------------------------------ */

static int32_t ae_meter(ae_entity_t *e, const ae_stats_t *st, ae_result_t *r,
                        int32_t *avg_lum_q8_out)
{
    ae_params_t *p = &e->p;
    const ae_clean_tables_t *t = TAB(e);
    const int32_t *G, *Gover = t->wght_over, *Gunder = t->wght_under;
    int total, row_expand, i;
    int32_t sum_lum_q8 = 0, sum_w_lum = 0, sum_w = 0, avg_lum_q8;

    switch (p->metering_mode) {
    case 2:  G = e->wgrid_touch;  break;
    case 0:  G = e->wgrid_avg;    break;
    case 1:  G = e->wgrid_center; break;
    default: G = e->wgrid_matrix; break;
    }

    total = (p->hdr_mode == 1) ? AE_NWIN_WDR : AE_NWIN;
    row_expand = (p->hdr_mode == 1) ? 1 : 2;

    for (i = 0; i < total; i++) {
        int col = (i % 24) / 3;
        int row = (i / 24) / row_expand;
        int gi = grid_index(e, row, col);
        int32_t lum = st->win_avg[i];
        int32_t w = G[gi];
        int32_t term;

        sum_lum_q8 += lum * 256;

        if (p->metering_mode == 2 || (lum > 9 && lum < 236)) {
            term = lum * w * 256;
            sum_w += w;
        } else if (lum <= 9) {
            int32_t iso = Gunder[gi];
            int32_t f = interp(lum, 0, 10, p->exposure_cfg[1], 256);
            int32_t n;
            term = lum * f * iso * w;
            n = iso * p->exposure_cfg[1] * w;
            if (n < 0) n += 255;
            sum_w += SAR(n, 8);
        } else {
            int32_t iso = Gover[gi];
            int32_t f = interp(lum, 235, 255, 256, p->exposure_cfg[0]);
            int32_t n;
            term = lum * f * iso * w;
            n = iso * p->exposure_cfg[0] * w;
            if (n < 0) n += 255;
            sum_w += SAR(n, 8);
        }
        sum_w_lum += term;
    }

    avg_lum_q8 = (sum_lum_q8 + total / 2) / total;
    r->avg_lum = SAR(avg_lum_q8, 8);
    *avg_lum_q8_out = avg_lum_q8;
    if (sum_w == 0)
        return 0;
    return (sum_w_lum + sum_w / 2) / sum_w;
}

static int32_t ae_hist_lum(ae_entity_t *e, const ae_stats_t *st)
{
    int i;
    int64_t num = 0, den = 0;
    for (i = 0; i < AE_NHIST; i++) {
        int32_t w;
        if (i < 5)
            w = interp(i, 0, 5, e->p.exposure_cfg[3], 256);
        else if (i <= 230)
            w = 256;
        else
            w = interp(i, 230, 255, 256, e->p.exposure_cfg[2]);
        num += (int64_t)i * st->hist[i] * w * 256;
        den += (int64_t)st->hist[i] * w;
    }
    if (den == 0)
        return 0;
    return (int32_t)(num / den);
}

/* Section 7.3 touch (spot) metering grid. */
static void ae_build_touch_grid(ae_entity_t *e)
{
    ae_params_t *p = &e->p;
    const ae_clean_tables_t *t = TAB(e);
    int32_t cx, cy, rx, ry, k;
    int row, col;

    cx = (((p->meter_roi.x1 + p->meter_roi.x2) / 2) * 7 + 8000) / 2000;
    cy = (((p->meter_roi.y1 + p->meter_roi.y2) / 2) * 7 + 8000) / 2000;
    rx = (((p->meter_roi.x2 - p->meter_roi.x1) / 2) * 7 + 1000) / 2000;
    ry = (((p->meter_roi.y2 - p->meter_roi.y1) / 2) * 7 + 1000) / 2000;
    cx = CLAMP(cx, 0, 7);
    cy = CLAMP(cy, 0, 7);
    rx = CLAMP(rx, 0, 3);
    ry = CLAMP(ry, 0, 3);

    k = p->touch_distance_index;
    if (k < 0) k = -k;
    k = CLAMP(k, 0, 5);

    for (row = 0; row < 8; row++) {
        for (col = 0; col < 8; col++) {
            int32_t ax = col - cx, ay = row - cy;
            int32_t dy, d;
            if (ax < 0) ax = -ax;
            if (ay < 0) ay = -ay;
            dy = ay - ry;
            if (ax > rx) {
                d = ax - rx;
                if (ay > ry)
                    d = isqrt_i64((int64_t)d * d + (int64_t)dy * dy);
            } else {
                d = (ay > ry) ? (dy < 0 ? -dy : dy) : 0;
            }
            d = CLAMP(d, 0, 11);
            e->wgrid_touch[row * 8 + col] = (int32_t)t->touchprob[k * 12 + d];
        }
    }
}

/* ------------------------------------------------------------------ */
/* Section 8: backlight scene classification                          */
/* ------------------------------------------------------------------ */

static int32_t luma_at(const ae_stats_t *st, int idx)
{
    if (idx < 0)
        return 0;
    if (idx < AE_NWIN)
        return st->win_avg[idx];
    if (idx < AE_NWIN + AE_NHIST)
        return st->hist[idx - AE_NWIN];
    return 0;
}

static int32_t night_mode_weight(ae_entity_t *e)
{
    int32_t d = e->idx_max - e->set_last.index;
    int32_t ad = (d < 0) ? -d : d;
    if (ad > 256)
        ad = 256;
    if (d < 25)
        return 64;
    if (d < 50)
        return (50 - ad) * 64 / 25;
    return 0;
}

static int32_t ae_backlight_centroid(ae_entity_t *e, const ae_stats_t *st,
                                     ae_result_t *r)
{
    ae_params_t *p = &e->p;
    const ae_clean_tables_t *t = TAB(e);
    int i, k, pidx;
    int32_t grid[64];
    int32_t neutral_th, dark_th, bright_th, mid;
    int32_t dark_pos = 0, bright_pos = 0, dark_val = 0, bright_val = 0;
    int32_t backlight;

    /* 8.1 collapse to 32 blocks */
    for (i = 0; i < 32; i++) {
        int32_t cnt = 0, s = 0, b;
        for (b = i * 8; b < i * 8 + 8; b++) {
            cnt += st->hist[b];
            s += b * st->hist[b];
        }
        e->hist_count[i] = cnt;
        e->hist_mean[i] = (cnt == 0) ? (i * 8 + 4) : (s / cnt);
    }

    /* 8.2 adaptive convolution and peak search */
    {
        int attempt = 1;
        for (;;) {
            int row = CLAMP(attempt + p->kernel_index, 0, 19);
            const int32_t *ker = t->kernel + row * 31;
            int npeaks = 0;

            memset(e->hist_conv, 0, sizeof(e->hist_conv));
            for (k = 0; k < 62; k++) {
                int32_t acc = e->hist_conv[k];
                int m;
                for (m = 0; m < 32; m++) {
                    int n = k - m;
                    if (n < 0 || n >= 31)
                        continue;
                    acc += (e->hist_count[m] * ker[n]) >> 10;
                }
                e->hist_conv[k] = acc;
            }

            for (k = 1; k < 61; k++) {
                if (e->hist_conv[k - 1] <= e->hist_conv[k] &&
                    e->hist_conv[k + 1] < e->hist_conv[k]) {
                    if (npeaks < 8) {
                        int pi = k - 15;
                        e->peak_pos[npeaks] =
                            (pi >= 0 && pi <= 31) ? e->hist_mean[pi] : 0;
                        e->peak_val[npeaks] = e->hist_conv[k];
                        npeaks++;
                    }
                }
            }

            if (npeaks == 2) {
                int32_t pa = CLAMP(e->peak_pos[0] - 1, 0, 255);
                int32_t pb = CLAMP(e->peak_pos[1] - 1, 0, 255);
                if (pa <= pb) {
                    dark_pos = pa; dark_val = e->peak_val[0];
                    bright_pos = pb; bright_val = e->peak_val[1];
                } else {
                    dark_pos = pb; dark_val = e->peak_val[1];
                    bright_pos = pa; bright_val = e->peak_val[0];
                }
                break;
            }
            if (npeaks == 0) {
                dark_pos = bright_pos = 0;
                dark_val = bright_val = 0;
                break;
            }
            if (npeaks == 1 || (19 - p->kernel_index) <= attempt) {
                dark_pos = bright_pos = CLAMP(e->peak_pos[0] - 1, 0, 255);
                dark_val = bright_val = e->peak_val[0];
                break;
            }
            attempt++;
        }
    }

    /* 8.3 class thresholds and midpoint */
    neutral_th = CLAMP((253 - bright_pos < 0 ? bright_pos - 253 : 253 - bright_pos) / 2, 10, 95);
    if (dark_val == bright_val) {
        dark_th = bright_th = 35;
        mid = (dark_pos + bright_pos) >> 1;
    } else {
        mid = (dark_pos * dark_val + bright_pos * bright_val) / (dark_val + bright_val);
        dark_th = CLAMP((mid - dark_pos < 0 ? dark_pos - mid : mid - dark_pos) / 2, 10, 95);
        bright_th = CLAMP((bright_pos - mid < 0 ? mid - bright_pos : bright_pos - mid) / 2, 10, 95);
    }

    /* 8.4 spatial grid (fixed addressing, spec 8.4) */
    for (i = 0; i < 64; i++) {
        int32_t acc = 0;
        int j, q;
        for (j = 0; j < 4; j++) {
            for (q = 0; q < 6; q++) {
                int idx = (i >> 3) * 96 + j * 24 + (i & 7) * 6 + q;
                acc += luma_at(st, idx);
            }
        }
        grid[i] = acc / 24;
    }

    /* 8.5 classification */
    if ((uint32_t)(bright_pos - dark_pos) < 101u) {
        if ((uint32_t)(bright_pos - dark_pos) < 21u) {
            backlight = 32;
        } else {
            int32_t mask[64], dist[3];
            int lo = 0, hi = 0;
            for (i = 0; i < 64; i++)
                mask[i] = (mid <= grid[i]) ? 1 : 0;
            for (pidx = 0; pidx < 3; pidx++) {
                int32_t s = 0;
                for (i = 1; i < 64; i++) {
                    int32_t v = mask[i] - t->blmask[pidx * 64 + i];
                    s += (v < 0) ? -v : v;
                }
                dist[pidx] = s;
            }
            for (pidx = 1; pidx < 3; pidx++) {
                if (dist[pidx] < dist[lo]) lo = pidx;
                if (dist[pidx] > dist[hi]) hi = pidx;
            }
            {
                int32_t comp = 64 - dist[lo];
                int32_t picked = dist[lo];
                if (comp < 0) comp = -comp;
                if (dist[hi] <= comp)
                    picked = dist[hi];
                picked -= night_mode_weight(e);
                backlight = picked > 0 ? picked : 0;
            }
        }
    } else {
        int32_t x[64], h[10];
        int64_t s0 = 0, s1 = 0;
        for (i = 0; i < 64; i++)
            x[i] = (grid[i] < mid) ? -1 : 1;
        for (pidx = 0; pidx < 10; pidx++) {
            int64_t s = t->net_bias[pidx];
            for (i = 0; i < 64; i++)
                s += (int64_t)x[i] * t->net_in[pidx * 64 + i];
            h[pidx] = (int32_t)(s / 100);
        }
        for (pidx = 0; pidx < 10; pidx++) {
            s0 += (int64_t)h[pidx] * t->net_out[pidx];
            s1 += (int64_t)h[pidx] * t->net_out[10 + pidx];
        }
        s0 = s0 / 100 + 3561;
        s1 = s1 / 100 + 6438;
        if (s0 < s1)
            backlight = CLAMP(40 - (int32_t)(s1 / 800), 0, 32);
        else
            backlight = CLAMP(20 + (int32_t)(s0 / 800), 32, 64);
    }
    backlight = MIN(backlight, 64);

    /* 8.6 class bin weights */
    {
        int32_t e10 = p->exposure_cfg[10], e11 = p->exposure_cfg[11];
        int32_t e12 = p->exposure_cfg[12], e13 = p->exposure_cfg[13];
        int32_t w0, w1, w2, tt;
        tt = backlight * (e11 - e10);
        if (tt < 0) tt += 63;
        w0 = e10 + (tt >> 6);
        w1 = (dark_pos != bright_pos) ? 10 : 0;
        tt = backlight * (e13 - e12);
        if (tt < 0) tt += 63;
        w2 = e13 - (tt >> 6);
        if (p->light_mode == 1)
            w1 += 16;
        else if (p->light_mode == 2)
            w0 += 16;
        e->bin_weight[0] = w0;
        e->bin_weight[1] = w1;
        e->bin_weight[2] = w2;

        /* 8.7 probability-weighted centroid */
        {
            int64_t num = 0, den = 0;
            for (i = 0; i < AE_NHIST; i++) {
                int32_t dd = i - dark_pos; if (dd < 0) dd = -dd;
                int32_t db = i - bright_pos; if (db < 0) db = -db;
                int32_t dn = i - 253; if (dn < 0) dn = -dn;
                int32_t pd = t->auxprob[dark_th * 256 + dd];
                int32_t pb = t->auxprob[bright_th * 256 + db];
                int32_t pn = t->auxprob[neutral_th * 256 + dn];
                int64_t term = (int64_t)st->hist[i] * (pd * w0 + pb * w1 + pn * w2);
                num += (int64_t)i * term;
                den += term;
            }
            r->bright_pos = bright_pos;
            r->dark_pos = dark_pos;
            r->backlight = backlight;
            if (den == 0)
                return 0;
            return (int32_t)((num * 256) / den);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Section 9: HDR / WDR blend ratio                                   */
/* ------------------------------------------------------------------ */

static void ae_update_hdr_ratio(ae_entity_t *e, const ae_stats_t *st, ae_result_t *r)
{
    int32_t ratio = e->wdr_ratio.tmp;
    int64_t blk;
    int32_t h[AE_NHIST];
    int32_t hl = 0, hm = 0, hh = 0;
    int32_t tgt, adiff;
    double f;
    int i;

    if (ratio == 0)
        ratio = 256;
    blk = ((int64_t)e->p.sensor_height * e->p.sensor_width) / 192 + 1;
    if (blk < 1)
        blk = 1;

    memset(h, 0, sizeof(h));
    for (i = 0; i < 192; i++) {
        int32_t lo = (int32_t)(((int64_t)(st->accum_r[i] + st->accum_g[i]) * 3 +
                                st->accum_b[i]) / blk);
        int32_t sh = (int32_t)(((int64_t)(st->accum_r[192 + i] + st->accum_g[192 + i]) * 3 +
                                st->accum_b[192 + i]) / blk);
        lo = CLAMP(lo, 0, 255);
        sh = CLAMP(sh, 0, 255);
        h[CLAMP(lo + sh, 0, 255)]++;
    }
    for (i = 0; i < 10; i++)
        hl += h[i];
    for (i = 10; i <= 245; i++)
        hm += h[i];
    for (i = 246; i < AE_NHIST; i++)
        hh += h[i];

    f = (double)hl / 192.0;
    f = CLAMP(f, 0.01, 1.0);
    r->hist_low = (int32_t)(f * 10000);
    f = (double)hm / 192.0;
    f = CLAMP(f, 0.01, 1.0);
    r->hist_mid = (int32_t)(f * 10000);
    f = (double)hh / 192.0;
    f = CLAMP(f, 0.01, 1.0);
    r->hist_hi = (int32_t)(f * 10000);

    tgt = interp(r->hist_hi, 100, 9300, 160, 304);
    tgt = CLAMP(tgt, 160, 304);
    adiff = ratio - tgt;
    if (adiff < 0)
        adiff = -adiff;
    e->wdr_ratio.tmp = ratio;

    if (adiff < 5) {
        e->wdr_change_cnt = 0;
    } else {
        e->wdr_change_cnt++;
        if (e->wdr_change_cnt > 3) {
            int32_t d = ratio - tgt;
            int32_t adj;
            if (d < 0) d = -d;
            if (d > 127) d = 127;
            adj = (int32_t)TAB(e)->conv[15 * 128 + d];
            e->wdr_ratio.tmp = ratio + (tgt < ratio ? -adj : adj);
        }
    }
    e->wdr_ratio.last = ratio;

    if (e->p.wdr_cfg[3] == 0) {
        e->wdr_ratio.tmp = e->p.wdr_cfg[0] << 4;
        e->wdr_change_cnt = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Section 13: exposure line-table construction                       */
/* ------------------------------------------------------------------ */

/* Section 13.9 / 15.10 */
static int32_t nearest_aperture(ae_entity_t *e, ae_setting_t *s)
{
    const int32_t *lad = e->p.aperture_ladder;
    int32_t f = s->f_number;
    int i;
    for (i = 0; i < 15; i++) {
        if (lad[i] <= f && f < lad[i + 1]) {
            s->f_number = lad[i];
            return lad[i] * lad[i];
        }
    }
    return s->fno2;
}

/* Section 13.4 gain split (also used by 13.6). */
static void split_gains(ae_entity_t *e, int64_t total_gain, int64_t ev,
                        int64_t exp_time, int64_t fno2,
                        int32_t *analog_out, int32_t *digital_out)
{
    int64_t ag, dg;
    if (exp_time <= 0)
        exp_time = 1;
    if (e->p.gain_split_ratio < 0.1) {
        ag = ((fno2 * ev / 10000) / exp_time) & ~0xfLL;
        ag = CLAMP(ag, e->analog_lo, e->analog_hi);
    } else {
        dg = isqrt_i64((int64_t)((double)(total_gain << 10) / e->p.gain_split_ratio));
        dg = CLAMP(dg, e->digital_lo, e->digital_hi);
        ag = ((total_gain << 10) / (dg ? dg : 1)) & ~0xfLL;
        ag = CLAMP(ag, e->analog_lo, e->analog_hi);
    }
    {
        int64_t q = ev / exp_time;
        dg = (((q * fno2) << 10) / 10000) / (ag ? ag : 1);
        dg = CLAMP(dg, e->digital_lo, e->digital_hi);
    }
    *analog_out = (int32_t)ag;
    *digital_out = (int32_t)dg;
}

/* Section 13.6 flicker-aligned entry. */
static void ae_align(ae_entity_t *e, ae_setting_t *dst, const ae_setting_t *src,
                     int32_t ceiling)
{
    int64_t exp = src->exposure_time;
    int64_t g = 0;
    if (ceiling < exp)
        exp = (int64_t)ceiling * (exp / ceiling);
    dst->ev = src->ev;
    dst->sensor_exp_line = ae_time_to_line(e, exp, 0);
    dst->exposure_time = ae_line_to_time(e, dst->sensor_exp_line);
    dst->f_number = src->f_number;
    dst->fno2 = nearest_aperture(e, dst);
    if (dst->exposure_time > 0)
        g = (int64_t)dst->fno2 * dst->ev / 10000 / dst->exposure_time;
    g = CLAMP(g, e->total_lo, e->total_hi);
    dst->total_gain = (int32_t)g;
    split_gains(e, g, dst->ev, dst->exposure_time, dst->fno2,
                &dst->analog_gain, &dst->digital_gain);
    dst->index = src->index;
    dst->ev_idx = src->index;
    dst->lv = src->lv;
    if (dst->digital_gain > 0 && dst->digital_gain < e->min_digital_gain)
        e->min_digital_gain = dst->digital_gain;
}

static void ae_build_line_tables(ae_entity_t *e, const ae_desc_t *d)
{
    ae_params_t *p = &e->p;
    const ae_clean_tables_t *t = TAB(e);
    ae_segment_t seg[AE_NSEG];
    int64_t ev_lo[AE_NSEG], ev_hi[AE_NSEG];
    uint32_t len = d->length;
    uint32_t scale;
    int64_t top_ev = 0;
    int i, idx;

    if (len > AE_NSEG)
        len = AE_NSEG;
    for (i = 0; i < AE_NSEG; i++) {
        seg[i] = d->seg[i];
        ev_lo[i] = ev_hi[i] = 0;
    }
    scale = (uint32_t)((int64_t)1000000 * (d->shutter_shift > 1 ? d->shutter_shift : 1));
    for (i = 0; i < (int)len; i++) {
        uint32_t max_exp = seg[i].max_exp ? scale / seg[i].max_exp : 0;
        uint32_t min_exp = seg[i].min_exp ? scale / seg[i].min_exp : 0;
        uint32_t min_iris = seg[i].min_iris * seg[i].min_iris;
        uint32_t max_iris = seg[i].max_iris * seg[i].max_iris;
        seg[i].max_exp = max_exp;
        seg[i].min_exp = min_exp;
        seg[i].min_iris = min_iris;
        seg[i].max_iris = max_iris;
        ev_lo[i] = (int64_t)min_exp * seg[i].min_gain * 10000 / (min_iris ? min_iris : 1);
        ev_hi[i] = (int64_t)max_exp * seg[i].max_gain * 10000 / (max_iris ? max_iris : 1);
    }

    if (p->analog_gain_range[0] == 0 || p->analog_gain_range[1] == 0) {
        p->analog_gain_range[0] = (int32_t)((uint32_t)p->gain_min << 4);
        p->analog_gain_range[1] = (int32_t)((uint32_t)p->gain_max << 4);
    }
    if (p->digital_gain_range[0] == 0 || p->digital_gain_range[1] == 0) {
        p->digital_gain_range[0] = 1024;
        p->digital_gain_range[1] = 3072;
    }
    e->analog_lo = p->analog_gain_range[0];
    e->analog_hi = p->analog_gain_range[1];
    e->digital_lo = p->digital_gain_range[0];
    e->digital_hi = p->digital_gain_range[1];
    if (len > 0) {
        p->total_gain_range[0] = (int32_t)d->seg[0].min_gain;
        p->total_gain_range[1] = (int32_t)d->seg[len - 1].max_gain;
    }
    e->total_lo = p->total_gain_range[0];
    e->total_hi = p->total_gain_range[1];

    e->idx_max = 0;
    e->min_digital_gain = 1024;
    memset(e->line_base, 0, sizeof(e->line_base));
    memset(e->line_50, 0, sizeof(e->line_50));
    memset(e->line_60, 0, sizeof(e->line_60));
    if (len > 0)
        top_ev = ev_hi[len - 1];

    for (idx = 0; idx < AE_IDX_MAX; idx++) {
        double dv = 1000.0 * pow(2.0, (double)(idx * 40) / 1000.0);
        uint64_t evt = (uint64_t)(dv < 0.0 ? 0.0 : dv);   /* truncate (VFP) */
        int64_t ev, exp_time = 0, total_gain = 0, fno2 = 0, true_exp_line, tt, ag;
        ae_setting_t *st = &e->line_base[idx];
        int s = -1;

        if (evt > 0xffffffffULL)
            evt = 0xffffffffULL;
        t->evtab[idx] = (uint32_t)evt;
        ev = (len > 0) ? (int64_t)ev_lo[0] * (int64_t)t->evtab[idx] / 1000 : 0;
        if (len > 0 && ev > top_ev)
            break;

        for (i = 0; i < (int)len; i++) {
            if (ev_lo[i] <= ev && ev < ev_hi[i]) {
                s = i;
                break;
            }
        }
        if (s < 0)
            continue;

        /* 13.3 segment solve */
        if (seg[s].min_exp == seg[s].max_exp) {
            if (seg[s].min_iris == seg[s].max_iris) {
                if (seg[s].min_gain == seg[s].max_gain) {
                    exp_time = (int64_t)seg[s].min_iris * ev / 10000 /
                               (seg[s].min_gain ? seg[s].min_gain : 1);
                    total_gain = seg[s].min_gain;
                    fno2 = seg[s].min_iris;
                } else {
                    exp_time = seg[s].min_exp;
                    total_gain = (int64_t)seg[s].max_iris * ev / 10000 /
                                 (seg[s].min_exp ? seg[s].min_exp : 1);
                    fno2 = seg[s].min_iris;
                }
            } else {
                exp_time = seg[s].min_exp;
                total_gain = seg[s].min_gain;
                fno2 = ev ? (int64_t)seg[s].min_gain * seg[s].min_exp * 10000 / ev : 0;
            }
        } else {
            exp_time = (int64_t)seg[s].min_iris * ev / 10000 /
                       (seg[s].min_gain ? seg[s].min_gain : 1);
            total_gain = seg[s].min_gain;
            fno2 = seg[s].min_iris;
        }

        st->ev = (int32_t)ev;
        split_gains(e, total_gain, ev, exp_time, fno2,
                    &st->analog_gain, &st->digital_gain);

        /* 13.5 per-entry scalars and final gain rebalance */
        st->f_number = isqrt_i64(fno2);
        st->fno2 = (int32_t)fno2;
        st->exposure_time = (int32_t)exp_time;
        st->sensor_exp_line = ae_time_to_line(e, exp_time, 0);
        true_exp_line = ((((exp_time * p->pclk) << 4) / 1000000) +
                         (p->hts ? p->hts / 2 : 0)) /
                        (p->hts ? p->hts : 1);
        if (true_exp_line == 0)
            true_exp_line = 1;
        if (true_exp_line > 160000000)
            true_exp_line = 160000000;
        if (st->sensor_exp_line != 0)
            total_gain = total_gain * true_exp_line / st->sensor_exp_line;
        st->total_gain = (int32_t)total_gain;
        tt = total_gain << 10;
        ag = (tt / (st->digital_gain ? st->digital_gain : 1)) & ~0xfLL;
        st->analog_gain = (int32_t)CLAMP(ag, e->analog_lo, e->analog_hi);
        st->digital_gain = (int32_t)CLAMP(tt / (st->analog_gain ? st->analog_gain : 1),
                                          e->digital_lo, e->digital_hi);
        st->index = idx;
        st->ev_idx = idx;
        st->lv = CLAMP(p->max_lv - idx * 4, 0, 3000);

        /* 13.6 flicker-aligned companion tables */
        if (st->exposure_time != 0) {
            ae_align(e, &e->line_50[idx], st, 10000);
            ae_align(e, &e->line_60[idx], st, 8333);
        }
        if (st->exposure_time != 0)
            e->idx_max = idx;
        if (st->digital_gain > 0 && st->digital_gain < e->min_digital_gain)
            e->min_digital_gain = st->digital_gain;
    }

    /* 13.7 digital-gain normalisation */
    if (e->min_digital_gain > 0) {
        for (i = 0; i < 3; i++) {
            ae_setting_t *tab = (i == 0) ? e->line_base
                             : (i == 1) ? e->line_50 : e->line_60;
            for (idx = 0; idx < e->idx_max; idx++) {
                tab[idx].digital_gain = (int32_t)
                    (((int64_t)tab[idx].digital_gain << 10) / e->min_digital_gain);
            }
        }
    }
}

/* Section 13.10 table selection and starting setting. */
static void ae_set_line_table(ae_entity_t *e)
{
    ae_params_t *p = &e->p;
    const ae_desc_t *d;
    int i;

    if (p->scene >= 0 && p->scene < AE_NSCENE)
        d = &p->scene_tables[p->scene];
    else
        d = &p->scene_tables[2];
    ae_build_line_tables(e, d);

    if (e->set_curr.sensor_exp_line == 0) {
        i = p->test_forced;
        if (i < 21)
            i = e->idx_max / 2;
    } else {
        i = e->set_curr.index;
    }
    i = CLAMP(i, 0, AE_IDX_MAX - 1);
    e->set_cur = e->set_curr = e->set_last = e->line_base[i];
}

/* ------------------------------------------------------------------ */
/* Section 14: per-mode exposure configuration                        */
/* ------------------------------------------------------------------ */

static void ae_configure_sensor(ae_entity_t *e, int32_t index)
{
    ae_params_t *p = &e->p;
    ae_setting_t *active;
    ae_setting_t *s;
    int32_t j;

    if (p->mains_detected == 0)
        active = e->line_base;
    else if (p->mains_detected == 2)
        active = e->line_60;
    else
        active = e->line_50;
    if (p->high_fps_handling_en == 1 && e->frame_rate_max > 58 &&
        (e->idx_max - e->set_last.index) < 50)
        active = e->line_base;
    e->line_active = active;

    e->set_last = e->set_cur;
    index = CLAMP(index, 0, AE_IDX_MAX - 1);
    e->set_cur = active[index];
    s = &e->set_cur;

    j = e->cur_delta / 3 + index;
    if (j >= e->idx_max)
        j = e->idx_max;
    if (j < 0) {
        /*
         * The probe index is below the table.  The deployed object reads the
         * entry before the array; in its layout that location is zero, and the
         * observed boundary output is 0.  Model the out-of-range read as zero
         * rather than substituting table row 0.
         */
        e->lv_adj = 0;
    } else {
        if (j >= AE_IDX_MAX)
            j = AE_IDX_MAX - 1;
        e->lv_adj = active[j].lv;
    }

    /* 14.2 shutter priority */
    if (p->exposure_mode == 2) {
        int64_t fe = (int64_t)s->fno2 * s->ev;
        int64_t et = fe / 2560000;
        int64_t tg, dg, ag;
        if (et < active[0].exposure_time)
            et = active[0].exposure_time;
        if (et > p->fixed_exposure)
            et = p->fixed_exposure;
        if (et < 0)
            et = 0;
        s->exposure_time = (int32_t)et;
        s->sensor_exp_line = ae_time_to_line(e, et, 1);
        s->exposure_time = ae_line_to_time(e, s->sensor_exp_line);
        tg = s->exposure_time ? (fe / 10000) / s->exposure_time : 0;
        tg = CLAMP(tg, e->total_lo, e->total_hi);
        s->total_gain = (int32_t)tg;
        dg = isqrt_i64((int64_t)((double)(tg << 10) * p->gain_split_ratio));
        dg = CLAMP(dg, e->digital_lo, e->digital_hi);
        s->digital_gain = (int32_t)dg;
        ag = ((tg << 10) / (dg ? dg : 1)) & ~0xfLL;
        ag = CLAMP(ag, e->analog_lo, e->analog_hi);
        s->analog_gain = (int32_t)ag;
        if (s->exposure_time && ag) {
            int64_t d2 = (((int64_t)s->fno2 * (s->ev / s->exposure_time)) * 256) /
                         10000 / ag;
            s->digital_gain = (int32_t)CLAMP(d2, e->digital_lo, e->digital_hi);
        }
    }
    /* 14.3 aperture priority */
    else if (p->exposure_mode == 3) {
        int64_t fe, tmp, expo, tg, dg, ag;
        s->f_number = p->aperture;
        s->fno2 = nearest_aperture(e, s);
        fe = (int64_t)s->ev * s->fno2;
        tmp = fe / 10000;
        expo = tmp / (s->analog_gain ? s->analog_gain : 1);
        s->sensor_exp_line = ae_time_to_line(e, expo, 1);
        s->exposure_time = ae_line_to_time(e, s->sensor_exp_line);
        tg = s->exposure_time ? tmp / s->exposure_time : 0;
        tg = CLAMP(tg, e->total_lo, e->total_hi);
        s->total_gain = (int32_t)tg;
        dg = isqrt_i64((int64_t)((double)(tg << 10) * p->gain_split_ratio));
        dg = CLAMP(dg, e->digital_lo, e->digital_hi);
        s->digital_gain = (int32_t)dg;
        ag = ((tg << 10) / (dg ? dg : 1)) & ~0xfLL;
        ag = CLAMP(ag, e->analog_lo, e->analog_hi);
        s->analog_gain = (int32_t)ag;
        if (s->exposure_time && ag) {
            int64_t d2 = (((int64_t)s->fno2 * (s->ev / s->exposure_time)) * 1024) /
                         10000 / ag;
            s->digital_gain = (int32_t)CLAMP(d2, e->digital_lo, e->digital_hi);
        }
    }

    /* 14.4 manual ISO */
    if (p->exposure_mode != 1 && p->iso_control == 0) {
        int32_t iso = p->iso_sensitivity;
        int64_t v, ag, denom;
        if (iso < 100) {
            if (p->sensor_gain == 0) {
                p->iso_sensitivity = 100;
                p->sensor_gain = 16;
            }
        } else {
            v = (int64_t)p->iso_to_gain_ratio * iso / 100;
            if (v < 16)
                v = 16;
            p->sensor_gain = (v < p->gain_max) ? (int32_t)v : p->gain_max;
        }
        v = (int64_t)s->ev * 256 / (active[0].ev ? active[0].ev : 1);
        if (v < 256)
            v = 256;
        ag = (int64_t)((uint32_t)p->sensor_gain << 4);
        if (v < ag)
            ag = v;
        denom = ((int64_t)s->fno2 * s->ev / 10000) / (ag ? ag : 1);
        if (denom < active[0].exposure_time)
            denom = active[0].exposure_time;
        if (e->idx_max >= 1 && denom > active[e->idx_max - 1].exposure_time)
            denom = active[e->idx_max - 1].exposure_time;
        s->exposure_time = (int32_t)denom;
        s->sensor_exp_line = ae_time_to_line(e, denom, 1);
        s->digital_gain = 1024;
        s->analog_gain = (int32_t)ag;
    }

    /* 14.5 WDR exposure alignment and delay arming */
    {
        int32_t delay = MAX(p->exp_delay_frames, p->gain_delay_frames);
        int32_t arm = MAX(delay, 2);
        if (p->hdr_mode == 1) {
            int32_t w = p->wdr_cfg[0] * 16;
            int32_t old_exp = s->exposure_time;
            int64_t v;
            if (w == 0)
                w = 16;
            v = (int64_t)w * (s->sensor_exp_line / w);
            if (v == 0)
                v = w;
            s->sensor_exp_line = (int32_t)v;
            s->exposure_time = ae_line_to_time(e, v);
            if (s->exposure_time == 0)
                s->digital_gain = 1024;
            else
                s->digital_gain = (s->digital_gain * old_exp) / s->exposure_time;
            if (s->digital_gain < 1024)
                s->digital_gain = 1024;
        }
        if (e->set_last.sensor_exp_line != s->sensor_exp_line ||
            e->set_last.total_gain != s->total_gain ||
            e->wdr_ratio.last != e->wdr_ratio.tmp)
            e->latency_count = arm;
    }
}

/* Section 14.6 manual exposure configuration. */
static void ae_configure_manual(ae_entity_t *e)
{
    ae_params_t *p = &e->p;
    ae_setting_t *s;

    e->set_last = e->set_cur;
    s = &e->set_cur;

    s->total_gain = p->test_gain;
    if (p->gain_split_ratio < 0.1) {
        int64_t ag = ((int64_t)s->fno2 * s->ev / 160000) /
                     (s->exposure_time ? s->exposure_time : 1);
        s->analog_gain = (int32_t)(ag << 4);
    } else {
        int64_t dg = isqrt_i64((int64_t)((double)((int64_t)s->total_gain << 10) *
                                          p->gain_split_ratio));
        dg = CLAMP(dg, e->digital_lo, e->digital_hi);
        s->digital_gain = (int32_t)dg;
        s->analog_gain = (int32_t)(((int64_t)s->total_gain << 10) /
                                   (dg ? dg : 1) & ~0xfLL);
    }
    s->analog_gain = (int32_t)CLAMP((int64_t)((uint32_t)p->sensor_gain << 4),
                                    256, 1536000);
    s->sensor_exp_line = ae_time_to_line(e, p->fixed_exposure, 1);
    s->exposure_time = p->fixed_exposure;
    s->f_number = p->aperture;
    s->fno2 = nearest_aperture(e, s);
    s->digital_gain = 1024;

    if (p->hdr_mode == 1) {
        int32_t w = p->wdr_cfg[0] * 16;
        int64_t v;
        if (w == 0)
            w = 16;
        v = (int64_t)w * (s->sensor_exp_line / w);
        s->sensor_exp_line = (v == 0) ? w : (int32_t)v;
        s->exposure_time = ae_line_to_time(e, s->sensor_exp_line);
        v = s->exposure_time ? ((int64_t)s->total_gain << 10) / s->exposure_time
                             : 1024;
        if (v < 1024)
            v = 1024;
        s->digital_gain = (int32_t)MIN(v, 0x4000);
    }

    if (e->set_last.sensor_exp_line == s->sensor_exp_line)
        e->latency_count = (e->set_last.total_gain == s->total_gain)
                           ? 2 : p->gain_delay_frames;
    else
        e->latency_count = p->exp_delay_frames;
}

/* ------------------------------------------------------------------ */
/* Section 12: flash tracking                                         */
/* ------------------------------------------------------------------ */

static void ae_flash_update(ae_entity_t *e, ae_result_t *r, int32_t delta)
{
    ae_params_t *p = &e->p;
    int active_flash;

    if (p->exposure_mode == 1) {
        r->flash_ev_cumul = e->flash_ev_accum;
        return;
    }
    active_flash = (p->flash_open != 0) &&
                   (p->flash_mode == 1 || p->flash_mode == 3);
    if (!active_flash) {
        e->flash_ev_accum = 0;
        r->flash_ev_cumul = 0;
        return;
    }
    if (p->capture_stage == 1)
        e->flash_before_lum = r->avg_lum;
    if (p->capture_stage == 3)
        e->flash_after_lum = r->avg_lum;
    e->flash_ev_accum += delta;
    r->flash_ev_cumul = e->flash_ev_accum;
}

int ae_flash_strength(const ae_entity_t *e, int32_t flash_expect_lum)
{
    int32_t d, ratio;
    if (!e)
        return 0;
    d = e->flash_after_lum - e->flash_before_lum;
    if (d < 0) {
        ratio = 10000;
    } else if (d == 0) {
        ratio = 0;
    } else {
        ratio = (int32_t)(((int64_t)(flash_expect_lum - e->flash_before_lum) * 100) / d);
    }
    return ratio;
}

/* ------------------------------------------------------------------ */
/* Section 10: error signal, tolerance and convergence                */
/* ------------------------------------------------------------------ */

static void compute_exposure_from_table(ae_entity_t *e, const ae_stats_t *st,
                                        ae_result_t *r)
{
    ae_params_t *p = &e->p;
    const ae_clean_tables_t *t = TAB(e);
    int32_t avg_lum_q8 = 0;
    int32_t weighted;
    int32_t target, orig_target;
    int32_t delta, raw_delta, tol, row, k, step;
    int32_t metering = p->metering_mode;

    weighted = ae_meter(e, st, r, &avg_lum_q8);
    /*
     * Section 7.4 consumes the Q8 luminance returned by section 7.1 (the
     * spatial weighted value); result.avg_lum keeps the unweighted mean.
     */
    (void)avg_lum_q8;

    /* Section 11: target and compensation */
    target = p->exposure_cfg[9];
    if (p->ev_bias >= -4 && p->ev_bias <= 4 && p->ev_bias != 0) {
        int32_t sstep = p->ev_comp_step;
        if (sstep == 0)
            sstep = 4;
        p->ev_comp_step = sstep;
        if (p->ev_bias > 0)
            target = (int32_t)((int64_t)t->evtab[p->ev_bias * sstep] * target / 1000);
        else
            target = (int32_t)((int64_t)target * 1000 /
                               (t->evtab[-p->ev_bias * sstep] ?
                                t->evtab[-p->ev_bias * sstep] : 1));
    }
    if (p->test_forced == 1)
        target = p->lum_forced;
    if (target == 0)
        target = 128;
    orig_target = target;
    if (e->target_comp > 0)
        target -= e->target_comp;
    target = CLAMP(target, 0, orig_target);
    r->target = target;

    /* Section 7.4: scene luminance used for the error */
    if (p->hdr_mode == 1) {
        ae_update_hdr_ratio(e, st, r);
        r->weight_lum = SAR(weighted + (weighted < 0 ? 255 : 0), 8);
    } else if (p->hist_metering_en != 0 && metering != 2) {
        int64_t hh = ae_hist_lum(e, st);
        int64_t bl = ae_backlight_centroid(e, st, r);
        int64_t tt = hh + bl + 2 * (int64_t)weighted;
        if (tt < 0)
            tt += 1023;
        r->weight_lum = (int32_t)(tt >> 10);
    } else {
        r->weight_lum = SAR(weighted + (weighted < 0 ? 255 : 0), 8);
    }

    /* Section 10.1: log2-domain error */
    {
        int32_t L = CLAMP(r->weight_lum, 1, 255);
        int32_t T = CLAMP(target, 1, 255);
        delta = (20 - t->log2[L] + t->log2[T]) / 40;
    }
    raw_delta = delta;

    /* Section 10.2: tolerance and hysteresis */
    tol = p->exposure_cfg[8];
    if (tol == 0)
        tol = 3;
    if (e->frame_index < 80)
        tol = 2 * tol - (tol * e->frame_index) / 80;
    if ((delta < 0 ? -delta : delta) < tol) {
        delta = 0;
        e->lum_change_cnt = 0;
    } else {
        e->lum_change_cnt++;
        if (e->lum_change_cnt < p->error_delay_frames)
            delta = 0;
    }
    e->cur_delta = raw_delta;
    r->delta_idx = raw_delta;

    /* Flash bookkeeping accumulates the raw error (before tolerance), as the
     * deployed code does. */
    ae_flash_update(e, r, raw_delta);

    /* Section 10.3: convergence curve row and step */
    if (metering == 2)
        row = p->exposure_cfg[7];
    else if (p->scene == 1)
        row = p->exposure_cfg[5];
    else if (p->scene == 2)
        row = p->exposure_cfg[6];
    else
        row = p->exposure_cfg[4];
    row = CLAMP(row, 0, 31);
    if (e->frame_index < 80)
        row += (e->frame_index * 8) / 80 - 8;
    if (row < 0)
        row = 0; /* spec 18.1: intended row-0 behaviour */
    k = (delta < 0 ? -delta : delta);
    k = CLAMP(k, 0, 127);
    step = (int32_t)t->conv[row * 128 + k];
    if (delta < 0)
        step = -step;

    /* Section 10.4: applied index and reported ratio */
    {
        int32_t ev_idx = e->set_cur.index;
        int32_t maxidx = e->idx_max - 1;
        int32_t idx_expect, new_idx;
        if (maxidx < 0)
            maxidx = 0;
        idx_expect = ev_idx + delta;
        if ((uint32_t)idx_expect > (uint32_t)maxidx)
            idx_expect = maxidx;
        new_idx = CLAMP(ev_idx + step, 0, maxidx);
        ae_configure_sensor(e, new_idx);
        r->idx_expect = idx_expect;
        if (new_idx == 0 || new_idx == maxidx)
            r->gain_ratio_out = 256;
        else if (step < 0)
            r->gain_ratio_out = (int32_t)(256000 /
                (int64_t)(t->evtab[-step] ? t->evtab[-step] : 1));
        else
            r->gain_ratio_out = (int32_t)(((int64_t)t->evtab[step] << 8) / 1000);
        r->gain_ratio_out = CLAMP(r->gain_ratio_out, 64, 640);
    }
}

/* ------------------------------------------------------------------ */
/* Section 6.2: run mode (recompute gate)                             */
/* ------------------------------------------------------------------ */

static void ae_run_mode(ae_entity_t *e, const ae_stats_t *st, ae_result_t *r)
{
    ae_params_t *p = &e->p;
    int32_t ft, n, k;

    if (e->latency_count >= 0)
        return;

    ft = p->frame_time;
    if (ft == 0) {
        n = 1;
    } else {
        n = (ft / 2 + 15) / ft;
        if (n < 1)
            n = 1;
        if (n > 2)
            n = 3;
    }
    if (n <= p->error_delay_frames)
        n = p->error_delay_frames;
    e->interval_frame = n;

    k = e->frame_index - 3;
    if (e->interval_frame != 0 &&
        (k == e->interval_frame * (k / e->interval_frame)) &&
        p->exposure_locked == 0) {
        if (p->exposure_mode == 1) {
            ae_configure_manual(e);
            /*
             * Manual exposure with automatic ISO: the gain is not under
             * manual control, so the configured manual gain is overridden
             * (the deployed code warns and forces the base analog gain).
             */
            if (p->iso_control != 0)
                e->set_cur.analog_gain = 256;
        } else {
            compute_exposure_from_table(e, st, r);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Section 6.3 / 9: delay gating and WDR post-processing              */
/* ------------------------------------------------------------------ */

static void ae_finish_result(ae_entity_t *e, ae_result_t *r)
{
    ae_params_t *p = &e->p;
    ae_setting_t *s = &e->set_cur;
    ae_setting_t *c = &e->set_curr;

    if (p->hdr_mode == 1) {
        if (e->latency_count == 2 && p->delay_en == 0)
            e->wdr_ratio.hw_ratio = e->wdr_ratio.tmp;
        if (e->frame_index < 11) {
            int32_t v = p->wdr_cfg[0] << 4;
            e->wdr_ratio.tmp = e->wdr_ratio.hw_ratio = e->wdr_ratio.sensor = v;
        }
        r->wdr_hi_th = p->wdr_cfg[2] << 8;
        r->wdr_low_th = p->wdr_cfg[1] << 8;
        e->set_short = *s;
        {
            int32_t div = e->wdr_ratio.sensor >> 4;
            if (div < 1)
                div = 1;
            e->set_short.exposure_time /= div;
            e->set_short.sensor_exp_line /= div;
        }
    } else {
        e->set_short = *s;
    }

    if (p->delay_en != 0) {
        c->sensor_exp_line = s->sensor_exp_line;
        c->exposure_time = s->exposure_time;
        c->analog_gain = s->analog_gain;
    }
    if (e->latency_count == 2)
        c->digital_gain = s->digital_gain;
    if (e->latency_count == p->gain_delay_frames)
        c->analog_gain = s->analog_gain;
    if (e->latency_count == p->exp_delay_frames) {
        c->sensor_exp_line = s->sensor_exp_line;
        c->exposure_time = s->exposure_time;
        e->wdr_ratio.sensor = e->wdr_ratio.tmp;
    }
    c->total_gain = s->total_gain;
    c->index = s->index;
    c->lv = s->lv;
    c->ev_idx = s->ev_idx;

    /* The short companion is committed under the same delay gates, sourced
     * from the short setting (section 6.3). */
    {
        ae_setting_t *ss = &e->set_short;
        ae_setting_t *sc = &e->set_short_curr;
        if (p->delay_en != 0) {
            sc->sensor_exp_line = ss->sensor_exp_line;
            sc->exposure_time = ss->exposure_time;
            sc->analog_gain = ss->analog_gain;
        }
        if (e->latency_count == 2)
            sc->digital_gain = ss->digital_gain;
        if (e->latency_count == p->gain_delay_frames)
            sc->analog_gain = ss->analog_gain;
        if (e->latency_count == p->exp_delay_frames) {
            sc->sensor_exp_line = ss->sensor_exp_line;
            sc->exposure_time = ss->exposure_time;
        }
    }

    e->latency_count--;
}

/* ------------------------------------------------------------------ */
/* Section 6.4: diagnostic sweep (placeholder)                        */
/* ------------------------------------------------------------------ */

static void ae_apply_test_ramp(ae_entity_t *e)
{
    ae_setting_t *s = &e->set_cur;

    /*
     * Open-loop test emission: fixed gain/line values, a digital gain of 1024
     * and a synthetic index of 10; the remaining fields (exposure time,
     * aperture, LV, EV) keep their previous values, as the deployed code does,
     * and the whole setting is mirrored into the last/current slots.
     */
    s->total_gain = e->p.test_gain;
    s->analog_gain = e->p.test_gain;
    s->sensor_exp_line = e->p.test_exp_line;
    s->digital_gain = 1024;
    s->ev_idx = 10;
    e->set_last = *s;
    e->set_curr = *s;
}

static void ae_run_delay_sweep(ae_entity_t *e, const ae_stats_t *st)
{
    /* TODO(spec 6.4): record the luma ring and step the swept quantity. */
    (void)e;
    (void)st;
}

/* ------------------------------------------------------------------ */
/* Section 6.1: entry dispatch (interrupt handler)                    */
/* ------------------------------------------------------------------ */

int ae_isr(ae_entity_t *e, const ae_stats_t *st, ae_result_t *r)
{
    if (!e || !st || !r) {
        if (r)
            ae_default_result(r);
        return -1;
    }

    if (e->p.test_enable == 0) {
        ae_apply_test_ramp(e);
        r->status = 0;
    } else if (e->frame_index < 3) {
        ae_configure_sensor(e, e->set_curr.index);
        r->backlight = 0x20;
        r->status = 0;
    } else {
        ae_run_mode(e, st, r);
        r->status = (e->set_last.index == e->set_cur.index) ? 2 : 1;
        if (e->p.delay_en != 0) {
            ae_apply_test_ramp(e);
            r->status = 0;
            ae_run_delay_sweep(e, st);
        }
    }

    ae_finish_result(e, r);
    sync_result(e, r);
    e->frame_index++;
    return 0;
}

int ae_run(ae_entity_t *e, const ae_stats_t *st, ae_result_t *r)
{
    if (!e || !st || !r) {
        if (r)
            ae_default_result(r);
        return -1;
    }
    return ae_isr(e, st, r);
}

/* ------------------------------------------------------------------ */
/* Section 6.0: initialisation stage                                  */
/* ------------------------------------------------------------------ */

static int ae_desc_valid(const ae_params_t *p, const ae_desc_t *d)
{
    uint32_t i, len = d->length;
    if (len > AE_NSEG)
        return 0;
    for (i = 0; i < len; i++) {
        uint32_t mx;
        if (d->seg[i].min_exp == 0 || d->seg[i].min_gain == 0 ||
            d->seg[i].max_gain == 0)
            return 0;
        mx = MAX(d->seg[i].min_gain, d->seg[i].max_gain);
        if ((uint64_t)mx > ((uint64_t)(uint32_t)p->gain_max << 8))
            return 0;
    }
    return 1;
}

static int ae_init_stage(ae_entity_t *e)
{
    ae_params_t *p = &e->p;
    const ae_clean_tables_t *t = TAB(e);
    int32_t fr = 0;
    int i;

    /* 1. frame-rate estimate */
    if (p->hts > 0 && p->vts > 0) {
        int64_t denom = (int64_t)p->hts * p->vts;
        fr = (int32_t)(((int64_t)p->pclk * 16 / denom + 8) >> 4);
    }
    e->frame_rate_max = fr;

    /* Runtime target-compensation offset (section 11). */
    e->target_comp = p->target_comp;

    /* 2. select scene descriptors */
    if (p->table_source == 1) {
        e->tbl_capture = ae_desc_valid(p, &p->scene_tables[1])
                             ? p->scene_tables[1] : *t->table_default;
        e->tbl_preview = ae_desc_valid(p, &p->scene_tables[0])
                             ? p->scene_tables[0] : *t->table_default;
        e->tbl_video = ae_desc_valid(p, &p->scene_tables[2])
                           ? p->scene_tables[2] : *t->table_default;
    } else if (p->table_source == 2) {
        ae_desc_t c = p->scene_tables[15];
        if (c.length != 0) {
            e->tbl_preview = e->tbl_capture = e->tbl_video = c;
        } else {
            e->tbl_preview = e->tbl_capture = e->tbl_video = *t->table_default;
        }
    } else {
        e->tbl_preview = e->tbl_capture = e->tbl_video = *t->table_default;
        e->tbl_preview = e->tbl_capture;
    }

    /* 3. optional replacement of the matrix metering grid */
    {
        int32_t sum = 0;
        for (i = 0; i < AE_NGRID; i++)
            sum += p->window_weight_seed[i];
        if (sum != 0)
            memcpy(e->wgrid_matrix, p->window_weight_seed, sizeof(e->wgrid_matrix));
    }

    /* 4. seed scene slots 0/1/2 when unset */
    if (p->scene_tables[0].seg[0].max_exp == 0) {
        p->scene_tables[0] = e->tbl_preview;
        p->scene_tables[1] = e->tbl_capture;
        p->scene_tables[2] = e->tbl_video;
    }

    /* 5. default aperture ladder */
    {
        const int32_t *def = t->fno_def ? t->fno_def : t->fno_ladder;

        if (p->aperture_ladder[0] == 0 && def) {
            for (i = 0; i < 16; i++)
                p->aperture_ladder[i] = def[i];
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Lifecycle and entry points                                         */
/* ------------------------------------------------------------------ */

int ae_get_params(ae_entity_t *e, ae_params_t *out)
{
    if (!e || !out)
        return -1;
    *out = e->p;
    return 0;
}

int ae_set_params(ae_entity_t *e, const ae_param_req_t *req, ae_result_t *r)
{
    (void)r;
    if (!e || !req)
        return -1;
    switch (req->kind) {
    case AE_PARAM_INIT:
        if (!req->params)
            return -1;
        e->p = *req->params;
        return ae_init_stage(e);
    case AE_PARAM_TABLES:
        ae_set_line_table(e);
        return 0;
    case AE_PARAM_SET_INDEX:
        /*
         * Section 14.7: the frame path snapshots setting_last before loading
         * the new entry; this direct path is documented as not doing that
         * extra snapshot.  The distinction is not reproduced here yet; the
         * common configure path (which does snapshot) is used.
         */
        e->cur_delta = 0;
        ae_configure_sensor(e, req->line_index);
        return 0;
    case AE_PARAM_TOUCH:
        ae_build_touch_grid(e);
        return 0;
    default:
        return -1;
    }
}

ae_entity_t *ae_init(ae_ops_t *out_ops)
{
    const freeisp_tables_t *ft = freeisp_get_tables();
    ae_entity_t *e;

    if (!ft || !ft->ae)
        return NULL;
    e = (ae_entity_t *)calloc(1, sizeof(*e));
    if (!e)
        return NULL;

    e->tab = ft->ae;
    e->busy_flag = 1;
    e->bin_weight[0] = e->bin_weight[1] = e->bin_weight[2] = 8;
    e->latency_count = 0;

    if (ft->ae->wght_matrix)
        memcpy(e->wgrid_matrix, ft->ae->wght_matrix, sizeof(e->wgrid_matrix));
    if (ft->ae->wght_avg)
        memcpy(e->wgrid_avg, ft->ae->wght_avg, sizeof(e->wgrid_avg));
    if (ft->ae->wght_center)
        memcpy(e->wgrid_center, ft->ae->wght_center, sizeof(e->wgrid_center));

    if (out_ops) {
        out_ops->get_params = ae_get_params;
        out_ops->set_params = ae_set_params;
        out_ops->run = ae_run;
        out_ops->isr = ae_isr;
    }
    return e;
}

void ae_exit(ae_entity_t *e)
{
    free(e);
}

void ae_set_frame_index(ae_entity_t *e, int32_t index)
{
    if (e)
        e->frame_index = index;
}

void ae_apply_wdr_feedback(ae_entity_t *e, int32_t sensor, int32_t hw_ratio,
                           int32_t tmp, int32_t last)
{
    ae_wdr_ratio_t *ratio;

    if (e == NULL)
        return;

    /*
     * The deployed core shares its result block with the framework, so the
     * WDR configuration step's between-run rewrite of these four slots lands
     * in the core's own state.  The clean core owns a private block, so the
     * shim replays that rewrite here before the next run.  Keeping the four
     * fields in the core's blend-ratio record is what lets the exposure path's
     * delay arming (last != tmp) and short/long alignment see the framework's
     * values.
     */
    ratio = &e->wdr_ratio;
    ratio->sensor   = sensor;
    ratio->hw_ratio = hw_ratio;
    ratio->tmp      = tmp;
    ratio->last     = last;
}
