/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Host-test stand-in for GetVeOpsS, which on target comes from the executable
 * loading libvenc_base.so. Weak so a test may define its own. Linked into every
 * tests/base/test_* binary. */

#include <stddef.h>

#include "freecodec/ve_iface.h"

__attribute__((weak)) fc_ve_ops *GetVeOpsS(int type)
{
    (void)type;
    return NULL;
}
