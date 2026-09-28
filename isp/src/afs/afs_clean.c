/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* afs_clean.c - auto-flicker (mains-ripple) detection (spec/afs.md). */
#include "afs_clean.h"
#include "freeisp/sdiv.h"

#include <stdlib.h>
#include <string.h>

#define ROWS_PER_SET   AFS_CLEAN_NROW
#define COLS           AFS_CLEAN_NCOL
#define BINS           AFS_CLEAN_NBIN
#define TRIG_INDEX_MSK 0x1Fu

#define FRAME_WARMUP      3
#define GAIN_LOW          255
#define GAIN_WINDOW       3
#define TREND_DEADBAND    1
#define TREND_MIX_MIN     64
#define TREND_BALANCE_DIV 2
#define ROW_IMBALANCE     32
#define ROW_IMBALANCE_CNT ROWS_PER_SET
#define BIN_LOW           4
#define BIN_SPAN          9u
#define BIN_SYM_SUM       16
#define SHARE_SCALE       100
#define FLICKER_LINE_MIN  35
#define NOMINAL_CELLS     (ROWS_PER_SET * COLS)
#define LATCH_UNKNOWN     0xFF

enum {
    TREND_RISING = 0,
    TREND_FALLING = 1,
    TREND_FLAT = 2
};

struct afs_clean_state {
    int raw[ROWS_PER_SET][COLS];
    int centered[ROWS_PER_SET][COLS];
    int spectrum[BINS][COLS];
    int spectrum_t[COLS][BINS];
    int energy[COLS];
    int trend[ROWS_PER_SET][COLS];
    int trend_counts[COLS][3];
    int total_up;
    int total_down;
    int total_flat;
    int total_cells;
    int col_sum[COLS];
    int col_avg[COLS];
    int trend_weight[3];
    int rows_filled;
    int type_latch;
    int active;
    int current_type;
    int busy_flag;

    const int *sin_tbl;
    const int *cos_tbl;

    afs_clean_params_t params;
};

static afs_clean_trig_provider_fn g_trig_provider;

void afs_clean_set_trig_provider(afs_clean_trig_provider_fn provider)
{
    g_trig_provider = provider;
}

afs_clean_trig_provider_fn afs_clean_get_trig_provider(void)
{
    return g_trig_provider;
}

static int iabs(int v)
{
    return v < 0 ? -v : v;
}

/* Seed policy: map seed_type to a concrete 50/60 provisional type. 0 maps to
 * 50; values below 1 map to 50; values above 1 map to 60. */
static int seed_to_type(int seed_type)
{
    if (seed_type == 0)
        seed_type = 1;
    if (seed_type < 1)
        seed_type = 1;
    if (seed_type > 2)
        seed_type = 2;
    return seed_type == 2 ? AFS_CLEAN_TYPE_HZ60 : AFS_CLEAN_TYPE_HZ50;
}

