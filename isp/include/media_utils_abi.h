/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* media_utils_abi.h - ABI shared by the media-utils units (32-bit ARM EABI5 layout,
 * 4-byte enums: never build with -fshort-enums). Self-contained, no vendor headers.
 * V4L2 macros and fourcc values restate Linux UAPI facts under its BSD-3-Clause
 * option; the notice is in CREDITS.md. */
#ifndef MEDIA_UTILS_ABI_H
#define MEDIA_UTILS_ABI_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Scalar aliases and common constants                                 */
/* ------------------------------------------------------------------ */

#ifndef ERRORTYPE_DEFINED
#define ERRORTYPE_DEFINED
typedef int ERRORTYPE;
#endif

#ifndef SUCCESS
#define SUCCESS 0
#endif
#ifndef FAILURE
#define FAILURE (-1)
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define PARAM_IN
#define PARAM_OUT

/* ------------------------------------------------------------------ */
/* Pixel / frame format enums (4-byte ABI)                             */
/* ------------------------------------------------------------------ */

/* Only the enumerators the media-utils units use; values are the ABI. Skipped when
 * fwm_media_enum.h (same values) is already included, e.g. with fwm_media_abi.h. */
#ifndef FWM_MEDIA_ENUM_H
typedef enum {
	FWM_MM_PIXEL_FORMAT_RGB_1BPP = 0,
	FWM_MM_PIXEL_FORMAT_RGB_1555 = 8,
	FWM_MM_PIXEL_FORMAT_RGB_8888 = 10,
	FWM_MM_PIXEL_FORMAT_YUV_PLANAR_420 = 20,
	FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_422 = 22,
	FWM_MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420 = 23,
	FWM_MM_PIXEL_FORMAT_YUYV_PACKAGE_422 = 26,
	FWM_MM_PIXEL_FORMAT_YVU_PLANAR_420 = 30,
	FWM_MM_PIXEL_FORMAT_YVU_SEMIPLANAR_422 = 31,
	FWM_MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420 = 32,
	FWM_MM_PIXEL_FORMAT_YUV_AW_AFBC = 33,
	FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_2_0X = 34,
	FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_2_5X = 35,
	FWM_MM_PIXEL_FORMAT_YUV_AW_LBC_1_0X = 36,
	FWM_MM_PIXEL_FORMAT_RAW_SBGGR8 = 37,
	FWM_MM_PIXEL_FORMAT_RAW_SGBRG8 = 38,
	FWM_MM_PIXEL_FORMAT_RAW_SGRBG8 = 39,
	FWM_MM_PIXEL_FORMAT_RAW_SRGGB8 = 40,
	FWM_MM_PIXEL_FORMAT_RAW_SBGGR10 = 41,
	FWM_MM_PIXEL_FORMAT_RAW_SGBRG10 = 42,
	FWM_MM_PIXEL_FORMAT_RAW_SGRBG10 = 43,
	FWM_MM_PIXEL_FORMAT_RAW_SRGGB10 = 44,
	FWM_MM_PIXEL_FORMAT_RAW_SBGGR12 = 45,
	FWM_MM_PIXEL_FORMAT_RAW_SGBRG12 = 46,
	FWM_MM_PIXEL_FORMAT_RAW_SGRBG12 = 47,
	FWM_MM_PIXEL_FORMAT_RAW_SRGGB12 = 48,
	FWM_MM_PIXEL_FORMAT_BUTT = 49
} fwm_pixel_format_e;

/* Embedded by value in VIDEO_FRAME_S / BITMAP_S: only the 4-byte width matters,
 * so one placeholder enumerator is enough. */
typedef enum { FWM_VIDEO_FIELD_NONE = 0 } fwm_video_field_e;
typedef enum { FWM_VIDEO_FORMAT_LINEAR = 0 } fwm_video_format_e;
typedef enum { FWM_COMPRESS_MODE_NONE = 0 } fwm_compress_mode_e;
#endif /* !FWM_MEDIA_ENUM_H */

