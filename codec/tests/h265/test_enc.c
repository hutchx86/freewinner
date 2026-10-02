/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Host test for the H.265 encoder device: a fake engine and fake memops drive
 * the fwm_venc_device_t table with no kernel, exactly like the H.264 device
 * test. Checks the lifecycle, the 90-byte VPS/SPS/PPS blob, the per-picture
 * command-block image and the [SHAPE] address relations of spec h265 11/12. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freecodec/h265_headers.h"
#include "freecodec/vencoder.h"

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

/* ============================================================ fake engine */

#define WIN_SIZE 0x2000u

typedef struct fake_ve {
    unsigned char win[WIN_SIZE];
    int           lock_depth;
    int           wait_fail;
    long long     sim_bits;
    unsigned int  sim_activity;
} fake_ve;

static fake_ve g_ve;

static void fake_ve_reset_win(fake_ve *fv)
{
    memset(fv->win + 0xb00, 0, 0x200);
    memset(fv->win + 0xa00, 0, 0x100);
    *(uint32_t *)(fv->win + 0xb1c) = 1u << 9;   /* bit-writer ready */
}

static void *fake_ve_open(fc_ve_config *cfg) { (void)cfg; return &g_ve; }
static void  fake_ve_close(void *v) { (void)v; }
static int   fake_ve_lock(void *v) { fake_ve *fv = v; fv->lock_depth++; return 0; }
static int   fake_ve_unlock(void *v) { fake_ve *fv = v; fv->lock_depth--; return 0; }
static void  fake_ve_reset(void *v) { fake_ve_reset_win((fake_ve *)v); }

static int fake_ve_wait(void *v)
{
    fake_ve *fv = v;
    uint32_t *r90 = (uint32_t *)(fv->win + 0xb90);
    uint32_t *r1c = (uint32_t *)(fv->win + 0xb1c);
    uint32_t *r50 = (uint32_t *)(fv->win + 0xb50);

    if (fv->wait_fail) {
        fv->wait_fail = 0;
        return -1;
    }
    *r90 += (uint32_t)fv->sim_bits;
    *r1c = (*r1c & ~0xfu) | 0x1u;
    *r50 = fv->sim_activity;
    return 0;
}

static void *fake_ve_group_base(void *v, int group)
{
    fake_ve *fv = v;

    return (group == FC_VE_GROUP_TOP) ? fv->win : NULL;
}
static int  fake_ve_dram_type(void *v) { (void)v; return 3; }
static void fake_ve_enc_on(void *v) { (void)v; }
static void fake_ve_enc_off(void *v) { (void)v; }

static fc_ve_ops g_ve_ops;

static void install_fake_ve(void)
{
    memset(&g_ve, 0, sizeof(g_ve));
    memset(&g_ve_ops, 0, sizeof(g_ve_ops));
    g_ve_ops.open = fake_ve_open;
    g_ve_ops.close = fake_ve_close;
    g_ve_ops.lock = fake_ve_lock;
    g_ve_ops.unlock = fake_ve_unlock;
    g_ve_ops.reset = fake_ve_reset;
    g_ve_ops.wait_irq = fake_ve_wait;
    g_ve_ops.group_base = fake_ve_group_base;
    g_ve_ops.dram_type = fake_ve_dram_type;
    g_ve_ops.enc_enable = fake_ve_enc_on;
    g_ve_ops.enc_disable = fake_ve_enc_off;
    fake_ve_reset_win(&g_ve);
}

/* =========================================================== fake memops */

static void *fake_palloc(int size, void *ve_ops, void *ve_self)
{
    (void)ve_ops; (void)ve_self;
    return calloc(1, (size_t)size);
}
static void fake_pfree(void *mem, void *ve_ops, void *ve_self)
{
    (void)ve_ops; (void)ve_self;
    free(mem);
}
static void fake_flush(void *mem, int size) { (void)mem; (void)size; }
static void *fake_get_phy(void *vir) { return vir; }

static struct vb_mem_ops g_memops;

