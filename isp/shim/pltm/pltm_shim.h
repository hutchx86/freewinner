/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * pltm_shim.h - public knob of the PLTM pilot shim.
 *
 * The entry points pltm_init()/pltm_exit() (the fwi_pltm_core_ops_t vtable) are declared
 * by framework_isp.h and implemented by pltm_shim.c.
 */
#ifndef PLTM_SHIM_H
#define PLTM_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Install the runtime PLTM tables used by the clean-room core: the
 * local-contrast-to-strength curve bank (4x256 int32) and the convergence
 * step bank (32x128 uint8).
 *
 * `tables` is a pointer to a const pltm_clean_tables_t.  It is kept opaque
 * here on purpose: this header is included next to the SDK ABI headers, and
 * pulling in pltm_clean.h would collide on the pltm_* entry-point names.
 *
 * Pass NULL to fall back to the built-in pilot defaults.
 * Must be called before pltm_init() (the clean core latches the table pointer
 * during its own initialisation).
 */
void pltm_shim_set_tables(const void *tables);

#ifdef __cplusplus
}
#endif

#endif /* PLTM_SHIM_H */