/* ------------------------------------------------------------------ */
/* V4L2 pixel-format fourccs (pinned, no host UAPI dependency)         */
/* ------------------------------------------------------------------ */

#ifndef V4L2_FOURCC
#define V4L2_FOURCC(a, b, c, d) \
	((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | \
	 ((uint32_t)(d) << 24))
#endif

/* Standard fourccs referenced by the media-utils conversion unit. */
#ifndef V4L2_PIX_FMT_YUV420M
#define V4L2_PIX_FMT_YUV420M V4L2_FOURCC('Y', 'M', '1', '2')
#endif
#ifndef V4L2_PIX_FMT_YVU420M
#define V4L2_PIX_FMT_YVU420M V4L2_FOURCC('Y', 'M', '2', '1')
#endif
#ifndef V4L2_PIX_FMT_NV12M
#define V4L2_PIX_FMT_NV12M V4L2_FOURCC('N', 'M', '1', '2')
#endif
#ifndef V4L2_PIX_FMT_NV21M
#define V4L2_PIX_FMT_NV21M V4L2_FOURCC('N', 'M', '2', '1')
#endif
#ifndef V4L2_PIX_FMT_NV16M
#define V4L2_PIX_FMT_NV16M V4L2_FOURCC('N', 'M', '1', '6')
#endif
#ifndef V4L2_PIX_FMT_NV61M
#define V4L2_PIX_FMT_NV61M V4L2_FOURCC('N', 'M', '6', '1')
#endif
#ifndef V4L2_PIX_FMT_YUYV
#define V4L2_PIX_FMT_YUYV V4L2_FOURCC('Y', 'U', 'Y', 'V')
#endif
#ifndef V4L2_PIX_FMT_RGB555
#define V4L2_PIX_FMT_RGB555 V4L2_FOURCC('R', 'G', 'B', 'O')
#endif
#ifndef V4L2_PIX_FMT_RGB32
#define V4L2_PIX_FMT_RGB32 V4L2_FOURCC('R', 'G', 'B', '4')
#endif
#ifndef V4L2_PIX_FMT_MJPEG
#define V4L2_PIX_FMT_MJPEG V4L2_FOURCC('M', 'J', 'P', 'G')
#endif
#ifndef V4L2_PIX_FMT_JPEG
#define V4L2_PIX_FMT_JPEG V4L2_FOURCC('J', 'P', 'E', 'G')
#endif
#ifndef V4L2_PIX_FMT_H264
#define V4L2_PIX_FMT_H264 V4L2_FOURCC('H', '2', '6', '4')
#endif
#ifndef V4L2_PIX_FMT_SBGGR8
#define V4L2_PIX_FMT_SBGGR8 V4L2_FOURCC('B', 'A', '8', '1')
#endif
#ifndef V4L2_PIX_FMT_SRGGB8
#define V4L2_PIX_FMT_SRGGB8 V4L2_FOURCC('R', 'G', 'G', 'B')
#endif
#ifndef V4L2_PIX_FMT_SGBRG8
#define V4L2_PIX_FMT_SGBRG8 V4L2_FOURCC('G', 'B', 'R', 'G')
#endif
#ifndef V4L2_PIX_FMT_SGRBG8
#define V4L2_PIX_FMT_SGRBG8 V4L2_FOURCC('G', 'R', 'B', 'G')
#endif
#ifndef V4L2_PIX_FMT_SBGGR10
#define V4L2_PIX_FMT_SBGGR10 V4L2_FOURCC('B', 'G', '1', '0')
#endif
#ifndef V4L2_PIX_FMT_SRGGB10
#define V4L2_PIX_FMT_SRGGB10 V4L2_FOURCC('R', 'G', '1', '0')
#endif
#ifndef V4L2_PIX_FMT_SGBRG10
#define V4L2_PIX_FMT_SGBRG10 V4L2_FOURCC('G', 'B', '1', '0')
#endif
#ifndef V4L2_PIX_FMT_SGRBG10
#define V4L2_PIX_FMT_SGRBG10 V4L2_FOURCC('B', 'A', '1', '0')
#endif
#ifndef V4L2_PIX_FMT_SBGGR12
#define V4L2_PIX_FMT_SBGGR12 V4L2_FOURCC('B', 'G', '1', '2')
#endif
#ifndef V4L2_PIX_FMT_SRGGB12
#define V4L2_PIX_FMT_SRGGB12 V4L2_FOURCC('R', 'G', '1', '2')
#endif
#ifndef V4L2_PIX_FMT_SGBRG12
#define V4L2_PIX_FMT_SGBRG12 V4L2_FOURCC('G', 'B', '1', '2')
#endif
#ifndef V4L2_PIX_FMT_SGRBG12
#define V4L2_PIX_FMT_SGRBG12 V4L2_FOURCC('B', 'A', '1', '2')
#endif

/* Camera-pipeline compressed formats, private to the platform (not UAPI). */
#ifndef V4L2_PIX_FMT_FBC
#define V4L2_PIX_FMT_FBC V4L2_FOURCC('F', 'C', '2', '1')
#endif
#ifndef V4L2_PIX_FMT_LBC_2_0X
#define V4L2_PIX_FMT_LBC_2_0X V4L2_FOURCC('L', 'C', '2', '1')
#endif
#ifndef V4L2_PIX_FMT_LBC_2_5X
#define V4L2_PIX_FMT_LBC_2_5X V4L2_FOURCC('L', 'C', '2', '2')
#endif
#ifndef V4L2_PIX_FMT_LBC_1_0X
#define V4L2_PIX_FMT_LBC_1_0X V4L2_FOURCC('L', 'C', '2', '3')
#endif

/* ------------------------------------------------------------------ */
/* Intrusive doubly-linked list (target layout {next, prev})           */
/* ------------------------------------------------------------------ */

/* Only the node layout and operation names are ABI (the VI/OSD glue embeds this
 * node); the operations are this project's own. */
struct list_head {
	struct list_head *next;
	struct list_head *prev;
};
typedef struct list_head cdx_list;

#ifndef container_of
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))
#endif

