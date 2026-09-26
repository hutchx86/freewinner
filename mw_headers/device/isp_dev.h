/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * device/isp_dev.h - thin include-name shim (H "single source",
 * 31-abi-headers.md §5.5): struct hw_isp_device, struct isp_table_reg_map
 * and isp_set_load_reg() are now declared once, in freewinner's
 * isp_dev_uapi.h; this file keeps only its name so mediad's #include lines
 * do not change.
 */
#ifndef FMW_DEVICE_ISP_DEV_H
#define FMW_DEVICE_ISP_DEV_H

#include "isp_dev_uapi.h"

#endif /* FMW_DEVICE_ISP_DEV_H */
