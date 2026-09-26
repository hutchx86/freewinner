/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Records, value sets and the device table shared by the encoder device
 * interface (vencoder.h) and the encoder framework (src/fenc/fenc_abi.h), as
 * specified in spec r2/05 part B.
 *
 * Sizes, offsets and numeric values are binary facts of the 32-bit ARM EABI
 * target and are asserted at the end of this file under __arm__. Every C
 * identifier here is local to this project; the link symbols are declared by
 * the header that owns them. */

#ifndef FREECODEC_VENC_TYPES_H
#define FREECODEC_VENC_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================== value sets */

/* H.264 profile_idc (H.264 A.2). */
typedef enum fwm_venc_h264_profile_e {
    FWM_VENC_H264_PROFILE_BASELINE = 66,
    FWM_VENC_H264_PROFILE_MAIN     = 77,
    FWM_VENC_H264_PROFILE_HIGH     = 100
} fwm_venc_h264_profile_e;

/* H.264 level_idc: the level number times ten (H.264 A.3). */
typedef enum fwm_venc_h264_level_e {
    FWM_VENC_H264_LEVEL32 = 32,
    FWM_VENC_H264_LEVEL51 = 51
} fwm_venc_h264_level_e;

/* Input picture layouts. */
typedef enum fwm_venc_pixel_format_e {
    FWM_VENC_PIXEL_YUV420SP = 0,    /* Y plane + interleaved UV (NV12)            */
    FWM_VENC_PIXEL_YVU420SP = 1,    /* Y plane + interleaved VU (NV21)            */
    FWM_VENC_PIXEL_LBC_AW   = 19    /* engine-specific frame-compressed input     */
} fwm_venc_pixel_format_e;

typedef enum VENC_RC_MODE {
    AW_CBR  = 0,                /* constant bit rate                          */
    AW_VBR  = 1,                /* variable bit rate                          */
    AW_AVBR = 2                 /* average variable bit rate                  */
} VENC_RC_MODE;

typedef enum fwm_venc_gop_mode_e {
    FWM_AW_NORMALP = 1              /* I followed by a plain chain of P pictures  */
} fwm_venc_gop_mode_e;

typedef enum fwm_venc_overlay_argb_type_e {
    FWM_VENC_OVERLAY_ARGB1555 = 2
} fwm_venc_overlay_argb_type_e;

typedef enum fwm_venc_overlay_type_e {
    FWM_NORMAL_OVERLAY = 0,
    FWM_LUMA_REVERSE_OVERLAY = 2   /* normal bitmap + per-frame luma inversion */
} fwm_venc_overlay_type_e;

/* Codec requested from the framework at create time. */
typedef enum VENC_CODEC_TYPE {
    VENC_CODEC_H264      = 0,
    VENC_CODEC_JPEG      = 1,
    VENC_CODEC_H264_VER2 = 2,   /* H.264 on the version-2 engine class        */
    VENC_CODEC_H265      = 3,
    VENC_CODEC_VP8       = 4
} VENC_CODEC_TYPE;

/* Index argument of the get/set-parameter calls. The device and the framework
 * share one set; a value the consumer never uses is forwarded unchanged. */
typedef enum VENC_INDEXTYPE {
    VENC_IndexParamBitrate                = 0x000,
    VENC_IndexParamFramerate              = 0x001,
    VENC_IndexParamMaxKeyInterval         = 0x002,
    VENC_IndexParamIfilter                = 0x003,
    VENC_IndexParamRotation               = 0x004,
    VENC_IndexParamSliceHeight            = 0x005,
    VENC_IndexParamForceKeyFrame          = 0x006,
    VENC_IndexParamROIConfig              = 0x00b,
    VENC_IndexParamStride                 = 0x00c,
    VENC_IndexParamColorFormat            = 0x00d,
    VENC_IndexParamSize                   = 0x00e,
    VENC_IndexParamSetVbvSize             = 0x00f,
    VENC_IndexParamVbvInfo                = 0x010,
    VENC_IndexParamSetPSkip               = 0x012,
    VENC_IndexParamHorizonFlip            = 0x015,
    VENC_IndexParamH264Param              = 0x100,
    VENC_IndexParamH264SPSPPS             = 0x101,
    VENC_IndexParamH264QPRange            = 0x102,
    VENC_IndexParamH264ProfileLevel       = 0x103,
    VENC_IndexParamH264EntropyCodingCABAC = 0x104,
    VENC_IndexParamH264FixQP              = 0x106,
    VENC_IndexParamH264SVCSkip            = 0x107,
    VENC_IndexParamFastEnc                = 0x109,
    VENC_IndexParamChmoraGray             = 0x10c,
    VENC_IndexParamIQpOffset              = 0x10d,
    VENC_IndexParamSetFrameLenThreshold   = 0x205,
    VENC_IndexParamSetBitRateRange        = 0x207,
    VENC_IndexParamH265Param              = 0x300,
    VENC_IndexParamAlterFrame             = 0x400,
    VENC_IndexParamChannelNum             = 0x402,
    VENC_IndexParamSetOverlay             = 0x404,
    VENC_IndexParam3DFilter               = 0x40e,
    VENC_IndexParamSetNullFrame           = 0x500,
    VENC_IndexParamFillingCbr             = 0x505,
    VENC_IndexParamRoi                    = 0x506
} VENC_INDEXTYPE;

