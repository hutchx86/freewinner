/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* ae_out_bias.c - extract the AE network output biases from the stock daemon: each is an
 * A32 immediate split over two adds right after a signed /100 (spec ae.md 8.5). Only that
 * code shape and public A32 encodings are known here; the values come from the image. */
#include "freeisp/ae_out_bias.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIV100_MAGIC_LO  0x851Fu      /* movw half of the /100 multiplier */
#define DIV100_MAGIC_HI  0x51EBu      /* movt half                        */
#define MAGIC_WINDOW     12           /* insns searched before the rsb    */
#define SITE_TAIL        64           /* bytes a site may need after it   */
#define ROLE_MAX_DIST    4096         /* B0 site follows B1 within this   */
#define MAX_SITES        3

typedef struct {
    size_t   lo, hi;                  /* scanned byte range [lo, hi)      */
    const uint8_t *img;
} seg_t;

typedef struct {
    size_t   rsb, end;                /* rsb and final add offsets        */
    unsigned rd;
    uint32_t val;
} site_t;

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

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* Word at byte offset `off`; 0 when it is not wholly inside the segment. */
static int word(const seg_t *s, long long off, uint32_t *w)
{
    if (off < (long long)s->lo || off + 4 > (long long)s->hi)
        return 0;
    *w = rd32(s->img + off);
    return 1;
}

static uint32_t a32_imm(uint32_t imm12)
{
    uint32_t v = imm12 & 0xFFu;
    unsigned r = (unsigned)((imm12 >> 8) & 0xFu) * 2u;

    return r ? ((v >> r) | (v << (32u - r))) : v;
}

/* True if v fits one A32 modified immediate (the compiler would not split it). */
static int a32_encodable(uint32_t v)
{
    unsigned r;

    for (r = 0; r < 32; r += 2)
        if (((r ? (v << r) | (v >> (32u - r)) : v)) < 256u)
            return 1;
    return 0;
}

static int is_rsb_asr5(uint32_t w)
{
    return (w >> 28) == 0xEu && (w & 0x0FE00010u) == 0x00600000u &&
           ((w >> 5) & 3u) == 2u && ((w >> 7) & 31u) == 5u;
}

/* add rd, rn, #imm (always, no flags). */
static int add_imm(uint32_t w, unsigned *rd, unsigned *rn, uint32_t *val)
{
    if ((w >> 28) != 0xEu || (w & 0x0FF00000u) != 0x02800000u)
        return 0;
    *rd = (w >> 12) & 0xFu;
    *rn = (w >> 16) & 0xFu;
    *val = a32_imm(w & 0xFFFu);
    return 1;
}

static int add_imm_is(const seg_t *s, long long off, uint32_t want)
{
    unsigned rd, rn;
    uint32_t w, v;

    return word(s, off, &w) && add_imm(w, &rd, &rn, &v) && v == want;
}

/* movw (op 0x30) / movt (op 0x34) with the given 16-bit immediate. */
static int mov16(uint32_t w, uint32_t op, uint32_t imm, unsigned *rd)
{
    if ((w >> 28) != 0xEu || (w & 0x0FF00000u) != op ||
        ((((w >> 4) & 0xF000u) | (w & 0xFFFu)) != imm))
        return 0;
    *rd = (w >> 12) & 0xFu;
    return 1;
}

/* Scan range: the first executable PT_LOAD of an ELF32 LE ARM image, else the whole buffer. */
static int find_segment(const uint8_t *img, size_t len, seg_t *s, const char **why)
{
    unsigned long long phoff, ent, num, i;

    s->img = img;
    s->lo = 0;
    s->hi = len;
    if (len < 4 || memcmp(img, "\177ELF", 4) != 0)
        return 0;
    if (len < 52 || img[4] != 1 || img[5] != 1 || rd16(img + 18) != 40) {
        set_why(why, "not a 32-bit little-endian ARM ELF");
        return -1;
    }
    phoff = rd32(img + 28);
    ent = rd16(img + 42);
    num = rd16(img + 44);
    if (ent < 32 || phoff > len || num * ent > len - phoff) {
        set_why(why, "malformed ELF program headers");
        return -1;
    }
    for (i = 0; i < num; i++) {
        const uint8_t *ph = img + phoff + i * ent;
        unsigned long long off = rd32(ph + 4), fsz = rd32(ph + 16);

        if (rd32(ph) != 1u || !(rd32(ph + 24) & 1u))
            continue;
        if (off > len || fsz > len - off) {
            set_why(why, "malformed ELF program headers");
            return -1;
        }
        s->lo = (size_t)((off + 3u) & ~3ull);
        s->hi = (size_t)(off + fsz);
        return 0;
    }
    set_why(why, "no executable segment");
    return -1;
}

