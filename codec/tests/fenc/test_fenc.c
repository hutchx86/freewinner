/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Black-box vectors for the encoder API framework (package C), cleanroom/
 * middleware/fenc/SPEC.md §9. The seam (tests/fenc/fenc_seam.c) supplies the
 * imported device tables, VE ops, adapter calls and a malloc-backed memory-ops
 * table; the real picture-queue manager (src/base/vb_frames.c) is linked so the
 * queue semantics are exercised, not mocked. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freecodec/venc_base_abi.h"

#include "fenc_seam.h"

static int g_fail;

static void check(int cond, const char *what)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        g_fail = 1;
    }
}

/* Wrappers let the test observe the manager calls the framework makes without
 * reaching into the manager. Linked with -Wl,--wrap=... */
static int g_fbm_destroy_n;

extern void __real_FrameBufferManagerDestroy(vb_frame_manager *fm);
void __wrap_FrameBufferManagerDestroy(vb_frame_manager *fm)
{
    g_fbm_destroy_n++;
    seam_log("fbm_destroy");
    __real_FrameBufferManagerDestroy(fm);
}

extern int __real_ResetFrameBuffer(vb_frame_manager *fm);
int __wrap_ResetFrameBuffer(vb_frame_manager *fm)
{
    seam_log("fbm_reset");
    return __real_ResetFrameBuffer(fm);
}

/* ------------------------------------------------------------- helpers */

static fwm_venc_input_picture_t mk_in(unsigned long id, unsigned char *phyY, unsigned char *phyC)
{
    fwm_venc_input_picture_t b;

    memset(&b, 0, sizeof(b));
    b.id = id;
    b.luma_phys = phyY;
    b.chroma_phys = phyC;
    return b;
}

static fwm_venc_handle_t *mk_created(void)
{
    return VideoEncCreate(FWM_VENC_CODEC_H264);
}

static fwm_venc_handle_t *mk_inited(void)
{
    fwm_venc_handle_t *e = VideoEncCreate(FWM_VENC_CODEC_H264);
    fwm_venc_base_config_t cfg;

    if (!e)
        return NULL;
    memset(&cfg, 0, sizeof(cfg));
    cfg.output_width = 1280;
    cfg.output_height = 720;
    if (VideoEncInit(e, &cfg) != 0) {
        VideoEncDestroy(e);
        return NULL;
    }
    return e;
}

/* ----------------------------------------------- create and device choice */

static void test_create_selection(void)
{
    fwm_venc_handle_t *e;

    /* #1: H.264, IC 0x1708 -> ver2. */
    seam_reset();
    e = VideoEncCreate(FWM_VENC_CODEC_H264);
    check(e != NULL, "#1 create h264");
    check(seam_ver2.open_n == 1, "#1 open on ver2");
    check(seam_ver2.open_ic == 0x1708u, "#1 open saw ic version");
    check(seam_ver1.open_n == 0 && seam_h265.open_n == 0 && seam_jpeg.open_n == 0,
          "#1 no other device opened");
    check(seam_ver2.open_cfg.memops == seam_fake_memops, "#1 open cfg memops");
    check(seam_ver2.open_cfg.engine_ops == &seam_ve_ops, "#1 open cfg veops");
    check(seam_ver2.open_cfg.engine == seam_ve_self, "#1 open cfg ve self");
    check(seam_last_ve_dec == 0 && seam_last_ve_enc == 1 &&
          seam_last_ve_format == (unsigned)FWM_VENC_CODEC_H264, "#1 ve config");
    check(seam_last_ve_afbc == 0 && seam_last_ve_reset == 0, "#1 ve flags");
    check(seam_ve_lock_n == 1 && seam_ve_unlock_n == 1, "#1 version read locked");
    VideoEncDestroy(e);

    /* #2: H.264, IC 0x1707 -> ver1. */
    seam_reset();
    seam_set_ic_version(0x1707u);
    e = VideoEncCreate(FWM_VENC_CODEC_H264);
    check(e != NULL, "#2 create h264 low ic");
    check(seam_ver1.open_n == 1 && seam_ver2.open_n == 0, "#2 open on ver1");
    VideoEncDestroy(e);

    /* #4: explicit ver2 value bypasses the IC test. */
    seam_reset();
    seam_set_ic_version(0x1707u);
    e = VideoEncCreate(FWM_VENC_CODEC_H264_VER2);
    check(e != NULL, "#4 create h264_ver2");
    check(seam_ver2.open_n == 1 && seam_ver1.open_n == 0, "#4 open on ver2");
    VideoEncDestroy(e);
}

