/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* fisp_load_reg.c - load-register hook helper (fisp_). mediad wraps isp_set_load_reg
 * (-Wl,--wrap) and may edit the register-program buffer before each LOAD_REG ioctl; the
 * edit it needs, forcing 3DNR off (bit 5 of the word at 0x1a0), is this testable helper.
 * The wrapper itself belongs to the consumer (NOT-IMPLEMENTED.md). */

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

int fisp_load_reg_set_3dnr(struct fwi_table_reg_map *reg, int on)
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
