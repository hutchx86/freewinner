/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.265 encoder device (spec h265/12-encoder-device.md): the video_encoder_h265
 * fwm_venc_device_t table, lifecycle, buffers and the per-picture engine flow of
 * 11 section 1. Register access goes only through fc_h265_reg_*(). The VE driver
 * and the encoder-internal ISP are the shared H.264 units, unchanged. */

#include "freecodec_h265_enc_priv.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FC_H265_LOG(...) fprintf(stderr, "freecodec_h265: " __VA_ARGS__)

/* ---------------------------------------------------------------- helpers */

unsigned int fc_h265_align_up(unsigned int x, unsigned int n)
{
    return ((x + n - 1u) / n) * n;
}

void fc_h265_set_trace(void *handle,
                       void (*trace)(void *opaque, unsigned int block,
                                     unsigned int off, uint32_t val),
                       void *opaque)
{
    fc_h265_instance *e = (fc_h265_instance *)handle;

    e->trace = trace;
    e->trace_opaque = opaque;
}

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void enc_write(fc_h265_instance *e, unsigned int off, uint32_t val)
{
    fc_h265_reg_write(e->enc_base, off / 4u, val);
    if (e->trace)
        e->trace(e->trace_opaque, 0, off, val);
}

static uint32_t enc_read(fc_h265_instance *e, unsigned int off)
{
    return fc_h265_reg_read(e->enc_base, off / 4u);
}

/* Chroma lambda (spec 11 section 3.12): lambda_c = lambda / 2^((QP - qpc)/3).
 * The spec records the formula but not the chroma table, and the captured
 * values do not admit one static table (11 section 7 item 3); the H.265
 * Table 8-10 mapping is used as the documented choice. */
static unsigned int lambda_chroma(unsigned int lambda, int qp)
{
    static const unsigned char qpc[52] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 29, 30,
        31, 32, 33, 33, 34, 34, 35, 35, 36, 36, 37, 37, 38, 38, 39, 39,
        40, 40, 41, 41
    };
    double k, v;

    if (qp < 0 || qp > 51)
        return lambda & 0x3ffffu;
    k = (double)(qp - (int)qpc[qp]) / 3.0;
    v = (double)lambda / pow(2.0, k);
    return (unsigned int)(v + 0.5) & 0x3ffffu;
}

/* --------------------------------------------------------------- buffers */

static int alloc_one(fc_h265_instance *e, fc_h265_buf *b, unsigned int size)
{
    if (size == 0u) {
        b->vir = NULL; b->phy = NULL; b->size = 0u;
        return 0;
    }
    b->vir = e->memops->palloc((int)size, e->ve_ops, e->ve_self);
    if (b->vir == NULL)
        return -1;
    b->size = size;
    b->phy = e->memops->ve_get_phyaddr(b->vir);
    e->memops->flush_cache(b->vir, (int)size);
    return 0;
}

static void free_one(fc_h265_instance *e, fc_h265_buf *b)
{
    if (b->vir != NULL)
        e->memops->pfree(b->vir, e->ve_ops, e->ve_self);
    memset(b, 0, sizeof(*b));
}

static void free_buffers(fc_h265_instance *e)
{
    int i;

    if (e->bufs.bs != NULL) {
        BitStreamDestroy(e->bufs.bs);
        e->bufs.bs = NULL;
    }
    for (i = 0; i < 2; i++) {
        free_one(e, &e->bufs.full[i]);
        free_one(e, &e->bufs.aux[i]);
        free_one(e, &e->bufs.tmvp[i]);
    }
    free_one(e, &e->bufs.mb_info);
    free_one(e, &e->bufs.dblk);
    free_one(e, &e->bufs.ctu_info);
    free_one(e, &e->bufs.mbrc);
    free_one(e, &e->bufs.ovl_hdr);
    free_one(e, &e->bufs.ovl_data);
    free_one(e, &e->bufs.ovl_inv);
    free_one(e, &e->bufs.filt3d[0]);
    free_one(e, &e->bufs.filt3d[1]);
}

/* Encoder 3-D (temporal noise) filter (3D-filter spec; 11 0xa8/0xac). Same
 * rules as the H.264 device. */
#define FC_H265_3D_FILL_MIN 0xccu

static unsigned int h265_3d_enabled(unsigned int level, unsigned int picture_index)
{
    return (level != 0u && picture_index != 0u) ? 1u : 0u;
}

static unsigned int h265_3d_threshold(unsigned int level, int slice_qp, unsigned int prev_t)
{
    if (slice_qp >= 52)
        return prev_t;
    if (slice_qp >= 20 && slice_qp <= 30)
        return 2u * level;
    return level;
}

static unsigned int h265_3d_fill(unsigned int level)
{
    unsigned int f = ((16u - level) * 0x11u) & 0xffu;

    return f > FC_H265_3D_FILL_MIN ? f : FC_H265_3D_FILL_MIN;
}

/* 11 section 7: H32 * (2-ctu-aligned width) * 0x30 + 0x400 per slot, the same
 * 0x30-per-32x32-block shape as the H.264 plane (12 bytes per 16x16 macroblock). */
static int alloc_filt3d(fc_h265_instance *e)
{
    unsigned int size = (fc_h265_align_up(e->geom.dst_w, 64u) / 32u)
                      * (fc_h265_align_up(e->geom.dst_h, 32u) / 32u)
                      * 0x30u + 0x400u;
    int i;

    size = (size + 255u) & ~255u;
    for (i = 0; i < 2; i++) {
        if (e->bufs.filt3d[i].vir != NULL)
            continue;
        if (alloc_one(e, &e->bufs.filt3d[i], size) != 0)
            return -1;
        memset(e->bufs.filt3d[i].vir, 0xff, size);
        e->memops->flush_cache(e->bufs.filt3d[i].vir, (int)size);
    }
    return 0;
}

