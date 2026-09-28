/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* rmm_tables.c - locate and load the libisp constant tables from a stock on-camera `rmm`
 * image. No table bytes are compiled in: only the two anchors and the offsets-only
 * layout (tables_layout.h). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "freeisp/rmm_tables.h"
#include "freeisp/tables_layout.h"

#define AE_LOG2_N 256
#define ANCHOR_DATA_ROW 9
static const char ANCHOR_DATA_STR[] = "Outlier Light";

static int read_file(const char *path, unsigned char **buf, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long n = ftell(f);
    if (n <= 0) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    unsigned char *b = malloc((size_t)n);
    if (!b) { fclose(f); return -1; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return -1; }
    fclose(f);
    *buf = b; *len = (size_t)n;
    return 0;
}

static void ae_log2_formula(int *out)
{
    for (int i = 0; i < AE_LOG2_N; i++)
        out[i] = (int)lround(1000.0 * log2((double)(i + 1)));
}

/* first byte offset of `needle`, or -1 */
static long find_bytes(const unsigned char *img, size_t len,
                       const unsigned char *needle, size_t nlen)
{
    if (nlen == 0 || len < nlen) return -1;
    for (size_t i = 0; i + nlen <= len; i++)
        if (img[i] == needle[0] && memcmp(img + i, needle, nlen) == 0)
            return (long)i;
    return -1;
}

static int mono_s32(const int *v, size_t n)
{
    for (size_t i = 0; i + 1 < n; i++) if (v[i] > v[i + 1]) return 0;
    return 1;
}

/* structural checks only -- generic invariants, never table content */
static int validate(const char *name, const void *p, size_t size)
{
    const unsigned char *b = p;
    int nonzero = 0;
    for (size_t i = 0; i < size; i++) if (b[i]) { nonzero = 1; break; }
    if (!nonzero) return 0;

    if (strcmp(name, "Ae_Log2") == 0) {
        int ref[AE_LOG2_N]; ae_log2_formula(ref);
        return memcmp(p, ref, sizeof(ref)) == 0;
    }
    if (strcmp(name, "AeGammaPre") == 0) {
        const int16_t *v = p;
        for (int r = 0; r < 11; r++) {
            const int16_t *row = v + r * 256;
            if (row[0] != 0 || row[255] != 4095) return 0;
            for (int i = 0; i + 1 < 256; i++) if (row[i] > row[i + 1]) return 0;
        }
        return 1;
    }
    if (strncmp(name, "gd_curve_", 9) == 0) {
        const int16_t *v = p;
        if (size != 512 || v[0] != 0) return 0;
        for (int i = 0; i + 1 < 256; i++) if (v[i] > v[i + 1]) return 0;
        return 1;
    }
    if (strcmp(name, "TBL2GAIN") == 0)
        return mono_s32((const int *)p, 350);
    if (strcmp(name, "iso_gain_point") == 0 ||
        strcmp(name, "iso_lum_point") == 0)
        return mono_s32((const int *)p, 14);
    if (strcmp(name, "Ae_LumWeight_avg") == 0) {
        const int32_t *v = p;
        if (v[0] <= 0) return 0;
        for (int i = 1; i < 64; i++) if (v[i] != v[0]) return 0;
        return 1;
    }
    if (strcmp(name, "AeConverData") == 0) {
        if (size != 4096) return 0;
        const unsigned char *row0 = p;
        if (row0[0] != 0 || row0[127] != 127) return 0;
        for (int i = 0; i + 1 < 128; i++) if (row0[i] > row0[i + 1]) return 0;
        return 1;
    }
    if (strcmp(name, "Ae_DeltaLvTbl") == 0)
        return ((const uint32_t *)p)[0] > 0;
    if (strcmp(name, "af_square_lut") == 0) {
        if (size != 16) return 0;
        const unsigned char *v = p;
        if (v[0] != 1 || v[15] != 0xff) return 0;
        for (int i = 0; i + 1 < 16; i++) if (v[i] > v[i + 1]) return 0;
        return 1;
    }
    if (strcmp(name, "AwbLightClassName") == 0) {
        const unsigned char *v = p;
        for (size_t i = 0; i < size; i++)
            if (v[i] != 0 && (v[i] < 0x20 || v[i] > 0x7e)) return 0;
        return 1;
    }
    return 1;
}

/* find the NUL-terminated anchor string at or after `min_off`, return its
 * start (the offset points at the string, not the containing array). */
