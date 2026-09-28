<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Clean-room spec — register-tier translation units (reglayer2)

Behaviour-only, implementation-ready specification of the three register-tier
translation units:

| TU | object | what it is |
|----|--------|-----------|
| `reg_writers.c` | `isp521_reg_cfg.o` | 69 register writers (RMW / LUT copies into the ISP register file) |
| `module_cfg.c` | `isp_module_cfg.o` | per-frame dispatch + 31 prepare routines + 29 enable routines + address binding |
| `base.c` | `isp_base.o` | statistics parsing, table/curve/window generation, 12 public entry points |

This is the **spec-team** output for a clean-room re-production. It describes what
the deployment *does*, not how the vendor source is written. Interface identifiers
(entry-point names, descriptor member names, register offsets, feature/change bits)
are reproduced because they are interop facts. Vendor-internal helper symbol
names are deliberately omitted; helpers are described by the behaviour they
perform. No vendor source, comments, log
strings or structural transcription are included. See `../docs/provenance.md`
for the full provenance picture.

Authority for each statement: the differential-verified behaviour of the three
objects under qemu-arm (the private differential harness) plus the
objects' debug type information. Where a statement is *not* covered by the
differential (a differential-blind path), it is marked **[blind]** in the traps
subsection.

How to read the formulas: all C integer arithmetic is 32-bit two's-complement
unless stated; `>>` on a signed quantity is arithmetic right shift; integer
division truncates toward zero; float/double operations are IEEE-754 and are
named wherever they matter. "store(fields,val)" and "put(mask,shift,val)" are the
two register write primitives defined in §1.1.

---

## 0. Shared model

### 0.1 Register descriptor

Each ISP instance `id` (0 or 1) owns one descriptor `g_isp_reg[id]`. A descriptor
is a flat array of `uint32_t*` members, one per named register field; each member
points at the memory-mapped 32-bit register at `instance_base + hardware_offset`
(§1.1 lists every member and offset). Every writer:

1. looks up the descriptor with `instance(id)` — returns non-NULL only for
   `id < 2`;
2. if the descriptor (or a required source/cfg pointer) is NULL, returns without
   writing anything;
3. otherwise read-modify-writes the 32-bit word(s).

The absolute base is a runtime fact supplied once to `isp_reg_map_load_addr`
(§2.1 of `module_cfg`). Before that call every member is NULL and every writer is
a no-op. Two ISP instances are supported; all offsets below are relative to one
instance base.

### 0.2 Register write primitives

```
put(reg, mask, shift, val):  *reg = (*reg & ~(mask << shift)) | ((val & mask) << shift)
store(reg, fields, val):     *reg = (*reg & ~fields) | (val & fields)
```

`put` writes one bitfield (`mask` is the *unshifted* width mask). `store` writes a
union of whole-word bitfields: `fields` is the OR of their shifted masks, so bits
outside `fields` survive. Every literal field mask in §1 is written exactly as the
`fields`/`mask` argument, in hex, so an implementer can copy it.

### 0.3 Table-injection contract (`base_tables_t`, via `freeisp_get_tables()`)

`base.c` compiles in no tuning data. It reads named pointers from
`const freeisp_tables_t *freeisp_get_tables()` → `t->base`:

| field | type / length | semantics |
|-------|---------------|-----------|
| `gamma_base` | `const uint16_t[3072]` | base gamma LUT, 3 planes × 1024 |
| `gamma_sub[4]` | `const uint16_t[3072]` each | per-temperature gamma LUTs (indices 1..4) |
| `gamma_trig` | `const int32_t[5]` | gamma LV trigger points (descending) |
| `lsc[12]` | `const uint16_t[768]` each | lens-shading rows: for each temp block a pair `lsc[j]`, `lsc[j+1]`, then a second pair at `+6,+7` (VCM rows) |
| `lsc_trig` | `const uint16_t[6]` | lens temperature triggers |
| `msc[12]` | `const uint16_t[1452]` each | MSC rows, 3 components × 484, +6 VCM offset |
| `msc_trig` | `const uint16_t[6]` | MSC temperature triggers |
| `linear` | `const uint16_t[768]` | front-end linear table (0x600 bytes) |
| `wdr_table` | `const uint16_t[8192]` | WDR tuning table (0x4000 bytes) |
| `wdr_front` | `const uint16_t[4096]` | WDR front-end table (unused by these 12 entries) |
| `anti_gamma` | `const uint16_t[4096]` | 12-bit anti-gamma LUT |
| `rgb2yuv_base[6]` | `const int16_t[12]` each | RGB2YUV source: 9 Q10 matrix + 3 Q9 offsets |
| `color_matrix` | `const uint16_t[cm_break_num*12]` | CM blocks: 9 matrix + 3 offset entries per block |
| `color_temp` | `const int32_t[cm_break_num]` | CM colour-temperature breakpoints |

Missing pointers (`t == NULL`, `t->field == NULL`) are always checked; the
behaviour is stated per entry point.

The one non-tabulated injected table is `comp_ref`: a 256-entry `double` radial
reference used by the MSC builders, provided by `freeisp_comp_ref_fill(double[256])`
(the deployment keeps it as a read-only data table; see `src/reg/comp_ref.c`).

### 0.4 Context / module-config fields referenced by name

`base.c` operates on `fwi_base_ctx_t` (see `include/base.h`).
`module_cfg.c` operates on `fwi_mod_config_t` (see
`include/module_cfg.h`); the SDK-ABI adapter that maps the framework's real
`struct fwi_hw_module_cfg` (vendor: `isp_module_config`, 37040 B) onto that clean type is
`shim/module_cfg/module_cfg_shim.c` (see `shim/INTEGRATION.md` §A9). `reg_writers.c` operates on
`fwi_reg_map_t` (`include/reg_writers.h`). An implementer must use those
headers unchanged: the spec below names fields by their header names.

`comp_ref` is only available through `freeisp_comp_ref_fill`; `isp_ae_set_params_helper`
is provided by the framework (`cmd 1` = refresh AE table, `cmd 3` = rebuild touch
weighting) and has no register effect in this tier.

---

## 1. `reg_writers.c` — 69 register writers

All signatures are `void f(unsigned long id, ...)`. "target" gives the descriptor
member name and its hardware offset; the offset is an interface fact.

### 1.1 Address map / control (6)
| # | entry point | target | behaviour |
|---|-------------|--------|-----------|
| 1 | `isp_reg_map_load_addr(id, base)` | all | for `id<2` and non-NULL `base`, set every member `m->field = base+off`. `NULL` id/base → no-op. |
| 2 | `isp_reg_set_input_fmt(id, fmt)` | `isp_global_cfg0` +0x000 | `put(0x7, 4, fmt)`: byte0 bits[6:4] = `fmt & 7`. |
| 3 | `isp_reg_update_table(id, mask)` | `isp_update_ctrl0` +0x020 | RMW (not a single `put`): byte0[7:1] ← `(mask>>1)&0x7f`; byte1[3:0] ← `(mask>>8)&0xf`; word bit13 (byte1 bit5) ← `(mask>>1)&1`. Bits outside these survive. `mask` is taken as 32-bit. |
| 4 | `isp_reg_top_control(id, n, w)` | `isp_top_ctrl` +0x0fc | `put(0x3fff,0,w)` then `put(0x1,16,(n-1)&1)`: [13:0]=`w&0x3fff`, bit16=`(n-1)&1`. |
| 5 | `isp_reg_module_enable(id, flag)` | `isp_module_bypass0` +0x1a0 (+ side effects) | see §1.1a. |
| 6 | `isp_reg_module_disable(id, flag)` | `isp_module_bypass0` +0x1a0 (+ side effects) | see §1.1a. |

**1.1a module enable/disable side effects.** `*isp_module_bypass0 |= flag`
(enable) or `&= ~flag` (disable). Then, tested on `flag` (all applicable):
`flag & 0x40000` (BLC) → set/clear `isp_s1_cfg` bit0; `flag & 0x2` (LINEAR) →
bit1; `flag & 0x4000000` (MODE) → bit2; `flag & 0x4` (WDR) → set/clear
`isp_global_cfg1` bit14 (byte1 bit6). Flag bits are the feature bits of §2.4.

**1.1b digital-gain routing (used only by `isp_reg_enable_digital_gain`).** There is no
vendor writer symbol for this; the enable routine's side effect is equivalent to
the helper `isp_reg_set_dg_bypass(id, en, dg_mode)`:

```
*isp_module_bypass0 &= ~(0x80000 | 0x4000000);   /* DG and MODE */
*isp_s1_cfg        &= ~(1u<<2);
if (en) { if (dg_mode == 2) { *isp_module_bypass0 |= 0x4000000; *isp_s1_cfg |= 1u<<2; }
          else                 *isp_module_bypass0 |= 0x80000; }
```
(The clearing of `isp_s1_cfg` bit2 is the MODE side effect of §1.1a; `dg_mode==2`
routes the gain after the sensor offset.) A faithful implementation must also
clear `isp_module_bypass0` bit `0x4000000` when disabling a non-`dg_mode==2` DG.

### 1.2 Mode selectors (13)
Each is a single `put`; target `isp_module_mode0` +0x1b0 unless stated.
| # | entry point | mask,shift | field |
|---|-------------|-----------|-------|
| 7 | `isp_reg_set_wdr_compress_mode` | `isp_global_cfg0`+0, `0x1,24` | byte3 bit0 |
| 8 | `isp_reg_set_saturation_mode` | `0x1,1` | SATU_STRONG_MODE |
| 9 | `isp_reg_set_cfa_mode` | `0x1,2` | CFA_BW_MODE |
| 10 | `isp_reg_set_dg_mode` | `0x1,3` | DG_AFTER_SO |
| 11 | `isp_reg_set_ae_mode` | `0x3,4` | byte0[5:4] |
| 12 | `isp_reg_set_lsc_mode` | `0x3,16` | byte2[1:0] |
| 13 | `isp_reg_set_msc_mode` | `0x3,18` | byte2[3:2] |
| 14 | `isp_reg_set_awb_mode` | `0x1,6` | AWB_AFTER_PLTM |
| 15 | `isp_reg_set_hist_src` | `0x1,7` | 1-bit source |
| 16 | `isp_reg_set_hist_mode` | `0x3,8` | byte1[1:0] |
| 17 | `isp_reg_set_dpc_mode` | `0x3,10` | byte1[3:2] |
| 18 | `isp_reg_set_d3d_mode` | `0x3,12` | byte1[5:4] |
| 19 | `isp_reg_set_af_mode` | `0x1,14` | byte1 bit6 |

