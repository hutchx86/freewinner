/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * gtm_clean.c - clean-room global tone-mapping module.
 *
 * Implements the behaviour specified in spec/gtm.md.  Every tuning
 * table is injected at runtime through freeisp_get_tables(); this file
 * contains no table data.  Sections below mirror the spec's numbered stages.
 */
#include "gtm_clean.h"
#include "freeisp/sdiv.h"

#include <math.h>
#include <stdlib.h>

#define IMIN(a, b)       ((a) < (b) ? (a) : (b))
#define ICLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

/* ------------------------------------------------------------------ */
/* Internal state (spec 5).                                            */
/* ------------------------------------------------------------------ */

struct gtm_entity {
    gtm_clean_params_t       p;              /* parameter block          */
    const gtm_clean_tables_t *tab;

    int32_t  alpha_ramp[GTM_NCURVE];
    int32_t  alpha_ramp_auto[GTM_NCURVE];
    int32_t  weight_hist[GTM_NCURVE];
    int32_t  weight_cum[GTM_NCURVE];
    int32_t  weight_gamma[GTM_NCURVE];
    int32_t  weight_balance[GTM_NCURVE];
    int32_t  norm_hist[GTM_NCURVE];
    int32_t  adj_hist[GTM_NCURVE];
    int32_t  cum_hist[GTM_NCURVE];
    int32_t  tone_map[GTM_NCURVE];
    int32_t  merge_curve[GTM_NCURVE];
    int32_t  blend_curve[GTM_NCURVE];
    int32_t  blend_ramp[321];
    int32_t  eq_conv[286];
    int32_t  scratch[GTM_NCURVE];

    uint32_t hist_total;
    uint16_t var_hold;
    uint16_t peak_level;
    uint16_t div_index;
    double   ratio_hold;
    int16_t  guide_state;
    int16_t  guide_streak;
    int32_t  brightness_saved;
    int32_t  contrast_saved;
    int32_t  busy_flag;
};

/* ------------------------------------------------------------------ */
/* Small helpers.                                                      */
/* ------------------------------------------------------------------ */

static int32_t interp(int32_t x, int32_t x0, int32_t x1, int32_t y0, int32_t y1)
{
    if (x1 == x0)
        return y0;
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
}

static int64_t gtm_isqrt(int64_t v)
{
    int64_t x;
    if (v <= 0)
        return 0;
    x = (int64_t)sqrt((double)v);
    while ((x + 1) * (x + 1) <= v)
        x++;
    while (x * x > v)
        x--;
    return x;
}

static int32_t gtm_window_stddev(const uint32_t *w, int n)
{
    int64_t sum = 0, sq = 0;
    int i;
    for (i = 0; i < n; i++) {
        sum += w[i];
        sq += (int64_t)w[i] * w[i];
    }
    {
        int64_t mean = sum / n;
        int64_t msq = sq / n;
        int64_t var = msq - mean * mean;
        if (var < 0)
            var = 0;
        return (int32_t)gtm_isqrt(var);
    }
}

static const int16_t *gtm_reclassify(gtm_entity_t *e,
                                     const gtm_clean_tables_t *t, double gry)
{
    e->guide_streak = 0;
    if (gry > 0.0) {
        e->guide_state = 1;
        return t->guide_high;
    }
    if (gry < 0.0) {
        e->guide_state = -1;
        return t->guide_low;
    }
    e->guide_state = 0;
    return t->guide_linear;
}

static int32_t gtm_inv_map(const gtm_clean_params_t *p, int32_t v)
{
    int j;
    if (v == 0)
        return 0;
    for (j = 0; j <= 1022; j++) {
        int32_t lo = (int32_t)p->gamma_lut[j];
        int32_t hi = (int32_t)p->gamma_lut[j + 1];
        if (lo <= v && v <= hi)
            return freeisp_sdiv((v - lo) * 4, hi - lo) + j * 4;
    }
    return 0xFFF;
}

/* ------------------------------------------------------------------ */
/* Section 7: average luminance / variance statistic.                  */
/* ------------------------------------------------------------------ */

