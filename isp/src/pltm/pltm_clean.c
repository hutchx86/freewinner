/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * pltm_clean.c - clean-room local tone-mapping module.
 *
 * Implements the behaviour specified in spec/pltm.md.  All tables, including
 * the 19-step preset bank, are injected at runtime through
 * freeisp_get_tables(); this file compiles in no table data.  See
 * docs/provenance.md for the full provenance record (which lists every
 * compiled-in table in the library and how each was obtained).  Sections below
 * mirror the spec's numbered stages.
 */
#include "pltm_clean.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PLTM_MIN(a, b)        ((a) < (b) ? (a) : (b))
#define PLTM_MAX(a, b)        ((a) > (b) ? (a) : (b))
#define PLTM_CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

/* ------------------------------------------------------------------ */
/* Internal state (spec 5).                                            */
/* ------------------------------------------------------------------ */

struct pltm_entity {
    pltm_clean_params_t       p;                /* live configuration       */
    const pltm_clean_tables_t *tab;

    int busy_flag;                              /* 1 at allocation          */
    int interval_frame;                         /* defaulted interval       */

    int32_t strength_curve[PLTM_STRENGTH_COLS]; /* 7.3 working curve        */
    int32_t min_change_cnt;                     /* 7.5 confirmation counter */

    int sensor_w_old;                           /* saved geometry           */
    int sensor_h_old;
    int block_cols_old;
    int block_rows_old;

    /* Persistent output state (spec 5/6). */
    uint16_t tbl[PLTM_NTBL];
    int      oripic_ratio_out;
    int      order_out;
    int      last_order_ratio_out;
    int      cal_en;
    int      frame_smooth_en;
    int      block_width_out;
    int      block_height_out;
    uint32_t stat_scale;
    uint8_t  ae_comp_out;
    uint16_t old_strength;
    uint16_t next_strength;
    uint16_t cal_strength;
    uint16_t min_threshold_out;
};

/* Module-global first-frame gate; initial value 3 (spec 5, 10). */
static int g_start_frame = 3;

/* ------------------------------------------------------------------ */
/* Small helpers (spec 2).                                             */
/* ------------------------------------------------------------------ */

static int32_t sat8(int32_t x)  { return PLTM_CLAMP(x, 0, 0xFF); }
static int32_t sat12(int32_t x) { return PLTM_CLAMP(x, 0, 0xFFF); }
static int32_t floor0(int32_t x) { return x & ~(x >> 31); }
static int32_t iabs32(int32_t x) { return x < 0 ? -x : x; }

static int32_t asr(int32_t v, int s)
{
    if (s <= 0)
        return v;
    if (s > 31)
        return v >> 31;
    return v >> s;
}

static uint32_t shr_u32(uint32_t v, int s)
{
    if (s <= 0)
        return v;
    if (s >= 32)
        return 0u;
    return v >> s;
}

static int shl_one(int s)
{
    if (s <= 0)
        return 1;
    if (s > 30)
        return 0;
    return (int)(1u << s);
}

/* ------------------------------------------------------------------ */
/* Preset table (spec 7.8): 19 quantised parameter sets, injected.     */
/* ------------------------------------------------------------------ */

/* Row used for every step when no preset table was injected: full original
 * blend, lowest order, no clip, unity gain -- local tone mapping has no
 * visible effect. */
static const int32_t k_neutral_preset[PLTM_PRESET_COLS] = {
    0xFF, 5, 0, PLTM_Q12_UNITY
};

/* presets: [PLTM_NPRESET][PLTM_PRESET_COLS] or NULL (neutral). */
static void preset_get(const int32_t *presets, int idx,
                       int *o, int *t, int *c, int *g)
{
    const int32_t *row;

    if (idx < 0)
        idx = 0;
    if (idx >= PLTM_NPRESET)
        idx = PLTM_NPRESET - 1;
    row = presets ? presets + idx * PLTM_PRESET_COLS : k_neutral_preset;
    if (o)
        *o = row[PLTM_PRESET_BLEND];
    if (t)
        *t = row[PLTM_PRESET_ORDER];
    if (c)
        *c = row[PLTM_PRESET_CLIP];
    if (g)
        *g = row[PLTM_PRESET_GAIN];
}

