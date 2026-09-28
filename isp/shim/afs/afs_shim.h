/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* afs_shim.h - public knobs of the AFS (auto-flicker suppression) shim.
 * afs_init()/afs_exit() are declared by framework_isp.h. */
#ifndef AFS_SHIM_H
#define AFS_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration: the shim does not expose the SDK ABI here. */
struct fwi_isp_ctx;

/* `trig` is an opaque afs_clean_trig_t (sin/cos int[32]); NULL = analytic
 * placeholder. Call before afs_init(): the core latches the pointer there. */
void afs_shim_set_trig_tables(const void *trig);

/* Project ctx onto the param mirror (docs/shim-mappings.md#afs; flicker_mode
 * excluded) and keep ctx for writeback; NULL detaches. Call after afs_init(). */
void afs_shim_update_cfg(void *obj, struct fwi_isp_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* AFS_SHIM_H */
