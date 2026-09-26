/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.264 encoder device (spec/12-encoder-device.md, r4): the video_encoder_h264
 * fwm_venc_device_t table. Lifecycle, memory and the per-picture engine envelope of
 * section 5.5, built against the kept register-shadow (h264_regs), header
 * (h264_headers), ISP (h264_isp) and rate-control (h264_rc, this pass's
 * companion unit) sources, and the kept support library (venc_base_abi) and
 * engine driver (ve_iface) ABIs.
 *
 * Every hardware register access goes through fc_enc_reg_{read,write,
 * write_block}() (freecodec_enc_regs.c), which are volatile-qualified and
 * kept in their own translation unit (spec section 5.7). */

#include "freecodec_enc_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- helpers */

unsigned int fc_enc_align_up(unsigned int x, unsigned int n)
{
    return ((x + n - 1u) / n) * n;
}

void fc_enc_set_trace(void *handle,
                      void (*trace)(void *opaque, unsigned int block,
                                   unsigned int off, uint32_t val),
                      void *opaque)
{
    fc_enc_instance *e = (fc_enc_instance *)handle;

    e->trace = trace;
    e->trace_opaque = opaque;
}

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void enc_write(fc_enc_instance *e, unsigned int off, uint32_t val)
{
    fc_enc_reg_write(e->enc_base, off / 4u, val);
    if (e->trace)
        e->trace(e->trace_opaque, 0, off, val);
}

static uint32_t enc_read(fc_enc_instance *e, unsigned int off)
{
    return fc_enc_reg_read(e->enc_base, off / 4u);
}

/* Per-picture register script: write back only the offsets the config step
 * computes (spec 12 section 5.5 step 6). A blind whole-window write also
 * zeroes 0x18, the bit-writer control used by the slice-header feed and the
 * kick. The offset list is a register-map fact, kept as observed. */
static const unsigned int ENC_SCRIPT_WRITE_OFFSETS[] = {
    0x04u, 0x08u, 0x14u, 0x1cu, 0x28u, 0x2cu, 0x30u, 0x34u, 0x38u, 0x3cu,
    0x40u, 0x44u, 0x48u, 0x4cu, 0x78u, 0x7cu, 0x80u, 0x84u, 0x88u, 0x8cu,
    0x94u, 0x9cu, 0xa0u, 0xa4u, 0xa8u, 0xacu, 0xb0u, 0xb4u, 0xb8u, 0xbcu,
    0xc0u, 0xc4u, 0xc8u, 0xf8u, 0xfcu
};

static void enc_write_script_selective(fc_enc_instance *e, const uint32_t *words)
{
    unsigned int i;

    for (i = 0; i < sizeof(ENC_SCRIPT_WRITE_OFFSETS) / sizeof(ENC_SCRIPT_WRITE_OFFSETS[0]); i++) {
        unsigned int off = ENC_SCRIPT_WRITE_OFFSETS[i];

        enc_write(e, off, words[off / 4u]);
    }
}

/* ----------------------------------------------------------------- memory */

static int alloc_one(fc_enc_instance *e, fc_enc_buf *b, unsigned int size)
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
    /* Section 3.3: flush before any encode() call can hand the address to
     * hardware. All of our buffers qualify (either the engine reads them, or
     * software wrote content the engine will read), so every buffer is
     * flushed once here, right after allocation -- the r2-style approach the
     * spec calls behaviourally equivalent to a single batch flush. */
    e->memops->flush_cache(b->vir, (int)size);
    return 0;
}

static void free_one(fc_enc_instance *e, fc_enc_buf *b)
{
    if (b->vir != NULL)
        e->memops->pfree(b->vir, e->ve_ops, e->ve_self);
    memset(b, 0, sizeof(*b));
}

static void free_buffers(fc_enc_instance *e)
{
    int i;

    if (e->bufs.bs != NULL) {
        BitStreamDestroy(e->bufs.bs);
        e->bufs.bs = NULL;
    }
    for (i = 0; i < 2; i++) {
        free_one(e, &e->bufs.full[i]);
        free_one(e, &e->bufs.aux[i]);
        free_one(e, &e->bufs.filt3d[i]);
    }
    free_one(e, &e->bufs.mb_info);
    free_one(e, &e->bufs.dblk);
    free_one(e, &e->bufs.mbrc);
    free_one(e, &e->bufs.mv);
    free_one(e, &e->ovl_hdr);
    free_one(e, &e->ovl_data);
    free_one(e, &e->ovl_inv);
    e->ovl.b_overlay = 0;
}

static int alloc_buffers(fc_enc_instance *e)
{
    const fc_enc_geom *g = &e->geom;
    /* Luma rows are align64(h16 + 1), not the spec's align64(h16): when h16 is
     * already a multiple of 64 (e.g. 1920x1088) the spec leaves no padding and
     * the engine writes past the luma plane into the start of the reference
     * chroma (top-left chroma drift that grows every P-frame, r35gb
     * 2026-09-26). One 16-row band of padding was measured clean; other
     * heights are unchanged. */
    unsigned int luma = fc_enc_align_up(g->w16, 32u) * fc_enc_align_up(g->h16 + 1u, 64u);
    unsigned int chroma = fc_enc_align_up(g->w16, 32u) * fc_enc_align_up(g->h16, 128u) / 2u;
    unsigned int full_size = luma + chroma;
    int i;

    e->bufs.luma_size = luma;
    e->bufs.chroma_size = chroma;

    for (i = 0; i < 2; i++)
        if (alloc_one(e, &e->bufs.full[i], full_size) != 0)
            goto fail;

    e->bufs.has_aux = (e->ic_version > FC_AUX_PLANE_MIN_IC);
    if (e->bufs.has_aux) {
        unsigned int aux_size = fc_aux_plane_bytes(g->dst_w, g->dst_h);

        for (i = 0; i < 2; i++)
            if (alloc_one(e, &e->bufs.aux[i], aux_size) != 0)
                goto fail;
    }

    if (alloc_one(e, &e->bufs.mb_info, g->w16 * 8u) != 0)
        goto fail;
    if (alloc_one(e, &e->bufs.dblk, g->w16 * 8u) != 0)
        goto fail;

    {
        unsigned int mbrc_size = fc_mbrc_buffer_bytes(g->h_mb);

        if (alloc_one(e, &e->bufs.mbrc, mbrc_size) != 0)
            goto fail;
        memset(e->bufs.mbrc.vir, 0, mbrc_size);   /* zero-filled, spec 3.2 */
        e->memops->flush_cache(e->bufs.mbrc.vir, (int)mbrc_size);
    }

    if (alloc_one(e, &e->bufs.mv, g->h_mb * 8u * fc_enc_align_up(g->w_mb, 4u)) != 0)
        goto fail;

    /* 3D-filter planes: dst-height-MB * 12 * align8(dst-width-MB), two of them,
     * memset 0xff (memory spec, allocation table 0x15d0). */
    {
        unsigned int f3d_size = g->h_mb * 12u * fc_enc_align_up(g->w_mb, 8u);

        for (i = 0; i < 2; i++) {
            if (alloc_one(e, &e->bufs.filt3d[i], f3d_size) != 0)
                goto fail;
            memset(e->bufs.filt3d[i].vir, 0xff, f3d_size);
            e->memops->flush_cache(e->bufs.filt3d[i].vir, (int)f3d_size);
        }
    }

    e->bufs.bs = BitStreamCreate(e->params.vbv_no_cache ? 1 : 0, (int)e->params.vbv_size, e->memops,
                                 e->ve_ops, e->ve_self);
    if (e->bufs.bs == NULL)
        goto fail;

    return 0;

fail:
    free_buffers(e);
    return -1;
}

