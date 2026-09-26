/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * test_module_cfg_shim.c - host self-test for the isp_module_cfg SDK-ABI shim.
 *
 * Drives the SDK entry points with a `struct isp_module_config` and checks that
 * the translation layer carries the inputs into the clean core and the clean
 * outputs back out:
 *   - linear: the deployed direction is linear_table -> fe_table, so the shim
 *     must fill the fe_table target with the linear_table contents (this is the
 *     bug the SDK differential exists to catch);
 *   - the embedded -> pointer target copies (cem/drc/pltm/wdr/gamma/saturation);
 *   - the d3d embedded outputs (tdnf_th -> tdnf_lum_th / tdnf_bri_th);
 *   - register programming through isp_map_addr + isp_hardware_update.
 *
 * The host ABI check in the shim is skipped (host pointers are 64-bit); the ARM
 * differential is the authoritative ABI test.
 */
#include <stdio.h>
#include <string.h>

#include "fwi_isp_api.h"
#include "freeisp/isp_dims.h"
#include "module_cfg_shim.h"

/* musl's static libm objects carry .ARM.exidx referencing the C++ unwinder;
 * the program never throws, so satisfy the linker as the other ARM harnesses
 * do. */
__attribute__((weak)) int __aeabi_unwind_cpp_pr0(void) { return 0; }
__attribute__((weak)) int __aeabi_unwind_cpp_pr1(void) { return 0; }
__attribute__((weak)) int __aeabi_unwind_cpp_pr2(void) { return 0; }

#define REG_BYTES 0x1000u
#define OUTSENT   0x5a

static uint32_t regmem[REG_BYTES / 4];

static uint8_t  lin_src[0x600],  fe_dst[0x600];
static uint8_t  cem_dst[0x1700];
static uint8_t  drc_dst[0x200];
static uint8_t  pltm_dst[0x600];
static uint8_t  wdr_dst[0x4000];
static uint8_t  gamma_dst[0x1000];
static uint8_t  sat_dst[0x200];
static uint16_t msc_tbl[0x40];

static int fails;

#define CHECK(cond, msg)                                            \
    do {                                                            \
        if (!(cond)) { printf("FAIL %s\n", msg); fails++; }         \
    } while (0)

static void fill_u8(uint8_t *p, size_t n, uint8_t v)
{
    memset(p, v, n);
}