static void gtm_statistic(gtm_entity_t *e, const int32_t *h,
                          gtm_clean_result_t *res)
{
    int32_t m[GTM_NCURVE];
    int32_t c[GTM_NCURVE];
    int32_t run_m = 0, run_c = 0;
    int32_t lum_l = 0, lum_h = 0;
    uint32_t best = 0, sum_k = 0, cnt = 0;
    int32_t otsu, avg;
    int i, k;

    for (i = 0; i < GTM_NCURVE; i++) {
        run_m += i * h[i];
        run_c += h[i];
        m[i] = run_m;
        c[i] = run_c;
    }
    avg = m[GTM_NCURVE - 1] >> 12;
    res->avg_lum = (uint16_t)avg;
    for (i = 0; i < GTM_NCURVE; i++)
        e->cum_hist[i] = c[i];

    /* Otsu-style threshold scan over odd bins.  The class weights and the
     * between-class measure are unsigned 32-bit, as in the object: a
     * normalised histogram can sum slightly above 4096, in which case the
     * upper weight wraps instead of going negative. */
    for (k = 1; k <= 253; k += 2) {
        uint32_t cw = (uint32_t)c[k];
        uint32_t uw = (uint32_t)GTM_Q12 - cw;
        int32_t d1, d2;
        uint32_t between;
        if (cw != 0)
            lum_l = m[k] / (int32_t)cw;
        if (cw != (uint32_t)GTM_Q12)
            lum_h = (int32_t)((uint32_t)(m[GTM_NCURVE - 1] - m[k]) / uw);
        d1 = lum_l - avg;
        if (d1 < 0)
            d1 = -d1;
        d2 = lum_h - avg;
        if (d2 < 0)
            d2 = -d2;
        between = cw * (uint32_t)d1 * (uint32_t)d1 +
                  uw * (uint32_t)d2 * (uint32_t)d2;
        if (between > best) {
            best = between;
            sum_k = (uint32_t)k;
            cnt = 1;
        } else if (between == best) {
            sum_k += (uint32_t)k;
            cnt += 1;
        }
    }
    otsu = (int32_t)freeisp_udiv(sum_k, cnt);

    {
        uint32_t cw = (uint32_t)c[otsu];
        uint32_t uw = (uint32_t)GTM_Q12 - cw;
        uint32_t sum_low = 0, sum_high = 0;
        uint32_t var_low, var_high, raw;
        if (cw != 0)
            lum_l = m[otsu] / (int32_t)cw;
        if (uw != 0)
            lum_h = (int32_t)((uint32_t)(m[GTM_NCURVE - 1] - m[otsu]) / uw);
        for (i = 0; i <= otsu; i++) {
            int32_t d = i - lum_l;
            if (d < 0)
                d = -d;
            sum_low += (uint32_t)h[i] * (uint32_t)d;
        }
        for (i = otsu + 1; i < GTM_NCURVE; i++) {
            int32_t d = i - lum_h;
            if (d < 0)
                d = -d;
            sum_high += (uint32_t)h[i] * (uint32_t)d;
        }
        var_low = (cw != 0) ? (sum_low / cw) : sum_low;
        var_high = (uw != 0) ? (sum_high / uw) : sum_high;
        raw = ((cw * var_low + uw * var_high) >> 12) * 5u;
        if (raw > 255)
            raw = 255;
        res->avg_var = (uint16_t)raw;
        if (raw < 200)
            e->var_hold = (uint16_t)raw;
        else
            res->avg_var = e->var_hold;
    }
}

/* ------------------------------------------------------------------ */
/* Section 8: dynamic-range path (histogram equalisation).             */
/* ------------------------------------------------------------------ */

