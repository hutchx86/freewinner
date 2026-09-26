/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Load-register hook companion (package B, `fisp_`), SPEC section 4.
 *
 * The deploy build links mediad with `-Wl,--wrap=isp_set_load_reg`. The wrapper
 * receives the live register-program buffer once at isp_init and once per frame,
 * before the framework's VIDIOC_VIN_ISP_LOAD_REG ioctl, and may mutate it. The
 * specific edit mediad needs is to force 3DNR off by clearing bit 5 of the
 * 32-bit module-enable word at byte offset 0x1a0.
 *
 * This unit provides that edit as a plain, testable helper. The `--wrap` shim
 * itself (buffer overlay, snapshot) belongs to the consumer, not this package;
 * see NOT-IMPLEMENTED.md. */

#define _GNU_SOURCE

#include "fisp_abi.h"

#include "isp_dev_uapi.h"

#include <string.h>

/* Byte offset of the 32-bit module-enable / bypass word. */
#define FISP_LOAD_REG_BYPASS0_OFF  0x1a0u
/* D3D (3DNR) feature bit inside that word (ISP_FEATURES_D3D). */
#define FISP_LOAD_REG_D3D_BIT      (1u << 5)
/* One past the word: a shorter buffer cannot hold the edit. */
#define FISP_LOAD_REG_BYPASS0_END  (FISP_LOAD_REG_BYPASS0_OFF + 4u)

int fisp_load_reg_set_3dnr(struct isp_table_reg_map *reg, int on)
{
	unsigned char *base;
	unsigned int word;

	if (reg == NULL || reg->addr == NULL)
		return -1;
	if (reg->size < FISP_LOAD_REG_BYPASS0_END)
		return -1;

	if (on)
		return 0;               /* nothing to change when 3DNR is enabled */

	base = (unsigned char *)reg->addr;

	/* Read before write, and tolerate any alignment: copy the word out. */
	memcpy(&word, base + FISP_LOAD_REG_BYPASS0_OFF, sizeof word);
	if ((word & FISP_LOAD_REG_D3D_BIT) == 0)
		return 0;               /* already clear: no store on the frame path */

	word &= ~FISP_LOAD_REG_D3D_BIT;
	memcpy(base + FISP_LOAD_REG_BYPASS0_OFF, &word, sizeof word);
	return 1;
}
