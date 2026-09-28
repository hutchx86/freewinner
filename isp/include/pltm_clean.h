/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * pltm_clean.h - clean-room local tone-mapping module.
 *
 * Behaviour-only reimplementation of the local tone-mapping stage described in
 * spec/pltm.md.  The module prepares per-tile merge weights, a tone
 * floor table, a gain table and a handful of scalar knobs once per qualified
 * frame; it never touches image pixels.  The local-contrast-to-strength curve
 * bank and the convergence step bank arrive at runtime through the
 * table-provider contract declared here (freeisp_get_tables()); see
 * docs/provenance.md for the full provenance record.  A provider that yields
 * no PLTM table is a programming error and initialisation fails.
 *
 * Own names and structures; the spec's neutral names are used directly.
 */
#ifndef PLTM_CLEAN_H
#define PLTM_CLEAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Shapes and constants (spec 2, 3, 10)                                */
/* ------------------------------------------------------------------ */

#define PLTM_NTBL           768   /* result table: 3 sections of 256     */
#define PLTM_NLEVEL         256   /* level-axis entries per table        */
#define PLTM_NSTAT          768   /* per-tile statistics averaged        */
#define PLTM_STRENGTH_ROWS    4   /* injected strength bank rows         */
#define PLTM_STRENGTH_COLS  256   /* injected strength bank columns      */
#define PLTM_CONV_ROWS       32   /* injected convergence bank rows      */
#define PLTM_CONV_COLS      128   /* injected convergence bank columns   */
#define PLTM_NPRESET         19   /* quantised presets, 0..18            */

#define PLTM_STRENGTH_MAX  0x0FFF /* 12-bit strength ceiling             */
#define PLTM_Q10_UNITY     0x0400 /* gain-table unity                    */
#define PLTM_Q12_UNITY     0x1000 /* full-scale blend weight             */

#define PLTM_TONE_BASE     0x0100 /* tbl[0x100..0x1FF] tone floor        */
#define PLTM_GAIN_BASE     0x0200 /* tbl[0x200..0x2FF] gain              */
#define PLTM_MERGE_H_BASE  0x0000 /* tbl[0x000..0x07F] merge horizontal  */
#define PLTM_MERGE_V_BASE  0x0080 /* tbl[0x080..0x0FF] merge vertical    */
#define PLTM_MERGE_HALF     128   /* u16 per merge half                  */

#define PLTM_AUTO_THRESHOLD  17   /* below this, auto_strength is level  */
#define PLTM_SPEED_FRAME   0x0050 /* frame from which config.speed is used */

/* ------------------------------------------------------------------ */
/* Per-instance configuration (spec 3.1-3.4).  Read live every run.    */
/* ------------------------------------------------------------------ */

typedef struct pltm_clean_config {
    int             mode;                 /* 0 auto/fixed, 1 semi, else static */
    int             oripic_ratio_cfg;     /* manual original/picture ratio      */
    int             order_cfg;            /* manual transform order             */
    int             last_order_ratio_cfg; /* manual last-order ratio            */
    int             clip_cfg;             /* manual tone-floor increment        */
    int             gain_cfg;             /* manual gain blend factor           */
    int             lss_switch;           /* unused by this module              */
    int             lum_ratio;            /* unused                             */
    int             lp_halo_res;          /* unused                             */
    int             white_level;          /* unused                             */
    int             spatial_asm;          /* unused                             */
    int             intens_asym;          /* unused                             */
    int             block_rows;           /* vertical tile divisions minus one  */
    int             block_cols;           /* horizontal tile divisions minus one*/
    int             contrast;             /* contrast trim                      */
    int             tolerance;            /* minimum-level deadband (0 -> 5)    */
    int             speed;                /* converge row selector              */
    int             step;                 /* fixed strength step (0 -> 2)       */
    int             interval;             /* strength-frame interval (0 -> 1)   */
    int             enable;               /* non-zero allows the module to run  */
    int             frame_id;             /* monotonic frame counter            */
    int             auto_strength;        /* automatic strength selector/scale  */
    int             manual_strength;      /* semi-manual strength level         */
    int             ae_comp;              /* exposure compensation copy         */
    int             min_threshold;        /* minimum-level target override      */
    int             bit_offset;           /* WDR level shift                    */
    const uint16_t *source_curve;         /* >= 0x300 tuning curve entries      */
} pltm_clean_config_t;

/* Sensor descriptor (spec 3.3). */
typedef struct pltm_clean_sensor {
    int      width;         /* active width in pixels   */
    int      height;        /* active height in pixels  */
    unsigned wdr_mode;      /* 1 selects sparse tables  */
    uint8_t  ae_settled;    /* zero suspends the auto path */
    uint8_t  backlight;     /* present, not consumed    */
} pltm_clean_sensor_t;

typedef struct pltm_clean_params {
    pltm_clean_config_t config;
    pltm_clean_sensor_t sensor;
} pltm_clean_params_t;

/* ------------------------------------------------------------------ */
/* Per-frame statistics block (spec 3.6).                              */
/* ------------------------------------------------------------------ */

typedef struct pltm_clean_stats {
    uint16_t measured_min;    /* measured minimum level, 12-bit */
    uint16_t lst[PLTM_NSTAT]; /* per-tile local-contrast values */
} pltm_clean_stats_t;

/* ------------------------------------------------------------------ */
/* Per-frame result (spec 4).                                          */
/* ------------------------------------------------------------------ */