static void test_reject(void)
{
    /* #3: only H.264 values are accepted. */
    seam_reset();
    check(VideoEncCreate(FWM_VENC_CODEC_JPEG) == NULL, "#3 jpeg rejected");
    check(VideoEncCreate(FWM_VENC_CODEC_H265) == NULL, "#3 h265 rejected");
    check(VideoEncCreate(FWM_VENC_CODEC_VP8) == NULL, "#3 vp8 rejected");
    check(seam_ve_init_n == 0, "#3 no ve init");
    check(seam_ver2.open_n == 0 && seam_ver1.open_n == 0 &&
          seam_h265.open_n == 0 && seam_jpeg.open_n == 0, "#3 no device open");
}

static void test_create_failures(void)
{
    /* #5: no VE ops table. */
    seam_reset();
    seam_set_ve_ops_null(1);
    check(VideoEncCreate(FWM_VENC_CODEC_H264) == NULL, "#5 ve ops null");
    check(seam_ve_init_n == 0, "#5 no ve init");

    /* #6: VE init fails; never-created instance is not released. */
    seam_reset();
    seam_set_ve_init_null(1);
    check(VideoEncCreate(FWM_VENC_CODEC_H264) == NULL, "#6 ve init null");
    check(seam_ve_release_n == 0, "#6 ve not released");

    /* #7: no memory-ops table; clean unwind releases the VE. */
    seam_reset();
    seam_set_memops_null(1);
    check(VideoEncCreate(FWM_VENC_CODEC_H264) == NULL, "#7 memops null");
    check(seam_ver2.open_n == 0, "#7 no device open");
    check(seam_ve_release_n == 1, "#7 ve released");
    check(seam_mem_release_n == 0, "#7 manager not released");

    /* #8: memory runtime init fails; no manager release, VE released. */
    seam_reset();
    seam_set_init_mem_rc(-1);
    check(VideoEncCreate(FWM_VENC_CODEC_H264) == NULL, "#8 mem init fails");
    check(seam_ver2.open_n == 0, "#8 no device open");
    check(seam_mem_release_n == 0, "#8 manager not released");
    check(seam_ve_release_n == 1, "#8 ve released");

    /* #9: device open fails; manager and VE released, no close. */
    seam_reset();
    seam_ver2.open_ret = NULL;
    check(VideoEncCreate(FWM_VENC_CODEC_H264) == NULL, "#9 device open null");
    check(seam_ver2.close_n == 0, "#9 no device close");
    check(seam_mem_release_n == 1, "#9 manager released");
    check(seam_ve_release_n == 1, "#9 ve released");
}

/* --------------------------------------------------------------- init */

static void test_init(void)
{
    fwm_venc_handle_t *e;
    fwm_venc_base_config_t cfg;
    fwm_venc_input_picture_t in;
    int i;

    /* #10: init overwrites the three pointer fields and creates 4 slots. */
    seam_reset();
    e = mk_created();
    check(e != NULL, "#10 created");
    memset(&cfg, 0, sizeof(cfg));
    cfg.output_width = 1280;
    cfg.output_height = 720;
    cfg.memops = (void *)(uintptr_t)0xdead;
    cfg.engine_ops = (void *)(uintptr_t)0xbeef;
    cfg.engine = (void *)(uintptr_t)0xcafe;
    check(VideoEncInit(e, &cfg) == 0, "#10 init ok");
    check(cfg.memops == seam_fake_memops && cfg.engine_ops == &seam_ve_ops &&
          cfg.engine == seam_ve_self, "#10 caller cfg overwritten");
    check(seam_ver2.init_n == 1, "#10 device init once");
    check(seam_ver2.init_cfg.memops == seam_fake_memops &&
          seam_ver2.init_cfg.engine_ops == &seam_ve_ops &&
          seam_ver2.init_cfg.engine == seam_ve_self, "#10 device got ctx pointers");
    for (i = 0; i < 4; i++) {
        in = mk_in((unsigned long)i, NULL, NULL);
        check(AddOneInputBuffer(e, &in) == 0, "#10 four slots");
    }
    in = mk_in(9, NULL, NULL);
    check(AddOneInputBuffer(e, &in) == -1, "#10 fifth add fails");

    /* #11: a second init is refused and does not re-init the device. */
    check(VideoEncInit(e, &cfg) == 8, "#11 second init -> 8");
    check(seam_ver2.init_n == 1, "#11 device not re-inited");
    VideoEncDestroy(e);

    /* #12: NULL arguments. */
    seam_reset();
    e = mk_created();
    check(VideoEncInit(NULL, &cfg) == 8, "#12 null encoder");
    check(VideoEncInit(e, NULL) == 8, "#12 null config");
    VideoEncDestroy(e);

    /* #13: a failing device init returns its code, leaves the context
     * uninitialised, and destroys the picture manager (no leak). */
    seam_reset();
    g_fbm_destroy_n = 0;
    seam_ver2.init_ret = 4;
    e = mk_created();
    check(VideoEncInit(e, &cfg) == 4, "#13 device init result");
    check(g_fbm_destroy_n == 1, "#13 picture manager destroyed");
    in = mk_in(0, NULL, NULL);
    check(AddOneInputBuffer(e, &in) == -1, "#13 uninitialised add fails");
    VideoEncDestroy(e);
}