### 1.3 Module payload writers (29)
| # | entry point | target(s) offset | exact packing |
|---|-------------|------------------|---------------|
| 20 | `isp_reg_set_blc_offset(id, r, gr, gb, b)` | `isp_s1_blc_offset0/1` 0x104/0x108, `isp_s0_blc_offset0/1` 0x1ec/0x1f0 | `lo=(r&0x1fff)|((gr&0x1fff)<<16)`, `hi=(gb&0x1fff)|((b&0x1fff)<<16)`; `store(0x1fff1fff,lo)` to both *0, `store(0x1fff1fff,hi)` to both *1. |
| 21 | `isp_reg_set_wdr_cfg(id, lo_th, hi_th, exp_ratio, slope, mv_th, mv_scale, out_sel)` | `isp_wdr_cfg0/1/2` 0x200/0x204/0x208 | c0 `store(0xffffffff, (lo_th&0xffff)|((hi_th&0xffff)<<16))`; c1 `(exp_ratio&0xffff)|((slope&0xffff)<<16)`; c2 `store(0xc07fffff, (mv_th&0xffff)|((mv_scale&0x7f)<<16)|((out_sel&0x3)<<30))` (bits 23,31 preserved). |
| 22 | `isp_reg_set_dpc(id, r0,r1,r2,r3, slope_th, cold_abs_th)` | `isp_dpc_cfg0/1` 0x260/0x264 | c0 `store(0xffffffff, r0|(r1<<8)|(r2<<16)|(r3<<24))` (low byte each); c1 `store(0x01ff03ff, (slope_th&0x3ff)|((cold_abs_th&0x1ff)<<16))`. |
| 23 | `isp_reg_set_ctc(id, cfg)` | `isp_ctc_cfg0/1/2` 0x270/0x274/0x278 | c0 `store(0x0fff0fff,(th_min&0xfff)|((th_max&0xfff)<<16))`; c1 `store(0xffff, slope&0xffff)`; c2 `store(0x000fff7f,(dir_wt&0x7f)|((dir_th&0xfff)<<8))`. |
| 24 | `isp_reg_set_gca(id, cfg)` | `isp_gca_center/r_para/b_para/ctrl` 0x280/0x284/0x288/0x28c | center `store(0x1fff1fff,(ct_h&0x1fff)|((ct_w&0x1fff)<<16))`; each para `store(0x0fffffff,(para0&0xff)|((para1&0x3ff)<<8)|((para2&0x3ff)<<18))`; ctrl `put(0xff,0, r.int_cns)` (only byte0; `b.int_cns` ignored). |
| 25 | `isp_reg_set_lca(id, cfg)` | `isp_lca_cor_ratio` 0x410, `det_ctrl0/1` 0x414/0x418, `cor_ctrl` 0x41c | cor `store(0x03ff03ff,(gf_cor_ratio&0x3ff)|((pf_cor_ratio&0x3ff)<<16))`; det0 `store(0x0fff0fff,(lum_th&0xfff)|((grad_th&0xfff)<<16))`; det1 `store(0x01fff3ff,(clr_gth&0x3ff)|((pf_rshf&0xf)<<12)|((pf_bslp&0x1ff)<<16))`; cor_ctrl `store(0x000fffff,(clrs_lum_th&0xff)|((pf_clrc_ratio&0xf)<<8)|((gf_clrc_ratio&0xf)<<12)|((pf_decr_ratio&0xf)<<16))`. |
| 26 | `isp_reg_set_d2d_cfg(id, cfg)` | `isp_d2d_cfg0..3` 0x2a0..0x2ac | c0 `store(0x00ffffff, lf|(bf<<8)|(hf<<16))`; c1 `store(0xffffffff, lp_core[0..3])`; c2 `store(0x00ffffff, lp_side[0..2])` (byte3 preserved, no lp3 side); c3 `store(0xffffffff, lp_pcnt[0..3])`. |
| 27 | `isp_reg_set_d3d_cfg(id, cfg)` | `isp_d3d_cfg0..3` 0x2d0..0x2dc | c0 `store(0xffffffff, bright_diff|(clip_ratio<<8)|(lum_diff_clip<<16)|(noise_clip<<24))` (only low byte of `noise_clip` used); c1 `store(0x031f00ff, st_2d|((mv_ori&0x1f)<<16)|((ltf_en&1)<<24)|((rec_en&1)<<25))`; c2 `store(0x0fff0fff, c_weight1|(c_weight2<<16))`; c3 `store(0x03ff0fff, c_weight3|((ltf_update_frm&0x3ff)<<16))`. |
| 28 | `isp_reg_set_sensor_offset(id, r, gr, gb, b)` | `isp_sensor_offset0/1` 0x340/0x344 | `store(0x1fff1fff, r|(gr<<16))`, `store(0x1fff1fff, gb|(b<<16))`, 13-bit fields. |
| 29 | `isp_reg_set_dg_gain(id, r, gr, gb, b)` | `isp_dg_gain0/1` 0x360/0x364 **and** `isp_s1_dg_gain0/1` 0x10c/0x110 **and** `isp_s0_dg_gain0/1` 0xf4/0xf8 | `g0=(r&0xffff)|((gr&0xffff)<<16)`, `g1=(gb&0xffff)|((b&0xffff)<<16)`; write `g0/g1` to all three pairs (6 `store(0xffffffff, …)`). |
| 30 | `isp_reg_set_wb_gain(id, r, gr, gb, b)` | `isp_wb_gain0/1` 0x370/0x374 | `store(0x0fff0fff, r|(gr<<16))`, `store(0x0fff0fff, gb|(b<<16))`, 12-bit. |
| 31 | `isp_reg_set_wb_clip(id, clip)` | `isp_wb_cfg0` 0x378 | `store(0x00000fff, clip&0xfff)`. |
| 32 | `isp_reg_set_lsc(id, ct_x, ct_y, rs_val)` | `isp_rsc_cfg0/1/2` 0x390/0x394/0x398 | `v=(ct_x&0x1fff)|((ct_y&0x1fff)<<13)|((rs_val&0x1f)<<26)`; `store(0x7fffffff,v)` to all three (bit31 preserved). 13/13/5-bit contiguous packing. |
| 33 | `isp_reg_set_pltm_cfg(id, cfg)` | `isp_pltm_cfg0..3` 0x3b0..0x3bc | c0 `store(0xff0f0f07, lss_switch|(cal_en<<1)|(frm_sm_en<<2)|((last_order_ratio&0xf)<<8)|((tr_order&0xf)<<16)|((oripic_ratio&0xff)<<24))`; c1 `store(0xffffffff, intens_asym|(spatial_asm<<8)|(white_level<<16)|((lp_halo_res&0xf)<<24)|((lum_ratio&0xf)<<28))`; c2 `store(0x1f1fffff, block_height|(block_width<<8)|((block_v_num&0x1f)<<16)|((block_h_num&0x1f)<<24))`; c3 `statistic_div`. |
| 34 | `isp_reg_set_cfa(id, dir_th, interp_mode, zig_zag)` | `isp_demosaic_cfg0` 0x400 | `store(0x00f11fff, (dir_th&0x1fff)|((interp_mode&1)<<16)|((zig_zag&0xf)<<20))`. |
| 35 | `isp_reg_set_sharp(id, cfg)` | `isp_sharp_edge_stren/hfrq_stren/diff_cfg/ref_noise/dir_diff_ctrl/edge_ctrl/over_shoot_ctrl/under_shoot_ctrl` 0x420..0x43c | 0x420 `store(0x0fff0fff, edge_black_stren|(edge_white_stren<<16))`; 0x424 `hfrq_black_stren|(hfrq_white_stren<<16)`; 0x428 `store(0x1f1f1f1f, edge_scale|(hfrq_scale<<8)|(scale_ratio<<16)|(conv_ratio<<24))`; 0x42c `ns_lw_th|(ns_hi_th<<16)`; 0x430 `dir_clip_val|(dir_eq_ratio<<16)`; 0x434 `store(0x1f1f00ff, edge_th|((hv_edge_sm&0x1f)<<16)|((aa_edge_sm&0x1f)<<24))`; 0x438 `over_val|(over_area<<16)`; 0x43c `under_val|(under_area<<16)` (12/10-bit as in header). |
| 36 | `isp_reg_set_rgb2rgb_gain_offset(id, gain[9], offset[3])` | `isp_rgb2rgb_gain0..4` 0x440..0x450, `isp_rgb2rgb_offset` 0x454 | g0..g3 = pairs `(gain[0],gain[1])`…`(gain[6],gain[7])` via `store(0x0fff0fff, a|(b<<16))`; g4 `store(0x1fff0fff,(gain[8]&0xfff)|((offset[0]&0x1fff)<<16))`; 0x454 `store(0x1fff1fff,(offset[1]&0x1fff)|((offset[2]&0x1fff)<<16))`. All 12-bit except offsets (13-bit). |
| 37 | `isp_reg_set_cnr(id, c_th,y_th,st_v_y,st_h_y)` | `isp_cnr_cfg0/1` 0x470/0x474 | `store(0x0fff0fff, c_th|(y_th<<16))`, `st_v_y|(st_h_y<<16)`. |
| 38 | `isp_reg_set_saturation(id, r, g, b)` | `isp_satu_cfg0` 0x490 | `store(0x00000fff, (r&0xf)|((g&0xf)<<4)|((b&0xf)<<8))`. |
| 39 | `isp_reg_set_dehaze(id)` | — | no-op stub. |
| 40 | `isp_reg_set_rgb2yuv_gain_offset(id, gain[9], offset[3])` | `isp_rgb2yuv_gain0..4` 0x520..0x530, `isp_rgb2yuv_offset0/1` 0x534/0x538 | g0..g3 pairs via `store(0x07ff07ff, a|(b<<16))`; g4 `store(0x000007ff, gain[8]&0x7ff)` (upper bits preserved); off0 `store(0x07ff07ff,(offset[0]&0x7ff)|((offset[1]&0x7ff)<<16))`; off1 `store(0x000007ff, offset[2]&0x7ff)`. 11-bit. |
| 41 | `isp_reg_set_ae_win(id, width,height,hor_start,ver_start)` | `isp_ae_size` 0x600, `isp_ae_start` 0x604 | size `store(0x0fff0fff, (((width>>1)-1)&0xfff)|((((height>>1)-1)&0xfff)<<16))`; start `store(0x0fff0fff, ((hor_start>>1)&0xfff)|(((ver_start>>1)&0xfff)<<16))`. Widths are pre-decremented exactly. |
| 42 | `isp_reg_set_af_en(id, en_bits)` | `isp_af_cfg` 0x610 | map input bits through `src_bit[12]={0,1,2,3,4,8,9,10,11,16,17,18}` → `hw_bit[12]={0,2,4,5,6,10,12,14,15,16,17,18}`; `hw` accumulates `1<<hw_bit` for each set `1<<src_bit`; write `*reg = (*reg & ~0x0007d475) | (hw & 0x0007d475)`. |
| 43 | `isp_reg_set_af_win(id, hor_num,ver_num,width,height,hor_start,ver_start)` | `isp_af_cfg` 0x610, `isp_af_size` 0x614, `isp_af_start` 0x618 | cfg `put(0x1f,19,hor_num)` and `put(0x1f,24,ver_num)` (byte2[7:3], byte3[4:0]); size/start identical to AE window. |
| 44 | `isp_reg_set_af_filter(id, f)` | 19 sparse `isp_af_filter[i]` banks, offsets (in order) `0x61c,0x620,0x62c,0x630,0x634,0x638,0x63c,0x648,0x64c,0x650,0x654,0x658,0x65c,0x660,0x664,0x668,0x66c,0x670,0x674` | Write order and fields: r0 `0x3fffffff` = iir0_coef[0..2] 10-bit×3; r1 same iir0_coef[3..5]; r2..r5 `0x3ff` = iir0_s[0..3]; r6 `0x3fffffff` = fir0_coef[0..4] 6-bit×5; r7 `0x3` = iir0_dilate; r8 `0xffff` = iir0_ldg_gain|(iir0_ldg_hgain<<8); r9 iir0_ldg_th|(hth<<8); r10 fir0 ldg gain pair; r11 fir0 ldg th pair; r12 `0x00ff00ff` = iir0_ldg_lslope|(hslope<<4)|(fir0 lslope<<16)|(fir0 hslope<<20); r13 `0x00ff00ff` = iir0_core_th|(iir0_core_peak<<16); r14 fir0 pair; r15 `0x00000f0f` = iir0_core_slope|(fir0_core_slope<<8); r16 `0xff` = hlt_th; r17 `0x1fff1fff` = r_offset|(g_offset<<16); r18 `0x1fff` = b_offset. `0x624,0x628,0x644` are never written. |
| 45 | `isp_reg_set_awb_satur_lim(id, lim_r, lim_g, lim_b)` | `isp_awb_cfg0/1` 0x690/0x694 | `put(0xff,0,lim_r)`, `put(0xff,16,lim_b)` on cfg0; `put(0xff,0,lim_b)` on cfg1. **`lim_g` is ignored** (never programmed). |
| 46 | `isp_reg_set_awb_win(id, width,height,hor_start,ver_start)` | `isp_awb_cfg2/3` 0x698/0x69c | cfg2 `store(0x01ff01ff, (((width>>1)-1)&0x1ff)|((((height>>1)-1)&0x1ff)<<16))`; cfg3 `store(0x07ff07ff, ((hor_start>>1)&0x7ff)|(((ver_start>>1)&0x7ff)<<16))`. 9-bit size, 11-bit start. |
| 47 | `isp_reg_set_hist_win(id, width,height,hor_start,ver_start)` | `isp_hist_size` 0x6c0, `isp_hist_start` 0x6c4 | size like AE; start `store(0x0fff0fff, ((ver_start>>1)&0xfff)|(((hor_start>>1)&0xfff)<<16))` — **ver in the low half, hor in the high half** (swapped vs AE). |
| 48 | `isp_reg_set_afs_anti_flick(id, line_inc)` | `isp_afs_cfg0` 0x6e0 | `put(0x3f,0,line_inc)`: byte0[5:0]. |