static void install_fake_memops(void)
{
    memset(&g_memops, 0, sizeof(g_memops));
    g_memops.palloc = fake_palloc;
    g_memops.palloc_no_cache = fake_palloc;
    g_memops.pfree = fake_pfree;
    g_memops.flush_cache = fake_flush;
    g_memops.ve_get_phyaddr = fake_get_phy;
    g_memops.cpu_get_phyaddr = fake_get_phy;
}

/* ================================================================ helpers */

static fwm_venc_base_config_t make_base_config(unsigned int w, unsigned int h)
{
    fwm_venc_base_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.nalu_en = 1;
    cfg.input_width = w;
    cfg.input_height = h;
    cfg.input_format = FWM_VENC_PIXEL_YUV420SP;
    cfg.memops = &g_memops;
    cfg.engine_ops = &g_ve_ops;
    cfg.engine = &g_ve;
    return cfg;
}

static fwm_venc_input_picture_t make_input(void)
{
    fwm_venc_input_picture_t in;
    static unsigned char y[4096], c[4096];

    memset(&in, 0, sizeof(in));
    in.luma_phys = y;
    in.chroma_phys = c;
    in.luma_virt = y;
    in.chroma_virt = c;
    in.pts = 1234;
    return in;
}

static uint32_t rd(unsigned int off)
{
    return *(uint32_t *)(g_ve.win + 0xb00 + off);
}

static void check_eq32(unsigned int off, uint32_t want, const char *tag)
{
    checkf(rd(off) == want, "%s 0x%02x: got %08x want %08x", tag, off, rd(off), want);
}

/* ================================================================ tests */

static void test_capability_and_lifecycle(void)
{
    fwm_venc_device_t *dev = &video_encoder_h265;
    fwm_venc_base_config_t cfg = make_base_config(1280, 720);
    void *h;

    install_fake_ve();
    install_fake_memops();

    /* Unmatched IC: open fails (12 section 5). */
    checkf(dev->open(&cfg, 0x1708u) == NULL, "open rejects an IC with no capability record");

    h = dev->open(&cfg, 0x21110u);
    checkf(h != NULL, "open accepts the V831 IC id");
    if (!h)
        return;
    checkf(dev->init(h, &cfg) == 0, "init returns OK");
    dev->close(h);
}

static void test_header_blob(void)
{
    fwm_venc_device_t *dev = &video_encoder_h265;
    fwm_venc_base_config_t cfg = make_base_config(1280, 720);
    fwm_venc_header_blob_t hd;
    void *h;

    install_fake_ve();
    install_fake_memops();
    h = dev->open(&cfg, 0x21110u);
    if (!h) { fail("open for header test"); return; }
    if (dev->init(h, &cfg) != 0) { fail("init for header test"); dev->close(h); return; }

    checkf(dev->get_parameter(h, FWM_VENC_PARAM_H265_HEADER, &hd) == 0,
           "get H265 header");
    checkf(hd.length == 90, "header blob is 90 bytes, got %u", hd.length);
    checkf(hd.data[0] == 0 && hd.data[1] == 0 && hd.data[2] == 0 && hd.data[3] == 1,
           "header starts with an Annex-B start code");
    checkf(hd.data[4] == 0x40 && hd.data[28 + 4] == 0x42 && hd.data[28 + 51 + 4] == 0x44,
           "VPS/SPS/PPS NAL types at their 28/51-byte offsets");

    checkf(dev->get_parameter(h, FWM_VENC_PARAM_SIZE, &(fwm_venc_size_t){0}) == 0,
           "get SIZE");
    dev->close(h);
}