/* Result codes of the device, the framework and the support library. */
#define VENC_RESULT_ERROR             (-1)
#define VENC_RESULT_OK                0
#define VENC_RESULT_NO_FRAME_BUFFER   1    /* no input picture queued         */
#define VENC_RESULT_BITSTREAM_IS_FULL 2    /* bitstream buffer full           */
#define VENC_RESULT_ILLEGAL_PARAM     3
#define VENC_RESULT_NOT_SUPPORT       4
#define VENC_RESULT_BITSTREAM_IS_EMPTY 5   /* no finished frame waiting       */
#define VENC_RESULT_NO_MEMORY         6
#define VENC_RESULT_NO_RESOURCE       7
#define VENC_RESULT_NULL_PTR          8
#define VENC_RESULT_DROP_FRAME        9    /* frame dropped                   */
#define VENC_RESULT_EFUSE_ERROR       25   /* e-fuse check failed             */

/* Output-frame flags. */
#define VENC_BUFFERFLAG_KEYFRAME 0x1

/* P-skip factors accepted by VENC_IndexParamSetPSkip. */
#define SKIP_4 4
#define SKIP_8 8

/* Overlay (OSD) regions per picture. */
#define MAX_OVERLAY_SIZE 64

/* =============================================================== records */

/* Rectangle in pixels (16 bytes). */
typedef struct VencRect {
    int nLeft;
    int nTop;
    int nWidth;
    int nHeight;
} VencRect;

/* One region of interest (32 bytes). */
typedef struct VencROIConfig {
    int            bEnable;
    int            index;        /* region slot, 0..7                         */
    int            nQPoffset;    /* QP delta, or absolute QP (flag below)     */
    unsigned char  roi_abs_flag; /* non-zero: nQPoffset is an absolute QP     */
    VencRect       sRect;
} VencROIConfig;

/* Profile and level (8 bytes). */
typedef struct VencH264ProfileLevel {
    fwm_venc_h264_profile_e nProfile;
    fwm_venc_h264_level_e   nLevel;
} VencH264ProfileLevel;

/* Slice-QP bounds (8 bytes). */
typedef struct VencQPRange {
    int nMaxqp;
    int nMinqp;
} VencQPRange;

/* Reference options of the GOP block (16 bytes; only the first word is read). */
typedef struct VencAdvancedRefParam {
    int          bAdvancedRefEn;
    unsigned int _reserved[3];
} VencAdvancedRefParam;

/* GOP structure (32 bytes). */
typedef struct VencGopParam {
    unsigned char        bUseGopCtrlEn;
    fwm_venc_gop_mode_e  eGopMode;
    int                  nVirtualIFrameInterval;
    int                  nSpInterval;
    VencAdvancedRefParam sRefParam;
} VencGopParam;

/* Variable-bit-rate options (12 bytes). */
typedef struct VencVbrParam {
    unsigned int uMaxBitRate;
    int          nMovingTh;
    int          nQuality;
} VencVbrParam;

/* Fixed-QP mode (12 bytes). */
typedef struct VencFixQP {
    int bEnable;
    int nIQp;
    int nPQp;
} VencFixQP;

/* Per-macroblock QP map (8 bytes on the target). */
typedef struct VencMBModeCtrl {
    unsigned int mode_ctrl_en;
    void        *p_info;
} VencMBModeCtrl;

/* Rate-control block (128 bytes); the unnamed words are not read. */
typedef struct VencRcParam {
    VENC_RC_MODE   eRcMode;
    unsigned int   _gap0[13];
    VencVbrParam   sVbrParam;
    VencFixQP      sFixQp;
    VencMBModeCtrl sQpMap;
    unsigned int   _gap1[10];
} VencRcParam;

/* H.264 stream parameters (200 bytes). */
typedef struct VencH264Param {
    VencH264ProfileLevel sProfileLevel;
    int                  bEntropyCodingCABAC;
    VencQPRange          sQPRange;
    int                  nFramerate;
    int                  nSrcFramerate;
    int                  nBitrate;
    int                  nMaxKeyInterval;
    int                  nCodingMode;
    VencGopParam         sGopParam;
    VencRcParam          sRcParam;
} VencH264Param;

