/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * awb_clean.h - clean-room auto white-balance module.
 *
 * Behaviour-only reimplementation of the AWB module described in
 * spec/awb.md.  No tuning data is compiled in: the trust table,
 * smoothing-weight table, class labels, standard-trust row and the
 * brightness/temperature matrix all arrive at runtime through the table
 * provider declared here (freeisp_get_tables()).
 *
 * Own names and structures; the spec's neutral names are used directly.
 */
#ifndef AWB_CLEAN_H
#define AWB_CLEAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AWB_NWIN        1024  /* 32 x 32 statistics windows             */
#define AWB_GRID_W        32
#define AWB_GRID_H        32
#define AWB_NREF          10  /* reference classes                      */
#define AWB_NCURVE        64  /* points in the global illuminant curve  */
#define AWB_NPTS          16  /* points per per-class curve             */
#define AWB_NGHIST        48  /* entries in the temporal gain history   */
#define AWB_NPAL           5  /* classes searched for the nearest ref   */
#define AWB_REF_INTS      10  /* configured integers per reference      */
#define AWB_REF_FIELDS    13  /* internal integers per reference         */
#define AWB_MAX_LIGHTS    32  /* standard light presets                 */
#define AWB_MAX_SKIN      16  /* skin references                        */
#define AWB_MAX_SPECIAL   32  /* special-colour references              */
#define AWB_NTRUST        96  /* rows in the trust table                */
#define AWB_NSPEED        48  /* rows in the smoothing-weight table     */
#define AWB_NSSC          64  /* columns in the smoothing-weight table  */

#define AWB_UNITY        256  /* unity gain / ratio scale               */
#define AWB_GAIN_MIN      32
#define AWB_GAIN_MAX    2048

/* ------------------------------------------------------------------ */
/* Reference record (spec 3.3 / 5.2)                                  */
/*                                                                    */
/* The configured prefix is ten integers.  Three derived fields are    */
/* appended internally: the reference R/G and B/G ratios and a         */
/* light-type tag that is carried but never consumed.                  */
/* ------------------------------------------------------------------ */

typedef struct awb_ref {
    int32_t ref[3];       /* 0..2 reference normalised R, G, B          */
    int32_t pref[3];      /* 3..5 preference-gain triple                */
    int32_t tol;          /* 6    trust-table row selector              */
    int32_t temp;         /* 7    correlated colour temperature         */
    int32_t prob_ls;      /* 8    light-source probability              */
    int32_t prob_pref;    /* 9    preference probability                */
    int32_t refk_r;       /* 10   derived round(256*ref[0]/ref[1])      */
    int32_t refk_b;       /* 11   derived round(256*ref[2]/ref[1])      */
    int32_t light_tag;    /* 12   carried, never compared               */
} awb_ref_t;

/* ------------------------------------------------------------------ */
/* Configuration block (spec 3.1).  Read live from per-instance        */
/* storage; set-params copies it and rebuilds the reference hierarchy.  */
/* ------------------------------------------------------------------ */

typedef struct awb_params {
    int32_t  mode;              /* 0 manual, 1 adaptive, 2..10 presets */
    int32_t  manual_gain[4];    /* r, gr, gb, b (manual mode only)      */
    int32_t  lock;              /* freeze the adaptive result            */
    int32_t  win_region[4];     /* window of interest (unused here)      */
    int32_t  interval;          /* re-estimate cadence                   */
    int32_t  speed;             /* temporal smoothing speed              */
    int32_t  temp_low;          /* unused by this module                 */
    int32_t  temp_high;         /* unused by this module                 */
    int32_t  base_temp;         /* preference target (0 -> 6500)         */
    int32_t  green_dist;        /* green-zone distance gate              */
    int32_t  blue_dist;         /* blue-sky distance gate                */
    int32_t  light_num;         /* 0..AWB_MAX_LIGHTS                     */
    int32_t  ext_light_num;     /* filled but never classified           */
    int32_t  skin_num;          /* 0..AWB_MAX_SKIN                       */
    int32_t  special_num;       /* 0..AWB_MAX_SPECIAL                    */

    int32_t  light_info[AWB_MAX_LIGHTS * AWB_REF_INTS];
    int32_t  ext_light_info[AWB_MAX_LIGHTS * AWB_REF_INTS];
    int32_t  skin_info[AWB_MAX_SKIN * AWB_REF_INTS];
    int32_t  special_info[AWB_MAX_SPECIAL * AWB_REF_INTS];

    int32_t  preset_gain[22];   /* per-scene red/blue ratio pairs        */
    int32_t  r_favor;           /* favourite red multiplier              */
    int32_t  b_favor;           /* favourite blue multiplier             */

    /* --- sensor descriptor fields actually read --- */
    int32_t  ae_lv;             /* scene brightness level                */
    uint32_t ae_index;          /* current exposure-table index          */
    uint32_t ae_index_max;      /* exposure-table length                 */
    uint8_t  ae_done;           /* exposure settled flag                 */

    /* --- test / diagnostic configuration --- */
    int32_t  test_mode;         /* not read by this module               */
    int32_t  platform_id;       /* not read by this module               */
    int32_t  fixed_temp;        /* fixed temperature (0 -> 6500)         */
    int32_t  control_enable;    /* non-zero enables the adaptive machine */
    int32_t  frame_id;          /* caller-supplied frame counter         */
} awb_params_t;

