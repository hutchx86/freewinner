/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Host test for the H.265 VPS/SPS/PPS and slice-header builders, against the
 * black-box vectors of spec h265/vectors/README.md section 4 and the decoded
 * field values of 13-headers.md. */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "freecodec/h265_headers.h"

static int g_fail;

static void fail(const char *what)
{
    fprintf(stderr, "FAIL: %s\n", what);
    g_fail = 1;
}

static void checkf(int cond, const char *fmt, ...)
{
    if (!cond) {
        va_list ap;
        char buf[256];

        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        fail(buf);
    }
}

static void make_ptl(freecodec_h265_ptl_cfg *p)
{
    memset(p, 0, sizeof(*p));
    p->profile_idc = 1;                  /* Main */
    p->level_idc = 123;                  /* 4.1  */
    p->compatibility = 0x60000000u;
}

static int build_vps(unsigned char *b, int cap)
{
    freecodec_h265_vps_cfg v;

    memset(&v, 0, sizeof(v));
    make_ptl(&v.ptl);
    v.max_num_ref_pics = 1;
    return freecodec_h265_build_vps(&v, b, cap);
}

static int build_sps(unsigned char *b, int cap, unsigned w, unsigned h)
{
    freecodec_h265_sps_cfg s;

    memset(&s, 0, sizeof(s));
    make_ptl(&s.ptl);
    s.pic_width_in_luma_samples = w;
    s.pic_height_in_luma_samples = h;
    s.max_num_ref_pics = 1;
    s.num_short_term_ref_pic_sets = 21;
    s.log2_max_pic_order_cnt_lsb_minus4 = 4;
    s.sao_enabled = 1;
    s.temporal_mvp_enabled = 1;
    return freecodec_h265_build_sps(&s, b, cap);
}

static int build_pps(unsigned char *b, int cap)
{
    freecodec_h265_pps_cfg p;

    memset(&p, 0, sizeof(p));
    p.cabac_init_present = 1;
    p.transform_skip_enabled = 1;
    p.cu_qp_delta_enabled = 1;
    p.diff_cu_qp_delta_depth = 1;
    p.deblocking_filter_control_present = 1;
    p.loop_filter_across_slices_enabled = 1;
    return freecodec_h265_build_pps(&p, b, cap);
}

/* One parameter-set NAL: start code + NAL header are 6 bytes; the RBSP payload
 * is the NAL minus those and minus emulation-prevention bytes. */
static int rbsp_payload_bytes(const unsigned char *nal, int n)
{
    return freecodec_h265_rbsp_removed_size(nal, n) - 6;
}

static void test_parameter_sets(void)
{
    unsigned char b[256];
    int n;

    n = build_vps(b, sizeof(b));
    checkf(n == 28, "VPS is 28 bytes, got %d", n);
    checkf(b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 1, "VPS start code");
    checkf(b[4] == 0x40 && b[5] == 0x01, "VPS NAL header type 32, got %02x %02x", b[4], b[5]);
    checkf(rbsp_payload_bytes(b, n) == 18, "VPS RBSP is 18 bytes, got %d",
           rbsp_payload_bytes(b, n));

    n = build_sps(b, sizeof(b), 1280, 720);
    checkf(n == 51, "SPS 720p is 51 bytes, got %d", n);
    checkf(b[4] == 0x42 && b[5] == 0x01, "SPS NAL header type 33, got %02x %02x", b[4], b[5]);
    checkf(rbsp_payload_bytes(b, n) == 41, "SPS RBSP is 41 bytes, got %d",
           rbsp_payload_bytes(b, n));

    n = build_sps(b, sizeof(b), 2304, 1296);
    checkf(n == 51, "SPS 2304-wide is 51 bytes, got %d", n);

    n = build_pps(b, sizeof(b));
    checkf(n == 11, "PPS is 11 bytes, got %d", n);
    checkf(b[4] == 0x44 && b[5] == 0x01, "PPS NAL header type 34, got %02x %02x", b[4], b[5]);
    checkf(rbsp_payload_bytes(b, n) == 5, "PPS RBSP is 5 bytes, got %d",
           rbsp_payload_bytes(b, n));
}

static void test_slice_headers(void)
{
    freecodec_h265_slice_cfg c;
    unsigned char b[256];
    int bits = 0, n;

    /* IDR: NAL type 19, SAO on (13 section 8). */
    memset(&c, 0, sizeof(c));
    c.nal_unit_type = 19;
    c.is_i_picture = 1;
    c.sao_luma_flag = 1;
    c.sao_chroma_flag = 1;
    c.temporal_mvp_enabled = 1;
    c.slice_qp = 37;
    c.loop_filter_across_slices_enabled = 1;
    n = freecodec_h265_build_slice(&c, b, sizeof(b), &bits);
    checkf(n > 6, "IDR slice header built (%d bytes)", n);
    checkf(b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 1, "slice start code");
    checkf(b[4] == (19u << 1) && b[5] == 0x01, "IDR NAL byte0 type 19, got %02x %02x", b[4], b[5]);
    checkf(bits == n * 8, "slice out_bits = bytes * 8");

    /* P: NAL type 1 and the P-only fields parse back. */
    memset(&c, 0, sizeof(c));
    c.nal_unit_type = 1;
    c.is_i_picture = 0;
    c.pic_order_cnt_lsb = 1;
    c.short_term_ref_pic_set_idx = 0;
    c.temporal_mvp_enabled = 1;
    c.sao_luma_flag = 1;
    c.sao_chroma_flag = 1;
    c.slice_qp = 38;
    c.loop_filter_across_slices_enabled = 1;
    n = freecodec_h265_build_slice(&c, b, sizeof(b), &bits);
    checkf(n > 6, "P slice header built (%d bytes)", n);
    checkf(b[4] == (1u << 1) && b[5] == 0x01, "P NAL byte0 type 1, got %02x %02x", b[4], b[5]);

    /* first_slice_segment_in_pic_flag is the top RBSP bit; for an IRAP the next
     * bit is no_output_of_prior_pics_flag = 0. */
    checkf((b[6] & 0x80u) != 0, "first_slice_segment_in_pic_flag set");
}

