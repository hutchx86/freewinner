/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * vencoder.h - clean-room interface declarations for the eyesee-mpp middleware
 * ABI (libcedarc public encoder API).  Declarations only; re-authored from
 * cleanroom/middleware/headers/SPEC.md sections 2.16 and 4.2.  Nothing here is
 * copied from a vendor header.  See NOT-IMPLEMENTED.md.
 *
 * Sizes are the measured 32-bit ARM EABI5 layouts.  This translation unit is
 * never built with -fshort-enums (it shares the prebuilt codec ABI), so the
 * enums here are the default 4-byte width; they are left as plain enums so the
 * header stays a faithful description of the interface.
 *
 * Records the spec measures only by size (VencAdvancedRefParam, VencFixQP,
 * VencMBModeCtrl, VencOverlayCoverYuvS) are opaque members of exactly those
 * measured sizes; no daemon translation unit reads them.  The unnamed gaps in
 * VencRcParam are likewise opaque fillers that reproduce the measured offsets
 * of the fields the daemon does use.
 */
#ifndef FMW_VENCODER_H
#define FMW_VENCODER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Enums (SPEC section 4.4)                                            */
/* ------------------------------------------------------------------ */

typedef enum VENC_CODEC_TYPE {
	VENC_CODEC_H264 = 0,
	VENC_CODEC_JPEG = 1,
	VENC_CODEC_H264_VER2 = 2,
	VENC_CODEC_H265 = 3,
	VENC_CODEC_VP8 = 4
} VENC_CODEC_TYPE;

typedef enum VENC_H264PROFILETYPE {
	VENC_H264ProfileBaseline = 66,
	VENC_H264ProfileMain     = 77,
	VENC_H264ProfileHigh     = 100
} VENC_H264PROFILETYPE;

typedef enum VENC_H264LEVELTYPE {
	VENC_H264Level32 = 32
} VENC_H264LEVELTYPE;

typedef enum VENC_CODING_MODE {
	VENC_FRAME_CODING = 0,
	VENC_FIELD_CODING = 1,
	VENC_MB16_CODING  = 2
} VENC_CODING_MODE;

typedef enum VENC_PIXEL_FMT {
	VENC_PIXEL_YVU420SP = 1,
	VENC_PIXEL_LBC_AW   = 19
} VENC_PIXEL_FMT;

typedef enum VENC_RC_MODE {
	AW_CBR  = 0,
	AW_VBR  = 1,
	AW_AVBR = 2
} VENC_RC_MODE;

typedef enum VENC_VIDEO_GOP_MODE {
	AW_NORMALP = 1
} VENC_VIDEO_GOP_MODE;

typedef enum VENC_OVERLAY_ARGB_TYPE {
	VENC_OVERLAY_ARGB1555 = 2
} VENC_OVERLAY_ARGB_TYPE;

typedef enum VENC_OVERLAY_TYPE {
	NORMAL_OVERLAY = 0,
	LUMA_REVERSE_OVERLAY = 2
} VENC_OVERLAY_TYPE;

/* Only the anchors the daemon names; the gaps are untouched members. */
typedef enum VENC_INDEXTYPE {
	VENC_IndexParamBitrate              = 0x0,
	VENC_IndexParamFramerate            = 0x1,
	VENC_IndexParamMaxKeyInterval       = 0x2,
	VENC_IndexParamIfilter              = 0x3,
	VENC_IndexParamForceKeyFrame        = 0x6,
	VENC_IndexParamSize                 = 0xe,
	VENC_IndexParamSetVbvSize           = 0xf,
	VENC_IndexParamVbvInfo              = 0x10,
	VENC_IndexParamH264Param            = 0x100,
	VENC_IndexParamH264SPSPPS           = 0x101,
	VENC_IndexParamFastEnc              = 0x109,
	VENC_IndexParamSetFrameLenThreshold = 0x205,
	VENC_IndexParamSetBitRateRange      = 0x207,
	VENC_IndexParamH265Param            = 0x300,
	VENC_IndexParamAlterFrame           = 0x400,
	VENC_IndexParamChannelNum           = 0x402,
	VENC_IndexParamSetOverlay           = 0x404,
	VENC_IndexParam3DFilter             = 0x40e,
	VENC_IndexParamSetNullFrame         = 0x500
} VENC_INDEXTYPE;

#define MAX_OVERLAY_SIZE 64

/* ------------------------------------------------------------------ */
/* Records (SPEC section 4.2)                                          */
/* ------------------------------------------------------------------ */

typedef struct VencRect {
	int nLeft;
	int nTop;
	int nWidth;
	int nHeight;
} VencRect;                                            /* 16 */

