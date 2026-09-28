// SPDX-License-Identifier: AGPL-3.0-only
/* A-LAYOUT probe: prints project device struct sizes for the two enum ABIs */
#include <stdio.h>
#include "isp_dev_uapi.h"
int main(void)
{
    printf("fwi_video_device=%zu video_fmt=%zu hw_isp_media_dev=%zu "
           "hw_isp_device=%zu video_buffer=%zu buffers_pool=%zu\n",
           sizeof(struct fwi_video_device), sizeof(struct video_fmt),
           sizeof(struct hw_isp_media_dev), sizeof(struct hw_isp_device),
           sizeof(struct video_buffer), sizeof(struct buffers_pool));
    return 0;
}