static int gtm_eq_path(gtm_entity_t *e, const gtm_clean_stats_t *stats)
{
    const gtm_clean_tables_t *t = e->tab;
    gtm_clean_params_t *p = &e->p;
    int32_t range_max = p->range_max;
    int32_t eq_gain = p->eq_gain;
    int32_t cover_bias = p->cover_bias;
    int32_t sd, strength, cover, gidx;
    int32_t inc, dec, bias;
    int64_t usum, cum, top, fix_sum = 0;
    int32_t f[254];
    int32_t hgamma[GTM_NCURVE];
    int32_t run = 0;
    int i, j, ti;

    /* 8.1 coefficient validation. */
    if ((uint32_t)(range_max - 0x200) > 0xE00u ||
        (uint32_t)eq_gain > 100u ||
        (uint32_t)(cover_bias + 10) > 0x14u) {
        range_max = 0x500;
        eq_gain = 0x28;
        cover_bias = 10;
    }

    /* 8.2 derived coefficients. */
    sd = gtm_window_stddev(stats->win_avg, GTM_NWIN);
    strength = ICLAMP((int32_t)((int64_t)eq_gain * sd / 50), 1, 100);
    cover = ICLAMP(sd / 5 + cover_bias, 0, 19);
    gidx = ICLAMP(p->pre_gamma_offset + 4, 0, 10);

    /* 8.3 static alpha ramp. */
    inc = p->white_slope * p->black_level * 32;
    dec = -p->black_slope * p->white_level * 256;
    for (i = 0; i < GTM_NCURVE; i++) {
        if (i < p->black_level)
            e->alpha_ramp[i] = freeisp_sdiv(inc, p->black_level);
        else if (i > p->white_level)
            e->alpha_ramp[i] = freeisp_sdiv(dec, 256 - p->white_level);
        else
            e->alpha_ramp[i] = 0;
        inc -= p->white_slope * 32;
        dec += p->black_slope * 256;
    }

    /* 8.4 weighting histogram and cumulative weight. */
    e->alpha_ramp_auto[0] = 0;
    for (i = 0; i < GTM_NCURVE; i++)
        e->weight_hist[i] = freeisp_sdiv(
            0x271000, e->alpha_ramp[i] + 0x100 + e->alpha_ramp_auto[i]);
    usum = 0;
    for (i = 0; i < GTM_NCURVE; i++)
        usum += e->weight_hist[i];
    cum = 0;
    for (i = 0; i < GTM_NCURVE; i++) {
        int64_t g;
        cum += e->weight_hist[i];
        e->weight_cum[i] = (int32_t)cum;
        g = (cum * 0x20000) / usum;
        g -= 0x200;
        if (g < 0)
            g = 0;
        if (g > 0x20000)
            g = 0x20000;
        e->weight_gamma[i] = (int32_t)g;
        if (i != 0)
            e->weight_balance[i] = (int32_t)((g + i / 2) / i);
    }
    e->weight_balance[0] = e->weight_balance[1];

    /* 8.5 weighted input histogram. */
    top = 0;
    for (i = 250; i < 256; i++)
        top += stats->hist_raw[i];
    bias = (int32_t)((top + 0x7F) / 0xFF);
    for (i = 0; i < GTM_NCURVE; i++)
        e->norm_hist[i] = (int32_t)stats->hist_raw[i] + bias;

    /* 8.6 equalisation convolution, central 254 taps. */
    for (ti = 0; ti <= 285; ti++) {
        int32_t acc = 0;
        int jlo = ti - 30;
        int jhi = ti;
        if (jlo < 0)
            jlo = 0;
        if (jhi > 255)
            jhi = 255;
        for (j = jlo; j <= jhi; j++)
            acc += (e->norm_hist[j] *
                    t->eq_kernel[cover * GTM_EQ_TAPS + (ti - j)]) >> 10;
        e->eq_conv[ti] = acc;
    }
    for (i = 0; i < 254; i++) {
        f[i] = e->eq_conv[i + 14];
        fix_sum += f[i];
    }
    if (fix_sum == 0)
        return 0;

    /* 8.7 integrated gamma profile. */
    for (i = 0; i < 254; i++) {
        int32_t v;
        run += f[i];
        v = (int32_t)(((int64_t)run * 0x1000 + fix_sum / 2) / fix_sum) - 0x10;
        hgamma[i] = ICLAMP(v, 0, GTM_CURVE_MAX);
    }
    hgamma[254] = hgamma[253];
    hgamma[255] = hgamma[253];

    /* 8.8 pre-gamma mapping. */
    for (i = 1; i < 256; i++) {
        uint16_t gt = p->gamma_lut[i];
        if (gt == 0) {
            e->tone_map[i] = 0x200;
        } else {
            int jj = ICLAMP(hgamma[i] >> 4, 0, 255);
            int32_t v = (int32_t)t->pre_gamma[gidx * GTM_PRE_COLS + jj] << 9;
            if (p->gamma_mode == GTM_GAMMA_FIXED) {
                v = v / i;
                if (v < 0)
                    v += 0xF;
                v >>= 4;
            } else {
                v = v / (int32_t)gt;
            }
            e->tone_map[i] = v;
        }
    }
    e->tone_map[0] = e->tone_map[1];
    e->tone_map[255] = e->tone_map[254];

    /* 8.9 strength scaling and peak normalisation. */
    for (i = 0; i < GTM_NCURVE; i++) {
        int32_t v = ((e->tone_map[i] - 0x200) * strength + 0x32) / 100 + 0x200;
        e->tone_map[i] = v;
        p->curve[i] = (uint16_t)v;
    }
    {
        int mi = 0;
        int32_t maxval;
        for (i = 1; i < GTM_NCURVE; i++)
            if (e->tone_map[i] > e->tone_map[mi])
                mi = i;
        maxval = e->tone_map[mi];
        if (range_max < maxval) {
            for (i = 0; i < GTM_NCURVE; i++)
                p->curve[i] = (uint16_t)(((e->tone_map[i] - 0x200) * range_max +
                                          maxval / 2) / maxval + 0x200);
        } else {
            for (i = 0; i < GTM_NCURVE; i++)
                p->curve[i] = (uint16_t)e->tone_map[i];
        }
    }

    /* 8.10 slope-balance weighting. */
    for (i = 0; i < GTM_NCURVE; i++) {
        int32_t v = (int32_t)p->curve[i] * e->weight_balance[i];
        int32_t r;
        if (v < 0)
            r = (v + 0x2FF) >> 9;
        else
            r = (v + 0x100) >> 9;
        p->curve[i] = (uint16_t)ICLAMP(r, 0, GTM_CURVE_MAX);
    }
    return 1;
}