NULL-cfg behaviour: `isp_reg_set_ctc/gca/lca/d2d_cfg/d3d_cfg/pltm_cfg/sharp` and
the array-taking writers (`rgb2rgb_gain_offset`, `rgb2yuv_gain_offset`) return
without writing if their cfg/array pointer is NULL. Scalar writers only check the
descriptor.

### 1.4 LUT writers (21)
| # | entry point | target offset | behaviour |
|---|-------------|---------------|-----------|
| 49 | `isp_reg_set_d3d_lum_th_lut` | 0x770 | memcpy 0x42 bytes |
| 50 | `isp_reg_set_d3d_bright_th_lut` | 0x7b4 | memcpy 0x42 |
| 51 | `isp_reg_set_d3d_ref_noise_lut` | 0x7f8 | memcpy 0x42 |
| 52 | `isp_reg_set_d3d_k_lut` | 0x83c..0x850 (6 words) | see §1.4a |
| 53 | `isp_reg_set_d3d_k_delta_lut` | 0x854..0x868 (6 words) | see §1.4a |
| 54 | `isp_reg_set_sharp_val_lut` | 0x86c | memcpy 0x42 |
| 55 | `isp_reg_set_sharp_edge_lum_lut` | 0x8b0 | memcpy 0x42 |
| 56 | `isp_reg_set_sharp_hfrq_lum_lut` | 0x8f4 | memcpy 0x42 |
| 57 | `isp_reg_set_sharp_hsv_lut` | 0x938 | memcpy 0x42 |
| 58 | `isp_reg_set_sharp_s_map_lut` | 0x994 | memcpy 0x21 bytes |
| 59–62 | `isp_reg_set_d2d_lp{0,1,2,3}_np_lut` | 0x9b8,0x9fc,0xa40,0xa84 | memcpy 0x42 each |
| 63 | `isp_reg_set_af_square_lut` | 0xac8 | memcpy 16 bytes |
| 64–67 | `isp_reg_set_msc_blw_lut`, `_blh_lut`, `_blw_dlt_lut`, `_blh_dlt_lut` | 0xad8,0xae8,0xaf8,0xb08 | 4 words; word `j` = `(v[3j]&0x3ff) | ((v[3j+1]&0x3ff)<<10) | ((v[3j+2]&0x3ff)<<20)`, fields `0x3fffffff`; 12 u16 in |
| 68 | `isp_reg_set_lca_pf_satu_lut` | 0xb18 | memcpy 0x21 bytes |
| 69 | `isp_reg_set_lca_gf_satu_lut` | 0xb3c | memcpy 0x21 bytes |

**1.4a d3d k / k-delta.** For `i` in 0..4: `w = Σ_{k=0..5} (vals[6i+k]&0x1f) << (5k)`,
written with `store(0x3fffffff,w)`. Word 5 (index 5) holds only
`(vals[30]&0x1f) | ((vals[31]&0x1f)<<5)` with `store(0x3ff,…)`; bits[31:10] survive.

All LUT writers return without writing if `src`/`vals` is NULL. The four MSC LUT
writers read a 12-`uint16_t` array; the d3d k writers read 32 bytes.

### 1.4b Exact signatures

Control:
```
void isp_reg_map_load_addr(unsigned long id, void *base);
void isp_reg_set_input_fmt(unsigned long id, uint32_t fmt);
void isp_reg_update_table(unsigned long id, uint32_t mask);
void isp_reg_top_control(unsigned long id, uint32_t n, uint32_t w);
void isp_reg_module_enable(unsigned long id, uint32_t flag);
void isp_reg_module_disable(unsigned long id, uint32_t flag);
void isp_reg_set_dg_bypass(unsigned long id, int en, uint32_t dg_mode);   /* helper, not a vendor symbol */
```
Mode selectors (all `void isp_reg_set_*(unsigned long id, uint32_t mode)`):
`isp_reg_set_wdr_compress_mode`, `isp_reg_set_saturation_mode`,
`isp_reg_set_cfa_mode`, `isp_reg_set_dg_mode`, `isp_reg_set_ae_mode`,
`isp_reg_set_lsc_mode`, `isp_reg_set_msc_mode`, `isp_reg_set_awb_mode`,
`isp_reg_set_hist_src`, `isp_reg_set_hist_mode`, `isp_reg_set_dpc_mode`,
`isp_reg_set_d3d_mode`, `isp_reg_set_af_mode`.
Payload:
```
void isp_reg_set_blc_offset(unsigned long id, uint32_t r, uint32_t gr, uint32_t gb, uint32_t b);
void isp_reg_set_wdr_cfg(unsigned long id, uint32_t lo_th, uint32_t hi_th, uint32_t exp_ratio,
                         uint32_t slope, uint32_t mv_th, uint32_t mv_scale, uint32_t out_sel);
void isp_reg_set_dpc(unsigned long id, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                     uint32_t slope_th, uint32_t cold_abs_th);
void isp_reg_set_ctc(unsigned long id, const fwi_reg_ctc_cfg_t *cfg);
void isp_reg_set_gca(unsigned long id, const fwi_reg_gca_cfg_t *cfg);
void isp_reg_set_lca(unsigned long id, const fwi_reg_lca_cfg_t *cfg);
void isp_reg_set_d2d_cfg(unsigned long id, const fwi_reg_d2d_cfg_t *cfg);
void isp_reg_set_d3d_cfg(unsigned long id, const fwi_reg_d3d_cfg_t *cfg);
void isp_reg_set_sensor_offset(unsigned long id, uint32_t r, uint32_t gr, uint32_t gb, uint32_t b);
void isp_reg_set_dg_gain(unsigned long id, uint32_t r, uint32_t gr, uint32_t gb, uint32_t b);
void isp_reg_set_wb_gain(unsigned long id, uint32_t r, uint32_t gr, uint32_t gb, uint32_t b);
void isp_reg_set_wb_clip(unsigned long id, uint32_t clip);
void isp_reg_set_lsc(unsigned long id, uint32_t ct_x, uint32_t ct_y, uint32_t rs_val);
void isp_reg_set_pltm_cfg(unsigned long id, const fwi_reg_pltm_cfg_t *cfg);
void isp_reg_set_cfa(unsigned long id, uint32_t dir_th, uint32_t interp_mode, uint32_t zig_zag);
void isp_reg_set_sharp(unsigned long id, const fwi_reg_sharp_cfg_t *cfg);
void isp_reg_set_rgb2rgb_gain_offset(unsigned long id, const uint16_t gain[9], const uint16_t offset[3]);
void isp_reg_set_cnr(unsigned long id, uint32_t c_th, uint32_t y_th, uint32_t st_v_y, uint32_t st_h_y);
void isp_reg_set_saturation(unsigned long id, uint32_t r, uint32_t g, uint32_t b);
void isp_reg_set_dehaze(unsigned long id);
void isp_reg_set_rgb2yuv_gain_offset(unsigned long id, const uint16_t gain[9], const uint16_t offset[3]);
void isp_reg_set_ae_win(unsigned long id, uint32_t width, uint32_t height,
                        uint32_t hor_start, uint32_t ver_start);
void isp_reg_set_af_en(unsigned long id, uint32_t en_bits);
void isp_reg_set_af_win(unsigned long id, uint32_t hor_num, uint32_t ver_num, uint32_t width,
                        uint32_t height, uint32_t hor_start, uint32_t ver_start);
void isp_reg_set_af_filter(unsigned long id, const fwi_reg_af_filter_t *f);
void isp_reg_set_awb_satur_lim(unsigned long id, uint32_t lim_r, uint32_t lim_g, uint32_t lim_b);
void isp_reg_set_awb_win(unsigned long id, uint32_t width, uint32_t height,
                         uint32_t hor_start, uint32_t ver_start);
void isp_reg_set_hist_win(unsigned long id, uint32_t width, uint32_t height,
                          uint32_t hor_start, uint32_t ver_start);
void isp_reg_set_afs_anti_flick(unsigned long id, uint32_t line_inc);
```
LUT:
```
void isp_reg_set_d3d_lum_th_lut(unsigned long id, const void *src);
void isp_reg_set_d3d_bright_th_lut(unsigned long id, const void *src);
void isp_reg_set_d3d_ref_noise_lut(unsigned long id, const void *src);
void isp_reg_set_d3d_k_lut(unsigned long id, const uint8_t *vals);
void isp_reg_set_d3d_k_delta_lut(unsigned long id, const uint8_t *vals);
void isp_reg_set_sharp_val_lut(unsigned long id, const void *src);
void isp_reg_set_sharp_edge_lum_lut(unsigned long id, const void *src);
void isp_reg_set_sharp_hfrq_lum_lut(unsigned long id, const void *src);
void isp_reg_set_sharp_hsv_lut(unsigned long id, const void *src);
void isp_reg_set_sharp_s_map_lut(unsigned long id, const void *src);
void isp_reg_set_d2d_lp0_np_lut(unsigned long id, const void *src);
void isp_reg_set_d2d_lp1_np_lut(unsigned long id, const void *src);
void isp_reg_set_d2d_lp2_np_lut(unsigned long id, const void *src);
void isp_reg_set_d2d_lp3_np_lut(unsigned long id, const void *src);
void isp_reg_set_af_square_lut(unsigned long id, const void *src);
void isp_reg_set_msc_blw_lut(unsigned long id, const uint16_t *vals);
void isp_reg_set_msc_blh_lut(unsigned long id, const uint16_t *vals);
void isp_reg_set_msc_blw_dlt_lut(unsigned long id, const uint16_t *vals);
void isp_reg_set_msc_blh_dlt_lut(unsigned long id, const uint16_t *vals);
void isp_reg_set_lca_pf_satu_lut(unsigned long id, const void *src);
void isp_reg_set_lca_gf_satu_lut(unsigned long id, const void *src);
```

### 1.5 `reg_writers.c` traps
- `isp_reg_set_awb_satur_lim` ignores `lim_g`; both cfg0 byte0 and cfg1 byte0 get
  `lim_b`, cfg0 byte2 gets `lim_b` as well. Do not "fix" the missing G.
- `isp_reg_update_table` shares mask bit1 between byte0 bit1 and byte1 bit5; a
  single-field `put` is wrong here.
- `isp_reg_module_enable/disable` mutate two extra registers when the flag
  contains BLC/LINEAR/MODE/WDR; the enable/disable pair is not symmetric for
  arbitrary flags unless those are modelled.
- `isp_reg_set_dpc`/`ctc`/`gca`/`lca`/`d2d`/`d3d`/`pltm`/`sharp` use `store` with
  a `fields` union that deliberately leaves some bits (e.g. d2d cfg2 byte3,
  dpc/d3d reserved bits, d3d cfg4 bits[15:10]) untouched.
- `isp_reg_set_af_filter` skips three descriptor slots; those offsets are never
  written even if the caller's payload has values for them.
- All writers are no-ops for `id>=2` or before `isp_reg_map_load_addr`.

---

## 2. `module_cfg.c` — dispatch and payload builders

### 2.1 Entry points
`void isp_map_addr(fwi_mod_config_t *cfg, void *vaddr)`:
if `cfg!=NULL`, `isp_reg_map_load_addr(cfg->isp_dev_id, vaddr)`.
(The second argument is passed through.)

`void isp_hardware_update(fwi_mod_config_t *cfg)`:
if `cfg==NULL` return. Walk `isp_module_attrs[0..30]` in table order:
```
if (cfg->module_enable_flag & attr.feature_bit) {
    if (attr.config) attr.config(cfg);
    if (attr.enable) attr.enable(cfg, ISP_MODULE_ENABLE);
} else if (attr.enable) {
    attr.enable(cfg, ISP_MODULE_DISABLE);
}
```
After the 31st entry: `cfg->table_update = 0xffffffff;`
`isp_reg_update_table(cfg->isp_dev_id, 0xffff);` `cfg->table_update = 0;`.
Consequence: per-module `table_update` bits set by the prepare routines are
discarded; a full 16-bit table load is issued every frame.

