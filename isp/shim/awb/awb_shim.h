/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* awb_shim.h - public knobs of the AWB shim.
 * awb_init()/awb_exit() are declared by framework_isp.h. */
#ifndef AWB_SHIM_H
#define AWB_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* `tables` is an opaque awb_clean_tables_t (awb_clean.h would collide on awb_*);
 * NULL = built-in defaults. Call before awb_init(): the core latches it there. */
void awb_shim_set_tables(const void *tables);

/* No-statistics fallback gains (r, gr, gb, b) for the built-in table; no
 * vendor table carries them, default unity. */
void awb_shim_set_safe_gain(const unsigned short gain[4]);

#ifdef __cplusplus
}
#endif

#endif /* AWB_SHIM_H */