static long find_anchor_str(const unsigned char *img, size_t len, size_t min_off)
{
    size_t n = sizeof(ANCHOR_DATA_STR); /* includes trailing NUL */
    for (size_t i = min_off; i + n <= len; i++)
        if (img[i] == ANCHOR_DATA_STR[0] &&
            memcmp(img + i, ANCHOR_DATA_STR, n) == 0)
            return (long)i;
    return -1;
}

int freeisp_tables_locate(const void *image, size_t len, freeisp_tables_t *out)
{
    const unsigned char *img = image;
    memset(out, 0, sizeof(*out));

    int ref[AE_LOG2_N]; ae_log2_formula(ref);
    long rodata = find_bytes(img, len, (const unsigned char *)ref, sizeof(ref));
    if (rodata < 0) return -1;

    long data = find_anchor_str(img, len, ANCHOR_DATA_ROW * 32);
    if (data < 0) return -1;
    data -= ANCHOR_DATA_ROW * 32;

    long base[2] = { rodata, data };

    for (size_t i = 0; i < FREEISP_TABLE_LOC_COUNT; i++) {
        const freeisp_table_loc_t *L = &freeisp_table_locs[i];
        long off = base[L->cluster] + L->delta;
        if (off < 0 || (size_t)off + L->size > len) goto fail;
        if (!validate(L->name, img + off, L->size)) goto fail;
        void *copy = malloc(L->size);
        if (!copy) goto fail;
        memcpy(copy, img + off, L->size);
        *(void **)((char *)out + L->off) = copy;
    }
    return 0;
fail:
    freeisp_tables_free(out);
    return -1;
}

int freeisp_tables_load_rmm(const char *path, freeisp_tables_t *out)
{
    unsigned char *buf = NULL; size_t len = 0;
    if (read_file(path, &buf, &len) != 0) return -1;
    int r = freeisp_tables_locate(buf, len, out);
    free(buf);
    return r;
}

/* ---------------------------------------------------- bundle I/O ---- */

#define BUNDLE_MAGIC     "FWTABL01"
#define BUNDLE_MAGIC_LEN 8u
#define BUNDLE_VERSION   1u
#define BUNDLE_HDR_LEN   32u
/* 16 MiB: far above any real table set (< 80 KiB) and far below an rmm image, so it
 * bounds a corrupt length field and cannot be confused with an rmm image. */
#define BUNDLE_MAX_LEN   (16u * 1024u * 1024u)

static uint32_t crc32_update(uint32_t c, const void *buf, size_t n)
{
    const unsigned char *p = buf;
    c = ~c;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static uint32_t crc32(const void *buf, size_t n)
{
    return crc32_update(0, buf, n);
}

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

/* Arch-independent layout fingerprint: names plus cluster/delta/size/writable, never table
 * data or pointer offsets, so a host-written bundle stays valid on the 32-bit device. */
static uint32_t layout_crc(void)
{
    uint32_t c = 0;
    for (size_t i = 0; i < FREEISP_TABLE_LOC_COUNT; i++) {
        const freeisp_table_loc_t *L = &freeisp_table_locs[i];
        unsigned char meta[10];
        meta[0] = L->cluster;
        put32(meta + 1, (uint32_t)L->delta);
        put32(meta + 5, L->size);
        meta[9] = L->writable;
        c = crc32_update(c, L->name, strlen(L->name) + 1);
        c = crc32_update(c, meta, sizeof(meta));
    }
    return c;
}

static int layout_total_size(uint32_t *out)
{
    uint64_t sum = 0;
    for (size_t i = 0; i < FREEISP_TABLE_LOC_COUNT; i++)
        sum += freeisp_table_locs[i].size;
    if (sum == 0 || sum > BUNDLE_MAX_LEN) return -1;
    *out = (uint32_t)sum;
    return 0;
}

/* mkdir -p, best-effort: a concurrent boot may have already created a level. */
static int mkdir_p(const char *path)
{
    char tmp[512];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(tmp)) return -1;
    memcpy(tmp, path, n + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            (void)mkdir(tmp, 0755);
            *p = '/';
        }
    }
    (void)mkdir(tmp, 0755);
    return 0;
}

static int ensure_parent_dir(const char *path)
{
    const char *slash = strrchr(path, '/');
    char dir[512];
    size_t n;
    if (slash == NULL || slash == path) return 0;   /* no dir, or root */
    n = (size_t)(slash - path);
    if (n >= sizeof(dir)) return -1;
    memcpy(dir, path, n);
    dir[n] = '\0';
    return mkdir_p(dir);
}