static int alloc_buffers(fc_h265_instance *e)
{
    const fc_h265_geom *g = &e->geom;
    /* Frame-ring slot (spec 11 section 6): luma = W16*(H16+8); the chroma plane
     * starts at that luma offset and is W16*H64/2 (height rounded up to 64),
     * plus a 0xc00 tail on pictures below 577 lines. */
    unsigned int luma = g->w16 * (g->h16 + 8u);
    unsigned int h64 = fc_h265_align_up(g->dst_h, 64u);
    unsigned int chroma = g->w16 * h64 / 2u;
    unsigned int tail = (g->dst_h < 577u) ? 0xc00u : 0u;
    unsigned int full_size = luma + chroma + tail;
    unsigned int w32 = fc_h265_align_up(g->dst_w, 32u);
    unsigned int wc = w32 / 32u;
    unsigned int h32 = fc_h265_align_up(g->dst_h, 32u) / 32u;
    int i;

    e->bufs.luma_size = luma;
    e->bufs.chroma_size = chroma;

    for (i = 0; i < 2; i++) {
        if (alloc_one(e, &e->bufs.full[i], full_size) != 0)
            goto fail;
        if (alloc_one(e, &e->bufs.aux[i], fc_aux_plane_bytes(g->dst_w, g->dst_h)) != 0)
            goto fail;
        /* MV field: align16(ceil(w/32)) x ceil(h/32) x 16 (12 section 8.1). */
        if (alloc_one(e, &e->bufs.tmvp[i],
                      ((wc + 15u) & ~15u) * h32 * 16u) != 0)
            goto fail;
    }

    if (alloc_one(e, &e->bufs.mb_info, g->w16 * 8u) != 0)
        goto fail;
    if (alloc_one(e, &e->bufs.dblk, w32 * 8u) != 0)
        goto fail;
    /* CTU-info output: one 0x18-byte record per CTU (spec 11 section 6). */
    if (alloc_one(e, &e->bufs.ctu_info, wc * h32 * 0x18u) != 0)
        goto fail;

    {
        unsigned int mbrc_size = fc_mbrc_buffer_bytes(g->h_mb);

        if (alloc_one(e, &e->bufs.mbrc, mbrc_size) != 0)
            goto fail;
        memset(e->bufs.mbrc.vir, 0, mbrc_size);
        e->memops->flush_cache(e->bufs.mbrc.vir, (int)mbrc_size);
    }

    /* 3-D-filter scratch planes: allocated regardless of the level (3D-filter
     * spec 5), so a live level change needs no allocation. */
    if (alloc_filt3d(e) != 0)
        goto fail;

    e->bufs.bs = BitStreamCreate(e->params.vbv_no_cache ? 1 : 0,
                                 (int)e->params.vbv_size, e->memops,
                                 e->ve_ops, e->ve_self);
    if (e->bufs.bs == NULL)
        goto fail;
    return 0;

fail:
    free_buffers(e);
    return -1;
}

/* --------------------------------------------------------------- geometry */

static void compute_geometry(fc_h265_instance *e, fwm_venc_base_config_t *cfg)
{
    fc_h265_geom *g = &e->geom;

    g->in_w = cfg->input_width;
    g->in_h = cfg->input_height;
    g->dst_w = cfg->output_width ? cfg->output_width : cfg->input_width;
    g->dst_h = cfg->output_height ? cfg->output_height : cfg->input_height;
    g->in_stride = cfg->input_stride ? cfg->input_stride : g->in_w;
    g->w16 = fc_h265_align_up(g->dst_w, 16u);
    g->h16 = fc_h265_align_up(g->dst_h, 16u);
    g->w_mb = g->w16 / 16u;
    g->h_mb = g->h16 / 16u;
}

/* ------------------------------------------------------------- VPS/SPS/PPS */

static void build_header(fc_h265_instance *e)
{
    freecodec_h265_vps_cfg vps;
    freecodec_h265_sps_cfg sps;
    freecodec_h265_pps_cfg pps;
    int n;
    unsigned char *out = e->bufs.header;
    unsigned int cap = (unsigned int)sizeof(e->bufs.header);
    unsigned int off = 0;

    e->bufs.header_len = 0;

    memset(&vps, 0, sizeof(vps));
    vps.ptl.profile_idc = (unsigned int)e->params.profile_level.profile;
    vps.ptl.level_idc = (unsigned int)e->params.profile_level.level;
    vps.ptl.compatibility = 0x60000000u;
    vps.max_num_ref_pics = 1u;
    n = freecodec_h265_build_vps(&vps, out + off, (int)(cap - off));
    if (n > 0) { off += (unsigned int)n; e->bufs.header_len += (unsigned int)n; }

    memset(&sps, 0, sizeof(sps));
    sps.ptl = vps.ptl;
    sps.pic_width_in_luma_samples = e->geom.dst_w;
    sps.pic_height_in_luma_samples = e->geom.dst_h;
    /* Displayed window: crop to the shown size (display_size or the whole
     * picture), centred or at display_offset, in 2-pixel units (4:2:0). Same
     * rules as the H.264 device's SPS crop. */
    {
        unsigned int show_w = e->geom.dst_w, show_h = e->geom.dst_h;
        unsigned int left, top;

        if (e->params.display_size.width > 0 && e->params.display_size.height > 0 &&
            (unsigned int)e->params.display_size.width <= e->geom.dst_w &&
            (unsigned int)e->params.display_size.height <= e->geom.dst_h) {
            show_w = (unsigned int)e->params.display_size.width;
            show_h = (unsigned int)e->params.display_size.height;
        }
        left = ((e->geom.dst_w - show_w) / 2u) & ~1u;
        top = ((e->geom.dst_h - show_h) / 2u) & ~1u;
        if (e->params.display_offset.left >= 0 &&
            ((unsigned int)e->params.display_offset.left & ~1u) + show_w <= e->geom.dst_w)
            left = (unsigned int)e->params.display_offset.left & ~1u;
        if (e->params.display_offset.top >= 0 &&
            ((unsigned int)e->params.display_offset.top & ~1u) + show_h <= e->geom.dst_h)
            top = (unsigned int)e->params.display_offset.top & ~1u;
        sps.conf_win_left_offset = left / 2u;
        sps.conf_win_top_offset = top / 2u;
        sps.conf_win_right_offset = (e->geom.dst_w - show_w - left) / 2u;
        sps.conf_win_bottom_offset = (e->geom.dst_h - show_h - top) / 2u;
    }
    sps.max_num_ref_pics = 1u;
    sps.num_short_term_ref_pic_sets = e->params.gop_size + 1u;
    sps.log2_max_pic_order_cnt_lsb_minus4 = 4u;
    sps.sao_enabled = e->params.sao.enabled ? 1u : 0u;
    sps.temporal_mvp_enabled = 1u;
    /* VUI timing info only when the caller set both fields (the default record
     * has num_units_in_tick == 0, i.e. the vendor's no-VUI stream). */
    if (e->params.timing.num_units_in_tick != 0u && e->params.timing.time_scale != 0u) {
        sps.vui_num_units_in_tick = e->params.timing.num_units_in_tick;
        sps.vui_time_scale = e->params.timing.time_scale;
    }
    n = freecodec_h265_build_sps(&sps, out + off, (int)(cap - off));
    if (n > 0) { off += (unsigned int)n; e->bufs.header_len += (unsigned int)n; }

    memset(&pps, 0, sizeof(pps));
    pps.cabac_init_present = 1u;
    pps.transform_skip_enabled = e->params.transform.transform_skip_en ? 1u : 0u;
    pps.cu_qp_delta_enabled = 1u;
    pps.diff_cu_qp_delta_depth = 1u;
    pps.deblocking_filter_control_present = 1u;
    pps.loop_filter_across_slices_enabled = 1u;
    n = freecodec_h265_build_pps(&pps, out + off, (int)(cap - off));
    if (n > 0) { off += (unsigned int)n; e->bufs.header_len += (unsigned int)n; }
}

/* ------------------------------------------------------------- ISP program */

/* Burned-in OSD overlay (shared encoder-internal ISP): the same buffer/block
 * layout as the H.264 device (h264-overlay). blk_num 0 disables. */