/* The provider's presets, for the entity-less query entry points. */
static const int32_t *provider_presets(void)
{
    const freeisp_tables_t *ft = freeisp_get_tables();

    return (ft && ft->pltm) ? ft->pltm->presets : NULL;
}

/* ------------------------------------------------------------------ */
/* Merge weighting table (spec 8.1).                                   */
/* ------------------------------------------------------------------ */

static double gauss_term(double i2, int32_t eta)
{
    if (eta == 0)
        return 0.0;
    return exp(-i2 / (2.0 * (double)eta));
}

/* Build one symmetric 3-tap kernel from a gaussian span n and pack the two
 * stored weights of each triple.  `count` entries are emitted.  The span and
 * the emitted count are independent: the geometry path emits
 * (block_width>>1)+2 entries from a span of block_width+1, while the direct
 * entry point emits (n>>1)+2 entries from a span of n. */
static void merge_axis_emit(uint16_t *dst, int n, int count)
{
    int half, top, i;
    int32_t eta1, eta2;
    int32_t *g1, *g2, *t, *u;

    for (i = 0; i < PLTM_MERGE_HALF; i++)
        dst[i] = 0;

    if (n <= 0)
        return;

    half = (n >> 1) + 1;
    top = n + half;
    eta1 = (15 * n) >> 5;
    eta1 = eta1 * eta1;
    eta2 = (18 * n) >> 5;
    eta2 = eta2 * eta2;

    g1 = (int32_t *)malloc((size_t)(top + 1) * sizeof(*g1));
    g2 = (int32_t *)malloc((size_t)(top + 1) * sizeof(*g2));
    t  = (int32_t *)malloc((size_t)(top + 1) * sizeof(*t));
    u  = (int32_t *)malloc((size_t)(top + 1) * sizeof(*u));
    if (!g1 || !g2 || !t || !u) {
        free(g1);
        free(g2);
        free(t);
        free(u);
        return;
    }

    t[0] = 0;

    for (i = 0; i <= top; i++) {
        double i2 = (double)i * (double)i;
        int32_t s, b;
        g1[i] = (int32_t)(0.45 + 256.0 * gauss_term(i2, eta1));
        g2[i] = (int32_t)(0.45 + 256.0 * gauss_term(i2, eta2));
        s = (i * i) >> 8;
        b = (g1[i] * (256 - s) + g2[i] * s) >> 8;
        t[i] = (i == 0) ? b : PLTM_MIN(b, t[i - 1]);
    }

    u[0] = t[0];
    for (i = 1; i < top; i++)
        u[i] = (t[i - 1] + t[i] + t[i + 1]) / 3;
    if (top >= 1)
        u[top] = (t[top - 1] + t[top]) >> 1;

    if (count > PLTM_MERGE_HALF)
        count = PLTM_MERGE_HALF;
    if (count < 0)
        count = 0;
    for (i = 0; i < count; i++) {
        int a = i + n;
        int b = (i >= n) ? (i - n) : (n - i);
        int64_t sum = (int64_t)u[a] + u[i] + u[b];
        int32_t r0 = 0, r1 = 0;
        if (sum > 0) {
            r0 = sat8((int32_t)(((int64_t)u[a] << 8) / sum));
            r1 = sat8((int32_t)(((int64_t)u[i] << 8) / sum));
        }
        dst[i] = (uint16_t)(r0 | (r1 << 8));
    }

    free(g1);
    free(g2);
    free(t);
    free(u);
}

/* Fill both merge halves for tile spans nw/nh (tile count = block length + 1). */
static void merge_table_fill(uint16_t *tbl, int nw, int nh)
{
    /* nw/nh are the tile counts (block length + 1).  The geometry path emits
     * from the block length, so the entry count is derived from the span
     * argument, not from the saved block dimensions. */
    merge_axis_emit(&tbl[PLTM_MERGE_H_BASE], nw, ((nw - 1) >> 1) + 2);
    merge_axis_emit(&tbl[PLTM_MERGE_V_BASE], nh, ((nh - 1) >> 1) + 2);
}