/* Device configuration handed to init (44 bytes). The framework fills in the
 * three engine/memory pointer fields. */
typedef struct VencBaseConfig {
    unsigned char   bEncH264Nalu;        /* emit NAL units with start codes    */
    unsigned int    nInputWidth;         /* input picture, px                  */
    unsigned int    nInputHeight;
    unsigned int    nDstWidth;           /* coded picture, px; 0 = input size  */
    unsigned int    nDstHeight;
    unsigned int    nStride;             /* input line stride, px; 0 = width   */
    fwm_venc_pixel_format_e  eInputFormat;
    void           *memops;              /* support-library memory table       */
    void           *veOpsS;              /* engine operations table            */
    void           *pVeOpsSelf;          /* engine instance                    */
    unsigned char   bOnlyWbFlag;
    unsigned char   bLbcLossyComEnFlag2x;
    unsigned char   bLbcLossyComEnFlag2_5x;
    unsigned char   bIsVbvNoCache;
} VencBaseConfig;

/* One input picture (344 bytes). The support library uses only nID, the four
 * plane addresses and bAllocMemSelf; everything else is carried opaquely. */
typedef struct VencInputBuffer {
    unsigned long   nID;
    long long       nPts;                /* presentation time stamp            */
    unsigned int    nFlag;
    unsigned char  *pAddrPhyY;           /* luma, physical                     */
    unsigned char  *pAddrPhyC;           /* chroma, physical                   */
    unsigned char  *pAddrVirY;           /* luma, CPU mapping                  */
    unsigned char  *pAddrVirC;           /* chroma, CPU mapping                */
    int             bEnableCorp;
    VencRect        sCropInfo;
    int             ispPicVar;           /* picture variance from the ISP      */
    int             ispPicVarChroma;
    int             bUseInputBufferRoi;  /* per-picture ROI                    */
    VencROIConfig   roi_param[8];
    int             bAllocMemSelf;       /* allocated by the support library   */
    int             nShareBufFd;         /* shared dma-buf fd                  */
    unsigned char   bUseCsiColorFormat;
    int             eCsiColorFormat;
    int             envLV;               /* scene light value                  */
} VencInputBuffer;

/* Per-frame coding statistics (20 bytes). */
typedef struct FrameInfo {
    int CurrQp;
    int avQp;
    int nGopIndex;
    int nFrameIndex;
    int nTotalIndex;
} FrameInfo;

/* One finished frame (64 bytes). A frame that wraps around the end of the
 * bitstream ring comes in two parts; part 2 is an optional extra. */
typedef struct VencOutputBuffer {
    int             nID;
    long long       nPts;
    unsigned int    nFlag;
    unsigned int    nSize0;
    unsigned int    nSize1;
    unsigned char  *pData0;
    unsigned char  *pData1;
    FrameInfo       frame_info;
    unsigned int    nSize2;
    unsigned char  *pData2;
} VencOutputBuffer;

/* A byte blob (8 bytes): the SPS + PPS headers. */
typedef struct VencHeaderData {
    unsigned char *pBuffer;
    unsigned int   nLength;
} VencHeaderData;

/* Four bytes of cover colour, 2-byte aligned. */
typedef union VencOverlayCoverYuvS {
    unsigned short _reserved[2];
} VencOverlayCoverYuvS;

/* One overlay region (40 bytes); coordinates in macroblocks. */
typedef struct VencOverlayHeaderS {
    unsigned short        start_mb_x;
    unsigned short        end_mb_x;
    unsigned short        start_mb_y;
    unsigned short        end_mb_y;
    unsigned char         extra_alpha_flag;
    unsigned char         extra_alpha;
    VencOverlayCoverYuvS  cover_yuv;
    fwm_venc_overlay_type_e     overlay_type;
    unsigned char        *overlay_blk_addr;
    unsigned int          bitmap_size;
    unsigned int          bforce_reverse_flag;
    unsigned int          reverse_unit_mb_w_minus1;   /* invert unit, MB - 1    */
    unsigned int          reverse_unit_mb_h_minus1;
} VencOverlayHeaderS;

/* Overlay configuration (2576 bytes). */
typedef struct VencOverlayInfoS {
    unsigned char           blk_num;
    fwm_venc_overlay_argb_type_e  argb_type;
    VencOverlayHeaderS      overlayHeaderList[MAX_OVERLAY_SIZE];
    unsigned int            invert_mode;
    unsigned int            invert_threshold;
} VencOverlayInfoS;

/* Picture size (8 bytes). */
typedef struct VencSize {
    int nWidth;
    int nHeight;
} VencSize;