#define FC_H265_OVL_REVERSE_LUMA   (1u << 29)
#define FC_H265_OVL_INVERT_MODE    2
#define FC_H265_OVL_INVERT_THRESH  96

static void ovl_invert_setup(fc_h265_instance *e,
                             const freecodec_h264_overlay_block *blocks, unsigned int n)
{
    const char *off = getenv("FREECODEC_OVL_INVERT");
    int enable = !(off && off[0] == '0');
    unsigned int i;

    if (e->ic_version <= 0x2110fu)
        return;
    if (e->bufs.ovl_inv.vir == NULL) {
        unsigned int sz = ((e->geom.in_w + 15u) / 16u) * 32u;

        sz = (sz + 255u) & ~255u;
        if (alloc_one(e, &e->bufs.ovl_inv, sz) != 0)
            return;
        memset(e->bufs.ovl_inv.vir, 0, e->bufs.ovl_inv.size);
        e->memops->flush_cache(e->bufs.ovl_inv.vir, (int)e->bufs.ovl_inv.size);
    }
    e->ovl.phy_address_overlay_invert = (unsigned int)((uintptr_t)e->bufs.ovl_inv.phy >> 8);
    e->ovl.invert_mode = enable ? FC_H265_OVL_INVERT_MODE : 0;
    e->ovl.invert_threshold = enable ? FC_H265_OVL_INVERT_THRESH : 0;
    if (!enable)
        return;
    for (i = 0; i < n; i++) {
        unsigned char *h = (unsigned char *)e->bufs.ovl_hdr.vir +
                           (uintptr_t)i * FREECODEC_H264_OVERLAY_HDR_STRIDE;
        if (blocks[i].reverse_luma)
            h[11] |= (unsigned char)(FC_H265_OVL_REVERSE_LUMA >> 24); /* +8, little-endian */
    }
}

static int set_overlay(fc_h265_instance *e, const fwm_venc_overlay_t *oi)
{
    freecodec_h264_overlay_block blocks[FREECODEC_H264_OVERLAY_MAX_BLOCKS];
    unsigned int n = oi->region_count, i, need;
    int rc = FWM_VENC_RESULT_OK;

    if (n > FREECODEC_H264_OVERLAY_MAX_BLOCKS)
        return FWM_VENC_RESULT_ILLEGAL_PARAM;
    for (i = 0; i < n; i++) {
        const fwm_venc_overlay_region_t *s = &oi->regions[i];

        if (s->overlay_type != FWM_VENC_OVERLAY_NORMAL &&
            s->overlay_type != FWM_VENC_OVERLAY_LUMA_REVERSE)
            return FWM_VENC_RESULT_NOT_SUPPORT;
        blocks[i].start_mb_x = s->start_mb_x;
        blocks[i].end_mb_x = s->end_mb_x;
        blocks[i].start_mb_y = s->start_mb_y;
        blocks[i].end_mb_y = s->end_mb_y;
        blocks[i].bitmap_vir = s->bitmap;
        blocks[i].bitmap_size = s->bitmap_size;
        blocks[i].reverse_luma = (s->overlay_type == FWM_VENC_OVERLAY_LUMA_REVERSE) ? 1u : 0u;
        blocks[i].reverse_unit_w_minus1 =
            (unsigned char)(s->reverse_unit_mb_w_minus1 > 3u ? 3u : s->reverse_unit_mb_w_minus1);
        blocks[i].reverse_unit_h_minus1 =
            (unsigned char)(s->reverse_unit_mb_h_minus1 > 3u ? 3u : s->reverse_unit_mb_h_minus1);
    }

    if (e->ve_ops->lock)
        e->ve_ops->lock(e->ve_self);

    if (n == 0u) {
        (void)freecodec_h264_isp_update_overlay(&e->ovl, NULL, 0u, (int)oi->argb_type,
                                                NULL, NULL, 0u);
        goto out;
    }

    need = freecodec_h264_overlay_data_size(blocks, n);
    if (e->bufs.ovl_hdr.vir == NULL &&
        alloc_one(e, &e->bufs.ovl_hdr, FREECODEC_H264_OVERLAY_HEADER_SIZE) != 0) {
        rc = FWM_VENC_RESULT_NO_MEMORY;
        goto out;
    }
    if (e->bufs.ovl_data.size < need) {
        e->ovl.b_overlay = 0;
        free_one(e, &e->bufs.ovl_data);
        if (alloc_one(e, &e->bufs.ovl_data, need) != 0) {
            rc = FWM_VENC_RESULT_NO_MEMORY;
            goto out;
        }
    }
    if (freecodec_h264_isp_update_overlay(&e->ovl, blocks, n, (int)oi->argb_type,
                                          (unsigned char *)e->bufs.ovl_hdr.vir,
                                          (unsigned char *)e->bufs.ovl_data.vir,
                                          e->bufs.ovl_data.size) != 0) {
        e->ovl.b_overlay = 0;
        rc = FWM_VENC_RESULT_ILLEGAL_PARAM;
        goto out;
    }
    ovl_invert_setup(e, blocks, n);
    e->memops->flush_cache(e->bufs.ovl_hdr.vir, (int)e->bufs.ovl_hdr.size);
    e->memops->flush_cache(e->bufs.ovl_data.vir, (int)e->bufs.ovl_data.size);
    e->ovl.phy_address_overlay_header = (unsigned int)((uintptr_t)e->bufs.ovl_hdr.phy >> 8);
    e->ovl.phy_address_overlay_data = (unsigned int)((uintptr_t)e->bufs.ovl_data.phy >> 8);

out:
    if (e->ve_ops->unlock)
        e->ve_ops->unlock(e->ve_self);
    return rc;
}

