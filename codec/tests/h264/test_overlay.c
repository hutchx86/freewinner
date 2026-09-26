/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Unit tests for the overlay (OSD) packer (spec/h264-overlay.md). No golden: sizing
 * and the disable-on-zero rule are computed from spec §2/§3; §4 layout stays provisional. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freecodec/h264_isp.h"

static int g_fails;

#define CHECK(cond, ...) do { \
        if (!(cond)) { \
            printf("test_overlay: FAIL at %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__); \
            printf("\n"); \
            g_fails++; \
        } \
    } while (0)

/* --------------------------------------------------- buffer-sizing math -- */

static void test_data_size_single_block(void)
{
    freecodec_h264_overlay_block b;
    unsigned int size;

    memset(&b, 0, sizeof(b));
    b.bitmap_size = 100u;   /* rounds up to one 256-byte slot */

    size = freecodec_h264_overlay_data_size(&b, 1u);
    CHECK(size == 256u + FREECODEC_H264_OVERLAY_DATA_SLACK,
          "single 100-byte block: got %u", size);
}

static void test_data_size_exact_multiple(void)
{
    freecodec_h264_overlay_block b;
    unsigned int size;

    memset(&b, 0, sizeof(b));
    b.bitmap_size = 512u;   /* already a 256-byte multiple: no extra padding */

    size = freecodec_h264_overlay_data_size(&b, 1u);
    CHECK(size == 512u + FREECODEC_H264_OVERLAY_DATA_SLACK,
          "exact-multiple 512-byte block: got %u", size);
}

static void test_data_size_multiple_blocks_each_padded(void)
{
    freecodec_h264_overlay_block blocks[3];
    unsigned int size;
    unsigned int expect = 0u;

    memset(blocks, 0, sizeof(blocks));
    blocks[0].bitmap_size = 1u;      /* -> 256 */
    blocks[1].bitmap_size = 257u;    /* -> 512 */
    blocks[2].bitmap_size = 256u;    /* -> 256 (already aligned) */
    expect = 256u + 512u + 256u + FREECODEC_H264_OVERLAY_DATA_SLACK;

    size = freecodec_h264_overlay_data_size(blocks, 3u);
    CHECK(size == expect, "three blocks padded independently: got %u want %u",
          size, expect);
}

static void test_data_size_empty(void)
{
    unsigned int size = freecodec_h264_overlay_data_size(NULL, 0u);

    CHECK(size == FREECODEC_H264_OVERLAY_DATA_SLACK,
          "blk_num==0 sizes to just the slack: got %u", size);
}

static void test_data_size_over_ceiling(void)
{
    unsigned int size = freecodec_h264_overlay_data_size(
        NULL, FREECODEC_H264_OVERLAY_MAX_BLOCKS + 1u);

    CHECK(size == 0u, "over-ceiling blk_num sizes to 0 (reject): got %u", size);
}

static void test_header_size_and_stride(void)
{
    /* Spec sec. 2: the header buffer is a fixed 1024 bytes, sized for the
     * 64-block ceiling; 1024/64 = 16 bytes/entry is the working stride. */
    CHECK(FREECODEC_H264_OVERLAY_HEADER_SIZE ==
          FREECODEC_H264_OVERLAY_MAX_BLOCKS * FREECODEC_H264_OVERLAY_HDR_STRIDE,
          "1024 must equal 64 * 16: header=%u max=%u stride=%u",
          FREECODEC_H264_OVERLAY_HEADER_SIZE,
          FREECODEC_H264_OVERLAY_MAX_BLOCKS, FREECODEC_H264_OVERLAY_HDR_STRIDE);
}

/* --------------------------------------------------------- packing/pack -- */

static void test_disable_on_zero_blocks(void)
{
    freecodec_h264_isp_info info;
    int rc;

    memset(&info, 0, sizeof(info));
    info.b_overlay = 1;
    info.n_overlay_num = 3;

    rc = freecodec_h264_isp_update_overlay(&info, NULL, 0u, 2, NULL, NULL, 0u);
    CHECK(rc == 0, "blk_num==0 must succeed, not error: rc=%d", rc);
    CHECK(info.b_overlay == 0, "blk_num==0 must clear b_overlay: got %d",
          info.b_overlay);
    CHECK(info.n_overlay_num == 0, "blk_num==0 must clear n_overlay_num: got %u",
          info.n_overlay_num);
    CHECK(info.e_overlay_argb_type == 2,
          "blk_num==0 still latches the caller's argb_type: got %d",
          info.e_overlay_argb_type);
}

static void test_disable_on_null_block_list(void)
{
    freecodec_h264_isp_info info;
    int rc;

    memset(&info, 0, sizeof(info));
    info.b_overlay = 1;

    rc = freecodec_h264_isp_update_overlay(&info, NULL, 5u, 0, NULL, NULL, 0u);
    CHECK(rc == 0, "NULL block list must succeed, not error: rc=%d", rc);
    CHECK(info.b_overlay == 0, "NULL block list must clear b_overlay: got %d",
          info.b_overlay);
}

static void test_reject_over_ceiling(void)
{
    freecodec_h264_isp_info info;
    int rc;

    memset(&info, 0, sizeof(info));
    rc = freecodec_h264_isp_update_overlay(
        &info, (const freecodec_h264_overlay_block *)1 /* never dereferenced */,
        FREECODEC_H264_OVERLAY_MAX_BLOCKS + 1u, 0, NULL, NULL, 0u);
    CHECK(rc != 0, "blk_num over the 64-block ceiling must be rejected");
    CHECK(info.b_overlay == 0, "a rejected call must not enable the overlay");
}

static void test_pack_one_block(void)
{
    unsigned char pixel[4] = { 0xde, 0xad, 0xbe, 0xef };
    freecodec_h264_overlay_block b;
    unsigned char header[FREECODEC_H264_OVERLAY_HEADER_SIZE];
    unsigned char data[FREECODEC_H264_OVERLAY_BLOCK_ALIGN +
                       FREECODEC_H264_OVERLAY_DATA_SLACK];
    freecodec_h264_isp_info info;
    int rc;

    memset(&b, 0, sizeof(b));
    b.start_mb_x = 1; b.end_mb_x = 3; b.start_mb_y = 2; b.end_mb_y = 5;
    b.bitmap_vir = pixel;
    b.bitmap_size = sizeof(pixel);

    memset(header, 0xaa, sizeof(header));
    memset(data, 0xaa, sizeof(data));
    memset(&info, 0, sizeof(info));

    rc = freecodec_h264_isp_update_overlay(&info, &b, 1u, 1, header, data,
                                           sizeof(data));
    CHECK(rc == 0, "single-block pack must succeed: rc=%d", rc);
    CHECK(info.b_overlay == 1, "a successful pack sets b_overlay: got %d",
          info.b_overlay);
    CHECK(info.n_overlay_num == 1, "n_overlay_num must be 1: got %u",
          info.n_overlay_num);
    CHECK(info.e_overlay_argb_type == 1, "argb_type must be latched: got %d",
          info.e_overlay_argb_type);
    /* n_blk_len is the total padded data used: one 4-byte bitmap rounds up to
     * one block-alignment unit. */
    CHECK(info.n_blk_len == (int)FREECODEC_H264_OVERLAY_BLOCK_ALIGN,
          "n_blk_len must be the padded data size: got %d", info.n_blk_len);
    CHECK(memcmp(data, pixel, sizeof(pixel)) == 0,
          "bitmap bytes must land at data-buffer offset 0");
}

static void test_pack_rejects_undersized_data_buffer(void)
{
    unsigned char pixel[300];
    freecodec_h264_overlay_block b;
    unsigned char header[FREECODEC_H264_OVERLAY_HEADER_SIZE];
    unsigned char data[16]; /* far too small for a 300-byte bitmap */
    freecodec_h264_isp_info info;
    int rc;

    memset(pixel, 0x5a, sizeof(pixel));
    memset(&b, 0, sizeof(b));
    b.bitmap_vir = pixel;
    b.bitmap_size = sizeof(pixel);

    memset(header, 0xaa, sizeof(header));
    memset(data, 0xaa, sizeof(data));
    memset(&info, 0, sizeof(info));

    rc = freecodec_h264_isp_update_overlay(&info, &b, 1u, 0, header, data,
                                           sizeof(data));
    CHECK(rc != 0, "an undersized data buffer must be rejected");
    CHECK(info.b_overlay == 0, "a rejected pack must not enable the overlay");
    CHECK(data[0] == 0xaa,
          "no partial write: the data buffer must be untouched on reject");
}

int main(void)
{
    test_data_size_single_block();
    test_data_size_exact_multiple();
    test_data_size_multiple_blocks_each_padded();
    test_data_size_empty();
    test_data_size_over_ceiling();
    test_header_size_and_stride();

    test_disable_on_zero_blocks();
    test_disable_on_null_block_list();
    test_reject_over_ceiling();
    test_pack_one_block();
    test_pack_rejects_undersized_data_buffer();

    if (g_fails) {
        printf("test_overlay: %d check(s) FAILED\n", g_fails);
        return 1;
    }
    printf("test_overlay: all checks passed\n");
    return 0;
}