static void build_merge_table(pltm_entity_t *e, int nw, int nh)
{
    merge_table_fill(e->tbl, nw, nh);
}

/* ------------------------------------------------------------------ */
/* Tone floor table (spec 8.2).                                        */
/* ------------------------------------------------------------------ */

static void tone_table_gen(pltm_entity_t *e, int32_t clip)
{
    const uint16_t *sc = e->p.config.source_curve;
    int off = e->p.config.bit_offset;
    uint16_t *tbl = e->tbl;

    if (e->p.sensor.wdr_mode == 1) {
        uint32_t tbl_max = shr_u32(0x10000u, off) - 1u;
        int step = shl_one(off);
        int limit = (int)shr_u32(0x100u, off) - 1;
        int32_t acc = clip;
        int k;

        tbl[PLTM_TONE_BASE] = 0;
        tbl[PLTM_GAIN_BASE] = (uint16_t)tbl_max;

        k = 1;
        while (k < limit) {
            int32_t v = asr((int32_t)sc[PLTM_TONE_BASE + k * step], off);
            int32_t ref = asr(acc, 4);
            acc += clip;
            if (v > ref)
                ref = v;
            tbl[PLTM_TONE_BASE + k] =
                (uint16_t)PLTM_MIN(ref, (int32_t)tbl_max);
            k++;
        }

        if (off != 0) {
            int j;
            for (j = (int)shr_u32(0x100u, off); j <= 0xFE; j++) {
                tbl[PLTM_TONE_BASE + j] = (uint16_t)tbl_max;
                tbl[PLTM_GAIN_BASE + j] = PLTM_Q10_UNITY;
            }
        }
    } else {
        int32_t acc = clip;
        int k;

        tbl[PLTM_TONE_BASE] = 0;
        for (k = 1; k <= 254; k++) {
            int32_t v = (int32_t)sc[PLTM_TONE_BASE + k];
            int32_t ref = asr(acc, 4);
            tbl[PLTM_TONE_BASE + k] = (uint16_t)PLTM_MAX(v, ref);
            acc += clip;
        }
        tbl[PLTM_TONE_BASE + 0xFF] = 0xFFFF;
    }
}

/* ------------------------------------------------------------------ */
/* Gain table (spec 8.3).                                              */
/* ------------------------------------------------------------------ */

static void gain_table_gen(pltm_entity_t *e, int32_t gain)
{
    const uint16_t *sc = e->p.config.source_curve;
    int off = e->p.config.bit_offset;
    uint16_t *tbl = e->tbl;

    if (e->p.sensor.wdr_mode == 1) {
        uint32_t tbl_max = shr_u32(0x10000u, off) - 1u;
        int step = shl_one(off);
        int limit = (int)shr_u32(0x100u, off) - 1;
        int k, j;

        tbl[PLTM_GAIN_BASE] = (uint16_t)tbl_max;
        for (k = 1; k < limit; k++) {
            int32_t v;
            int idx = k * step;
            v = (int32_t)sc[PLTM_GAIN_BASE + idx];
            tbl[PLTM_GAIN_BASE + k] = (uint16_t)
                (((PLTM_Q12_UNITY - gain) * v + gain * PLTM_Q10_UNITY) >> 12);
        }
        for (j = (int)shr_u32(0x100u, off); j <= 0xFE; j++)
            tbl[PLTM_GAIN_BASE + j] = PLTM_Q10_UNITY;
    } else {
        int k;

        tbl[PLTM_GAIN_BASE] = 0xFFFF;
        for (k = 1; k <= 254; k++) {
            int32_t v = (int32_t)sc[PLTM_GAIN_BASE + k];
            tbl[PLTM_GAIN_BASE + k] = (uint16_t)
                ((v * (PLTM_Q12_UNITY - gain) + gain * PLTM_Q10_UNITY) >> 12);
        }
        tbl[PLTM_GAIN_BASE + 0xFF] = PLTM_Q10_UNITY;
    }
}

