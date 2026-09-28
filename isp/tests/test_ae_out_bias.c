/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* test_ae_out_bias.c - the AE output-bias extractor against synthetic A32 images
 * (made-up biases in the code shape it looks for), malformed input and the text cache. */
#include "freeisp/ae_out_bias.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;
static int g_pass;

#define CHECK(cond, msg) do {                                             \
    if (cond) g_pass++;                                                   \
    else { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); g_fail++; } \
} while (0)

#define NOP 0xE1A00000u   /* mov r0, r0 */
#define IP  12u

/* Synthetic biases (not device data): B0 < B1, both need two adds. */
#define SB0 2345
#define SB1 7777

static uint32_t img[4096];
static int n;

static void put(uint32_t w) { img[n++] = w; }
static void pad(int k) { while (k-- > 0) put(NOP); }

static uint32_t enc_imm(uint32_t v)
{
    unsigned r;

    for (r = 0; r < 32; r += 2) {
        uint32_t x = r ? (v << r) | (v >> (32u - r)) : v;
        if (x < 256u)
            return ((r / 2u) << 8) | x;
    }
    return 0xFFFFFFFFu;
}

static uint32_t movw(unsigned rd, uint32_t v)
{ return 0xE3000000u | ((v & 0xF000u) << 4) | (rd << 12) | (v & 0xFFFu); }
static uint32_t movt(unsigned rd, uint32_t v)
{ return 0xE3400000u | ((v & 0xF000u) << 4) | (rd << 12) | (v & 0xFFFu); }
static uint32_t rsb_asr5(unsigned rd, unsigned rn, unsigned rm)
{ return 0xE0600000u | (rn << 16) | (rd << 12) | (5u << 7) | (2u << 5) | rm; }
static uint32_t add_i(unsigned rd, unsigned rn, uint32_t v)
{ return 0xE2800000u | (rn << 16) | (rd << 12) | enc_imm(v); }
static uint32_t cmp_r(unsigned rn, unsigned rm) { return 0xE1500000u | (rn << 16) | rm; }
static uint32_t mov_asr8(unsigned rd, unsigned rm)
{ return 0xE1A00000u | (rd << 12) | (8u << 7) | (2u << 5) | rm; }
static uint32_t bge(int from, int to)
{ return 0xAA000000u | ((uint32_t)(to - (from + 2)) & 0xFFFFFFu); }

/* Split v into a high part (low byte cleared) and the low byte, like the compiler. */
static void split(uint32_t v, uint32_t *hi, uint32_t *lo)
{
    *hi = v & ~0xFFu;
    *lo = v & 0xFFu;
}

enum {
    F_NO_MAGIC = 1, F_MAGIC_SPLIT = 2, F_NO_CMP = 4, F_CMP_SWAP = 8, F_B0_FIRST = 16,
    F_FAR = 32, F_NO_ASR8 = 64, F_NO_40 = 128, F_NO_20 = 256, F_THIRD = 512
};

static void site_b0(uint32_t b0, int flags)
{
    uint32_t hi, lo;

    split(b0, &hi, &lo);
    put(movw(2, 0x851F));
    put((flags & F_NO_MAGIC) ? NOP : movt((flags & F_MAGIC_SPLIT) ? 4 : 2, 0x51EB));
    put(NOP);                                 /* smull stand-in */
    put(rsb_asr5(1, 1, IP));
    put(add_i(IP, 1, hi));
    put(NOP);                                 /* unrelated load */
    put(add_i(IP, IP, lo));
}

static void site_b1(uint32_t b1, int flags)
{
    uint32_t hi, lo;
    int at_bge, tgt;

    split(b1, &hi, &lo);
    put(movw(2, 0x851F));
    put(movt(2, 0x51EB));
    put(NOP);
    put(rsb_asr5(3, 3, 1));
    put(add_i(1, 3, hi));
    put(add_i(1, 1, lo));
    if (flags & F_NO_CMP)
        put(NOP);
    else
        put((flags & F_CMP_SWAP) ? cmp_r(1, IP) : cmp_r(IP, 1));
    at_bge = n;
    put(0);
    put((flags & F_NO_ASR8) ? NOP : mov_asr8(0, 1));
    put((flags & F_NO_40) ? NOP : add_i(0, 0, 40));
    pad(4);
    tgt = n;
    put(NOP);
    put((flags & F_NO_20) ? NOP : add_i(0, 0, 20));
    pad(2);
    img[at_bge] = bge(at_bge, tgt);
}

static void build(uint32_t b0, uint32_t b1, int flags)
{
    n = 0;
    pad(8);
    if (flags & F_B0_FIRST) {
        site_b0(b0, flags);
        pad(40);
        site_b1(b1, flags);
    } else {
        site_b1(b1, flags);
        pad((flags & F_FAR) ? 1100 : 180);
        site_b0(b0, flags);
    }
    if (flags & F_THIRD) {
        pad(8);
        site_b0(b0, flags);
    }
    pad(24);
}

static int run(const char **why, freeisp_ae_out_bias_t out)
{
    return freeisp_ae_out_bias_extract(img, (size_t)n * 4, out, why);
}