/* Section 11.1: dynamic-range temporal prefilter. */
static void gtm_prefilter(gtm_entity_t *e)
{
    int i;
    for (i = 0; i < GTM_NCURVE; i++) {
        int32_t v = ((int32_t)e->p.curve_prev[i] * 15 +
                     (int32_t)e->p.curve[i] + 8) >> 4;
        e->p.curve[i] = (uint16_t)v;
        e->p.curve_prev[i] = (uint16_t)v;
    }
}

/* ------------------------------------------------------------------ */
/* Section 10: shared brightness/contrast S-curve blend.               */
/* ------------------------------------------------------------------ */

static void gtm_blend(gtm_entity_t *e)
{
    gtm_clean_params_t *p = &e->p;
    int32_t c = p->contrast;
    int32_t b = p->brightness;
    int32_t v;
    int32_t base;
    int i;

    if (c < 0) {
        c = c / 2;
        b = c + b;
    }

    v = (c + 85) * (-128) + b * 100 + 9800;
    for (i = 0; i <= 320; i++) {
        int32_t tv = v;
        if (v > 0x639B)
            tv = 0x639C;
        v += c + 85;
        if (tv < 1)
            tv = 1;
        e->blend_ramp[i] = tv;
    }

    for (i = 0; i < GTM_NCURVE; i++) {
        int64_t sum = 0;
        int k;
        for (k = 0; k < 64; k++)
            sum += e->blend_ramp[i + k];
        e->blend_curve[i] = (int32_t)(sum >> 6);
    }

    base = e->blend_curve[0];
    if (p->contrast < 0)
        base = base / 2;
    for (i = 1; i < GTM_NCURVE; i++)
        e->blend_curve[i] -= base;
    if (e->blend_curve[255] == 0)
        e->blend_curve[255] = 1;

    for (i = 0; i < GTM_NCURVE; i++) {
        int32_t tv = (e->blend_curve[i] << 12) / e->blend_curve[255];
        e->blend_curve[i] = IMIN(tv, 0x1000);
    }

    for (i = 0; i < GTM_NCURVE; i++) {
        if (i == 0 || e->blend_curve[i] == 0) {
            p->curve[i] = 0;
        } else {
            int32_t tv = (e->blend_curve[i] << 9) / i;
            uint16_t out;
            if (p->mode == GTM_MODE_FIXED) {
                if (tv < 0)
                    tv += 0xF;
                out = (uint16_t)(tv >> 4);
            } else {
                tv = (int32_t)((uint32_t)tv * (uint32_t)p->curve[i]);
                if (tv < 0)
                    tv += 0x1FFF;
                out = (uint16_t)(tv >> 13);
            }
            p->curve[i] = out;
            if (p->curve[i] >= 0x1000)
                p->curve[i] = 0xFFF;
        }
    }

    e->brightness_saved = p->brightness;
    e->contrast_saved = p->contrast;
}