/* ------------------------------------------------------------------ */
/* Strength curve and its per-sample judge (spec 7.1, 7.3, 8.4).              */
/* ------------------------------------------------------------------ */

static void strength_curve_build(pltm_entity_t *e)
{
    const int32_t *bank = e->tab->strength_bank;
    int selector;
    int row, frac;
    int k;

    /* The selector addresses a (rows x 64) grid.  Pin it inside the bank
     * before splitting it, so a value past the last row can never index the
     * bank out of range (spec 13); in-domain selectors are untouched. */
    selector = e->p.config.auto_strength;
    selector = PLTM_CLAMP(selector, 0, PLTM_STRENGTH_ROWS * 64 - 1);
    row  = selector >> 6;
    frac = selector & 0x3F;

    for (k = 0; k < PLTM_STRENGTH_COLS; k++) {
        const int32_t *hi_row = bank + row * PLTM_STRENGTH_COLS;
        int32_t val;

        if (row == 0) {
            /* The zeroth row mixes its own squared form with itself. */
            int32_t base = bank[k];
            int32_t sq = (base * base) >> 8;

            val = (frac == 0) ? sq
                              : (((64 - frac) * sq + frac * base) >> 6);
        } else {
            const int32_t *lo_row = hi_row - PLTM_STRENGTH_COLS;
            int32_t lo = lo_row[k];
            int32_t hi = hi_row[k];

            val = (frac == 0) ? lo
                              : ((lo * (64 - frac) + hi * frac) >> 6);
        }
        e->strength_curve[k] = val;
    }
}

static int32_t strength_judge(pltm_entity_t *e, uint16_t x)
{
    int off = e->p.config.bit_offset;
    int top = PLTM_STRENGTH_COLS - 1;
    int i = x >> 4;
    int32_t v;

    /* The coarse index has no successor once it reaches the last curve slot.
     * Any statistic at or beyond it -- including values above the 12-bit
     * statistics domain -- collapses to the top-of-curve contribution
     * (spec 13). */
    if (i >= top)
        return asr(e->strength_curve[top] << 1, off);

    v = (e->strength_curve[i] * (16 - (x & 15)) +
         e->strength_curve[i + 1] * (x & 15)) >> 4;
    return asr(v << 1, off);
}

/* ------------------------------------------------------------------ */
/* Preset selection and modulation (spec 7.7).                         */
/* ------------------------------------------------------------------ */

static void preset_map(const pltm_clean_params_t *p, const int32_t *presets,
                       uint16_t strength,
                       int *oripic_ratio, int *order, int *last_order_ratio,
                       int *clip_out, int *gain_out)
{
    const pltm_clean_config_t *c = &p->config;
    const pltm_clean_sensor_t *s = &p->sensor;
    int high = (int)(strength >> 8);
    int low = (int)(strength & 0xFF);
    int idx = PLTM_MIN(high, 15);
    int o0, t0, c0, g0;
    int v_oripic, v_order, v_last, v_clip, v_gain;

    preset_get(presets, idx, &o0, &t0, &c0, &g0);

    if (low == 0) {
        v_oripic = o0;
        v_order = t0;
        v_clip = c0;
        v_gain = g0;
        v_last = 15;
    } else {
        int o1, t1, c1, g1;
        int w = 256 - low;
        preset_get(presets, idx + 1, &o1, &t1, &c1, &g1);
        v_oripic = sat8((o0 * w + o1 * low) >> 8);
        v_order = t0;
        v_last = (low >> 4) - 1;
        if (v_last < 0)
            v_last = 0;
        v_clip = PLTM_CLAMP(floor0((c0 * w + c1 * low) >> 8), 0,
                            PLTM_Q12_UNITY);
        v_gain = PLTM_CLAMP(floor0((g0 * w + g1 * low) >> 8), 0,
                            PLTM_Q12_UNITY);
    }

    if (high > 2 && ((uint32_t)(idx - 11) > 3u) &&
        ((uint32_t)(idx - 11) != 4u))
        v_order = PLTM_CLAMP(t0 + 1, 5, 13);
    else
        v_last = 15;

    if (s->wdr_mode == 1)
        v_gain = 0;

    v_clip = floor0(v_clip * (256 - c->contrast) / 256);
    v_clip = PLTM_CLAMP(v_clip, 0, PLTM_Q12_UNITY);

    if (c->auto_strength < PLTM_AUTO_THRESHOLD)
        v_clip = (v_clip * c->auto_strength) >> 4;

    if (oripic_ratio)
        *oripic_ratio = v_oripic;
    if (order)
        *order = v_order;
    if (last_order_ratio)
        *last_order_ratio = v_last;
    if (clip_out)
        *clip_out = v_clip;
    if (gain_out)
        *gain_out = v_gain;
}