static int fails_with(int flags, uint32_t b0, uint32_t b1, const char *want)
{
    freeisp_ae_out_bias_t out = { -1, -1 };
    const char *why = NULL;

    build(b0, b1, flags);
    if (run(&why, out) == 0 || out[0] != -1 || out[1] != -1) {
        printf("  unexpected success\n");
        return 0;
    }
    if (strcmp(why, want) != 0) {
        printf("  why = \"%s\", want \"%s\"\n", why, want);
        return 0;
    }
    return 1;
}

static void test_positive(void)
{
    freeisp_ae_out_bias_t out;
    const char *why = NULL;
    size_t at[2];

    build(SB0, SB1, 0);
    CHECK(freeisp_ae_out_bias_extract_at(img, (size_t)n * 4, out, at, &why) == 0,
          "synthetic image extracts");
    CHECK(out[0] == SB0 && out[1] == SB1, "B0/B1 decoded with roles");
    CHECK(strcmp(why, "ok") == 0, "why == ok");
    CHECK(at[1] == 8 * 4 + 3 * 4, "B1 site offset");
    CHECK(at[0] > at[1], "B0 site after B1");
}

static void test_failures(void)
{
    n = 0;
    pad(64);
    {
        freeisp_ae_out_bias_t out;
        const char *why = NULL;
        CHECK(run(&why, out) != 0 && strcmp(why, "anchor not found") == 0, "V1 no site");
    }
    CHECK(fails_with(F_THIRD, SB0, SB1, "anchor count is not 2"), "V1 three sites");
    CHECK(fails_with(F_NO_MAGIC, SB0, SB1, "no /100 multiplier before a site"),
          "V2 missing movt");
    CHECK(fails_with(F_MAGIC_SPLIT, SB0, SB1, "no /100 multiplier before a site"),
          "V2 movw/movt on different registers");
    CHECK(fails_with(F_NO_CMP, SB0, SB1, "no cmp/bge binding"), "V3 no cmp");
    CHECK(fails_with(F_CMP_SWAP, SB0, SB1, "no cmp/bge binding"), "V3 cmp operands swapped");
    CHECK(fails_with(F_B0_FIRST, SB0, SB1, "site order/distance"), "V3 B0 before B1");
    CHECK(fails_with(F_FAR, SB0, SB1, "site order/distance"), "V3 sites too far apart");
    CHECK(fails_with(F_NO_ASR8, SB0, SB1, "no /800 tail"), "V4 no asr #8");
    CHECK(fails_with(F_NO_40, SB0, SB1, "no +40 tail"), "V4 no +40");
    CHECK(fails_with(F_NO_20, SB0, SB1, "no +20 at branch target"), "V4 no +20");
    CHECK(fails_with(0, SB0, 0x1F00, "bias is a single immediate"), "V5 encodable bias");
    CHECK(fails_with(0, SB1, SB1, "biases equal"), "V5 equal");
    CHECK(fails_with(0, SB1, SB0, "biases not ordered"), "B0 > B1");
    CHECK(fails_with(0, SB0, 30001, "bias out of range"), "B1 above window");
    CHECK(fails_with(0, 999, SB1, "bias out of range"), "B0 below window");
}

/* Minimal ELF32 LE ARM: a non-exec PT_LOAD with a decoy pair, then the exec one. */
static uint8_t elf[40000];

