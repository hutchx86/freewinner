/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * test_pltm_presets.c - the runtime preset extractor against synthetic A32
 * images (made-up values in the switch shape it looks for) and the text cache.
 */
#include "freeisp/pltm_presets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;

#define CHECK(cond, msg) do {                                   \
    if (!(cond)) { printf("FAIL: %s\n", msg); g_fail = 1; }     \
} while (0)

#define ROWS FREEISP_PLTM_PRESET_ROWS
#define COLS FREEISP_PLTM_PRESET_COLS
#define NOP  0xE1A00000u   /* mov r0, r0 */

/* Synthetic, rule-abiding table (not device data). */
static void synth(freeisp_pltm_presets_t t)
{
    int r;

    for (r = 0; r < ROWS; r++) {
        t[r][0] = r <= 15 ? 255 - 17 * r : 0;
        t[r][1] = 5 + r / 2;
        t[r][2] = 100 + 200 * r;
        t[r][3] = 4096 - 200 * r;
    }
}

static uint32_t img[1024];
static int n;

static void put(uint32_t w) { img[n++] = w; }

static uint32_t movw(unsigned rd, uint32_t v)
{
    return 0xE3000000u | ((v & 0xF000u) << 4) | (rd << 12) | (v & 0xFFFu);
}

static uint32_t str0(unsigned rt, unsigned rn)
{
    return 0xE5800000u | (rn << 16) | (rt << 12);
}

static uint32_t branch(int from, int to)
{
    return 0xEA000000u | ((uint32_t)(to - (from + 2)) & 0xFFFFFFu);
}

/* Build: padding, the switch prologue, the jump table and 19 cases. */
static void build(const freeisp_pltm_presets_t t)
{
    int table, cases[ROWS], r, c, k;

    n = 0;
    for (k = 0; k < 8; k++)
        put(NOP);
    put(0xE52DE004u);                     /* push {lr}          */
    put(0xE59DC004u);                     /* ldr  ip, [sp, #4]  */
    put(0xE35C0012u);                     /* cmp  ip, #18       */
    put(0x908FF10Cu);                     /* addls pc, pc, ip, lsl #2 */
    put(0);                               /* b default (patched) */
    table = n;
    for (r = 0; r < ROWS; r++)
        put(0);                           /* b case r (patched) */
    for (r = 0; r < ROWS; r++) {
        cases[r] = n;
        for (c = 0; c < COLS; c++) {
            unsigned rt = 4 + (unsigned)((r + c) % 8);

            if (t[r][c] == 4096)
                put(0xE3A00A01u | (rt << 12));   /* mov rt, #0x1000 (rotated) */
            else
                put(movw(rt, (uint32_t)t[r][c]));
            put(str0(rt, (unsigned)c));
        }
        put(0xE49DF004u);                 /* pop {pc} */
    }
    img[table - 1] = branch(table - 1, cases[3]);
    for (r = 0; r < ROWS; r++)
        img[table + r] = branch(table + r, cases[r]);
    for (k = 0; k < 8; k++)
        put(NOP);
}

static void test_extract(void)
{
    freeisp_pltm_presets_t t, got;
    const char *why = NULL;
    int saved, k;

    synth(t);
    build(t);
    CHECK(freeisp_pltm_presets_extract(img, (size_t)n * 4, got, &why) == 0,
          "synthetic image extracts");
    CHECK(memcmp(got, t, sizeof(t)) == 0, "decoded table equals the synthetic one");

    /* Anchor absent. */
    CHECK(freeisp_pltm_presets_extract(img, 8 * 4, got, &why) != 0 &&
          strcmp(why, "switch not found") == 0, "no anchor -> fail");

    /* Anchor twice. */
    build(t);
    k = n;
    img[n++] = 0xE35C0012u;
    img[n++] = 0x908FF10Cu;
    CHECK(freeisp_pltm_presets_extract(img, (size_t)n * 4, got, &why) != 0 &&
          strcmp(why, "switch found more than once") == 0, "duplicate anchor -> fail");
    n = k;

    /* A store replaced by a NOP: the case no longer sets every column. */
    build(t);
    for (k = 0; k < n; k++)
        if ((img[k] & 0x0FF00FFFu) == 0x05800000u) {
            saved = (int)img[k];
            img[k] = NOP;
            break;
        }
    CHECK(freeisp_pltm_presets_extract(img, (size_t)n * 4, got, &why) != 0,
          "undecodable case -> fail");
    (void)saved;

    /* A branch-table entry that is not a branch. */
    build(t);
    img[8 + 5 + 7] = NOP;
    CHECK(freeisp_pltm_presets_extract(img, (size_t)n * 4, got, &why) != 0,
          "non-branch table entry -> fail");
}

static void test_validate(void)
{
    freeisp_pltm_presets_t t;
    const char *why = NULL;

    synth(t);
    CHECK(freeisp_pltm_presets_validate(t, &why) == 0, "synthetic table valid");

    synth(t);
    t[0][3] = 4000;
    CHECK(freeisp_pltm_presets_validate(t, &why) != 0, "non-neutral row 0 rejected");

    synth(t);
    memcpy(t[1], t[2], sizeof(t[1]));
    t[2][2] += 1;                          /* keep rows distinct */
    t[1][0] = 0; t[2][0] = 200;            /* blend rises: not monotone */
    CHECK(freeisp_pltm_presets_validate(t, &why) != 0, "non-monotone rejected");

    synth(t);
    t[4][1] = 16;
    CHECK(freeisp_pltm_presets_validate(t, &why) != 0, "order out of range rejected");

    synth(t);
    memcpy(t[18], t[17], sizeof(t[18]));
    CHECK(freeisp_pltm_presets_validate(t, &why) != 0, "duplicate rows rejected");
}

static void test_text(void)
{
    freeisp_pltm_presets_t t, back;
    const char *why = NULL;
    char path[] = "/tmp/test_pltm_presets_XXXXXX";
    FILE *f;
    int fd = mkstemp(path);

    CHECK(fd >= 0, "temp file");
    if (fd < 0)
        return;
    synth(t);
    CHECK(freeisp_pltm_presets_save_text(path, (const int32_t (*)[COLS])t) == 0,
          "save text");
    CHECK(freeisp_pltm_presets_load_text(path, back, &why) == 0 &&
          memcmp(t, back, sizeof(t)) == 0, "text round-trip");

    f = fopen(path, "w");
    fputs("# header only\n255 5 100 4096\n", f);
    fclose(f);
    CHECK(freeisp_pltm_presets_load_text(path, back, &why) != 0, "short file rejected");
    CHECK(freeisp_pltm_presets_load_text("/nonexistent/x", back, &why) != 0,
          "missing file rejected");
    remove(path);
}

int main(void)
{
    test_extract();
    test_validate();
    test_text();
    if (!g_fail)
        printf("test_pltm_presets: all passed\n");
    return g_fail;
}
