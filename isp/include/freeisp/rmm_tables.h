/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* freeisp - locate the compiled-in tables inside a stock `rmm` image. */
#ifndef FREEISP_RMM_TABLES_H
#define FREEISP_RMM_TABLES_H

#include <stddef.h>
#include "freeisp/tables.h"

/* Locate and copy every table out of a whole stock rmm ELF image into owned storage.
 * Fail-closed: any bad anchor/slice/validator frees everything and returns -1. */
int freeisp_tables_locate(const void *image, size_t len, freeisp_tables_t *out);

/* Read `path` and locate as above. Returns 0 on success, -1 otherwise. */
int freeisp_tables_load_rmm(const char *path, freeisp_tables_t *out);

/* Release the storage allocated by a successful locate/load. */
void freeisp_tables_free(freeisp_tables_t *t);

/* Persist a located set as a little-endian "FWTABL01" bundle (format: isp/shim/integration/
 * README.md); written via <path>.tmp + rename. Returns 0 or -1. */
int freeisp_tables_save(const char *path, const freeisp_tables_t *t);

/* Parse a bundle into an owned table set. Fail-closed: any bad header, checksum, size
 * or validator frees everything and returns -1 with *out all-NULL. */
int freeisp_tables_parse_bundle(const void *image, size_t len,
                                freeisp_tables_t *out);

/* Read `path` and parse it as above.  Returns 0 on success. */
int freeisp_tables_load_bundle(const char *path, freeisp_tables_t *out);

#endif /* FREEISP_RMM_TABLES_H */