/* ------------------------------------------------------------------ */
/* Section 9: luma-hold path.                                          */
/* ------------------------------------------------------------------ */

static void gtm_luma_hold(gtm_entity_t *e, const gtm_clean_stats_t *stats,
                          gtm_clean_result_t *res)
{
    const gtm_clean_tables_t *t = e->tab;
    gtm_clean_params_t *p = &e->p;
    const int16_t *guide_linear = t->guide_linear;
    const int16_t *guide;
    uint64_t hist_total = 0;
    double cc = 1.0 / 4096.0;
    double gry = 0.0, gud = 0.0, use, merge_ratio;
    int32_t m[GTM_NCURVE];
    int i, j;

    /* 9.1 normalised histogram. */
    for (i = 0; i < GTM_NCURVE; i++)
        hist_total += stats->hist_raw[i];
    e->hist_total = (uint32_t)hist_total;
    for (j = 0; j < GTM_NCURVE; j++)
        e->norm_hist[j] = 0;
    for (i = 0; i < GTM_NCURVE; i++) {
        int jj = (int)(p->gamma_lut[i * 4] >> 4);
        e->norm_hist[jj] += (int32_t)stats->hist_raw[i];
    }
    for (j = 0; j < GTM_NCURVE; j++) {
        if (hist_total != 0)
            e->norm_hist[j] = (int32_t)((double)e->norm_hist[j] * 4096.0 /
                                        (double)hist_total + 0.5);
        else
            e->norm_hist[j] = 0;
    }

    /* 9.2 statistic (fills cum_hist, avg_lum, avg_var). */
    gtm_statistic(e, e->norm_hist, res);

    /* 9.3 peak-level slew. */
    {
        int li = IMIN((int)res->avg_lum >> 5, 8);
        int lr = res->avg_lum & 31;
        int vi = IMIN((int)res->avg_var >> 5, 8);
        int vr = res->avg_var & 31;
        int32_t maxval_cal = (((32 - lr) *
            (((32 - vr) * p->peak_map[li][vi] + vr * p->peak_map[li][vi + 1])
             >> 5) +
            lr * (((32 - vr) * p->peak_map[li + 1][vi] +
                   vr * p->peak_map[li + 1][vi + 1]) >> 5)) >> 5);
        int32_t cur = e->peak_level;
        int32_t diff = maxval_cal - cur;
        if (diff < 0)
            diff = -diff;
        if (diff > 20) {
            int32_t raw = maxval_cal - (int32_t)cur;
            if (maxval_cal < (int32_t)cur)
                e->peak_level = (cur - maxval_cal < 5)
                                    ? (uint16_t)maxval_cal
                                    : (uint16_t)(cur - 4);
            else
                e->peak_level = (raw < 5) ? (uint16_t)maxval_cal
                                          : (uint16_t)(cur + 4);
            if (e->peak_level < 0x80)
                e->peak_level = 0x80;
        }
    }

    /* 9.4 divergence index. */
    {
        int32_t thresh = p->hist_pixel_count * 16;
        int32_t acc = 0;
        for (i = 0; i < GTM_NCURVE; i++) {
            acc += e->norm_hist[i];
            if (acc >= thresh) {
                e->div_index = (uint16_t)i;
                break;
            }
        }
    }

    /* 9.5 floor/cap clamping and total normalisation. */
    {
        int32_t dark = p->dark_floor >> 3;
        int32_t bright = p->bright_floor >> 3;
        int32_t cap = (int32_t)e->peak_level >> 3;
        int64_t total = 0;
        for (i = 0; i < GTM_NCURVE; i++) {
            int32_t lim = bright;
            int32_t h, adj;
            if (i <= (int)e->div_index)
                lim = interp(i, 0, (int)e->div_index, dark, bright);
            h = e->norm_hist[i];
            adj = (h > cap) ? cap : (h < lim ? lim : h);
            e->adj_hist[i] = adj;
            total += adj;
        }
        if (total < GTM_Q12) {
            int32_t d = (int32_t)(GTM_Q12 - total);
            if (d > 0xFF) {
                for (i = 0; i < GTM_NCURVE; i++)
                    e->adj_hist[i] += d >> 8;
                d &= 0xFF;
            }
            for (i = d; i >= 1; i--)
                e->adj_hist[i] += 1;
        } else if (total != GTM_Q12) {
            int32_t d = (int32_t)(total - GTM_Q12);
            int idx = 0;
            while (d != 0) {
                if (bright < e->adj_hist[idx]) {
                    d--;
                    e->adj_hist[idx] -= 1;
                }
                idx++;
                if (idx > 256)
                    idx = 0;
            }
        }
    }

    /* 9.6 cumulative. */
    e->cum_hist[0] = e->adj_hist[0];
    for (i = 1; i < GTM_NCURVE; i++)
        e->cum_hist[i] = e->cum_hist[i - 1] + e->adj_hist[i];

    /* 9.7 correlations and guide selection. */
    for (i = 0; i < GTM_NCURVE; i++)
        gry += ((double)e->norm_hist[i] * cc) *
               ((double)(e->cum_hist[i] - (int32_t)guide_linear[i]) * cc);

    if (e->guide_state < 1) {
        if (e->guide_state == 0) {
            if (gry == 0.0) {
                guide = guide_linear;
                e->guide_streak = 0;
            } else {
                e->guide_streak++;
                if (e->guide_streak < 10)
                    guide = guide_linear;
                else
                    guide = gtm_reclassify(e, t, gry);
            }
        } else {
            if (gry < 0.0) {
                guide = t->guide_low;
                e->guide_streak = 0;
            } else {
                e->guide_streak++;
                if (e->guide_streak < 10)
                    guide = t->guide_low;
                else
                    guide = gtm_reclassify(e, t, gry);
            }
        }
    } else {
        if (gry <= 0.0) {
            e->guide_streak++;
            if (e->guide_streak < 10)
                guide = t->guide_high;
            else
                guide = gtm_reclassify(e, t, gry);
        } else {
            guide = t->guide_high;
            e->guide_streak = 0;
        }
    }

    for (i = 0; i < GTM_NCURVE; i++)
        gud += ((double)e->norm_hist[i] * cc) *
               ((double)(guide[i] - (int32_t)guide_linear[i]) * cc);

    merge_ratio = (gry == 0.0) ? 0.0 : gud / (gud - gry);
    if (gry < 0.0)
        merge_ratio = -merge_ratio;

    use = e->ratio_hold;
    if (e->guide_streak == 0 && fabs((merge_ratio - use) * 1000.0) > 9.0) {
        e->ratio_hold = merge_ratio;
        use = merge_ratio;
    }

    /* 9.8 merge curve and gamma round-trip. */
    m[0] = 0;
    m[255] = 0x1000;
    {
        double w = fabs(use);
        for (i = 1; i < 255; i++) {
            double v = (double)guide[i] * (1.0 - w) +
                       (double)e->cum_hist[i] * w;
            v += (double)((p->brightness * (int32_t)guide_linear[i]) >> 7);
            if (v < 0.0)
                v = 0.0;
            if (v > 4096.0)
                v = 4096.0;
            m[i] = (int32_t)v;
        }
    }
    for (i = 0; i < GTM_NCURVE; i++)
        e->merge_curve[i] = m[(int)(p->gamma_lut[i * 4] >> 4)];
    for (i = 0; i < GTM_NCURVE; i++)
        e->merge_curve[i] = gtm_inv_map(p, e->merge_curve[i]);

    /* 9.9 output curve, WDR downscale, smoothing. */
    for (i = 0; i < GTM_NCURVE; i++) {
        int32_t g = (int32_t)guide_linear[i];
        if (g == 0) {
            p->curve[i] = 0x200;
        } else {
            int32_t v = (e->merge_curve[i] << 9) / g;
            if (v > GTM_CURVE_MAX)
                v = GTM_CURVE_MAX;
            p->curve[i] = (uint16_t)v;
        }
    }
    {
        int b, passes, span;
        if (!p->wdr_en) {
            b = 0;
            passes = 2;
            span = 256;
        } else {
            uint32_t ex;
            b = p->bit_offset & 0xFF;
            ex = ((uint32_t)b + 1u) & 0xFFu;
            passes = (int)((ex >= 31u) ? 0u : ((2u << ex) & 0xFFu));
            span = (b < 9) ? (256 >> b) : 0;
            for (i = 0; i < GTM_NCURVE; i++)
                p->curve[i] = (i < span) ? p->curve[i << b] : 0x200;
        }
        {
            int sel = span - 1;
            int rep;
            for (rep = 0; rep < passes; rep++) {
                for (i = 1; i < sel; i++) {
                    if (i == 1 || i == span - 2)
                        e->scratch[i] = (p->curve[i - 1] + p->curve[i + 1] +
                                         2 * p->curve[i]) >> 2;
                    else
                        e->scratch[i] = (p->curve[i - 2] + p->curve[i + 2] +
                                         2 * (p->curve[i - 1] +
                                              p->curve[i + 1]) +
                                         4 * p->curve[i]) / 10;
                }
                for (i = 1; i < sel; i++)
                    p->curve[i] = (uint16_t)IMIN(e->scratch[i], GTM_CURVE_MAX);
            }
        }
    }

    /* 9.10 step-limited temporal smoother. */
    for (i = 0; i < GTM_NCURVE; i++) {
        int32_t last = p->curve_prev[i];
        int32_t des = p->curve[i];
        int32_t d = last - des;
        int32_t step, out;
        if (d < 0)
            d = -d;
        if (d > 127)
            d = 127;
        step = t->converge[26 * GTM_CONV_COLS + d];
        out = (last < des) ? (last + step) : (last - step);
        p->curve[i] = (uint16_t)out;
        p->curve_prev[i] = (uint16_t)out;
    }

    res->peak_level = e->peak_level;
    res->div_index = e->div_index;
    res->ratio_hold = e->ratio_hold;
}

