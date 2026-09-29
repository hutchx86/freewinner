/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Host test for the H.265 command-block image builder against the Config A/B
 * vectors of spec h265/vectors/README.md section 2 and the fact tables of
 * 11-register-programming.md section 3. */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "freecodec/h265_regs.h"

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

static void expect(uint32_t *r, unsigned off, uint32_t want, const char *tag)
{
    checkf(r[off / 4u] == want, "%s 0x%02x: got %08x want %08x",
           tag, off, r[off / 4u], want);
}

static void expect_masked(uint32_t *r, unsigned off, uint32_t mask,
                          uint32_t want, const char *tag)
{
    checkf((r[off / 4u] & mask) == want, "%s 0x%02x: got %08x (mask %08x) want %08x",
           tag, off, r[off / 4u], mask, want);
}

/* ------------------------------------------------------------ pure helpers */

static void test_helpers(void)
{
    unsigned int th[4];

    checkf(freecodec_h265_length_stride(1280) == 2, "length_stride 1280 = 2");
    checkf(freecodec_h265_length_stride(2304) == 3, "length_stride 2304 = 3");

    freecodec_h265_dyn_me_thresholds(1280, th);
    checkf(th[0] == 4 && th[1] == 24 && th[2] == 104 && th[3] == 128,
           "dynamic-ME 720p = 4,24,104,128");
    freecodec_h265_dyn_me_thresholds(2304, th);
    checkf(th[0] == 7 && th[1] == 43 && th[2] == 187 && th[3] == 230,
           "dynamic-ME 2304 = 7,43,187,230");

    checkf(freecodec_h265_intra_thresholds(1) == 0x10010405u, "intra thresholds I");
    checkf(freecodec_h265_intra_thresholds(0) == 0x10010304u, "intra thresholds P");

    {
        uint32_t mad[3];

        freecodec_h265_mad_thresholds(1, mad);
        checkf(mad[0] == 0x0c080000u && mad[1] == 0x30241a12u && mad[2] == 0xffffffffu,
               "MAD I table");
        freecodec_h265_mad_thresholds(0, mad);
        checkf(mad[0] == 0x0a060400u && mad[1] == 0x2418100cu && mad[2] == 0xffffffffu,
               "MAD P table");
    }

    checkf(freecodec_h265_tendency_word(1280, 1) == 0xffbbb75fu, "tendency 720p I");
    checkf(freecodec_h265_tendency_word(1280, 0) == 0xdf7fb75fu, "tendency 720p P");
    checkf(freecodec_h265_tendency_word(2304, 1) == 0xffbbb75fu, "tendency 2304 I");
    checkf(freecodec_h265_tendency_word(2304, 0) == 0xffbbb75fu, "tendency 2304 P");
}

/* ------------------------------------------------------- Config A (720p) */

static void test_config_a(void)
{
    static const int qps[6] = { 37, 38, 33, 30, 29, 28 };
    static const int is_i[6] = { 1, 0, 0, 0, 0, 0 };
    static const uint32_t w04[6] = { 0xc0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0 };
    static const uint32_t w08[6] = { 0x825, 0x926, 0x921, 0x91e, 0x91d, 0x91c };
    static const uint32_t w10[6] = { 0x08c00000u, 0x08400001u, 0x08000001u,
                                     0x08000001u, 0x08000001u, 0x08000001u };
    static const uint32_t w94[6] = { 0x10010405u, 0x10010304u, 0x10010304u,
                                     0x10010304u, 0x10010304u, 0x10010304u };
    static const uint32_t w74[6] = { 0xffbbb75fu, 0xdf7fb75fu, 0xdf7fb75fu,
                                     0xdf7fb75fu, 0xdf7fb75fu, 0xdf7fb75fu };
    static const uint32_t w68[6] = { 0x18a00u, 0x31400u, 0x31400u,
                                     0x31400u, 0x31400u, 0x31400u };
    static const uint32_t w6c[6] = { 0x3cc8027bu, 0x3cc80382u, 0x3cc80382u,
                                     0x3cc80382u, 0x3cc80382u, 0x3cc80382u };
    uint32_t r[0x200];
    freecodec_h265_frame_cfg c;
    int i;

    for (i = 0; i < 6; i++) {
        memset(&c, 0, sizeof(c));
        c.width_px = 1280;
        c.height_px = 720;
        c.length_stride = freecodec_h265_length_stride(1280);
        c.is_i = is_i[i];
        c.picture_index = (unsigned int)i;
        c.qp = qps[i];
        c.rc_mode = FWM_VENC_H265_RC_CBR;
        c.dynamic_me_en = is_i[i] ? 0 : 1;
        /* MV fields: write from the first P, read from the second (intra 40). */
        c.tmvp_write_phy = is_i[i] ? 0u : 0x4500u;
        c.tmvp_read_phy = (is_i[i] || i == 1) ? 0u : 0x4500u;
        c.lambda = is_i[i] ? 0x18a00u : 0x31400u;
        c.lambda_sqrt = is_i[i] ? 635u : 898u;
        c.lambda_c = is_i[i] ? 0x18a00u : 0x138b7u;
        c.th_bright = 0xc8u;
        c.th_dark = 0x3cu;
        c.roi_disable_mask = 0xffu;
        freecodec_h265_config_registers(&c, r);

        expect(r, 0x04, w04[i], "A");
        expect(r, 0x08, w08[i], "A");
        expect(r, 0x10, w10[i], "A");
        expect(r, 0x94, w94[i], "A");
        expect(r, 0x74, w74[i], "A");
        expect_masked(r, 0x68, 0x3ffffu, w68[i], "A");
        expect(r, 0x6c, w6c[i], "A");
        expect(r, 0x40, 0x74000000u, "A");
        expect(r, 0x44, 0x0c1e0000u, "A");
        expect(r, 0x48, 0x00180004u, "A");
        expect(r, 0x4c, 0x00800068u, "A");
        expect_masked(r, 0x2c, 0xff000000u, 0x98000000u, "A");
        expect_masked(r, 0x70, 0xff000000u, 0xff000000u, "A");
        expect(r, 0x38, 0xffffffffu, "A");
        if (is_i[i]) {
            expect(r, 0x30, 0x0c080000u, "A");
            expect(r, 0x34, 0x30241a12u, "A");
        } else {
            expect(r, 0x30, 0x0a060400u, "A");
            expect(r, 0x34, 0x2418100cu, "A");
        }
    }
}