static void test_encode_sequence(void)
{
    fwm_venc_device_t *dev = &video_encoder_h265;
    fwm_venc_base_config_t cfg = make_base_config(1280, 720);
    fwm_venc_fixed_qp_t fqp;
    void *h;
    int i;
    uint32_t prev_rec = 0;
    static const uint32_t exp04[6] = { 0xc0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0 };
    static const int exp_qp[6] = { 37, 38, 38, 38, 38, 38 };
    static const uint32_t exp10[6] = { 0x08c00000u, 0x08400001u, 0x08000001u,
                                       0x08000001u, 0x08000001u, 0x08000001u };

    install_fake_ve();
    install_fake_memops();
    h = dev->open(&cfg, 0x21110u);
    if (!h) { fail("open for encode test"); return; }
    if (dev->init(h, &cfg) != 0) { fail("init for encode test"); dev->close(h); return; }

    memset(&fqp, 0, sizeof(fqp));
    fqp.enable = 1;
    fqp.i_qp = 37;
    fqp.p_qp = 38;
    checkf(dev->set_parameter(h, FWM_VENC_PARAM_H264_FIXED_QP, &fqp) == 0,
           "set fixed QP");

    for (i = 0; i < 6; i++) {
        fwm_venc_input_picture_t in = make_input();
        fwm_venc_output_frame_t out;

        g_ve.sim_bits = 4357 * 8 + i * 8;   /* a whole number of bytes */
        g_ve.sim_activity = 1000u + (unsigned int)i;

        checkf(dev->encode(h, &in) == 0, "encode picture %d", i);

        check_eq32(0x04, exp04[i], "encode");
        checkf((rd(0x08) & 0x3fu) == (uint32_t)exp_qp[i], "picture %d: QP", i);
        checkf((rd(0x08) & (1u << 8)) == (i == 0 ? 0u : (1u << 8)),
               "picture %d: dynamic_me_en", i);
        check_eq32(0x10, exp10[i], "encode");
        checkf((rd(0x14) & 7u) == 7u, "picture %d: irq enable", i);
        checkf((rd(0x18)) == 0x00030008u, "picture %d: kick value", i);
        checkf((rd(0x2c) >> 24) == 0x98u, "picture %d: CBR flags", i);
        check_eq32(0x40, 0x74000000u, "encode");
        check_eq32(0x44, 0x0c1e0000u, "encode");
        check_eq32(0x48, 0x00180004u, "encode");
        check_eq32(0x4c, 0x00800068u, "encode");
        checkf((rd(0x70) >> 24) == 0xffu, "picture %d: ROI disable byte", i);

        if (i == 0) {
            check_eq32(0x30, 0x0c080000u, "encode");
            check_eq32(0x34, 0x30241a12u, "encode");
            check_eq32(0x6c, 0x3cc8027bu, "encode");
            check_eq32(0x74, 0xffbbb75fu, "encode");
            check_eq32(0x94, 0x10010405u, "encode");
        } else {
            check_eq32(0x30, 0x0a060400u, "encode");
            check_eq32(0x34, 0x2418100cu, "encode");
            check_eq32(0x6c, 0x3cc80382u, "encode");
            check_eq32(0x74, 0xdf7fb75fu, "encode");
            check_eq32(0x94, 0x10010304u, "encode");
        }
        checkf((rd(0x68) & 0x3ffffu) == (i == 0 ? 0x18a00u : 0x31400u),
               "picture %d: lambda", i);

        /* MV fields (12 section 8.1): the IDR has neither; the first P writes
         * its field (0x64) but does not read (0x60); later P frames do both. */
        if (i == 0)
            checkf(rd(0x60) == 0 && rd(0x64) == 0, "picture %d: no MV fields", i);
        else if (i == 1)
            checkf(rd(0x60) == 0 && rd(0x64) != 0, "picture %d: MV write only", i);
        else
            checkf(rd(0x60) != 0 && rd(0x64) != 0, "picture %d: MV read+write", i);

        /* Bitstream bounds: 8 MB default ring, offsets in bits. */
        checkf(rd(0x80) != 0 && rd(0x84) != 0, "picture %d: bitstream bounds", i);
        check_eq32(0x88, 0u, "encode");
        check_eq32(0x8c, 0x04000000u, "encode");

        /* Reconstruction of picture i is the reference of picture i+1. */
        if (i > 0)
            checkf(rd(0xa0) == prev_rec, "picture %d: ref = previous reconstruction", i);
        prev_rec = rd(0xb0);
        checkf(rd(0xa4) != 0 && rd(0xb0) != 0 && rd(0xb4) != 0,
               "picture %d: ring addresses non-zero", i);
        checkf(rd(0xb8) != 0 && rd(0xbc) != 0, "picture %d: aux addresses", i);

        /* ISP block from the shared H.264 ISP unit: input size and stride. */
        checkf(*(uint32_t *)(g_ve.win + 0xa00) == 0x00a0005au,
               "picture %d: ISP size word", i);
        checkf(*(uint32_t *)(g_ve.win + 0xa14) == 0x50u,
               "picture %d: ISP stride word", i);

        checkf(dev->ready_frame_count(h) == 1, "picture %d: one frame queued", i);
        checkf(dev->get_frame(h, &out) == 0, "picture %d: get_frame", i);
        checkf(out.size0 == (unsigned int)(4357 + i), "picture %d: frame size", i);
        checkf(out.flags == (i == 0 ? FWM_VENC_FRAME_KEYFRAME : 0u),
               "picture %d: keyframe flag", i);
        checkf(out.stats.qp == exp_qp[i], "picture %d: stats QP", i);
        checkf(dev->release_frame(h, &out) == 0, "picture %d: release_frame", i);
        checkf(dev->ready_frame_count(h) == 0, "picture %d: queue drained", i);
    }

    dev->close(h);
}