/* --------------------------------------------------------------- geometry */

static void compute_geometry(fc_enc_instance *e, fwm_venc_base_config_t *cfg)
{
    fc_enc_geom *g = &e->geom;

    g->in_w = cfg->input_width;
    g->in_h = cfg->input_height;
    g->dst_w = cfg->output_width ? cfg->output_width : cfg->input_width;
    g->dst_h = cfg->output_height ? cfg->output_height : cfg->input_height;
    g->in_stride = cfg->input_stride ? cfg->input_stride : g->dst_w;

    g->w16 = fc_enc_align_up(g->dst_w, 16u);
    g->h16 = fc_enc_align_up(g->dst_h, 16u);
    g->w_mb = g->w16 / 16u;
    g->h_mb = g->h16 / 16u;
}

static unsigned int effective_fps(const fc_enc_params *p)
{
    /* Section 8: framerate >= 1000 means milli-fps. The rate-control unit's
     * own interface (spec 10 section 2) takes an integer frames/s figure, so
     * a milli-fps configuration is truncated to whole frames/s for its bit-
     * budget math -- a boundary choice forced by that unit's interface, not
     * a bug. */
    return (p->framerate >= 1000u) ? (p->framerate / 1000u) : p->framerate;
}

/* ----------------------------------------------------------- persistent regs */

static void compute_persistent_regs(fc_enc_instance *e)
{
    freecodec_h264_reg_info_cfg cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.profile_idc = (unsigned int)e->params.profile_level.profile;
    cfg.ic_version = e->ic_version;
    cfg.entropy_coding_mode_flag = (unsigned int)e->params.cabac_enable;
    cfg.cabac_context_index = 1u;   /* r2/02 section 2.1: fixed, always 1 */
    cfg.deblock_idc = 0u;
    cfg.alpha_offset_div2 = 0;
    cfg.beta_offset_div2 = 0;
    cfg.slice_qp = 0u;              /* overwritten every picture at slice time */
    cfg.dst_width_mb = e->geom.w_mb;
    cfg.dst_height_mb = e->geom.h_mb;
    cfg.display_height_16align = e->geom.h16;
    cfg.chroma_qp_index_offset = 0u;
    cfg.alter_frame_en = 0u;
    cfg.transform8x8_mode_flag = 0u;
    cfg.use_intra_in_p = 1u;        /* r2/02 section 2.5 default */
    cfg.use_intra_4x4 = 1u;
    cfg.mv_addr_vir_null = 0u;      /* the MV workspace is always allocated */
    cfg.binned_image_output_enable = 0u;  /* clean design divergence, spec 11 sec 8 */
    cfg.use_hp_filter = 0u;
    cfg.qp_mad_sse_output_en = 0u;  /* bit 27 stays 0 (spec 12 sec 16 item 4) */
    cfg.fix_qp_enable = (unsigned int)e->params.fixed_qp_enable;
    cfg.fast_enc = e->params.fast_enc ? 1u : 0u;
    cfg.filter_3d_level = e->params.filter_3d_level;
    cfg.th_smart_img_bin = 0x14u;   /* reference 0x74000014, spec 11 sec 2.5 */
    cfg.smart_en = 0u;
    cfg.smart_shift_bits = 0u;
    cfg.hvs_en = 0u;
    cfg.th_hvs_dir = 0u;
    cfg.th_hvs_coef_shift = 0u;
    cfg.skip_tend_pic_en = 0u;
    cfg.mode_ctrl_en = 0u;
    cfg.hpfilter_coef_th = 5u;
    cfg.hpfilter_coef_shift = 3u;
    cfg.hpfilter_contrast_th = 0u;
    cfg.hpfilter_mad_th = 0u;
    cfg.th_bright = 0u;
    cfg.th_dark = 0u;

    freecodec_h264_init_reg_info(&cfg, &e->reg_info);
    e->slice_seed = e->reg_info.shadow;
}

/* ------------------------------------------------------------- SPS/PPS/ISP */

static void build_sps_pps(fc_enc_instance *e)
{
    freecodec_h264_seq_cfg sps;
    freecodec_h264_pic_cfg pps;
    int n;

    memset(&sps, 0, sizeof(sps));
    sps.profile_idc = (unsigned int)e->params.profile_level.profile;
    sps.level_idc = (unsigned int)e->params.profile_level.level;
    sps.log2_max_frame_num = 8u;
    sps.pic_order_cnt_type = 0u;
    sps.log2_max_pic_order_cnt_lsb = 8u;
    sps.num_ref_frames = 1u;
    sps.gaps_in_frame_num_allowed = 0u;
    sps.pic_width_in_mbs = e->geom.w_mb;
    sps.pic_height_in_map_units = e->geom.h_mb;
    sps.frame_mbs_only_flag = 1u;
    sps.direct_8x8_inference_flag = 1u;
    {
        /* The coded picture is the 16-aligned w16 x h16, so
         * the SPS must always crop back to the shown window -- not only when
         * a display size is set (640x360 went out as 640x368, eight extra
         * rows). The shown window is the display size if set, else dst,
         * centred within dst; everything past it up to the coded edge is
         * cropped on the right/bottom. Crop units are 2 pixels (4:2:0). */
        unsigned int show_w = e->geom.dst_w, show_h = e->geom.dst_h;
        unsigned int left, top, right, bottom;

        if (e->params.display_size.width > 0 && e->params.display_size.height > 0 &&
            (unsigned int)e->params.display_size.width <= e->geom.dst_w &&
            (unsigned int)e->params.display_size.height <= e->geom.dst_h) {
            show_w = (unsigned int)e->params.display_size.width;
            show_h = (unsigned int)e->params.display_size.height;
        }
        left = ((e->geom.dst_w - show_w) / 2u) & ~1u;
        top = ((e->geom.dst_h - show_h) / 2u) & ~1u;
        /* A set display offset moves the window off-centre (e.g. to crop a
         * sensor's bad edge columns); ignored if it would leave dst. */
        if (e->params.display_offset.left >= 0 &&
            ((unsigned int)e->params.display_offset.left & ~1u) + show_w <= e->geom.dst_w)
            left = (unsigned int)e->params.display_offset.left & ~1u;
        if (e->params.display_offset.top >= 0 &&
            ((unsigned int)e->params.display_offset.top & ~1u) + show_h <= e->geom.dst_h)
            top = (unsigned int)e->params.display_offset.top & ~1u;
        right = e->geom.w16 - show_w - left;
        bottom = e->geom.h16 - show_h - top;

        sps.crop_left_units = left / 2u;
        sps.crop_top_units = top / 2u;
        sps.crop_right_units = right / 2u;
        sps.crop_bottom_units = bottom / 2u;
    }
    sps.vui_flag = 0u;

    n = freecodec_h264_build_sps(&sps, e->spspps, (int)sizeof(e->spspps));
    e->spspps_len = (n > 0) ? (unsigned int)n : 0u;

    memset(&pps, 0, sizeof(pps));
    pps.profile_idc = sps.profile_idc;
    pps.entropy_coding_mode_flag = (unsigned int)e->params.cabac_enable;
    pps.pic_init_qp_minus26 = 0u;   /* pic_init_qp = 26, spec 12 sec 8 default */
    pps.chroma_qp_index_offset = 0u;
    pps.constrained_intra_pred_flag = 0u;
    pps.transform_8x8_mode_flag = 0u;
    pps.num_ref_idx_l0_default_active = 1u;
    pps.num_ref_idx_l1_default_active = 1u;
    pps.deblocking_filter_control_present = 1u;

    if (e->spspps_len < sizeof(e->spspps)) {
        n = freecodec_h264_build_pps(&pps, e->spspps + e->spspps_len,
                                     (int)(sizeof(e->spspps) - e->spspps_len));
        if (n > 0)
            e->spspps_len += (unsigned int)n;
    }
}

