/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/*
 * test_fisp_abi.c - target-ABI conformance for the fisp_ interoperability
 * surface (SPEC section 5). A mismatch fails the build via the SA typedefs
 * below. Mirrors the framework's own ABI conformance TU.
 *
 * cleanimpl's isp_dev_uapi.h (adopted per H task 1 step 4 / decision #5) has
 * no ISP_UAPI_ABI_MODEL 32-bit-pointer emulation mode, so the pointer-width-
 * dependent facts below (sizeof/offsetof across a `void *addr` member, and
 * the load-register ioctl code, whose _IOWR size field is
 * sizeof(struct isp_table_reg_map)) are ARM EABI ILP32 target-only facts and
 * are commented out rather than asserted against this 64-bit host's 8-byte
 * pointers (see the note by isp_table_reg_map_addr below).
 */

#include "isp_dev_uapi.h"
#include "framework_events.h"

#include <stddef.h>

#define SA(cond, tag) typedef char sa_##tag[(cond) ? 1 : -1]

/* struct isp_table_reg_map: { void *addr; unsigned int size; } -> 8 bytes on
 * the ARM EABI ILP32 target (4-byte pointer + 4-byte size, no padding).  The
 * old ISP_UAPI_ABI_MODEL dual-compile trick simulated that on a 64-bit host by
 * redeclaring `addr` as a 32-bit placeholder; cleanimpl's isp_dev_uapi.h has
 * no such mode (H 31-abi-headers.md §5.4/decision #2: check_abi.py replaces
 * the trick for the generated fwi_* records, computed in Python against
 * ILP32 arithmetic rather than compiled).  This struct is project-owned, not
 * generated, so it has no check_abi.py fact-file row; the sizeof/offsetof(size)
 * facts below are genuinely ARM-target-only (a real `void *addr` is 8 bytes on
 * this x86-64 host, giving sizeof==16 and offsetof(size)==8 here, not the
 * ARM values), so they cannot be asserted by compiling natively here without
 * reintroducing a pointer-width emulation typedef.  Only the arch-invariant
 * fact (addr is the first member) is asserted; the full 8-byte/offset-4 fact
 * is pinned by 20-framework.md §4.6 prose and by the ARM camera build
 * (VIDIOC_VIN_ISP_LOAD_REG below is also only meaningful there for the same
 * reason). Reported as an open item rather than guessed around. */
SA(offsetof(struct isp_table_reg_map, addr) == 0, isp_table_reg_map_addr);

/* Kernel control ABI ids (SPEC 5.3). */
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

/* Load-register ABI (SPEC 5.1). */
SA(ISP_LOAD_DRAM_SIZE == 0x13240, load_dram_size);
/* VIDIOC_VIN_ISP_LOAD_REG's _IOWR size field is sizeof(struct
 * isp_table_reg_map): 8 on the ARM ILP32 target (0xc0085706), but 16 on this
 * 64-bit host (8-byte `void *addr`), giving 0xc0105706 here -- an ARM-only
 * fact, not asserted natively (see the isp_table_reg_map_addr note above). */

/* Number of compile-time ABI facts asserted above. */
int fisp_abi_model_checks(void)
{
	return 15;
}
