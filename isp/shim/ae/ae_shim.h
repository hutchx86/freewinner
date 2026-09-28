/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* ae_shim.h - table knob of the AE shim; ae_init()/ae_exit() are declared by
 * framework_isp.h. */
#ifndef AE_SHIM_H
#define AE_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Install a const ae_clean_tables_t (opaque here to keep clean typedefs away
 * from SDK code); NULL = placeholder defaults. Call before ae_init(). */
void ae_shim_set_tables(const void *tables);

#ifdef __cplusplus
}
#endif

#endif /* AE_SHIM_H */