/* ------------------------------------------------------------------ */
/* Minimum-level target, feedback and step limiter (spec 7.4-7.6).     */
/* ------------------------------------------------------------------ */

static void strength_apply(pltm_entity_t *e, const pltm_clean_stats_t *st)
{
    pltm_clean_config_t *c = &e->p.config;
    int tol = (c->tolerance == 0) ? 5 : c->tolerance;
    int step = (c->step == 0) ? 2 : c->step;
    int speed_index = 0;
    int32_t min_th, d, delta;
    uint16_t next;
    int oripic, order, last_order, clip, gain;

    /* From PLTM_SPEED_FRAME the configured speed selects a convergence-bank
     * row; pin it to the bank before it is used as an index (spec 13). */
    if (c->frame_id >= PLTM_SPEED_FRAME)
        speed_index = PLTM_CLAMP(c->speed, 0, PLTM_CONV_ROWS - 1);

    if (c->min_threshold == 0)
        min_th = PLTM_CLAMP((int32_t)((e->cal_strength + 6) / 12), 1, 0x100);
    else
        min_th = (uint16_t)((c->min_threshold << 4) & 0xFFFF);
    e->min_threshold_out = (uint16_t)min_th;

    d = (int32_t)min_th - (int32_t)st->measured_min;
    if (iabs32(d) < tol) {
        e->min_change_cnt = 0;
    } else {
        int32_t a = 30 - (iabs32(d) - tol);
        a = PLTM_CLAMP(a, 2, 30);
        e->min_change_cnt += 1;
        if (e->min_change_cnt < a)
            d = 0;
    }

    delta = (int32_t)e->cal_strength - (int32_t)e->old_strength;
    {
        int32_t ai = iabs32(delta);
        int step_tbl;
        if (ai > 127)
            ai = 127;
        step_tbl = e->tab->converge_bank[speed_index * PLTM_CONV_COLS + ai];

        if (iabs32(d) < tol) {
            if (delta < 0)
                next = (uint16_t)sat12((int32_t)e->old_strength - step);
            else
                next = e->old_strength;
        } else if (d >= tol) {
            if (delta >= 1) {
                int32_t v = (int32_t)e->old_strength + step_tbl;
                next = (uint16_t)(v > PLTM_STRENGTH_MAX
                                      ? PLTM_STRENGTH_MAX : v);
            } else {
                next = (uint16_t)sat12((int32_t)e->old_strength - step);
            }
        } else {
            if (delta < 0)
                next = (uint16_t)sat12((int32_t)e->old_strength - step_tbl);
            else
                next = (uint16_t)sat12((int32_t)e->old_strength + step);
        }
    }
    e->next_strength = next;

    preset_map(&e->p, e->tab ? e->tab->presets : NULL, next, &oripic, &order,
               &last_order, &clip, &gain);

    e->oripic_ratio_out = oripic;
    e->order_out = order;
    e->last_order_ratio_out = last_order;
    tone_table_gen(e, clip);
    gain_table_gen(e, gain);
}

/* ------------------------------------------------------------------ */
/* Strength-control dispatcher (spec 7).                               */
/* ------------------------------------------------------------------ */