/* ----------------------------------------------------------- bit-writer -- */

/* Spec section 5.6: wait for status bit 9 (bounded poll), else write the
 * value and the (length<<8)|1 command. On timeout, emit nothing for this
 * call -- the reference's own observed behaviour, carried forward as-is. */
static void feed_bits(fc_enc_instance *e, unsigned int val, unsigned int n)
{
    unsigned int tries;
    int ready = 0;

    for (tries = 0; tries < 10000u; tries++) {
        if (enc_read(e, 0x1c) & (1u << 9)) {
            ready = 1;
            break;
        }
    }
    if (!ready)
        return;
    enc_write(e, 0x20, val);
    enc_write(e, 0x18, (n << 8) | 1u);
}

static void feed_header(fc_enc_instance *e, const unsigned char *buf, int bits)
{
    int pos = 0;

    while (pos < bits) {
        int take = bits - pos;
        unsigned int chunk;

        if (take > 8) take = 8;
        chunk = buf[pos / 8];
        if (take < 8)
            chunk >>= (unsigned int)(8 - take);
        feed_bits(e, chunk, (unsigned int)take);
        pos += take;
    }
}

/* ---------------------------------------------------- init-time SPS/PPS -- */

static void emit_sps_pps(fc_enc_instance *e)
{
    unsigned int off, free_sp;

    fc_ve_enc_on(e->ve_ops, e->ve_self);

    off = (unsigned int)BitStreamWriteOffset(e->bufs.bs);
    free_sp = (unsigned int)BitStreamFreeBufferSize(e->bufs.bs);

    enc_write(e, 0x88, off << 3);
    enc_write(e, 0x80, (uint32_t)(uintptr_t)BitStreamBasePhyAddress(e->bufs.bs) >> 8);
    enc_write(e, 0x84, (uint32_t)(uintptr_t)BitStreamEndPhyAddress(e->bufs.bs) >> 8);
    enc_write(e, 0x8c, (off + free_sp) << 3);

    enc_write(e, 0x18, 0u);   /* open header mode */
    {
        unsigned int i;

        for (i = 0; i < e->spspps_len; i++)
            feed_bits(e, e->spspps[i], 8u);
    }
    enc_write(e, 0x18, 2u);   /* close header mode */
}

/* --------------------------------------------------------------- ROI 0x70 */

static uint32_t compute_roi_reg(const fc_enc_params *p)
{
    uint32_t v = 0xff000000u;
    int i;

    if (!p->roi_enable)
        return v;
    for (i = 0; i < 8; i++)
        if (p->roi[i].enable)
            v &= ~(1u << (24 + i));
    return v;
}

/* ----------------------------------------------------------- ISP program */

