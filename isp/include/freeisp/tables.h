/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * freeisp - runtime-injected view of the compiled-in libisp constant tables.
 *
 * These tables are NOT compiled into freeisp. They are located inside the
 * stock on-camera `rmm` image at runtime (see rmm_tables.h) and handed to the
 * algorithm modules through this struct. Only `Ae_DeltaLvTbl` has writable
 * storage (the AE module rewrites it while building its tone curve); every other
 * pointer is const.
 *
 * Dimensions follow the target algorithm library's declarations for these
 * tables (ARMv7).
 */
#ifndef FREEISP_TABLES_H
#define FREEISP_TABLES_H

#include "freeisp/types.h"

typedef struct freeisp_tables {
  /* AE */
  const HW_S32        *Ae_Log2;              /* int[256] */
  HW_U32              *Ae_DeltaLvTbl;        /* unsigned int[1024]  (WRITABLE) */
  const HW_U8         *AeConverData;         /* unsigned char[32][128] */
  const HW_U32        *AeProbData;           /* unsigned int[6][12] */
  const HW_S32        *AeHistData;           /* int[20][31] */
  const HW_S16        *AeGammaPre;           /* short[11][256] */
  const HW_S32        *AeBackLightWeight;    /* int[3][64] */
  const HW_S32        *Ae_LumWeight_win;     /* int[64] */
  const HW_S32        *Ae_LumWeight_avg;     /* int[64] */
  const HW_S32        *Ae_LumWeight_center;  /* int[64] */
  const HW_S32        *Ae_OverExp_LumWeight; /* int[64] */
  const HW_S32        *Ae_UnderExp_LumWeight;/* int[64] */
  const HW_S32        *AeTblDef;             /* ae_table_info, 252 B */
  const HW_S32        *ae_fno_def;           /* int[16] */
  const HW_S32        *IW;                   /* int[640]  (10x64 backlight MLP) */
  const HW_S32        *LW;                   /* int[20] */
  const HW_S32        *b1;                   /* int[10] */
  /* AWB */
  const HW_U8         *AwbProbData;          /* unsigned char[96][256] */
  const HW_U16        *AwbSpeedData;         /* unsigned short[48][64] */
  const char          *AwbLightClassName;    /* char[10][32] */
  const HW_S32        *AwbStdTempWeight;     /* int[10] */
  const HW_S32        *AwbTempLvWeightDef;   /* int[10][10] */
  /* AFS */
  const HW_S32        *afs_cos;              /* int[32] */
  const HW_S32        *afs_sin;              /* int[32] */
  /* ISO (AF filter defaults block) */
  const HW_U8         *af_square_lut;        /* unsigned char[16] */
  /* ISO */
  const HW_U32        *TBL2GAIN;             /* const unsigned int[350] */
  const HW_S32        *iso_gain_point;       /* const HW_S32[14] */
  const HW_S32        *iso_lum_point;        /* const HW_S32[14] */
  /* PLTM */
  const HW_S32        *pltm_stren_tbl_buf;   /* int[4][256] */
  /* GTM */
  const HW_S16        *gd_curve_high;        /* HW_S16[256] */
  const HW_S16        *gd_curve_low;         /* HW_S16[256] */
  const HW_S16        *gd_curve_linear;      /* HW_S16[256] */
  /* base / module cfg */
  const HW_U16        *anti_gamma_table;     /* HW_U16[4096] */
  const HW_S32        *isp_cm_color_temp;    /* int[3] (reconstructed) */
  const void          *rgb2yuv_matrix;       /* isp_rgb2yuv_gain_offset[6] */
  const HW_U16        *lsc_trig_cfg_def;     /* HW_U16[6] */
  const HW_U16        *msc_trig_cfg_def;     /* HW_U16[6] */
  const void          *module_attrs;         /* isp_module_attribute[31] (rebuilt) */
} freeisp_tables_t;

#endif /* FREEISP_TABLES_H */
