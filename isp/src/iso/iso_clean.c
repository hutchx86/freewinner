/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * iso_clean.c - clean-room ISO gain/luminance switchboard.
 *
 * Implements the behaviour specified in spec/iso.md.  The algorithm tuning
 * data arrives at runtime through freeisp_get_tables(); the AF IIR/FIR
 * coefficient banks in blk_af_cfg() are the only compiled-in tables in this
 * file.  See docs/provenance.md for the full provenance record (which lists
 * every compiled-in table in the library and how each was obtained).  Own
 * decomposition, own names.
 */
#include "iso_clean.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MINV(a, b)   ((a) < (b) ? (a) : (b))
#define CLRNEG(v)    ((v) < 0 ? 0 : (v))
#define S8(v)        ((int32_t)(int8_t)(v))
#define SAR(x, n)    ((x) >> (n))

static int32_t usat(int32_t v, int b)
{
    int32_t hi = (int32_t)((1u << b) - 1u);
    if (v < 0)
        return 0;
    return v > hi ? hi : v;
}

static int32_t iabs32(int32_t v)
{
    return v < 0 ? -v : v;
}

/* Signed 32-bit linear interpolation (spec 2). */
static int32_t interp(int32_t c, int32_t x0, int32_t x1, int32_t y0, int32_t y1)
{
    if (x1 == x0)
        return y0;
    return y0 + (int32_t)(((int32_t)(y1 - y0) * (int32_t)(c - x0)) /
                          (int32_t)(x1 - x0));
}

/* Spec 6.7.2: hue angle in degrees from centred chroma components. */
static int32_t hue_deg(int32_t u, int32_t v)
{
    double h = atan2(-((double)u - 128.0), (double)v - 128.0);

    if (h < 0.0)
        h += 6.2831853;
    return (int32_t)(h * 180.0 / 3.14159265);
}

/* Spec 6.7.3: saturation shaping on centred byte components. */
static void apply_saturation(int32_t p0, int32_t p1, int32_t sat,
                             int32_t *pa, int32_t *pb)
{
    int32_t v = p0 - 128;
    int32_t w = p1 - 128;
    int32_t a = S8(v);
    int32_t b = S8(w);

    if (sat < 1) {
        int32_t s = sat < -0x100 ? -0x100 : sat;
        a = a + S8((s * v) >> 8);
        b = b + S8((s * w) >> 8);
    } else if (iabs32(w) < iabs32(v)) {
        int32_t t = 0;
        int32_t n, d;

        if (v >= 4)
            t = (interp(v, 4, 0x7f, 0x100, 0) * sat) >> 8;
        else if (v < -3)
            t = (interp(v, -4, -0x80, 0x100, 0) * sat) >> 8;

        n = v + ((t * v) >> 8);
        if (n < 0x80) {
            if (n < -0x80) {
                n = -0x80;
                d = S8(-(w * p0) / v);
            } else {
                d = S8((t * w) >> 8);
            }
            a = S8(n);
            b = b + d;
        } else {
            a = 0x7f;
            b = b + S8((w * (0x7f - v)) / v);
        }
    } else {
        int32_t t = 0;
        int32_t n, d;

        if (w >= 4)
            t = (interp(w, 4, 0x7f, 0x100, 0) * sat) >> 8;
        else if (w < -3)
            t = (interp(w, -4, -0x80, 0x100, 0) * sat) >> 8;

        n = w + ((t * w) >> 8);
        if (n < 0x80) {
            if (n < -0x80) {
                b = -0x80;
                d = S8(-(p1 * v) / w);
            } else {
                b = S8(n);
                d = S8((t * v) >> 8);
            }
            a = a + d;
        } else {
            b = 0x7f;
            a = a + S8((v * (0x7f - w)) / w);
        }
    }

    *pa = a;
    *pb = b;
}

enum {
    CGD_GRAY = 0,
    CGD_BLEND = 1,
    CGD_SKIP = 2
};

/* Spec 6.7.3: which side of the chroma-gray window a hue angle falls on. */
static int colour_gray_decision(int32_t x, int32_t a, int32_t b, int32_t c,
                                int32_t d, int32_t *y0, int32_t *y1,
                                int32_t *xp)
{
    if (a < 0) {
        if ((a + 360 <= x) || (x <= b))
            return CGD_GRAY;
        if (b > 360)
            goto E;
        goto P;
    } else {
        if (b < 361) {
            if ((b < x) || (x < a))
                goto P;
        } else {
E:
            if ((x < a) && (b - 360 < x))
                goto P;
        }
        return CGD_GRAY;
    }

P:
    if (c < 0) {
        if ((c + 360 <= x) || (x <= d))
            goto ADJUST;
        if (d > 360)
            goto Q;
        goto ADJUST;
    } else {
        if (d < 361) {
            if ((d < x) || (x < c))
                return CGD_SKIP;
        } else {
Q:
            if ((x < c) && (d - 360 < x))
                return CGD_SKIP;
        }
    }
    goto ADJUST;

ADJUST:
    if (c < 0) {
        *y0 = a;
        *y1 = c;
        if (a > 0)
            *xp = (x > a) ? x - 360 : x;
        else
            *xp = x - 360;
    } else {
        if (d > 360) {
            if ((x >= c) || (x <= d - 360)) {
                *y0 = b;
                *y1 = d;
                *xp = (a >= 360 || x < b) ? x + 360 : x;
            } else {
                *y0 = a;
                *y1 = c;
                *xp = x;
            }
        } else {
            if (x >= b) {
                *y0 = b;
                *y1 = d;
            } else {
                *y0 = a;
                *y1 = c;
            }
            *xp = x;
        }
    }
    return CGD_BLEND;
}

/* ------------------------------------------------------------------ */
/* Instance state (spec 5.2, 5.3)                                     */
/* ------------------------------------------------------------------ */

struct iso_entity {
    iso_params_t             param;
    const iso_clean_tables_t *tab;
    int32_t                  lum_i;
    int32_t                  gain_i;
    iso_dyn_t                by_gain[ISO_CURVE_N];
    iso_dyn_t                by_lum[ISO_CURVE_N];
    int32_t                  gain_axis[ISO_BP_N];
    int32_t                  lum_axis[ISO_BP_N];
    int32_t                  busy_flag;
};