/* Bitstream buffer status (16 bytes). */
typedef struct VbvInfo {
    unsigned int vbv_size;
    unsigned int coded_frame_num;
    unsigned int coded_size;
    unsigned int maxFrameLen;
} VbvInfo;

/* Temporal layering / frame skipping (28 bytes). */
typedef struct VencH264SVCSkip {
    int          nTemporalSVC;
    int          nSkipFrame;
    int          bEnableLayerRatio;
    unsigned int nLayerRatio[4];
} VencH264SVCSkip;

/* Input-picture pool request (12 bytes). */
typedef struct VencAllocateBufferParam {
    unsigned int nBufferNum;
    unsigned int nSizeY;
    unsigned int nSizeC;
} VencAllocateBufferParam;

/* ========================================================== device table */

/* One encoder device: 12 slots (48 bytes on the target). Handles are opaque. */
typedef struct VENC_DEVICE {
    const char *name;
    void *(*open)(VencBaseConfig *pBaseConfig, unsigned int nIcVersion);
    int   (*init)(void *handle, VencBaseConfig *pBaseConfig);  /* allocates buffers */
    int   (*uninit)(void *handle);
    void  (*close)(void *handle);
    int   (*encode)(void *handle, VencInputBuffer *pInBuffer); /* one picture */
    int   (*GetParameter)(void *handle, int indexType, void *param);
    int   (*SetParameter)(void *handle, int indexType, void *param);
    int   (*ValidBitStreamFrameNum)(void *handle);             /* finished, waiting */
    int   (*GetOneBitStreamFrame)(void *handle, VencOutputBuffer *pOutBuffer);
    int   (*FreeOneBitStreamFrame)(void *handle, VencOutputBuffer *pOutBuffer);
    int   (*ResetBitStreamFrame)(void *handle);                /* discard all */
} VENC_DEVICE;

/* Exported device tables (fixed link names, spec r2/05 B.1): the two H.264
 * entries describe the working device; the others fail every call. */
extern VENC_DEVICE video_encoder_h264_ver1;
extern VENC_DEVICE video_encoder_h264_ver2;
extern VENC_DEVICE video_encoder_h265;
extern VENC_DEVICE video_encoder_jpeg;

/* ========================================================= layout checks */

#if defined(__arm__)
_Static_assert(sizeof(fwm_venc_h264_profile_e) == 4, "fwm_venc_h264_profile_e is 4 bytes");
_Static_assert(sizeof(fwm_venc_h264_level_e) == 4, "fwm_venc_h264_level_e is 4 bytes");
_Static_assert(sizeof(fwm_venc_pixel_format_e) == 4, "fwm_venc_pixel_format_e is 4 bytes");
_Static_assert(sizeof(VENC_RC_MODE) == 4, "VENC_RC_MODE is 4 bytes");
_Static_assert(sizeof(fwm_venc_gop_mode_e) == 4, "fwm_venc_gop_mode_e is 4 bytes");
_Static_assert(sizeof(fwm_venc_overlay_argb_type_e) == 4, "fwm_venc_overlay_argb_type_e is 4 bytes");
_Static_assert(sizeof(fwm_venc_overlay_type_e) == 4, "fwm_venc_overlay_type_e is 4 bytes");
_Static_assert(sizeof(VENC_CODEC_TYPE) == 4, "VENC_CODEC_TYPE is 4 bytes");
_Static_assert(sizeof(VENC_INDEXTYPE) == 4, "VENC_INDEXTYPE is 4 bytes");

/* B.4.1 rectangle */
_Static_assert(sizeof(VencRect) == 16, "VencRect is 16 bytes");
_Static_assert(offsetof(VencRect, nLeft) == 0, "VencRect.nLeft");
_Static_assert(offsetof(VencRect, nTop) == 4, "VencRect.nTop");
_Static_assert(offsetof(VencRect, nWidth) == 8, "VencRect.nWidth");
_Static_assert(offsetof(VencRect, nHeight) == 12, "VencRect.nHeight");

/* B.4.2 ROI entry */
_Static_assert(sizeof(VencROIConfig) == 32, "VencROIConfig is 32 bytes");
_Static_assert(offsetof(VencROIConfig, bEnable) == 0, "VencROIConfig.bEnable");
_Static_assert(offsetof(VencROIConfig, index) == 4, "VencROIConfig.index");
_Static_assert(offsetof(VencROIConfig, nQPoffset) == 8, "VencROIConfig.nQPoffset");
_Static_assert(offsetof(VencROIConfig, roi_abs_flag) == 12, "VencROIConfig.roi_abs_flag");
_Static_assert(offsetof(VencROIConfig, sRect) == 16, "VencROIConfig.sRect");

