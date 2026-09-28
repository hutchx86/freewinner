/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* ae_clean.h - auto-exposure module (behaviour per isp/spec/ae.md). No tuning data
 * is compiled in: every table arrives at runtime via freeisp_get_tables(). */
#ifndef AE_CLEAN_H
#define AE_CLEAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AE_NWIN        384   /* metered windows, normal mode (16x24) */
#define AE_NWIN_WDR    192   /* metered windows, WDR mode (8x24)     */
#define AE_NGRID        64   /* 8x8 spatial weight grid              */
#define AE_NHIST       256   /* histogram bins / luma levels         */
#define AE_IDX_MAX    1024   /* entries per line table               */
#define AE_NSEG         10   /* segments per exposure descriptor     */
#define AE_NSCENE       16   /* scene descriptor slots               */

/* ------------------------------------------------------------------ */
/* Exposure descriptor (spec 3.3.1, 15.11)                            */
/* ------------------------------------------------------------------ */

typedef struct ae_segment {
    uint32_t min_exp;
    uint32_t max_exp;
    uint32_t min_gain;
    uint32_t max_gain;
    uint32_t min_iris;
    uint32_t max_iris;
} ae_segment_t;

typedef struct ae_desc {
    ae_segment_t seg[AE_NSEG];
    uint32_t     length;
    uint32_t     ev_step;       /* copied, never consumed            */
    uint32_t     shutter_shift; /* scales the reciprocal exposure    */
} ae_desc_t;

/* ------------------------------------------------------------------ */
/* Setting / output shapes (spec 4)                                   */
/* ------------------------------------------------------------------ */

typedef struct ae_setting {
    int32_t exposure_time;      /* microseconds                      */
    int32_t sensor_exp_line;    /* Q4 line count                     */
    int32_t total_gain;
    int32_t analog_gain;        /* Q4                                */
    int32_t digital_gain;       /* Q10                               */
    int32_t f_number;
    int32_t fno2;               /* squared aperture                  */
    int32_t lv;
    int32_t index;              /* line-table index                  */
    int32_t ev_idx;             /* index stored by the line builder  */
    int32_t ev;                 /* per-entry EV (line builder scratch) */
} ae_setting_t;

typedef struct ae_roi {
    int32_t x1, y1, x2, y2;     /* 0..2000 coordinate space          */
} ae_roi_t;

typedef struct ae_wdr_ratio {
    int32_t sensor;
    int32_t hw_ratio;
    int32_t tmp;
    int32_t last;
} ae_wdr_ratio_t;

/* ------------------------------------------------------------------ */
/* Parameter block (spec 3.2/3.3/3.4/3.5)                             */
/* ------------------------------------------------------------------ */

