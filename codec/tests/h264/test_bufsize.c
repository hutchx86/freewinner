/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Compressed auxiliary reference-plane size (spec r2/03 section 5), driven by
 * data/subpic_size.csv: the size helper returns plane_bytes for every picture
 * size, and its column-group count agrees with the stride-group field the
 * register-shadow unit writes into register 0x08 (bits 10-13). */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freecodec/h264_bufs.h"
#include "freecodec/h264_regs.h"

#ifndef BUFSIZE_VECTORS
#define BUFSIZE_VECTORS "tests/h264/data/subpic_size.csv"
#endif

static int g_fail;

static unsigned int shadow_stride_groups(unsigned int width_mb,
                                         unsigned int height16)
{
    freecodec_h264_reg_info_cfg cfg;
    freecodec_h264_reg_info_out out;

    memset(&cfg, 0, sizeof(cfg));
    memset(&out, 0, sizeof(out));
    cfg.profile_idc            = 66;
    cfg.ic_version             = 0x21110u;
    cfg.slice_qp               = 26;
    cfg.dst_width_mb           = width_mb;
    cfg.dst_height_mb          = height16 / 16u;
    cfg.display_height_16align = height16;
    freecodec_h264_init_reg_info(&cfg, &out);
    return (out.shadow.slice_stride_control >> 10) & 0xfu;
}

int main(void)
{
    char line[256];
    FILE *f = fopen(BUFSIZE_VECTORS, "r");
    int rows = 0;

    if (f == NULL) {
        fprintf(stderr, "FAIL: cannot open %s\n", BUFSIZE_VECTORS);
        return 1;
    }
    if (fgets(line, sizeof(line), f) == NULL) {   /* header */
        fprintf(stderr, "FAIL: empty vector file\n");
        fclose(f);
        return 1;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        unsigned int w, h, wmb, h16, g, n, bytes, got, shadow_g;

        if (sscanf(line, "%u,%u,%u,%u,%u,%u,%u",
                   &w, &h, &wmb, &h16, &g, &n, &bytes) != 7)
            continue;
        rows++;

        got = fc_aux_plane_bytes(w, h);
        if (got != bytes) {
            fprintf(stderr, "FAIL %ux%u: plane %u bytes, expected %u\n",
                    w, h, got, bytes);
            g_fail = 1;
        }
        if (fc_aux_plane_groups(wmb) != g) {
            fprintf(stderr, "FAIL %ux%u: helper groups %u, expected %u\n",
                    w, h, fc_aux_plane_groups(wmb), g);
            g_fail = 1;
        }
        shadow_g = shadow_stride_groups(wmb, h16);
        if (shadow_g != g) {
            fprintf(stderr, "FAIL %ux%u: 0x08 stride groups %u, expected %u\n",
                    w, h, shadow_g, g);
            g_fail = 1;
        }
        if (fc_mbrc_buffer_bytes(h16 / 16u) != 512u + 12u * (h16 / 16u - 1u)) {
            fprintf(stderr, "FAIL %ux%u: MB-RC buffer size\n", w, h);
            g_fail = 1;
        }
    }
    fclose(f);

    if (rows != 19) {
        fprintf(stderr, "FAIL: expected 19 vector rows, read %d\n", rows);
        g_fail = 1;
    }
    if (g_fail)
        return 1;
    printf("bufsize: %d picture sizes (plane bytes, group count = 0x08 field) ok\n",
           rows);
    return 0;
}