The 31-entry table order, feature bit, config, enable and hardware module are in
§2.4. `module_enable_flag` is the OR of feature bits.

### 2.2 Prepare routines — `void isp_reg_prepare_*(fwi_mod_config_t*)`
All return immediately if `cfg==NULL`. "writer calls" are the §1 entry points in
call order. Payload fields named are `fwi_mod_config_t` members.

| config | effect (writer calls and payload writes) |
|--------|------------------------------------------|
| `isp_reg_prepare_afs` | `isp_reg_set_afs_anti_flick(dev, cfg->afs_cfg.inc_line)`. |
| `isp_reg_prepare_sharpness` | `isp_reg_set_sharp(dev,&cfg->sharp_cfg)`; then sharp_val, sharp_edge_lum, sharp_hfrq_lum, sharp_hsv, sharp_s_map LUT writers from the matching arrays. |
| `isp_reg_prepare_contrast` | no-op. |
| `isp_reg_prepare_d2d` | `isp_reg_set_d2d_cfg(dev,&cfg->bdnf_cfg)`; d2d_lp0..3 writers from `cfg->d2d_lp_lut[0..3]`. |
| `isp_reg_prepare_rgb_drc` | copy `drc_src`→`drc_table` 0x200 bytes; `table_update |= 0x10`. |
| `isp_reg_prepare_pltm` | `isp_reg_set_pltm_cfg(dev,&cfg->pltm_cfg)`; copy `pltm_src`→`pltm_table` 0x600; `table_update |= 0x100`. |
| `isp_reg_prepare_wdr` | `isp_reg_set_wdr_compress_mode(dev,cfg->mode_cfg.wdr_cmp_mode)`; `isp_reg_set_wdr_cfg(dev, …)`; copy `wdr_src`→`wdr_cfg.wdr_table` 0x4000; `table_update |= 0x40`. |
| `isp_reg_prepare_cem` | copy `cem_src`→`cem_table` 0x1700; `table_update |= 0x200`. |
| `isp_reg_prepare_lens` | `isp_reg_set_lsc_mode(dev,cfg->mode_cfg.rsc_mode)`; `isp_reg_set_lsc(dev, lens_cfg.lsc_cfg)`; copy `lens_src`→`lens_table` (768 u16); `table_update |= 0x4`. |
| `isp_reg_prepare_gamma` | see §2.3; `table_update |= 0x8`. |
| `isp_reg_prepare_rgb2yuv` | `isp_reg_set_rgb2yuv_gain_offset(dev, cfg->rgb2yuv.gain, cfg->rgb2yuv.offset)`. |
| `isp_reg_prepare_rgb2rgb` | `isp_reg_set_rgb2rgb_gain_offset(dev, cfg->rgb2rgb_cfg.matrix, offset)`. |
| `isp_reg_prepare_ae_win` | `isp_reg_set_ae_mode(dev,cfg->mode_cfg.ae_mode)`; `isp_reg_set_ae_win(dev, cfg->ae_cfg.win.{width,height,hor_start,ver_start})`. |
| `isp_reg_prepare_af` | `isp_reg_set_af_mode(dev,cfg->af_cfg.mode)`; `isp_reg_set_af_en(dev,cfg->af_cfg.en_bits)`; `isp_reg_set_af_win(dev, cfg->af_cfg.win.{hor_num,ver_num,width,height,hor_start,ver_start})`; `isp_reg_set_af_filter(dev,&cfg->af_cfg.filter)`; `isp_reg_set_af_square_lut(dev,cfg->af_cfg.square_lut)`. |
| `isp_reg_prepare_awb` | `isp_reg_set_awb_mode(dev,cfg->mode_cfg.awb_mode)`; `isp_reg_set_awb_satur_lim(dev, sat_r,sat_g,sat_b)`; `isp_reg_set_awb_win(dev, win)`. |
| `isp_reg_prepare_hist` | `isp_reg_set_hist_src(dev,cfg->mode_cfg.hist_sel)`; `isp_reg_set_hist_mode(dev,cfg->hist_cfg.mode)`; `isp_reg_set_hist_win(dev, cfg->hist_cfg.win)`. |
| `isp_reg_prepare_blc` | `isp_reg_set_blc_offset(dev, cfg->gain_offset_cfg.offset[0..3])`. |
| `isp_reg_prepare_wb_gain` | `isp_reg_set_wb_clip(dev,clip_val)`; `isp_reg_set_wb_gain(dev, wb_gain[0..3])`. |
| `isp_reg_prepare_dpc` | `isp_reg_set_dpc_mode(dev,cfg->mode_cfg.otf_dpc_mode)`; `isp_reg_set_dpc(dev, otf_cfg.ratio[0..3], slope_th, cold_abs_th)`. |
| `isp_reg_prepare_cfa` | `isp_reg_set_cfa_mode(dev,cfg->mode_cfg.cfa_mode)`; `isp_reg_set_cfa(dev, dir_th, interp_mode, zig_zag)`. |
| `isp_reg_prepare_d3d` | `isp_reg_set_d3d_mode(dev,cfg->mode_cfg.d3d_mode)`; `isp_reg_set_d3d_cfg(dev,&cfg->tdf_cfg)`; copy `d3d_tdnf_th`→`d3d_lum_th_lut` and →`d3d_bright_th_lut` (0x42 each); d3d lum_th, bright_th, ref_noise, k, k_delta writers; `table_update |= 0x80`. (The copy overwrites any pre-set lum/bright arrays.) |
| `isp_reg_prepare_cnr` | `isp_reg_set_cnr(dev, c_th,y_th,st_v_y,st_h_y)`. |
| `isp_reg_prepare_saturation` | `sum = satu_r+satu_g+satu_b` (signed 32-bit); if `sum != 0x10` → return doing nothing (no mode, no LUT, no writer). Else `isp_reg_set_saturation_mode(dev,satu_cfg.mode)`, `isp_reg_set_saturation(dev, satu_r,satu_g,satu_b)`; if the top-level `saturation_table` pointer (`satu_src`) is non-NULL copy 0x200 bytes **from the embedded `satu_cfg.table` into the pointer target** and `table_update |= 0x20`. |
| `isp_reg_prepare_linear` | if the linear source is absent, return without touching the update bit; else copy it 0x600 bytes into the front-end table, then `table_update |= 0x2`. The clean model carries the SDK `linear_table` target as `linear_src` (source) and the SDK `fe_table` target as `fe_table` (destination). |
| `isp_reg_prepare_sensor_offset` | `isp_reg_set_sensor_offset(dev, sensor_offset[0..3])`. |
| `isp_reg_prepare_digital_gain` | `isp_reg_set_dg_mode(dev,cfg->mode_cfg.dg_mode)`; `isp_reg_set_dg_gain(dev, gain[0..3])`. |
| `isp_reg_prepare_ctc` | `isp_reg_set_ctc(dev,&cfg->ctc_cfg)`. |
| `isp_reg_prepare_mode` | `isp_reg_set_input_fmt(dev,cfg->mode_cfg.input_fmt)`. |
| `isp_reg_prepare_msc` | `isp_reg_set_msc_mode(dev,cfg->mode_cfg.msc_mode)`; blw, blh, blw_dlt, blh_dlt writers from the four `msc_cfg` banks; if `cfg->msc_table != NULL` then `table_update |= 0x400`. Four distinct banks, not all from blw. |
| `isp_reg_prepare_lca` | `isp_reg_set_lca(dev,&cfg->lca_cfg)`; `isp_reg_set_lca_pf_satu_lut`; `isp_reg_set_lca_gf_satu_lut`. |
| `isp_reg_prepare_gca` | `isp_reg_set_gca(dev,&cfg->gca_cfg)`. |

**Correction (2026-09-16):** `isp_reg_prepare_saturation`'s 0x200-byte table copy was
found to run embedded → pointer by black-box probing of the deployed object (the
spec earlier stated the inverted pointer → embedded direction).

### 2.3 `isp_reg_prepare_gamma` — three-way source selection
The prepare routine's job is to fill `gamma_cfg.gamma_packed[1024]` from the
3×1024 `gamma_cfg.gamma_tbl` **only if the output pointer is present**. The
deployed object chooses by probing two entries of `gamma_tbl`:

1. **`gamma_tbl[0xbff] != 0`** (normal curve) — for each `i` in 0..1023:
   `r = (gamma_tbl[i]+2)>>2`, `g = (gamma_tbl[1024+i]+2)>>2`,
   `b = (gamma_tbl[2048+i]+2)>>2`; clamp each to `0x3ff` if `>=0x400`; emit
   `packed[i] = r | (g<<10) | (b<<20)`.
2. **`gamma_tbl[0xbff]==0 && gamma_tbl[0x2ff]==0`** (all-zero/invalid curve) —
   generate a 10-bit gamma-2.2 ramp:
   `v = (uint32_t)(1023.0 * pow((double)i/1023.0, 1.0/2.2))`, `packed[i] = v | (v<<10) | (v<<20)`.
3. **`gamma_tbl[0xbff]==0 && gamma_tbl[0x2ff]!=0`** **[blind]** — upscale the
   first 256 entries of each plane to 1024 and pack:
   for `i`=0..1023: `a=i>>2`, `f=i&3`, `n2=min(a+1,255)`;
   `r=interp(f,0,4,gamma_tbl[a],gamma_tbl[n2])`, add 3 if negative;
   `g=interp(f,0,4,gamma_tbl[256+a],gamma_tbl[256+n2])`, add 3 if negative;
   `b=interp(f,0,4,gamma_tbl[512+a],gamma_tbl[512+n2])`, add 3 if negative;
   then `g4=(g>>2)`, `b4=(b>>2)`, `r4=(r>>2)` (negative→0), each clamped to
   `0x3ff` if `>=0x3ff`; `packed[i] = (b4<<20)|(g4<<10)|(r4<0x400?r4:0x3ff)`.
   This branch is not covered by the differential; treat the exact negative
   rounding as inferred from the object.

`isp_reg_prepare_gamma` sets `table_update |= 0x8` unconditionally (regardless of which
branch, and even if the output pointer is absent in the vendor form).

### 2.4 Dispatch table and enable routines
`isp_module_attrs[31]` in order (feature bit, name, config, enable). A `—` enable
means the slot is NULL (never called).

