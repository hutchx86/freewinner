/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* iso_shim.h - table knob of the ISO shim; iso_init()/iso_exit() are declared
 * by framework_isp.h. */
#ifndef ISO_SHIM_H
#define ISO_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Install a const iso_clean_tables_t (opaque: iso_clean.h would collide on
 * iso_result_t); NULL = placeholder defaults. Call before iso_init(). */
void iso_shim_set_tables(const void *tables);

#ifdef __cplusplus
}
#endif

#endif /* ISO_SHIM_H */