/* ------------------------------------------------------------------ */
/* Entry points.                                                       */
/* ------------------------------------------------------------------ */

gtm_entity_t *gtm_init(gtm_ops_t *out_ops)
{
    const freeisp_tables_t *ft = freeisp_get_tables();
    const gtm_clean_tables_t *t;
    gtm_entity_t *e;

    if (!ft || !ft->gtm)
        return NULL;
    t = ft->gtm;
    if (!t->guide_linear || !t->guide_low || !t->guide_high ||
        !t->pre_gamma || !t->eq_kernel || !t->converge)
        return NULL;

    e = (gtm_entity_t *)calloc(1, sizeof(*e));
    if (!e)
        return NULL;

    e->tab = t;
    e->busy_flag = 1;
    e->brightness_saved = (int32_t)0xFFFF0001;
    e->contrast_saved = (int32_t)0xFFFF0001;

    if (out_ops) {
        out_ops->get_params = gtm_get_params;
        out_ops->set_params = gtm_set_params;
        out_ops->run = gtm_run;
    }
    return e;
}

void gtm_exit(gtm_entity_t *e)
{
    free(e);
}

int gtm_get_params(gtm_entity_t *e, gtm_clean_params_t **out)
{
    if (e == NULL)
        return -1;
    if (out != NULL)
        *out = &e->p;
    return 0;
}