/* B.4.3 profile / level */
_Static_assert(sizeof(VencH264ProfileLevel) == 8, "VencH264ProfileLevel is 8 bytes");
_Static_assert(offsetof(VencH264ProfileLevel, nProfile) == 0, "VencH264ProfileLevel.nProfile");
_Static_assert(offsetof(VencH264ProfileLevel, nLevel) == 4, "VencH264ProfileLevel.nLevel");

/* B.4.4 QP range */
_Static_assert(sizeof(VencQPRange) == 8, "VencQPRange is 8 bytes");
_Static_assert(offsetof(VencQPRange, nMaxqp) == 0, "VencQPRange.nMaxqp");
_Static_assert(offsetof(VencQPRange, nMinqp) == 4, "VencQPRange.nMinqp");

/* B.4.5 GOP block */
_Static_assert(sizeof(VencAdvancedRefParam) == 16, "VencAdvancedRefParam is 16 bytes");
_Static_assert(offsetof(VencAdvancedRefParam, bAdvancedRefEn) == 0, "VencAdvancedRefParam.bAdvancedRefEn");
_Static_assert(sizeof(VencGopParam) == 32, "VencGopParam is 32 bytes");
_Static_assert(offsetof(VencGopParam, bUseGopCtrlEn) == 0, "VencGopParam.bUseGopCtrlEn");
_Static_assert(offsetof(VencGopParam, eGopMode) == 4, "VencGopParam.eGopMode");
_Static_assert(offsetof(VencGopParam, nVirtualIFrameInterval) == 8, "VencGopParam.nVirtualIFrameInterval");
_Static_assert(offsetof(VencGopParam, nSpInterval) == 12, "VencGopParam.nSpInterval");
_Static_assert(offsetof(VencGopParam, sRefParam) == 16, "VencGopParam.sRefParam");

/* B.4.6 VBR / fixed-QP / QP-map blocks */
_Static_assert(sizeof(VencVbrParam) == 12, "VencVbrParam is 12 bytes");
_Static_assert(offsetof(VencVbrParam, uMaxBitRate) == 0, "VencVbrParam.uMaxBitRate");
_Static_assert(offsetof(VencVbrParam, nMovingTh) == 4, "VencVbrParam.nMovingTh");
_Static_assert(offsetof(VencVbrParam, nQuality) == 8, "VencVbrParam.nQuality");
_Static_assert(sizeof(VencFixQP) == 12, "VencFixQP is 12 bytes");
_Static_assert(offsetof(VencFixQP, bEnable) == 0, "VencFixQP.bEnable");
_Static_assert(offsetof(VencFixQP, nIQp) == 4, "VencFixQP.nIQp");
_Static_assert(offsetof(VencFixQP, nPQp) == 8, "VencFixQP.nPQp");
_Static_assert(sizeof(VencMBModeCtrl) == 8, "VencMBModeCtrl is 8 bytes");
_Static_assert(offsetof(VencMBModeCtrl, mode_ctrl_en) == 0, "VencMBModeCtrl.mode_ctrl_en");
_Static_assert(offsetof(VencMBModeCtrl, p_info) == 4, "VencMBModeCtrl.p_info");

/* B.4.7 rate-control block */
_Static_assert(sizeof(VencRcParam) == 128, "VencRcParam is 128 bytes");
_Static_assert(offsetof(VencRcParam, eRcMode) == 0, "VencRcParam.eRcMode");
_Static_assert(offsetof(VencRcParam, sVbrParam) == 56, "VencRcParam.sVbrParam");
_Static_assert(offsetof(VencRcParam, sFixQp) == 68, "VencRcParam.sFixQp");
_Static_assert(offsetof(VencRcParam, sQpMap) == 80, "VencRcParam.sQpMap");

/* B.4.8 H.264 parameter block */
_Static_assert(sizeof(VencH264Param) == 200, "VencH264Param is 200 bytes");
_Static_assert(offsetof(VencH264Param, sProfileLevel) == 0, "VencH264Param.sProfileLevel");
_Static_assert(offsetof(VencH264Param, bEntropyCodingCABAC) == 8, "VencH264Param.bEntropyCodingCABAC");
_Static_assert(offsetof(VencH264Param, sQPRange) == 12, "VencH264Param.sQPRange");
_Static_assert(offsetof(VencH264Param, nFramerate) == 20, "VencH264Param.nFramerate");
_Static_assert(offsetof(VencH264Param, nSrcFramerate) == 24, "VencH264Param.nSrcFramerate");
_Static_assert(offsetof(VencH264Param, nBitrate) == 28, "VencH264Param.nBitrate");
_Static_assert(offsetof(VencH264Param, nMaxKeyInterval) == 32, "VencH264Param.nMaxKeyInterval");
_Static_assert(offsetof(VencH264Param, nCodingMode) == 36, "VencH264Param.nCodingMode");
_Static_assert(offsetof(VencH264Param, sGopParam) == 40, "VencH264Param.sGopParam");
_Static_assert(offsetof(VencH264Param, sRcParam) == 72, "VencH264Param.sRcParam");

