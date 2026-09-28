/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* pltm_shim.h - public knob of the PLTM shim.
 * pltm_init()/pltm_exit() are declared by framework_isp.h. */
#ifndef PLTM_SHIM_H
#define PLTM_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* `tables` is an opaque pltm_clean_tables_t (pltm_clean.h would collide on
 * pltm_*); NULL = built-in defaults. Call before pltm_init(). */
void pltm_shim_set_tables(const void *tables);

#ifdef __cplusplus
}
#endif

#endif /* PLTM_SHIM_H */