/* ------------------------------------------------------------------ */
/* Per-frame statistics (spec 3.2).                                   */
/* ------------------------------------------------------------------ */

typedef struct awb_win_stat {
    uint32_t avg[3];            /* average R, G, B in the window         */
    uint32_t npix;              /* contributing pixel count              */
} awb_win_stat_t;

typedef struct awb_stats {
    awb_win_stat_t win[AWB_NWIN];   /* row-major 32 x 32 grid            */
} awb_stats_t;

/* ------------------------------------------------------------------ */
/* Output (spec 4).                                                   */
/* ------------------------------------------------------------------ */

typedef struct awb_gain {
    uint16_t r, gr, gb, b;
} awb_gain_t;

typedef struct awb_result {
    awb_gain_t gain_out;
    int32_t    color_temp_out;
} awb_result_t;

/* ------------------------------------------------------------------ */
/* Injected tables (spec 3.4) - all supplied at runtime, never built   */
/* in.  A missing handle is a programming error.                      */
/* ------------------------------------------------------------------ */

typedef struct awb_clean_tables {
    const uint8_t  *trust;        /* [AWB_NTRUST][256]                   */
    const uint16_t *speed_w;      /* [AWB_NSPEED][AWB_NSSC]              */
    const char     *class_label;  /* [AWB_NREF][32], diagnostics only    */
    const int32_t  *std_trust;    /* [AWB_NREF]                          */
    const int32_t  *temp_bright;  /* [AWB_NREF][AWB_NREF]                */
    const uint16_t *safe_gain;    /* [4] r, gr, gb, b: the fixed quadruple*/
                                  /* the run entry point installs when it */
                                  /* is called without statistics and an  */
                                  /* output channel is still zero.        */
} awb_clean_tables_t;

typedef struct freeisp_tables {
    const awb_clean_tables_t *awb;
} freeisp_tables_t;

/* Provided by the framework; must be non-NULL before awb_init(). */
const freeisp_tables_t *freeisp_get_tables(void);

/* ------------------------------------------------------------------ */
/* Entry points (interfaces.md).                                      */
/* ------------------------------------------------------------------ */

typedef struct awb_entity awb_entity_t;

enum {
    AWB_PARAM_INIT = 0    /* (re)initialise from configuration            */
};

typedef struct awb_param_req {
    int                  kind;
    const awb_params_t  *params;   /* for AWB_PARAM_INIT                */
} awb_param_req_t;

typedef struct awb_ops {
    int (*get_params)(awb_entity_t *e, awb_params_t **out);
    int (*set_params)(awb_entity_t *e, const awb_param_req_t *req, int *result);
    int (*run)(awb_entity_t *e, const awb_stats_t *stats, awb_result_t *result);
    int (*isr)(awb_entity_t *e, const awb_stats_t *stats, awb_result_t *result);
} awb_ops_t;

awb_entity_t *awb_init(awb_ops_t *out_ops);
void          awb_exit(awb_entity_t *e);
int           awb_get_params(awb_entity_t *e, awb_params_t **out);
int           awb_set_params(awb_entity_t *e, const awb_param_req_t *req, int *result);
int           awb_run(awb_entity_t *e, const awb_stats_t *stats, awb_result_t *result);
int           awb_isr(awb_entity_t *e, const awb_stats_t *stats, awb_result_t *result);

/* Diagnostics for tests; no effect on gains. */
int           awb_clean_is_night(const awb_entity_t *e);
int           awb_clean_color_temp(const awb_entity_t *e);
uint32_t      awb_clean_frame_count(const awb_entity_t *e);

/* Per-window classification diagnostics (spec 5.1 / 6.3); no effect on
 * gains.  The window index is 0..AWB_NWIN-1.  For any out-of-range index
 * or null entity the accessors return 0. */
int           awb_clean_window_class(const awb_entity_t *e, int i);
int           awb_clean_window_dist(const awb_entity_t *e, int i);
int           awb_clean_window_temp(const awb_entity_t *e, int i);
int           awb_clean_window_seg(const awb_entity_t *e, int i);
int           awb_clean_window_level(const awb_entity_t *e, int i);

#ifdef __cplusplus
}
#endif

#endif /* AWB_CLEAN_H */