static void test_parameters_and_failures(void)
{
    fwm_venc_device_t *dev = &video_encoder_h265;
    fwm_venc_base_config_t cfg = make_base_config(1280, 720);
    fwm_venc_h265_config_t hc;
    void *h;

    install_fake_ve();
    install_fake_memops();
    h = dev->open(&cfg, 0x21110u);
    if (!h) { fail("open for parameter test"); return; }
    if (dev->init(h, &cfg) != 0) { fail("init for parameter test"); dev->close(h); return; }

    /* The handler forces idr/intra 40 and gop 20 (10 section 5). */
    memset(&hc, 0, sizeof(hc));
    hc.profile_level.profile = FWM_VENC_H265_PROFILE_MAIN;
    hc.profile_level.level = 123;
    hc.qp_range.qp_min = 10;
    hc.qp_range.qp_max = 51;
    hc.frame_rate = 20;
    hc.bitrate = 2000000;
    hc.idr_period = 20;
    hc.intra_period = 20;
    hc.gop_size = 20;
    hc.qp_init = 26;
    hc.rc_mode = FWM_VENC_H265_RC_CBR;
    checkf(dev->set_parameter(h, FWM_VENC_PARAM_H265_CONFIG, &hc) == 0, "set H265 config");
    checkf(dev->set_parameter(h, FWM_VENC_PARAM_MAX_KEY_INTERVAL, &(int){7}) == 0,
           "set max key interval");
    {
        int v = 0;
        checkf(dev->get_parameter(h, FWM_VENC_PARAM_MAX_KEY_INTERVAL, &v) == 0 && v == 40,
               "max key interval is forced to 40, got %d", v);
    }

    /* Unknown index is not supported; the 0x40a HighPassFilter gap is
     * reproduced (12 section 3.1). */
    checkf(dev->set_parameter(h, 0x40a, &(int){1}) == FWM_VENC_RESULT_NOT_SUPPORT,
           "0x40a HighPassFilter not handled");
    checkf(dev->get_parameter(h, 0x7f00, &(int){0}) == FWM_VENC_RESULT_NOT_SUPPORT,
           "unknown get index not supported");

    /* Interrupt timeout -> a real (negative) error. */
    g_ve.wait_fail = 1;
    checkf(dev->encode(h, &(fwm_venc_input_picture_t){0}) == FWM_VENC_RESULT_ERROR,
           "encode reports an interrupt failure");

    dev->close(h);
}

int main(void)
{
    test_capability_and_lifecycle();
    test_header_blob();
    test_encode_sequence();
    test_parameters_and_failures();

    if (g_fail) {
        fprintf(stderr, "test_enc: FAILED\n");
        return 1;
    }
    printf("test_enc: ok\n");
    return 0;
}
