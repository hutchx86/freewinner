/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * iso_shim.h - public knob of the ISO pilot shim.
 *
 * The entry points iso_init()/iso_exit() (the fwi_iso_cfg_core_ops_t vtable) are declared
 * by framework_isp.h and implemented by iso_shim.c.
 */
#ifndef ISO_SHIM_H
#define ISO_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Install the runtime ISO tables used by the clean-room core.
 *
 * `tables` is a pointer to a const iso_clean_tables_t.  It is kept opaque here
 * on purpose: this header is included next to the SDK ABI headers, and pulling
 * in iso_clean.h would collide on iso_result_t.
 *
 * Pass NULL to fall back to the built-in pilot defaults.
 * Must be called before iso_init() (the clean core latches the table pointer
 * during its own initialisation).
 */
void iso_shim_set_tables(const void *tables);

#ifdef __cplusplus
}
#endif

#endif /* ISO_SHIM_H */