/*
 * Module-global temporal-denoise warm-up marker (spec 5.3, 6.13).  One word is
 * shared by every instance and holds the frame at which accumulation last
 * (re)started.  The deployed object ships it initialised to 1 and its init never
 * clears it, so the clean core must start at 1 too: a zero start
 * would let rec_en assert one frame earlier than the deployed warm-up window.
 */
static int32_t s_tdnr_start_frame = 1;

static const iso_dyn_t *pick(const iso_entity_t *e, int32_t trig)
{
    return (trig == ISO_TRIG_LUM) ? &e->by_lum[e->lum_i]
                                  : &e->by_gain[e->gain_i];
}

/* ------------------------------------------------------------------ */
/* Index computation (spec 6.1)                                       */
/* ------------------------------------------------------------------ */

static void compute_indices(iso_entity_t *e, iso_result_t *r)
{
    const iso_ctx_t *c = e->param.gen;
    int32_t mx = c->ae_pos_max;
    int32_t p = c->ae_pos;
    int32_t g;
    int32_t k;

    if (mx < p)
        p = mx;

    r->lum_i = interp(p, 0, mx, 0, ISO_SEARCH_HI);
    if (mx == 0)
        r->lum_i = 0;

    g = (int32_t)((c->total_gain * 100) >> 8);
    if (g > 0xc7fff)
        g = 0xc8000;

    r->gain_i = ISO_SEARCH_HI;
    for (k = ISO_SEARCH_LO; k != ISO_SEARCH_HI; k++) {
        if (g <= (int32_t)e->tab->gain_index_table[k]) {
            r->gain_i = k;
            break;
        }
    }

    e->lum_i = r->lum_i;
    e->gain_i = r->gain_i;
}

/* ------------------------------------------------------------------ */
/* Array construction (spec 6.2)                                      */
/* ------------------------------------------------------------------ */

static int32_t gain_table_index(const iso_entity_t *e, int32_t v)
{
    int32_t k;

    for (k = ISO_SEARCH_LO; k != ISO_SEARCH_HI; k++) {
        if (v <= (int32_t)e->tab->gain_index_table[k])
            return k;
    }
    return ISO_SEARCH_HI;
}

static void build_arrays(iso_entity_t *e)
{
    iso_params_t *p = &e->param;
    int bad_gain = 0;
    int bad_lum = 0;
    int i, x, j, w;

    for (i = 0; i < ISO_BP_N; i++) {
        if (p->gain_point[i] < 1)
            bad_gain = 1;
        if (p->lum_point[i] < 1)
            bad_lum = 1;
    }
    if (bad_gain)
        memcpy(p->gain_point, e->tab->gain_point_default,
               sizeof(p->gain_point));
    if (bad_lum)
        memcpy(p->lum_point, e->tab->lum_point_default,
               sizeof(p->lum_point));

    for (i = 0; i < ISO_BP_N; i++) {
        e->gain_axis[i] = gain_table_index(e, p->gain_point[i]);
        e->lum_axis[i] = p->lum_point[i];
    }

    /*
     * Gain-indexed array.  Spec 6.2.2 increments the segment index without a
     * cap; spec 10 flags the resulting one-record overrun.  We cap at the last
     * configured item (13) so the access stays defined, matching the luminance
     * builder's documented cap.
     */
    j = 0;
    for (x = 0; x < ISO_CURVE_N; x++) {
        int32_t lo, hi;
        const iso_dyn_t *prev, *next;

        if (j < ISO_BP_N - 1 && e->gain_axis[j] <= x)
            j++;

        if (j == 0) {
            lo = hi = e->gain_axis[0];
            prev = next = &p->cfg[0];
        } else {
            lo = e->gain_axis[j - 1];
            hi = e->gain_axis[j];
            prev = &p->cfg[j - 1];
            next = &p->cfg[j];
        }
        for (w = 0; w < ISO_DYN_WORDS; w++)
            e->by_gain[x].word[w] =
                interp(x, lo, hi, prev->word[w], next->word[w]);
    }

    /* Luminance-indexed array (spec 6.2.3). */
    j = 0;
    for (x = 0; x < ISO_CURVE_N; x++) {
        int32_t lo, hi;
        const iso_dyn_t *prev, *next;

        if (x < e->lum_axis[j]) {
            if (j == 0) {
                lo = hi = e->lum_axis[0];
                prev = next = &p->cfg[0];
            } else {
                goto segment;
            }
        } else {
            if (j < ISO_BP_N - 1)
                j++;
segment:
            lo = e->lum_axis[j - 1];
            hi = e->lum_axis[j];
            prev = &p->cfg[j - 1];
            next = &p->cfg[j];
        }
        for (w = 0; w < ISO_DYN_WORDS; w++)
            e->by_lum[x].word[w] =
                interp(x, lo, hi, prev->word[w], next->word[w]);
    }
}

/* ------------------------------------------------------------------ */
/* CNR / colour denoise (spec 6.3)                                    */
/* ------------------------------------------------------------------ */

static void blk_cnr(iso_entity_t *e, iso_result_t *r)
{
    const iso_ctx_t *c = e->param.gen;
    const iso_dyn_t *d = pick(e, c->trig_color_denoise);
    int32_t cc = d->f.color_denoise;
    int32_t m = CLRNEG(cc);
    int32_t a, cap, t, v;

    if (m > 0xffe)
        m = 0xfff;
    r->c_threshold = (uint16_t)m;

    a = m;
    if (a < 0x40)
        a = 0x40;
    a = (cc * a) >> 8;
    cap = m < 0x3ff ? m : 0x3ff;
    if (a < 0x20)
        a = 0x20;
    if (cap < a)
        a = cap;
    r->y_threshold = (uint16_t)a;

    t = cc * 0xc0;
    if (t < 0)
        t += 0x1ff;
    v = 0x100 - (t >> 9);
    if (v < 0x40)
        v = 0x40;
    if (v > 0xffe)
        v = 0xfff;
    r->st_v_yth = (uint16_t)v;

    t = (int32_t)((uint32_t)cc << 2);
    if (t < 0)
        t = 4 * cc + 0x1ff;
    v = 0x10 - (t >> 9);
    if (v < 0xc)
        v = 0xc;
    if (v > 0xffe)
        v = 0xfff;
    r->st_h_yth = (uint16_t)v;
}