static void strength_control(pltm_entity_t *e, const pltm_clean_stats_t *st)
{
    pltm_clean_config_t *c = &e->p.config;

    if (c->mode == 0 && c->auto_strength >= PLTM_AUTO_THRESHOLD) {
        int32_t acc = 0;
        int k;
        strength_curve_build(e);
        for (k = 0; k < PLTM_NSTAT; k++)
            acc += strength_judge(e, st->lst[k]);
        e->cal_strength = (uint16_t)sat12((acc * 16) / PLTM_NSTAT);
        strength_apply(e, st);
    } else if (c->mode == 0) {
        e->cal_strength = (uint16_t)((c->auto_strength << 4) & 0xFFFF);
        strength_apply(e, st);
    } else if (c->mode == 1) {
        e->cal_strength = (uint16_t)((c->manual_strength << 4) & 0xFFFF);
        strength_apply(e, st);
    } else {
        /* Manual/static: copy ratios, use clip/gain directly, no statistics. */
        e->oripic_ratio_out = c->oripic_ratio_cfg;
        e->order_out = c->order_cfg;
        e->last_order_ratio_out = c->last_order_ratio_cfg;
        tone_table_gen(e, c->clip_cfg);
        gain_table_gen(e, c->gain_cfg);
    }
}

/* ------------------------------------------------------------------ */
/* Tile geometry update (spec 6.1).                                    */
/* ------------------------------------------------------------------ */

/* Tile count along one axis.  The grid divisor is the block count plus one,
 * which a degenerate configuration can drive to zero; that yields no tiles
 * rather than a division (spec 13). */
static int tile_axis_count(int pixels, int grid_blocks)
{
    return (grid_blocks != 0) ? pixels / grid_blocks : 0;
}

static void tile_geometry_update(pltm_entity_t *e)
{
    pltm_clean_config_t *c = &e->p.config;
    pltm_clean_sensor_t *s = &e->p.sensor;

    if (e->block_rows_old != c->block_rows ||
        e->block_cols_old != c->block_cols)
        g_start_frame = c->frame_id + 3;

    if (e->sensor_h_old != s->height || e->block_rows_old != c->block_rows ||
        e->sensor_w_old != s->width  || e->block_cols_old != c->block_cols) {
        int nw = tile_axis_count(s->width, c->block_cols + 1);
        int nh = tile_axis_count(s->height, c->block_rows + 1);
        uint64_t denom = (uint64_t)(int64_t)nw * (uint64_t)(int64_t)nh;

        e->block_width_out = nw - 1;
        e->block_height_out = nh - 1;
        e->stat_scale = (denom != 0)
                            ? (uint32_t)(0x100000000ULL / denom)
                            : 0u;
        build_merge_table(e, nw, nh);
        e->sensor_h_old = s->height;
        e->sensor_w_old = s->width;
        e->block_rows_old = c->block_rows;
        e->block_cols_old = c->block_cols;
    }
}

/* ------------------------------------------------------------------ */
/* Output publication.                                                 */
/* ------------------------------------------------------------------ */

static void publish(pltm_entity_t *e, pltm_clean_result_t *r)
{
    memcpy(r->tbl, e->tbl, sizeof(r->tbl));
    r->oripic_ratio = e->oripic_ratio_out;
    r->order = e->order_out;
    r->last_order_ratio = e->last_order_ratio_out;
    r->cal_en = e->cal_en;
    r->frame_smooth_en = e->frame_smooth_en;
    r->block_width = e->block_width_out;
    r->block_height = e->block_height_out;
    r->stat_scale = e->stat_scale;
    r->ae_comp = e->ae_comp_out;
    r->old_strength = e->old_strength;
    r->next_strength = e->next_strength;
    r->cal_strength = e->cal_strength;
    r->min_threshold = e->min_threshold_out;
}

/* ------------------------------------------------------------------ */
/* Entry points.                                                       */
/* ------------------------------------------------------------------ */