/* B.4.9 base configuration */
_Static_assert(sizeof(VencBaseConfig) == 44, "VencBaseConfig is 44 bytes");
_Static_assert(offsetof(VencBaseConfig, bEncH264Nalu) == 0, "VencBaseConfig.bEncH264Nalu");
_Static_assert(offsetof(VencBaseConfig, nInputWidth) == 4, "VencBaseConfig.nInputWidth");
_Static_assert(offsetof(VencBaseConfig, nInputHeight) == 8, "VencBaseConfig.nInputHeight");
_Static_assert(offsetof(VencBaseConfig, nDstWidth) == 12, "VencBaseConfig.nDstWidth");
_Static_assert(offsetof(VencBaseConfig, nDstHeight) == 16, "VencBaseConfig.nDstHeight");
_Static_assert(offsetof(VencBaseConfig, nStride) == 20, "VencBaseConfig.nStride");
_Static_assert(offsetof(VencBaseConfig, eInputFormat) == 24, "VencBaseConfig.eInputFormat");
_Static_assert(offsetof(VencBaseConfig, memops) == 28, "VencBaseConfig.memops");
_Static_assert(offsetof(VencBaseConfig, veOpsS) == 32, "VencBaseConfig.veOpsS");
_Static_assert(offsetof(VencBaseConfig, pVeOpsSelf) == 36, "VencBaseConfig.pVeOpsSelf");
_Static_assert(offsetof(VencBaseConfig, bOnlyWbFlag) == 40, "VencBaseConfig.bOnlyWbFlag");
_Static_assert(offsetof(VencBaseConfig, bLbcLossyComEnFlag2x) == 41, "VencBaseConfig.bLbcLossyComEnFlag2x");
_Static_assert(offsetof(VencBaseConfig, bLbcLossyComEnFlag2_5x) == 42, "VencBaseConfig.bLbcLossyComEnFlag2_5x");
_Static_assert(offsetof(VencBaseConfig, bIsVbvNoCache) == 43, "VencBaseConfig.bIsVbvNoCache");

/* B.4.10 input picture */
_Static_assert(sizeof(VencInputBuffer) == 344, "VencInputBuffer is 344 bytes");
_Static_assert(offsetof(VencInputBuffer, nID) == 0, "VencInputBuffer.nID");
_Static_assert(offsetof(VencInputBuffer, nPts) == 8, "VencInputBuffer.nPts");
_Static_assert(offsetof(VencInputBuffer, nFlag) == 16, "VencInputBuffer.nFlag");
_Static_assert(offsetof(VencInputBuffer, pAddrPhyY) == 20, "VencInputBuffer.pAddrPhyY");
_Static_assert(offsetof(VencInputBuffer, pAddrPhyC) == 24, "VencInputBuffer.pAddrPhyC");
_Static_assert(offsetof(VencInputBuffer, pAddrVirY) == 28, "VencInputBuffer.pAddrVirY");
_Static_assert(offsetof(VencInputBuffer, pAddrVirC) == 32, "VencInputBuffer.pAddrVirC");
_Static_assert(offsetof(VencInputBuffer, bEnableCorp) == 36, "VencInputBuffer.bEnableCorp");
_Static_assert(offsetof(VencInputBuffer, sCropInfo) == 40, "VencInputBuffer.sCropInfo");
_Static_assert(offsetof(VencInputBuffer, ispPicVar) == 56, "VencInputBuffer.ispPicVar");
_Static_assert(offsetof(VencInputBuffer, ispPicVarChroma) == 60, "VencInputBuffer.ispPicVarChroma");
_Static_assert(offsetof(VencInputBuffer, bUseInputBufferRoi) == 64, "VencInputBuffer.bUseInputBufferRoi");
_Static_assert(offsetof(VencInputBuffer, roi_param) == 68, "VencInputBuffer.roi_param");
_Static_assert(offsetof(VencInputBuffer, bAllocMemSelf) == 324, "VencInputBuffer.bAllocMemSelf");
_Static_assert(offsetof(VencInputBuffer, nShareBufFd) == 328, "VencInputBuffer.nShareBufFd");
_Static_assert(offsetof(VencInputBuffer, bUseCsiColorFormat) == 332, "VencInputBuffer.bUseCsiColorFormat");
_Static_assert(offsetof(VencInputBuffer, eCsiColorFormat) == 336, "VencInputBuffer.eCsiColorFormat");
_Static_assert(offsetof(VencInputBuffer, envLV) == 340, "VencInputBuffer.envLV");

