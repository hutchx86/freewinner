/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/*
 * test_fcap_abi.c - target-ABI conformance for the capture runtime surface
 * (SPEC 2.2), compiled with FCAP_ABI_MODEL.  The pointer-sized members of
 * VIDEO_FRAME_S collapse to their 32-bit target width so the host reproduces
 * the measured 32-bit ARM EABI5 layout exactly; a mismatch fails the build via
 * the SA typedefs below.  Mirrors the fisp_ / isp_dev_uapi ABI conformance TUs.
 */

#define FCAP_ABI_MODEL 1

#include "fcap_abi.h"

#include <stdint.h>

#define SA(cond, tag) typedef char fcap_sa_model_##tag[(cond) ? 1 : -1]

typedef uint32_t fcap_model_ptr_t;

/*
 * VIDEO_FRAME_S with 4-byte pointers and the same field order as
 * media_utils_abi.h; the two uint64_t fields keep their natural 8-byte
 * alignment, so on a 64-bit host this record has the target's 144-byte layout.
 */
typedef struct model_video_frame {
	unsigned int   mWidth;
	unsigned int   mHeight;
	fwm_video_field_e  mField;
	fwm_pixel_format_e mPixelFormat;
	fwm_video_format_e mVideoFormat;
	fwm_compress_mode_e mCompressMode;
	unsigned int   mPhyAddr[3];
	fcap_model_ptr_t mpVirAddr[3];
	unsigned int   mStride[3];
	unsigned int   mHeaderPhyAddr[3];
	fcap_model_ptr_t mpHeaderVirAddr[3];
	unsigned int   mHeaderStride[3];
	short          mOffsetTop;
	short          mOffsetBottom;
	short          mOffsetLeft;
	short          mOffsetRight;
	uint64_t       mpts;
	unsigned int   mExposureTime;
	unsigned int   mFramecnt;
	int            mEnvLV;
	unsigned int   mWhoSetFlag;
	uint64_t       mFlagPts;
	unsigned int   mFrmFlag;
} model_video_frame;

typedef struct model_video_frame_info {
	model_video_frame VFrame;
	unsigned int      mId;
} model_video_frame_info;

SA(sizeof(model_video_frame) == 144, video_frame_size);
SA(offsetof(model_video_frame, mWidth) == 0, mwidth);
SA(offsetof(model_video_frame, mHeight) == 4, mheight);
SA(offsetof(model_video_frame, mField) == 8, mfield);
SA(offsetof(model_video_frame, mPixelFormat) == 12, mpixelformat);
SA(offsetof(model_video_frame, mVideoFormat) == 16, mvideoformat);
SA(offsetof(model_video_frame, mCompressMode) == 20, mcompressmode);
SA(offsetof(model_video_frame, mPhyAddr) == 24, mphyaddr);
SA(offsetof(model_video_frame, mpVirAddr) == 36, mpviraddr);
SA(offsetof(model_video_frame, mStride) == 48, mstride);
SA(offsetof(model_video_frame, mHeaderPhyAddr) == 60, mheaderphy);
SA(offsetof(model_video_frame, mpHeaderVirAddr) == 72, mheadervir);
SA(offsetof(model_video_frame, mHeaderStride) == 84, mheaderstride);
SA(offsetof(model_video_frame, mOffsetTop) == 96, mofftop);
SA(offsetof(model_video_frame, mOffsetBottom) == 98, moffbot);
SA(offsetof(model_video_frame, mOffsetLeft) == 100, moffleft);
SA(offsetof(model_video_frame, mOffsetRight) == 102, moffright);
SA(offsetof(model_video_frame, mpts) == 104, mpts);
SA(offsetof(model_video_frame, mExposureTime) == 112, mexposure);
SA(offsetof(model_video_frame, mFramecnt) == 116, mframecnt);
SA(offsetof(model_video_frame, mEnvLV) == 120, menvlv);
SA(offsetof(model_video_frame, mWhoSetFlag) == 124, mwhosetflag);
SA(offsetof(model_video_frame, mFlagPts) == 128, mflagpts);
SA(offsetof(model_video_frame, mFrmFlag) == 136, mfrmflag);

SA(sizeof(model_video_frame_info) == 152, frame_info_size);
SA(offsetof(model_video_frame_info, VFrame) == 0, frame_info_vframe);
SA(offsetof(model_video_frame_info, mId) == 144, frame_info_mid);

/* Error codes (SPEC 7 / 8.1). */
SA(ERR_SYS_ILLEGAL_PARAM == (int)0xA0028003u, e_sys_illegal);
SA(ERR_SYS_NOT_PERM == (int)0xA0028009u, e_sys_notperm);
SA(ERR_SYS_NOTREADY == (int)0xA0028010u, e_sys_notready);
SA(ERR_VI_INVALID_DEVID == (int)0xA0108001u, e_vi_dev);
SA(ERR_VI_INVALID_CHNID == (int)0xA0108002u, e_vi_chn);
SA(ERR_VI_EXIST == (int)0xA0108004u, e_vi_exist);
SA(ERR_VI_UNEXIST == (int)0xA0108005u, e_vi_unexist);
SA(ERR_VI_NOT_PERM == (int)0xA0108009u, e_vi_notperm);
SA(ERR_VI_BUSY == (int)0xA0108012u, e_vi_busy);

/* Constants (SPEC 2.3). */
SA(VI_VIPP_NUM_MAX == 4, vipp_max);
SA(VI_VIRCHN_NUM_MAX == 4, virchn_max);
SA(VI_ISP_NUM_MAX == 2, isp_max);
SA(MAX_VIPP_DEV_NUM == 4, max_vipp);
SA(VI_HIGH_FRAMERATE_STANDARD == 60, high_fps);
SA(VI_FIFO_LEVEL == 20, fifo_level);

/* Number of compile-time ABI facts asserted above. */
int fcap_abi_model_checks(void)
{
	return 42;
}
