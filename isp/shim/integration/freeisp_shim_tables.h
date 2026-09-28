/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* freeisp_shim_tables.h - runtime table feed: locates the libisp tuning tables
 * in a stock rmm image (via freeisp_tables_locate), maps them onto each
 * module's *_clean_tables_t and installs them through the shim setters. */
#ifndef FREEISP_SHIM_TABLES_H
#define FREEISP_SHIM_TABLES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pre-located bundle, beside yi-mediad's vendor-tuning cache so all ISP data
 * lives in one directory. */
#ifndef FREEISP_TABLE_BUNDLE_PATH
#define FREEISP_TABLE_BUNDLE_PATH "/tmp/sd/unifi/isp_cfg/freeisp_tables.bin"
#endif

/* PLTM preset cache (freeisp/pltm_presets.h), extracted from the device
 * firmware on first boot; without it PLTM runs neutral. */
#ifndef FREEISP_PLTM_PRESETS_PATH
#define FREEISP_PLTM_PRESETS_PATH "/tmp/sd/unifi/isp_cfg/pltm_presets.txt"
#endif

/* AE backlight-network output-bias cache (freeisp/ae_out_bias.h), extracted
 * the same way; without it the network is off and backlight stays 32. */
#ifndef FREEISP_AE_OUT_BIAS_PATH
#define FREEISP_AE_OUT_BIAS_PATH "/tmp/sd/unifi/isp_cfg/ae_out_bias.txt"
#endif

typedef enum freeisp_shim_table_id {
    FREEISP_SHIM_TABLE_AE = 0,
    FREEISP_SHIM_TABLE_AWB,
    FREEISP_SHIM_TABLE_AFS,
    FREEISP_SHIM_TABLE_ISO,
    FREEISP_SHIM_TABLE_GTM,
    FREEISP_SHIM_TABLE_PLTM,
    FREEISP_SHIM_TABLE_COUNT
} freeisp_shim_table_id_t;

/* Locate, map and install from the rmm image at `path`, replacing any loaded
 * set. Fail-closed: any error installs nothing and returns -1. */
int freeisp_shim_tables_from_rmm(const char *path);

/* Same, given an image already in memory.  The buffer stays caller-owned. */
int freeisp_shim_tables_from_memory(const void *image, size_t len);

/* Install from a bundle written by freeisp_shim_tables_save_cache(), without
 * reading any vendor image. Fail-closed like the locator. */
int freeisp_shim_tables_from_cache(const char *path);

/* Save the installed set as a bundle to seed the next boot's cache; -1 (nothing
 * installed or write failed) may be treated as non-fatal. */
int freeisp_shim_tables_save_cache(const char *path);

/* Cache-first: valid bundle (NULL = default path, "" = no cache), else locate in
 * rmm_path and seed the bundle, else install nothing and return -1. */
int freeisp_shim_tables_from_rmm_or_cache(const char *rmm_path,
                                          const char *bundle_path);

/* Uninstall (shims fall back to built-in defaults) and free; safe when empty. */
void freeisp_shim_tables_free(void);

/* Where the PLTM presets came from for the last load ("from cache (ok)",
 * "extracted from firmware (ok)", "neutral, ... (<reason>)"), for logging. */
const char *freeisp_shim_pltm_presets_status(void);

/* Same for the AE output biases ("from cache (ok)", "extracted from firmware
 * (ok)", "not found, backlight network disabled (<reason>)"). */
const char *freeisp_shim_ae_out_bias_status(void);

/* Mapped per-module block or NULL; cast to <mod>_clean_tables_t (AFS:
 * afs_clean_trig_t). For tests and diagnostics. */
const void *freeisp_shim_tables_get(freeisp_shim_table_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* FREEISP_SHIM_TABLES_H */
