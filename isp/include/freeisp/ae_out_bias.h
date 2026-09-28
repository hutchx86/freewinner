/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* ae_out_bias.h - extract the AE backlight-network output biases B0/B1 (spec ae.md 8.5)
 * from the device's own firmware, where they are A32 add immediates; accepted only if
 * they pass structural checks, else the network is disabled. Cached on the device. */
#ifndef FREEISP_AE_OUT_BIAS_H
#define FREEISP_AE_OUT_BIAS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FREEISP_AE_OUT_BIAS_N   2       /* [0] = B0 (score 0), [1] = B1 (score 1) */
#define FREEISP_AE_OUT_BIAS_MIN 1000    /* plausibility window, both biases */
#define FREEISP_AE_OUT_BIAS_MAX 20000

typedef int32_t freeisp_ae_out_bias_t[FREEISP_AE_OUT_BIAS_N];

/* Each returns 0 on success, -1 on failure with *why (if non-NULL) set to a
 * static reason string.  `out` is written only on success. */
int freeisp_ae_out_bias_extract(const void *image, size_t len,
                                freeisp_ae_out_bias_t out, const char **why);
/* As above; `at` (if non-NULL) receives the file offsets of the B0/B1 sites. */
int freeisp_ae_out_bias_extract_at(const void *image, size_t len,
                                   freeisp_ae_out_bias_t out, size_t at[2],
                                   const char **why);
int freeisp_ae_out_bias_extract_file(const char *path, freeisp_ae_out_bias_t out,
                                     const char **why);
int freeisp_ae_out_bias_validate(const freeisp_ae_out_bias_t b, const char **why);

/* Text cache: comment lines start with '#', then one line "B0 B1".
 * Loading validates the pair. */
int freeisp_ae_out_bias_load_text(const char *path, freeisp_ae_out_bias_t out,
                                  const char **why);
int freeisp_ae_out_bias_save_text(const char *path, const freeisp_ae_out_bias_t b);

#ifdef __cplusplus
}
#endif

#endif /* FREEISP_AE_OUT_BIAS_H */
