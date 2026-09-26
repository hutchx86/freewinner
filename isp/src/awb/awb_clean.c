/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * awb_clean.c - clean-room auto white-balance module.
 *
 * Implements the behaviour specified in spec/awb.md.  Every tuning
 * table is injected at runtime through freeisp_get_tables(); this file
 * contains no table data.  Section numbers below refer to the spec.
 */
#include "awb_clean.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define IMIN(a, b)        ((a) < (b) ? (a) : (b))
#define ICLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

/*
 * When the run entry point is called without statistics and any output
 * channel is still zero it installs a fixed fallback gain quadruple.  The
 * quadruple is not unity and is sensor-specific tuning, so it is injected
 * through the table contract (safe_gain) rather than compiled in.
 */

/* ------------------------------------------------------------------ */
/* Internal state (spec 5).                                            */
/* ------------------------------------------------------------------ */

typedef struct awb_win {
    uint32_t avg[3];        /* window channel averages                   */
    int32_t  kr, kb;        /* round(256*R/G), round(256*B/G)            */
    uint32_t npix;
    uint32_t sum;           /* R + G + B                                 */
    int32_t  dist;          /* saturated class distance                  */
    int32_t  w_dist;        /* trust weight (0 = reject)                 */
    int32_t  color_temp;    /* decided colour temperature                */
    int32_t  seg;           /* index into curve[64]                      */
    int32_t  w_level;       /* brightness/temperature weight             */
    int32_t  valid;         /* accepted flag                             */
    int32_t  cls;           /* reference class 0..9                      */
    int32_t  is_skin;
    int32_t  is_special;
    int32_t  usable;        /* survived statistics extraction           */
} awb_win_t;

struct awb_entity {
    awb_params_t              p;
    const awb_clean_tables_t *tab;

    int32_t   frame_count;                 /* smoothed-frame counter      */
    int       total_windows;               /* 1024                        */
    awb_win_t win[AWB_NWIN];
    awb_ref_t decision[AWB_NWIN];          /* chosen curve point per win  */
    awb_ref_t pal[AWB_NREF][AWB_NPAL];
    int       pal_count[AWB_NREF];
    awb_ref_t class_curve[AWB_NREF][AWB_NPTS];
    awb_ref_t curve[AWB_NCURVE];
    int32_t   dist_min[AWB_NREF];
    int32_t   win_count[AWB_NREF];
    int32_t   temp_mean[AWB_NREF];
    int32_t   class_weight[AWB_NREF];
    int32_t   trust_lut[100][20];
    uint16_t  gain_new[4];
    uint16_t  gain_hist[AWB_NGHIST][4];
    uint16_t  gain_target[4];
    uint16_t  gain_saved[4];
    uint16_t  high_temp_gain[4];
    int       is_night;
    int32_t   color_temp;
    int32_t   color_temp_target;
    uint32_t  hist[765];
    uint32_t  max_sum;
};

/* ------------------------------------------------------------------ */
/* Small helpers.                                                      */
/* ------------------------------------------------------------------ */

static int32_t round_div(int32_t a, int32_t b)
{
    if (b == 0)
        return 0;
    if (a >= 0)
        return (a + b / 2) / b;
    return -(((-a) + b / 2) / b);
}

static int32_t make_ratio(int32_t channel, int32_t green)
{
    if (green == 0)
        return 0;
    return (channel * 256 + green / 2) / green;
}

static void derive_ref(awb_ref_t *r)
{
    r->refk_r = make_ratio(r->ref[0], r->ref[1]);
    r->refk_b = make_ratio(r->ref[2], r->ref[1]);
}

static void parse_ref(awb_ref_t *r, const int32_t *src)
{
    int i;
    memset(r, 0, sizeof(*r));
    if (src == NULL)
        return;
    for (i = 0; i < 3; i++) {
        r->ref[i]  = src[i];
        r->pref[i] = src[3 + i];
    }
    r->tol       = src[6];
    r->temp      = src[7];
    r->prob_ls   = src[8];
    r->prob_pref = src[9];
    derive_ref(r);
}

/*
 * Full reference load (spec 5.2): parse the ten configured integers, then
 * apply the two defaults the preset record carries.  The skin and special
 * adjusters read the built records, so they must see the same defaults the
 * light-preset path applies.
 */
static void parse_ref_full(const awb_entity_t *e, awb_ref_t *r,
                           const int32_t *src, int index)
{
    parse_ref(r, src);
    if (r->prob_ls > 128)
        r->prob_ls = (index < AWB_NREF) ? e->tab->std_trust[index]
                                        : e->tab->temp_bright[0];
    if (r->prob_pref < 101) {
        int32_t pp = r->prob_pref;
        r->pref[1] = 256;
        r->pref[0] = ((100 - pp) * (r->refk_r - 256)) / 100 + 256;
        r->pref[2] = ((100 - pp) * (r->refk_b - 256)) / 100 + 256;
    }
}

static int32_t sqdist_ref(const awb_ref_t *r, int32_t kr, int32_t kb)
{
    int64_t dr = (int64_t)kr - r->refk_r;
    int64_t db = (int64_t)kb - r->refk_b;
    int64_t d  = dr * dr + db * db;
    if (d > 0x7fffffff)
        d = 0x7fffffff;
    return (int32_t)d;
}

static int32_t isqrt_int(int32_t v)
{
    int32_t x;
    if (v <= 0)
        return 0;
    x = (int32_t)sqrt((double)v);
    while ((int64_t)(x + 1) * (x + 1) <= v)
        x++;
    while ((int64_t)x * x > v)
        x--;
    return x;
}

/* ------------------------------------------------------------------ */
/* Curve interpolation (spec 6.4).                                    */
/* ------------------------------------------------------------------ */