/* rsb Rq,..,asr #5 ; add Rd,Rq,#hi (<=3 insns) ; add Rd,Rd,#lo (<=7 insns of the rsb). */
static int match_site(const seg_t *s, size_t off, site_t *out)
{
    uint32_t w, hi, lo;
    unsigned rq, rd, rn, rd2, rn2;
    int k, j;

    if (!word(s, (long long)off, &w) || !is_rsb_asr5(w))
        return 0;
    rq = (w >> 12) & 0xFu;
    for (k = 1; k <= 3; k++) {
        if (!word(s, (long long)off + 4 * k, &w) || !add_imm(w, &rd, &rn, &hi) || rn != rq)
            continue;
        for (j = k + 1; j <= 7; j++) {
            if (word(s, (long long)off + 4 * j, &w) && add_imm(w, &rd2, &rn2, &lo) &&
                rd2 == rd && rn2 == rd) {
                out->rsb = off;
                out->end = off + 4u * (size_t)j;
                out->rd = rd;
                out->val = hi + lo;
                return 1;
            }
        }
        return 0;
    }
    return 0;
}

/* V2: movw/movt of the /100 multiplier into one register shortly before the rsb. */
static int has_div100(const seg_t *s, size_t rsb)
{
    unsigned wl = 0, th = 0, rd;
    uint32_t w;
    int k;

    for (k = 1; k <= MAGIC_WINDOW; k++) {
        if (!word(s, (long long)rsb - 4 * k, &w))
            break;
        if (mov16(w, 0x03000000u, DIV100_MAGIC_LO, &rd))
            wl |= 1u << rd;
        if (mov16(w, 0x03400000u, DIV100_MAGIC_HI, &rd))
            th |= 1u << rd;
    }
    return (wl & th) != 0;
}

/* V3 binding: "cmp Rd_other, Rd_this ; bge" right after this site. */
static int binds_cmp_bge(const seg_t *s, const site_t *me, const site_t *other,
                         uint32_t *bge)
{
    uint32_t c, b;

    if (!word(s, (long long)me->end + 4, &c) || !word(s, (long long)me->end + 8, &b))
        return 0;
    if ((c & 0x0FF0FFF0u) != 0x01500000u || ((c >> 16) & 0xFu) != other->rd ||
        (c & 0xFu) != me->rd || (b & 0xFF000000u) != 0xAA000000u)
        return 0;
    *bge = b;
    return 1;
}

static int is_asr8(uint32_t w)
{
    if (((w >> 7) & 31u) != 8u)
        return 0;
    return (w & 0x0FE00070u) == 0x01A00040u ||                     /* mov  ..,asr #8 */
           ((w & 0x0FE00010u) == 0x00400000u && ((w >> 5) & 3u) == 2u); /* sub ..,asr #8 */
}

int freeisp_ae_out_bias_validate(const freeisp_ae_out_bias_t b, const char **why)
{
    int i;

    for (i = 0; i < FREEISP_AE_OUT_BIAS_N; i++) {
        if (b[i] < FREEISP_AE_OUT_BIAS_MIN || b[i] > FREEISP_AE_OUT_BIAS_MAX) {
            set_why(why, "bias out of range");
            return -1;
        }
        if (a32_encodable((uint32_t)b[i])) {
            set_why(why, "bias is a single immediate");
            return -1;
        }
    }
    if (b[0] == b[1]) {
        set_why(why, "biases equal");
        return -1;
    }
    if (b[0] > b[1]) {
        set_why(why, "biases not ordered");
        return -1;
    }
    return 0;
}