/* ---------------------------------------------------- input / encode flow */

static void test_input_flow(void)
{
    fwm_venc_handle_t *e;
    fwm_venc_input_picture_t in, out;
    int i;

    /* #14/#15/#16: calls before init. */
    seam_reset();
    seam_ver2.valid_ret = -1;
    e = mk_created();
    in = mk_in(0, NULL, NULL);
    check(AddOneInputBuffer(e, &in) == -1, "#14 add before init");
    check(VideoEncodeOneFrame(e) == 1, "#15 encode before init");
    check(ValidBitstreamFrameNum(e) == -1, "#16 valid forwarded (-1)");
    seam_ver2.valid_ret = 3;
    check(ValidBitstreamFrameNum(e) == 3, "#16 valid forwarded (3)");
    VideoEncDestroy(e);

    /* #17: the device sees engine-relative addresses; the used queue keeps the
     * caller's originals. */
    seam_reset();
    seam_set_ve_offset(0x100u);
    e = mk_inited();
    if (!e) {
        check(0, "#17 inited");
        return;
    }
    in = mk_in(0, (unsigned char *)(uintptr_t)0x1000u, (unsigned char *)(uintptr_t)0x2000u);
    check(AddOneInputBuffer(e, &in) == 0, "#17 add");
    check(VideoEncodeOneFrame(e) == 0, "#17 encode");
    check(seam_ver2.encode_buf.luma_phys == (unsigned char *)(uintptr_t)0xF00u,
          "#17 Y offset applied");
    check(seam_ver2.encode_buf.chroma_phys == (unsigned char *)(uintptr_t)0x1F00u,
          "#17 C offset applied");
    check(seam_ve_lock_n == 2 && seam_ve_unlock_n == 2, "#17 encode lock-bracketed");
    out = mk_in(0, NULL, NULL);
    check(AlreadyUsedInputBuffer(e, &out) == 0, "#17 reclaim");
    check(out.id == 0 &&
          out.luma_phys == (unsigned char *)(uintptr_t)0x1000u &&
          out.chroma_phys == (unsigned char *)(uintptr_t)0x2000u,
          "#17 used queue holds originals");
    VideoEncDestroy(e);

    /* #18: empty input queue. */
    seam_reset();
    e = mk_inited();
    check(VideoEncodeOneFrame(e) == 1, "#18 empty queue");
    check(seam_ver2.encode_n == 0, "#18 device not called");
    VideoEncDestroy(e);

    /* #19/#21: a failed encode still moves the picture to the used queue. */
    seam_reset();
    seam_ver2.encode_ret = 7;
    e = mk_inited();
    in = mk_in(5, (unsigned char *)(uintptr_t)0x1000u, (unsigned char *)(uintptr_t)0x2000u);
    check(AddOneInputBuffer(e, &in) == 0, "#19 add");
    check(VideoEncodeOneFrame(e) == 7, "#19 encode result passed through");
    out = mk_in(0, NULL, NULL);
    check(AlreadyUsedInputBuffer(e, &out) == 0, "#19 reclaim after failure");
    check(out.id == 5 && out.luma_phys == (unsigned char *)(uintptr_t)0x1000u,
          "#21 reclaimed entry equals original");
    check(AlreadyUsedInputBuffer(e, &out) == -1, "#21 second reclaim fails");
    VideoEncDestroy(e);

    /* #20: four slots, no reclaim -> fifth add fails. */
    seam_reset();
    e = mk_inited();
    for (i = 0; i < 4; i++) {
        in = mk_in((unsigned long)i, NULL, NULL);
        check(AddOneInputBuffer(e, &in) == 0, "#20 add");
    }
    in = mk_in(4, NULL, NULL);
    check(AddOneInputBuffer(e, &in) == -1, "#20 slots exhausted");
    VideoEncDestroy(e);
}

