/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* gtm_shim.h - public knob of the GTM shim.
 * gtm_init()/gtm_exit() are declared by framework_isp.h. */
#ifndef GTM_SHIM_H
#define GTM_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* `tables` is an opaque gtm_clean_tables_t (gtm_clean.h would collide on gtm_*);
 * NULL = built-in defaults. Call before gtm_init(): the core latches it there. */
void gtm_shim_set_tables(const void *tables);

#ifdef __cplusplus
}
#endif

#endif /* GTM_SHIM_H */