static void program_isp(fc_h265_instance *e, fwm_venc_input_picture_t *in)
{
    freecodec_h264_isp_info info;

    if (e->isp == NULL)
        return;
    memset(&info, 0, sizeof(info));
    info.input_width_mb = (int)(fc_h265_align_up(e->geom.in_w, 8u) / 8u);
    info.input_height_mb = (int)(fc_h265_align_up(e->geom.in_h, 8u) / 8u);
    info.output_width_mb = (int)(fc_h265_align_up(e->geom.dst_w, 8u) / 8u);
    info.output_height_mb = (int)(fc_h265_align_up(e->geom.dst_h, 8u) / 8u);
    info.input_stride_mb = (int)(e->geom.in_stride / 16u);
    info.color_format = (int)e->params.color_fmt;
    if (in != NULL && in->csi_colour_format_en)
        info.color_format = in->csi_colour_format;
    if (in != NULL) {
        uint64_t addr_y = (uint64_t)(uintptr_t)in->luma_phys;
        uint64_t addr_c0 = (uint64_t)(uintptr_t)in->chroma_phys;
        uint64_t addr_c1 = addr_c0 +
            (((uint64_t)e->geom.in_h * (uint64_t)e->geom.in_stride) >> 2);

        info.phy_address_y = (unsigned int)addr_y;
        info.phy_address_c0 = (unsigned int)addr_c0;
        info.phy_address_c1 = (unsigned int)addr_c1;
        info.phy_address_y_high8 = (unsigned char)(addr_y >> 32);
        info.phy_address_c0_high8 = (unsigned char)(addr_c0 >> 32);
        info.phy_address_c1_high8 = (unsigned char)(addr_c1 >> 32);
    }
    info.b_height_is8_align = ((e->geom.in_h & 7u) == 0u) ? 1u : 0u;
    info.b_lbc_lossy_com_en_2x = (unsigned char)e->params.lbc_lossy_2x;
    info.b_lbc_lossy_com_en_2_5x = (unsigned char)e->params.lbc_lossy_2_5x;
    info.ic_version = (int)e->ic_version;
    info.n_encode_format = 1;
    if (e->ovl.b_overlay) {
        info.b_overlay = 1;
        info.n_overlay_num = e->ovl.n_overlay_num;
        info.e_overlay_argb_type = e->ovl.e_overlay_argb_type;
        info.n_blk_len = e->ovl.n_blk_len;
        info.phy_address_overlay_header = e->ovl.phy_address_overlay_header;
        info.phy_address_overlay_data = e->ovl.phy_address_overlay_data;
        info.phy_address_overlay_invert = e->ovl.phy_address_overlay_invert;
        info.invert_mode = e->ovl.invert_mode;
        info.invert_threshold = e->ovl.invert_threshold;
    }
    freecodec_h264_isp_set_register(e->isp, &info);
}

/* ----------------------------------------------------------- register script */

static const unsigned int ADDR_OFFSETS[] = {
    0x3cu, 0x60u, 0x64u, 0x80u, 0x84u, 0x88u, 0x8cu, 0x9cu,
    0xa0u, 0xa4u, 0xb0u, 0xb4u, 0xb8u, 0xbcu, 0xc0u, 0xc4u, 0xc8u, 0xfcu
};

static const unsigned int PARAM_OFFSETS[] = {
    0x04u, 0x08u, 0x10u, 0x28u, 0x2cu, 0x30u, 0x34u, 0x38u, 0x40u, 0x44u,
    0x48u, 0x4cu, 0x68u, 0x6cu, 0x70u, 0x74u, 0x94u, 0x98u, 0xccu,
    0xd0u, 0xd4u, 0xd8u, 0xdcu, 0xe8u, 0xecu, 0xf0u, 0xf4u, 0xf8u
};

static void write_offsets(fc_h265_instance *e, const uint32_t *regs,
                          const unsigned int *offs, unsigned int n)
{
    unsigned int i;

    for (i = 0; i < n; i++)
        enc_write(e, offs[i], regs[offs[i] / 4u]);
}

/* Slice-header feed (spec 11 section 5). */
static void feed_slice_header(fc_h265_instance *e, const unsigned char *buf,
                              int bytes, uint32_t pic_control)
{
    int i;

    enc_write(e, 0x04u, pic_control | 0x80000000u);   /* eptb_disable = 1 */
    enc_write(e, 0x18u, enc_read(e, 0x18u) | 0x00030000u);   /* open, mode 3 */
    for (i = 0; i < bytes; i++) {
        unsigned int tries;
        int ready = 0;

        for (tries = 0; tries < 10000u; tries++) {
            if (enc_read(e, 0x1cu) & (1u << 9)) {
                ready = 1;
                break;
            }
        }
        if (!ready)
            return;
        enc_write(e, 0x20u, buf[i]);
        enc_write(e, 0x18u, enc_read(e, 0x18u) | 0x00030801u);   /* mode 3, emit 8 bits */
    }
    enc_write(e, 0x04u, pic_control);                 /* eptb_disable = 0 */
}

/* ================================================================ encode */