/* ------------------------------------------------------------------ */
/* Contrast (spec 6.4)                                                */
/* ------------------------------------------------------------------ */

static void blk_contrast(iso_entity_t *e, iso_result_t *r)
{
    iso_ctx_t *c = e->param.gen;
    const iso_dyn_t *d = pick(e, c->trig_contrast);
    int32_t v = d->f.contrast + c->contrast_level;

    (void)r;

    if (v < -0x80)
        v = -0x80;
    if (v > 0x7f)
        v = 0x80;
    c->adjust.contrast = v;

    c->dynamic_enable = (c->contrast_enable != 0);
    if (c->dynamic_enable) {
        int32_t c0 = d->f.contrast_cfg[0];
        int32_t c1 = d->f.contrast_cfg[1];
        int32_t c2 = d->f.contrast_cfg[2];
        int32_t c3 = d->f.contrast_cfg[3];
        int32_t c4 = d->f.contrast_cfg[4];
        int32_t c5 = d->f.contrast_cfg[5];
        int32_t c6 = d->f.contrast_cfg[6];
        int32_t c7 = d->f.contrast_cfg[7];
        int32_t c8 = d->f.contrast_cfg[8];
        int32_t c9 = d->f.contrast_cfg[9];

        c->comp.tdnf_comp[0] = c0;
        c->comp.lp_th_ratio_comp[0] = c1;
        c->comp.sharp_hfrq_comp[0] = c2;
        c->comp.sharp_edge_comp[0] = c3;
        c->comp.sharp_under_shoot_comp[0] = c4;
        c->comp.tdnf_comp[3] = c5;
        c->comp.lp_th_ratio_comp[3] = c6;
        c->comp.sharp_hfrq_comp[3] = c7;
        c->comp.sharp_edge_comp[3] = c8;
        c->comp.sharp_under_shoot_comp[3] = c9;

        c->comp.tdnf_comp[1] = c0 / 2;
        c->comp.lp_th_ratio_comp[1] = c1 / 2;
        c->comp.sharp_hfrq_comp[1] = c2 / 2;
        c->comp.sharp_edge_comp[1] = c3 / 2;
        c->comp.sharp_under_shoot_comp[1] = c4 / 2;
        c->comp.tdnf_comp[2] = c5 / 2;
        c->comp.lp_th_ratio_comp[2] = c6 / 2;
        c->comp.sharp_hfrq_comp[2] = c7 / 2;
        c->comp.sharp_edge_comp[2] = c8 / 2;
        c->comp.sharp_under_shoot_comp[2] = c9 / 2;
    }
}

/* ------------------------------------------------------------------ */
/* Brightness (spec 6.5)                                              */
/* ------------------------------------------------------------------ */

static void blk_brightness(iso_entity_t *e, iso_result_t *r)
{
    iso_ctx_t *c = e->param.gen;
    const iso_dyn_t *d = pick(e, c->trig_brightness);
    int32_t v = d->f.brightness + c->brightness_level;

    (void)r;

    if (v < -0x80)
        v = -0x80;
    if (v > 0x7f)
        v = 0x80;
    c->adjust.brightness = v;
}

/* ------------------------------------------------------------------ */
/* Saturation (spec 6.6)                                              */
/* ------------------------------------------------------------------ */

static void blk_saturation(iso_entity_t *e, iso_result_t *r)
{
    iso_ctx_t *c = e->param.gen;
    const iso_dyn_t *d = pick(e, c->trig_saturation);
    int32_t cb, cr, den, base, hi, lo, k;

    r->satu_r = (int16_t)d->f.sat_cfg[0];
    r->satu_g = (int16_t)d->f.sat_cfg[1];
    r->satu_b = (int16_t)d->f.sat_cfg[2];
    r->saturation_mode = d->f.sat_cfg[3];

    cb = d->f.sat_cfg[4];
    cr = d->f.sat_cfg[5];
    den = d->f.sat_cfg[6];

    if (c->pltm_enable) {
        cb += (d->f.sat_cb * c->pltm_strength) >> 12;
        cr += (d->f.sat_cr * c->pltm_strength) >> 12;
    }

    base = c->sat_level;
    hi = base + cb;
    lo = base + cr;
    if (hi < -0x100)
        hi = -0x100;
    if (hi > 0x200)
        hi = 0x200;
    if (lo < -0x100)
        lo = -0x100;
    if (lo > 0x200)
        lo = 0x200;

    for (k = -den; k <= 0xff - den; k++) {
        double dd = pow(2.72, ((double)k * -10.0) / (double)den);
        int32_t val = (int32_t)((double)lo +
                                (1.0 / (dd + 1.0)) * (double)(hi - lo));
        r->sat_curve[k + den] = (int16_t)val;
    }

    c->table_update |= 0x20u;
}

/* ------------------------------------------------------------------ */
/* CEM chroma table and colour-to-gray (spec 6.7)                     */
/* ------------------------------------------------------------------ */