/* Detection stage, run on a completed 16-row set. */
static void afs_clean_decide(afs_clean_state_t *s, afs_clean_result_t *result,
                             const afs_clean_params_t *p)
{
    int c, k, b;

    memset(s->centered, 0, sizeof(s->centered));
    memset(s->spectrum, 0, sizeof(s->spectrum));
    memset(s->spectrum_t, 0, sizeof(s->spectrum_t));
    memset(s->energy, 0, sizeof(s->energy));
    memset(s->trend, 0, sizeof(s->trend));
    memset(s->trend_counts, 0, sizeof(s->trend_counts));
    s->total_up = 0;
    s->total_down = 0;
    s->total_flat = 0;

    /* Trend construction: mean removal plus adjacent-row classes. */
    for (c = 0; c < COLS; c++) {
        int col_total = 0;

        for (k = 0; k < ROWS_PER_SET; k++)
            col_total += s->raw[k][c];

        s->col_sum[c] = col_total;
        s->col_avg[c] = col_total;

        for (k = 0; k < ROWS_PER_SET; k++)
            s->centered[k][c] = ROWS_PER_SET * s->raw[k][c] - col_total;

        s->trend[0][c] = TREND_FLAT;
        for (k = 1; k < ROWS_PER_SET; k++) {
            int prev = s->raw[k - 1][c];
            int cur = s->raw[k][c];

            if (cur > prev + TREND_DEADBAND) {
                s->trend[k][c] = TREND_RISING;
                s->trend_counts[c][TREND_RISING]++;
            } else if (cur < prev - TREND_DEADBAND) {
                s->trend[k][c] = TREND_FALLING;
                s->trend_counts[c][TREND_FALLING]++;
            } else {
                s->trend[k][c] = TREND_FLAT;
                s->trend_counts[c][TREND_FLAT]++;
            }
        }

        s->total_up += s->trend_counts[c][TREND_RISING];
        s->total_down += s->trend_counts[c][TREND_FALLING];
        s->total_flat += s->trend_counts[c][TREND_FLAT];
    }
    s->total_cells = NOMINAL_CELLS;

    /* Trend-balance gate. */
    {
        int diff = s->total_up + s->total_down;

        if (!(diff > TREND_MIX_MIN &&
              iabs(s->total_up - s->total_down) < diff / TREND_BALANCE_DIV))
            return;
    }

    {
        int imbalanced = 0;

        for (k = 0; k < ROWS_PER_SET; k++) {
            int w = 0;

            for (c = 0; c < COLS; c++)
                w += s->trend_weight[s->trend[k][c]];

            if (iabs(w) > ROW_IMBALANCE)
                imbalanced++;
        }

        if (imbalanced >= ROW_IMBALANCE_CNT)
            return;
    }

    /* Orientation projection onto the 16-bin spatial-frequency basis. */
    for (c = 0; c < COLS; c++) {
        for (b = 0; b < BINS; b++) {
            int step = 2 * b;
            int acc_i = 0;
            int acc_r = 0;

            for (k = 0; k < ROWS_PER_SET; k++) {
                int m = (step * k) & TRIG_INDEX_MSK;
                int cell = s->centered[k][c];

                acc_i += s->sin_tbl[m] * cell;
                acc_r += s->cos_tbl[m] * cell;
            }

            s->spectrum[b][c] = iabs(acc_i) + iabs(acc_r);
        }
    }

    for (c = 0; c < COLS; c++) {
        int total = 0;

        for (b = 0; b < BINS; b++) {
            total += s->spectrum[b][c];
            s->spectrum_t[c][b] = s->spectrum[b][c];
        }
        s->energy[c] = total;
    }

    /* Peak-pair detection. */
    {
        int flicker_lines = 0;

        for (c = 0; c < COLS; c++) {
            const int *row = s->spectrum_t[c];
            int m1 = 0;
            int m2;
            int share;

            for (b = 1; b < BINS; b++)
                if (row[b] > row[m1])
                    m1 = b;

            m2 = (m1 == 0) ? 1 : 0;
            for (b = 0; b < BINS; b++) {
                if (b == m1)
                    continue;
                if (row[b] > row[m2])
                    m2 = b;
            }

            if ((unsigned)(m1 - BIN_LOW) >= BIN_SPAN ||
                (unsigned)(m2 - BIN_LOW) >= BIN_SPAN)
                continue;
            if (m1 + m2 != BIN_SYM_SUM)
                continue;
            if (s->energy[c] == 0)
                continue;

            share = (row[m1] + row[m2]) * SHARE_SCALE / s->energy[c];
            if (share > p->min_peak_ratio)
                flicker_lines++;
        }

        if (flicker_lines > FLICKER_LINE_MIN) {
            if (p->seed_type == AFS_CLEAN_SEED_ALTERNATE) {
                result->detected_type = (s->current_type == AFS_CLEAN_TYPE_HZ50)
                                            ? AFS_CLEAN_TYPE_HZ60
                                            : AFS_CLEAN_TYPE_HZ50;
            } else {
                result->detected_type = (p->seed_type == AFS_CLEAN_SEED_PREFER60)
                                            ? AFS_CLEAN_TYPE_HZ60
                                            : AFS_CLEAN_TYPE_HZ50;
            }
            s->current_type = result->detected_type;
            s->type_latch = result->detected_type;
        }
    }
}

