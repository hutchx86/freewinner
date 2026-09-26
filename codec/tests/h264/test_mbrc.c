/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Per-row rate-budget table for macroblock-level rate control (spec r2/01
 * section 6), driven by the black-box vectors in data/mbrc_table.csv:
 *   (a) the words from byte 512 on equal the expected table;
 *   (b) no byte outside the table region changes;
 *   (c) exactly two cache operations, each over the whole buffer. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freecodec/h264_bufs.h"

#ifndef MBRC_VECTORS
#define MBRC_VECTORS "tests/h264/data/mbrc_table.csv"
#endif

#define SENTINEL 0xa5u
#define MAX_WORDS (6u * FC_MBRC_MAX_ROWS)

static int g_fail;

struct sync_log {
    int      calls;
    int      bad;
    uint8_t *buf;
    int      size;
};

static void count_sync(void *opaque, void *mem, int size)
{
    struct sync_log *log = (struct sync_log *)opaque;

    log->calls++;
    if ((uint8_t *)mem != log->buf || size != log->size)
        log->bad++;
}

/* Parse up to `max` space-separated unsigned numbers from `s`. */
static unsigned int parse_words(const char *s, unsigned int *out, unsigned int max)
{
    unsigned int n = 0;
    char *end;

    while (*s != '\0' && n < max) {
        unsigned long v = strtoul(s, &end, 10);

        if (end == s)
            break;
        out[n++] = (unsigned int)v;
        s = end;
        while (*s == ' ')
            s++;
    }
    return n;
}

static void fail(int id, const char *what)
{
    fprintf(stderr, "FAIL case %d: %s\n", id, what);
    g_fail = 1;
}

static int run_case(int id, unsigned int rows, const unsigned int *mad,
                    unsigned int nmad, const unsigned int *tab, unsigned int ntab)
{
    unsigned int size = fc_mbrc_buffer_bytes(rows);
    unsigned int tab_end = FC_MBRC_TABLE_OFFSET + FC_MBRC_RECORD_BYTES * (rows - 1u);
    uint8_t *buf, *ref;
    struct sync_log log;
    unsigned int i;
    int err = g_fail;

    if (nmad != rows || ntab != 6u * (rows - 1u)) {
        fail(id, "malformed vector row");
        return -1;
    }

    buf = malloc(size);
    ref = malloc(size);
    if (buf == NULL || ref == NULL) {
        fail(id, "out of memory");
        free(buf);
        free(ref);
        return -1;
    }
    memset(buf, SENTINEL, size);
    for (i = 0; i < rows; i++)
        fc_mbrc_put16(buf + 2u * i, mad[i]);
    memcpy(ref, buf, size);

    memset(&log, 0, sizeof(log));
    log.buf  = buf;
    log.size = (int)size;
    if (fc_mbrc_build_table(buf, rows, count_sync, &log) != 0)
        fail(id, "builder refused the row count");

    for (i = 0; i < ntab; i++) {
        unsigned int got = fc_mbrc_get16(buf + FC_MBRC_TABLE_OFFSET + 2u * i);

        if (got != tab[i]) {
            fprintf(stderr, "FAIL case %d: word %u = %u, expected %u\n",
                    id, i, got, tab[i]);
            g_fail = 1;
            break;
        }
    }
    for (i = 0; i < size; i++) {
        if (i >= FC_MBRC_TABLE_OFFSET && i < tab_end)
            continue;
        if (buf[i] != ref[i]) {
            fprintf(stderr, "FAIL case %d: byte %u outside the table changed\n",
                    id, i);
            g_fail = 1;
            break;
        }
    }
    if (log.calls != 2 || log.bad != 0)
        fail(id, "expected exactly two whole-buffer cache operations");

    free(buf);
    free(ref);
    return (g_fail != err) ? -1 : 0;
}

int main(void)
{
    static unsigned int mad[FC_MBRC_MAX_ROWS + 1u];
    static unsigned int tab[MAX_WORDS + 1u];
    static char line[1u << 16];
    FILE *f = fopen(MBRC_VECTORS, "r");
    int cases = 0, passed = 0;
    uint8_t probe[4];

    if (f == NULL) {
        fprintf(stderr, "FAIL: cannot open %s\n", MBRC_VECTORS);
        return 1;
    }
    if (fgets(line, sizeof(line), f) == NULL) {   /* header */
        fprintf(stderr, "FAIL: empty vector file\n");
        fclose(f);
        return 1;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        char *c1, *c2, *c3;
        int id;
        unsigned int rows, nmad, ntab;

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0')
            continue;
        c1 = strchr(line, ',');
        c2 = c1 ? strchr(c1 + 1, ',') : NULL;
        c3 = c2 ? strchr(c2 + 1, ',') : NULL;
        if (c3 == NULL) {
            fprintf(stderr, "FAIL: bad line %.40s\n", line);
            g_fail = 1;
            continue;
        }
        *c1 = *c2 = *c3 = '\0';
        id   = atoi(line);
        rows = (unsigned int)strtoul(c1 + 1, NULL, 10);
        if (rows == 0u || rows > FC_MBRC_MAX_ROWS) {
            fail(id, "row count out of range");
            continue;
        }
        nmad = parse_words(c2 + 1, mad, FC_MBRC_MAX_ROWS + 1u);
        ntab = parse_words(c3 + 1, tab, MAX_WORDS + 1u);
        cases++;
        if (run_case(id, rows, mad, nmad, tab, ntab) == 0)
            passed++;
    }
    fclose(f);

    /* Row counts the layout cannot hold are refused without touching memory. */
    memset(probe, SENTINEL, sizeof(probe));
    if (fc_mbrc_build_table(probe, 0u, NULL, NULL) != -1 ||
        fc_mbrc_build_table(probe, FC_MBRC_MAX_ROWS + 1u, NULL, NULL) != -1 ||
        probe[0] != SENTINEL) {
        fprintf(stderr, "FAIL: out-of-range row counts must be refused\n");
        g_fail = 1;
    }

    if (cases != 68) {
        fprintf(stderr, "FAIL: expected 68 vector cases, read %d\n", cases);
        g_fail = 1;
    }
    if (g_fail)
        return 1;
    printf("mbrc: %d/%d vector cases (table words, untouched bytes, 2 cache ops) ok\n",
           passed, cases);
    return 0;
}
