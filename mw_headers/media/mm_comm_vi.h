/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * media/mm_comm_vi.h - thin include-name shim (H "single source",
 * 31-abi-headers.md §5.5/§5.6): VI_ATTR_S and VI_SHUTTIME_CFG_S are now
 * declared once, in freewinner's generated MPP boundary header
 * (fwm_media_abi.h, as fwm_vi_attr_t / fwm_vi_shutter_cfg_t).  This file
 * keeps only its name so mediad's #include lines do not change; app code now
 * spells the types directly (Task 2 punch list).
 */
#ifndef FMW_MEDIA_MM_COMM_VI_H
#define FMW_MEDIA_MM_COMM_VI_H

#include <linux/videodev2.h>

#include "fwm_media_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Small local mode/reset constants: not MPP-boundary layout, not in the
 * generated fwm_media_enum.h set, and mediad only ever needs the numeric
 * values (0/1/2 and 0/1) it already passed through fwm_vi_shutter_cfg_t's
 * plain uint32_t reset_mode/shutter_mode fields, so these are kept local
 * rather than invented under a fwm_ name. */
typedef enum VI_SHUTTIME_MODE_E {
	VI_SHUTTIME_MODE_AUTO       = 0,
	VI_SHUTTIME_MODE_PREVIEW    = 1,
	VI_SHUTTIME_MODE_NIGHT_VIEW = 2
} VI_SHUTTIME_MODE_E;

typedef enum VI_SHUTTIME_RESET_E {
	VI_SHUTTIME_RESET_AUTO_DELAY = 0,
	VI_SHUTTIME_RESET_AUTO_NOW   = 1
} VI_SHUTTIME_RESET_E;

#ifdef __cplusplus
}
#endif

#endif /* FMW_MEDIA_MM_COMM_VI_H */