/* An empty head links to itself, so the initialiser needs no run-time step. */
#define LIST_HEAD_INIT(name) { .next = &(name), .prev = &(name) }

/* Re-point an existing head at itself. */
static inline void cdx_list_reset(struct list_head *head)
{
	head->prev = head;
	head->next = head;
}
#define INIT_LIST_HEAD(ptr) cdx_list_reset(ptr)

static inline int
list_empty(const struct list_head *l)
{
	return l == l->next;
}

static inline void list_add(struct list_head *node, struct list_head *head)
{
	node->next = head->next;
	node->prev = head;
	head->next->prev = node;
	head->next = node;
}

static inline void list_add_tail(struct list_head *node, struct list_head *head)
{
	node->next = head;
	node->prev = head->prev;
	head->prev->next = node;
	head->prev = node;
}

static inline void list_del(struct list_head *node)
{
	node->prev->next = node->next;
	node->next->prev = node->prev;
	node->next = node;
	node->prev = node;
}

#define list_for_each(p, h) \
	for ((p) = (h)->next; (p) != (h); (p) = (p)->next)

#define list_entry(ptr, type, member) container_of(ptr, type, member)

#define list_first_entry(head, type, member) \
	container_of((head)->next, type, member)

#define list_for_each_safe(p, n, h) \
	for ((p) = (h)->next, (n) = (p)->next; (p) != (h); (p) = (n), (n) = (p)->next)

#define list_for_each_entry(pos, head, member) \
	for (pos = container_of((head)->next, __typeof__(*(pos)), member); \
	     &(pos)->member != (head); \
	     pos = container_of((pos)->member.next, __typeof__(*(pos)), member))

/* ------------------------------------------------------------------ */
/* Video frame descriptors                                             */
/* ------------------------------------------------------------------ */