static void build_curve(const awb_ref_t *anchors, int n, awb_ref_t *out)
{
    int32_t t_lo, t_hi, step;
    int k;

    if (n <= 0)
        return;

    t_lo = anchors[0].temp;
    t_hi = anchors[n - 1].temp;
    if ((uint32_t)(t_lo - 1000) >= 8000u || t_lo > t_hi)
        return;

    step = (t_hi - t_lo) / 15;

    for (k = 0; k < AWB_NPTS; k++) {
        int32_t t = t_lo + k * step;
        int j, lo, hi, f;
        int32_t *dst;
        const int32_t *a, *b;

        for (j = 0; j < n; j++) {
            if (anchors[j].temp > t)
                break;
        }
        if (j == n)
            j = n - 1;
        if (j == 0) {
            lo = 0;
            hi = 1;
        } else {
            lo = j - 1;
            hi = j;
        }
        if (hi >= n)
            hi = n - 1;

        dst = (int32_t *)&out[k];
        a   = (const int32_t *)&anchors[lo];
        b   = (const int32_t *)&anchors[hi];
        for (f = 0; f < AWB_REF_FIELDS; f++) {
            int32_t denom = anchors[hi].temp - anchors[lo].temp;
            if (denom == 0)
                dst[f] = a[f];
            else
                dst[f] = a[f] + (b[f] - a[f]) * (t - anchors[lo].temp) / denom;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Trust table resample (spec 6.5).                                   */
/* ------------------------------------------------------------------ */

static void build_trust_lut(awb_entity_t *e)
{
    const int32_t *in = e->tab->temp_bright;
    int i, j;

    for (i = 0; i < 100; i++) {
        double fs = (double)i * 9.0 / 100.0;
        int s0 = (int)fs;
        int s1 = s0 + 1;
        double ts = fs - s0;
        if (s1 > 9)
            s1 = 9;
        for (j = 0; j < 20; j++) {
            double fb = (double)j * 9.0 / 20.0;
            int b0 = (int)fb;
            int b1 = b0 + 1;
            double tb = fb - b0;
            double v;
            if (b1 > 9)
                b1 = 9;
            v  = (double)in[s0 * AWB_NREF + b0] * (1.0 - ts) * (1.0 - tb);
            v += (double)in[s1 * AWB_NREF + b0] * ts * (1.0 - tb);
            v += (double)in[s0 * AWB_NREF + b1] * (1.0 - ts) * tb;
            v += (double)in[s1 * AWB_NREF + b1] * ts * tb;
            e->trust_lut[i][j] = (int32_t)v;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Reference hierarchy (spec 5.2).                                    */
/* ------------------------------------------------------------------ */

static void build_reference_hierarchy(awb_entity_t *e)
{
    const awb_params_t *p = &e->p;
    awb_ref_t rec[AWB_MAX_LIGHTS];
    int g0[AWB_MAX_LIGHTS], g1[AWB_MAX_LIGHTS], g2[AWB_MAX_LIGHTS];
    int n0 = 0, n1 = 0, n2 = 0;
    int n, i, j;

    memset(e->pal, 0, sizeof(e->pal));
    memset(e->pal_count, 0, sizeof(e->pal_count));
    memset(e->class_curve, 0, sizeof(e->class_curve));
    memset(e->curve, 0, sizeof(e->curve));

    n = p->light_num;
    if (n < 0)
        n = 0;
    if (n > AWB_MAX_LIGHTS)
        n = AWB_MAX_LIGHTS;

    for (i = 0; i < n; i++) {
        parse_ref(&rec[i], &p->light_info[i * AWB_REF_INTS]);
        if (rec[i].prob_pref < 101) {
            int32_t pp = rec[i].prob_pref;
            rec[i].pref[1] = 256;
            rec[i].pref[0] = ((100 - pp) * (rec[i].refk_r - 256)) / 100 + 256;
            rec[i].pref[2] = ((100 - pp) * (rec[i].refk_b - 256)) / 100 + 256;
        }
        if (rec[i].prob_ls > 128)
            rec[i].prob_ls = (i < AWB_NREF) ? e->tab->std_trust[i]
                                            : e->tab->temp_bright[0];
    }

    for (i = 0; i < n; i++) {
        if (rec[i].temp < 3500)
            g0[n0++] = i;
        else if (rec[i].temp < 4500)
            g1[n1++] = i;
        else
            g2[n2++] = i;
    }

    for (j = 0; j < n0; j++)
        e->pal[0][j] = rec[g0[j]];
    e->pal_count[0] = n0;
    for (j = 0; j < n1; j++)
        e->pal[1][j] = rec[g1[j]];
    e->pal_count[1] = n1;
    for (j = 0; j < n2; j++)
        e->pal[2][j] = rec[g2[j]];
    e->pal_count[2] = n2;

    build_curve(e->pal[0], n0, e->class_curve[0]);
    build_curve(e->pal[1], n1, e->class_curve[1]);
    build_curve(e->pal[2], n2, e->class_curve[2]);

    if (n0 > 0) {
        e->pal[3][0] = rec[g0[n0 - 1]];
        e->pal[3][1] = e->class_curve[1][0];
        e->pal_count[3] = 2;
        build_curve(e->pal[3], 2, e->class_curve[3]);
    }
    if (n2 > 0) {
        e->pal[4][0] = e->class_curve[1][0];
        e->pal[4][1] = rec[g2[0]];
        e->pal_count[4] = 2;
        build_curve(e->pal[4], 2, e->class_curve[4]);
    }

    for (i = 0; i < AWB_NPTS; i++) {
        e->curve[i]            = e->class_curve[0][i];
        e->curve[16 + i]       = e->class_curve[3][i];
        e->curve[32 + i]       = e->class_curve[4][i];
        e->curve[48 + i]       = e->class_curve[2][i];
    }
}

/* ------------------------------------------------------------------ */
/* Statistics extraction (spec 6.2).                                  */
/* ------------------------------------------------------------------ */

static void extract_stats(awb_entity_t *e, const awb_stats_t *stats)
{
    int i;

    memset(e->hist, 0, sizeof(e->hist));
    e->max_sum = 0;

    for (i = 0; i < e->total_windows; i++) {
        const awb_win_stat_t *s = &stats->win[i];
        awb_win_t *w = &e->win[i];
        uint32_t R = s->avg[0], G = s->avg[1], B = s->avg[2];

        memset(w, 0, sizeof(*w));

        if ((R - 2u) < 249u && (G - 2u) < 249u && (B - 2u) < 249u) {
            w->avg[0] = R;
            w->avg[1] = G;
            w->avg[2] = B;
            w->sum    = R + G + B;
            w->kr     = make_ratio((int32_t)R, (int32_t)G);
            w->kb     = make_ratio((int32_t)B, (int32_t)G);
            w->npix   = s->npix;
            w->usable = 1;
            if (w->sum < 765)
                e->hist[w->sum]++;
            if (w->sum > e->max_sum)
                e->max_sum = w->sum;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Night detection (spec 6.8).                                        */
/* ------------------------------------------------------------------ */

static void detect_night(awb_entity_t *e)
{
    if (e->p.ae_lv > 600) {
        e->is_night = 0;
        return;
    }
    {
        int dark = 0, i;
        for (i = 0; i < e->total_windows; i++) {
            const awb_win_t *w = &e->win[i];
            if ((int64_t)w->avg[0] + 2 * (int64_t)w->avg[1] + w->avg[2] < 84)
                dark++;
        }
        e->is_night = (e->total_windows * 70 / 100 < dark);
    }
}

/* ------------------------------------------------------------------ */
/* Per-window classification (spec 6.3).                              */
/* ------------------------------------------------------------------ */

static void classify(awb_entity_t *e)
{
    int i, c, k;

    for (i = 0; i < e->total_windows; i++) {
        awb_win_t *w = &e->win[i];
        int have[AWB_NREF];
        int min_idx = -1, second_idx = -1;
        int64_t best1 = 0, best2 = 0;
        const awb_ref_t *pt1 = NULL, *pt2 = NULL;
        int chosen_cls;

        for (c = 0; c < AWB_NREF; c++)
            have[c] = 0;
        for (c = 0; c < AWB_NPAL; c++) {
            /*
             * The per-class minimum is accumulated in a 16-bit quantity
             * seeded to 65535, so any anchor distance above 65535
             * saturates rather than propagating as a 32-bit value
             * (spec 6.3 step 1).  Squared distances shrank to 16 bits
             * here is a behaviour fact, not a storage optimisation.
             */
            int32_t dmin = 65535;
            if (e->pal_count[c] <= 0)
                continue;
            for (k = 0; k < e->pal_count[c]; k++) {
                int32_t d = sqdist_ref(&e->pal[c][k], w->kr, w->kb);
                if (d < dmin)
                    dmin = d;
            }
            e->dist_min[c] = dmin;
            have[c] = 1;
        }

        for (c = 0; c < AWB_NPAL; c++) {
            if (!have[c])
                continue;
            if (min_idx < 0 || e->dist_min[c] < best1) {
                best1 = e->dist_min[c];
                min_idx = c;
            }
        }
        /*
         * Second-nearest selection (spec 6.3 step 2).  The reference search
         * seeds its candidate with the last searched index when the nearest
         * class is index 0, and with the first index otherwise, then keeps
         * strictly smaller candidates.  Exact ties therefore resolve to the
         * last index in the first case and to the first index otherwise.
         */
        second_idx = (min_idx == 0) ? (AWB_NPAL - 1) : 0;
        if (!have[second_idx] || second_idx == min_idx)
            second_idx = -1;
        for (c = 0; c < AWB_NPAL; c++) {
            if (!have[c] || c == min_idx)
                continue;
            if (second_idx < 0 || e->dist_min[c] < e->dist_min[second_idx]) {
                second_idx = c;
                best2 = e->dist_min[c];
            }
        }

        if (min_idx < 0) {
            w->cls = 9;
            w->dist = 255;
            w->color_temp = 0;
            w->seg = 0;
            w->valid = 0;
            memset(&e->decision[i], 0, sizeof(e->decision[i]));
            continue;
        }

        for (k = 0; k < AWB_NPTS; k++) {
            int32_t d = sqdist_ref(&e->class_curve[min_idx][k], w->kr, w->kb);
            if (pt1 == NULL || d < best1) {
                best1 = d;
                pt1 = &e->class_curve[min_idx][k];
            }
        }
        if (second_idx >= 0) {
            for (k = 0; k < AWB_NPTS; k++) {
                int32_t d = sqdist_ref(&e->class_curve[second_idx][k], w->kr, w->kb);
                if (pt2 == NULL || d < best2) {
                    best2 = d;
                    pt2 = &e->class_curve[second_idx][k];
                }
            }
        }

        if (pt2 != NULL && best2 < best1) {
            chosen_cls = second_idx;
            e->decision[i] = *pt2;
            w->dist = (int32_t)isqrt_int((int32_t)best2);
        } else {
            chosen_cls = min_idx;
            e->decision[i] = *pt1;
            w->dist = (int32_t)isqrt_int((int32_t)best1);
        }
        w->dist = ICLAMP(w->dist, 0, 255);
        w->cls = chosen_cls;
        w->color_temp = e->decision[i].temp;

        for (k = 0; k < 63; k++) {
            if (e->curve[k].temp >= w->color_temp)
                break;
        }
        w->seg = k;
    }
}

/* ------------------------------------------------------------------ */
/* Per-window trust and scene weighting (spec 6.6).                   */
/* ------------------------------------------------------------------ */

static int green_gate(const awb_entity_t *e, const awb_win_t *w, const awb_ref_t *d)
{
    return e->p.green_dist < w->dist &&
           w->kr < d->refk_r &&
           w->kb < d->refk_b &&
           (uint32_t)(d->temp - 4001) < 3499u;
}

static int blue_gate(const awb_entity_t *e, const awb_win_t *w, const awb_ref_t *d)
{
    int32_t diff = w->kr - d->refk_r;
    if (diff < 0)
        diff = -diff;
    return e->p.blue_dist < w->dist &&
           diff < e->p.blue_dist &&
           d->refk_b < w->kb &&
           d->temp > 7300;
}

static void trust_weight(awb_entity_t *e)
{
    const awb_params_t *p = &e->p;
    int i, c;

    for (c = 0; c < AWB_NREF; c++) {
        e->win_count[c] = 0;
        e->temp_mean[c] = 0;
        e->class_weight[c] = 0;
    }

    for (i = 0; i < e->total_windows; i++) {
        awb_win_t *w = &e->win[i];
        const awb_ref_t *d = &e->decision[i];
        uint32_t ti;
        int32_t level_idx, prob, wl, tol;

        ti = (uint32_t)(w->color_temp - 950) / 100u;
        if (ti > 99u)
            ti = 99u;

        level_idx = (p->ae_lv + 50) / 100;
        if (level_idx < 0)
            level_idx = 0;
        if (level_idx > 18)
            level_idx = 19;

        prob = ICLAMP(d->prob_ls, 16, 128);
        wl = e->trust_lut[ti][level_idx] * prob;
        if (wl < 0)
            wl += 15;
        wl >>= 4;

        w->w_level = wl;
        tol = ICLAMP(d->tol, 0, AWB_NTRUST - 1);
        w->w_dist = e->tab->trust[tol * 256 + ICLAMP(w->dist, 0, 255)];
        w->valid = (wl != 0 && w->w_dist != 0);

        if (green_gate(e, w, d))
            w->cls = 5;
        if (blue_gate(e, w, d))
            w->cls = 7;
        if (!w->valid)
            w->cls = 9;

        if (w->cls < 0 || w->cls >= AWB_NREF)
            w->cls = 9;
        e->win_count[w->cls]++;
        e->temp_mean[w->cls] += w->color_temp * w->w_dist;
        e->class_weight[w->cls] += w->w_dist;
    }

    for (c = 0; c < AWB_NREF; c++) {
        if (e->class_weight[c] != 0)
            e->temp_mean[c] /= e->class_weight[c];
        else
            e->temp_mean[c] = 0;
    }
}

static void scene_weights(awb_entity_t *e, int32_t *sw)
{
    int i;
    int32_t half   = e->total_windows * 50 / 100;
    int32_t threeq = e->total_windows * 75 / 100;
    int32_t seven  = e->total_windows * 70 / 100;

    for (i = 0; i < AWB_NREF; i++)
        sw[i] = 16;

    if (e->win_count[0] > half) {
        sw[0] = 16; sw[1] = 12; sw[2] = 1; sw[3] = 12; sw[4] = 2;
    } else if (e->win_count[0] + e->win_count[3] > threeq) {
        sw[0] = 16; sw[1] = 12; sw[2] = 2; sw[3] = 16; sw[4] = 8;
    } else if (e->win_count[3] + e->win_count[4] <= seven) {
        if (e->win_count[4] <= seven) {
            sw[0] = 16; sw[1] = 16; sw[2] = 16; sw[3] = 16; sw[4] = 16;
        } else {
            sw[0] = 8; sw[1] = 12; sw[2] = 16; sw[3] = 8; sw[4] = 16;
            sw[5] = 8;
        }
    } else {
        sw[0] = 12; sw[1] = 16; sw[2] = 2; sw[3] = 16; sw[4] = 16;
    }

    for (i = 0; i < e->total_windows; i++) {
        awb_win_t *w = &e->win[i];
        if (w->valid && w->cls >= 0 && w->cls < AWB_NREF)
            w->w_level = (w->w_level * sw[w->cls]) >> 2;
    }
}

/* ------------------------------------------------------------------ */
/* Colour adjusters (spec 6.7).                                       */
/* ------------------------------------------------------------------ */

static int first_curve_above(const awb_entity_t *e, int32_t t)
{
    int i;
    for (i = 0; i < 63; i++) {
        if (e->curve[i].temp >= t)
            return i;
    }
    return 63;
}

/* 6.7.2 blue sky */
static void blue_sky_adjust(awb_entity_t *e)
{
    int i;
    int base = first_curve_above(e, 5499);

    for (i = 0; i < e->total_windows; i++) {
        awb_win_t *w = &e->win[i];
        int seg;
        int32_t rr, br, base_r, base_b;
        int32_t scale_r, scale_b;

        if (w->cls != 7 || !w->usable)
            continue;
        seg = ICLAMP(w->seg, 0, AWB_NCURVE - 1);
        rr = make_ratio(e->curve[seg].ref[0], e->curve[seg].ref[1]);
        br = make_ratio(e->curve[seg].ref[2], e->curve[seg].ref[1]);
        base_r = make_ratio(e->curve[base].ref[0], e->curve[base].ref[1]);
        base_b = make_ratio(e->curve[base].ref[2], e->curve[base].ref[1]);
        if (rr == 0 || br == 0)
            continue;
        scale_r = round_div(w->kr * base_r, rr);
        scale_b = round_div(w->kb * base_b, br);
        w->avg[0] = (uint32_t)((w->avg[1] * scale_r) >> 8);
        w->avg[2] = (uint32_t)((w->avg[1] * scale_b) >> 8);
        w->kr = e->curve[base].refk_r;
        w->kb = e->curve[base].refk_b;
        w->valid = 1;
    }
}

/* 6.7.4 special colour.  Runs on all windows with non-zero ratios. */
static void special_adjust(awb_entity_t *e)
{
    const awb_params_t *p = &e->p;
    int n = p->special_num;
    int i, j;

    if (n <= 0)
        return;
    if (n > AWB_MAX_SPECIAL)
        n = AWB_MAX_SPECIAL;

    for (i = 0; i < e->total_windows; i++) {
        awb_win_t *w = &e->win[i];
        int best = -1;
        int32_t min_d = 0;
        int32_t real_dist;
        int32_t gain_r, gain_b, weight = 0;
        const awb_ref_t *special;
        const awb_ref_t *base;
        int32_t ratio_r, ratio_b, t0, t2;

        if (n == 0 || w->kr == 0 || w->kb == 0)
            continue;

        for (j = 0; j < n; j++) {
            awb_ref_t r;
            int32_t d;
            parse_ref_full(e, &r, &p->special_info[j * AWB_REF_INTS], j);
            d = sqdist_ref(&r, w->kr, w->kb);
            if (best < 0 || d < min_d) {
                min_d = d;
                best = j;
                w->is_special = 1;
            }
        }
        if (best < 0)
            continue;

        {
            awb_ref_t chosen;
            parse_ref_full(e, &chosen, &p->special_info[best * AWB_REF_INTS],
                           best);
            special = &chosen;

            real_dist = ICLAMP(isqrt_int(min_d), 0, 255);
            {
                int32_t tol = ICLAMP(special->tol, 0, AWB_NTRUST - 1);
                int32_t tv = e->tab->trust[tol * 256 + real_dist];
                if (tv == 0) {
                    w->is_special = 0;
                    continue;
                }

                if (special->prob_pref <= 100) {
                    gain_r = gain_b = 256;
                    if (p->ae_lv <= 700)
                        continue;
                    weight = 32;
                } else {
                    gain_r = round_div(special->pref[1] * 256, special->pref[0]);
                    gain_b = round_div(special->pref[1] * 256, special->pref[2]);
                    if (special->prob_pref <= special->prob_ls) {
                        if (special->prob_pref == special->prob_ls) {
                            if (special->prob_ls >= p->ae_lv)
                                continue;
                            weight = 32;
                        } else {
                            if (special->prob_pref > p->ae_lv)
                                weight = 32;
                            else if (p->ae_lv > special->prob_ls)
                                continue;
                            else
                                weight = (p->ae_lv - special->prob_pref) * 32 /
                                         (special->prob_pref - special->prob_ls) + 32;
                        }
                    } else {
                        if (p->ae_lv < special->prob_ls)
                            continue;
                        else if (special->prob_pref < p->ae_lv)
                            weight = 32;
                        else
                            weight = (p->ae_lv - special->prob_ls) * 32 /
                                     (special->prob_pref - special->prob_ls);
                    }
                    if (weight == 0)
                        continue;
                }

                base = &e->curve[first_curve_above(e, special->temp)];
                ratio_r = make_ratio(base->ref[0], base->ref[1]);
                ratio_b = make_ratio(base->ref[2], base->ref[1]);
                t0 = round_div(gain_r * ratio_r, 256);
                t2 = round_div(gain_b * ratio_b, 256);
                w->avg[0] = (uint32_t)((w->avg[1] * t0) >> 8);
                w->avg[2] = (uint32_t)((w->avg[1] * t2) >> 8);
                w->kr = base->refk_r;
                w->kb = base->refk_b;
                w->dist = real_dist;
                w->is_special = 1;
                w->color_temp = base->temp;
                w->w_dist = round_div(weight * tv, 32);
            }
        }
    }
}

/* 6.7.1 green zone */
static void green_zone_adjust(awb_entity_t *e)
{
    int i;

    for (i = 0; i < e->total_windows; i++) {
        awb_win_t *w = &e->win[i];
        int seg;
        int32_t rr, br;

        if (w->cls != 5 || !w->usable)
            continue;
        seg = ICLAMP(w->seg, 0, AWB_NCURVE - 1);
        rr = make_ratio(e->curve[seg].ref[0], e->curve[seg].ref[1]);
        br = make_ratio(e->curve[seg].ref[2], e->curve[seg].ref[1]);
        w->avg[0] = (uint32_t)((w->avg[1] * rr) >> 8);
        w->avg[2] = (uint32_t)((w->avg[1] * br) >> 8);
        w->kr = e->curve[seg].refk_r;
        w->kb = e->curve[seg].refk_b;
        w->valid = 0;
    }
}

/* 6.7.3 skin, invoked only in the high-temperature branch. */
static void skin_adjust(awb_entity_t *e)
{
    const awb_params_t *p = &e->p;
    int n = p->skin_num;
    int64_t sum_r = 0, sum_g = 0, sum_b = 0;
    int i, j;
    uint16_t preset[4];

    if (e->win_count[2] + e->win_count[4] <= e->total_windows / 25)
        return;

    for (i = 0; i < e->total_windows; i++) {
        const awb_win_t *w = &e->win[i];
        if (w->cls == 2 || w->cls == 4) {
            sum_r += w->avg[0];
            sum_g += w->avg[1];
            sum_b += w->avg[2];
        }
    }

    /*
     * Deployed guard: the red source is only used when green exceeds 16;
     * otherwise the green sum itself is the sanity check.
     */
    {
        int32_t gsel = (sum_g > 0x10) ? (int32_t)sum_r : (int32_t)sum_g;

        if (sum_b < 0x11 || gsel < 0x11) {
            preset[0] = 256; preset[1] = 256;
            preset[2] = 256; preset[3] = 256;
        } else {
            preset[0] = (uint16_t)((sum_g << 4) / (sum_r >> 4));
            preset[1] = 256;
            preset[2] = 256;
            preset[3] = (uint16_t)((sum_g << 4) / (sum_b >> 4));
        }
    }
    e->high_temp_gain[0] = preset[0];
    e->high_temp_gain[1] = preset[1];
    e->high_temp_gain[2] = preset[2];
    e->high_temp_gain[3] = preset[3];

    if (n <= 0)
        return;
    if (n > AWB_MAX_SKIN)
        n = AWB_MAX_SKIN;

    for (i = 0; i < e->total_windows; i++) {
        awb_win_t *w = &e->win[i];
        awb_ref_t skin;
        int best = -1;
        int32_t min_d = 0;
        int32_t u, v, real_dist;

        if (!w->valid || w->kr == 0 || w->kb == 0)
            continue;

        /* Vendor integer truncation, not rounding. */
        u = (w->kr * preset[0]) / preset[1];
        v = (w->kb * preset[3]) / preset[1];
        if (u == 0 || v == 0) {
            w->is_skin = 0;
            continue;
        }
        for (j = 0; j < n; j++) {
            awb_ref_t r;
            int32_t d;
            parse_ref_full(e, &r, &p->skin_info[j * AWB_REF_INTS], j);
            d = sqdist_ref(&r, u, v);
            if (best < 0 || d < min_d) {
                min_d = d;
                best = j;
            }
        }
        if (best < 0) {
            w->is_skin = 0;
            continue;
        }
        parse_ref_full(e, &skin, &p->skin_info[best * AWB_REF_INTS], best);
        real_dist = ICLAMP(isqrt_int(min_d), 0, 255);
        {
            int32_t tol = ICLAMP(skin.tol, 0, AWB_NTRUST - 1);
            int32_t tv = e->tab->trust[tol * 256 + real_dist];
            if (tv == 0 || u == 0 || v == 0) {
                w->is_skin = 0;
                continue;
            }
            w->dist = real_dist;
            w->is_skin = 1;
            w->avg[0] = (uint32_t)round_div(w->avg[0] * 256, u);
            w->avg[2] = (uint32_t)round_div(w->avg[2] * 256, v);
            w->kr = round_div(w->kr * 256, u);
            w->kb = round_div(w->kb * 256, v);
            w->w_dist = tv;
        }
    }
}

static void colour_adjust(awb_entity_t *e)
{
    blue_sky_adjust(e);
    special_adjust(e);
    green_zone_adjust(e);
    skin_adjust(e);
}

/* ------------------------------------------------------------------ */
/* Grey-world estimate (spec 6.9).                                    */
/* ------------------------------------------------------------------ */

static void neighbour_filter(awb_entity_t *e)
{
    int i;

    if (e->is_night)
        return;

    for (i = 0; i < e->total_windows; i++) {
        awb_win_t *w = &e->win[i];
        int cx, cy, dx, dy, same = 0, total = 0;
        int32_t bad;

        if (!w->valid)
            continue;
        cx = i % AWB_GRID_W;
        cy = i / AWB_GRID_W;
        for (dy = -1; dy <= 1; dy++) {
            for (dx = -1; dx <= 1; dx++) {
                int nx = cx + dx, ny = cy + dy;
                int32_t diff;
                if (nx < 0 || nx >= AWB_GRID_W || ny < 0 || ny >= AWB_GRID_H)
                    continue;
                total++;
                diff = w->color_temp - e->win[ny * AWB_GRID_W + nx].color_temp;
                if (diff > -500 && diff < 500)
                    same++;
            }
        }
        if (total == 0)
            continue;
        bad = (int32_t)((int64_t)same * 256 / total);
        if (bad < 100)
            w->valid = 0;
    }
}

static int grey_world(awb_entity_t *e)
{
    int i;
    int64_t sum_r = 0, sum_g = 0, sum_b = 0;
    int64_t Y;

    for (i = 0; i < e->total_windows; i++) {
        const awb_win_t *w = &e->win[i];
        int64_t weight = (int64_t)w->w_dist * w->valid * w->w_level;
        if (weight == 0)
            continue;
        sum_r += weight * w->avg[0];
        sum_g += weight * w->avg[1];
        sum_b += weight * w->avg[2];
    }

    if (sum_r == 0 || sum_g == 0 || sum_b == 0) {
        int c;
        for (c = 0; c < 4; c++) {
            e->gain_new[c] = e->gain_saved[c];
            e->gain_target[c] = e->gain_new[c];
        }
        return 0;
    }

    Y = (sum_g + 1) / 2 + (sum_b + 2) / 4 + (sum_r + 2) / 4;

    e->gain_new[0] = (uint16_t)((Y * 256 + sum_r / 2) / sum_r);
    e->gain_new[1] = (uint16_t)((Y * 256 + sum_g / 2) / sum_g);
    e->gain_new[2] = e->gain_new[1];
    e->gain_new[3] = (uint16_t)((Y * 256 + sum_b / 2) / sum_b);
    return 0;
}

/* spec 6.9.3 */
static int32_t curve_lookup(const awb_entity_t *e, const uint16_t g[4])
{
    int i, best = -1;
    int64_t bestd = 0;
    int32_t r = g[0], gr = g[1], gb = g[2], b = g[3];

    if (r == 0 || b == 0)
        return 0;

    for (i = 0; i < AWB_NCURVE; i++) {
        int32_t v = round_div(b, 2) + gb * 256;
        int32_t rr = round_div(r, 2) + gr * 256;
        int64_t d;
        v = v / b - e->curve[i].refk_b;
        rr = rr / r - e->curve[i].refk_r;
        d = (int64_t)rr * rr + (int64_t)v * v;
        if (best < 0 || d < bestd) {
            bestd = d;
            best = i;
        }
    }
    return e->curve[best].temp;
}

/* ------------------------------------------------------------------ */
/* Temporal smoothing (spec 6.10).                                    */
/* ------------------------------------------------------------------ */

static void smooth_gain(awb_entity_t *e, uint16_t out[4])
{
    int32_t speed = e->p.speed;
    int row, k, c;
    int taps;
    const uint16_t *weights;
    int64_t numer[4] = {0, 0, 0, 0};
    int64_t denom = 0;

    if (e->p.frame_id < 33) {
        if (speed < 16)
            speed = speed * (e->p.frame_id - 3) / 30 + 1;
        else
            speed = (e->p.frame_id * 15 - 45) / 30 + (speed - 15);
    }
    row = (int)((uint32_t)speed & 0xffffu);
    weights = e->tab->speed_w + row * AWB_NSSC;

    for (c = 0; c < 4; c++)
        e->gain_hist[e->frame_count % AWB_NGHIST][c] = e->gain_new[c];

    /*
     * Warm-up: average only the taps that hold real history.  The ring is
     * seeded to unity but those entries are not yet "available"; until 48
     * frames have been pushed the sum runs over frame_count + 1 taps
     * (spec 6.10).
     */
    taps = e->frame_count + 1;
    if (taps > AWB_NGHIST)
        taps = AWB_NGHIST;
    if (taps < 1)
        taps = 1;

    for (k = 0; k < taps; k++) {
        uint16_t wk = weights[k];
        int idx = (int)((e->frame_count + AWB_NGHIST - k) % AWB_NGHIST);
        for (c = 0; c < 4; c++)
            numer[c] += (int64_t)wk * e->gain_hist[idx][c];
        denom += wk;
    }
    if (denom == 0)
        denom = 1;

    for (c = 0; c < 4; c++) {
        int32_t v = (int32_t)((numer[c] + denom / 2) / denom);
        out[c] = (uint16_t)ICLAMP(v, AWB_GAIN_MIN, AWB_GAIN_MAX);
    }
    e->frame_count++;
}

/* ------------------------------------------------------------------ */
/* User preference blend (spec 6.11).                                 */
/* ------------------------------------------------------------------ */

static void preference_blend(awb_entity_t *e, uint16_t g[4])
{
    const awb_params_t *p = &e->p;
    int i = first_curve_above(e, e->color_temp_target);
    const awb_ref_t *cr = &e->curve[i];
    int32_t pr = cr->pref[0], pg = cr->pref[1], pb = cr->pref[2];
    int32_t r = g[0], gr = g[1], b = g[3];

    if (cr->prob_pref < 101 && r != 0 && b != 0) {
        int32_t base = p->base_temp ? p->base_temp : 6500;
        int j = first_curve_above(e, base);
        int32_t ratio_r = round_div(gr * 256, r);
        int32_t ratio_b = round_div(gr * 256, b);
        int32_t yr, yb;
        if (e->curve[j].refk_r == 0 || e->curve[j].refk_b == 0 || pg == 0)
            goto favourites;
        yr = round_div(ratio_r * 256, e->curve[j].refk_r);
        yb = round_div(ratio_b * 256, e->curve[j].refk_b);
        pr = pr * (((100 - cr->prob_pref) * (yr - 256)) / 100) / pg + 256;
        pb = pb * (((100 - cr->prob_pref) * (yb - 256)) / 100) / pg + 256;
        pg = 256;
    }

    if (pg != 0) {
        r = r * pr / pg;
        b = b * pb / pg;
    }

favourites:
    if (p->r_favor != 0 && p->b_favor != 0) {
        r = (p->r_favor * r + 128) >> 8;
        b = (p->b_favor * b + 128) >> 8;
    }
    g[0] = (uint16_t)r;
    g[3] = (uint16_t)b;
}

/* ------------------------------------------------------------------ */
/* Gain normalisation (spec 6.12).                                    */
/* ------------------------------------------------------------------ */

static void constrain_gain(uint16_t g[4])
{
    int32_t m = IMIN(g[0], IMIN(g[2], g[3]));
    if (m <= 0)
        return;
    g[0] = (uint16_t)(g[0] * 256 / m);
    g[1] = (uint16_t)(g[1] * 256 / m);
    g[2] = (uint16_t)(g[2] * 256 / m);
    g[3] = (uint16_t)(g[3] * 256 / m);
}

/* ------------------------------------------------------------------ */
/* Preset scenes and fixed temperature (spec 6.14).                   */
/* ------------------------------------------------------------------ */

static void preset_scene(awb_entity_t *e, uint16_t g[4])
{
    const awb_params_t *p = &e->p;
    int mode = p->mode;
    int32_t p_r, p_b, lo_r, hi_r, lo_b, hi_b;

    if (mode < 2)
        return;

    if (mode > 9) {
        /*
         * The `7 < ((mode - 2) & 0xff)` branch of the preset-scene
         * correction: any mode outside the preset range (e.g. WB_TUNGSTEN =
         * 10) applies unity ratios, which normalises red and blue onto the
         * green gain.
         */
        p_r = 256;
        p_b = 256;
    } else {
        switch (mode) {
        case 2: lo_r = 120; hi_r = 170; lo_b = 450; hi_b = 500; break;
        case 3: lo_r = 200; hi_r = 220; lo_b = 320; hi_b = 370; break;
        case 4: lo_r = 290; hi_r = 305; lo_b = 290; hi_b = 305; break;
        case 6: lo_r = 250; hi_r = 260; lo_b = 250; hi_b = 260; break;
        case 8: lo_r = 260; hi_r = 270; lo_b = 245; hi_b = 250; break;
        default: lo_r = 254; hi_r = 256; lo_b = 254; hi_b = 256; break;
        }

        p_r = p->preset_gain[mode * 2];
        p_b = p->preset_gain[mode * 2 + 1];
        if (p_r == 0 || p_b == 0) {
            p_r = round_div(g[0] * 256, g[1]);
            p_b = round_div(g[3] * 256, g[1]);
            p_r = ICLAMP(p_r, lo_r, hi_r);
            p_b = ICLAMP(p_b, lo_b, hi_b);
        }
    }
    g[0] = (uint16_t)((g[1] * p_r) >> 8);
    g[2] = g[1];
    g[3] = (uint16_t)((g[1] * p_b) >> 8);
}

static void fixed_temperature(awb_entity_t *e, awb_result_t *result)
{
    int32_t ct = e->p.fixed_temp ? e->p.fixed_temp : 6500;
    int i = first_curve_above(e, ct);
    int32_t rr = e->curve[i].refk_r;
    int32_t bb = e->curve[i].refk_b;
    uint16_t r = 256, b = 256;

    if (rr != 0)
        r = (uint16_t)((rr / 2 + 65536) / rr);
    if (bb != 0)
        b = (uint16_t)((bb / 2 + 65536) / bb);

    result->gain_out.r  = r;
    result->gain_out.gr = 256;
    result->gain_out.gb = 256;
    result->gain_out.b  = b;
    result->color_temp_out = ct;
    e->color_temp = ct;
}

/* ------------------------------------------------------------------ */
/* Per-frame state machine (spec 6.1 / 6.9).                          */
/* ------------------------------------------------------------------ */

static int fresh_estimate_needed(const awb_entity_t *e)
{
    const awb_params_t *p = &e->p;
    if (p->ae_index < 2)
        return 1;
    if (p->ae_index_max != 0 && p->ae_index_max - 1 <= p->ae_index)
        return 1;
    if (p->interval != 0)
        return 1;
    if (p->ae_done == 0)
        return 1;
    if (p->frame_id < 34)
        return 1;
    if (p->frame_id % 10 == 0)
        return 1;
    return 0;
}

static int adaptive_estimate(awb_entity_t *e, const awb_stats_t *stats)
{
    int32_t sw[AWB_NREF];
    int thr;

    if (!fresh_estimate_needed(e))
        return 0;

    /*
     * Pipeline order: statistics, classification
     * and trust, then the neighbour filter (daytime only), then the colour
     * adjusters (blue sky, special, green zone, skin), then the scene weights,
     * then the outlier abort, and only then the grey-world estimate.  The
     * scene weights must follow the adjusters: blue sky can revive a window
     * that was invalid at classification time.
     */
    extract_stats(e, stats);
    detect_night(e);
    classify(e);
    trust_weight(e);
    neighbour_filter(e);
    colour_adjust(e);
    scene_weights(e, sw);

    thr = e->is_night ? 90 : 80;
    if (thr * e->total_windows / 100 < e->win_count[9])
        return -1;

    grey_world(e);

    e->gain_target[0] = e->gain_new[0];
    e->gain_target[1] = e->gain_new[1];
    e->gain_target[2] = e->gain_new[2];
    e->gain_target[3] = e->gain_new[3];
    e->color_temp_target = curve_lookup(e, e->gain_new);
    e->color_temp = e->color_temp_target;
    return 0;
}

int awb_isr(awb_entity_t *e, const awb_stats_t *stats, awb_result_t *result)
{
    const awb_params_t *p = &e->p;
    uint16_t g[4];
    int i;
    int gate;

    if (p->frame_id < 3) {
        if (result->gain_out.r != 0 && result->gain_out.gr != 0 &&
            result->gain_out.gb != 0 && result->gain_out.b != 0)
            return 0;
        fixed_temperature(e, result);
        return 0;
    }

    if (p->control_enable == 0) {
        fixed_temperature(e, result);
        return 0;
    }

    /*
     * Run gate: run when the frame is in the
     * start-up window, when no re-estimate interval is configured, or when the
     * frame lands on the interval boundary; the lock flag then suppresses it.
     * The interval is masked to 16 bits, exactly as the object does.
     */
    {
        uint32_t interval = (uint32_t)p->interval & 0xffffu;
        uint32_t fid = (uint32_t)p->frame_id;

        gate = (fid < 33u) || (interval == 0u) ||
               ((fid - 1u) == interval * ((fid - 1u) / interval));
    }

    if (!gate || p->lock)
        return 0;

    if (p->mode == 0) {
        for (i = 0; i < 4; i++)
            g[i] = (uint16_t)ICLAMP(p->manual_gain[i], 0, 0xffff);
        result->gain_out.r = g[0];
        result->gain_out.gr = g[1];
        result->gain_out.gb = g[2];
        result->gain_out.b = g[3];
        return 0;
    }

    /*
     * The deployed object abandons the adaptive estimate on an outlier
     * imbalance *before* touching the result.  On abort the gains and colour
     * temperature are left exactly as the caller supplied them.
     */
    if (adaptive_estimate(e, stats) == 0) {
        smooth_gain(e, g);

        /*
         * The colour temperature is sampled from the *smoothed* gains, before
         * the preference blend and the constraint.  Those two steps then
         * change the gains without changing the published temperature.
         */
        result->color_temp_out = curve_lookup(e, g);
        /* The preference/curve lookup is indexed by the temperature derived
         * from the smoothed output gains, recomputed every frame, not by the
         * pre-smoothing grey-world target held in color_temp_target. */
        e->color_temp_target = result->color_temp_out;
        preference_blend(e, g);
        constrain_gain(g);

        result->gain_out.r = g[0];
        result->gain_out.gr = g[1];
        result->gain_out.gb = g[2];
        result->gain_out.b = g[3];
        e->color_temp = result->color_temp_out;

        for (i = 0; i < 4; i++)
            e->gain_saved[i] = e->gain_target[i];
    }

    /*
     * Modes other than adaptive (WB_AUTO) run the preset-scene correction on
     * the result gains, including when the adaptive estimate aborted and the
     * gains were left untouched (the preset branch applies it
     * unconditionally).
     */
    if (p->mode != 1) {
        g[0] = result->gain_out.r;
        g[1] = result->gain_out.gr;
        g[2] = result->gain_out.gb;
        g[3] = result->gain_out.b;
        preset_scene(e, g);
        result->gain_out.r = g[0];
        result->gain_out.gr = g[1];
        result->gain_out.gb = g[2];
        result->gain_out.b = g[3];
    }
    return 0;
}

int awb_run(awb_entity_t *e, const awb_stats_t *stats, awb_result_t *result)
{
    if (e == NULL || stats == NULL) {
        /*
         * Without statistics the configured safe quadruple replaces the
         * result when *any* channel is zero, not only when all four are (a
         * partly-zero gain is replaced wholesale).  The quadruple is injected
         * through the table contract; when none is supplied the caller's
         * result is left untouched.
         */
        if (result != NULL &&
            (result->gain_out.r == 0 || result->gain_out.gr == 0 ||
             result->gain_out.gb == 0 || result->gain_out.b == 0)) {
            const freeisp_tables_t *ft = freeisp_get_tables();
            const awb_clean_tables_t *t = (ft != NULL) ? ft->awb : NULL;
            const uint16_t *sg = (t != NULL) ? t->safe_gain : NULL;

            if (sg != NULL) {
                result->gain_out.r = sg[0];
                result->gain_out.gr = sg[1];
                result->gain_out.gb = sg[2];
                result->gain_out.b = sg[3];
                result->color_temp_out = 6500;
            }
        }
        return -1;
    }
    return awb_isr(e, stats, result);
}

/* ------------------------------------------------------------------ */
/* Entry points.                                                      */
/* ------------------------------------------------------------------ */

awb_entity_t *awb_init(awb_ops_t *out_ops)
{
    const freeisp_tables_t *ft = freeisp_get_tables();
    const awb_clean_tables_t *t;
    awb_entity_t *e;
    int i;

    if (ft == NULL || ft->awb == NULL)
        return NULL;
    t = ft->awb;
    if (t->trust == NULL || t->speed_w == NULL || t->std_trust == NULL ||
        t->temp_bright == NULL)
        return NULL;

    e = (awb_entity_t *)calloc(1, sizeof(*e));
    if (e == NULL)
        return NULL;

    e->tab = t;
    e->total_windows = AWB_NWIN;
    e->frame_count = 0;
    for (i = 0; i < 4; i++) {
        e->gain_new[i] = 256;
        e->gain_target[i] = 256;
        e->gain_saved[i] = 256;
        e->high_temp_gain[i] = 256;
    }
    for (i = 0; i < AWB_NGHIST; i++) {
        int c;
        for (c = 0; c < 4; c++)
            e->gain_hist[i][c] = 256;
    }
    build_trust_lut(e);

    if (out_ops != NULL) {
        out_ops->get_params = awb_get_params;
        out_ops->set_params = awb_set_params;
        out_ops->run = awb_run;
        out_ops->isr = awb_isr;
    }
    return e;
}

void awb_exit(awb_entity_t *e)
{
    free(e);
}

int awb_get_params(awb_entity_t *e, awb_params_t **out)
{
    if (e == NULL)
        return -1;
    if (out != NULL)
        *out = &e->p;
    return 0;
}

int awb_set_params(awb_entity_t *e, const awb_param_req_t *req, int *result)
{
    if (e == NULL)
        return -1;
    if (req != NULL && req->kind == AWB_PARAM_INIT) {
        if (req->params != NULL)
            e->p = *req->params;
        build_reference_hierarchy(e);
        if (result != NULL)
            *result = 0;
        return 0;
    }
    return -1;
}

int awb_clean_is_night(const awb_entity_t *e)
{
    if (e == NULL)
        return -1;
    return e->is_night;
}

int awb_clean_color_temp(const awb_entity_t *e)
{
    if (e == NULL)
        return -1;
    return e->color_temp;
}

uint32_t awb_clean_frame_count(const awb_entity_t *e)
{
    if (e == NULL)
        return 0;
    return (uint32_t)e->frame_count;
}

int awb_clean_window_class(const awb_entity_t *e, int i)
{
    if (e == NULL || i < 0 || i >= e->total_windows)
        return 0;
    return e->win[i].cls;
}

int awb_clean_window_dist(const awb_entity_t *e, int i)
{
    if (e == NULL || i < 0 || i >= e->total_windows)
        return 0;
    return e->win[i].dist;
}

int awb_clean_window_temp(const awb_entity_t *e, int i)
{
    if (e == NULL || i < 0 || i >= e->total_windows)
        return 0;
    return e->win[i].color_temp;
}

int awb_clean_window_seg(const awb_entity_t *e, int i)
{
    if (e == NULL || i < 0 || i >= e->total_windows)
        return 0;
    return e->win[i].seg;
}

int awb_clean_window_level(const awb_entity_t *e, int i)
{
    if (e == NULL || i < 0 || i >= e->total_windows)
        return 0;
    return e->win[i].w_level;
}