static int enc_encode(void *h, fwm_venc_input_picture_t *in)
{
    fc_h265_instance *e = (fc_h265_instance *)h;
    unsigned int n = e->pic_count;
    int is_i, qp;
    uint32_t regs[0x200];
    freecodec_h265_frame_cfg fcfg;
    freecodec_h265_slice_cfg scfg;
    unsigned char slice[256];
    int slice_bits = 0, slice_bytes;
    uint32_t baseline, status;
    long long length;
    unsigned int rec_slot, ref_slot, lambda;
    vb_stream_info sinfo;

    if (!e->initialised || e->enc_base == NULL)
        return FWM_VENC_RESULT_ERROR;

    is_i = e->first_picture || e->params.force_key ||
           (e->params.idr_period > 0u && (n % e->params.idr_period) == 0u);
    e->params.force_key = 0;

    freecodec_h265_rc_start_picture(&e->rc, (long)n, is_i);
    qp = e->rc.cur_qp;
    if (e->params.fixed_qp_enable)
        qp = is_i ? e->params.fixed_i_qp : e->params.fixed_p_qp;
    qp = clampi(qp, e->params.qp_min, e->params.qp_max);

    e->ve_ops->reset(e->ve_self);
    fc_ve_enc_on(e->ve_ops, e->ve_self);
    enc_write(e, 0x18u, enc_read(e, 0x18u) | 0x00030000u);   /* mode 3 before the script (vendor order) */

    rec_slot = n % 2u;
    ref_slot = (n == 0u) ? 0u : ((n - 1u) % 2u);
    lambda = is_i ? FC_H265_LAMBDA_I : FC_H265_LAMBDA_P;

    /* Slice header (spec 13 section 8). */
    memset(&scfg, 0, sizeof(scfg));
    scfg.nal_unit_type = is_i ? 19u : 1u;
    scfg.is_i_picture = is_i ? 1u : 0u;
    scfg.pic_order_cnt_lsb = e->poc_lsb & 0xffu;
    scfg.short_term_ref_pic_set_idx = 0u;
    scfg.temporal_mvp_enabled = 1u;
    scfg.sao_luma_flag = e->params.sao.enabled ? (unsigned int)e->params.sao.slice_sao_luma : 0u;
    scfg.sao_chroma_flag = e->params.sao.enabled ? (unsigned int)e->params.sao.slice_sao_chroma : 0u;
    scfg.cabac_init_flag = 0u;
    scfg.collocated_ref_idx = 0u;
    scfg.five_minus_max_num_merge_cand = 0u;
    scfg.num_ref_idx_active_override_flag = 0u;
    scfg.slice_qp = qp;
    scfg.loop_filter_across_slices_enabled = 1u;
    /* Feed the slice-header RBSP (no start code, no NAL header); eptb_disable
     * is set during the feed, mirroring the H.264 device's header feed. */
    slice_bytes = freecodec_h265_build_slice(&scfg, slice, (int)sizeof(slice), &slice_bits);
    if (slice_bytes <= 0)
        return FWM_VENC_RESULT_ERROR;

    /* Command-block image (spec 11 section 3). */
    memset(&fcfg, 0, sizeof(fcfg));
    fcfg.width_px = e->geom.dst_w;
    fcfg.height_px = e->geom.dst_h;
    fcfg.length_stride = freecodec_h265_length_stride(e->geom.dst_w);
    fcfg.is_i = is_i;
    fcfg.picture_index = n;
    fcfg.qp = qp;
    fcfg.rc_mode = e->params.rc_mode;
    /* 3-D (temporal noise) filter (3D-filter spec; 11 0xa8/0xac). The level is
     * read every picture; a direct strength runs as level 3 with its own T. */
    {
        unsigned int f3d_level = e->params.filter_3d_strength ? 3u : e->params.filter_3d_level;

        if (f3d_level != 0u) {
            if (e->bufs.filt3d[0].vir == NULL || e->bufs.filt3d[1].vir == NULL)
                alloc_filt3d(e);
            e->params.filt3d_t = e->params.filter_3d_strength
                                 ? e->params.filter_3d_strength
                                 : h265_3d_threshold(f3d_level, qp, e->params.filt3d_t);
            if (e->bufs.filt3d[ref_slot].vir != NULL) {
                memset(e->bufs.filt3d[ref_slot].vir, (int)h265_3d_fill(f3d_level),
                       e->bufs.filt3d[ref_slot].size);
                e->memops->flush_cache(e->bufs.filt3d[ref_slot].vir,
                                       (int)e->bufs.filt3d[ref_slot].size);
            }
            fcfg.f3d_rec_phy = (unsigned int)((uintptr_t)e->bufs.filt3d[rec_slot].phy >> 8);
            if (h265_3d_enabled(f3d_level, (unsigned int)n)) {
                fcfg.fore_3d_filter_en = 1u;
                fcfg.fore_3d_filter_block_th = e->params.filt3d_t;
                fcfg.f3d_ref_phy = (unsigned int)((uintptr_t)e->bufs.filt3d[ref_slot].phy >> 8);
            }
        }
    }
    /* Dynamic ME follows the init-time latch, on P pictures only (3D spec 6). */
    fcfg.dynamic_me_en = (!is_i && e->params.dyn_me) ? 1u : 0u;
    fcfg.img_bin_enable = ((e->params.rc_mode == FWM_VENC_H265_RC_VBR ||
                            e->params.rc_mode == FWM_VENC_H265_RC_ABR ||
                            e->params.rc_mode == FWM_VENC_H265_RC_AVBR) && n >= 1u) ? 1 : 0;
    fcfg.allocate_bits = (unsigned int)e->rc.cur_budget;
    fcfg.deblock_tc = (unsigned int)e->params.deblock.tc_offset_div2 & 0xfu;
    fcfg.deblock_beta = (unsigned int)e->params.deblock.beta_offset_div2 & 0xfu;
    fcfg.lambda = lambda;
    fcfg.lambda_sqrt = (unsigned int)(2.0 * sqrt((double)lambda) + 0.5);
    /* Chroma lambda uses the picture-type luma base (I vs P), not always the I
     * constant (spec 10 s3 / 11 s3.12). */
    fcfg.lambda_c = lambda_chroma(lambda, qp);
    fcfg.th_bright = e->params.th_bright;
    fcfg.th_dark = e->params.th_dark;
    fcfg.roi_disable_mask = 0xffu;                 /* ROI off on this device path */
    fcfg.transform_8x8_en = (unsigned int)e->params.transform.transform_8x8_en;
    fcfg.longterm_en = 0u;
    fcfg.frame_num = 0u;

    fcfg.bitstream_base_phy = (unsigned int)((uintptr_t)BitStreamBasePhyAddress(e->bufs.bs) >> 8);
    fcfg.bitstream_end_phy = (unsigned int)((uintptr_t)BitStreamEndPhyAddress(e->bufs.bs) >> 8);
    /* The step-2 reset returns the engine write position to 0 and 0x88 does not
     * move it, so encode and publish at offset 0 (11 section 6). */
    fcfg.bitstream_offset = 0u;
    fcfg.bitstream_size = (unsigned int)BitStreamBufferSize(e->bufs.bs);
    fcfg.ctu_info_phy = (unsigned int)((uintptr_t)e->bufs.ctu_info.phy >> 8);
    /* MV fields (12 section 8.1): the reconstruction slot's field is written
     * from the first P (its absence left the engine DMAing to address 0 and the
     * next P reading an unwritten field — the P-frame corruption); the reference
     * slot's field is read once the read is enabled. Read off on the first P
     * after an IDR; write off on the P before the next IDR. */
    {
        unsigned int intra = e->params.intra_period ? e->params.intra_period : 40u;
        unsigned int poc = e->poc_lsb;
        int mv_write_en = !is_i && (((poc + 1u) % intra) != 0u);
        int mv_read_en  = !is_i && ((poc % intra) != 1u);

        fcfg.tmvp_write_phy = mv_write_en
            ? (unsigned int)((uintptr_t)e->bufs.tmvp[rec_slot].phy >> 8) : 0u;
        fcfg.tmvp_read_phy = mv_read_en
            ? (unsigned int)((uintptr_t)e->bufs.tmvp[ref_slot].phy >> 8) : 0u;
    }
    fcfg.mb_rc_phy = (unsigned int)((uintptr_t)e->bufs.mbrc.phy >> 8);
    fcfg.ref_y_phy = (unsigned int)((uintptr_t)e->bufs.full[ref_slot].phy >> 8);
    fcfg.ref_c_phy = (unsigned int)(((uintptr_t)e->bufs.full[ref_slot].phy
                                     + e->bufs.luma_size) >> 8);
    fcfg.rec_y_phy = (unsigned int)((uintptr_t)e->bufs.full[rec_slot].phy >> 8);
    fcfg.rec_c_phy = (unsigned int)(((uintptr_t)e->bufs.full[rec_slot].phy
                                     + e->bufs.luma_size) >> 8);
    fcfg.aux_ref_phy = (unsigned int)((uintptr_t)e->bufs.aux[ref_slot].phy >> 8);
    fcfg.aux_rec_phy = (unsigned int)((uintptr_t)e->bufs.aux[rec_slot].phy >> 8);
    fcfg.mb_info_phy = (unsigned int)((uintptr_t)e->bufs.mb_info.phy >> 8);
    fcfg.dblk_phy = (unsigned int)((uintptr_t)e->bufs.dblk.phy >> 8);
    fcfg.lt_aux_phy = 0u;
    fcfg.img_bin_phy = 0u;
    fcfg.ctu_mode_phy = 0u;

    freecodec_h265_config_registers(&fcfg, regs);

    /* [R-frame] order (11 section 1): ISP, address writer, parameter writer. */
    program_isp(e, in);
    write_offsets(e, regs, ADDR_OFFSETS, sizeof(ADDR_OFFSETS) / sizeof(ADDR_OFFSETS[0]));
    enc_write(e, 0x14u, enc_read(e, 0x14u) | 7u);
    enc_write(e, 0x1cu, enc_read(e, 0x1cu) | 7u);
    write_offsets(e, regs, PARAM_OFFSETS, sizeof(PARAM_OFFSETS) / sizeof(PARAM_OFFSETS[0]));

    baseline = enc_read(e, 0x90u);
    feed_slice_header(e, slice, slice_bytes, regs[0x04u / 4u]);
    enc_write(e, 0x18u, 0x00030008u);      /* kick, mode 3 */

    if (e->ve_ops->wait_irq(e->ve_self) != 0) {
        fc_ve_enc_off(e->ve_ops, e->ve_self);
        return FWM_VENC_RESULT_ERROR;
    }

    length = ((long long)enc_read(e, 0x90u) - (long long)baseline) / 8;
    if (length <= 0 || length > (long long)BitStreamBufferSize(e->bufs.bs)) {
        fc_ve_enc_off(e->ve_ops, e->ve_self);
        return FWM_VENC_RESULT_ERROR;
    }

    if (!e->params.vbv_no_cache)
        e->memops->flush_cache(BitStreamBaseAddress(e->bufs.bs), (int)length);

    status = enc_read(e, 0x1cu);
    enc_write(e, 0x1cu, status);

    memset(&sinfo, 0, sizeof(sinfo));
    sinfo.nStreamOffset = (int)fcfg.bitstream_offset;
    sinfo.nStreamLength = (int)length;
    sinfo.nPts = (in != NULL) ? in->pts : 0;
    sinfo.nFlags = is_i ? FWM_VENC_FRAME_KEYFRAME : 0;
    sinfo.CurrQp = qp;
    sinfo.avQp = qp;
    sinfo.nGopIndex = (int)(n % e->params.gop_size);
    sinfo.nFrameIndex = (int)n;
    sinfo.nTotalIndex = (int)n;
    BitStreamAddOneBitstream(e->bufs.bs, &sinfo);

    freecodec_h265_rc_finish_picture(&e->rc, length * 8);

    fc_ve_enc_off(e->ve_ops, e->ve_self);

    if (is_i) {
        e->poc_lsb = 1u;      /* the IDR is POC 0; the next P must not repeat it */
        e->frame_num = 0u;
    } else {
        e->poc_lsb = (e->poc_lsb + 1u) & 0xffu;
        e->frame_num = (e->frame_num + 1u) & 0xfu;
    }
    e->first_picture = 0;
    e->pic_count = n + 1u;
    return FWM_VENC_RESULT_OK;
}