static const afs_clean_ops_t g_ops = {
    afs_clean_exit,
    afs_clean_get_params,
    afs_clean_set_params,
    afs_clean_run,
    afs_clean_isr
};

afs_clean_state_t *afs_clean_init(const afs_clean_ops_t **out_ops)
{
    const afs_clean_trig_t *trig;
    afs_clean_state_t *s;

    if (out_ops != NULL)
        *out_ops = NULL;

    trig = (g_trig_provider != NULL) ? g_trig_provider() : NULL;
    if (trig == NULL || trig->sine == NULL || trig->cosine == NULL)
        return NULL;

    s = (afs_clean_state_t *)calloc(1, sizeof(*s));
    if (s == NULL)
        return NULL;

    s->sin_tbl = trig->sine;
    s->cos_tbl = trig->cosine;
    s->type_latch = LATCH_UNKNOWN;
    s->rows_filled = 0;
    s->trend_weight[TREND_RISING] = 1;
    s->trend_weight[TREND_FALLING] = -1;
    s->trend_weight[TREND_FLAT] = 0;
    s->busy_flag = 1;
    s->current_type = 0;
    s->active = 0;

    if (out_ops != NULL)
        *out_ops = &g_ops;

    return s;
}

void afs_clean_exit(afs_clean_state_t *self)
{
    free(self);
}

int afs_clean_get_params(afs_clean_state_t *self, afs_clean_params_t **out)
{
    if (self == NULL)
        return -1;
    if (out != NULL)
        *out = &self->params;
    return 0;
}

int afs_clean_set_params(afs_clean_state_t *self, const afs_clean_params_t *in,
                         int *result)
{
    (void)self;
    (void)in;
    if (result != NULL)
        *result = 0;
    return 0;
}

int afs_clean_run(afs_clean_state_t *self, const afs_clean_stats_t *stats,
                  afs_clean_result_t *result)
{
    const afs_clean_params_t *p;
    int r;
    int c;

    if (self == NULL || stats == NULL) {
        result->detected_type = AFS_CLEAN_TYPE_HZ50;
        return -1;
    }

    p = &self->params;

    if (p->frame_index < FRAME_WARMUP)
        return 0;

    /* Mode dispatch: sets output and active flag together. */
    switch (p->mode) {
    case AFS_CLEAN_MODE_OFF:
        result->detected_type = AFS_CLEAN_TYPE_NONE;
        self->active = 0;
        break;
    case AFS_CLEAN_MODE_FORCE50:
        result->detected_type = AFS_CLEAN_TYPE_HZ50;
        self->active = 0;
        break;
    case AFS_CLEAN_MODE_FORCE60:
        result->detected_type = AFS_CLEAN_TYPE_HZ60;
        self->active = 0;
        break;
    case AFS_CLEAN_MODE_AUTO: {
        int t = self->type_latch;

        self->active = 1;
        if (t == LATCH_UNKNOWN)
            t = seed_to_type(p->seed_type);
        result->detected_type = t;
        break;
    }
    default:
        /* Unknown mode: leave output and active stale. */
        break;
    }

    if (self->active != 1 || p->enable == 0)
        return 0;

    if ((unsigned)(p->gain_level - GAIN_LOW) >= (unsigned)GAIN_WINDOW) {
        self->rows_filled = 0;
        return 0;
    }

    r = self->rows_filled % ROWS_PER_SET;
    for (c = 0; c < COLS; c++)
        self->raw[r][c] =
            (int)freeisp_udiv(stats->column_sum[c],
                              (unsigned int)p->image_width);
    self->rows_filled = r + 1;

    if (r + 1 != ROWS_PER_SET)
        return 0;

    afs_clean_decide(self, result, p);
    self->rows_filled = 0;
    return 0;
}

int afs_clean_isr(afs_clean_state_t *self, const afs_clean_stats_t *stats,
                  afs_clean_result_t *result)
{
    return afs_clean_run(self, stats, result);
}

int afs_clean_rows_filled(const afs_clean_state_t *self)
{
    return (self != NULL) ? self->rows_filled : -1;
}