int freeisp_tables_save(const char *path, const freeisp_tables_t *t)
{
    unsigned char hdr[BUNDLE_HDR_LEN];
    char tmp[600];
    uint32_t total, pcrc = 0;
    FILE *f;
    size_t i;

    if (path == NULL || t == NULL) return -1;
    if (layout_total_size(&total) != 0) return -1;

    for (i = 0; i < FREEISP_TABLE_LOC_COUNT; i++) {
        const freeisp_table_loc_t *L = &freeisp_table_locs[i];
        const void *p = *(void *const *)((const char *)t + L->off);
        if (p == NULL) return -1;         /* incomplete set: refuse to dump */
        pcrc = crc32_update(pcrc, p, L->size);
    }

    memcpy(hdr, BUNDLE_MAGIC, BUNDLE_MAGIC_LEN);
    put32(hdr + 8, BUNDLE_VERSION);
    put32(hdr + 12, (uint32_t)FREEISP_TABLE_LOC_COUNT);
    put32(hdr + 16, layout_crc());
    put32(hdr + 20, total);
    put32(hdr + 24, pcrc);
    put32(hdr + 28, crc32(hdr, 28));

    if (ensure_parent_dir(path) != 0) return -1;
    if ((size_t)snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= sizeof(tmp))
        return -1;

    f = fopen(tmp, "wb");
    if (f == NULL) return -1;
    if (fwrite(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) goto fail;
    for (i = 0; i < FREEISP_TABLE_LOC_COUNT; i++) {
        const freeisp_table_loc_t *L = &freeisp_table_locs[i];
        const void *p = *(void *const *)((const char *)t + L->off);
        if (fwrite(p, 1, L->size, f) != L->size) goto fail;
    }
    if (fclose(f) != 0) { (void)remove(tmp); return -1; }
    if (rename(tmp, path) != 0) { (void)remove(tmp); return -1; }
    return 0;

fail:
    (void)fclose(f);
    (void)remove(tmp);
    return -1;
}

int freeisp_tables_parse_bundle(const void *image, size_t len,
                                freeisp_tables_t *out)
{
    const unsigned char *b = image;
    uint32_t version, count, lcrc, total, pcrc, hcrc, want_total;
    size_t off, i;

    memset(out, 0, sizeof(*out));
    if (b == NULL || len < BUNDLE_HDR_LEN || len > BUNDLE_MAX_LEN) return -1;
    if (memcmp(b, BUNDLE_MAGIC, BUNDLE_MAGIC_LEN) != 0) return -1;

    version = le32(b + 8);
    count   = le32(b + 12);
    lcrc    = le32(b + 16);
    total   = le32(b + 20);
    pcrc    = le32(b + 24);
    hcrc    = le32(b + 28);

    if (version != BUNDLE_VERSION) return -1;
    if (count != (uint32_t)FREEISP_TABLE_LOC_COUNT) return -1;
    if (lcrc != layout_crc()) return -1;
    if (layout_total_size(&want_total) != 0 || total != want_total) return -1;
    if (len != BUNDLE_HDR_LEN + (size_t)total) return -1;
    if (hcrc != crc32(b, 28)) return -1;
    if (pcrc != crc32(b + BUNDLE_HDR_LEN, total)) return -1;

    off = BUNDLE_HDR_LEN;
    for (i = 0; i < FREEISP_TABLE_LOC_COUNT; i++) {
        const freeisp_table_loc_t *L = &freeisp_table_locs[i];
        void *copy = malloc(L->size);
        if (copy == NULL) goto fail;
        memcpy(copy, b + off, L->size);
        if (!validate(L->name, copy, L->size)) { free(copy); goto fail; }
        *(void **)((char *)out + L->off) = copy;
        off += L->size;
    }
    return 0;

fail:
    freeisp_tables_free(out);
    return -1;
}

int freeisp_tables_load_bundle(const char *path, freeisp_tables_t *out)
{
    unsigned char *buf = NULL;
    size_t len = 0;
    int r;

    if (path == NULL) return -1;
    if (read_file(path, &buf, &len) != 0) return -1;
    r = freeisp_tables_parse_bundle(buf, len, out);
    free(buf);
    return r;
}

void freeisp_tables_free(freeisp_tables_t *t)
{
    for (size_t i = 0; i < FREEISP_TABLE_LOC_COUNT; i++) {
        void **p = (void **)((char *)t + freeisp_table_locs[i].off);
        free(*p);
        *p = NULL;
    }
}