/* ============================================================== lifecycle */

static const unsigned int FC_H265_CAPS[4][9] = {
    { 0x00020010u, 1u, 1u, 1u, 1u, 1u, 0u, 0x1000u, 0x1000u },
    { 0x00021010u, 0u, 1u, 1u, 1u, 1u, 0u, 0x1000u, 0x1000u },
    { 0x00021110u, 0u, 1u, 1u, 1u, 1u, 0u, 0x1000u, 0x1000u },
    { 0x00021210u, 0u, 1u, 1u, 1u, 1u, 0u, 0x1000u, 0x1000u },
};

static int capability_match(unsigned int ic_version, unsigned int out[9])
{
    unsigned int i, k;

    for (i = 0; i < 4u; i++) {
        if ((ic_version & 0x000fffffu) == (FC_H265_CAPS[i][0] & 0x000fffffu)) {
            for (k = 0; k < 9u; k++)
                out[k] = FC_H265_CAPS[i][k];
            return 0;
        }
    }
    return -1;
}

static void set_defaults(fc_h265_instance *e)
{
    memset(&e->params, 0, sizeof(e->params));
    e->params.bitrate = 2000000u;
    e->params.framerate = 30000u;                 /* framerate divisor (12 section 2.1) */
    e->params.max_key_interval = 40u;
    e->params.profile_level.profile = FWM_VENC_H265_PROFILE_MAIN;
    e->params.profile_level.level = 123;          /* level 4.1 */
    e->params.qp_min = 10;
    e->params.qp_max = 51;
    e->params.qp_init = 26;
    e->params.min_i_qp = 30;
    e->params.rc_mode = FWM_VENC_H265_RC_CBR;
    e->params.idr_period = 40u;
    e->params.intra_period = 40u;
    e->params.gop_size = 20u;
    e->params.vbv_size = 8u * 1024u * 1024u;
    e->params.color_fmt = FWM_VENC_PIXEL_YUV420SP;
    e->params.th_bright = 0xc8u;
    e->params.th_dark = 0x3cu;
    e->params.transform.transform_skip_en = 1;
    e->params.sao.enabled = 1;
    e->params.sao.slice_sao_luma = 1;
    e->params.sao.slice_sao_chroma = 1;
    e->params.timing.time_scale = 30000u;
    e->params.gop.gop_control_en = 1;
    e->params.gop.gop_mode = FWM_VENC_H265_GOP_NORMAL_P;
    e->params.gop.gop_size = 20;
}

static void *enc_open(fwm_venc_base_config_t *cfg, unsigned int ic_version)
{
    fc_h265_instance *e;
    fc_ve_ops *ve_ops;

    if (cfg == NULL)
        return NULL;
    e = calloc(1, sizeof(*e));
    if (e == NULL)
        return NULL;

    ve_ops = (fc_ve_ops *)cfg->engine_ops;
    e->ve_ops = ve_ops;
    e->ve_self = cfg->engine;
    e->memops = (struct vb_mem_ops *)cfg->memops;
    e->ic_version = ic_version;

    /* open calls getGroupRegAddr but discards it; init binds the blocks
     * (spec 12 section 2.1/2.2). */
    if (ve_ops != NULL && ve_ops->group_base != NULL)
        (void)ve_ops->group_base(e->ve_self, FC_VE_GROUP_TOP);
    if (ve_ops != NULL && ve_ops->dram_type != NULL)
        e->dram_type = (unsigned int)ve_ops->dram_type(e->ve_self);

    if (capability_match(ic_version, e->capability) != 0) {
        FC_H265_LOG("no capability record for IC 0x%x\n", ic_version);
        free(e);
        return NULL;
    }

    e->isp = freecodec_h264_isp_create();
    if (e->isp == NULL) {
        free(e);
        return NULL;
    }

    set_defaults(e);
    return e;
}

