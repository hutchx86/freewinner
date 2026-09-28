/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* pltm_presets.c - extract the PLTM preset bank from the stock daemon. Its lookup is a dense
 * 19-way A32 switch storing blend/order/clip/gain through r0..r3, compiled as
 * `cmp ip, #18; addls pc, pc, ip, lsl #2; b <default>; b <case 0..18>`. Only that shape
 * and public A32 encodings are known here; the values come from the image. */
#include "freeisp/pltm_presets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROWS FREEISP_PLTM_PRESET_ROWS
#define COLS FREEISP_PLTM_PRESET_COLS

#define A32_CMP_IP_18     0xE35C0012u  /* cmp   ip, #18                     */
#define A32_ADDLS_PC_IP2  0x908FF10Cu  /* addls pc, pc, ip, lsl #2          */
#define A32_POP_PC        0xE49DF004u  /* pop   {pc} (ldr pc, [sp], #4)     */
#define CASE_MAX_WORDS    16

#define BLEND_MAX   255
#define ORDER_MIN   5
#define ORDER_MAX   15
#define Q12_UNITY   4096
#define REACHABLE   17                 /* rows 0..16 are reachable (spec 7.8) */

static void set_why(const char **why, const char *msg)
{
    if (why)
        *why = msg;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* A32 modified immediate: 8-bit value rotated right by twice the 4-bit field. */
static uint32_t a32_imm(uint32_t imm12)
{
    uint32_t v = imm12 & 0xFFu;
    unsigned r = (unsigned)((imm12 >> 8) & 0xFu) * 2u;

    return r ? ((v >> r) | (v << (32u - r))) : v;
}

/* Interpret one case: immediate loads into registers, stores through r0..r3,
 * ending in pop {pc}.  Anything else rejects the case. */
static int decode_case(const uint8_t *img, size_t len, size_t off, int32_t row[COLS])
{
    uint32_t reg[16];
    unsigned known = 0, stored = 0;
    int k;

    for (k = 0; k < CASE_MAX_WORDS; k++) {
        uint32_t w, rd;

        if (off + 4u * (size_t)(k + 1) > len)
            return -1;
        w = rd32(img + off + 4u * (size_t)k);
        if (w == A32_POP_PC)
            return stored == 0xFu ? 0 : -1;
        if ((w >> 28) != 0xEu)
            return -1;
        rd = (w >> 12) & 0xFu;
        if ((w & 0x0FEF0000u) == 0x03A00000u) {            /* mov  rd, #imm   */
            reg[rd] = a32_imm(w & 0xFFFu);
            known |= 1u << rd;
        } else if ((w & 0x0FEF0000u) == 0x03E00000u) {     /* mvn  rd, #imm   */
            reg[rd] = ~a32_imm(w & 0xFFFu);
            known |= 1u << rd;
        } else if ((w & 0x0FF00000u) == 0x03000000u) {     /* movw rd, #imm16 */
            reg[rd] = ((w >> 4) & 0xF000u) | (w & 0xFFFu);
            known |= 1u << rd;
        } else if ((w & 0x0FF00000u) == 0x03400000u) {     /* movt rd, #imm16 */
            if (!(known & (1u << rd)))
                return -1;
            reg[rd] = (reg[rd] & 0xFFFFu) |
                      ((((w >> 4) & 0xF000u) | (w & 0xFFFu)) << 16);
        } else if ((w & 0x0FF00FFFu) == 0x05800000u) {     /* str rt, [rn]    */
            uint32_t rn = (w >> 16) & 0xFu;

            if (rn >= COLS || !(known & (1u << rd)))
                return -1;
            row[rn] = (int32_t)reg[rd];
            stored |= 1u << rn;
        } else {
            return -1;
        }
    }
    return -1;
}

int freeisp_pltm_presets_validate(const freeisp_pltm_presets_t t, const char **why)
{
    int i, j;

    for (i = 0; i < ROWS; i++) {
        if (t[i][0] < 0 || t[i][0] > BLEND_MAX ||
            t[i][1] < ORDER_MIN || t[i][1] > ORDER_MAX ||
            t[i][2] < 0 || t[i][2] > Q12_UNITY ||
            t[i][3] < 0 || t[i][3] > Q12_UNITY) {
            set_why(why, "value out of range");
            return -1;
        }
        for (j = 0; j < i; j++)
            if (memcmp(t[i], t[j], sizeof(t[i])) == 0) {
                set_why(why, "duplicate rows");
                return -1;
            }
    }
    /* Row 0 is "no effect": full original blend and unity gain. */
    if (t[0][0] != BLEND_MAX || t[0][3] != Q12_UNITY) {
        set_why(why, "first row is not neutral");
        return -1;
    }
    /* Reachable rows: strength rises -> less blend, higher order, less gain. */
    for (i = 1; i < REACHABLE; i++)
        if (t[i][0] > t[i - 1][0] || t[i][1] < t[i - 1][1] ||
            t[i][3] > t[i - 1][3]) {
            set_why(why, "rows not monotone");
            return -1;
        }
    return 0;
}

int freeisp_pltm_presets_extract(const void *image, size_t len,
                                 freeisp_pltm_presets_t out, const char **why)
{
    const uint8_t *img = (const uint8_t *)image;
    freeisp_pltm_presets_t t;
    size_t off, at = 0;
    int hits = 0, i;

    if (img == NULL || len < 16) {
        set_why(why, "no image");
        return -1;
    }
    for (off = 0; off + 8 <= len; off += 4)
        if (rd32(img + off) == A32_CMP_IP_18 &&
            rd32(img + off + 4) == A32_ADDLS_PC_IP2) {
            at = off;
            hits++;
        }
    if (hits != 1) {
        set_why(why, hits ? "switch found more than once" : "switch not found");
        return -1;
    }

    /* pc reads as the addls address + 8, so entry k is at at + 12 + 4k. */
    for (i = 0; i < ROWS; i++) {
        size_t e = at + 12u + 4u * (size_t)i;
        uint32_t w;
        int32_t disp;
        long tgt;

        if (e + 4 > len) {
            set_why(why, "jump table truncated");
            return -1;
        }
        w = rd32(img + e);
        if ((w & 0xFF000000u) != 0xEA000000u) {
            set_why(why, "jump table entry is not a branch");
            return -1;
        }
        disp = (int32_t)(w << 8) >> 8;              /* sign-extend imm24 */
        tgt = (long)e + 8 + 4L * disp;
        if (tgt < 0 || (size_t)tgt >= len ||
            decode_case(img, len, (size_t)tgt, t[i]) != 0) {
            set_why(why, "case not decodable");
            return -1;
        }
    }
    if (freeisp_pltm_presets_validate(t, why) != 0)
        return -1;
    memcpy(out, t, sizeof(t));
    set_why(why, "ok");
    return 0;
}

int freeisp_pltm_presets_extract_file(const char *path, freeisp_pltm_presets_t out,
                                      const char **why)
{
    FILE *f;
    long n;
    void *buf;
    int rc;

    if (path == NULL || (f = fopen(path, "rb")) == NULL) {
        set_why(why, "cannot open firmware image");
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0 ||
        fseek(f, 0, SEEK_SET) != 0 || (buf = malloc((size_t)n)) == NULL) {
        fclose(f);
        set_why(why, "cannot read firmware image");
        return -1;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        set_why(why, "cannot read firmware image");
        return -1;
    }
    fclose(f);
    rc = freeisp_pltm_presets_extract(buf, (size_t)n, out, why);
    free(buf);
    return rc;
}

int freeisp_pltm_presets_load_text(const char *path, freeisp_pltm_presets_t out,
                                   const char **why)
{
    freeisp_pltm_presets_t t;
    char line[160];
    FILE *f;
    int rows = 0;

    if (path == NULL || (f = fopen(path, "r")) == NULL) {
        set_why(why, "no cache file");
        return -1;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        long v[COLS];
        char extra;

        if (line[0] == '#' || line[strspn(line, " \t\r\n")] == '\0')
            continue;
        if (rows >= ROWS ||
            sscanf(line, "%ld %ld %ld %ld %c", &v[0], &v[1], &v[2], &v[3],
                   &extra) != COLS) {
            fclose(f);
            set_why(why, "cache file malformed");
            return -1;
        }
        t[rows][0] = (int32_t)v[0];
        t[rows][1] = (int32_t)v[1];
        t[rows][2] = (int32_t)v[2];
        t[rows][3] = (int32_t)v[3];
        rows++;
    }
    fclose(f);
    if (rows != ROWS) {
        set_why(why, "cache file malformed");
        return -1;
    }
    if (freeisp_pltm_presets_validate(t, why) != 0)
        return -1;
    memcpy(out, t, sizeof(t));
    set_why(why, "ok");
    return 0;
}

int freeisp_pltm_presets_save_text(const char *path, const freeisp_pltm_presets_t t)
{
    char tmp[512];
    FILE *f;
    int i;

    if (path == NULL || snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
        return -1;
    if ((f = fopen(tmp, "w")) == NULL)
        return -1;
    fprintf(f, "# PLTM presets read from this camera's own firmware at first boot.\n"
               "# Device data: do not copy or redistribute.\n"
               "# blend order clip gain\n");
    for (i = 0; i < ROWS; i++)
        fprintf(f, "%d %d %d %d\n", (int)t[i][0], (int)t[i][1], (int)t[i][2],
                (int)t[i][3]);
    if (fclose(f) != 0 || rename(tmp, path) != 0)
        return -1;
    return 0;
}
