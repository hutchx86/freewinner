/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * gtm_clean.h - clean-room global tone-mapping module.
 *
 * Behaviour-only reimplementation of the GTM stage described in
 * spec/gtm.md.  The module computes one 256-entry tone curve per
 * qualified frame.  No tuning data is compiled in: the guide profiles,
 * pre-gamma bank, equalisation kernel bank and convergence bank all arrive at
 * runtime through the table-provider contract declared here
 * (freeisp_get_tables()).  A provider that yields no GTM table is a
 * programming error and initialisation fails.
 *
 * Own names and structures; the spec's neutral names are used directly.
 */
#ifndef GTM_CLEAN_H
#define GTM_CLEAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GTM_NCURVE     256   /* curve entries / histogram bins            */
#define GTM_NWIN       384   /* coarse block-average luminance entries    */
#define GTM_NGAMMA    1024   /* per-instance gamma map entries            */
#define GTM_NPEAK        9   /* peak-map side (9x9)                       */
#define GTM_PRE_ROWS    11   /* pre-gamma bank rows                       */
#define GTM_PRE_COLS   256   /* pre-gamma bank columns                    */
#define GTM_EQ_ROWS     20   /* equalisation kernel rows                  */
#define GTM_EQ_TAPS     31   /* equalisation kernel taps                  */
#define GTM_CONV_ROWS   32   /* convergence bank rows                     */
#define GTM_CONV_COLS  128   /* convergence bank columns                  */

#define GTM_Q12       4096   /* histogram / merge-curve scale             */
#define GTM_CURVE_MAX 0x0FFF /* 12-bit output clamp                      */

/* Mode values; the deployed object implements exactly these four. */
typedef enum gtm_clean_mode {
    GTM_MODE_FIXED         = 0,
    GTM_MODE_DYNAMIC_RANGE = 1,
    GTM_MODE_DYNAMIC_GAMMA = 2,
    GTM_MODE_LUMA_HOLD     = 3
} gtm_clean_mode_t;

/* Tone-domain transfer kind used by the equalisation stage. */
typedef enum gtm_clean_gamma_mode {
    GTM_GAMMA_FIXED   = 0,
    GTM_GAMMA_DYNAMIC = 1
} gtm_clean_gamma_mode_t;

/* ------------------------------------------------------------------ */
/* Per-instance parameter block.  Read live on every run; curve and    */
/* curve_prev are the module's persistent curve buffers.               */
/* ------------------------------------------------------------------ */

typedef struct gtm_clean_params {
    int             mode;            /* gtm_clean_mode_t                    */
    int             gamma_mode;      /* gtm_clean_gamma_mode_t              */
    int             frame_index;     /* monotonic frame counter; runs if >2 */
    int             enable;          /* non-zero allows the module to run   */
    int             range_max;       /* target maximum output level         */
    int             eq_gain;         /* equalisation strength, 0..100       */
    int             cover_bias;      /* kernel-row bias, -10..10            */
    int             black_level;     /* shadow pivot                         */
    int             white_level;     /* highlight pivot                      */
    int             white_slope;     /* highlight ramp slope                 */
    int             black_slope;     /* shadow ramp slope                    */
    int             pre_gamma_offset;/* pre-gamma row selector               */
    int             hist_pixel_count;/* divergence pixel count               */
    int             dark_floor;      /* shadow floor                         */
    int             bright_floor;    /* highlight floor                      */
    int             peak_map[GTM_NPEAK][GTM_NPEAK]; /* activity/luma -> peak  */
    int             brightness;      /* user brightness                      */
    int             contrast;        /* user contrast                        */
    int             bit_offset;      /* WDR output downscale selector        */
    int             wdr_en;          /* WDR downscale enable                 */

    uint16_t        gamma_lut[GTM_NGAMMA];  /* monotone tone map            */

    /* Persistent output curves (read-modify-write each run). */
    uint16_t        curve[GTM_NCURVE];
    uint16_t        curve_prev[GTM_NCURVE];
} gtm_clean_params_t;