| idx | feature bit | config | enable | enable action (on set / on clear) |
|----:|-------------|--------|--------|-----------------------------------|
| 0 | 0x00010000 AFS | `isp_reg_prepare_afs` | `isp_reg_enable_afs` | set/clear 0x10000 |
| 1 | 0x00000400 SHARP | `isp_reg_prepare_sharpness` | `isp_reg_enable_sharpness` | set/clear 0x400 |
| 2 | 0x00400000 CONTRAST | `isp_reg_prepare_contrast` | `isp_reg_enable_contrast` | no-op |
| 3 | 0x00000010 D2D | `isp_reg_prepare_d2d` | `isp_reg_enable_d2d` | set/clear 0x10 |
| 4 | 0x00002000 RGB_DRC | `isp_reg_prepare_rgb_drc` | `isp_reg_enable_rgb_drc` | set/clear 0x2000 |
| 5 | 0x00004000 PLTM | `isp_reg_prepare_pltm` | `isp_reg_enable_pltm` | set/clear 0x4000 |
| 6 | 0x00000004 WDR | `isp_reg_prepare_wdr` | `isp_reg_enable_wdr` | set/clear 0x4 |
| 7 | 0x00008000 CEM | `isp_reg_prepare_cem` | `isp_reg_enable_cem` | set/clear 0x8000 |
| 8 | 0x00000100 LSC | `isp_reg_prepare_lens` | `isp_reg_enable_lens` | set/clear 0x100 |
| 9 | 0x00000200 GAMMA | `isp_reg_prepare_gamma` | `isp_reg_enable_gamma` | set/clear 0x200 |
| 10 | 0x40000000 RGB2YUV | `isp_reg_prepare_rgb2yuv` | `isp_reg_enable_rgb2yuv` | no-op (void) |
| 11 | 0x00001000 RGB2RGB | `isp_reg_prepare_rgb2rgb` | `isp_reg_enable_rgb2rgb` | set/clear 0x1000 |
| 12 | 0x00000001 AE | `isp_reg_prepare_ae_win` | `isp_reg_enable_ae` | set/clear 0x1 |
| 13 | 0x00000800 AF | `isp_reg_prepare_af` | `isp_reg_enable_af` | set/clear 0x800 |
| 14 | 0x00000040 AWB | `isp_reg_prepare_awb` | `isp_reg_enable_awb` | set/clear 0x40 |
| 15 | 0x00020000 HIST | `isp_reg_prepare_hist` | `isp_reg_enable_hist` | set/clear 0x20000 |
| 16 | 0x00040000 BLC | `isp_reg_prepare_blc` | `isp_reg_enable_blc` | set/clear 0x40000 |
| 17 | 0x00000080 WB | `isp_reg_prepare_wb_gain` | `isp_reg_enable_wb_gain` | set/clear 0x80 |
| 18 | 0x00000008 DPC | `isp_reg_prepare_dpc` | `isp_reg_enable_dpc` | set/clear 0x8 |
| 19 | 0x02000000 CFA | `isp_reg_prepare_cfa` | — | — |
| 20 | 0x00000020 D3D | `isp_reg_prepare_d3d` | `isp_reg_enable_d3d` | set/clear 0x20 |
| 21 | 0x00800000 CNR | `isp_reg_prepare_cnr` | `isp_reg_enable_cnr` | set/clear 0x800000 |
| 22 | 0x01000000 SATU | `isp_reg_prepare_saturation` | `isp_reg_enable_saturation` | set/clear 0x1000000 |
| 23 | 0x00000002 LINEAR | `isp_reg_prepare_linear` | `isp_reg_enable_linear` | set/clear 0x2 |
| 24 | 0x00100000 SO | `isp_reg_prepare_sensor_offset` | `isp_reg_enable_sensor_offset` | set/clear 0x100000 |
| 25 | 0x00080000 DG | `isp_reg_prepare_digital_gain` | `isp_reg_enable_digital_gain` | see below |
| 26 | 0x00200000 CTC | `isp_reg_prepare_ctc` | `isp_reg_enable_ctc` | set/clear 0x200000 |
| 27 | 0x04000000 MODE | `isp_reg_prepare_mode` | — | — |
| 28 | 0x20000000 MSC | `isp_reg_prepare_msc` | `isp_reg_enable_msc` | see below |
| 29 | 0x08000000 LCA | `isp_reg_prepare_lca` | `isp_reg_enable_lca` | set/clear 0x8000000 |
| 30 | 0x10000000 GCA | `isp_reg_prepare_gca` | `isp_reg_enable_gca` | set/clear 0x10000000 |

`isp_reg_enable_digital_gain(cfg,en)` uses `isp_reg_set_dg_bypass(dev, en==1, cfg->mode_cfg.dg_mode)`
(§1.1b). `isp_reg_enable_msc(cfg,en)` is the odd one: it drives the **CONTRAST**
module bit `0x400000`, not MSC — on enable `isp_reg_module_enable(dev,0x400000)`,
on disable `isp_reg_module_disable(dev,0x400000)`. `isp_reg_enable_contrast` and
`isp_reg_enable_rgb2yuv` are no-ops.

Signatures: every prepare routine is `void isp_reg_prepare_X(fwi_mod_config_t *cfg);`; every enable routine is `void isp_reg_enable_X(fwi_mod_config_t *cfg, isp_module_enable_t en);` with `ISP_MODULE_DISABLE==0`, `ISP_MODULE_ENABLE==1`. `isp_reg_enable_contrast` and `isp_reg_enable_rgb2yuv` ignore both arguments; all other enables ignore `cfg` except for `cfg->isp_dev_id` and (DG) `cfg->mode_cfg.dg_mode`.

### 2.5 `module_cfg.c` traps
- `isp_reg_enable_msc` toggles CONTRAST; any implementation that toggles 0x20000000 is
  wrong. The same routine's disable branch clears 0x400000; the current
  differential seeds have that bit clear in both the zero and preload images
  (`0xA5A5A5A5 &
  0x400000 == 0`), so an omitted disable-clear is invisible to it **[blind]**.
- `isp_reg_prepare_saturation`'s `sum==0x10` gate skips *all* work (including the mode
  register) on mismatch. The table-update bit is only raised when the saturation
  table pointer is non-NULL **[blind]**.
- `isp_reg_prepare_gamma` three-way probe (§2.3); the all-zero fallback curve is
  `floor(1023*(i/1023)^(1/2.2))` packed in all three fields.
- `isp_reg_prepare_d3d` overwrites `d3d_lum_th_lut` and `d3d_bright_th_lut` from
  `d3d_tdnf_th` before writing them, and sets the D3D bit unconditionally.
- `isp_hardware_update` discards per-module `table_update` bits and always issues a
  full `0xffff` load; a "smarter" implementation that ORs the bits is wrong.
- `isp_reg_prepare_linear` **direction**: the deployed copy is source = linear table
  (clean `linear_src`, SDK `linear_table`), destination = front-end table (clean
  `fe_table`, SDK `fe_table`). A `fe_table → linear_src` copy is wrong. The
  layout-agnostic `reg_cfg` differential could not see this because its harness
  aliased both pointer targets to one buffer; the `reg_cfg_sdk` differential uses
  distinct buffers and catches it.
- `isp_reg_prepare_gamma`, `isp_reg_prepare_cem`, `isp_reg_prepare_rgb_drc`, `isp_reg_prepare_wdr`,
  `isp_reg_prepare_pltm` gate their table copy and update bit on the corresponding SDK
  `void *` being non-NULL (and `isp_reg_prepare_msc`/`isp_reg_prepare_saturation` gate the bit
  on `msc_table`/`saturation_table`). The clean model embeds those tables as arrays,
  so it cannot represent the absent case; in the deployed framework the pointers are
  always assigned at init. The clean `isp_reg_prepare_lens` `lens_src → lens_table` copy
  has no SDK-visible counterpart: the SDK `lens_table` region is written by the base
  tier's `config_lens_table`, and the deployed `isp_reg_prepare_lens` performs no copy.
- CFA and MODE have no enable slot; their config still runs when the feature bit is
  in `module_enable_flag`.

---

## 3. `base.c` — configuration / statistics layer

Entry points on `fwi_base_ctx_t *ctx`. Internal helpers are required behaviour
and are specified where they carry formulas.

### 3.1 Statistics capture

**`void isp_handle_stats(ctx, const void *buffer)`** — NULL ctx/buffer → return.
Calls, in order: `handle_ae`, `handle_awb`, `handle_af`, `handle_afs`,
`handle_pltm`, then `stats_attach`. DMA sub-buffer offsets from `buffer` (bytes):
HIST `0x40`, AE `0x240`, AF `0x4a40`, AFS `0x8640`, AWB `0x8840`, PLTM `0xdc40`;
AWB window counts at AWB base `+0x4800`.

**`void isp_handle_stats_sync(ctx, buf0, buf1)`** — NULL check on all three.
Calls `merge_ae`, `merge_awb`, `merge_afs`, `merge_pltm`, then `stats_attach`.
**No AF merge is performed** and no AF/DMA pointer is de-interleaved.

**Common:** `stats_attach` sets `stats_attach.{awb,ae,af,afs,pltm}_stats` to the
matching `stats_ctx.stats` sub-struct and `{md,gtm,rolloff}_stats` to
`&stats_ctx.stats`.

**`handle_ae(ctx, buf)`** — if `!isp_test_settings.ae_en` return (no history
rotation). Otherwise:
- rotate `dynamic_stats.accum_last3←last2`, `last2←last1`, `last1←accum`;
- `n = module_cfg.ae_cfg.ae_reg_win.height * .width`;
  `sh = (ae_settings.ae_mode==0)?2:1`; `ae.win_pix_n = n >> sh` (0 if n==0);
- if `isp_ini_cfg.ae_stat_sel != 0 && wdr_en != 0`: per window `i` (384):
  `raw[0..2] = u32(win+12i+{0,4,8})`; for each channel
  `idx = ((raw<<4) << (16 - cmdout)) / win_pix_n`, clamp `>0xfff`→`0xfff`,
  `corr = (win_pix_n * anti_gamma_tbl[idx]) >> 4`; store `win_r/g/b = corr`,
  `luma = clamp_u8(((10*raw1+4*raw0+2*raw2)>>4) / win_pix_n)`,
  `accum = (corr[0]+corr[2]+2*corr[1])>>2`;
- else per window: `win_r/g/b = raw`, `luma = clamp_u8(((10*g+4*r+2*b)>>4)/win_pix_n)`,
  `accum = (r+b+2g)>>2`;
- histogram: for `i` 0..127: `v = u32(hist+4i)>>1`, `hist[2i]=hist[2i+1]=v`.
`cmdout = ae_param.comanding_output_bits`. Div-by-zero if `win_pix_n==0`.

**`handle_awb(ctx, buf)`** — if `!awb_en` return. `gr = wb_gain[1]`;
`ratio_r = gr ? ((wb_gain[0]<<8) + gr/2)/gr : 0`; `ratio_b` likewise with
`wb_gain[3]`. Per window `i` (1024): `r/g/b = u32(base+12i)`, `c = u16(cnt+2i)`;
`count=c`. If `c==0`: sums and averages 0. Else if `wdr_en==0`:
`sr = ratio_r ? ((ratio_r>>1)+r*256)/ratio_r : r*256` (same for `sb`);
`sum_r/g/b = sr,g,sb`; `avg = (sum + c/2)/c`. Else (WDR): for each channel
`gi = (c/2 + ((raw<<4) << (16-cmdout)))/c`, clamp `>0xfff`→`0xfff`,
`gam = anti_gamma_tbl[gi]>>4`; `sum = raw`; `avg_g = gam[1]`,
`avg_r = ratio_r ? ((ratio_r>>1)+gam[0]*256)/ratio_r : gam[0]*256`,
`avg_b` likewise.

**`handle_af(ctx, buf)`** — gate: `af_en || isp_test_focus`. Per window (384),
all `u64`: `iir=base+0x0000`, `fir=+0x0c00`, `iir_cnt=+0x6c00`, `fir_cnt=+0xcc00`,
`hlt_cnt=+0x12c00` (stride 8); `h_d1=iir`, `h_d2=fir`, `v_d1=v_d2=0`,
`count = iir_cnt+fir_cnt+hlt_cnt` (64-bit wrap).

**`handle_afs(ctx, buf)`** — gate `afs_en`. `sum[0..127]=u32(base+4i)`;
`pic_w/pic_h = stats_ctx.pic_w/h`.

**`handle_pltm(ctx, buf)`** — gate `pltm_en`. Read `lst[0..767]=u16(base+2i)`,
accumulate `sum_before` (u64), `mn_before` (init 0xfff, min), `mx_before`.
Then for `i<1024`: `w = 10*awb.avg_g[i] + 4*awb.avg_r[i] + 2*awb.avg_b[i]`
(32-bit wrap); store `awb.avg[i]=w`, accumulate `sum_after` (u64),
`mn_after` ignoring `w==0` (init 0xfff), `mx_after`. Finally
`avg_before = sum_before/768`, `min_before`, `max_before`,
`avg_after = sum_after>>10`, `min_after`, `max_after`.

**`merge_ae(ctx,b0,b1)`** — gate `ae_en`; `win_pix_n = n>>sh` as above. Per output
window `i`: `row=i/24`, `col=i%24`; if `col<=11` source is `b0` at half-index
`(row*12+col)*24`, else `b1` at `(row*12+(col-12))*24`; each half-window is
12 bytes of r,g,b followed by another 12; `r=(u32(src+0)+u32(src+12))>>1`,
`g=(u32(src+4)+u32(src+16))>>1`, `b=(u32(src+8)+u32(src+20))>>1`; luma as above.
Histogram: `hist[i] = (u32(h0+(i>>1)*4)+u32(h1+(i>>1)*4))>>1` for 256 bins.

**`merge_awb(ctx,b0,b1)`** — gate `awb_en`. Per window
`use1=(i&16)!=0`, `idx=(i/32)*16+(i&15)`; source half `p1`/`p0` at `idx*24`, count
half `c1`/`c0`. `r/g/b` averaged as in merge_ae; `c = (u16(cntp+idx*4+2)+u16(cntp+idx*4))>>1`.
Then the same ratio/sum/avg arithmetic as `handle_awb` in the **non-WDR** branch
(there is no WDR branch in merge).