/* ------------------------------------------------------- bitstream flow */

static void test_bitstream(void)
{
    fwm_venc_handle_t *e;
    fwm_venc_output_frame_t ob;

    /* #22/#24: a good frame is handed out and freed with the same identity. */
    seam_reset();
    e = mk_inited();
    memset(&ob, 0, sizeof(ob));
    check(GetOneBitstreamFrame(e, &ob) == 0, "#22 get frame");
    check(ob.id == 0x1234 && ob.size0 == 111 && ob.size1 == 222,
          "#22 device wrote the descriptor");
    check(FreeOneBitStreamFrame(e, &ob) == 0, "#24 free frame");
    check(seam_ver2.free_buf.id == ob.id && seam_ver2.free_buf.size0 == ob.size0,
          "#24 free saw same nID/size");

    /* #25: the device refuses the release. */
    seam_ver2.freeone_ret = -1;
    check(FreeOneBitStreamFrame(e, &ob) == -1, "#25 free fails -> -1");

    /* #23: the device reports nothing outstanding. */
    seam_ver2.getone_ret = -1;
    check(GetOneBitstreamFrame(e, &ob) == 5, "#23 empty -> 5");
    VideoEncDestroy(e);
}

/* --------------------------------------------------------------- reset */

static void test_reset(void)
{
    fwm_venc_handle_t *e;
    fwm_venc_input_picture_t in;

    /* #26: picture manager reset first, then the device bitstream reset. */
    seam_reset();
    e = mk_inited();
    in = mk_in(1, NULL, NULL);
    check(AddOneInputBuffer(e, &in) == 0, "#26 queue one");
    seam_events_reset();
    check(VideoEncoderReset(e) == 0, "#26 reset ok");
    check(seam_ver2.reset_n == 1, "#26 device reset called");
    check(seam_event_index("fbm_reset") >= 0 &&
          seam_event_index("fbm_reset") < seam_event_index("reset"),
          "#26 manager reset precedes device reset");
    check(VideoEncodeOneFrame(e) == 1, "#26 input queue cleared");
    VideoEncDestroy(e);

    /* #27: the device reset fails. */
    seam_reset();
    seam_ver2.reset_ret = -1;
    e = mk_inited();
    check(VideoEncoderReset(e) == -1, "#27 device reset fails -> -1");
    VideoEncDestroy(e);

    /* #28: reset before init. */
    seam_reset();
    e = mk_created();
    check(VideoEncoderReset(e) == -1, "#28 reset before init");
    check(seam_ver2.reset_n == 0, "#28 device reset not called");
    VideoEncDestroy(e);
}

/* --------------------------------------------------------- parameters */

static void test_params(void)
{
    fwm_venc_handle_t *e;
    int h264 = 0;
    unsigned char hdr[8] = { 0 };
    unsigned int range[2] = { 1000u, 200u };

    seam_reset();
    seam_ver2.setparam_ret = 3;
    seam_ver2.getparam_ret = 7;
    e = mk_inited();
    if (!e) {
        check(0, "params inited");
        return;
    }

    /* #29: index and pointer travel verbatim, result passed through. */
    check(VideoEncSetParameter(e, FWM_VENC_PARAM_H264_CONFIG, &h264) == 3, "#29 set result");
    check(seam_ver2.setparam_index == 0x100 && seam_ver2.setparam_ptr == &h264,
          "#29 set forwarded");
    check(VideoEncGetParameter(e, FWM_VENC_PARAM_H264_SPS_PPS, hdr) == 7, "#29 get result");
    check(seam_ver2.getparam_index == 0x101 && seam_ver2.getparam_ptr == hdr,
          "#29 get forwarded");

    /* #30: the framework copies nothing (pointer identity preserved). */
    seam_ver2.setparam_ret = 0;
    check(VideoEncSetParameter(e, FWM_VENC_PARAM_BITRATE_RANGE, range) == 0, "#30 set ok");
    check(seam_ver2.setparam_ptr == range, "#30 pointer identity");

    VideoEncDestroy(e);
}

/* ---------------------------------------------------------- destroy */