static void program_isp(fc_enc_instance *e, fwm_venc_input_picture_t *in)
{
    freecodec_h264_isp_info info;

    memset(&info, 0, sizeof(info));
    info.rotate_angle = e->params.rotation / 90;
    info.input_width_mb = (int)fc_enc_align_up(e->geom.in_w, 8u) / 8;
    info.input_height_mb = (int)fc_enc_align_up(e->geom.in_h, 8u) / 8;
    info.output_width_mb = (int)fc_enc_align_up(e->geom.dst_w, 8u) / 8;
    info.output_height_mb = (int)fc_enc_align_up(e->geom.dst_h, 8u) / 8;
    info.input_stride_mb = (int)(e->geom.in_stride / 16u);
    info.color_format = (int)e->params.color_fmt;
    if (in != NULL && in->csi_colour_format_en)
        info.color_format = in->csi_colour_format;
    /* LBC input: the lossy-ratio flags select the compressed line strides
     * and the ISP's lossy-decode enable (spec 13). */
    info.b_lbc_lossy_com_en_2x = (unsigned char)e->params.lbc_lossy_2x;
    info.b_lbc_lossy_com_en_2_5x = (unsigned char)e->params.lbc_lossy_2_5x;
    if (in != NULL) {
        /* Second chroma-plane address: one quarter-frame past the first
         * (spec 13); the high-8 fields carry address bits 32-39. */
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
    info.b_height_is8_align = ((e->geom.in_h & 7u) == 0u) ? 1u : 0u;
    info.horizonflip_enable = e->params.hflip;
    info.ic_version = (int)e->ic_version;
    info.n_encode_format = 1;   /* spec 13 section 1.3: 1 on this IC */
    info.memory_type = 0;

    freecodec_h264_isp_set_register(e->isp, &info);
}

/* --------------------------------------------------------- rate control -- */

static void configure_rc(fc_enc_instance *e)
{
    freecodec_rc_configure(&e->rc, (double)e->params.bitrate, effective_fps(&e->params),
                           e->geom.dst_w, e->geom.dst_h,
                           e->params.qp_min, e->params.qp_max, e->params.max_qp_step,
                           e->params.max_key_interval);
}

/* ================================================================ encode */

static int enc_encode(void *h, fwm_venc_input_picture_t *in)
{
    fc_enc_instance *e = (fc_enc_instance *)h;
    unsigned int n = e->pic.picture_count;
    int is_idr;
    int qp;
    long long i_budget = 0;
    unsigned char slice_buf[512];
    int slice_bits = 0;
    uint32_t seed_1c;
    uint32_t regs[0x200];
    freecodec_h264_config_reg_cfg rcfg;
    unsigned int main_last, main_curr;
    uint32_t baseline, status, slice_regs[6];
    long long length;
    freecodec_h264_slice_state sstate;
    freecodec_h264_slice_cfg scfg;
    vb_stream_info sinfo;

    if (!e->initialised)
        return FWM_VENC_RESULT_ERROR;


    is_idr = e->pic.first_picture || e->params.force_key ||
             (e->params.max_key_interval > 0u &&
              (n % e->params.max_key_interval) == 0u);
    e->params.force_key = 0;

    /* Section 5.2/5.3: fixed-QP mode uses the configured QP outright; a
     * live IDR uses the fixed default of 37 (spec 10 section 7), and the
     * rate-control unit's own start_picture(I) return value is discarded,
     * though it is still called (with finish_picture afterwards) so its
     * internal bookkeeping -- pictures-since-start-sequence, activity
     * history -- stays continuous across the IDR (spec 10 section 7's
     * recommendation when this default is adopted). */
    {
        int rc_qp = freecodec_rc_start_picture(&e->rc, is_idr ? FC_RC_PIC_I : FC_RC_PIC_P);

        if (e->params.fixed_qp_enable)
            qp = is_idr ? e->params.fixed_i_qp : e->params.fixed_p_qp;
        else if (is_idr && e->last_p_qp > 0)
            /* Divergence from the fixed IDR QP 37: a fixed I-QP is far coarser
             * than the P pictures whenever rate control runs P below 37 (dark
             * or static scenes), so detail visibly "snaps" at every IDR - once
             * a second on a 1 s GOP (r35gb 2026-09-26). Code the IDR at the
             * latest P picture's QP instead: the same quantisation keeps detail
             * steady across the IDR (QP-2 also worked but made 1080p IDRs
             * ~210 KB, bursts a weak Wi-Fi link struggles with). */
            qp = clampi(e->last_p_qp, e->params.qp_min, e->params.qp_max);
        else if (is_idr)
            qp = 37;                    /* first picture: no P history yet */
        else
            qp = e->last_p_qp = clampi(rc_qp, e->params.qp_min, e->params.qp_max);
    }

    if (is_idr)
        i_budget = (long long)(5.0 * (double)e->params.bitrate / (double)effective_fps(&e->params));

    /* Steps 1/19 (lock): not taken here. The framework (fenc.c) already
     * holds the shared VE lock around encode(); taking the same
     * non-recursive mutex again deadlocks on the first picture. */
    e->ve_ops->reset(e->ve_self);                                  /* step 2 */
    /* No BitStreamReset(): the consumer drains the ring on its own thread,
     * so the ring's bookkeeping must not be wiped per picture. The engine
     * side is handled at step 6 (offset 0). */
    fc_ve_enc_on(e->ve_ops, e->ve_self);                           /* step 3 */

    if (!is_idr) {
        /* step 5: per-row MB-RC table, flushed before the kick (section 3.3). */
        fc_mbrc_build_table((uint8_t *)e->bufs.mbrc.vir, e->geom.h_mb, NULL, NULL);
        e->memops->flush_cache(e->bufs.mbrc.vir,
                               (int)fc_mbrc_buffer_bytes(e->geom.h_mb));
    }

    /* step 6: register script. Confirmed-bug-1 (spec sections 9/5.5 item 6):
     * seed 0x1c from the LIVE value read after the reset pulse just above --
     * never a hardcoded constant, even though it reads 0 today. */
    seed_1c = enc_read(e, 0x1c);
    memset(regs, 0, sizeof(regs));
    regs[0x1c / 4] = seed_1c;

    main_curr = n % 2u;
    main_last = (n == 0u) ? 0u : ((n - 1u) % 2u);

    memset(&rcfg, 0, sizeof(rcfg));
    /* The step-2 reset pulse returns the engine's own (shared,
     * engine-internal) bitstream write position to 0, and 0x88 does not move
     * it, so every picture lands at buffer offset 0 whatever the ring's
     * running offset says. Encode against, and publish from, offset 0 with
     * the whole buffer free (h264-two-channel.md section 3). The ring keeps
     * its own bookkeeping; only the frame's recorded offset is 0. */
    rcfg.out_buffer_offset = 0u;
    rcfg.valid_buffer_size = (unsigned int)BitStreamBufferSize(e->bufs.bs);
    rcfg.ic_version = e->ic_version;
    rcfg.profile_idc = (unsigned int)e->params.profile_level.profile;
    rcfg.level_idc = (unsigned int)e->params.profile_level.level;
    rcfg.dst_width_mb = e->geom.w_mb;
    rcfg.dst_height_mb = e->geom.h_mb;
    rcfg.display_width_16align = e->geom.w16;
    rcfg.display_height_16align = e->geom.h16;
    rcfg.slice_type = is_idr ? 2u : 0u;
    rcfg.coding_type = is_idr ? 18u : 0u;
    rcfg.n_slice_height = 0u;    /* software-header path, the only one used */
    rcfg.slice_qp = qp;
    rcfg.curr_frm_idx = main_curr;
    rcfg.frame_count = n;
    rcfg.classify_engine_enable = e->params.fixed_qp_enable ? 0u : 1u;
    rcfg.mb_rc_enable = e->reg_info.mb_rc_enable;
    /* The 3D filter level can change live; dynamic ME runs only with it off
     * (reg-shadow spec: dynamic-ME enable <- level == 0). */
    rcfg.filter_3d_level = e->params.filter_3d_level;
    rcfg.dynamic_me_enable = e->params.filter_3d_level == 0u;
    rcfg.filter_3d_phy[0] = (unsigned int)((uintptr_t)e->bufs.filt3d[0].phy >> 8);
    rcfg.filter_3d_phy[1] = (unsigned int)((uintptr_t)e->bufs.filt3d[1].phy >> 8);
    rcfg.qp_mad_sse_output_flag = 0u;   /* stays off, spec 12 section 16 item 4 */
    rcfg.use_img_bin_flag = 0u;
    rcfg.binned_image_output_enable = 0u;
    rcfg.hp_coef_th = 5u; rcfg.hp_coef_shift = 3u;
    rcfg.hp_contrast_th = 0u; rcfg.hp_mad_th = 0u;
    rcfg.rpara1 = e->reg_info.shadow.slice_stride_control;
    rcfg.rrate_ctrl_init = e->reg_info.shadow.mb_rc_control;
    rcfg.rsmart_func = e->reg_info.shadow.adaptive_control;
    rcfg.rtemporal_para = e->reg_info.shadow.temporal_param;
    rcfg.rtemporal_para_v5 = e->reg_info.shadow.hp_variance_control;
    rcfg.rpara0_v1 = e->reg_info.shadow.picture_control;
    rcfg.bitstream_base_phy = (unsigned int)((uintptr_t)BitStreamBasePhyAddress(e->bufs.bs) >> 8);
    rcfg.bitstream_end_phy = (unsigned int)((uintptr_t)BitStreamEndPhyAddress(e->bufs.bs) >> 8);
    rcfg.mb_info_phy = (unsigned int)((uintptr_t)e->bufs.mb_info.phy >> 8);
    rcfg.dblk_phy = (unsigned int)((uintptr_t)e->bufs.dblk.phy >> 8);
    rcfg.mb_rate_ctrl_phy = (unsigned int)((uintptr_t)e->bufs.mbrc.phy >> 8);
    rcfg.enc_pic_y[0] = (unsigned int)((uintptr_t)e->bufs.full[0].phy >> 8);
    rcfg.enc_pic_c[0] = (unsigned int)(((uintptr_t)e->bufs.full[0].phy + e->bufs.luma_size) >> 8);
    rcfg.enc_pic_y[1] = (unsigned int)((uintptr_t)e->bufs.full[1].phy >> 8);
    rcfg.enc_pic_c[1] = (unsigned int)(((uintptr_t)e->bufs.full[1].phy + e->bufs.luma_size) >> 8);
    if (e->bufs.has_aux) {
        rcfg.sub_enc_pic_y[0] = (unsigned int)((uintptr_t)e->bufs.aux[0].phy >> 8);
        rcfg.sub_enc_pic_y[1] = (unsigned int)((uintptr_t)e->bufs.aux[1].phy >> 8);
    }
    rcfg.main_ring_last_idx = main_last;
    rcfg.main_ring_curr_idx = main_curr;
    rcfg.sub_ring_last_idx = main_last;
    rcfg.sub_ring_curr_idx = main_curr;
    rcfg.target_bits = is_idr ? (unsigned int)i_budget
                              : (unsigned int)freecodec_rc_target(&e->rc);
    rcfg.classify_th[0] = 0x120f0c09u;
    rcfg.classify_th[1] = 0x261e1814u;
    rcfg.classify_th[2] = 0xffff3630u;
    rcfg.dyn_me_th[0] = (e->geom.w_mb * 1u) / 10u;
    rcfg.dyn_me_th[1] = (e->geom.w_mb * 6u) / 10u;
    rcfg.dyn_me_th[2] = (e->geom.w_mb * 26u) / 10u;
    rcfg.dyn_me_th[3] = (e->geom.w_mb * 32u) / 10u;

    freecodec_h264_config_registers(&rcfg, regs);
    enc_write_script_selective(e, regs); /* selected offsets only -- see the fix note above enc_write_script_selective */

    /* step 4: ISP, after the register script (spec 12 section 5.5). */
    program_isp(e, in);

    /* step 7: ROI (spec 11 section 1). */
    enc_write(e, 0x70, compute_roi_reg(&e->params));

    /* step 8: stream-counter baseline. */
    baseline = enc_read(e, 0x90);

    /* step 9: slice header, fed through the bit-writer. */
    memset(&scfg, 0, sizeof(scfg));
    scfg.coding_type = is_idr ? 18u : 0u;
    scfg.slice_type = is_idr ? 2u : 0u;
    scfg.model_number = 1u;             /* r2/02 section 2.1: fixed context index 1 */
    scfg.entropy_coding_mode_flag = (unsigned int)e->params.cabac_enable;
    scfg.slice_qp = qp;
    scfg.pic_type = 0u;                 /* frame */
    scfg.frame_num = e->pic.frame_num;
    scfg.pic_order_cnt_type = 0u;
    scfg.log2_max_pic_order_cnt_lsb = 8u;
    scfg.top_poc = (int)e->pic.poc_lsb;
    scfg.bottom_poc = (int)e->pic.poc_lsb;
    scfg.n_slice_height = 0u;
    scfg.long_term_ref_enable = 0u;
    scfg.frame_count = n;
    scfg.n_long_ref_poc = 0u;
    scfg.virtual_period = 0u;
    scfg.motion_parameter = 0u;
    scfg.seed_shadow = &e->slice_seed;

    if (is_idr) {
        freecodec_h264_idr_slice_cfg h;

        memset(&h, 0, sizeof(h));
        h.log2_max_frame_num = 8u;
        h.pic_order_cnt_type = 0u;
        h.log2_max_pic_order_cnt_lsb = 8u;
        h.pic_parameter_set_id = 0u;
        h.entropy_coding_mode_flag = (unsigned int)e->params.cabac_enable;
        h.frame_num = e->pic.frame_num;
        h.idr_pic_id = e->pic.idr_pic_id;
        h.pic_order_cnt_lsb = e->pic.poc_lsb;
        h.slice_qp = (unsigned int)qp;
        h.pic_init_qp = 26u;
        h.disable_deblocking_filter_idc = 0u;
        h.alpha_offset_div2 = 0;
        h.beta_offset_div2 = 0;
        slice_bits = 0;
        freecodec_h264_build_idr_slice(&h, slice_buf, (int)sizeof(slice_buf), &slice_bits);
    } else {
        freecodec_h264_p_slice_cfg h;

        memset(&h, 0, sizeof(h));
        h.log2_max_frame_num = 8u;
        h.pic_order_cnt_type = 0u;
        h.log2_max_pic_order_cnt_lsb = 8u;
        h.pic_parameter_set_id = 0u;
        h.entropy_coding_mode_flag = (unsigned int)e->params.cabac_enable;
        h.cabac_init_idc = 1u;   /* r2/02 section 2.1: equal to the 0x04 context index */
        h.frame_num = e->pic.frame_num;
        h.pic_order_cnt_lsb = e->pic.poc_lsb;
        h.nal_reference_idc = 2u;
        h.num_ref_idx_l0_active = 1u;
        h.pps_num_ref_idx_l0_default = 1u;
        h.slice_qp = (unsigned int)qp;
        h.pic_init_qp = 26u;
        h.disable_deblocking_filter_idc = 0u;
        h.alpha_offset_div2 = 0;
        h.beta_offset_div2 = 0;
        slice_bits = 0;
        freecodec_h264_build_p_slice(&h, slice_buf, (int)sizeof(slice_buf), &slice_bits);
    }
    feed_header(e, slice_buf, slice_bits);

    /* step 10: slice-time words. Confirmed-bug-2 (spec sections 9/5.5 item
     * 10): 0x0c keeps its real computed value (already produced correctly by
     * the kept register unit below); the whole top byte of 0x04 is cleared.
     * That includes bit 31, eptbDisable: it is 1 while the software-built
     * start code and slice header go through the bit-writer (the step-6
     * script sets it) and must be 0 for the slice data, so the engine inserts
     * emulation-prevention bytes. Left at 1, the occasional 00 00 0x run in
     * the coded data breaks the picture (h264-idr-divergence.md; reference
     * capture 0x04 = 0x00000100). */
    freecodec_h264_hw_slice_header(&scfg, 1 /* top_poc source */, 1 /* ver2 */, &sstate);
    freecodec_h264_set_reg_ver2(&sstate, e->dram_type, e->reg_info.over_time_mb_count,
                                (uint32_t)((uintptr_t)e->bufs.mv.phy >> 8), slice_regs);
    slice_regs[0] &= 0x00ffffffu;

    enc_write(e, 0x04, slice_regs[0]);
    enc_write(e, 0x08, slice_regs[1]);
    enc_write(e, 0x0c, slice_regs[2]);
    enc_write(e, 0x10, slice_regs[3]);
    enc_write(e, 0x24, slice_regs[4]);
    enc_write(e, 0x60, slice_regs[5]);


    /* step 11: start. */
    enc_write(e, 0x18, 8u);

    /* step 12: wait. */
    if (e->ve_ops->wait_irq(e->ve_self) != 0) {
        fc_ve_enc_off(e->ve_ops, e->ve_self);
        return FWM_VENC_RESULT_ERROR;
    }

    /* step 13: length. */
    length = ((long long)enc_read(e, 0x90) - (long long)baseline) / 8;
    if (length <= 0 || (unsigned long long)length > (unsigned long long)rcfg.valid_buffer_size) {
        fc_ve_enc_off(e->ve_ops, e->ve_self);
        return FWM_VENC_RESULT_ERROR;
    }

    /* A cached ring: every picture lands at offset 0, so lines the CPU read
     * from the previous picture can still be cached there; drop them before
     * the consumer reads this one (spec 12 section 3.3). */
    if (!e->params.vbv_no_cache)
        e->memops->flush_cache(BitStreamBaseAddress(e->bufs.bs), (int)length);

    /* Optional CBR filling (spec section 5.5, section 8): pad a below-
     * threshold-QP picture up to the nominal per-picture byte budget. */
    if (e->params.cbr_filling && qp < 3) {
        long long want = (long long)e->params.bitrate / (long long)effective_fps(&e->params) / 8;

        if (want > length && (unsigned long long)want <= (unsigned long long)rcfg.valid_buffer_size) {
            unsigned char *base = (unsigned char *)BitStreamBaseAddress(e->bufs.bs);

            memset(base + rcfg.out_buffer_offset + length, 0, (size_t)(want - length));
            if (!e->params.vbv_no_cache)   /* write the padding back past the cache */
                e->memops->flush_cache(base + rcfg.out_buffer_offset + length,
                                       (int)(want - length));
            length = want;
        }
    }

    /* step 14: acknowledge -- write back the freshly read 0x1c value. */
    status = enc_read(e, 0x1c);
    enc_write(e, 0x1c, status);

    /* step 15: activity. Bit 27 (MAD/SSE output) is never enabled by this
     * device (spec section 16 item 4), so 0x50 reads back whatever the
     * classification path alone leaves there; the rate-control unit applies
     * its own zero-fallback (spec 10 section 5). */
    {
        unsigned int activity = enc_read(e, 0x50);

        /* step 16: publish to the ring. */
        memset(&sinfo, 0, sizeof(sinfo));
        sinfo.nStreamOffset = (int)rcfg.out_buffer_offset;
        sinfo.nStreamLength = (int)length;
        sinfo.nPts = (in != NULL) ? in->pts : 0;
        sinfo.nFlags = is_idr ? FWM_VENC_FRAME_KEYFRAME : 0;
        sinfo.CurrQp = qp;
        sinfo.avQp = qp;
        sinfo.nGopIndex = 0;
        sinfo.nFrameIndex = (int)n;
        sinfo.nTotalIndex = (int)n;
        BitStreamAddOneBitstream(e->bufs.bs, &sinfo);

        /* step 17: finish picture (not in fixed-QP mode; still safe to call
         * otherwise -- see the start_picture note above). */
        freecodec_rc_finish_picture(&e->rc, length * 8, activity);
    }

    /* step 18: disable. */
    fc_ve_enc_off(e->ve_ops, e->ve_self);

    /* Section 5.1 counters, advanced only after a picture fully succeeds. */
    if (is_idr) {
        e->pic.frame_num = 0u;
        e->pic.poc_lsb = 0u;
        if (!e->pic.first_picture)
            e->pic.idr_pic_id ^= 1u;
    } else {
        e->pic.frame_num = (e->pic.frame_num + 1u) & 0xffu;
        e->pic.poc_lsb = (e->pic.poc_lsb + 2u) & 0xffu;
    }
    e->pic.first_picture = 0;
    e->pic.picture_count = n + 1u;


    return FWM_VENC_RESULT_OK;
}

/* ============================================================== lifecycle */

static void *enc_open(fwm_venc_base_config_t *cfg, unsigned int ic_version)
{
    fc_enc_instance *e;
    fc_ve_ops *ve_ops;
    void *top = NULL;

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

    if (ve_ops != NULL && ve_ops->group_base != NULL)
        top = ve_ops->group_base(e->ve_self, FC_VE_GROUP_TOP);
    e->enc_base = top ? (volatile uint32_t *)((volatile unsigned char *)top + 0xb00) : NULL;
    e->isp_base = top ? (volatile uint32_t *)((volatile unsigned char *)top + 0xa00) : NULL;
    if (ve_ops != NULL && ve_ops->dram_type != NULL)
        e->dram_type = (unsigned int)ve_ops->dram_type(e->ve_self);

    e->isp = freecodec_h264_isp_create();
    if (e->isp != NULL && e->isp_base != NULL)
        freecodec_h264_isp_set_base(e->isp, (unsigned long)(uintptr_t)e->isp_base);

    /* Defaults, spec section 8. */
    e->params.framerate = 25u;
    e->params.bitrate = 2000000u;
    e->params.max_key_interval = 25u;
    e->params.profile_level.profile = FWM_VENC_H264_PROFILE_BASELINE;
    e->params.profile_level.level = FWM_VENC_H264_LEVEL51;
    e->params.cabac_enable = 0;
    e->params.qp_min = 10;
    e->params.qp_max = 50;
    e->params.max_qp_step = 2;      /* r2/02 section 2.4 */
    e->params.vbv_size = 8u * 1024u * 1024u;
    e->params.display_offset.left = -1;     /* centred unless set */
    e->params.display_offset.top = -1;
    e->params.color_fmt = cfg->input_format;
    e->params.stride = cfg->input_stride;

    return e;
}

static int enc_init(void *h, fwm_venc_base_config_t *cfg)
{
    fc_enc_instance *e = (fc_enc_instance *)h;
    int first_time = !e->initialised;

    if (e->enc_base == NULL || e->isp_base == NULL)
        return FWM_VENC_RESULT_NO_RESOURCE;

    /* The input description (pixel format, stride, LBC ratio) arrives here:
     * the framework's open() config carries only the ops pointers. */
    e->params.color_fmt = cfg->input_format;
    e->params.stride = cfg->input_stride;
    e->params.lbc_lossy_2x = cfg->lbc_lossy_2x_en ? 1 : 0;
    e->params.lbc_lossy_2_5x = cfg->lbc_lossy_2_5x_en ? 1 : 0;
    e->params.vbv_no_cache = cfg->bitstream_uncached_en ? 1 : 0;

    compute_geometry(e, cfg);

    if (first_time) {
        if (alloc_buffers(e) != 0)
            return FWM_VENC_RESULT_NO_MEMORY;
    }

    if (!e->params.fixed_qp_enable)
        configure_rc(e);
    compute_persistent_regs(e);
    build_sps_pps(e);
    emit_sps_pps(e);

    memset(&e->pic, 0, sizeof(e->pic));
    e->pic.first_picture = 1;
    e->initialised = 1;

    return FWM_VENC_RESULT_OK;
}

static int enc_uninit(void *h)
{
    fc_enc_instance *e = (fc_enc_instance *)h;

    if (!e->initialised)
        return FWM_VENC_RESULT_OK;
    free_buffers(e);
    e->initialised = 0;
    return FWM_VENC_RESULT_OK;
}

static void enc_close(void *h)
{
    fc_enc_instance *e = (fc_enc_instance *)h;

    if (e == NULL)
        return;
    if (e->initialised)
        free_buffers(e);
    if (e->isp != NULL)
        freecodec_h264_isp_destroy(e->isp);
    free(e);
}

/* ---------------------------------------------------------------- params */

static int enc_get_parameter(void *h, int index, void *param)
{
    fc_enc_instance *e = (fc_enc_instance *)h;

    switch (index) {
    case FWM_VENC_PARAM_BITRATE:
        *(int *)param = (int)e->params.bitrate;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_FRAME_RATE:
        *(int *)param = (int)e->params.framerate;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_MAX_KEY_INTERVAL:
        *(int *)param = (int)e->params.max_key_interval;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_SIZE: {
        fwm_venc_size_t *s = (fwm_venc_size_t *)param;
        s->width = (int)e->geom.dst_w;
        s->height = (int)e->geom.dst_h;
        return FWM_VENC_RESULT_OK;
    }
    case FWM_VENC_PARAM_H264_SPS_PPS: {
        fwm_venc_header_blob_t *hd = (fwm_venc_header_blob_t *)param;
        hd->data = e->spspps;
        hd->length = e->spspps_len;
        return FWM_VENC_RESULT_OK;
    }
    case FWM_VENC_PARAM_H264_QP_RANGE: {
        fwm_venc_qp_range_t *r = (fwm_venc_qp_range_t *)param;
        r->qp_max = e->params.qp_max;
        r->qp_min = e->params.qp_min;
        return FWM_VENC_RESULT_OK;
    }
    case FWM_VENC_PARAM_H264_PROFILE_LEVEL:
        *(fwm_venc_h264_profile_level_t *)param = e->params.profile_level;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H264_CABAC:
        *(int *)param = e->params.cabac_enable;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H264_FIXED_QP: {
        fwm_venc_fixed_qp_t *f = (fwm_venc_fixed_qp_t *)param;
        f->enable = e->params.fixed_qp_enable;
        f->i_qp = e->params.fixed_i_qp;
        f->p_qp = e->params.fixed_p_qp;
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
    default:
        return FWM_VENC_RESULT_NOT_SUPPORT;
    }
}

/* Overlay (OSD), h264-overlay.md: pack the caller's blocks with the kept ISP
 * unit's packer into grow-only header/data buffers, flush them, and latch the
 * engine addresses (>> 8, the width the ISP registers take) for every later
 * picture. NORMAL and LUMA_REVERSE blocks are supported; blk_num 0 disables. Runs under
 * the shared VE lock so an in-flight encode never reads a half-packed or
 * freed buffer (the framework takes that lock around encode() only). */
/*
 * OSD luma-adaptive inversion (codec-spec/overlay-invert/spec.md).  Blocks of
 * type LUMA_REVERSE get header +8 bit 29: the hardware re-decides them every
 * frame (no hysteresis), encoder-wide mode 2 (invert where the video/overlay
 * luma difference is below the threshold) at threshold 96.  NORMAL blocks are
 * drawn as given.  The hardware needs a per-instance scratch buffer at reg
 * 0x54 whenever overlay is on (ceil(width/16) * 32 bytes, 256-byte aligned;
 * never touched by software).  FREECODEC_OVL_INVERT=0 disables bit 29 for all
 * blocks (the buffer is still set).  Called under the VE lock from
 * set_overlay, after the headers are packed.
 */
#define FC_OVL_REVERSE_LUMA   (1u << 29)
#define FC_OVL_INVERT_MODE    2
#define FC_OVL_INVERT_THRESH  96

static void ovl_invert_setup(fc_enc_instance *e,
                             const freecodec_h264_overlay_block *blocks, unsigned int n)
{
    const char *off = getenv("FREECODEC_OVL_INVERT");
    int enable = !(off && off[0] == '0');
    unsigned int i;

    if (e->ic_version <= 0x2110fu)
        return;
    if (e->ovl_inv.vir == NULL) {
        unsigned int sz = ((e->geom.in_w + 15u) / 16u) * 32u;

        sz = (sz + 255u) & ~255u;
        if (alloc_one(e, &e->ovl_inv, sz) != 0)
            return;
        memset(e->ovl_inv.vir, 0, e->ovl_inv.size);
        e->memops->flush_cache(e->ovl_inv.vir, (int)e->ovl_inv.size);
    }
    e->ovl.phy_address_overlay_invert = (unsigned int)((uintptr_t)e->ovl_inv.phy >> 8);
    e->ovl.invert_mode = enable ? FC_OVL_INVERT_MODE : 0;
    e->ovl.invert_threshold = enable ? FC_OVL_INVERT_THRESH : 0;
    if (!enable)
        return;
    for (i = 0; i < n; i++) {
        unsigned char *h = (unsigned char *)e->ovl_hdr.vir +
                           (uintptr_t)i * FREECODEC_H264_OVERLAY_HDR_STRIDE;
        if (blocks[i].reverse_luma)
            h[11] |= (unsigned char)(FC_OVL_REVERSE_LUMA >> 24);   /* +8 is little-endian */
    }
}

static int set_overlay(fc_enc_instance *e, const fwm_venc_overlay_t *oi)
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
    if (e->ovl_hdr.vir == NULL &&
        alloc_one(e, &e->ovl_hdr, FREECODEC_H264_OVERLAY_HEADER_SIZE) != 0) {
        rc = FWM_VENC_RESULT_NO_MEMORY;
        goto out;
    }
    if (e->ovl_data.size < need) {
        /* Grow only; the old buffer is released first since nothing can be
         * reading it while we hold the lock. */
        e->ovl.b_overlay = 0;
        free_one(e, &e->ovl_data);
        if (alloc_one(e, &e->ovl_data, need) != 0) {
            rc = FWM_VENC_RESULT_NO_MEMORY;
            goto out;
        }
    }
    if (freecodec_h264_isp_update_overlay(&e->ovl, blocks, n, (int)oi->argb_type,
                                          (unsigned char *)e->ovl_hdr.vir,
                                          (unsigned char *)e->ovl_data.vir,
                                          e->ovl_data.size) != 0) {
        e->ovl.b_overlay = 0;
        rc = FWM_VENC_RESULT_ILLEGAL_PARAM;
        goto out;
    }
    ovl_invert_setup(e, blocks, n);
    e->memops->flush_cache(e->ovl_hdr.vir, (int)e->ovl_hdr.size);
    e->memops->flush_cache(e->ovl_data.vir, (int)e->ovl_data.size);
    e->ovl.phy_address_overlay_header = (unsigned int)((uintptr_t)e->ovl_hdr.phy >> 8);
    e->ovl.phy_address_overlay_data = (unsigned int)((uintptr_t)e->ovl_data.phy >> 8);

out:
    if (e->ve_ops->unlock)
        e->ve_ops->unlock(e->ve_self);
    return rc;
}

/* Bit rate / frame rate changed after init: the consumer changes them live
 * (no re-init), so rebuild rate control now. Under the shared VE lock: the
 * encode thread uses the RC state while the caller's control thread lands
 * here. */
static void reconfigure_rc_live(fc_enc_instance *e)
{
    if (!e->initialised || e->params.fixed_qp_enable)
        return;
    if (e->ve_ops->lock)
        e->ve_ops->lock(e->ve_self);
    configure_rc(e);
    if (e->ve_ops->unlock)
        e->ve_ops->unlock(e->ve_self);
}

static int enc_set_parameter(void *h, int index, void *param)
{
    fc_enc_instance *e = (fc_enc_instance *)h;

    switch (index) {
    case FWM_VENC_PARAM_BITRATE:
        e->params.bitrate = (unsigned int)*(int *)param;
        reconfigure_rc_live(e);
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_FRAME_RATE:
        e->params.framerate = (unsigned int)*(int *)param;
        reconfigure_rc_live(e);
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_FAST_ENCODE:
        e->params.fast_enc = *(int *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_FILTER_3D: {
        unsigned int lvl = *(unsigned char *)param;

        e->params.filter_3d_level = lvl > 3u ? 3u : lvl;
        return FWM_VENC_RESULT_OK;
    }
    case FWM_VENC_PARAM_MAX_KEY_INTERVAL:
        e->params.max_key_interval = (unsigned int)*(int *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_FORCE_KEY_FRAME:
        e->params.force_key = 1;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_ROTATION:
        e->params.rotation = *(int *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_HORIZONTAL_FLIP:
        e->params.hflip = *(int *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_SLICE_HEIGHT:
        e->params.slice_height = (unsigned int)*(int *)param;
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
    case FWM_VENC_PARAM_P_SKIP:
        e->params.p_skip_factor = (unsigned int)*(int *)param;   /* accepted, unused */
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H264_QP_RANGE: {
        fwm_venc_qp_range_t *r = (fwm_venc_qp_range_t *)param;
        e->params.qp_max = r->qp_max;
        e->params.qp_min = r->qp_min;
        return FWM_VENC_RESULT_OK;
    }
    case FWM_VENC_PARAM_H264_PROFILE_LEVEL:
        e->params.profile_level = *(fwm_venc_h264_profile_level_t *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H264_CABAC:
        e->params.cabac_enable = *(int *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H264_FIXED_QP: {
        fwm_venc_fixed_qp_t *f = (fwm_venc_fixed_qp_t *)param;
        e->params.fixed_qp_enable = f->enable;
        e->params.fixed_i_qp = f->i_qp;
        e->params.fixed_p_qp = f->p_qp;
        return FWM_VENC_RESULT_OK;
    }
    case FWM_VENC_PARAM_CHROMA_GRAY:
        e->params.chroma_gray = *(int *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_I_QP_OFFSET:
        e->params.i_qp_offset = *(int *)param;   /* stored; see spec 12 sec 16 item 7 */
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_CBR_FILLING:
        e->params.cbr_filling = *(int *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_ROI_CONFIG: {
        fwm_venc_roi_t *r = (fwm_venc_roi_t *)param;
        if (r->index < 0 || r->index >= 8)
            return FWM_VENC_RESULT_ILLEGAL_PARAM;
        e->params.roi[r->index] = *r;
        return FWM_VENC_RESULT_OK;
    }
    case FWM_VENC_PARAM_ROI:
        e->params.roi_enable = *(int *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_OVERLAY:
        return set_overlay(e, (const fwm_venc_overlay_t *)param);
    case 0x7f000001: /* FWM_VENC_PARAM_DISPLAY_SIZE */
        e->params.display_size = *(fwm_venc_display_size_t *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_DISPLAY_OFFSET:
        e->params.display_offset = *(fwm_venc_display_offset_t *)param;
        return FWM_VENC_RESULT_OK;
    case FWM_VENC_PARAM_H264_CONFIG: {
        fwm_venc_h264_config_t *p = (fwm_venc_h264_config_t *)param;

        e->params.profile_level = p->profile_level;
        e->params.cabac_enable = p->cabac_en;
        e->params.qp_max = p->qp_range.qp_max;
        e->params.qp_min = p->qp_range.qp_min;
        e->params.framerate = (unsigned int)p->frame_rate;
        e->params.bitrate = (unsigned int)p->bitrate;
        e->params.max_key_interval = (unsigned int)p->key_interval_max;
        e->params.fixed_qp_enable = p->rate_control.fixed_qp.enable;
        e->params.fixed_i_qp = p->rate_control.fixed_qp.i_qp;
        e->params.fixed_p_qp = p->rate_control.fixed_qp.p_qp;
        return FWM_VENC_RESULT_OK;
    }
    default:
        return FWM_VENC_RESULT_NOT_SUPPORT;
    }
}

/* --------------------------------------------------------------- ring API */

static int enc_valid_count(void *h)
{
    fc_enc_instance *e = (fc_enc_instance *)h;

    return e->bufs.bs ? BitStreamFrameNum(e->bufs.bs) : 0;
}

static int enc_get_frame(void *h, fwm_venc_output_frame_t *out)
{
    fc_enc_instance *e = (fc_enc_instance *)h;
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
    out->id = info->nID;
    return FWM_VENC_RESULT_OK;
}

static int enc_free_frame(void *h, fwm_venc_output_frame_t *out)
{
    fc_enc_instance *e = (fc_enc_instance *)h;
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
    fc_enc_instance *e = (fc_enc_instance *)h;

    if (e->bufs.bs == NULL)
        return FWM_VENC_RESULT_ERROR;
    return (BitStreamReset(e->bufs.bs, e->memops) == 0) ? FWM_VENC_RESULT_OK : FWM_VENC_RESULT_ERROR;
}

/* ---------------------------------------------------------------- tables */

fwm_venc_device_t video_encoder_h264_ver2 = {
    "video_encoder_h264_ver2",
    enc_open, enc_init, enc_uninit, enc_close, enc_encode,
    enc_get_parameter, enc_set_parameter,
    enc_valid_count, enc_get_frame, enc_free_frame, enc_reset_frames
};

/* Section 12: ver1 is aliased to the same, exhaustively-verified ver2 table
 * -- V833 (the only IC this product ships on) always selects ver2 regardless
 * of which symbol a caller probes, so ver1's own behaviour is unreachable on
 * shipping hardware; aliasing exposes a working encoder rather than an
 * outright failure under that symbol. See spec section 12 for the owner
 * decision point if a future IC below the ver2 threshold is ever targeted. */
fwm_venc_device_t video_encoder_h264_ver1 = {
    "video_encoder_h264_ver1",
    enc_open, enc_init, enc_uninit, enc_close, enc_encode,
    enc_get_parameter, enc_set_parameter,
    enc_valid_count, enc_get_frame, enc_free_frame, enc_reset_frames
};

/* Section 1: video_encoder_h265 and video_encoder_jpeg are tables whose
 * create returns NULL and whose other entries return the error result. */
static void *stub_open(fwm_venc_base_config_t *cfg, unsigned int ic)
{
    (void)cfg; (void)ic;
    return NULL;
}
static int stub_init(void *h, fwm_venc_base_config_t *c) { (void)h; (void)c; return FWM_VENC_RESULT_NOT_SUPPORT; }
static int stub_uninit(void *h) { (void)h; return FWM_VENC_RESULT_NOT_SUPPORT; }
static void stub_close(void *h) { (void)h; }
static int stub_encode(void *h, fwm_venc_input_picture_t *b) { (void)h; (void)b; return FWM_VENC_RESULT_NOT_SUPPORT; }
static int stub_get_param(void *h, int i, void *p) { (void)h; (void)i; (void)p; return FWM_VENC_RESULT_NOT_SUPPORT; }
static int stub_set_param(void *h, int i, void *p) { (void)h; (void)i; (void)p; return FWM_VENC_RESULT_NOT_SUPPORT; }
static int stub_valid_count(void *h) { (void)h; return FWM_VENC_RESULT_NOT_SUPPORT; }
static int stub_get_frame(void *h, fwm_venc_output_frame_t *b) { (void)h; (void)b; return FWM_VENC_RESULT_NOT_SUPPORT; }
static int stub_free_frame(void *h, fwm_venc_output_frame_t *b) { (void)h; (void)b; return FWM_VENC_RESULT_NOT_SUPPORT; }
static int stub_reset_frames(void *h) { (void)h; return FWM_VENC_RESULT_NOT_SUPPORT; }

fwm_venc_device_t video_encoder_h265 = {
    "video_encoder_h265",
    stub_open, stub_init, stub_uninit, stub_close, stub_encode,
    stub_get_param, stub_set_param,
    stub_valid_count, stub_get_frame, stub_free_frame, stub_reset_frames
};

fwm_venc_device_t video_encoder_jpeg = {
    "video_encoder_jpeg",
    stub_open, stub_init, stub_uninit, stub_close, stub_encode,
    stub_get_param, stub_set_param,
    stub_valid_count, stub_get_frame, stub_free_frame, stub_reset_frames
};