typedef struct pltm_clean_result {
    uint16_t tbl[PLTM_NTBL]; /* merge / tone floor / gain tables */
    int      oripic_ratio;
    int      order;
    int      last_order_ratio;
    int      cal_en;
    int      frame_smooth_en;
    int      block_width;
    int      block_height;
    uint32_t stat_scale;
    uint8_t  ae_comp;
    uint16_t old_strength;
    uint16_t next_strength;
    uint16_t cal_strength;
    uint16_t min_threshold;
} pltm_clean_result_t;

/* ------------------------------------------------------------------ */
/* Injected tables - supplied at runtime, never built in (spec 3.5).   */
/* ------------------------------------------------------------------ */

/* Preset columns (spec 7.8), one row per quantised strength step. */
#define PLTM_PRESET_BLEND  0   /* original-picture blend, 0..255          */
#define PLTM_PRESET_ORDER  1   /* pyramid order, 5..15                     */
#define PLTM_PRESET_CLIP   2   /* tone clip, Q12                           */
#define PLTM_PRESET_GAIN   3   /* gain blend, Q12                          */
#define PLTM_PRESET_COLS   4

typedef struct pltm_clean_tables {
    const int32_t *strength_bank; /* [PLTM_STRENGTH_ROWS][PLTM_STRENGTH_COLS] */
    const uint8_t *converge_bank; /* [PLTM_CONV_ROWS][PLTM_CONV_COLS]         */
    /* [PLTM_NPRESET][PLTM_PRESET_COLS]; extracted from the device's own
     * firmware at runtime (src/tables/pltm_presets.c).  NULL = neutral: every
     * step maps to full original blend, lowest order, no clip, unity gain,
     * i.e. local tone mapping has no effect. */
    const int32_t *presets;
} pltm_clean_tables_t;

typedef struct freeisp_tables {
    const pltm_clean_tables_t *pltm;
} freeisp_tables_t;

/* Provided by the framework; must be non-NULL before pltm_init(). */
const freeisp_tables_t *freeisp_get_tables(void);

/* ------------------------------------------------------------------ */
/* Entry points (interfaces.md / spec 4).                              */
/* ------------------------------------------------------------------ */

typedef struct pltm_entity pltm_entity_t;

/* set-parameters dispatch.  The deployed body is a no-op. */
enum {
    PLTM_PARAM_INIT = 0
};

typedef struct pltm_clean_param_req {
    int                       kind;
    const pltm_clean_params_t *params; /* optional */
} pltm_clean_param_req_t;

typedef struct pltm_ops {
    int (*get_params)(pltm_entity_t *e, pltm_clean_params_t **out);
    int (*set_params)(pltm_entity_t *e, const pltm_clean_param_req_t *req,
                      int *result);
    int (*run)(pltm_entity_t *e, const pltm_clean_stats_t *stats,
               pltm_clean_result_t *result);
    int (*set_default_result)(pltm_entity_t *e, pltm_clean_result_t *result);
} pltm_ops_t;

pltm_entity_t *pltm_init(pltm_ops_t *out_ops);
void           pltm_exit(pltm_entity_t *e);
int            pltm_get_params(pltm_entity_t *e, pltm_clean_params_t **out);
int            pltm_set_params(pltm_entity_t *e,
                               const pltm_clean_param_req_t *req, int *result);
int            pltm_run(pltm_entity_t *e, const pltm_clean_stats_t *stats,
                        pltm_clean_result_t *result);
int            pltm_set_default_result(pltm_entity_t *e,
                                       pltm_clean_result_t *result);

/* ------------------------------------------------------------------ */
/* Diagnostics (not part of the interop contract).                     */
/* ------------------------------------------------------------------ */

/* Module-global first-frame gate (spec 5/6): initial value 3. */
int  pltm_clean_get_start_frame(void);
void pltm_clean_set_start_frame(int frame);

/* Carried strength state (spec 5/6).  The clean core keeps the applied
 * (`old_strength`) and newly computed (`next_strength`) values on the entity
 * across frames; the SDK carries the same two values in pltm_result_t
 * (`pltm_old_stren`/`pltm_next_stren`).  An integration layer seeds this from
 * the SDK result before each run so the clean core reads the state the vendor
 * object reads, then publishes the result back afterwards. */
void pltm_clean_set_strength_state(pltm_entity_t *e, uint16_t old_strength,
                                   uint16_t next_strength);

/* Preset table query, index 0..18 (spec 7.8), from the table provider's
 * presets (neutral when it has none).  Same for the strength map below. */
void pltm_clean_preset(int index, int *oripic_ratio, int *order,
                       int *clip, int *gain);

/* Full strength -> (ratio, order, last-order, clip, gain) mapping used by the
 * strength path (spec 7.7), exposed so the interpolation and modulation can be
 * checked directly. */
void pltm_clean_strength_map(const pltm_clean_params_t *p, uint16_t strength,
                             int *oripic_ratio, int *order,
                             int *last_order_ratio, int *clip, int *gain);

/* Build the merge half-tables for explicit tile spans into a result's table
 * (diagnostic; spec 8.1).  nw/nh are the tile counts per axis; the per-frame
 * geometry path derives them from the sensor size and block counts. */
void pltm_clean_merge_build(pltm_clean_result_t *result, int nw, int nh);

#ifdef __cplusplus
}
#endif

#endif /* PLTM_CLEAN_H */