**`merge_afs(ctx,b0,b1)`** — gate `afs_en`; `sum[i]=(u32(s0+4i)+u32(s1+4i))>>1`;
pic copy.

**`merge_pltm(ctx,b0,b1)`** — gate `pltm_en`;
`lst[i]=(u16(p0+2i)+u16(p1+2i))>>1`.

### 3.2 `__isp_stat_dynamic_judge(ctx)`
NULL ctx → return. If `!dynamic_stats.enable || !ae_en`: zero the six
`*_comp_target` fields, return.

Otherwise with `d=dynamic_stats`, `accum` plane and `accum_last1/2/3`:
- `p1[i]=|accum[i]-accum_last1[i]|`, same for p2/p3 with last2/last3 (32-bit signed
  absolute difference of the unsigned values via `(int32_t)(a-b)`);
- `sum += win_pix_n ? accum[i]/win_pix_n : 0` (u32);
- `f_k = min_filter_sum(p_k)`: 3×3 minimum filter summed over the interior
  `r in 0..13`, `c in 0..21` (i.e. `r+2<16`, `c+2<24`) — signed per-window minimum,
  `int32_t` accumulator (may wrap);
- `minval = min(f1,f2,f3)` comparing as `uint32_t`;
- shift history: `mov_save[i]=mov_save[i+1]` for i<6;
- `t = (int32_t)(25u * sum)`; if `t >= 142080` append `minval`; else
  `q=t/3840` (if q<0 q=0), `t = (36 * minval)/(q+1)`, if `t<minval` t=minval,
  if `4*minval < t` t=`4*minval`; append `t` (store as u32);
- `med = median7(mov_save)` = element 3 of the ascending sort of the 7 entries
  (values cast to `int32_t`);
- `low = med/4`; if `low<6160` low=6160; `low = mov_old - low`;
  `high = 2*med`; if high>2464 high=2464; if high<308 high=308; `high += mov_old`;
  `t = max(med,low)`; `mov = min(high,t)`; `d->mov = mov`;
- thresholds `th[i]=mov_th[i]` (signed);
- for each of the six coefficient arrays `comp[i]` and targets `target[i]`:
  ```
  if (mov <= th[0]) v = ca[0];
  else if (mov <= th[1]) v = VI(mov, th[0], th[1], ca[0], ca[1]);
  else if (mov <= th[2]) v = VI(mov, th[1], th[2], ca[1], 0);
  else if (mov <= th[3]) v = 0;
  else if (mov <= th[4]) v = VI(mov, th[3], th[4], 0, ca[2]);
  else if (mov <= th[5]) v = VI(mov, th[4], th[5], ca[2], ca[3]);
  else v = ca[3];   *target[i] = v;
  ```
  coefficient order: tdnf, tdnf_diff, lp_th_ratio, sharp_hfrq, sharp_edge,
  sharp_under_shoot;
- if `af_en`: `af_param.mov = mov/308` (signed division);
- `d->mov_old = mov`.

`VI(curr,xlo,xhi,ylo,yhi)` = `(xhi-xlo)==0 ? ylo : ylo + (yhi-ylo)*(curr-xlo)/(xhi-xlo)`,
C truncation toward zero.

### 3.3 `isp_apply_settings(ctx)`
NULL ctx → return. Set `ctx->ae_entity_ctx.ae_param = &ctx->ae_param`. Let
`flags = ctx->isp_3a_change_flags`. For bit = 0..11, if `flags & (1u<<bit)`:
perform the action below, then `ctx->isp_3a_change_flags &= ~(1u<<bit)`.

| bit | flag | action |
|----:|------|--------|
| 0 | SCENE 0x001 | `isp_ae_set_params_helper(&ae_entity_ctx, 1)` only. **It does not copy the tune gains** (the vendor jump target is the helper call, after the copy block). |
| 1 | AWB 0x002 | `set_awb_win`; `awb_frame_cnt=0`. |
| 2 | FLICKER 0x004 | if `afs_param` non-NULL: `afs_param->flicker_mode = ae_settings.flicker_mode`; NULL → skip (vendor logs) **[blind]**. |
| 3,4,5,9 | SHARPNESS/BRIGHTNESS/SATURATION/CONTRAST | cleared, no action. |
| 6 | EFFECT 0x040 | build `module_cfg.rgb2yuv` from sensor colour-space base matrix and the selected effect (see below). |
| 7 | AF_METERING 0x080 | `set_af_win` (spot → 1×1 normalised window; else 24×16 grid); `af_frame_cnt=0`. |
| 8 | AE_METERING 0x100 | see below; `ae_frame_cnt=0`. |
| 10 | HUE 0x400 | build the hue-rotated rgb2yuv matrix (see below). |
| 11 | GAIN_STR 0x800 | `set_ae_ini_from_gains` (copy tune gains into `ae_param.ae_ini`: `gain_favour`, analog min/max, `digital_gain_min/max<<2`), then `isp_ae_set_params_helper(&ae_entity_ctx, 1)`. |

**`set_ae_ini_from_gains`**: `ae_ini.gain_favour=gains.gain_favour`,
`analog_gain_min/max`, `digital_gain_min/max = gains.digital_gain_min/max << 2`.

**`set_awb_win`**: `win.hor_num=32`, `win.ver_num=32`. From
`ae_settings.awb_coor[0..3]` (normalised −1000..1000), picture `w,h`:
`x1=w*(c0+1000)/2000`, `y1=h*(c1+1000)/2000`, `x2=w*(c2+1000)/2000 - x1`,
`y2=h*(c3+1000)/2000 - y1`; `nw=clamp(x2>>5,4,0x70)`, `nh=clamp(y2>>5,4,0xa8)`;
`x1 += (x2 - nw*32)>>1`, `y1 += (y2 - nh*32)>>1`;
`hor_start = min(x1,w)`, `ver_start = min(y1,h)`. Width/height set to nw/nh.

**`set_ae_win`**: `ae_reg_win.hor_num=24`, `.ver_num=16`. If `ae_spot`:
`x1=w*(c0+1000)/2000` … `x2=w*(c2+1000)/2000`, `y2`; `nw=clamp((x2-x1)/24,4,0x70)`,
`nh=clamp((y2-y1)>>4,4,0xa8)`; store size; `x2 -= nw*24`, `y2 -= nh*16`;
`hor_start=min(x2,w)`, `ver_start=min(y2,h)`. Else `width=w/24`, `height=h>>4`,
`hor_start=(w%24)>>1`, `ver_start=(h&0xf)>>1`. Always set `hist_reg_win` to
1×1, width=w, height=h, starts 0.

**`set_af_win`**: if `af_spot`: `hor_num=ver_num=1`, `width=x2-x1`, `height=y2-y1`,
`hor_start=x1`, `ver_start=y1` (x1=east from `af_coor`, formula as above). Else
`hor_num=24`, `ver_num=16`, `width=w/24`, `height=h>>4`, starts 0.

**`build_rgb2yuv_effect`**: load base matrix `A` and offset `B` for
`clamp(sensor_info.colour_space,0,5)` from `rgb2yuv_base`: `A[i][j]=tbl[3i+j]/1024.0`,
`B[i]=tbl[9+i]/512.0`. Choose the 3×3 effect gain `C` and offset `D` by
`effect=clamp(isp_ini_cfg.color_effect,0,6)`:
0 none `I,0`; 1 gray `[[.3,.6,.1]×3],0`; 2 negative `−I,[2,2,2]`;
3 antique `[[.393,.769,.189],[.349,.686,.168],[.272,.534,.131]],0`;
4 R `[[1.5,0,0],[0,2/3,0],[0,0,2/3]],0`;
5 G `[[2/3,0,0],[0,1.5,0],[0,0,2/3]],0`;
6 B `[[2/3,0,0],[0,2/3,0],[0,0,1.5]],0`.
Then `M=A·C` (double), `O[i]=Σ_j A[i][j]*D[j] + B[i]`; store
`yuv->gain[i][j] = enc_matrix(M[i][j])`, `yuv->offset[i] = enc_offset(O[i])`.

**`enc_matrix(v)`** = `clamp((int32_t)(v*1024.0), −1024, 1023)` stored `int16_t`.
**`enc_offset(v)`** = `clamp((int32_t)(v*512.0), −1024, 1023)` stored `int16_t`.
(The cast to `long`/`int32_t` truncates toward zero **before** clamping; this is
the "signed clamp" trap.)

**`build_rgb2yuv_hue`**: `ang = (double)hue_level * 3.1415926 / 180.0` — the
**truncated π constant `3.1415926`**, not full precision. `c=cos(ang)`, `s=sin(ang)`;
`J=[[1,0,0],[0,c,s],[0,−s,c]]`; hue offset `[0,(1−c)−s,s+(1−c)]`; load base A/B;
`M=J·A`, `O=J·B + hue offset`; encode as above.

**AE metering (bit 8)** uses `ae_entity_ctx.ae_param`; if non-NULL and `ae_spot`:
`ae_param.ae_setting = ae_settings` (struct copy), then helper(3), then the same
spot AE/hist window as `set_ae_win`; NULL `ae_param` → skip the spot branch; then
`set_ae_win` (full grid when not spot). `ae_frame_cnt=0` always.

### 3.4 `isp_apply_colormatrix(ctx)` + the colour-matrix builder
NULL → return. Calls `config_color_matrix`, then the defog pass.

**`config_color_matrix`**: `temp = (isp_test_mode==0)? awb_color_temp_output :
isp_test_settings.isp_color_temp`. `curr=clamp(temp,2500,7000)`. `sel=(temp<4000)?1:2`.
If injected `color_matrix` and `color_temp` are present:
`xlo=color_temp[sel-1]`, `xhi=color_temp[sel]`, `blo=&color_matrix[(sel-1)*12]`,
`bhi=&color_matrix[sel*12]`; for `i=0..11` (0..8 matrix, 9..11 offset)
`slot[i]=(uint16_t)(int16_t)VI(curr,xlo,xhi,(int16_t)blo[i],(int16_t)bhi[i])`.
If absent, slots are 0. `sh = 16 - ae_param.comanding_output_bits`.
- `wdr_en==0`: if `cm_en==0` → identity matrix diag 0x100, offsets 0; else keep.
- `wdr_en!=0`: if `cm_en!=0` → each of the 12 slots `= (uint16_t)(int16_t)((int16_t)slot << sh)`;
  else identity diag `0x100<<sh`, offsets 0.

**defog pass**: if `!defog_en || defog_value<4` return.
`nz = (int32_t)stat.min_rgb_saved` (clamp <0→0); if `defog_value<=nz` then `nz=defog_value`.
`pre = defog_ctx.min_rgb_pre[0]`;
if `pre+0x10 < nz` → `pre+=6`, `defog_changed=1`;
else if `nz < pre−0x10` → `pre−=6`, `defog_changed=1`; else `defog_changed=0`.
If `pre<1` pre=1; if `defog_value<=pre` pre=defog_value.
If `ae_settings.flash_open==1`: `nz=stat.min_rgb_saved`, `if nz<1 nz=1`, `pre=nz`,
`if defog_value*2 < pre` then `pre=defog_value*2`.
If `ae_result.ae_gain != 0x100`: `pre = defog_ctx.defog_pre`.
`scale = 0x3ff00 / (0x3ff - pre)` (division by zero if pre==0x3ff).
For each row: `matrix[row][c] = (uint16_t)(int16_t)((scale * (int16_t)matrix[row][c]) >> 8)`;
`offset[row] = (uint16_t)(int16_t)(((int16_t)((scale*pre)>>8)) * -4)`.
`defog_ctx.min_rgb_pre[0]=pre`; `defog_ctx.defog_pre=pre`.

### 3.5 `config_dig_gain(ctx, exp_digital_gain)`
Gate: `dig_gain_en || wdr_en`, else return.
1. For i=0..3: `g[i] = (uint16_t)((bayer_gain[i] * exp_digital_gain + 0x200) >> 10)`;
   `sensor_info.gain_offset[i] = g[i]`.
2. If `linear_en && tables && tables->linear`: memcpy `linear_table` from
   `tables->linear` (768 u16).
