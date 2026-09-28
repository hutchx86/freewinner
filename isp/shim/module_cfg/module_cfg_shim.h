/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* module_cfg_shim.h - SDK-ABI entry points of the clean module_cfg dispatcher.
 * The SDK struct is forward-declared only, keeping its collisions out. */
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