/* Size 144, align 8 on the 32-bit target (two uint64_t fields force 8-byte
 * alignment; the tail is padded 140 -> 144).  Field order is ABI. */
typedef struct VIDEO_FRAME_S {
	unsigned int mWidth;
	unsigned int mHeight;
	fwm_video_field_e mField;
	fwm_pixel_format_e mPixelFormat;
	fwm_video_format_e mVideoFormat;
	fwm_compress_mode_e mCompressMode;
	unsigned int mPhyAddr[3];
	void *mpVirAddr[3];
	unsigned int mStride[3];
	unsigned int mHeaderPhyAddr[3];
	void *mpHeaderVirAddr[3];
	unsigned int mHeaderStride[3];
	short mOffsetTop;
	short mOffsetBottom;
	short mOffsetLeft;
	short mOffsetRight;
	uint64_t mpts;
	unsigned int mExposureTime;
	unsigned int mFramecnt;
	int mEnvLV;
	unsigned int mWhoSetFlag;
	uint64_t mFlagPts;
	unsigned int mFrmFlag;
} VIDEO_FRAME_S;

/* Size 152, align 8: VFrame at 0 (144), mId at 144, 4-byte tail padding. */
typedef struct VIDEO_FRAME_INFO_S {
	VIDEO_FRAME_S VFrame;
	unsigned int mId;
} VIDEO_FRAME_INFO_S;

/* BITMAP_S: pixel format, dimensions and the payload pointer. */
typedef struct BITMAP_S {
	fwm_pixel_format_e mPixelFormat;
	unsigned int mWidth;
	unsigned int mHeight;
	void *mpData;
} BITMAP_S;

/* Size 12, align 4: three signed plane byte counts. */
typedef struct VideoFrameBufferSizeInfo {
	int mYSize;
	int mUSize;
	int mVSize;
} VideoFrameBufferSizeInfo, VIDEO_FRAME_BUFFER_SIZE_INFO;

/* ------------------------------------------------------------------ */
/* Counting semaphore (target size 76, align 4)                        */
/* ------------------------------------------------------------------ */

typedef struct {
	pthread_cond_t condition;
	pthread_mutex_t mutex;
	unsigned int semval;
} cdx_sem_t;

/* ------------------------------------------------------------------ */
/* Channel descriptor and message queue                               */
/* ------------------------------------------------------------------ */

/* 4-byte enum; only the struct copy crosses the boundary, so one sentinel fixes
 * the width. Skipped when fwm_media_enum.h is in scope, as above. */
#ifndef FWM_MEDIA_ENUM_H
typedef enum { FWM_MOD_ID_PLACEHOLDER = 0 } fwm_mod_id_e;
#endif

typedef struct MPP_CHN_S {
	fwm_mod_id_e mModId;
	int mDevId;
	int mChnId;
} MPP_CHN_S;

#define MAX_MESSAGE_ELEMENTS 8

typedef struct message_t {
	int id;
	int command;
	int para0;
	int para1;
	void *mpData;
	int mDataSize;
	struct list_head mList;
} message_t;

/* Field order is ABI: VI glue embeds this struct by value. */
typedef struct message_queue_t {
	struct list_head mIdleMessageList;
	struct list_head mReadyMessageList;
	int message_count;
	pthread_mutex_t mutex;
	pthread_cond_t mCondMessageQueueChanged;
	int mWaitMessageFlag;
} message_queue_t;

/* ------------------------------------------------------------------ */
/* Frame pool                                                          */
/* ------------------------------------------------------------------ */

/* Size 160, align 8: one heap node per pool slot, frame header by value. */
typedef struct VideoFrameListInfo {
	VIDEO_FRAME_INFO_S mFrame;
	struct list_head mList;
} VideoFrameListInfo;

struct VideoBufferManager;

/* Nine-entry method table; declaration order is the ABI order and matches the
 * manager's in-struct vtable slots starting at offset 104. */
