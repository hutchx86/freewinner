/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * afs_shim.h - public knobs of the AFS (auto-flicker suppression) shim.
 *
 * The entry points afs_init()/afs_exit() (the fwi_afs_core_ops_t vtable) are declared
 * by framework_isp.h and implemented by afs_shim.c.
 */
#ifndef AFS_SHIM_H
#define AFS_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration: the shim does not expose the SDK ABI here. */
struct fwi_isp_ctx;

/*
 * Install the runtime rotation tables used by the clean-room AFS core.
 *
 * `trig` is a pointer to an afs_clean_trig_t (two const int[32] tables, one
 * sine period and one cosine period).  The type is kept opaque here on
 * purpose: this header is included next to the SDK ABI headers, and pulling
 * in afs_clean.h would expose the clean typedefs to SDK-visible code.
 *
 * Pass NULL to fall back to the built-in pilot defaults (an analytic
 * round(100*sin/cos) placeholder; see afs_shim.c).  Must be called before
 * afs_init(): the clean core latches the table pointer during its own
 * initialisation.
 */
void afs_shim_set_trig_tables(const void *trig);

/*
 * Map the SDK tuning/config corpus into the shim's Afs parameter block.
 *
 * The framework derives the algorithm's afs_param_t from the shared context in
 * two steps: the vendor AFS context-update step copies the tuning/test corpus
 * (flicker_ratio, flicker_type, isp_test_mode, afs_en), and the vendor
 * per-frame AFS parameter step copies module_cfg.isp_platform_id, af_frame_cnt
 * and the whole sensor_info descriptor before each frame.  This helper
 * performs both copies against the embedded mirror and re-publishes it to the
 * clean core, so an integration that has only the isp_lib_context can drive
 * the clean AFS core exactly as the framework would.
 *
 * `ctx` is retained so the shim can publish the result the way the vendor AFS
 * run stage does (ae_settings.flicker_type).  Pass NULL to detach.
 *
 * NOTE: the vendor's AFS context-update step never assigns
 * afs_param->flicker_mode (checked in both the deployed v521/v316 and the
 * v833 sources); flicker_mode is therefore left untouched here and must be
 * supplied by the integration through the get-params mirror, exactly as the
 * framework leaves it.
 *
 * Must be called after afs_init() (it writes the instance mirror).
 */
void afs_shim_update_cfg(void *obj, struct fwi_isp_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* AFS_SHIM_H */