pltm_entity_t *pltm_init(pltm_ops_t *out_ops)
{
    const freeisp_tables_t *ft = freeisp_get_tables();
    const pltm_clean_tables_t *t;
    pltm_entity_t *e;

    if (!ft || !ft->pltm)
        return NULL;
    t = ft->pltm;
    if (!t->strength_bank || !t->converge_bank)
        return NULL;

    e = (pltm_entity_t *)calloc(1, sizeof(*e));
    if (!e)
        return NULL;

    e->tab = t;
    e->busy_flag = 1;
    e->interval_frame = 0;
    memset(e->strength_curve, 0, sizeof(e->strength_curve));
    e->sensor_w_old = 0;
    e->sensor_h_old = 0;
    e->block_cols_old = 0;
    e->block_rows_old = 0;

    if (out_ops) {
        out_ops->get_params = pltm_get_params;
        out_ops->set_params = pltm_set_params;
        out_ops->run = pltm_run;
        out_ops->set_default_result = pltm_set_default_result;
    }
    return e;
}

void pltm_exit(pltm_entity_t *e)
{
    free(e);
}

int pltm_get_params(pltm_entity_t *e, pltm_clean_params_t **out)
{
    if (e == NULL)
        return -1;
    if (out != NULL)
        *out = &e->p;
    return 0;
}

int pltm_set_params(pltm_entity_t *e, const pltm_clean_param_req_t *req,
                    int *result)
{
    (void)e;
    (void)req;
    if (result != NULL)
        *result = 0;
    return 0;
}

int pltm_set_default_result(pltm_entity_t *e, pltm_clean_result_t *result)
{
    (void)e;
    (void)result;
    return 0;
}

int pltm_run(pltm_entity_t *e, const pltm_clean_stats_t *stats,
             pltm_clean_result_t *result)
{
    pltm_clean_config_t *c;
    pltm_clean_sensor_t *s;
    int interval, start, fid;

    if (e == NULL || stats == NULL || result == NULL)
        return -1;

    c = &e->p.config;
    s = &e->p.sensor;

    /* Spec 6: geometry always runs first. */
    tile_geometry_update(e);

    interval = (c->interval > 1) ? c->interval : 1;
    e->interval_frame = interval;

    if (c->enable == 0) {
        e->cal_en = 0;
        e->frame_smooth_en = 0;
        e->ae_comp_out = 0;
        e->next_strength = 0;
        publish(e, result);
        return 0;
    }

    e->ae_comp_out = (uint8_t)(c->ae_comp & 0xFF);
    start = g_start_frame;
    fid = c->frame_id;

    if (fid < start) {
        if (fid == start - 1) {
            e->cal_en = 1;
            e->frame_smooth_en = 0;
            if (e->next_strength == 0)
                e->next_strength = (uint16_t)(c->manual_strength & 0xFFFF);
        } else {
            e->cal_en = 0;
            e->frame_smooth_en = 0;
        }
    } else {
        if (s->ae_settled == 0) {
            e->min_change_cnt = 0;
            publish(e, result);
            return 0; /* old_strength deliberately frozen */
        }
        if (((fid - start) % interval) == 0) {
            e->cal_en = 1;
            e->frame_smooth_en = 1;
            strength_control(e, stats);
        }
    }

    e->old_strength = e->next_strength;
    publish(e, result);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Diagnostics.                                                        */
/* ------------------------------------------------------------------ */

int pltm_clean_get_start_frame(void)
{
    return g_start_frame;
}

void pltm_clean_set_start_frame(int frame)
{
    g_start_frame = frame;
}

void pltm_clean_set_strength_state(pltm_entity_t *e, uint16_t old_strength,
                                   uint16_t next_strength)
{
    if (e == NULL)
        return;
    e->old_strength = old_strength;
    e->next_strength = next_strength;
}

void pltm_clean_preset(int index, int *oripic_ratio, int *order,
                       int *clip, int *gain)
{
    preset_get(provider_presets(), index, oripic_ratio, order, clip, gain);
}

void pltm_clean_strength_map(const pltm_clean_params_t *p, uint16_t strength,
                             int *oripic_ratio, int *order,
                             int *last_order_ratio, int *clip, int *gain)
{
    if (p == NULL)
        return;
    preset_map(p, provider_presets(), strength, oripic_ratio, order,
               last_order_ratio, clip, gain);
}

void pltm_clean_merge_build(pltm_clean_result_t *result, int nw, int nh)
{
    if (result == NULL)
        return;
    memset(result->tbl, 0, sizeof(result->tbl));
    merge_table_fill(result->tbl, nw, nh);
}