3. If `wdr_en && !ae_param.nor_cmd_mode`: for i=0..3
   `g[i] = (uint16_t)(uint32_t)sqrt((double)g[i] * 1024.0)`.
4. If `sensor_info.so_en`: `d = ((int16_t)gain_offset_cfg.sensor_offset[1] >> 4) + 0x100`;
   for i: `g[i] = (uint16_t)((g[i]*0x100 + d/2)/d)`.
5. If `sensor_info.blc_en`: same with `gain_offset_cfg.offset[1]`.
6. If `linear_en && wdr_en && awb_en && !wb_en`:
   `g[0] = (g[0]*wb_gain[0] + 0x80)>>8`, `g[3] = (g[3]*wb_gain[3] + 0x80)>>8`.
7. `module_cfg.gain_offset_cfg.gain[i] = g[i]`.
No register write and no `table_update` bit. `d` can be 0 (div by zero) for an
extreme negative sensor offset.

### 3.6 `config_wdr(ctx, flag)`
`t = tables()`, `in_bits=ae_param.comanding_input_bits`, `out_bits=ae_param.comanding_output_bits`.

**A. Commanding path** — if `mode_cfg.wdr_mode==1` **and** the injected WDR
tuning table is "zero" (u16 entries `0`, `0x1000`, `0x1ffe` all 0; a missing
`wdr_table` counts as zero): set `ae_param.nor_cmd_mode=1`; if `flag==0 ||
frame_cnt>99` return. Generate:
- `table[0]=0`; `table[k] = k >> (12-in_bits)` for k=1..0xffe; `table[0xfff]=
  (1<<in_bits)-1`;
- for `k=0..0xfff`: `table[0x1000+k] = (uint16_t)(((1<<out_bits)-1) * sqrt(k/4095.0))`;
- copy `t->anti_gamma` (4096 u16) into `ctx->anti_gamma_tbl`; return.

**B. WDR disabled** — else if `!wdr_en`: `ae_settings.ae_mode = 0`; return.

**C. Normal WDR** — compute from `ae_result`:
`hi=wdr_hi_th`, `isp_hw=wdr_ratio_isp_hw`; if `wdr_ratio_sensor==0` isp_hw=0x100;
if `wdr_ratio_tmp==0` isp_hw=0x100; if isp_hw==0 isp_hw=0x100;
`hi=hi/isp_hw`; `lo=(uint32_t)wdr_low_th/isp_hw`; store back to `ae_result`;
`exp_ratio=(uint16_t)(0x100000/isp_hw)`; `cfg->hi_th=(uint16_t)hi`,
`cfg->exp_ratio=exp_ratio`; if `iso_cem_color2gray_th <= iso_lum_idx` then
`cfg->hi_th = exp_ratio`; `cfg->lo_th=(uint16_t)lo`;
`cfg->slope=(uint16_t)(0x10000 / ((cfg->hi_th>>4)+1 - (cfg->lo_th>>4)))`;
`cfg->mv_th=0xc00`; `cfg->mv_scale=0x3c`;
`cfg->out_sel=(uint16_t)ae_settings.wdr_output_select`. If `flag==0` return.
- If the tuning table is non-zero: memcpy it into `cfg->table` (0x4000 bytes),
  copy anti_gamma, return.
- Else generate first half (indices 0..0xfff):
  - `wdr_mode==0`: for `k=0; k<0x10000; k+=16` `table[k>>4]=(uint16_t)sqrt((double)(k<<8))`.
  - else (only reachable if `wdr_mode` is neither 0 nor 1): for `k=0..0xfff`,
    `x = k<0x800 ? k : (k<=0xbe0 ? k*64-0x1f800 : k*1024-0x2e8000)`;
    `table[k]=(uint16_t)sqrt((double)(x<<4))`. **Unreachable for the deployed
    state machine (wdr_mode ∈ {0,1}).**
- second half: for `k=0; k<0xfff0; k+=16` `table[0x1000+(k>>4)] = k >> (16-out_bits)`;
  `table[0x1fff]=(1<<out_bits)-1`.
- copy anti_gamma.

### 3.7 `config_gamma(ctx)`
Requires `tables` and `t->gamma_base`. `trig = isp_ini_cfg.gamma_trig_cfg[0]!=0 ?
gamma_trig_cfg : t->gamma_trig`; NULL → return. `lv=sensor_info.ae_lv`;
`idx` = first i in 0..3 with `lv>=trig[i]`, else 4. If `idx==0`:
`down=trig[0]`, `up=trig[1]`, `a=gamma_base`, `b=a`; else `down=trig[idx-1]`,
`up=trig[idx]`, `a=gamma_table(idx)`, `b=gamma_table(idx-1)` where
`gamma_table(k)= k==0?gamma_base : gamma_sub[k-1]`. If `a==NULL` return.
If `up<down`: for i in 0..3071 `out[i] = (uint16_t)VI(lv,down,up, b?b[i]:a[i], a[i])`;
else `out[i]=a[i]`.
Then if `wdr_en`: `tmp[1024]`; if `!gamma_en`: for i<1024
`out[i]=(uint16_t)(( (double)i/1023.0)^2 * 4095.0)`; else
`tmp[i]=out[(i*i)/0x3ff]` then `memcpy(out,tmp,1024*2)`. Then replicate:
`memcpy(out+0x400, out, 0x800)` and `memcpy(out+0x800, out, 0x800)` (each 0x800
bytes = 1024 u16).

### 3.8 `config_lens_table(ctx, vcm_std_pos)`
Gate `lsc_en` and `tables` non-NULL. Output `module_cfg.lens_table[1024]` u16.
Temperature helper: `trig = lsc_trig_cfg[0]!=0 ? lsc_trig_cfg :
(tb->lsc_trig_def ? tb->lsc_trig_def : tb->lsc_trig)`; NULL → return;
`t = clamp(temp, trig[0], trig[5])`; `idx` = first i in 0..5 with `t<trig[i]`, else 5;
`j = idx?idx-1:0`. Temperature source depends on path.

- **`ff_mod==2`**: temp = `awb_color_temp_output` (or `isp_color_temp` in test
  mode); `down=tb->lsc[j]`, `up=tb->lsc[j+1]`, `lo=trig[j]`, `hi=trig[j+1]`,
  `s=lsc_mode&31`. `lens_build_shifted`:
  - if `hi-lo < 1`: `lens[4i+ch] = (uint16_t)(down[ch*256+i] >> s)` for ch 0..2;
  - else `lens[4i+ch] = VI(t,lo,hi, down[ch*256+i]>>s, up[ch*256+i]>>s)`.
- **`ff_mod==1`**: temp = `awb_color_temp_output`/`isp_color_temp`; build as above
  with `s=lsc_mode`; then `roll=rolloff_ratio`, `low_gain=(roll&0xfff)<<4`,
  `high_gain=((roll&0xffffff)>>12)<<4`, `anagain=ev_analog_gain`. If
  `high_gain>=low_gain`: `g=0x400>>(s&31)`; for i 0..255, ch 0..2:
  if `high_gain<anagain` → `lens[4i+ch]=g`; else if `low_gain<anagain` →
  `lens[4i+ch]=VI(anagain, low_gain, high_gain, lens[4i+ch], g)`.
- **else (`ff_mod==0`)**: temp = `awb_color_temp_output`/`isp_color_temp`;
  `ad=tb->lsc[j]`, `au=tb->lsc[j+1]`, `bd=tb->lsc[j+6]`, `bu=tb->lsc[j+7]`;
  `vcm=clamp(vcm_std_pos,0,0x3ff)`.
  - If `hi-lo < 1`: for `i=0..767` **contiguously** `lens[i]=VI(vcm,0,0x3ff, ad[i], bd[i])`.
  - Else: `pos1[i]=VI(t,lo,hi,ad[i],au[i])`, `pos2[i]=VI(t,lo,hi,bd[i],bu[i])` for
    i<768; then for i<256: `lens[4i+0]=VI(vcm,0,0x3ff,pos1[i],pos2[i])`,
    `lens[4i+1]=VI(vcm,0,0x3ff,pos1[i+256],pos2[i+256])`,
    `lens[4i+2]=VI(vcm,0,0x3ff,pos1[i+512],pos2[i+512])`.
- **WDR post-pass** (all paths): if `wdr_en && ae_entity_ctx.ae_param && !ae_param->nor_cmd_mode`:
  for i<256, ch 0..2 `lens[4i+ch] = (uint16_t)(uint32_t)sqrt((double)lens[4i+ch] * 1024.0)`.

### 3.9 `config_lens_center(ctx)`
`pw=pic_w`, `ph=pic_h`; if either 0 return. `cx=(lsc_center_x*pw)/4096`,
`cy=(lsc_center_y*ph)/4096` (signed product, truncate toward zero). `dxw=cx-pw`,
`dyh=cy-ph`; squared corner distances `d1=dxw²+dyh²`, `d2=dxw²+cy²`,
`d3=cx²+cy²`, `d4=cx²+dyh²` (each term computed in `int32_t` then cast to u32;
overflow wraps). `m=max(d1..d4)`. `lens_cfg.ct_x=(uint16_t)cx`, `.ct_y=(uint16_t)cy`.
For `s=0..19`: if `(m>>s) < 0x100` then `rs_val=s` and break; if none qualifies
`rs_val` is **left at its previous value**. Mirror `ct_x/ct_y/rs_val` into
`disc_cfg.disc_ct_x/ct_y/rs_val`.

### 3.10 `config_msc_table(ctx, vcm_std_pos)`
Gate `msc_en` and `tables` non-NULL. `out = module_cfg.msc_table`. If
`mff_mod==2` → FF helper, else AF helper with `vcm_std_pos`.
`N = (msc_mode<4) ? 256 : 484`. Temperature source is always
`awb_color_temp_output` (or `isp_color_temp`); trigger default is supplied
through the injected table contract (`msc_trig_cfg[0]!=0 ? msc_trig_cfg :
(tb->msc_trig_def ? tb->msc_trig_def : tb->msc_trig)`); `t`, `idx`, `j`,
`tlo=trig[j]`, `thi=trig[j+1]`.
`src` index for component c and position i: `src = (msc_mode<4) ? i + 6*(i>>4) : i`.

**Component layout differs by helper:** AF uses component stride `N` in the
injected `msc` rows (component c at `row[c*N + src]`); FF uses fixed stride
`484` (`row[c*484 + src]`) but golden-ratio stride `n` (`golden[c*n+i]`).

**AF helper:**
- `adj = (float)VI(t,tlo,thi, (int)msc_adjust_ratio[j], (int)msc_adjust_ratio[j+1])`;
  `c100 = (float)VI(t,tlo,thi,100,100)`.
- degenerate `thi-tlo<1`: for each i, `v0=VI(vcm,0,0x3ff, row_lo[src], row2_lo[src])`,
  `v1` with `+N`, `v2` with `+2N`; correction as below.
- else `pos1[p]=VI(t,tlo,thi,row_lo[p],row_hi[p])`,
  `pos2[p]=VI(t,tlo,thi,row2_lo[p],row2_hi[p])` for `p<3N` (scratch persists across
  calls); then `v0=VI(vcm,0,0x3ff,pos1[src],pos2[src])`, `v1` at `src+N`,
  `v2` at `src+2N`.
- correction per i: `g0=msc_golden_ratio[i]`; if `msc_golden_flag[i]==1`:
  `s=(double)(msc_r_ratio-1.0f)`, `d=cref[i]*(double)(adj/100.0f)`; else
  `s=(double)(1.0f-msc_r_ratio)`, `d=cref[i]*(double)(c100/100.0f)`;
  `out[4i+0]=(uint16_t)(uint32_t)((double)((float)v0*g0) * (1.0 - d*s))`;
  `out[4i+1]=(uint16_t)(uint32_t)((float)v1 * msc_golden_ratio[N+i])`;
  `out[4i+2]=(uint16_t)(uint32_t)((float)v2 * msc_golden_ratio[2N+i])`.
  (`cref` is the injected 256-double comp_ref.)

**FF helper:**
- `adj=(float)VI(t,tlo,thi, (int)msc_adjust_ratio[j], …)`;
  `less=(float)VI(t,tlo,thi, (int)msc_adjust_ratio_less[j], …)`.