/* B.4.11 output frame and frame info */
_Static_assert(sizeof(VencOutputBuffer) == 64, "VencOutputBuffer is 64 bytes");
_Static_assert(offsetof(VencOutputBuffer, nID) == 0, "VencOutputBuffer.nID");
_Static_assert(offsetof(VencOutputBuffer, nPts) == 8, "VencOutputBuffer.nPts");
_Static_assert(offsetof(VencOutputBuffer, nFlag) == 16, "VencOutputBuffer.nFlag");
_Static_assert(offsetof(VencOutputBuffer, nSize0) == 20, "VencOutputBuffer.nSize0");
_Static_assert(offsetof(VencOutputBuffer, nSize1) == 24, "VencOutputBuffer.nSize1");
_Static_assert(offsetof(VencOutputBuffer, pData0) == 28, "VencOutputBuffer.pData0");
_Static_assert(offsetof(VencOutputBuffer, pData1) == 32, "VencOutputBuffer.pData1");
_Static_assert(offsetof(VencOutputBuffer, frame_info) == 36, "VencOutputBuffer.frame_info");
_Static_assert(offsetof(VencOutputBuffer, nSize2) == 56, "VencOutputBuffer.nSize2");
_Static_assert(offsetof(VencOutputBuffer, pData2) == 60, "VencOutputBuffer.pData2");
_Static_assert(sizeof(FrameInfo) == 20, "FrameInfo is 20 bytes");
_Static_assert(offsetof(FrameInfo, CurrQp) == 0, "FrameInfo.CurrQp");
_Static_assert(offsetof(FrameInfo, avQp) == 4, "FrameInfo.avQp");
_Static_assert(offsetof(FrameInfo, nGopIndex) == 8, "FrameInfo.nGopIndex");
_Static_assert(offsetof(FrameInfo, nFrameIndex) == 12, "FrameInfo.nFrameIndex");
_Static_assert(offsetof(FrameInfo, nTotalIndex) == 16, "FrameInfo.nTotalIndex");

/* B.4.12 header blob */
_Static_assert(sizeof(VencHeaderData) == 8, "VencHeaderData is 8 bytes");
_Static_assert(offsetof(VencHeaderData, pBuffer) == 0, "VencHeaderData.pBuffer");
_Static_assert(offsetof(VencHeaderData, nLength) == 4, "VencHeaderData.nLength");

/* B.4.13 overlay region header and block */
_Static_assert(sizeof(VencOverlayCoverYuvS) == 4, "VencOverlayCoverYuvS is 4 bytes");
_Static_assert(sizeof(VencOverlayHeaderS) == 40, "VencOverlayHeaderS is 40 bytes");
_Static_assert(offsetof(VencOverlayHeaderS, start_mb_x) == 0, "VencOverlayHeaderS.start_mb_x");
_Static_assert(offsetof(VencOverlayHeaderS, end_mb_x) == 2, "VencOverlayHeaderS.end_mb_x");
_Static_assert(offsetof(VencOverlayHeaderS, start_mb_y) == 4, "VencOverlayHeaderS.start_mb_y");
_Static_assert(offsetof(VencOverlayHeaderS, end_mb_y) == 6, "VencOverlayHeaderS.end_mb_y");
_Static_assert(offsetof(VencOverlayHeaderS, extra_alpha_flag) == 8, "VencOverlayHeaderS.extra_alpha_flag");
_Static_assert(offsetof(VencOverlayHeaderS, extra_alpha) == 9, "VencOverlayHeaderS.extra_alpha");
_Static_assert(offsetof(VencOverlayHeaderS, cover_yuv) == 10, "VencOverlayHeaderS.cover_yuv");
_Static_assert(offsetof(VencOverlayHeaderS, overlay_type) == 16, "VencOverlayHeaderS.overlay_type");
_Static_assert(offsetof(VencOverlayHeaderS, overlay_blk_addr) == 20, "VencOverlayHeaderS.overlay_blk_addr");
_Static_assert(offsetof(VencOverlayHeaderS, bitmap_size) == 24, "VencOverlayHeaderS.bitmap_size");
_Static_assert(offsetof(VencOverlayHeaderS, bforce_reverse_flag) == 28, "VencOverlayHeaderS.bforce_reverse_flag");
_Static_assert(offsetof(VencOverlayHeaderS, reverse_unit_mb_w_minus1) == 32, "VencOverlayHeaderS.reverse_unit_mb_w_minus1");
_Static_assert(offsetof(VencOverlayHeaderS, reverse_unit_mb_h_minus1) == 36, "VencOverlayHeaderS.reverse_unit_mb_h_minus1");
_Static_assert(sizeof(VencOverlayInfoS) == 2576, "VencOverlayInfoS is 2576 bytes");
_Static_assert(offsetof(VencOverlayInfoS, blk_num) == 0, "VencOverlayInfoS.blk_num");
_Static_assert(offsetof(VencOverlayInfoS, argb_type) == 4, "VencOverlayInfoS.argb_type");
_Static_assert(offsetof(VencOverlayInfoS, overlayHeaderList) == 8, "VencOverlayInfoS.overlayHeaderList");
_Static_assert(offsetof(VencOverlayInfoS, invert_mode) == 2568, "VencOverlayInfoS.invert_mode");
_Static_assert(offsetof(VencOverlayInfoS, invert_threshold) == 2572, "VencOverlayInfoS.invert_threshold");