/* --------------------------------------------------- Config B (2304-wide) */

static void test_config_b(void)
{
    static const int qps[6] = { 35, 37, 38, 38, 38, 38 };
    static const int is_i[6] = { 1, 0, 0, 0, 0, 0 };
    static const uint32_t w08[6] = { 0xc23, 0xd25, 0xd26, 0xd26, 0xd26, 0xd26 };
    uint32_t r[0x200];
    freecodec_h265_frame_cfg c;
    int i;

    for (i = 0; i < 6; i++) {
        memset(&c, 0, sizeof(c));
        c.width_px = 2304;
        c.height_px = 1296;
        c.length_stride = freecodec_h265_length_stride(2304);
        c.is_i = is_i[i];
        c.picture_index = (unsigned int)i;
        c.qp = qps[i];
        c.rc_mode = FWM_VENC_H265_RC_CBR;
        c.dynamic_me_en = is_i[i] ? 0 : 1;
        /* MV fields: write from the first P, read from the second (intra 40). */
        c.tmvp_write_phy = is_i[i] ? 0u : 0xcd00u;
        c.tmvp_read_phy = (is_i[i] || i == 1) ? 0u : 0xcd00u;
        c.lambda = is_i[i] ? 0x18a00u : 0x31400u;
        c.lambda_sqrt = is_i[i] ? 635u : 898u;
        c.th_bright = 0xc8u;
        c.th_dark = 0x3cu;
        c.roi_disable_mask = 0xffu;
        freecodec_h265_config_registers(&c, r);

        expect(r, 0x04, is_i[i] ? 0xc0u : 0xd0u, "B");
        expect(r, 0x08, w08[i], "B");
        expect(r, 0x10, is_i[i] ? 0x08c00000u : (i == 1 ? 0x08400001u : 0x08000001u), "B");
        expect(r, 0x74, 0xffbbb75fu, "B");
        expect(r, 0x48, 0x002b0007u, "B");
        expect(r, 0x4c, 0x00e600bbu, "B");
        expect_masked(r, 0x68, 0x3ffffu, is_i[i] ? 0x18a00u : 0x31400u, "B");
    }
}

/* The ABR and FixQP flag words of 10 section 1 / vectors section 2.5. */
static void test_rc_modes(void)
{
    uint32_t r[0x200];
    freecodec_h265_frame_cfg c;

    memset(&c, 0, sizeof(c));
    c.width_px = 1280;
    c.height_px = 720;
    c.length_stride = 2;
    c.qp = 30;

    c.is_i = 1;
    c.rc_mode = FWM_VENC_H265_RC_FIXQP;
    c.allocate_bits = 0;
    freecodec_h265_config_registers(&c, r);
    expect_masked(r, 0x2c, 0xff000000u, 0x18000000u, "FixQP");
    expect(r, 0x30, 0u, "FixQP MAD zero");
    expect(r, 0x34, 0u, "FixQP MAD zero");

    c.is_i = 0;
    c.rc_mode = FWM_VENC_H265_RC_ABR;
    freecodec_h265_config_registers(&c, r);
    expect_masked(r, 0x2c, 0xff000000u, 0x58000000u, "ABR");
    expect(r, 0x30, 0u, "ABR MAD zero");

    c.rc_mode = FWM_VENC_H265_RC_VBR;
    c.img_bin_enable = 1;
    freecodec_h265_config_registers(&c, r);
    expect_masked(r, 0x2c, 0xff000000u, 0x9a000000u, "VBR f1 img-bin");

    c.rc_mode = FWM_VENC_H265_RC_CBR;
    c.img_bin_enable = 0;
    c.allocate_bits = 0x123456u;
    freecodec_h265_config_registers(&c, r);
    expect_masked(r, 0x2c, 0x003fffffu, 0x123456u, "CBR allocate bits");
    expect_masked(r, 0x2c, 0xff000000u, 0x98000000u, "CBR flags");
}

int main(void)
{
    test_helpers();
    test_config_a();
    test_config_b();
    test_rc_modes();

    if (g_fail) {
        fprintf(stderr, "test_regs: FAILED\n");
        return 1;
    }
    printf("test_regs: ok\n");
    return 0;
}