int gtm_set_params(gtm_entity_t *e, const gtm_clean_param_req_t *req,
                   int *result)
{
    if (e == NULL)
        return -1;
    if (req != NULL && req->kind == GTM_PARAM_INIT) {
        int i;
        if (req->params != NULL)
            e->p = *req->params;
        if (e->p.curve[255] == 0) {
            for (i = 0; i < GTM_NCURVE; i++) {
                e->p.curve[i] = 0x200;
                e->p.curve_prev[i] = 0x200;
            }
        }
        if (result != NULL)
            *result = 0;
        return 0;
    }
    return -1;
}

int gtm_run(gtm_entity_t *e, const gtm_clean_stats_t *stats,
            gtm_clean_result_t *result)
{
    gtm_clean_params_t *p;

    if (e == NULL || stats == NULL || result == NULL)
        return -1;

    p = &e->p;
    if (p->frame_index <= 2 || p->enable == 0)
        return 0;

    result->curve = p->curve;
    result->curve_prev = p->curve_prev;

    /*
     * An empty histogram (fix_sum == 0) leaves the equalisation path without a
     * generated curve, but the run must still fall through to the curve
     * refresh and the brightness/contrast blend rather than returning here.
     */
    if (p->mode == GTM_MODE_DYNAMIC_RANGE) {
        (void)gtm_eq_path(e, stats);
        gtm_prefilter(e);
    }

    if (p->mode == GTM_MODE_LUMA_HOLD)
        gtm_luma_hold(e, stats, result);
    else
        gtm_blend(e);

    result->hdr_flag = 1;
    return 0;
}

int gtm_clean_guide_state(const gtm_entity_t *e)
{
    if (e == NULL)
        return -1;
    return e->guide_state;
}
