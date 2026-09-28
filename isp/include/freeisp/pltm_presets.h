/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* pltm_presets.h - extract the 19 x 4 PLTM preset table (spec pltm.md 7.8) from the
 * device's own firmware, where it is a switch of A32 immediates; accepted only if it
 * passes structural checks, else neutral. The result is cached on the device. */
#ifndef FREEISP_PLTM_PRESETS_H
#define FREEISP_PLTM_PRESETS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FREEISP_PLTM_PRESET_ROWS 19
#define FREEISP_PLTM_PRESET_COLS 4   /* blend, order, clip, gain */

typedef int32_t freeisp_pltm_presets_t[FREEISP_PLTM_PRESET_ROWS][FREEISP_PLTM_PRESET_COLS];

/* Each returns 0 on success, -1 on failure with *why (if non-NULL) set to a
 * static reason string.  `out` is written only on success. */
int freeisp_pltm_presets_extract(const void *image, size_t len,
                                 freeisp_pltm_presets_t out, const char **why);
int freeisp_pltm_presets_extract_file(const char *path,
                                      freeisp_pltm_presets_t out,
                                      const char **why);
int freeisp_pltm_presets_validate(const freeisp_pltm_presets_t t,
                                  const char **why);

/* Text cache: comment lines start with '#', then 19 lines of 4 integers.
 * Loading validates the table. */
int freeisp_pltm_presets_load_text(const char *path, freeisp_pltm_presets_t out,
                                   const char **why);
int freeisp_pltm_presets_save_text(const char *path,
                                   const freeisp_pltm_presets_t t);

#ifdef __cplusplus
}
#endif

#endif /* FREEISP_PLTM_PRESETS_H */