static void blk_cem(iso_entity_t *e, iso_result_t *r)
{
    iso_ctx_t *c = e->param.gen;
    const iso_dyn_t *d = pick(e, c->trig_cem_ratio);
    int32_t ratio = d->f.cem_ratio;
    int32_t span_min, span_max;
    int32_t inner_lo, inner_hi, outer_lo, outer_hi;
    int active;
    int i, g1, g2, g3;

    for (i = 0; i < ISO_CEM_TABLE_N; i++) {
        int32_t v = (((0x100 - ratio) * (int32_t)c->cem_table_b[i]) +
                     ratio * (int32_t)c->cem_table_a[i]) >> 8;
        r->out_cem[i] = (uint8_t)v;
    }

    active = (e->param.chroma_gray_span_max >=
              e->param.chroma_gray_span_min) &&
             (c->ae_mode == 1) &&
             (e->lum_i >= (int32_t)e->param.chroma_gray_th) &&
             (c->wb_enable != 0);

    if (!active) {
        span_min = 0x14;
        span_max = 0x1e;
        inner_lo = inner_hi = outer_lo = outer_hi = 0;
    } else {
        int32_t gb = (c->wb_gb_gain * 0xfff) >> 12;
        int32_t b = (c->wb_b_gain * 0xfff) >> 12;
        int32_t rr = (c->wb_r_gain * 0xfff) >> 12;
        int32_t yy = c->color_matrix.off[0] +
                     ((b * c->color_matrix.m[0][2] +
                       c->color_matrix.m[0][0] * rr +
                       gb * c->color_matrix.m[0][1]) >> 8);
        int32_t cb_ = c->color_matrix.off[1] +
                      ((yy * c->color_matrix.m[1][0] +
                        gb * c->color_matrix.m[1][1] +
                        b * c->color_matrix.m[1][2]) >> 8);
        int32_t cr_ = c->color_matrix.off[2] +
                      ((cb_ * c->color_matrix.m[2][1] +
                        yy * c->color_matrix.m[2][0] +
                        b * c->color_matrix.m[2][2]) >> 8);
        int32_t gy = c->gamma_table[yy >> 2] >> 4;
        int32_t gcb = c->gamma_table[(cb_ >> 2) + 0x400] >> 4;
        int32_t gcr = c->gamma_table[(cr_ >> 2) + 0x800] >> 4;
        int32_t uu = (c->rgb2yuv.off[1] >> 2) +
                     ((gcr * c->rgb2yuv.m[1][2] + gy * c->rgb2yuv.m[1][0] +
                       gcb * c->rgb2yuv.m[1][1]) >> 10);
        int32_t vv = (c->rgb2yuv.off[2] >> 2) +
                     ((gcr * c->rgb2yuv.m[2][2] + gy * c->rgb2yuv.m[2][0] +
                       gcb * c->rgb2yuv.m[2][1]) >> 10);
        int32_t h0 = hue_deg(uu, vv);

        span_min = e->param.chroma_gray_span_min;
        span_max = e->param.chroma_gray_span_max;
        inner_lo = h0 - span_min;
        inner_hi = h0 + span_min;
        outer_lo = h0 - span_max;
        outer_hi = h0 + span_max;
    }

    for (g3 = 0; g3 <= 8; g3++) {
        for (g2 = 0; g2 <= 16; g2++) {
            for (g1 = 0; g1 <= 16; g1++) {
                int bits = ((g3 & 1) << 2) + ((g2 & 1) * 2) + (g1 & 1);
                int plane = ((g3 >> 1) * 0x51) + ((g2 >> 1) * 9) +
                            (g1 >> 1);
                int base = bits > 3 ? bits + 0x65c : bits;
                int idx = base + plane * 4;

                if (c->sat_enable == 0) {
                    int32_t p0 = r->out_cem[2 * idx];
                    int32_t p1 = r->out_cem[2 * idx + 1];
                    int32_t sat = g3 < 2 ? d->f.sat_cfg[0]
                                : g3 < 4 ? d->f.sat_cfg[1]
                                : g3 < 7 ? d->f.sat_cfg[2]
                                         : d->f.sat_cfg[3];
                    int32_t a, b;

                    apply_saturation(p0, p1, sat, &a, &b);
                    r->out_cem[2 * idx] = (uint8_t)(a + 0x80);
                    r->out_cem[2 * idx + 1] = (uint8_t)(b + 0x80);
                }

                if (active) {
                    int32_t x = hue_deg(g1 << 4, g2 << 4);
                    int32_t y0 = 0, y1 = 0, xp = 0;
                    int dec = colour_gray_decision(x, inner_lo, inner_hi,
                                                   outer_lo, outer_hi,
                                                   &y0, &y1, &xp);

                    if (dec == CGD_GRAY) {
                        r->out_cem[2 * idx] = 0x80;
                        r->out_cem[2 * idx + 1] = 0x80;
                    } else if (dec == CGD_BLEND) {
                        int32_t q0 = r->out_cem[2 * idx] - 0x80;
                        int32_t q1 = r->out_cem[2 * idx + 1] - 0x80;
                        int32_t coeff = interp(xp, y0, y1, -0x100, 0);

                        q0 += S8((coeff * q0) >> 8);
                        q1 += S8((coeff * q1) >> 8);
                        r->out_cem[2 * idx] = (uint8_t)(q0 + 0x80);
                        r->out_cem[2 * idx + 1] = (uint8_t)(q1 + 0x80);
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* 2D denoise (spec 6.8)                                              */
/* ------------------------------------------------------------------ */

static void blk_denoise(iso_entity_t *e, iso_result_t *r)
{
    iso_ctx_t *c = e->param.gen;
    const iso_dyn_t *d = pick(e, c->trig_denoise);
    int32_t g1 = d->f.denoise_cfg[1];
    int32_t g3 = d->f.denoise_cfg[3];
    int32_t r_b, r_c, r_d;
    int k;

    if (c->pltm_enable) {
        g1 = (g1 * c->pltm_strength) >> 12;
        g3 = (g3 * c->pltm_strength) >> 12;
    }

    r->hf_ratio = (uint8_t)d->f.denoise_cfg[4];
    r->bf_ratio = (uint8_t)d->f.denoise_cfg[5];
    r->lf_ratio = (uint8_t)d->f.denoise_cfg[6];
    r->lp0_np_side_ratio = (uint8_t)d->f.denoise_cfg[7];
    r->lp1_np_side_ratio = (uint8_t)d->f.denoise_cfg[8];
    r->lp2_np_side_ratio = (uint8_t)d->f.denoise_cfg[9];

    r->lp0_np_core_ratio = (uint8_t)e->param.dn_core_ratio[0];
    r->lp1_np_core_ratio = (uint8_t)e->param.dn_core_ratio[1];
    r->lp2_np_core_ratio = (uint8_t)e->param.dn_core_ratio[2];
    r->lp3_np_core_ratio = (uint8_t)e->param.dn_core_ratio[3];

    r->lp0_pcnt_ratio = (uint8_t)d->f.denoise_cfg[0xe];
    r->lp1_pcnt_ratio = (uint8_t)d->f.denoise_cfg[0xf];
    r->lp2_pcnt_ratio = (uint8_t)d->f.denoise_cfg[0x10];
    r->lp3_pcnt_ratio = (uint8_t)d->f.denoise_cfg[0x11];

    r_b = d->f.denoise_cfg[0xb];
    r_c = d->f.denoise_cfg[0xc];
    r_d = d->f.denoise_cfg[0xd];
    if (c->dynamic_enable)
        r_c += (c->lp_th_ratio_comp_target * r_c) >> 8;

    for (k = 0; k <= 32; k++) {
        int32_t v1 = interp(k, 0, 0x20, d->f.denoise_cfg[0],
                            d->f.denoise_cfg[2]);
        int32_t v2 = interp(k, 0, 0x20, g1, g3);
        int32_t t = ((c->denoise_level * v1) / 0x32) *
                    (int32_t)c->bdnf_th[k];
        int32_t u, p;

        if (t < 0)
            t += 0xff;
        u = CLRNEG(v2 + (t >> 8));
        if (u > 0xffe)
            u = 0xfff;
        r->d2d_lp0_th[k] = (uint16_t)u;

        if (u < 1) {
            r->d2d_lp1_th[k] = 0;
            r->d2d_lp2_th[k] = 0;
            r->d2d_lp3_th[k] = 0;
        } else {
            p = (u * r_b) >> 8;
            p = p < 1 ? 1 : (p > 0xffe ? 0xfff : p);
            r->d2d_lp1_th[k] = (uint16_t)p;
            p = (r_c * u) >> 8;
            p = p < 1 ? 1 : (p > 0xffe ? 0xfff : p);
            r->d2d_lp2_th[k] = (uint16_t)p;
            p = (u * r_d) >> 8;
            p = p < 1 ? 1 : (p > 0xffe ? 0xfff : p);
            r->d2d_lp3_th[k] = (uint16_t)p;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Sensor offset (spec 6.9)                                           */
/* ------------------------------------------------------------------ */

static void blk_sensor_offset(iso_entity_t *e, iso_result_t *r)
{
    iso_ctx_t *c = e->param.gen;
    const iso_dyn_t *d = pick(e, c->trig_sensor_offset);
    int i;

    (void)r;
    for (i = 0; i < 4; i++) {
        int16_t s = (int16_t)d->f.sensor_offset[i];

        c->module.sensor_offset[i] = s;
        c->sensor.gain_offset[i] = s;
    }
}

/* ------------------------------------------------------------------ */
/* Black level (spec 6.10)                                            */
/* ------------------------------------------------------------------ */

static void blk_black_level(iso_entity_t *e, iso_result_t *r)
{
    iso_ctx_t *c = e->param.gen;
    const iso_dyn_t *d = pick(e, c->trig_black_level);

    (void)r;
    c->module.offset[0] = (int16_t)d->f.black_level[0];
    c->module.offset[1] = (int16_t)d->f.black_level[1];
    c->module.offset[2] = (int16_t)d->f.black_level[2];
    c->module.offset[3] = (int16_t)d->f.black_level[3];
}

/* ------------------------------------------------------------------ */
/* Defect-pixel correction (spec 6.11)                                */
/* ------------------------------------------------------------------ */

static void blk_dpc(iso_entity_t *e, iso_result_t *r)
{
    const iso_ctx_t *c = e->param.gen;
    const iso_dyn_t *d = pick(e, c->trig_dpc);

    r->hot_ratio = (uint8_t)d->f.dpc_cfg[0];
    r->cold_ratio = (uint8_t)d->f.dpc_cfg[1];
    r->nbhd_diff_ratio = (uint8_t)d->f.dpc_cfg[2];
    r->nearest_diff_ratio = (uint8_t)d->f.dpc_cfg[3];
    r->slope_th = (uint16_t)d->f.dpc_cfg[4];
    r->cold_abs_th = (uint16_t)d->f.dpc_cfg[5];
}

/* ------------------------------------------------------------------ */
/* Defog / PLTM / AE / GTM / LCA (spec 6.12)                          */
/* ------------------------------------------------------------------ */

static void blk_defog(iso_entity_t *e, iso_result_t *r)
{
    iso_ctx_t *c = e->param.gen;
    const iso_dyn_t *d = pick(e, c->trig_defog);

    (void)r;
    c->adjust.defog_value = d->f.defog_value;
}

static void blk_pltm_dynamic(iso_entity_t *e, iso_result_t *r)
{
    iso_ctx_t *c = e->param.gen;
    const iso_dyn_t *d = pick(e, c->trig_pltm_dynamic);

    (void)r;
    c->ae.pltm_dynamic_cfg[0] =
        usat(c->pltm_level + d->f.pltm_dynamic_cfg[0], 8);
    c->ae.pltm_dynamic_cfg[1] =
        usat(c->pltm_level + d->f.pltm_dynamic_cfg[1], 12);
    c->ae.pltm_dynamic_cfg[2] = d->f.pltm_dynamic_cfg[2];
    c->ae.pltm_dynamic_cfg[3] = d->f.pltm_dynamic_cfg[3];
}

static void blk_ae_cfg(iso_entity_t *e, iso_result_t *r)
{
    iso_ctx_t *c = e->param.gen;
    const iso_dyn_t *d = pick(e, c->trig_ae_cfg);
    int32_t v;
    int k;

    (void)r;

    v = d->f.ae_cfg[0];
    if (c->highlight_level < 0) {
        v = v * (c->highlight_level + 0x20);
        if (v < 0)
            v += 0x1f;
        v >>= 5;
    } else {
        v = c->highlight_level * v + v;
    }
    c->ae.exposure_cfg[0] = (uint32_t)v;

    v = d->f.ae_cfg[1];
    if (c->backlight_level < 0) {
        v = v * (c->backlight_level + 0x20);
        if (v < 0)
            v += 0x1f;
        v >>= 5;
    } else {
        v = c->backlight_level * v + v;
    }
    c->ae.exposure_cfg[1] = (uint32_t)v;

    for (k = 2; k <= 13; k++)
        c->ae.exposure_cfg[k] = d->f.ae_cfg[k];
}

static void blk_gtm_cfg(iso_entity_t *e, iso_result_t *r)
{
    iso_ctx_t *c = e->param.gen;
    const iso_dyn_t *d = pick(e, c->trig_ae_cfg);
    int k;

    (void)r;
    for (k = 0; k <= 8; k++)
        c->ae.ae_hist_eq_cfg[k] = d->f.gtm_cfg[k];
}

static void blk_lca_cfg(iso_entity_t *e, iso_result_t *r)
{
    const iso_ctx_t *c = e->param.gen;
    const iso_dyn_t *d = pick(e, c->trig_lca_cfg);

    r->lca_gf_cor_ratio = (uint16_t)d->f.lca_cfg[0];
    r->lca_pf_cor_ratio = (uint16_t)d->f.lca_cfg[1];
    r->lca_lum_th = (uint16_t)d->f.lca_cfg[2];
    r->lca_grad_th = (uint16_t)d->f.lca_cfg[3];
    r->lca_clr_gth = (uint16_t)d->f.lca_cfg[4];
    r->lca_pf_rshf = (uint16_t)d->f.lca_cfg[5];
    r->lca_pf_bslp = (uint16_t)d->f.lca_cfg[6];
    r->lca_clrs_lum_th = (uint8_t)d->f.lca_cfg[7];
    r->lca_pf_clrc_ratio = (uint8_t)d->f.lca_cfg[8];
    r->lca_gf_clrc_ratio = (uint8_t)d->f.lca_cfg[9];
    r->lca_pf_decr_ratio = (uint8_t)d->f.lca_cfg[10];
}

static void blk_af_cfg(iso_entity_t *e, iso_result_t *r)
{
    const iso_ctx_t *c = e->param.gen;
    iso_af_out_t *a = &r->af;
    static const int32_t iir_g[6] = { 0x01e0, -233, 0x019a, -206, 0x01a5,
                                      -186 };
    static const int32_t fir_g[5] = { 0x14, 0x10, 0x00, -16, -20 };
    const iso_dyn_t *so = pick(e, c->trig_sensor_offset);
    int i;

    /* The offsets come from the module config's sensor-offset block, not from
     * the shared context state the per-frame sensor-offset block fills. */
    a->r_offset = (int16_t)so->f.sensor_offset[0];
    a->g_offset = (int16_t)((so->f.sensor_offset[1] +
                             so->f.sensor_offset[2]) >> 1);
    a->b_offset = (int16_t)so->f.sensor_offset[3];

    a->mode = 0;
    a->iir0_en = 1;
    a->fir0_en = 1;
    a->iir0_sec0_en = 1;
    a->iir0_sec1_en = 1;
    a->iir0_sec2_en = 1;
    a->iir0_ldg_en = 1;
    a->fir0_ldg_en = 1;
    a->offset_en = 1;
    a->peak_en = 1;
    a->iir_ds_en = 0;
    a->fir_ds_en = 0;
    a->squ_en = 0;

    for (i = 0; i < 6; i++)
        a->iir_g[i] = iir_g[i];
    if (e->tab->af_iir_s != NULL)
        for (i = 0; i < 4; i++)
            a->iir_s[i] = e->tab->af_iir_s[i];
    else    /* not supplied (the rmm feed never has it): AF IIR feedback off */
        for (i = 0; i < 4; i++)
            a->iir_s[i] = 0;
    for (i = 0; i < 5; i++)
        a->fir_g[i] = fir_g[i];

    a->iir0_dilate = 2;

    a->iir_ldg_lgain = a->fir_ldg_lgain = 8;
    a->iir_ldg_hgain = a->fir_ldg_hgain = 8;
    a->iir_ldg_lth = a->fir_ldg_lth = 4;
    a->iir_ldg_hth = a->fir_ldg_hth = 0xc8;
    a->iir_ldg_lslope = a->fir_ldg_lslope = 0x0a;
    a->iir_ldg_hslope = a->fir_ldg_hslope = 0x0e;
    a->iir_core_th = a->fir_core_th = 2;
    a->iir_core_peak = a->fir_core_peak = 0xff;
    a->iir_core_slope = a->fir_core_slope = 5;
    a->hlt_th = 0xeb;

    memcpy(r->af_square_lut, e->tab->af_square_table,
           sizeof(r->af_square_lut));
}

/* ------------------------------------------------------------------ */
/* Sharpness (spec 6.14)                                              */
/* ------------------------------------------------------------------ */

static void blk_sharp(iso_entity_t *e, iso_result_t *r)
{
    iso_ctx_t *c = e->param.gen;
    const iso_dyn_t *d = pick(e, c->trig_sharp);
    const int32_t *s = d->f.sharp_cfg;
    int32_t w_area, b_area, w_frq, b_frq, und_a, und_v, L;
    int k;

    r->edge_scale_ratio = (uint8_t)s[10];
    r->hfrq_scale_ratio = (uint8_t)s[11];
    r->edge_conv_para = (uint8_t)s[12];
    r->hfrq_conv_para = (uint8_t)s[13];
    r->dir_eq_ratio = (uint16_t)s[14];
    r->dir_clip_val = (uint16_t)s[15];
    r->ns_lw_th = (uint16_t)s[16];
    r->ns_hi_th = (uint16_t)s[17];
    r->edge_th = (uint8_t)s[18];
    r->hv_edge_sm_ratio = (uint8_t)s[19];
    r->aa_edge_sm_ratio = (uint8_t)s[20];

    w_area = s[0x15];
    b_area = s[0x16];
    w_frq = s[0x17];
    b_frq = s[0x18];
    und_a = s[0x1a];
    und_v = s[0x1c];

    if (c->dynamic_enable) {
        int32_t t = c->sharp_hfrq_comp_target;
        int32_t t2;

        w_frq += (t * w_frq) >> 8;
        b_frq += (t * b_frq) >> 8;
        t = c->sharp_edge_comp_target;
        t2 = c->sharp_under_shoot_comp_target;
        w_area += (t * w_area) >> 8;
        b_area += (t * b_area) >> 8;
        und_a += (t2 * und_a) >> 8;
        und_v += (t2 * und_v) >> 8;
    }

    L = CLRNEG(c->sharp_level);
    if (L > 999)
        L = 1000;
    c->tune.sharpness_level = L;

    r->edge_white_stren = MINV(0xfff, (w_area * L) / 100);
    r->edge_black_stren = MINV(0xfff, (b_area * L) / 100);
    r->hfrq_white_stren = MINV(0xfff, (w_frq * L) / 100);
    r->hfrq_black_stren = MINV(0xfff, (b_frq * L) / 100);
    r->under_area_ctrl = (uint16_t)und_a;
    r->over_area_ctrl = (uint16_t)s[0x19];
    r->under_val_ctrl = (uint16_t)und_v;
    r->over_val_ctrl = (uint16_t)s[0x1b];

    memcpy(r->sharp_edge_lum, c->sharp_edge_lum, sizeof(r->sharp_edge_lum));
    memcpy(r->sharp_hfrq_lum, c->sharp_hfrq_lum, sizeof(r->sharp_hfrq_lum));
    memcpy(r->sharp_hsv, c->sharp_hsv, sizeof(r->sharp_hsv));
    memcpy(r->sharp_s_map, c->sharp_s_map, sizeof(r->sharp_s_map));

    for (k = 0; k <= 32; k++) {
        int32_t v38 = interp(k, 0, 0x20, s[6], s[8]);
        int32_t v32 = interp(k, 0, 0x20, s[7], s[9]);
        int32_t src[2];
        int dst[2];
        int q;

        src[0] = c->sharp_val[k];
        src[1] = c->sharp_val[0x21 + k];
        dst[0] = k;
        dst[1] = 0x21 + k;

        for (q = 0; q < 2; q++) {
            int32_t t = v38 * src[q];
            int32_t u;

            if (t < 0)
                t += 0xff;
            u = CLRNEG(v32 + (t >> 8));
            if (u > 0xffe)
                u = 0xfff;
            r->sharp_val[dst[q]] = (uint16_t)u;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Temporal (3D) denoise (spec 6.13)                                  */
/* ------------------------------------------------------------------ */

static void blk_tdnr(iso_entity_t *e, iso_result_t *r)
{
    iso_ctx_t *c = e->param.gen;
    const iso_dyn_t *d = pick(e, c->trig_tdf);
    iso_tdf_out_t *o = &r->tdf;
    int32_t local_diff[256];
    int32_t p4, p5, p6, p7, p8, p9, p10, p11;
    int32_t limit;
    int i;

    if (c->tdf_enable == 0) {
        o->rec_en = 0;
        s_tdnr_start_frame = e->param.frame_id + 1;
    } else {
        int32_t enable = c->module_enable_flag;

        if (d->f.tdf_cfg[4] == 0 && d->f.tdf_cfg[6] == 0) {
            c->module_enable_flag = enable & ~0x20;
            s_tdnr_start_frame = e->param.frame_id + 1;
        } else {
            c->module_enable_flag = enable | 0x20;
        }
        if ((c->module_enable_flag & 0x20) == 0 ||
            e->param.frame_id <= s_tdnr_start_frame)
            o->rec_en = 0;
        else
            o->rec_en = 1;
    }

    p4 = d->f.tdf_cfg[4];
    p5 = d->f.tdf_cfg[5];
    p6 = d->f.tdf_cfg[6];
    p7 = d->f.tdf_cfg[7];
    p8 = d->f.tdf_cfg[8];
    p9 = d->f.tdf_cfg[9];
    p10 = d->f.tdf_cfg[10];
    p11 = d->f.tdf_cfg[0xb];
    if (c->pltm_enable) {
        int32_t s = c->pltm_strength;

        p7 = (p7 * s) >> 12;
        p5 = (p5 * s) >> 12;
        p9 = (s * p9) >> 12;
        p11 = (p11 * s) >> 12;
    }

    for (i = 0; i < 256; i++)
        o->tdnf_table[i] = (uint16_t)c->k3d_incre_curve[i];
    for (i = 0; i < 256; i++)
        local_diff[i] = c->tdnf_diff[i];

    limit = CLRNEG(d->f.tdf_cfg[0x14]);
    if (c->dynamic_enable) {
        int32_t cc = c->tdnf_comp_target;

        limit += (c->tdnf_diff_comp_target * limit) >> 8;
        p4 += (p4 * cc) >> 8;
        p6 += (p6 * cc) >> 8;
        p8 += (p8 * cc) >> 8;
        p10 += (p10 * cc) >> 8;
    }

    for (i = 0; i < 256; i++)
        if (local_diff[i] > limit)
            local_diff[i] = limit;
    for (i = 0; i < 256; i++)
        o->tdnf_table[0x100 + i] = (uint16_t)local_diff[i];

    o->noise_clip_ratio = (uint16_t)d->f.tdf_cfg[0];
    o->lum_diff_clip_ratio = (uint8_t)d->f.tdf_cfg[0xc];
    o->bright_diff_clip_ratio = (uint8_t)d->f.tdf_cfg[0xd];
    o->bright_diff_ratio = (uint8_t)d->f.tdf_cfg[0xe];
    o->mv_ori_ratio = (uint8_t)d->f.tdf_cfg[0xf];
    o->st_2d_ratio = (uint8_t)d->f.tdf_cfg[0x10];
    o->c_weight1 = (uint16_t)d->f.tdf_cfg[0x11];
    o->c_weight2 = (uint16_t)d->f.tdf_cfg[0x12];
    o->c_weight3 = (uint16_t)d->f.tdf_cfg[0x13];
    o->ltf_update_frm = 0x19;
    o->ltf_en = (uint8_t)d->f.tdf_cfg[0x15];

    for (i = 0; i < 32; i++)
        o->tdnf_k_delta[i] = (uint8_t)(0x1f - i);

    for (i = 0; i <= 32; i++) {
        int32_t v3 = interp(i, 0, 0x20, p4, p6);
        int32_t v4 = interp(i, 0, 0x20, p5, p7);
        int32_t v5 = interp(i, 0, 0x20, p8, p10);
        int32_t v6 = interp(i, 0, 0x20, p9, p11);
        int32_t t, u;

        t = ((c->tdf_level * v3) / 0x32) * (int32_t)c->tdnf_th[i];
        if (t < 0)
            t += 0xff;
        u = CLRNEG(v4 + (t >> 8));
        if (u > 0xffe)
            u = 0xfff;
        o->tdnf_th[i] = (uint16_t)u;

        t = v5 * (int32_t)c->tdnf_th[0x21 + i];
        if (t < 0)
            t += 0xff;
        u = CLRNEG(v6 + (t >> 8));
        if (u > 0xffe)
            u = 0xfff;
        o->tdnf_th[0x21 + i] = (uint16_t)u;
    }

    for (i = 0; i < 32; i++)
        o->tdnf_k[i] = (uint16_t)c->tdnf_k[i];
}

/* ------------------------------------------------------------------ */
/* Per-frame entry point (spec 8 ordering)                            */
/* ------------------------------------------------------------------ */

int iso_run(iso_entity_t *e, iso_result_t *result)
{
    const iso_params_t *p;

    if (!e || !result)
        return -1;

    p = &e->param;
    compute_indices(e, result);

    if (p->cnr_on)
        blk_cnr(e, result);
    if (p->sharp_on)
        blk_sharp(e, result);
    if (p->sat_on)
        blk_saturation(e, result);
    if (p->contrast_on)
        blk_contrast(e, result);
    if (p->brightness_on)
        blk_brightness(e, result);
    if (p->cem_on)
        blk_cem(e, result);
    if (p->denoise_on)
        blk_denoise(e, result);
    if (p->sensor_offset_on)
        blk_sensor_offset(e, result);
    if (p->black_level_on)
        blk_black_level(e, result);
    if (p->dpc_on)
        blk_dpc(e, result);
    if (p->defog_on)
        blk_defog(e, result);
    if (p->pltm_dyn_on)
        blk_pltm_dynamic(e, result);
    if (p->tdnr_on)
        blk_tdnr(e, result);
    if (p->ae_cfg_on)
        blk_ae_cfg(e, result);
    if (p->gtm_cfg_on)
        blk_gtm_cfg(e, result);
    if (p->lca_cfg_on)
        blk_lca_cfg(e, result);
    if (p->af_cfg_on)
        blk_af_cfg(e, result);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Lifecycle and parameter entry points (spec 4, 5.4)                 */
/* ------------------------------------------------------------------ */

static int ops_get(iso_entity_t *e, iso_params_t **out)
{
    return iso_get_params(e, out);
}

static int ops_set(iso_entity_t *e, const iso_params_t *in,
                   iso_result_t *result)
{
    return iso_set_params(e, in, result);
}

static int ops_run(iso_entity_t *e, iso_result_t *result)
{
    return iso_run(e, result);
}

iso_entity_t *iso_init(iso_ops_t *out_ops)
{
    iso_entity_t *e;
    const freeisp_tables_t *t = freeisp_get_tables();

    e = (iso_entity_t *)calloc(1, sizeof(*e));
    if (!e)
        return NULL;

    e->busy_flag = 1;
    e->lum_i = 0x20;
    e->gain_i = 0x20;
    e->tab = t->iso;

    if (out_ops) {
        out_ops->get_params = ops_get;
        out_ops->set_params = ops_set;
        out_ops->run = ops_run;
    }
    return e;
}

void iso_exit(iso_entity_t *e)
{
    free(e);
}

int iso_get_params(iso_entity_t *e, iso_params_t **out)
{
    if (!e || !out)
        return -1;
    *out = &e->param;
    return 0;
}

int iso_set_params(iso_entity_t *e, const iso_params_t *in,
                   iso_result_t *result)
{
    (void)result;

    if (!e || !in)
        return -1;
    if (in->param_kind != ISO_PARAM_REBUILD)
        return -1;

    e->param = *in;
    build_arrays(e);
    return 0;
}

/*
 * Override the frame counter the temporal-denoise warm-up window is measured
 * against.  The framework mutates the frame id in its stored parameter block in
 * place every frame without re-entering set-parameters; the shim forwards the
 * live value here before each run.
 */
void iso_set_frame_id(iso_entity_t *e, int32_t frame_id)
{
    if (e != NULL)
        e->param.frame_id = frame_id;
}

/*
 * Refresh the per-frame inputs the framework rewrites in its stored parameter
 * block in place without re-entering set-parameters: the per-block enable gates
 * it raises *after* the install call, the 2D-denoise core ratios, the frame
 * counter and the colour-to-gray window.  Everything else -- the interpolation
 * arrays, the configured breakpoints and the shared context pointer -- is left
 * as built, because set-parameters is re-entered only when that data changes.
 * The boundary shim calls this before each run so the core reads the live
 * mirror exactly as the deployed core does (spec 11).
 */
void iso_set_live_params(iso_entity_t *e, const iso_params_t *in)
{
    iso_params_t *dst;
    int i;

    if (e == NULL || in == NULL)
        return;

    dst = &e->param;

    dst->frame_id             = in->frame_id;
    dst->chroma_gray_th       = in->chroma_gray_th;
    dst->chroma_gray_span_min = in->chroma_gray_span_min;
    dst->chroma_gray_span_max = in->chroma_gray_span_max;

    /* The seventeen per-block gates, listed in parameter-block order. */
#define ISO_COPY_GATE(field) (dst->field = in->field)
    ISO_COPY_GATE(cnr_on);
    ISO_COPY_GATE(sharp_on);
    ISO_COPY_GATE(sat_on);
    ISO_COPY_GATE(contrast_on);
    ISO_COPY_GATE(brightness_on);
    ISO_COPY_GATE(cem_on);
    ISO_COPY_GATE(denoise_on);
    ISO_COPY_GATE(sensor_offset_on);
    ISO_COPY_GATE(black_level_on);
    ISO_COPY_GATE(dpc_on);
    ISO_COPY_GATE(defog_on);
    ISO_COPY_GATE(pltm_dyn_on);
    ISO_COPY_GATE(tdnr_on);
    ISO_COPY_GATE(ae_cfg_on);
    ISO_COPY_GATE(gtm_cfg_on);
    ISO_COPY_GATE(lca_cfg_on);
    ISO_COPY_GATE(af_cfg_on);
#undef ISO_COPY_GATE

    for (i = 0; i < 4; i++)
        dst->dn_core_ratio[i] = in->dn_core_ratio[i];
}

/* ------------------------------------------------------------------ */
/* Test diagnostics                                                    */
/* ------------------------------------------------------------------ */

int32_t iso_debug_gain_word(const iso_entity_t *e, int index, int word)
{
    if (!e || index < 0 || index >= ISO_CURVE_N ||
        word < 0 || word >= ISO_DYN_WORDS)
        return 0;
    return e->by_gain[index].word[word];
}

int32_t iso_debug_lum_word(const iso_entity_t *e, int index, int word)
{
    if (!e || index < 0 || index >= ISO_CURVE_N ||
        word < 0 || word >= ISO_DYN_WORDS)
        return 0;
    return e->by_lum[index].word[word];
}

void iso_debug_sat_pick(const iso_entity_t *e, int32_t out[7])
{
    const iso_dyn_t *d;
    int i;

    if (!e || !out || !e->param.gen)
        return;
    d = pick(e, e->param.gen->trig_saturation);
    for (i = 0; i < 7; i++)
        out[i] = d->f.sat_cfg[i];
}
