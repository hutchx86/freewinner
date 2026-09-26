/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* freeisp - locate the compiled-in tables inside a stock `rmm` image. */
#ifndef FREEISP_RMM_TABLES_H
#define FREEISP_RMM_TABLES_H

#include <stddef.h>
#include "freeisp/tables.h"

/*
 * Locate every table inside `image` (a whole stock rmm ELF file read into
 * memory) using the two anchors and the offsets-only layout, then
 * copy each table into freshly allocated, owned storage in `*out`.
 *
 * Only `Ae_DeltaLvTbl` is given writable storage. Every slice is structurally
 * validated; on any missing anchor, out-of-range slice or failed validator the
 * function frees what it allocated and returns -1 without emitting partial
 * data (fail-closed).
 *
 * Returns 0 on success.
 */
int freeisp_tables_locate(const void *image, size_t len, freeisp_tables_t *out);

/* Read `path` and locate as above. Returns 0 on success, -1 otherwise. */
int freeisp_tables_load_rmm(const char *path, freeisp_tables_t *out);

/* Release the storage allocated by a successful locate/load. */
void freeisp_tables_free(freeisp_tables_t *t);

/*
 * Bundle serialisation -- persist a located set so a later boot does not have
 * to read the vendor image again.
 *
 * A bundle is self-describing and byte-order fixed (all integers little-
 * endian), so a bundle written on the 64-bit host parses on 32-bit ARM and
 * vice versa:
 *
 *   header (32 bytes):
 *     char     magic[8]    "FWTABL01"
 *     uint32_t version     format version (1)
 *     uint32_t count       number of records (FREEISP_TABLE_LOC_COUNT)
 *     uint32_t layout_crc  crc32 of the compiled layout descriptor
 *     uint32_t total_size  sum of the record sizes
 *     uint32_t payload_crc crc32 of the concatenated record bytes
 *     uint32_t header_crc  crc32 of the preceding 28 bytes
 *   payload:
 *     the FREEISP_TABLE_LOC_COUNT tables, in freeisp_table_locs[] order, each
 *     exactly its layout size, concatenated (no per-record framing).
 *
 * layout_crc covers only the architecture-independent descriptor fields
 * (name, cluster, delta, size, writable) -- never a data byte and never a
 * pointer offset -- so the same bundle is valid on host and device.
 *
 * freeisp_tables_save() creates the parent directory if needed and writes to
 * `<path>.tmp`, then renames it into place, so a reader never sees a partial
 * file.  It returns 0 on success, -1 otherwise (and leaves no temp file).
 */
int freeisp_tables_save(const char *path, const freeisp_tables_t *t);

/*
 * Parse a bundle image and produce an owned table set.  Fail-closed: any bad
 * magic/version/count/layout/checksum, a size mismatch, an out-of-range slice
 * or a failed structural validator frees everything allocated so far and
 * returns -1, with *out left all-NULL (nothing partially installed).
 */
int freeisp_tables_parse_bundle(const void *image, size_t len,
                                freeisp_tables_t *out);

/* Read `path` and parse it as above.  Returns 0 on success. */
int freeisp_tables_load_bundle(const char *path, freeisp_tables_t *out);

#endif /* FREEISP_RMM_TABLES_H */