int freeisp_ae_out_bias_extract_at(const void *image, size_t len,
                                   freeisp_ae_out_bias_t out, size_t at[2],
                                   const char **why)
{
    const uint8_t *img = (const uint8_t *)image;
    site_t st[MAX_SITES];
    const site_t *b0 = NULL, *b1 = NULL;
    freeisp_ae_out_bias_t v;
    seg_t s;
    size_t off;
    uint32_t bge = 0, w;
    int n = 0, i, k, ok;
    long long tgt;

    if (img == NULL || len < 16) {
        set_why(why, "no image");
        return -1;
    }
    if (find_segment(img, len, &s, why) != 0)
        return -1;

    /* V1: exactly two sites in the scanned range. */
    for (off = s.lo; off + SITE_TAIL < s.hi && n < MAX_SITES; off += 4)
        if (match_site(&s, off, &st[n]))
            n++;
    if (n != 2) {
        set_why(why, n ? "anchor count is not 2" : "anchor not found");
        return -1;
    }
    for (i = 0; i < 2; i++)
        if (!has_div100(&s, st[i].rsb)) {
            set_why(why, "no /100 multiplier before a site");
            return -1;
        }

    /* V3: roles from the compare that consumes both scores. */
    for (i = 0; i < 2; i++) {
        if (!binds_cmp_bge(&s, &st[i], &st[1 - i], &w))
            continue;
        if (b1) {
            set_why(why, "ambiguous cmp/bge binding");
            return -1;
        }
        b1 = &st[i];
        b0 = &st[1 - i];
        bge = w;
    }
    if (!b1) {
        set_why(why, "no cmp/bge binding");
        return -1;
    }
    if (b0->rsb <= b1->rsb || b0->rsb - b1->rsb >= ROLE_MAX_DIST) {
        set_why(why, "site order/distance");
        return -1;
    }

    /* V4: /800 and +40 on the fall-through, +20 at the branch target. */
    ok = 0;
    for (k = 3; k <= 14; k++)
        if (word(&s, (long long)b1->end + 4 * k, &w) && is_asr8(w))
            ok = 1;
    if (!ok) {
        set_why(why, "no /800 tail");
        return -1;
    }
    ok = 0;
    for (k = 3; k <= 14; k++)
        if (add_imm_is(&s, (long long)b1->end + 4 * k, 40))
            ok = 1;
    if (!ok) {
        set_why(why, "no +40 tail");
        return -1;
    }
    tgt = (long long)b1->end + 16 + 4LL * ((int32_t)(bge << 8) >> 8);
    ok = 0;
    for (k = 0; k <= 4; k++)
        if (add_imm_is(&s, tgt + 4 * k, 20))
            ok = 1;
    if (!ok) {
        set_why(why, "no +20 at branch target");
        return -1;
    }

    /* V5 and plausibility. */
    if (b0->val < 1 || b0->val > 0xFFFFu || b1->val < 1 || b1->val > 0xFFFFu) {
        set_why(why, "bias out of range");
        return -1;
    }
    v[0] = (int32_t)b0->val;
    v[1] = (int32_t)b1->val;
    if (freeisp_ae_out_bias_validate(v, why) != 0)
        return -1;
    out[0] = v[0];
    out[1] = v[1];
    if (at) {
        at[0] = b0->rsb;
        at[1] = b1->rsb;
    }
    set_why(why, "ok");
    return 0;
}

int freeisp_ae_out_bias_extract(const void *image, size_t len,
                                freeisp_ae_out_bias_t out, const char **why)
{
    return freeisp_ae_out_bias_extract_at(image, len, out, NULL, why);
}

int freeisp_ae_out_bias_extract_file(const char *path, freeisp_ae_out_bias_t out,
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
    rc = freeisp_ae_out_bias_extract(buf, (size_t)n, out, why);
    free(buf);
    return rc;
}

int freeisp_ae_out_bias_load_text(const char *path, freeisp_ae_out_bias_t out,
                                  const char **why)
{
    freeisp_ae_out_bias_t b;
    char line[160];
    FILE *f;
    int rows = 0;

    if (path == NULL || (f = fopen(path, "r")) == NULL) {
        set_why(why, "no cache file");
        return -1;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        long v0, v1;
        char extra;

        if (line[0] == '#' || line[strspn(line, " \t\r\n")] == '\0')
            continue;
        if (rows >= 1 || sscanf(line, "%ld %ld %c", &v0, &v1, &extra) != 2 ||
            v0 < INT32_MIN || v0 > INT32_MAX || v1 < INT32_MIN || v1 > INT32_MAX) {
            fclose(f);
            set_why(why, "cache file malformed");
            return -1;
        }
        b[0] = (int32_t)v0;
        b[1] = (int32_t)v1;
        rows++;
    }
    fclose(f);
    if (rows != 1) {
        set_why(why, "cache file malformed");
        return -1;
    }
    if (freeisp_ae_out_bias_validate(b, why) != 0)
        return -1;
    out[0] = b[0];
    out[1] = b[1];
    set_why(why, "ok");
    return 0;
}

int freeisp_ae_out_bias_save_text(const char *path, const freeisp_ae_out_bias_t b)
{
    char tmp[512];
    FILE *f;

    if (path == NULL || snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
        return -1;
    if ((f = fopen(tmp, "w")) == NULL)
        return -1;
    fprintf(f, "# AE backlight-network output biases read from this camera's own firmware.\n"
               "# Device data: do not copy or redistribute.\n"
               "# B0 B1\n"
               "%d %d\n", (int)b[0], (int)b[1]);
    if (fclose(f) != 0 || rename(tmp, path) != 0)
        return -1;
    return 0;
}