typedef struct ae_params {
    /* --- runtime settings, read each frame --- */
    int32_t  ev_bias;
    int32_t  fixed_exposure;        /* us                                  */
    int32_t  sensor_gain;           /* Q4                                   */
    int32_t  iso_sensitivity;
    int32_t  aperture;
    int32_t  exposure_mode;         /* 0 auto,1 manual,2 shutter,3 aperture */
    int32_t  iso_control;           /* 0 manual, 1 auto                     */
    int32_t  light_mode;            /* 0 normal,1 high,2 low                */
    int32_t  metering_mode;         /* 0 avg,1 center,2 spot,3 matrix       */
    int32_t  hdr_mode;              /* 0 normal, 1 WDR                      */
    int32_t  mains_detected;        /* 0 none,1 50Hz,2 60Hz                 */
    int32_t  scene;
    int32_t  flash_mode;
    ae_roi_t meter_roi;
    int32_t  exposure_locked;
    int32_t  flash_open;
    int32_t  capture_stage;
    int32_t  target_comp;           /* section 11 runtime target offset  */
    int32_t  exposure_cfg[14];

    /* --- static configuration --- */
    int32_t  table_source;          /* 0 default,1 scene,2 copy slot 15     */
    int32_t  max_lv;
    int32_t  window_weight_seed[AE_NGRID];
    int32_t  hist_metering_en;
    int32_t  kernel_index;
    int32_t  error_delay_frames;
    int32_t  exp_delay_frames;
    int32_t  gain_delay_frames;
    int32_t  ev_comp_step;
    int32_t  touch_distance_index;
    int32_t  high_fps_handling_en;
    int32_t  iso_to_gain_ratio;
    int32_t  aperture_ladder[16];
    int32_t  wdr_cfg[4];            /* base ratio, lo, hi, enable           */
    int32_t  total_gain_range[2];
    int32_t  analog_gain_range[2];
    int32_t  digital_gain_range[2];
    ae_desc_t scene_tables[AE_NSCENE];
    double   gain_split_ratio;

    /* --- sensor descriptor --- */
    int32_t  pclk;
    int32_t  hts;
    int32_t  vts;
    int32_t  frame_time;
    int32_t  gain_min;
    int32_t  gain_max;
    int32_t  sensor_width;
    int32_t  sensor_height;
    int32_t  hflip;
    int32_t  vflip;

    /* --- test / diagnostic configuration --- */
    int32_t  test_enable;
    int32_t  test_forced;
    int32_t  test_gain;
    int32_t  test_exp_line;
    int32_t  lum_forced;
    int32_t  test_exptime;
    int32_t  exp_line_start;
    int32_t  exp_line_step;
    int32_t  exp_line_end;
    int32_t  exp_change_interval;
    int32_t  test_gain_en;
    int32_t  gain_start;
    int32_t  gain_step;
    int32_t  gain_end;
    int32_t  gain_change_interval;
    int32_t  delay_en;
    int32_t  delay_type;
} ae_params_t;

/* ------------------------------------------------------------------ */
/* Statistics block (spec 3.6)                                        */
/* ------------------------------------------------------------------ */

typedef struct ae_stats {
    uint32_t win_avg[AE_NWIN];
    uint32_t hist[AE_NHIST];
    uint32_t accum_r[AE_NWIN];
    uint32_t accum_g[AE_NWIN];
    uint32_t accum_b[AE_NWIN];
    int32_t  win_pix_n;             /* unused */
} ae_stats_t;

/* ------------------------------------------------------------------ */
/* Result block (spec 4)                                              */
/* ------------------------------------------------------------------ */

typedef struct ae_result {
    int32_t      status;            /* 0 idle,1 busy,2 done             */
    ae_setting_t setting;
    ae_setting_t setting_last;
    ae_setting_t setting_curr;
    ae_setting_t setting_short;
    ae_setting_t setting_short_curr;
    int32_t      idx_max;
    int32_t      idx_expect;
    int32_t      bright_pos;
    int32_t      dark_pos;
    int32_t      gain_ratio_out;    /* Q8, clamp 64..640                */
    int32_t      target;
    int32_t      avg_lum;
    int32_t      weight_lum;
    int32_t      delta_idx;
    int32_t      lv_adj;
    int32_t      flash_ev_cumul;
    ae_wdr_ratio_t wdr_ratio;
    int32_t      wdr_hi_th;
    int32_t      wdr_low_th;
    int32_t      hist_low;
    int32_t      hist_mid;
    int32_t      hist_hi;
    int32_t      backlight;         /* 0..64                            */
    double       gain_ratio;        /* copy of gain_split_ratio         */
} ae_result_t;

/* ------------------------------------------------------------------ */
/* Injected tables (spec 15) - all supplied at runtime, never built in */
/* ------------------------------------------------------------------ */