typedef struct VencROIConfig {
	int      bEnable;        /* +0  */
	int      index;          /* +4  */
	int      nQPoffset;      /* +8  */
	unsigned char roi_abs_flag; /* +12 */
	VencRect sRect;          /* +16 */
} VencROIConfig;                                       /* 32 */

typedef struct VencH264ProfileLevel {
	VENC_H264PROFILETYPE nProfile;
	VENC_H264LEVELTYPE   nLevel;
} VencH264ProfileLevel;                                /* 8  */

typedef struct VencQPRange {
	int nMaxqp;
	int nMinqp;
} VencQPRange;                                         /* 8  */

/* Opaque reference-parameter block, measured 16 bytes. */
typedef union {
	unsigned int _reserved[4];
} VencAdvancedRefParam;

typedef struct VencGopParam {
	unsigned char        bUseGopCtrlEn;         /* +0  */
	VENC_VIDEO_GOP_MODE  eGopMode;              /* +4  */
	int                  nVirtualIFrameInterval;/* +8  */
	int                  nSpInterval;           /* +12 */
	VencAdvancedRefParam sRefParam;             /* +16 */
} VencGopParam;                                        /* 32 */

typedef struct VencVbrParam {
	unsigned int uMaxBitRate;
	int          nMovingTh;
	int          nQuality;
} VencVbrParam;                                        /* 12 */

/* Opaque fixed-QP block, measured 12 bytes. */
typedef union {
	unsigned int _reserved[3];
} VencFixQP;

/* Opaque QP-map block, measured 8 bytes. */
typedef union {
	unsigned int _reserved[2];
} VencMBModeCtrl;

/* Rate-control parameters, sizeof 128 (SPEC section 4.2).  Only eRcMode and
 * sVbrParam are named by the daemon; the aligned fillers reproduce the measured
 * offsets (sVbrParam at 56, sFixQp at 68, sQpMap at 80). */
typedef struct VencRcParam {
	VENC_RC_MODE   eRcMode;                       /* +0  */
	unsigned int   _gap0[13];                     /* +4  */
	VencVbrParam   sVbrParam;                     /* +56 */
	VencFixQP      sFixQp;                        /* +68 */
	VencMBModeCtrl sQpMap;                        /* +80 */
	unsigned int   _gap1[10];                     /* +88 */
} VencRcParam;                                        /* 128 */

typedef struct VencH264Param {
	VencH264ProfileLevel sProfileLevel;        /* +0  */
	int                  bEntropyCodingCABAC;  /* +8  */
	VencQPRange          sQPRange;             /* +12 */
	int                  nFramerate;           /* +20 */
	int                  nSrcFramerate;        /* +24 */
	int                  nBitrate;             /* +28 */
	int                  nMaxKeyInterval;      /* +32 */
	VENC_CODING_MODE     nCodingMode;          /* +36 */
	VencGopParam         sGopParam;            /* +40 */
	VencRcParam          sRcParam;             /* +72 */
} VencH264Param;                                      /* 200 */

typedef struct VencBaseConfig {
	unsigned char   bEncH264Nalu;            /* +0  */
	unsigned int    nInputWidth;             /* +4  */
	unsigned int    nInputHeight;            /* +8  */
	unsigned int    nDstWidth;               /* +12 */
	unsigned int    nDstHeight;              /* +16 */
	unsigned int    nStride;                 /* +20 */
	VENC_PIXEL_FMT  eInputFormat;            /* +24 */
	void           *memops;                  /* +28 */
	void           *veOpsS;                  /* +32 */
	void           *pVeOpsSelf;              /* +36 */
	unsigned char   bOnlyWbFlag;             /* +40 */
	unsigned char   bLbcLossyComEnFlag2x;    /* +41 */
	unsigned char   bLbcLossyComEnFlag2_5x;  /* +42 */
	unsigned char   bIsVbvNoCache;           /* +43 */
} VencBaseConfig;                                     /* 44 */

typedef struct VencInputBuffer {
	unsigned long   nID;                  /* +0   */
	long long       nPts;                 /* +8   */
	unsigned int    nFlag;                /* +16  */
	unsigned char  *pAddrPhyY;            /* +20  */
	unsigned char  *pAddrPhyC;            /* +24  */
	unsigned char  *pAddrVirY;            /* +28  */
	unsigned char  *pAddrVirC;            /* +32  */
	int             bEnableCorp;          /* +36  */
	VencRect        sCropInfo;            /* +40  */
	int             ispPicVar;            /* +56  */
	int             ispPicVarChroma;      /* +60  */
	int             bUseInputBufferRoi;   /* +64  */
	VencROIConfig   roi_param[8];         /* +68  */
	int             bAllocMemSelf;        /* +324 */
	int             nShareBufFd;          /* +328 */
	unsigned char   bUseCsiColorFormat;   /* +332 */
	int             eCsiColorFormat;      /* +336 */
	int             envLV;                /* +340 */
} VencInputBuffer;                                    /* 344 */

