/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* Offsets-only layout of the freeisp tables: no table bytes are compiled in. */
#ifndef FREEISP_TABLES_LAYOUT_H
#define FREEISP_TABLES_LAYOUT_H

#include <stddef.h>
#include "freeisp/tables.h"

typedef struct {
  size_t       off;      /* offsetof(freeisp_tables_t, field) */
  unsigned char cluster; /* 0 = read-only data (Ae_Log2 anchor), 1 = writable data */
  int          delta;    /* bytes from the cluster anchor */
  unsigned int size;     /* table size in bytes */
  unsigned char writable;/* needs an owned writable copy */
  const char  *name;
} freeisp_table_loc_t;

static const freeisp_table_loc_t freeisp_table_locs[] = {
  { offsetof(freeisp_tables_t, AeBackLightWeight), 0, -768, 768u, 0, "AeBackLightWeight" },
  { offsetof(freeisp_tables_t, AeConverData), 0, 25684, 4096u, 0, "AeConverData" },
  { offsetof(freeisp_tables_t, AeGammaPre), 0, 70832, 5632u, 0, "AeGammaPre" },
  { offsetof(freeisp_tables_t, AeHistData), 1, 8692, 2480u, 0, "AeHistData" },
  { offsetof(freeisp_tables_t, AeProbData), 0, -3736, 288u, 0, "AeProbData" },
  { offsetof(freeisp_tables_t, AeTblDef), 1, -19804, 252u, 0, "AeTblDef" },
  { offsetof(freeisp_tables_t, Ae_DeltaLvTbl), 1, -19552, 4096u, 1, "Ae_DeltaLvTbl" },
  { offsetof(freeisp_tables_t, Ae_Log2), 0, 0, 1024u, 0, "Ae_Log2" },
  { offsetof(freeisp_tables_t, Ae_LumWeight_avg), 1, -20212, 256u, 0, "Ae_LumWeight_avg" },
  { offsetof(freeisp_tables_t, Ae_LumWeight_center), 1, -20064, 256u, 0, "Ae_LumWeight_center" },
  { offsetof(freeisp_tables_t, Ae_LumWeight_win), 1, -20576, 256u, 0, "Ae_LumWeight_win" },
  { offsetof(freeisp_tables_t, Ae_OverExp_LumWeight), 1, -21152, 256u, 0, "Ae_OverExp_LumWeight" },
  { offsetof(freeisp_tables_t, Ae_UnderExp_LumWeight), 1, -20896, 256u, 0, "Ae_UnderExp_LumWeight" },
  { offsetof(freeisp_tables_t, AwbLightClassName), 1, 0, 320u, 0, "AwbLightClassName" },
  { offsetof(freeisp_tables_t, AwbProbData), 0, 1108, 24576u, 0, "AwbProbData" },
  { offsetof(freeisp_tables_t, AwbSpeedData), 0, 35312, 6144u, 0, "AwbSpeedData" },
  { offsetof(freeisp_tables_t, AwbStdTempWeight), 0, 34808, 40u, 0, "AwbStdTempWeight" },
  { offsetof(freeisp_tables_t, AwbTempLvWeightDef), 0, 34848, 400u, 0, "AwbTempLvWeightDef" },
  { offsetof(freeisp_tables_t, IW), 0, -3408, 2560u, 0, "IW" },
  { offsetof(freeisp_tables_t, LW), 0, -848, 80u, 0, "LW" },
  { offsetof(freeisp_tables_t, TBL2GAIN), 0, 81584, 1400u, 0, "TBL2GAIN" },
  { offsetof(freeisp_tables_t, ae_fno_def), 1, -20640, 64u, 0, "ae_fno_def" },
  { offsetof(freeisp_tables_t, af_square_lut), 0, 81436, 16u, 0, "af_square_lut" },
  { offsetof(freeisp_tables_t, afs_cos), 0, 34348, 128u, 0, "afs_cos" },
  { offsetof(freeisp_tables_t, afs_sin), 0, 34220, 128u, 0, "afs_sin" },
  { offsetof(freeisp_tables_t, anti_gamma_table), 1, 332, 8192u, 0, "anti_gamma_table" },
  { offsetof(freeisp_tables_t, b1), 0, -3448, 40u, 0, "b1" },
  { offsetof(freeisp_tables_t, gd_curve_high), 1, 11172, 512u, 0, "gd_curve_high" },
  { offsetof(freeisp_tables_t, gd_curve_linear), 1, 12196, 512u, 0, "gd_curve_linear" },
  { offsetof(freeisp_tables_t, gd_curve_low), 1, 11684, 512u, 0, "gd_curve_low" },
  { offsetof(freeisp_tables_t, iso_gain_point), 0, 81528, 56u, 0, "iso_gain_point" },
  { offsetof(freeisp_tables_t, iso_lum_point), 0, 81472, 56u, 0, "iso_lum_point" },
  { offsetof(freeisp_tables_t, lsc_trig_cfg_def), 1, 8668, 12u, 0, "lsc_trig_cfg_def" },
  { offsetof(freeisp_tables_t, msc_trig_cfg_def), 1, 8668, 12u, 0, "msc_trig_cfg_def" },
  { offsetof(freeisp_tables_t, pltm_stren_tbl_buf), 1, 12900, 4096u, 0, "pltm_stren_tbl_buf" },
  { offsetof(freeisp_tables_t, rgb2yuv_matrix), 1, 8524, 144u, 0, "rgb2yuv_matrix" },
};

#define FREEISP_TABLE_LOC_COUNT (sizeof(freeisp_table_locs) / sizeof(freeisp_table_locs[0]))

#endif /* FREEISP_TABLES_LAYOUT_H */
