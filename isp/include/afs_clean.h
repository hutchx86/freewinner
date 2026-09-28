/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* afs_clean.h - auto-flicker (mains-ripple) detection (behaviour per isp/spec/afs.md).
 * Per-instance state is opaque. The two 32-entry trig tables come from the table
 * provider at init; a provider with no tables makes init fail. */
#ifndef AFS_CLEAN_H
#define AFS_CLEAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AFS_CLEAN_NCOL  128
#define AFS_CLEAN_NROW  16
#define AFS_CLEAN_NBIN  16
#define AFS_CLEAN_NTRIG 32

/* Mains type written to the result. The spec's algorithm steps use the
 * literal 50/60 values; none is 0. */
typedef enum afs_clean_type {
    AFS_CLEAN_TYPE_NONE = 0,
    AFS_CLEAN_TYPE_HZ50 = 50,
    AFS_CLEAN_TYPE_HZ60 = 60
} afs_clean_type_t;

typedef enum afs_clean_mode {
    AFS_CLEAN_MODE_OFF     = 0,
    AFS_CLEAN_MODE_FORCE50 = 1,
    AFS_CLEAN_MODE_FORCE60 = 2,
    AFS_CLEAN_MODE_AUTO    = 3
} afs_clean_mode_t;

typedef enum afs_clean_seed {
    AFS_CLEAN_SEED_ALTERNATE = 0,
    AFS_CLEAN_SEED_PREFER50  = 1,
    AFS_CLEAN_SEED_PREFER60  = 2
} afs_clean_seed_t;

/* Per-instance parameter block. Written by the caller through get-params. */
typedef struct afs_clean_params {
    int           frame_index;    /* monotonic frame counter; <3 ignored */
    int           mode;           /* afs_clean_mode_t */
    int           seed_type;      /* afs_clean_seed_t; 0/1/2 significant */
    int           min_peak_ratio; /* strict energy-share threshold, percent */
    int           enable;         /* non-zero allows detection */
    int           gain_level;     /* accepted only in 255..257 */
    int           image_width;    /* normaliser for raw column sums; >0 */
    unsigned char param_kind;     /* unused */
    int           platform_id;    /* unused */
    int           auto_flag_param;/* unused */
} afs_clean_params_t;

/* Per-frame hardware statistics block. Exactly 128 column sums are read. */
typedef struct afs_clean_stats {
    const unsigned int *column_sum;
    unsigned int        stats_width;  /* unused */
    unsigned int        stats_height; /* unused */
} afs_clean_stats_t;

/* Per-frame result. */
typedef struct afs_clean_result {
    int detected_type; /* afs_clean_type_t */
} afs_clean_result_t;

/* Table-provider contract: two independent 32-entry integer tables. */
typedef struct afs_clean_trig {
    const int *sine;   /* 32 entries, one period */
    const int *cosine; /* 32 entries, one period */
} afs_clean_trig_t;

typedef const afs_clean_trig_t *(*afs_clean_trig_provider_fn)(void);

typedef struct afs_clean_state afs_clean_state_t;

/* Operations vtable returned by init. */
typedef struct afs_clean_ops {
    void (*exit)(afs_clean_state_t *self);
    int  (*get_params)(afs_clean_state_t *self, afs_clean_params_t **out);
    int  (*set_params)(afs_clean_state_t *self, const afs_clean_params_t *in,
                       int *result);
    int  (*run)(afs_clean_state_t *self, const afs_clean_stats_t *stats,
                afs_clean_result_t *result);
    int  (*isr)(afs_clean_state_t *self, const afs_clean_stats_t *stats,
                afs_clean_result_t *result);
} afs_clean_ops_t;

/* Register the process-wide table provider. Must be set before init. */
void afs_clean_set_trig_provider(afs_clean_trig_provider_fn provider);
afs_clean_trig_provider_fn afs_clean_get_trig_provider(void);

/* Allocate per-instance state and return the ops vtable. Returns NULL (and
 * *out_ops == NULL) when no valid trig tables are available or on OOM. */
afs_clean_state_t *afs_clean_init(const afs_clean_ops_t **out_ops);

void afs_clean_exit(afs_clean_state_t *self);

/* Returns 0 and stores the instance parameter pointer, or -1 if self is NULL. */
int afs_clean_get_params(afs_clean_state_t *self, afs_clean_params_t **out);

/* Always returns 0 and performs no work. */
int afs_clean_set_params(afs_clean_state_t *self, const afs_clean_params_t *in,
                         int *result);

/* Per-frame handler. Returns 0 normally, -1 when self or stats is NULL (in
 * which case detected_type is set to 50). result must be non-NULL. */
int afs_clean_run(afs_clean_state_t *self, const afs_clean_stats_t *stats,
                  afs_clean_result_t *result);

/* Interrupt-shaped entry point; forwards to afs_clean_run. */
int afs_clean_isr(afs_clean_state_t *self, const afs_clean_stats_t *stats,
                  afs_clean_result_t *result);

/* Diagnostics only: rows captured in the current (possibly partial) set. */
int afs_clean_rows_filled(const afs_clean_state_t *self);

#ifdef __cplusplus
}
#endif

#endif /* AFS_CLEAN_H */
