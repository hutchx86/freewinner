/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * base_shim.h - public ABI of the isp_base-tier integration shim.
 *
 * The shim presents the clean-room base tier (src/reg/base.c) to the
 * ISP framework through its entry points and `struct fwi_isp_ctx` (the
 * generated fwi_* ABI).
 *
 * Only a forward declaration of the SDK context is needed so this header can be
 * included next to the clean-room headers without pulling the SDK struct
 * definition (and its tag/macro collisions) into the same translation unit.
 */
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

/* ------------------------------------------------------------------ */
/* Table provider knobs                                                */
/* ------------------------------------------------------------------ */
/*
 * The clean base tier never compiles tuning data in; it reads every table
 * through freeisp_get_tables()->base.  This shim builds that `base_tables_t`
 * from the runtime tuning carried in the SDK context it is handed on each
 * entry, so the provider is refreshed before the clean routine reads it.
 *
 * Two knobs, mirroring the 3A shim setter pattern:
 *
 *   base_shim_set_tables()   install a complete table set, bypassing the
 *                            per-call build (used by tests / a future caller
 *                            that already holds a base_tables_t).
 *
 *   base_shim_set_locator()  install the five compiled-in defaults that have
 *                            no runtime-tuning source, taken from the located
 *                            vendor table set (freeisp/tables.h):
 *                            anti_gamma_table, rgb2yuv_matrix,
 *                            lsc_trig_cfg_def, msc_trig_cfg_def and
 *                            isp_cm_color_temp.  `NULL` for any argument
 *                            leaves that default unset.
 */
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