typedef struct VideoBufferManagerOps {
	VIDEO_FRAME_INFO_S *(*GetOldestValidFrame)(struct VideoBufferManager *pMgr);
	VIDEO_FRAME_INFO_S *(*GetOldestUsingFrame)(struct VideoBufferManager *pMgr);
	VIDEO_FRAME_INFO_S *(*GetSpecUsingFrameWithAddr)(struct VideoBufferManager *pMgr,
							 void *pVirAddr);
	VIDEO_FRAME_INFO_S *(*GetAllValidUsingFrame)(struct VideoBufferManager *pMgr);
	VIDEO_FRAME_INFO_S *(*getValidFrame)(struct VideoBufferManager *pMgr);
	int (*releaseFrame)(struct VideoBufferManager *pMgr,
			    VIDEO_FRAME_INFO_S *pFrame);
	int (*pushFrame)(struct VideoBufferManager *pMgr,
			 VIDEO_FRAME_INFO_S *pFrame);
	int (*usingFrmEmpty)(struct VideoBufferManager *pMgr);
	int (*waitUsingFrmEmpty)(struct VideoBufferManager *pMgr);
} VideoBufferManagerOps;

/* Size 140, align 4, heap-allocated.  The pthread objects sit between the
 * three list heads and the vtable, so the vtable begins at offset 104. */
typedef struct VideoBufferManager {
	struct list_head mFreeFrmList;
	struct list_head mValidFrmList;
	struct list_head mUsingFrmList;
	pthread_mutex_t mFrmListLock;
	pthread_cond_t mCondUsingFrmEmpty;
	int mFrameNodeNum;
	int mbWaitUsingFrmEmptyFlag;
	VideoBufferManagerOps mOps;
} VideoBufferManager;

#define VideoBufMgrGetOldestValidFrame(pMgr) \
	((pMgr)->mOps.GetOldestValidFrame(pMgr))
#define VideoBufMgrGetOldestUsingFrame(pMgr) \
	((pMgr)->mOps.GetOldestUsingFrame(pMgr))
#define VideoBufMgrGetSpecUsingFrameWithAddr(pMgr, pVirAddr) \
	((pMgr)->mOps.GetSpecUsingFrameWithAddr((pMgr), (pVirAddr)))
#define VideoBufMgrGetAllValidUsingFrame(pMgr) \
	((pMgr)->mOps.GetAllValidUsingFrame(pMgr))
#define VideoBufMgrGetValidFrame(pMgr) ((pMgr)->mOps.getValidFrame(pMgr))
#define VideoBufMgrReleaseFrame(pMgr, pFrame) \
	((pMgr)->mOps.releaseFrame((pMgr), (pFrame)))
#define VideoBufMgrPushFrame(pMgr, pFrame) \
	((pMgr)->mOps.pushFrame((pMgr), (pFrame)))
#define VideoBufMgrUsingEmpty(pMgr) ((pMgr)->mOps.usingFrmEmpty(pMgr))
#define VideoBufMgrWaitUsingEmpty(pMgr) ((pMgr)->mOps.waitUsingFrmEmpty(pMgr))

/* ------------------------------------------------------------------ */
/* Exported surface                                                    */
/* ------------------------------------------------------------------ */

/* frame_size */
ERRORTYPE getVideoFrameBufferSizeInfo(VIDEO_FRAME_INFO_S *pFrame,
				      VideoFrameBufferSizeInfo *pSizeInfo);

/* pixel_format */
fwm_pixel_format_e map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(int v4l2PixFmt);
int map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(fwm_pixel_format_e format);

/* frame_pool */
VideoBufferManager *VideoBufMgrCreate(int frmNum, int frmSize);
void VideoBufMgrDestroy(VideoBufferManager *pMgr);

/* Diagnostic seam; the default definition is weak so a consumer or test can
 * override it. The message text is not a contract. */
void media_utils_log_error(const char *reason, unsigned int detail);

#ifdef __cplusplus
}
#endif

#endif /* MEDIA_UTILS_ABI_H */
