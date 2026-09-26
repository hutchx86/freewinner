/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * awb_shim.h - public knob of the AWB shim.
 *
 * The entry points awb_init()/awb_exit() (the fwi_awb_core_ops_t vtable) are declared
 * by framework_isp.h and implemented by awb_shim.c.
 */
#ifndef AWB_SHIM_H
#define AWB_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Install the runtime AWB tables used by the clean-room core: the
 * distance/tolerance trust table (96x256 u8), the temporal smoothing-weight
 * rows (48x64 u16), the class labels (10x32 char), the per-preset standard
 * trust (10 s32) and the brightness x temperature trust matrix (10x10 s32).
 *
 * `tables` is a pointer to a const awb_clean_tables_t.  It is kept opaque
 * here on purpose: this header is included next to the SDK ABI headers, and
 * pulling in awb_clean.h would collide on the awb_* entry-point names.
 *
 * Pass NULL to fall back to the built-in pilot defaults.
 * Must be called before awb_init() (the clean core latches the table pointer
 * during its own initialisation).
 */
void awb_shim_set_tables(const void *tables);

/*
 * Install the no-statistics fallback gain quadruple (r, gr, gb, b) used by
 * the built-in pilot default.  The clean core installs this quadruple when
 * its run entry point is called without statistics and any output channel is
 * still zero.  No vendor table carries it, so the runtime feed or the
 * framework caller supplies it; the pilot default is unity.
 */
void awb_shim_set_safe_gain(const unsigned short gain[4]);

#ifdef __cplusplus
}
#endif

#endif /* AWB_SHIM_H */