static void le32(uint8_t *p, uint32_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static size_t build_elf(void)
{
    size_t code, decoy, len;

    memset(elf, 0, sizeof(elf));
    memcpy(elf, "\177ELF", 4);
    elf[4] = 1; elf[5] = 1; elf[6] = 1;
    le16(elf + 18, 40);
    le32(elf + 28, 52);
    le16(elf + 42, 32);
    le16(elf + 44, 2);
    decoy = 256;
    build(3000, 9000, 0);
    memcpy(elf + decoy, img, (size_t)n * 4);
    le32(elf + 52 + 0, 1);
    le32(elf + 52 + 4, (uint32_t)decoy);
    le32(elf + 52 + 16, (uint32_t)n * 4);
    le32(elf + 52 + 24, 4);                   /* PF_R only */
    code = decoy + (size_t)n * 4 + 64;
    build(SB0, SB1, 0);
    memcpy(elf + code, img, (size_t)n * 4);
    le32(elf + 84 + 0, 1);
    le32(elf + 84 + 4, (uint32_t)code);
    le32(elf + 84 + 16, (uint32_t)n * 4);
    le32(elf + 84 + 24, 5);                   /* PF_R | PF_X */
    len = code + (size_t)n * 4 + 32;
    return len;
}

static void test_elf(void)
{
    freeisp_ae_out_bias_t out;
    const char *why = NULL;
    size_t len = build_elf();

    CHECK(freeisp_ae_out_bias_extract(elf, len, out, &why) == 0 &&
          out[0] == SB0 && out[1] == SB1, "ELF: only the exec segment is scanned");

    le32(elf + 52 + 24, 5);
    CHECK(freeisp_ae_out_bias_extract(elf, len, out, &why) == 0 && out[0] == 3000,
          "ELF: first exec segment wins");
    le32(elf + 52 + 24, 4);
    le32(elf + 84 + 24, 4);
    CHECK(freeisp_ae_out_bias_extract(elf, len, out, &why) != 0 &&
          strcmp(why, "no executable segment") == 0, "ELF: no exec segment");
    le32(elf + 84 + 24, 5);
    le32(elf + 84 + 16, 0x7FFFFFF0u);
    CHECK(freeisp_ae_out_bias_extract(elf, len, out, &why) != 0 &&
          strcmp(why, "malformed ELF program headers") == 0, "ELF: segment past end");
    len = build_elf();
    le32(elf + 28, 0xFFFFFFF0u);
    CHECK(freeisp_ae_out_bias_extract(elf, len, out, &why) != 0 &&
          strcmp(why, "malformed ELF program headers") == 0, "ELF: phoff past end");
    len = build_elf();
    le16(elf + 44, 0xFFFF);
    CHECK(freeisp_ae_out_bias_extract(elf, len, out, &why) != 0, "ELF: phnum too large");
    len = build_elf();
    le16(elf + 42, 8);
    CHECK(freeisp_ae_out_bias_extract(elf, len, out, &why) != 0, "ELF: phentsize too small");
    len = build_elf();
    le16(elf + 18, 3);
    CHECK(freeisp_ae_out_bias_extract(elf, len, out, &why) != 0, "ELF: not ARM");
    CHECK(freeisp_ae_out_bias_extract(elf, 40, out, &why) != 0, "ELF: header truncated");
}

/* Every prefix and pseudo-random junk: must not crash, only the full image extracts. */
static void test_robust(void)
{
    freeisp_ae_out_bias_t out;
    const char *why;
    size_t len, full, ok = 0;
    uint32_t x = 12345;
    int i, k;

    full = build_elf();
    for (len = 0; len <= full; len++)
        if (freeisp_ae_out_bias_extract(elf, len, out, &why) == 0)
            ok++;
    CHECK(ok > 0 && freeisp_ae_out_bias_extract(elf, full - 33, out, &why) != 0,
          "ELF prefixes shorter than the segment fail");
    build(SB0, SB1, 0);
    full = (size_t)n * 4;
    for (len = 0, ok = 0; len < full; len++)
        if (freeisp_ae_out_bias_extract(img, len, out, &why) == 0)
            ok++;
    CHECK(ok < full, "raw prefixes do not all extract");
    CHECK(freeisp_ae_out_bias_extract(NULL, 100, out, &why) != 0, "NULL image");
    for (k = 0; k < 200; k++) {
        for (i = 0; i < 1024; i++) {
            x = x * 1103515245u + 12345u;
            img[i] = x;
        }
        img[0] = (k & 1) ? 0x464C457Fu : img[0];
        (void)freeisp_ae_out_bias_extract(img, 4096, out, &why);
    }
    CHECK(1, "junk input survives");
}

static int write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");

    if (!f)
        return -1;
    fputs(text, f);
    return fclose(f);
}

static void test_cache(void)
{
    const char *path = "build/test_ae_out_bias.txt";
    freeisp_ae_out_bias_t b = { SB0, SB1 }, got = { 0, 0 };
    const char *why = NULL;

    CHECK(freeisp_ae_out_bias_save_text(path, b) == 0, "save cache");
    CHECK(freeisp_ae_out_bias_load_text(path, got, &why) == 0 &&
          got[0] == SB0 && got[1] == SB1, "cache round trip");
    CHECK(write_file(path, "# c\n2345\n") == 0 &&
          freeisp_ae_out_bias_load_text(path, got, &why) != 0 &&
          strcmp(why, "cache file malformed") == 0, "one value rejected");
    CHECK(write_file(path, "2345 7777\n2345 7777\n") == 0 &&
          freeisp_ae_out_bias_load_text(path, got, &why) != 0, "two rows rejected");
    CHECK(write_file(path, "2345 7777 1\n") == 0 &&
          freeisp_ae_out_bias_load_text(path, got, &why) != 0, "extra field rejected");
    CHECK(write_file(path, "7777 2345\n") == 0 &&
          freeisp_ae_out_bias_load_text(path, got, &why) != 0 &&
          strcmp(why, "biases not ordered") == 0, "unordered cache rejected");
    CHECK(write_file(path, "99999999999 7777\n") == 0 &&
          freeisp_ae_out_bias_load_text(path, got, &why) != 0, "overflow rejected");
    CHECK(write_file(path, "") == 0 &&
          freeisp_ae_out_bias_load_text(path, got, &why) != 0, "empty cache rejected");
    CHECK(got[0] == SB0 && got[1] == SB1, "failed loads leave out untouched");
    remove(path);
    CHECK(freeisp_ae_out_bias_load_text(path, got, &why) != 0 &&
          strcmp(why, "no cache file") == 0, "missing cache");
}

int main(void)
{
    test_positive();
    test_failures();
    test_elf();
    test_robust();
    test_cache();
    printf("\nAE output-bias extractor tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
