/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * module_cfg_shim.h - public ABI of the isp_module_cfg integration shim.
 *
 * The shim presents the clean-room register tier's dispatcher
 * (src/reg/module_cfg.c) to the Yi/mediad ISP framework through the
 * SDK's own `struct fwi_hw_module_cfg` and its two entry points
 * `isp_hardware_update()` / `isp_map_addr()`.
 *
 * The declarations below use the SDK struct tag on purpose: they are the
 * framework-visible ABI (the generated fwi_hw_module_cfg record).  Only a forward declaration is needed
 * so this header can be included next to the clean-room headers without
 * pulling the SDK struct definition (and its tag/macro collisions) into the
 * same translation unit.
 */
#ifndef MODULE_CFG_SHIM_H
#define MODULE_CFG_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

struct fwi_hw_module_cfg;

/* Per-frame dispatch: config+enable each module, then force a full table load. */
void isp_hardware_update(struct fwi_hw_module_cfg *cfg);

/* Bind the instance register base into the SDK-side register writers. */
void isp_map_addr(struct fwi_hw_module_cfg *cfg, unsigned long vaddr);

#ifdef __cplusplus
}
#endif

#endif /* MODULE_CFG_SHIM_H */