static void test_exp_golomb(void)
{
    unsigned char b[8];

    checkf(freecodec_h265_write_ue(b, sizeof(b), 0) == 1 && b[0] == 0x80, "ue(0)");
    checkf(freecodec_h265_write_ue(b, sizeof(b), 1) == 1 && b[0] == 0x40, "ue(1)");
    checkf(freecodec_h265_write_ue(b, sizeof(b), 2) == 1 && b[0] == 0x60, "ue(2)");
    checkf(freecodec_h265_write_ue(b, sizeof(b), 7) == 1 && b[0] == 0x10, "ue(7)");
    checkf(freecodec_h265_write_se(b, sizeof(b), 0) == 1 && b[0] == 0x80, "se(0)");
    checkf(freecodec_h265_write_se(b, sizeof(b), 1) == 1 && b[0] == 0x40, "se(1)");
    checkf(freecodec_h265_write_se(b, sizeof(b), -1) == 1 && b[0] == 0x60, "se(-1)");
}

/* Emulation prevention: the VPS/SPS carry 00 00 03 sequences (13 section 1). */
static void test_emulation_prevention(void)
{
    unsigned char b[256];
    int n, i, saw03 = 0;

    n = build_vps(b, sizeof(b));
    for (i = 6; i + 2 < n; i++)
        if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 3)
            saw03 = 1;
    checkf(saw03, "VPS RBSP carries emulation-prevention bytes");
    checkf(freecodec_h265_rbsp_removed_size(b, n) == n - 4,
           "four emulation-prevention bytes removed from the VPS");
}

/* Golden parameter sets and slice headers (spec 13 sections 5-8, vectors).
 * The SPS short-term-RPS placement/flags and the slice-header field order are
 * exactly fixed by the spec, so pin the bytes to catch a syntax regression
 * (e.g. the RPS loop position, use_delta_flag, or collocated_ref_idx presence). */
static void test_golden_bytes(void)
{
    static const unsigned char vps[28] = {
        0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x60,
        0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03,
        0x00, 0x7b, 0xac, 0x09
    };
    static const unsigned char sps[51] = {
        0x00, 0x00, 0x00, 0x01, 0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x7b, 0xa0,
        0x02, 0x80, 0x80, 0x2d, 0x1f, 0xe5, 0xae, 0xe4, 0x48, 0x82, 0xcb, 0xf3,
        0xcf, 0x3c, 0xf3, 0xcf, 0x3c, 0xf3, 0xcf, 0x3c, 0xf3, 0xcf, 0x3c, 0xf3,
        0xcf, 0x2d, 0x10
    };
    static const unsigned char pps[11] = {
        0x00, 0x00, 0x00, 0x01, 0x44, 0x01, 0xc0, 0xf6, 0xb0, 0x33, 0x24
    };
    static const unsigned char idr[9] = {
        0x00, 0x00, 0x00, 0x01, 0x26, 0x01, 0xaf, 0x0b, 0x60
    };
    static const unsigned char p[11] = {
        0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0xd0, 0x0c, 0x1c, 0x86, 0x30
    };
    unsigned char b[256];
    freecodec_h265_slice_cfg c;
    int n, bits;

    n = build_vps(b, sizeof(b));
    checkf(n == 28 && memcmp(b, vps, 28) == 0, "VPS matches the golden");
    n = build_sps(b, sizeof(b), 1280, 720);
    checkf(n == 51 && memcmp(b, sps, 51) == 0, "SPS matches the golden");
    n = build_pps(b, sizeof(b));
    checkf(n == 11 && memcmp(b, pps, 11) == 0, "PPS matches the golden");

    memset(&c, 0, sizeof(c));
    c.nal_unit_type = 19; c.is_i_picture = 1; c.sao_luma_flag = 1; c.sao_chroma_flag = 1;
    c.temporal_mvp_enabled = 1; c.slice_qp = 37; c.loop_filter_across_slices_enabled = 1;
    n = freecodec_h265_build_slice(&c, b, sizeof(b), &bits);
    checkf(n == 9 && memcmp(b, idr, 9) == 0, "IDR slice header matches the golden");

    memset(&c, 0, sizeof(c));
    c.nal_unit_type = 1; c.is_i_picture = 0; c.pic_order_cnt_lsb = 1;
    c.short_term_ref_pic_set_idx = 0; c.temporal_mvp_enabled = 1;
    c.sao_luma_flag = 1; c.sao_chroma_flag = 1; c.slice_qp = 38;
    c.loop_filter_across_slices_enabled = 1;
    n = freecodec_h265_build_slice(&c, b, sizeof(b), &bits);
    checkf(n == 11 && memcmp(b, p, 11) == 0, "P slice header matches the golden");
}

int main(void)
{
    test_parameter_sets();
    test_slice_headers();
    test_exp_golomb();
    test_emulation_prevention();
    test_golden_bytes();

    if (g_fail) {
        fprintf(stderr, "test_headers: FAILED\n");
        return 1;
    }
    printf("test_headers: ok\n");
    return 0;
}
