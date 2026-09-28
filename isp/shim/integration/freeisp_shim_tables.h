/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * freeisp_shim_tables.h - on-device runtime table feed for the clean-room
 * shims.
 *
 * Locates the compiled-in libisp tuning tables inside a stock `rmm` image and
 * populates one `*_clean_tables_t` block per clean-room module, then installs
 * them through each shim's per-module setter so the clean cores (which never
 * compile tuning data in) see the real tables.
 *
 * The location logic (two anchors + an offsets-only layout, with
 * fail-closed structural validators) is the freewinner `freeisp_tables_locate`
 * C port; this layer only adapts the located vendor table set onto the clean
 * per-module shapes.
 */
#ifndef FREEISP_SHIM_TABLES_H
#define FREEISP_SHIM_TABLES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Default on-SD location of the pre-located bundle.  Kept in the same
 * directory as yi-mediad's existing vendor-tuning cache
 * (`rmm_tuning.c`: RMM_TUNING_CACHE_DIR "/tmp/sd/unifi/isp_cfg") so the whole
 * ISP data cache lives in one place.  Override with -DFREEISP_TABLE_BUNDLE_PATH
 * at build time.
 */
#ifndef FREEISP_TABLE_BUNDLE_PATH
#define FREEISP_TABLE_BUNDLE_PATH "/tmp/sd/unifi/isp_cfg/freeisp_tables.bin"
#endif

/*
 * PLTM preset text cache (freeisp/pltm_presets.h), next to the bundle.  The
 * presets are read out of the device's own firmware on the first boot and
 * cached here; without them the PLTM core runs neutral.
 */
#ifndef FREEISP_PLTM_PRESETS_PATH
#define FREEISP_PLTM_PRESETS_PATH "/tmp/sd/unifi/isp_cfg/pltm_presets.txt"
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

/* Locate the tables in the rmm image at `path`, map them onto every module and
 * install them.  Returns 0 on success; on any missing anchor, out-of-range
 * slice, failed validator or OOM it releases everything and returns -1 with
 * nothing installed (fail-closed).  Replaces any previously loaded set. */
int freeisp_shim_tables_from_rmm(const char *path);

/* Same, given an image already in memory.  The buffer stays caller-owned. */
int freeisp_shim_tables_from_memory(const void *image, size_t len);

/*
 * Install the tables from a pre-located bundle previously written by
 * freeisp_shim_tables_save_cache() -- i.e. without reading any vendor image.
 * Fail-closed, exactly like the locator: a missing, truncated, mis-versioned or
 * corrupt bundle installs nothing and returns -1.
 */
int freeisp_shim_tables_from_cache(const char *path);

/*
 * Persist the currently installed table set to `path` as a bundle (see
 * freeisp_tables_save()).  Call it right after a successful locator load to
 * seed the cache for the next boot.  Returns 0 on success, -1 if nothing is
 * installed or the write failed; the caller may treat -1 as non-fatal.
 */
int freeisp_shim_tables_save_cache(const char *path);

/*
 * Cache-first deploy resolver.
 *
 *   1. if `bundle_path` (or FREEISP_TABLE_BUNDLE_PATH when NULL) holds a valid
 *      bundle, install it and return 0 without touching the vendor image;
 *   2. otherwise locate the tables in the `rmm_path` image, install them and
 *      seed the bundle (best-effort) for the next boot;
 *   3. if neither yields a complete set, install nothing and return -1.
 *
 * Pass an empty string as `bundle_path` to disable the cache (pure locator
 * behaviour, e.g. for auditing).  The caller's kill switch
 * (MEDIAD_NO_RMM_TUNING) is applied upstream and disables both steps.
 */
int freeisp_shim_tables_from_rmm_or_cache(const char *rmm_path,
                                          const char *bundle_path);

/* Uninstall the tables (each shim falls back to its pilot defaults) and
 * release every owned copy.  Safe to call when nothing is loaded. */
void freeisp_shim_tables_free(void);

/* Where the PLTM presets came from for the last load ("from cache (ok)",
 * "extracted from firmware (ok)", "neutral, ... (<reason>)"), for logging. */
const char *freeisp_shim_pltm_presets_status(void);

/* The mapped per-module block, or NULL when nothing is loaded.  For tests and
 * diagnostics; cast to the matching `<mod>_clean_tables_t` (or
 * `afs_clean_trig_t` for AFS). */
const void *freeisp_shim_tables_get(freeisp_shim_table_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* FREEISP_SHIM_TABLES_H */