/* B.4.14 size, VBV info, temporal-skip block */
_Static_assert(sizeof(VencSize) == 8, "VencSize is 8 bytes");
_Static_assert(offsetof(VencSize, nWidth) == 0, "VencSize.nWidth");
_Static_assert(offsetof(VencSize, nHeight) == 4, "VencSize.nHeight");
_Static_assert(sizeof(VbvInfo) == 16, "VbvInfo is 16 bytes");
_Static_assert(offsetof(VbvInfo, vbv_size) == 0, "VbvInfo.vbv_size");
_Static_assert(offsetof(VbvInfo, coded_frame_num) == 4, "VbvInfo.coded_frame_num");
_Static_assert(offsetof(VbvInfo, coded_size) == 8, "VbvInfo.coded_size");
_Static_assert(offsetof(VbvInfo, maxFrameLen) == 12, "VbvInfo.maxFrameLen");
_Static_assert(sizeof(VencH264SVCSkip) == 28, "VencH264SVCSkip is 28 bytes");
_Static_assert(offsetof(VencH264SVCSkip, nTemporalSVC) == 0, "VencH264SVCSkip.nTemporalSVC");
_Static_assert(offsetof(VencH264SVCSkip, nSkipFrame) == 4, "VencH264SVCSkip.nSkipFrame");
_Static_assert(offsetof(VencH264SVCSkip, bEnableLayerRatio) == 8, "VencH264SVCSkip.bEnableLayerRatio");
_Static_assert(offsetof(VencH264SVCSkip, nLayerRatio) == 12, "VencH264SVCSkip.nLayerRatio");

/* B.4.15 allocation request */
_Static_assert(sizeof(VencAllocateBufferParam) == 12, "VencAllocateBufferParam is 12 bytes");
_Static_assert(offsetof(VencAllocateBufferParam, nBufferNum) == 0, "VencAllocateBufferParam.nBufferNum");
_Static_assert(offsetof(VencAllocateBufferParam, nSizeY) == 4, "VencAllocateBufferParam.nSizeY");
_Static_assert(offsetof(VencAllocateBufferParam, nSizeC) == 8, "VencAllocateBufferParam.nSizeC");

/* B.3 device table: 12 slots, slot n at 4 * n */
_Static_assert(sizeof(VENC_DEVICE) == 48, "VENC_DEVICE is 48 bytes");
_Static_assert(offsetof(VENC_DEVICE, name) == 0, "VENC_DEVICE.name");
_Static_assert(offsetof(VENC_DEVICE, open) == 4, "VENC_DEVICE.open");
_Static_assert(offsetof(VENC_DEVICE, init) == 8, "VENC_DEVICE.init");
_Static_assert(offsetof(VENC_DEVICE, uninit) == 12, "VENC_DEVICE.uninit");
_Static_assert(offsetof(VENC_DEVICE, close) == 16, "VENC_DEVICE.close");
_Static_assert(offsetof(VENC_DEVICE, encode) == 20, "VENC_DEVICE.encode");
_Static_assert(offsetof(VENC_DEVICE, GetParameter) == 24, "VENC_DEVICE.GetParameter");
_Static_assert(offsetof(VENC_DEVICE, SetParameter) == 28, "VENC_DEVICE.SetParameter");
_Static_assert(offsetof(VENC_DEVICE, ValidBitStreamFrameNum) == 32, "VENC_DEVICE.ValidBitStreamFrameNum");
_Static_assert(offsetof(VENC_DEVICE, GetOneBitStreamFrame) == 36, "VENC_DEVICE.GetOneBitStreamFrame");
_Static_assert(offsetof(VENC_DEVICE, FreeOneBitStreamFrame) == 40, "VENC_DEVICE.FreeOneBitStreamFrame");
_Static_assert(offsetof(VENC_DEVICE, ResetBitStreamFrame) == 44, "VENC_DEVICE.ResetBitStreamFrame");
#endif

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_VENC_TYPES_H */