static int enc_init(void *h, fwm_venc_base_config_t *cfg)
{
    fc_h265_instance *e = (fc_h265_instance *)h;
    int first_time = !e->initialised;

    if (e->ve_ops == NULL || e->ve_ops->group_base == NULL)
        return FWM_VENC_RESULT_NO_RESOURCE;

    {
        void *top = e->ve_ops->group_base(e->ve_self, FC_VE_GROUP_TOP);

        if (top == NULL)
            return FWM_VENC_RESULT_NO_RESOURCE;
        e->enc_base = (volatile uint32_t *)((volatile unsigned char *)top + 0xb00);
        e->isp_base = (volatile uint32_t *)((volatile unsigned char *)top + 0xa00);
        freecodec_h264_isp_set_base(e->isp, (unsigned long)(uintptr_t)e->isp_base);
    }

    e->params.color_fmt = cfg->input_format;
    e->params.stride = cfg->input_stride;
    e->params.lbc_lossy_2x = cfg->lbc_lossy_2x_en ? 1u : 0u;
    e->params.lbc_lossy_2_5x = cfg->lbc_lossy_2_5x_en ? 1u : 0u;
    e->params.vbv_no_cache = cfg->bitstream_uncached_en ? 1 : 0;
    /* Dynamic ME is mutually exclusive with the 3-D filter only w.r.t. the
     * level programmed before init (3D-filter spec 6). */
    e->params.dyn_me = (e->params.filter_3d_level == 0u) ? 1 : 0;
    compute_geometry(e, cfg);

    /* SoC limit from the capability record's width/height words (12 section 2.2). */
    if ((e->geom.dst_w > e->capability[7]) || (e->geom.dst_h > e->capability[8]))
        return FWM_VENC_RESULT_NOT_SUPPORT;

    if (first_time) {
        if (alloc_buffers(e) != 0)
            return FWM_VENC_RESULT_NO_MEMORY;
    }

    freecodec_h265_rc_configure(&e->rc, (double)e->params.bitrate,
                                e->params.framerate >= 1000u
                                    ? e->params.framerate / 1000u
                                    : e->params.framerate,
                                e->geom.w16, e->geom.h16,
                                e->params.qp_min, e->params.qp_max,
                                e->params.gop_size, e->params.idr_period);
    freecodec_h265_rc_set_tracking(&e->rc, (int)e->params.rc_track);
    build_header(e);

    e->pic_count = 0;
    e->poc_lsb = 0;
    e->frame_num = 0;
    e->first_picture = 1;
    e->initialised = 1;
    return FWM_VENC_RESULT_OK;
}

static int enc_uninit(void *h)
{
    fc_h265_instance *e = (fc_h265_instance *)h;

    if (!e->initialised)
        return FWM_VENC_RESULT_OK;
    free_buffers(e);
    e->initialised = 0;
    return FWM_VENC_RESULT_OK;
}

static void enc_close(void *h)
{
    fc_h265_instance *e = (fc_h265_instance *)h;

    if (e == NULL)
        return;
    if (e->initialised)
        free_buffers(e);
    if (e->isp != NULL)
        freecodec_h264_isp_destroy(e->isp);
    free(e);
}

/* ---------------------------------------------------------------- params */

static void reconfigure_rc(fc_h265_instance *e)
{
    freecodec_h265_rc_configure(&e->rc, (double)e->params.bitrate,
                                e->params.framerate >= 1000u
                                    ? e->params.framerate / 1000u
                                    : e->params.framerate,
                                e->geom.w16 ? e->geom.w16 : e->geom.dst_w,
                                e->geom.h16 ? e->geom.h16 : e->geom.dst_h,
                                e->params.qp_min, e->params.qp_max,
                                e->params.gop_size, e->params.idr_period);
    freecodec_h265_rc_set_tracking(&e->rc, (int)e->params.rc_track);
}

static int enc_get_parameter(void *h, int index, void *param)
{
    fc_h265_instance *e = (fc_h265_instance *)h;

    switch (index) {
    case FWM_VENC_PARAM_H265_HEADER: {
        fwm_venc_header_blob_t *hd = (fwm_venc_header_blob_t *)param;
        hd->data = e->bufs.header;
        hd->length = e->bufs.header_len;
        return FWM_VENC_RESULT_OK;
    }
    case FWM_VENC_PARAM_SIZE: {
        fwm_venc_size_t *s = (fwm_venc_size_t *)param;
        s->width = (int)e->geom.dst_w;
        s->height = (int)e->geom.dst_h;
        return FWM_VENC_RESULT_OK;
    }
    case FWM_VENC_PARAM_BITSTREAM_STATUS: {
        fwm_venc_bitstream_status_t *v = (fwm_venc_bitstream_status_t *)param;
        v->vbv_size = e->params.vbv_size;
        v->coded_frame_num = (unsigned int)(e->bufs.bs ? BitStreamFrameNum(e->bufs.bs) : 0);
        v->coded_size = e->bufs.bs ? (unsigned int)BitStreamBufferSize(e->bufs.bs) : 0u;
        v->max_frame_length = e->params.vbv_size;
        return FWM_VENC_RESULT_OK;
    }
    case FWM_VENC_PARAM_BITRATE:
        *(int *)param = (int)e->params.bitrate;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_FRAME_RATE:
        *(int *)param = (int)e->params.framerate;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_MAX_KEY_INTERVAL:
        *(int *)param = (int)e->params.max_key_interval;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H265_TRANSFORM:
        *(fwm_venc_h265_transform_t *)param = e->params.transform;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H265_SAO:
        *(fwm_venc_h265_sao_t *)param = e->params.sao;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H265_DEBLOCK:
        *(fwm_venc_h265_deblock_t *)param = e->params.deblock;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H265_TIMING:
        *(fwm_venc_h265_timing_t *)param = e->params.timing;
        return FWM_VENC_RESULT_OK;
    default:
        return FWM_VENC_RESULT_NOT_SUPPORT;
    }
}