- degenerate: `out[4i+c]=(uint16_t)((float)row_lo[src + c*484] * golden[c*N+i])`, c 0..2.
- else `v_c=VI(t,tlo,thi, row_lo[src+c*484], row_hi[src+c*484])`; correction:
  flag==1 → `s=(double)(msc_r_ratio-1.0f)`, `d=cref[i]*(double)(adj/100.0f)`;
  else `s=(double)(1.0f-msc_r_ratio)`, `d=cref[i]*(double)(less/100.0f)`;
  emit as in the AF helper.
  No VCM interpolation in the FF path.

**WDR post-pass** (both): if `wdr_en && ae_param && !nor_cmd_mode`: for i<256,
c 0..2 `out[4i+c]=(uint16_t)(uint32_t)sqrt((double)out[4i+c] * 1024.0)`.

> **Spec correction (2026-09-16).** Sections 3.8 and 3.10 previously prescribed
> the platform's default MSC trigger thresholds as a literal table. That
> recompiled vendor data into the shipped source (the release-gate R3
> regression); the defaults are now supplied through the injected
> `base_tables_t.lsc_trig_def` / `msc_trig_def` contract, as written above.
> Corrected after an independent verifier found the regression.

### 3.11 `config_band_step(ctx)`
`inc = clamp(pic_h>>7, 1, 63)`; store `module_cfg.afs_cfg.inc_line = inc`; then
program the anti-flicker line increment with that value:
`isp_reg_set_afs_anti_flick(ctx->isp_dev_id, inc)`.

> **Spec correction (2026-09-16).** An earlier revision of this section claimed the
> vendor object performs no register write here and that an implementation must not
> call `isp_reg_set_afs_anti_flick`. That claim was wrong, and contradicted §6 of
> this same document (which lists the `config_band_step` register write as a true
> vendor behaviour the differential cannot observe). The register write is part of
> the deployed behaviour; it is differential-blind, so only the unit tests and the
> binary-derived record attest to it.

### 3.12 `base.c` traps
- **4-stride lane-3 writes.** `config_lens_table` (non-degenerate branches of all
  three `ff_mod` paths) and `config_msc_table` write only `[4i+0..2]`; lane
  `[4i+3]` is never written. The one exception is `config_lens_table` with
  `ff_mod==0` and a degenerate temperature range, which writes `lens[0..767]`
  contiguously (lane-3 positions included). Seed lane 3 with a sentinel on both
  sides of any differential.
- **Truncated π.** The hue path uses the literal double `3.1415926` (not `M_PI`);
  the last bits of `cos`/`sin` and therefore some Q10/Q9 entries differ by ±1.
- **Signed clamps.** `enc_matrix`/`enc_offset` compute `(long)(v*1024.0)` /
  `(long)(v*512.0)` (truncation toward zero) and then clamp to `[-1024,1023]`
  before storing `int16_t`. The defog matrix/offset multiplications use `int16_t`
  intermediates and arithmetic `>>8`; they are not clamped.
- **Unreachable branch.** `config_wdr`'s first-half `wdr_mode!=0` x-transform is
  dead for the deployed state machine (`wdr_mode` is 0 or 1 and the `mode==1`
  zero-tuning case returns in the commanding path) **[blind]**.
- **`config_gamma` middle branch** (`[0xbff]==0`, `[0x2ff]!=0`) is implemented by
  the object but not covered by the differential **[blind]**; §2.3 case 3 gives
  its formula.
- **`isp_apply_settings` bits 0 and 11 are not the same.** Bit 0 (SCENE) only
  calls helper(1); bit 11 (GAIN_STR) first copies `tune.gains` into
  `ae_param.ae_ini` and then calls helper(1). A naive implementation that
  shares one case body will diverge.
- **`isp_reg_enable_msc` disable** clears CONTRAST 0x400000 **[blind]**.
- Division-by-zero edges: `handle_ae`/`merge_ae` (`win_pix_n==0`), `handle_awb`
  (guarded by `c!=0`), `config_lens_center` (`pw/ph==0` returns; `rs_val`
  unqualified otherwise), `config_dig_gain` (`d==0`), `isp_apply_colormatrix`
  (`pre==0x3ff`), `config_wdr` slope denominator (`hi>>4)+1 == lo>>4` → division
  by zero).
- `config_msc_table` AF helper with `msc_mode>=4`: the vendor reads `comp_ref`
  beyond its 256 entries up to index 483 (out-of-frame over-read) **[blind]**: the
  existing differential neutralises the term (`msc_r_ratio==1.0`) and therefore
  does not certify it. For the 256-entry mode the access is in range.
- `config_lens_center` leaves `rs_val` untouched when no shift in 0..19
  qualifies; `disc_cfg.disc_rs_val` mirrors whatever value that is.

---

## 4. Coverage checklist

`reg_writers.c` (69 writers + the descriptor filler): `isp_reg_map_load_addr`, `isp_reg_set_input_fmt`,
`isp_reg_update_table`, `isp_reg_top_control`, `isp_reg_module_enable`,
`isp_reg_module_disable`, `isp_reg_set_wdr_compress_mode`,
`isp_reg_set_saturation_mode`, `isp_reg_set_cfa_mode`, `isp_reg_set_dg_mode`,
`isp_reg_set_ae_mode`, `isp_reg_set_lsc_mode`, `isp_reg_set_msc_mode`,
`isp_reg_set_awb_mode`, `isp_reg_set_hist_src`, `isp_reg_set_hist_mode`,
`isp_reg_set_dpc_mode`, `isp_reg_set_d3d_mode`, `isp_reg_set_af_mode`,
`isp_reg_set_blc_offset`, `isp_reg_set_wdr_cfg`, `isp_reg_set_dpc`,
`isp_reg_set_ctc`, `isp_reg_set_gca`, `isp_reg_set_lca`, `isp_reg_set_d2d_cfg`,
`isp_reg_set_d3d_cfg`, `isp_reg_set_sensor_offset`, `isp_reg_set_dg_gain`,
`isp_reg_set_wb_gain`, `isp_reg_set_wb_clip`, `isp_reg_set_lsc`,
`isp_reg_set_pltm_cfg`, `isp_reg_set_cfa`, `isp_reg_set_sharp`,
`isp_reg_set_rgb2rgb_gain_offset`, `isp_reg_set_cnr`, `isp_reg_set_saturation`,
`isp_reg_set_dehaze`, `isp_reg_set_rgb2yuv_gain_offset`, `isp_reg_set_ae_win`,
`isp_reg_set_af_en`, `isp_reg_set_af_win`, `isp_reg_set_af_filter`,
`isp_reg_set_awb_satur_lim`, `isp_reg_set_awb_win`, `isp_reg_set_hist_win`,
`isp_reg_set_afs_anti_flick`, `isp_reg_set_d3d_lum_th_lut`,
`isp_reg_set_d3d_bright_th_lut`, `isp_reg_set_d3d_ref_noise_lut`,
`isp_reg_set_d3d_k_lut`, `isp_reg_set_d3d_k_delta_lut`,
`isp_reg_set_sharp_val_lut`, `isp_reg_set_sharp_edge_lum_lut`,
`isp_reg_set_sharp_hfrq_lum_lut`, `isp_reg_set_sharp_hsv_lut`,
`isp_reg_set_sharp_s_map_lut`, `isp_reg_set_d2d_lp0_np_lut`,
`isp_reg_set_d2d_lp1_np_lut`, `isp_reg_set_d2d_lp2_np_lut`,
`isp_reg_set_d2d_lp3_np_lut`, `isp_reg_set_af_square_lut`,
`isp_reg_set_msc_blw_lut`, `isp_reg_set_msc_blh_lut`,
`isp_reg_set_msc_blw_dlt_lut`, `isp_reg_set_msc_blh_dlt_lut`,
`isp_reg_set_lca_pf_satu_lut`, `isp_reg_set_lca_gf_satu_lut`.
Plus the non-vendor helper `isp_reg_set_dg_bypass` used by the DG enable routine.

`module_cfg.c` (2 + 31 + 29): `isp_map_addr`, `isp_hardware_update`;
`isp_reg_prepare_{afs,sharpness,contrast,d2d,rgb_drc,pltm,wdr,cem,lens,gamma,rgb2yuv,
rgb2rgb,ae_win,af,awb,hist,blc,wb_gain,dpc,cfa,d3d,cnr,saturation,linear,sensor_offset,digital_gain,ctc,
mode,msc,lca,gca}`;
`isp_reg_enable_{afs,sharpness,contrast,d2d,rgb_drc,pltm,wdr,cem,lens,gamma,rgb2yuv,
rgb2rgb,ae,af,awb,hist,blc,wb_gain,dpc,d3d,cnr,saturation,linear,sensor_offset,
digital_gain,ctc,msc,lca,gca}`. (CFA and MODE have no enable; there are **29**
enable functions, not 30 as an earlier brief stated.)

`base.c` (12): `config_band_step`, `config_lens_center`, `config_dig_gain`,
`config_gamma`, `config_wdr`, `config_lens_table`, `config_msc_table`,
`isp_apply_colormatrix`, `__isp_stat_dynamic_judge`, `isp_handle_stats`,
`isp_handle_stats_sync`, `isp_apply_settings`.
Required internal helpers: the colour-matrix builder, the value-interpolation
helper, the LSC interpolation-index helper, the AE/AWB/AF/AFS/PLTM handlers and merge variants,
history rotation, `stats_attach`, `min_filter_sum`, `median7`.

---

## 5. How to verify

The private differential harness links the deployed object (symbol-prefixed)
beside the candidate under `qemu-arm` and compares observable words. A pass is
exact equality on every observed word plus a detected sensitivity bump. The
harness and its recorded vectors are withheld from the published tree.

The harness provides three suites — `reg`, `reg_cfg` and `base` — each writing
its result to the withheld golden vectors (`diff_reg.txt`, `diff_module_cfg.txt`
and `diff_base.txt` respectively).

- `reg`: exits 0 iff the report contains `reg sensitivity: detected`.
  Current record: **680 cases, 0 mismatches**.
- `reg_cfg`: exits 0 iff the report contains
  `reg_cfg sensitivity: detected`. Current record: **1481 cases, 0 mismatches,
  0/70 routines**.
- `base`: the harness's own exit status is authoritative. It must be 0 and
  the report must contain `0 with mismatches, 0 word(s) differ`,
  `== base deferred (absent from this tier): none ==`, and
  `base sensitivity: detected`. Current record: 12 ops + the config_wdr (7),
  `isp_apply_colormatrix` (12), `__isp_stat_dynamic_judge` (12),
  `isp_apply_settings` (12), `config_lens_table` (20) and `config_msc_table` (22)
  variants, 0 mismatches, both deferral lines `none`.
- qemu-arm is expected on the harness host, and the harness Makefiles build the
  `*.bin` binaries under each `build/` dir.
- Optional link-level validation: the `link_a8` suite checks
  the clean objects satisfy the vendor archive's register-tier entry-point
  references and are selected ahead of the replaced members.

A candidate that passes all three has reproduced the verified behaviour. Nothing
under §3.12/§2.5 marked **[blind]** is certified by these commands.

---

## 6. Not determined here / residual uncertainty

- The exact **rounding of the `config_gamma` middle branch** (§2.3 case 3) is
  inferred from the object; the differential does not exercise it. An implementer should
  keep it behind the same probe so it cannot affect the verified paths.
- The **MSC AF `comp_ref` over-read** for `msc_mode>=4` cannot be reproduced
  portably (it reads stack bytes past a 256-entry array). The differential does not
  certify it; the spec describes the in-range `msc_mode<4` behaviour exactly and
  flags the `>=4` correction term as unverified.
- The **`isp_reg_enable_msc` disable-clear**, **`isp_apply_settings` bit-0 gains copy**
  and **`config_band_step` register write** are true vendor behaviours that the
  current differential cannot observe (§3.12/§2.5). They are stated for fidelity.
- `reg_writers.c`'s `isp_reg_set_dg_bypass` is a clean-room helper, not a vendor
  symbol; its register effect is reconstructed from `isp_reg_enable_digital_gain`.
- Vendor log/format strings and comments are intentionally omitted; no behaviour in
  these TUs was found to depend on them.
