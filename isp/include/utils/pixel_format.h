// SPDX-License-Identifier: AGPL-3.0-only
#ifndef UTILS_PIXEL_FORMAT_H
#define UTILS_PIXEL_FORMAT_H
#include "media_utils_abi.h"

fwm_pixel_format_e map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(int v4l2PixFmt);
int            map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(fwm_pixel_format_e format);

#endif
