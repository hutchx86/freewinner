/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* test_fisp_abi.c - target-ABI conformance for the fisp_ surface; a
 * mismatch fails the build via the SA typedefs. */

#include "isp_dev_uapi.h"
#include "framework_events.h"

#include <stddef.h>

#define SA(cond, tag) typedef char sa_##tag[(cond) ? 1 : -1]

/* fwi_table_reg_map is 8 bytes (offset(size)=4) only on the ILP32 target; with
 * an 8-byte host pointer only "addr first" is arch-invariant, so only that. */
SA(offsetof(struct fwi_table_reg_map, addr) == 0, fwi_table_reg_map_addr);

/* Kernel control ABI ids. */
SA(V4L2_CID_BASE == 0x00980900, cid_base);
SA(V4L2_CID_BRIGHTNESS == 0x00980900, cid_brightness);
SA(V4L2_CID_CONTRAST == 0x00980901, cid_contrast);
SA(V4L2_CID_SATURATION == 0x00980902, cid_saturation);
SA(V4L2_CID_EXPOSURE == 0x00980911, cid_exposure);
SA(V4L2_CID_GAIN == 0x00980913, cid_gain);
SA(V4L2_CID_POWER_LINE_FREQUENCY == 0x00980918, cid_plf);
SA(V4L2_CID_SHARPNESS == 0x0098091b, cid_sharpness);
SA(V4L2_CID_EXPOSURE_AUTO == 0x009a0901, cid_exposure_auto);
SA(V4L2_CID_EXPOSURE_ABSOLUTE == 0x009a0902, cid_exposure_absolute);
SA(V4L2_CID_AUTOGAIN == 0x00980912, cid_autogain);
SA(V4L2_CID_EXPOSURE_BIAS == 0x009a0913, cid_bias); /* renamed from the vendor
    V4L2_CID_AUTO_EXPOSURE_BIAS spelling; framework_events.h's name, same value */
SA(V4L2_CID_EXPOSURE_METERING == 0x009a0919, cid_metering);

/* Load-register ABI. */
SA(ISP_LOAD_DRAM_SIZE == 0x13240, load_dram_size);
/* VIDIOC_VIN_ISP_LOAD_REG embeds sizeof(fwi_table_reg_map): 0xc0085706 on
 * ARM, 0xc0105706 on a 64-bit host, so it is not asserted here. */

/* Number of compile-time ABI facts asserted above. */
int fisp_abi_model_checks(void)
{
	return 15;
}
