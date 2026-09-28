/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* Include-name shim: keeps mediad's #include line; the types are declared in
 * fwm_media_abi.h. */
#ifndef FMW_MEDIA_MM_COMM_VI_H
#define FMW_MEDIA_MM_COMM_VI_H

#include <linux/videodev2.h>

#include "fwm_media_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Local values for fwm_vi_shutter_cfg_t's plain uint32_t shutter_mode/reset_mode
 * fields; not MPP-boundary layout, so not in fwm_media_enum.h. */
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