static int enc_set_parameter(void *h, int index, void *param)
{
    fc_h265_instance *e = (fc_h265_instance *)h;

    switch (index) {
    case FWM_VENC_PARAM_BITRATE:
        e->params.bitrate = (unsigned int)*(int *)param;
        reconfigure_rc(e);
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_FRAME_RATE:
        e->params.framerate = (unsigned int)*(int *)param;
        reconfigure_rc(e);
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_MAX_KEY_INTERVAL:
        /* The handler writes a fixed 40 (12 section 3.1 / 10 section 5). */
        (void)param;
        e->params.max_key_interval = 40u;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_FORCE_KEY_FRAME:
        e->params.force_key = 1;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_ROTATION:
    case FWM_VENC_PARAM_SLICE_HEIGHT:
    case FWM_VENC_PARAM_CHROMA_GRAY:
    case FWM_VENC_PARAM_I_QP_OFFSET:
        (void)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_STRIDE:
        e->params.stride = (unsigned int)*(int *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_COLOUR_FORMAT:
        e->params.color_fmt = *(fwm_venc_pixel_format_e *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_BITSTREAM_SIZE:
        e->params.vbv_size = (unsigned int)*(int *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H264_QP_RANGE: {
        fwm_venc_qp_range_t *r = (fwm_venc_qp_range_t *)param;
        e->params.qp_max = r->qp_max;
        e->params.qp_min = r->qp_min;
        reconfigure_rc(e);
        return FWM_VENC_RESULT_OK;
    }
    case FWM_VENC_PARAM_H264_PROFILE_LEVEL: {
        fwm_venc_h264_profile_level_t *p = (fwm_venc_h264_profile_level_t *)param;
        e->params.profile_level.profile = (fwm_venc_h265_profile_e)p->profile;
        e->params.profile_level.level = p->level;
        return FWM_VENC_RESULT_OK;
    }
    case FWM_VENC_PARAM_H264_FIXED_QP: {
        fwm_venc_fixed_qp_t *f = (fwm_venc_fixed_qp_t *)param;
        e->params.fixed_qp_enable = f->enable;
        e->params.fixed_i_qp = f->i_qp;
        e->params.fixed_p_qp = f->p_qp;
        return FWM_VENC_RESULT_OK;
    }
    case FWM_VENC_PARAM_FAST_ENCODE:
        e->params.fast_enc = *(int *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_FILTER_3D: {
        unsigned int lvl = *(unsigned char *)param;

        /* 0..6 stored unchanged; above 6 falls back to 3 (3D-filter spec 7). */
        if (lvl > 6u) {
            FC_H265_LOG("3D filter level %u out of range, using 3\n", lvl);
            lvl = 3u;
        }
        e->params.filter_3d_level = lvl;
        return FWM_VENC_RESULT_OK;
    }
    case FWM_VENC_PARAM_H265_RC_TRACK:
        e->params.rc_track = (*(int *)param != 0) ? 1u : 0u;
        freecodec_h265_rc_set_tracking(&e->rc, (int)e->params.rc_track);
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_FILTER_3D_STRENGTH: {
        int s = *(int *)param;

        e->params.filter_3d_strength = s < 0 ? 0u : s > 511 ? 511u : (unsigned int)s;
        return FWM_VENC_RESULT_OK;
    }
    case FWM_VENC_PARAM_DISPLAY_SIZE:
        e->params.display_size = *(fwm_venc_display_size_t *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_DISPLAY_OFFSET:
        e->params.display_offset = *(fwm_venc_display_offset_t *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H265_CONFIG: {
        fwm_venc_h265_config_t *p = (fwm_venc_h265_config_t *)param;

        e->params.profile_level = p->profile_level;
        e->params.qp_max = p->qp_range.qp_max;
        e->params.qp_min = p->qp_range.qp_min;
        e->params.framerate = (unsigned int)p->frame_rate;
        e->params.bitrate = (unsigned int)p->bitrate;
        e->params.qp_init = p->qp_init;
        e->params.rc_mode = p->rc_mode;
        e->params.min_i_qp = p->min_i_qp;
        e->params.gop = p->gop;
        /* gop_size is fixed at 20 (12 section 2.2 / 10 section 5). The vendor
         * also pins idr_period/intra_period to 40; here a configured period is
         * kept when it is a whole number of GOPs (and the 8-bit POC LSB cannot
         * wrap inside it), otherwise the vendor's 40. */
        e->params.gop_size = 20u;
        {
            unsigned int want = (unsigned int)p->idr_period;
            unsigned int keep = (want >= 40u && want <= 200u && (want % 20u) == 0u) ? want : 40u;
            e->params.idr_period = keep;
            e->params.intra_period = keep;
        }
        e->params.gop.gop_size = 20;
        e->params.fixed_qp_enable = p->fixed_qp.enable;
        e->params.fixed_i_qp = p->fixed_qp.i_qp;
        e->params.fixed_p_qp = p->fixed_qp.p_qp;
        reconfigure_rc(e);
        return FWM_VENC_RESULT_OK;
    }
    case FWM_VENC_PARAM_H265_GOP:
        e->params.gop = *(fwm_venc_h265_gop_t *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H265_TOTAL_FRAMES:
        e->params.gop.total_frames_num = *(int *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H265_TENDENCY:
        e->params.tendency = *(fwm_venc_h265_tendency_t *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H265_TRANSFORM:
        e->params.transform = *(fwm_venc_h265_transform_t *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H265_SAO:
        e->params.sao = *(fwm_venc_h265_sao_t *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H265_DEBLOCK:
        e->params.deblock = *(fwm_venc_h265_deblock_t *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H265_TIMING:
        e->params.timing = *(fwm_venc_h265_timing_t *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H265_INTRA_PERIOD:
        (void)param;
        e->params.intra_period = e->params.idr_period;   /* keep equal to the IDR period */
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_OVERLAY:
        return set_overlay(e, (const fwm_venc_overlay_t *)param);
    default:
        /* 0x40a HighPassFilter is deliberately not handled (12 section 3.1). */
        return FWM_VENC_RESULT_NOT_SUPPORT;
    }
}

/* --------------------------------------------------------------- ring API */

static int enc_valid_count(void *h)
{
    fc_h265_instance *e = (fc_h265_instance *)h;

    return e->bufs.bs ? BitStreamFrameNum(e->bufs.bs) : 0;
}

static int enc_get_frame(void *h, fwm_venc_output_frame_t *out)
{
    fc_h265_instance *e = (fc_h265_instance *)h;
    vb_stream_info *info;
    unsigned char *base;

    if (e->bufs.bs == NULL)
        return FWM_VENC_RESULT_BITSTREAM_IS_EMPTY;
    info = BitStreamGetOneBitstream(e->bufs.bs);
    if (info == NULL)
        return FWM_VENC_RESULT_BITSTREAM_IS_EMPTY;

    base = (unsigned char *)BitStreamBaseAddress(e->bufs.bs);
    memset(out, 0, sizeof(*out));
    out->id = info->nID;
    out->pts = info->nPts;
    out->flags = (unsigned int)info->nFlags;
    out->stats.qp = info->CurrQp;
    out->stats.qp_average = info->avQp;
    out->stats.gop_index = info->nGopIndex;
    out->stats.frame_index = info->nFrameIndex;
    out->stats.total_index = info->nTotalIndex;

    {
        int ring_size = BitStreamBufferSize(e->bufs.bs);
        int off = info->nStreamOffset;
        int len = info->nStreamLength;

        if (off + len <= ring_size) {
            out->size0 = (unsigned int)len;
            out->data0 = base + off;
        } else {
            out->size0 = (unsigned int)(ring_size - off);
            out->data0 = base + off;
            out->size1 = (unsigned int)(len - (ring_size - off));
            out->data1 = base;
        }
    }
    return FWM_VENC_RESULT_OK;
}

static int enc_free_frame(void *h, fwm_venc_output_frame_t *out)
{
    fc_h265_instance *e = (fc_h265_instance *)h;
    vb_stream_info info;

    if (e->bufs.bs == NULL)
        return FWM_VENC_RESULT_ERROR;
    memset(&info, 0, sizeof(info));
    info.nID = out->id;
    info.nStreamLength = (int)(out->size0 + out->size1);
    return (BitStreamReturnOneBitstream(e->bufs.bs, &info) == 0)
           ? FWM_VENC_RESULT_OK : FWM_VENC_RESULT_ERROR;
}

static int enc_reset_frames(void *h)
{
    fc_h265_instance *e = (fc_h265_instance *)h;

    if (e->bufs.bs == NULL)
        return FWM_VENC_RESULT_ERROR;
    return (BitStreamReset(e->bufs.bs, e->memops) == 0)
           ? FWM_VENC_RESULT_OK : FWM_VENC_RESULT_ERROR;
}

/* ---------------------------------------------------------------- table */

fwm_venc_device_t video_encoder_h265 = {
    "video_encoder_h265",
    enc_open, enc_init, enc_uninit, enc_close, enc_encode,
    enc_get_parameter, enc_set_parameter,
    enc_valid_count, enc_get_frame, enc_free_frame, enc_reset_frames
};