typedef struct ae_clean_tables {
    const int32_t    *log2;         /* 15.1  256                          */
    uint32_t         *evtab;        /* 15.2  1024, writable               */
    const uint8_t    *conv;         /* 15.3  32*128                       */
    const uint32_t   *touchprob;    /* 15.4  6*12                         */
    const int32_t    *kernel;       /* 15.5  20*31                        */
    const int16_t    *pregamma;     /* 15.6  11*256 (unused by AE)        */
    const int32_t    *blmask;       /* 15.7  3*64                         */
    const int32_t    *wght_matrix;  /* 15.8  64                           */
    const int32_t    *wght_avg;     /* 15.8  64                           */
    const int32_t    *wght_center;  /* 15.8  64                           */
    const int32_t    *wght_over;    /* 15.8  64                           */
    const int32_t    *wght_under;   /* 15.8  64                           */
    const int32_t    *net_in;       /* 15.9  10*64                        */
    const int32_t    *net_bias;     /* 15.9  10                           */
    const int32_t    *net_out;      /* 15.9  20                           */
    const int32_t    *fno_ladder;   /* 15.10 16                           */
    const int32_t    *fno_def;      /* 15.10 16, injected default ladder  */
                                    /* (vendor ae_fno_def); falls back to */
                                    /* fno_ladder when NULL               */
    const ae_desc_t  *table_default;/* 15.11 descriptor                   */
    const uint8_t    *auxprob;      /* 15.12 96*256                       */
    const int32_t    *net_out_bias; /* 15.9  2 (B0, B1); NULL = net off   */
} ae_clean_tables_t;

typedef struct freeisp_tables {
    const ae_clean_tables_t *ae;
} freeisp_tables_t;

/* Provided by the framework; must be non-NULL before ae_init(). */
const freeisp_tables_t *freeisp_get_tables(void);

/* ------------------------------------------------------------------ */
/* Entry points (interfaces.md)                                       */
/* ------------------------------------------------------------------ */

typedef struct ae_entity ae_entity_t;

/* set-parameters dispatch (spec 3.1) */
enum {
    AE_PARAM_INIT      = 0,   /* (re)initialise from configuration   */
    AE_PARAM_TABLES    = 1,   /* exposure tables updated             */
    AE_PARAM_SET_INDEX = 2,   /* explicit exposure index             */
    AE_PARAM_TOUCH     = 3    /* touch region changed                */
};

typedef struct ae_param_req {
    int32_t             kind;
    int32_t             line_index;   /* for AE_PARAM_SET_INDEX        */
    const ae_params_t  *params;       /* for AE_PARAM_INIT             */
} ae_param_req_t;

typedef struct ae_ops {
    int (*get_params)(ae_entity_t *e, ae_params_t *out);
    int (*set_params)(ae_entity_t *e, const ae_param_req_t *req, ae_result_t *result);
    int (*run)(ae_entity_t *e, const ae_stats_t *stats, ae_result_t *result);
    int (*isr)(ae_entity_t *e, const ae_stats_t *stats, ae_result_t *result);
} ae_ops_t;

ae_entity_t *ae_init(ae_ops_t *out_ops);
void         ae_exit(ae_entity_t *e);
int          ae_get_params(ae_entity_t *e, ae_params_t *out);
int          ae_set_params(ae_entity_t *e, const ae_param_req_t *req, ae_result_t *result);
int          ae_run(ae_entity_t *e, const ae_stats_t *stats, ae_result_t *result);
int          ae_isr(ae_entity_t *e, const ae_stats_t *stats, ae_result_t *result);

/* Section 12 helper; not called by the per-frame entry point. */
int          ae_flash_strength(const ae_entity_t *e, int32_t flash_expect_lum);

/* Override the internal frame counter; the shim applies the SDK's frame id here
 * each frame so the core tracks the framework's count exactly. */
void         ae_set_frame_index(ae_entity_t *e, int32_t index);

/* Replay the framework WDR step's in-place rewrite of the blend ratios (the next run
 * reads them) into the core's own record before each run; own values = no-op, NULL ignored. */
void         ae_apply_wdr_feedback(ae_entity_t *e, int32_t sensor,
                                   int32_t hw_ratio, int32_t tmp, int32_t last);

/* Default-result initialiser (spec 17). */
void         ae_default_result(ae_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* AE_CLEAN_H */
