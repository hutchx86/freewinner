/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* base_shim.h - SDK-ABI entry points of the clean isp_base tier. The SDK
 * context is forward-declared only, keeping its tag collisions out. */
#ifndef BASE_SHIM_H
#define BASE_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

struct fwi_isp_ctx;

/* ------------------------------------------------------------------ */
/* SDK entry points (signatures from libisp/include/isp_base.h)        */
/* ------------------------------------------------------------------ */

void config_blc(struct fwi_isp_ctx *isp_gen);
void config_dig_gain(struct fwi_isp_ctx *isp_gen,
                     unsigned int exp_digital_gain);
void config_wdr(struct fwi_isp_ctx *isp_gen, int flag);
void config_gamma(struct fwi_isp_ctx *isp_gen);
void config_lens_table(struct fwi_isp_ctx *isp_gen, int vcm_std_pos);
void config_msc_table(struct fwi_isp_ctx *isp_gen, int vcm_std_pos);
void config_lens_center(struct fwi_isp_ctx *isp_gen);
void config_band_step(struct fwi_isp_ctx *isp_gen);
void isp_handle_stats(struct fwi_isp_ctx *isp_gen, const void *buffer);
void isp_handle_stats_sync(struct fwi_isp_ctx *isp_gen,
                           const void *buffer0, const void *buffer1);
void isp_apply_colormatrix(struct fwi_isp_ctx *isp_gen);
void isp_apply_settings(struct fwi_isp_ctx *isp_gen);
void __isp_stat_dynamic_judge(struct fwi_isp_ctx *isp_gen);

/* Table provider knobs. set_tables: install a full base_tables_t, bypassing
 * the per-call build. set_locator: vendor defaults with no tuning source (NULL = unset). */
void base_shim_set_tables(const void *tables);
void base_shim_set_locator(const void *anti_gamma_table,
                           const void *rgb2yuv_matrix,
                           const void *lsc_trig_cfg_def,
                           const void *msc_trig_cfg_def,
                           const void *isp_cm_color_temp);

#ifdef __cplusplus
}
#endif

#endif /* BASE_SHIM_H */