static void test_destroy(void)
{
    fwm_venc_handle_t *e;
    fwm_venc_input_picture_t in;

    /* #31: created but never initialised: no device uninit. */
    seam_reset();
    e = mk_created();
    seam_events_reset();
    VideoEncDestroy(e);
    check(seam_ver2.uninit_n == 0, "#31 uninit not called");
    check(seam_event_index("close") >= 0 &&
          seam_event_index("close") < seam_event_index("mem_release") &&
          seam_event_index("mem_release") < seam_event_index("ve_release"),
          "#31 close -> manager -> ve order");

    /* #32: initialised: uninit first, then close, manager, ve. */
    seam_reset();
    e = mk_inited();
    seam_events_reset();
    VideoEncDestroy(e);
    check(seam_ver2.uninit_n == 1, "#32 uninit called");
    check(seam_event_index("uninit") >= 0 &&
          seam_event_index("uninit") < seam_event_index("close") &&
          seam_event_index("close") < seam_event_index("mem_release") &&
          seam_event_index("mem_release") < seam_event_index("ve_release"),
          "#32 uninit -> close -> manager -> ve order");

    /* #33: NULL is a no-op. */
    VideoEncDestroy(NULL);

    /* #34: raw buffer calls with a NULL handle. */
    seam_reset();
    e = mk_inited();
    in = mk_in(0, NULL, NULL);
    check(AlreadyUsedInputBuffer(NULL, &in) == -1 &&
          AddOneInputBuffer(NULL, &in) == -1 &&
          VideoEncodeOneFrame(NULL) == -1, "#34 null guards");
    VideoEncDestroy(e);
}

/* ------------------------------------------------ companion entry points */

static void test_companions(void)
{
    fwm_venc_handle_t *e;
    fwm_venc_input_pool_t p;
    fwm_venc_input_picture_t b;
    struct user_iommu_param io;

    seam_reset();
    e = mk_inited();
    if (!e) {
        check(0, "companions inited");
        return;
    }

    check(VideoEncoderGetUnencodedBufferNum(NULL) == (unsigned int)-1,
          "pending null -> (unsigned)-1");
    check(VideoEncoderGetUnencodedBufferNum(e) == 0, "pending 0");
    b = mk_in(1, NULL, NULL);
    check(AddOneInputBuffer(e, &b) == 0, "pending add");
    check(VideoEncoderGetUnencodedBufferNum(e) == 1, "pending 1");

    p.count = 2;
    p.luma_size = 64;
    p.chroma_size = 32;
    check(AllocInputBuffer(e, &p) == 0, "pool alloc");
    check(AllocInputBuffer(NULL, &p) == 8, "pool alloc null encoder");
    check(AllocInputBuffer(e, NULL) == 8, "pool alloc null param");
    b = mk_in(0, NULL, NULL);
    check(GetOneAllocInputBuffer(e, &b) == 0, "pool get");
    check(b.luma_virt != NULL, "pool picture has memory");
    check(FlushCacheAllocInputBuffer(e, &b) == 0, "pool flush");
    check(ReturnOneAllocInputBuffer(e, &b) == 0, "pool return");
    check(ReleaseAllocInputBuffer(e) == 0, "pool release no-op");
    check(ReleaseAllocInputBuffer(NULL) == -1, "pool release null");

    io.fd = 1;
    io.engine_addr = 0;
    VideoEncoderGetVeIommuAddr(e, &io);
    VideoEncoderFreeVeIommuAddr(e, &io);
    check(seam_iommu_n == 2, "iommu slots called");
    check(VideoEncoderSetFreq(e, 500) == 0 && seam_speed_last == 500, "set freq");
    VideoEncoderSetDdrMode(e, 1);
    check(seam_ddr_n == 1, "set ddr mode");

    VideoEncDestroy(e);

    /* Pool calls on a context that was never initialised. */
    seam_reset();
    e = mk_created();
    check(AllocInputBuffer(e, &p) == 7, "pool alloc without manager -> 7");
    check(GetOneAllocInputBuffer(e, &b) == 8, "pool get without manager -> 8");
    VideoEncDestroy(e);
}

int main(void)
{
    test_create_selection();
    test_reject();
    test_create_failures();
    test_init();
    test_input_flow();
    test_bitstream();
    test_reset();
    test_params();
    test_destroy();
    test_companions();

    printf("fenc: selection, create unwind, init, input/bitstream queues, reset, "
           "params, destroy %s\n", g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