/* ------------------------------------------------------------------ */
/* Per-frame hardware statistics.  Exactly 256 bins and 384 averages   */
/* are read.                                                           */
/* ------------------------------------------------------------------ */

typedef struct gtm_clean_stats {
    uint32_t hist_raw[GTM_NCURVE];
    uint32_t win_avg[GTM_NWIN];
} gtm_clean_stats_t;

/* ------------------------------------------------------------------ */
/* Per-frame result.                                                   */
/* ------------------------------------------------------------------ */

typedef struct gtm_clean_result {
    uint16_t  avg_lum;      /* mean luma of the normalised histogram (luma-hold) */
    uint16_t  avg_var;      /* mean-absolute-deviation activity statistic        */
    uint16_t  peak_level;   /* slew-limited target peak level                    */
    uint16_t  div_index;    /* divergence histogram index                        */
    double    ratio_hold;   /* held merge ratio                                  */
    int       hdr_flag;     /* set to 1 whenever the module executes             */

    /* Point into the instance curve buffers; valid after a run. */
    uint16_t *curve;
    uint16_t *curve_prev;
} gtm_clean_result_t;

/* ------------------------------------------------------------------ */
/* Injected tables - supplied at runtime, never built in.              */
/* ------------------------------------------------------------------ */

typedef struct gtm_clean_tables {
    const int16_t  *guide_linear; /* 256                                   */
    const int16_t  *guide_low;    /* 256, shadow-biased                    */
    const int16_t  *guide_high;   /* 256, highlight-biased                 */
    const int16_t  *pre_gamma;    /* GTM_PRE_ROWS * GTM_PRE_COLS           */
    const int32_t  *eq_kernel;    /* GTM_EQ_ROWS * GTM_EQ_TAPS             */
    const uint8_t  *converge;     /* GTM_CONV_ROWS * GTM_CONV_COLS         */
} gtm_clean_tables_t;

typedef struct freeisp_tables {
    const gtm_clean_tables_t *gtm;
} freeisp_tables_t;

/* Provided by the framework; must be non-NULL before gtm_init(). */
const freeisp_tables_t *freeisp_get_tables(void);

/* ------------------------------------------------------------------ */
/* Entry points (interfaces.md).                                       */
/* ------------------------------------------------------------------ */

typedef struct gtm_entity gtm_entity_t;

/* set-parameters dispatch. */
enum {
    GTM_PARAM_INIT = 0
};

typedef struct gtm_clean_param_req {
    int                       kind;
    const gtm_clean_params_t *params; /* optional, for GTM_PARAM_INIT */
} gtm_clean_param_req_t;

typedef struct gtm_ops {
    int (*get_params)(gtm_entity_t *e, gtm_clean_params_t **out);
    int (*set_params)(gtm_entity_t *e, const gtm_clean_param_req_t *req,
                      int *result);
    int (*run)(gtm_entity_t *e, const gtm_clean_stats_t *stats,
               gtm_clean_result_t *result);
} gtm_ops_t;

gtm_entity_t *gtm_init(gtm_ops_t *out_ops);
void          gtm_exit(gtm_entity_t *e);
int           gtm_get_params(gtm_entity_t *e, gtm_clean_params_t **out);
int           gtm_set_params(gtm_entity_t *e, const gtm_clean_param_req_t *req,
                             int *result);
int           gtm_run(gtm_entity_t *e, const gtm_clean_stats_t *stats,
                      gtm_clean_result_t *result);

/* Diagnostics only: current guide polarity (-1, 0 or +1); INT_MIN if self is
 * NULL.  Exposed so the hysteresis state machine can be exercised in tests. */
int gtm_clean_guide_state(const gtm_entity_t *e);

#ifdef __cplusplus
}
#endif

#endif /* GTM_CLEAN_H */