typedef struct FrameInfo {
	int CurrQp;
	int avQp;
	int nGopIndex;
	int nFrameIndex;
	int nTotalIndex;
} FrameInfo;                                          /* 20 */

typedef struct VencOutputBuffer {
	int             nID;             /* +0  */
	long long       nPts;            /* +8  */
	unsigned int    nFlag;           /* +16 */
	unsigned int    nSize0;          /* +20 */
	unsigned int    nSize1;          /* +24 */
	unsigned char  *pData0;          /* +28 */
	unsigned char  *pData1;          /* +32 */
	FrameInfo       frame_info;      /* +36 */
	unsigned int    nSize2;          /* +56 */
	unsigned char  *pData2;          /* +60 */
} VencOutputBuffer;                                   /* 64 */

typedef struct VencBitRateRange {
	unsigned int bitRateMax;         /* +0 */
	unsigned int bitRateMin;         /* +4 */
} VencBitRateRange;                                   /* 8 */

typedef struct VencHeaderData {
	unsigned char *pBuffer;          /* +0 */
	unsigned int   nLength;          /* +4 */
} VencHeaderData;                                     /* 8 */

/* Opaque cover-colour block, measured 4 bytes with 2-byte alignment so it sits
 * at offset 10 inside VencOverlayHeaderS. */
typedef union {
	unsigned short _reserved[2];
} VencOverlayCoverYuvS;

typedef struct VencOverlayHeaderS {
	unsigned short        start_mb_x;               /* +0  */
	unsigned short        end_mb_x;                 /* +2  */
	unsigned short        start_mb_y;               /* +4  */
	unsigned short        end_mb_y;                 /* +6  */
	unsigned char         extra_alpha_flag;         /* +8  */
	unsigned char         extra_alpha;              /* +9  */
	VencOverlayCoverYuvS  cover_yuv;                /* +10 */
	VENC_OVERLAY_TYPE     overlay_type;             /* +16 */
	unsigned char        *overlay_blk_addr;         /* +20 */
	unsigned int          bitmap_size;              /* +24 */
	unsigned int          bforce_reverse_flag;      /* +28 */
	unsigned int          reverse_unit_mb_w_minus1; /* +32 */
	unsigned int          reverse_unit_mb_h_minus1; /* +36 */
} VencOverlayHeaderS;                                 /* 40 */

typedef struct VencOverlayInfoS {
	unsigned char           blk_num;                /* +0   */
	VENC_OVERLAY_ARGB_TYPE  argb_type;              /* +4   */
	VencOverlayHeaderS      overlayHeaderList[MAX_OVERLAY_SIZE]; /* +8 */
	unsigned int            invert_mode;            /* +2568 */
	unsigned int            invert_threshold;       /* +2572 */
} VencOverlayInfoS;                                   /* 2576 */

/* ------------------------------------------------------------------ */
/* Handles and entry points (SPEC section 2.16)                        */
/* ------------------------------------------------------------------ */

typedef void *VideoEncoder;

VideoEncoder *VideoEncCreate(VENC_CODEC_TYPE codecType);
void          VideoEncDestroy(VideoEncoder *encoder);
int           VideoEncInit(VideoEncoder *encoder, VencBaseConfig *config);
int           VideoEncGetParameter(VideoEncoder *encoder,
				   VENC_INDEXTYPE indexType, void *param);
int           VideoEncSetParameter(VideoEncoder *encoder,
				   VENC_INDEXTYPE indexType, void *param);
int           VideoEncoderReset(VideoEncoder *encoder);

int AddOneInputBuffer(VideoEncoder *encoder, VencInputBuffer *buffer);
int VideoEncodeOneFrame(VideoEncoder *encoder);
int AlreadyUsedInputBuffer(VideoEncoder *encoder, VencInputBuffer *buffer);

int ValidBitstreamFrameNum(VideoEncoder *encoder);
int GetOneBitstreamFrame(VideoEncoder *encoder, VencOutputBuffer *buffer);
int FreeOneBitStreamFrame(VideoEncoder *encoder, VencOutputBuffer *buffer);

#ifdef __cplusplus
}
#endif

#endif /* FMW_VENCODER_H */
