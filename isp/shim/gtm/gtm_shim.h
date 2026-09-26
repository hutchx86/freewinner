/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * gtm_shim.h - public knob of the GTM shim.
 *
 * The entry points gtm_init()/gtm_exit() (the fwi_gtm_core_ops_t vtable) are declared
 * by framework_isp.h and implemented by gtm_shim.c.
 */
#ifndef GTM_SHIM_H
#define GTM_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Install the runtime GTM tables used by the clean-room core.
 *
 * `tables` is a pointer to a const gtm_clean_tables_t.  It is kept opaque here
 * on purpose: this header is included next to the SDK ABI headers, and pulling
 * in gtm_clean.h would collide on gtm_init/gtm_exit.
 *
 * Pass NULL to fall back to the built-in pilot defaults.
 * Must be called before gtm_init() (the clean core latches the table pointer
 * during its own initialisation).
 */
void gtm_shim_set_tables(const void *tables);

#ifdef __cplusplus
}
#endif

#endif /* GTM_SHIM_H */