int main(void)
{
    fwi_hw_module_cfg_t cfg;
    unsigned i;
    int any_reg = 0;

    memset(&cfg, 0, sizeof(cfg));
    cfg.dev_id = 0;
    cfg.module_enable_flag = 0xffffffffu;   /* run every prepare + enable */

    /* saturation must sum to 0x10 for the writer to act */
    cfg.saturation_cfg.saturation_r = 4;
    cfg.saturation_cfg.saturation_g = 6;
    cfg.saturation_cfg.saturation_b = 6;

    /* fill embedded sources with distinct patterns */
    for (i = 0; i < sizeof(cfg.colour_enhance_cfg.colour_enhance_table); i++)
        cfg.colour_enhance_cfg.colour_enhance_table[i] = (uint8_t)(i * 3u + 1u);
    for (i = 0; i < sizeof(cfg.drc_cfg.drc_table) / sizeof(cfg.drc_cfg.drc_table[0]); i++)
        cfg.drc_cfg.drc_table[i] = (uint16_t)(0x1000u + i);
    for (i = 0; i < sizeof(cfg.pltm_cfg.pltm_table); i++)
        cfg.pltm_cfg.pltm_table[i] = (uint8_t)(i ^ 0x5u);
    for (i = 0; i < sizeof(cfg.wdr_cfg.wdr_table); i++)
        cfg.wdr_cfg.wdr_table[i] = (uint8_t)(i + 7u);
    for (i = 0; i < sizeof(cfg.lens_cfg.lens_r_table) / sizeof(cfg.lens_cfg.lens_r_table[0]); i++)
        cfg.lens_cfg.lens_r_table[i] = (uint16_t)(0x200u + i);
    cfg.gamma_cfg.gamma_tbl[0xbff] = 0x7ff;   /* select the normal gamma pack */
    for (i = 0; i < 0x100; i++) {
        cfg.gamma_cfg.gamma_tbl[i]         = (uint16_t)(i * 4u);
        cfg.gamma_cfg.gamma_tbl[1024 + i]  = (uint16_t)(i * 4u + 1u);
        cfg.gamma_cfg.gamma_tbl[2048 + i]  = (uint16_t)(i * 4u + 2u);
    }
    for (i = 0; i < sizeof(cfg.denoise_3d_cfg.temporal_denoise_threshold) / sizeof(cfg.denoise_3d_cfg.temporal_denoise_threshold[0]); i++)
        cfg.denoise_3d_cfg.temporal_denoise_threshold[i] = (uint16_t)(0x30u + i);
    for (i = 0; i < sizeof(cfg.saturation_cfg.saturation_table) / sizeof(cfg.saturation_cfg.saturation_table[0]); i++)
        cfg.saturation_cfg.saturation_table[i] = (int16_t)(0x100 + i);

    /* input pointer targets */
    for (i = 0; i < sizeof(lin_src); i++)
        lin_src[i] = (uint8_t)(i * 9u + 5u);

    /* output pointer targets, sentineled */
    fill_u8(fe_dst, sizeof(fe_dst), OUTSENT);
    fill_u8(cem_dst, sizeof(cem_dst), OUTSENT);
    fill_u8(drc_dst, sizeof(drc_dst), OUTSENT);
    fill_u8(pltm_dst, sizeof(pltm_dst), OUTSENT);
    fill_u8(wdr_dst, sizeof(wdr_dst), OUTSENT);
    fill_u8(gamma_dst, sizeof(gamma_dst), OUTSENT);
    fill_u8(sat_dst, sizeof(sat_dst), OUTSENT);

    cfg.linearize_table    = lin_src;
    cfg.fe_table        = fe_dst;
    cfg.colour_enhance_table       = cem_dst;
    cfg.drc_table       = drc_dst;
    cfg.pltm_table      = pltm_dst;
    cfg.wdr_table       = wdr_dst;
    cfg.gamma_table     = gamma_dst;
    cfg.saturation_table = sat_dst;
    cfg.mesh_shading_table       = msc_tbl;

    /* exercised through the real SDK entry points */
    isp_map_addr(&cfg, (unsigned long)regmem);
    isp_hardware_update(&cfg);

    CHECK(cfg.table_update == 0, "table_update reset after the forced load");

    CHECK(memcmp(fe_dst, lin_src, sizeof(lin_src)) == 0,
          "linear: fe_table target got the linear_table contents");
    CHECK(memcmp(cem_dst, cfg.colour_enhance_cfg.colour_enhance_table, sizeof(cem_dst)) == 0,
          "cem_table target got the embedded cem table");
    CHECK(memcmp(drc_dst, cfg.drc_cfg.drc_table, 0x200) == 0,
          "drc_table target got the embedded drc table");
    CHECK(memcmp(pltm_dst, cfg.pltm_cfg.pltm_table, 0x600) == 0,
          "pltm_table target got the embedded pltm table");
    CHECK(memcmp(wdr_dst, cfg.wdr_cfg.wdr_table, 0x4000) == 0,
          "wdr_table target got the embedded wdr table");
    CHECK(memcmp(sat_dst, cfg.saturation_cfg.saturation_table, 0x200) == 0,
          "saturation_table target got the embedded saturation table");
    CHECK(memcmp(gamma_dst, (uint8_t[0x1000]){0}, 0x1000) != 0,
          "gamma_table target was written");

    CHECK(memcmp(cfg.denoise_3d_cfg.temporal_denoise_luminance_threshold, cfg.denoise_3d_cfg.temporal_denoise_threshold,
                 sizeof(cfg.denoise_3d_cfg.temporal_denoise_luminance_threshold)) == 0,
          "tdnf_lum_th got tdnf_th");
    CHECK(memcmp(cfg.denoise_3d_cfg.temporal_denoise_bri_threshold, cfg.denoise_3d_cfg.temporal_denoise_threshold,
                 sizeof(cfg.denoise_3d_cfg.temporal_denoise_bri_threshold)) == 0,
          "tdnf_bri_th got tdnf_th");

    for (i = 0; i < REG_BYTES / 4; i++)
        if (regmem[i] != 0)
            any_reg = 1;
    CHECK(any_reg, "registers were programmed through isp_map_addr");

    if (fails == 0)
        printf("module_cfg_shim self-test: PASS (all checks)\n");
    else
        printf("module_cfg_shim self-test: FAIL (%d checks)\n", fails);
    return fails ? 1 : 0;
}
