/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * ae_shim.h - public knobs of the AE (auto-exposure) shim.
 *
 * The entry points ae_init()/ae_exit() (the fwi_ae_core_ops_t vtable) are declared
 * by framework_isp.h and implemented by ae_shim.c.
 */
#ifndef AE_SHIM_H
#define AE_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Install the runtime AE tables used by the clean-room core.
 *
 * `tables` is a pointer to a const ae_clean_tables_t.  It is kept opaque here
 * on purpose: this header is included next to the SDK ABI headers, and pulling
 * in ae_clean.h would expose the clean typedefs (and collide on ae_stats_t /
 * ae_result_t) to SDK-visible code.
 *
 * Pass NULL to fall back to the built-in pilot defaults.
 * Must be called before ae_init() (the clean core latches the table pointer
 * during its own initialisation).
 */
void ae_shim_set_tables(const void *tables);

#ifdef __cplusplus
}
#endif

#endif /* AE_SHIM_H */
