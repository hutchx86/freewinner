/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Encoder support library interface (spec r2/05 part C): engine memory, the
 * bitstream ring and input queues, with the spec's fixed link names. Part-B
 * records carry only the offsets touched here; the engine ops are ve_iface.h. */

#ifndef FREECODEC_VENC_BASE_ABI_H
#define FREECODEC_VENC_BASE_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes of the support library (a subset of the device codes). */
enum vb_result {
    VB_RESULT_ERROR           = -1,
    VB_RESULT_OK              = 0,
    VB_RESULT_NO_FRAME_BUFFER = 1,
    VB_RESULT_ILLEGAL_PARAM   = 3,
    VB_RESULT_NO_RESOURCE     = 7,
    VB_RESULT_NULL_PTR        = 8
};

/* Memory kind reported by CdcIonGetMemType. */
enum vb_mem_type {
    VB_MEM_NORMAL = 0,
    VB_MEM_IOMMU  = 1
};

/* One finished frame of the bitstream ring (56 bytes): where it sits, what it
 * is, and the per-frame statistics the ring carries back. */
typedef struct vb_stream_info {
    int          nStreamOffset;            /* byte offset of the frame        */
    int          nStreamLength;            /* byte length                     */
    long long    nPts;                     /* presentation time stamp         */
    int          nFlags;
    int          nID;                      /* descriptor slot, set by the ring */
    int          CurrQp;
    int          avQp;
    int          nGopIndex;
    int          nFrameIndex;
    int          nTotalIndex;
    unsigned int nThumbLen;
    unsigned int nThumbPreExifDataLen;
} vb_stream_info;

/* Input picture (part-B record, 344 bytes); only the fields this library
 * touches are named, the gaps are copied opaquely. */
typedef struct vb_input_buffer {
    unsigned long  nID;
    unsigned char  _opaque0[16];           /* pts, flags, crop enable, crop   */
    unsigned char *pAddrPhyY;              /* luma, physical                  */
    unsigned char *pAddrPhyC;              /* chroma, physical                */
    unsigned char *pAddrVirY;              /* luma, CPU mapping               */
    unsigned char *pAddrVirC;              /* chroma, CPU mapping             */
    unsigned char  _opaque1[288];          /* the rest of the part-B record   */
    int            bAllocMemSelf;          /* allocated by this library       */
    unsigned char  _opaque2[16];           /* dma-buf fd, colour format, env  */
} vb_input_buffer;

/* Input-picture pool request (12 bytes; the part-B allocation request). */
typedef struct vb_alloc_param {
    unsigned int nBufferNum;
    unsigned int nSizeY;
    unsigned int nSizeC;
} vb_alloc_param;

/* Memory-operations table: exactly 19 function-pointer slots, slot n at 4 * n
 * on the 32-bit target (spec r2/05 C.3). */
struct vb_mem_ops {
    int   (*open)(void);
    void  (*close)(void);
    int   (*total_size)(void);
    void *(*palloc)(int size, void *ve_ops, void *ve_self);
    void *(*palloc_no_cache)(int size, void *ve_ops, void *ve_self);
    void  (*pfree)(void *mem, void *ve_ops, void *ve_self);
    void  (*flush_cache)(void *mem, int size);
    void *(*ve_get_phyaddr)(void *vir);    /* engine-visible of a virtual      */
    void *(*ve_get_viraddr)(void *phy);    /* inverse of ve_get_phyaddr        */
    void *(*cpu_get_phyaddr)(void *vir);   /* CPU physical of a virtual        */
    void *(*cpu_get_viraddr)(void *phy);   /* inverse of cpu_get_phyaddr       */
    int   (*mem_set)(void *s, int c, size_t n);
    int   (*mem_cpy)(void *dst, void *src, size_t n);
    int   (*mem_read)(void *dst, void *src, size_t n);
    int   (*mem_write)(void *dst, void *src, size_t n);
    int   (*setup)(void);
    int   (*shutdown)(void);
    void *(*palloc_secure)(int size, void *ve_ops, void *ve_self);
    unsigned int (*get_ve_addr_offset)(void);
};

/* Kernel ION request numbers and records (32-bit kernel ABI; values fixed). */
#define VB_ION_IOC_ALLOC   0xc0144900u
#define VB_ION_IOC_FREE    0xc0044901u
#define VB_ION_IOC_MAP     0xc0084902u
#define VB_ION_IOC_IMPORT  0xc0084905u
#define VB_ION_IOC_CUSTOM  0xc0084906u

#define VB_ION_IOC_FLUSH_RANGE       5u   /* issued directly                  */
#define VB_ION_CUSTOM_PHYS_ADDR      7u   /* custom sub-command               */

#define VB_ION_HEAP_SYSTEM_MASK   0x01u
#define VB_ION_HEAP_CARVEOUT_MASK 0x04u
#define VB_ION_HEAP_DMA_MASK      0x10u
#define VB_ION_FLAG_CACHED        0x01u
#define VB_ION_FLAG_CACHED_SYNC   0x02u
#define VB_ION_ALLOC_ALIGN        4096u

typedef int vb_ion_handle;

struct vb_ion_alloc_data {                 /* allocate request, 20 bytes      */
    size_t        len;
    size_t        align;
    unsigned int  heap_id_mask;
    unsigned int  flags;
    vb_ion_handle handle;                  /* out                             */
};
struct vb_ion_handle_data {                /* free request, 4 bytes           */
    vb_ion_handle handle;
};
struct vb_ion_fd_data {                    /* map/import request, 8 bytes     */
    vb_ion_handle handle;
    int           fd;                      /* out for map                     */
};
struct vb_ion_custom_data {                /* custom request, 8 bytes         */
    unsigned int  cmd;
    unsigned long arg;
};
struct vb_ion_phys_data {                  /* phys-address reply, 12 bytes    */
    vb_ion_handle handle;
    unsigned int  phys_addr;
    unsigned int  size;
};
struct vb_ion_cache_range {                /* flush range, 8 bytes            */
    long start;
    long end;                              /* exclusive                       */
};

/* ========================================================= layout checks */

#if defined(__arm__)
_Static_assert(sizeof(enum vb_result) == 4, "enum vb_result is 4 bytes");
_Static_assert(sizeof(enum vb_mem_type) == 4, "enum vb_mem_type is 4 bytes");

/* C.2 stream-frame record */
_Static_assert(sizeof(vb_stream_info) == 56, "vb_stream_info is 56 bytes");
_Static_assert(offsetof(vb_stream_info, nStreamOffset) == 0, "vb_stream_info.nStreamOffset");
_Static_assert(offsetof(vb_stream_info, nStreamLength) == 4, "vb_stream_info.nStreamLength");
_Static_assert(offsetof(vb_stream_info, nPts) == 8, "vb_stream_info.nPts");
_Static_assert(offsetof(vb_stream_info, nFlags) == 16, "vb_stream_info.nFlags");
_Static_assert(offsetof(vb_stream_info, nID) == 20, "vb_stream_info.nID");
_Static_assert(offsetof(vb_stream_info, CurrQp) == 24, "vb_stream_info.CurrQp");
_Static_assert(offsetof(vb_stream_info, avQp) == 28, "vb_stream_info.avQp");
_Static_assert(offsetof(vb_stream_info, nGopIndex) == 32, "vb_stream_info.nGopIndex");
_Static_assert(offsetof(vb_stream_info, nFrameIndex) == 36, "vb_stream_info.nFrameIndex");
_Static_assert(offsetof(vb_stream_info, nTotalIndex) == 40, "vb_stream_info.nTotalIndex");
_Static_assert(offsetof(vb_stream_info, nThumbLen) == 44, "vb_stream_info.nThumbLen");
_Static_assert(offsetof(vb_stream_info, nThumbPreExifDataLen) == 48, "vb_stream_info.nThumbPreExifDataLen");

/* C.2 input picture (the part-B 344-byte record) */
_Static_assert(sizeof(vb_input_buffer) == 344, "vb_input_buffer is 344 bytes");
_Static_assert(offsetof(vb_input_buffer, nID) == 0, "vb_input_buffer.nID");
_Static_assert(offsetof(vb_input_buffer, pAddrPhyY) == 20, "vb_input_buffer.pAddrPhyY");
_Static_assert(offsetof(vb_input_buffer, pAddrPhyC) == 24, "vb_input_buffer.pAddrPhyC");
_Static_assert(offsetof(vb_input_buffer, pAddrVirY) == 28, "vb_input_buffer.pAddrVirY");
_Static_assert(offsetof(vb_input_buffer, pAddrVirC) == 32, "vb_input_buffer.pAddrVirC");
_Static_assert(offsetof(vb_input_buffer, bAllocMemSelf) == 324, "vb_input_buffer.bAllocMemSelf");

/* C.2 allocation request */
_Static_assert(sizeof(vb_alloc_param) == 12, "vb_alloc_param is 12 bytes");
_Static_assert(offsetof(vb_alloc_param, nBufferNum) == 0, "vb_alloc_param.nBufferNum");
_Static_assert(offsetof(vb_alloc_param, nSizeY) == 4, "vb_alloc_param.nSizeY");
_Static_assert(offsetof(vb_alloc_param, nSizeC) == 8, "vb_alloc_param.nSizeC");

/* C.3 memory-operations table: 19 slots, slot n at 4 * n */
_Static_assert(sizeof(struct vb_mem_ops) == 19 * sizeof(void (*)(void)), "vb_mem_ops is 19 slots");
_Static_assert(sizeof(struct vb_mem_ops) == 76, "vb_mem_ops is 76 bytes");
_Static_assert(offsetof(struct vb_mem_ops, open) == 0, "vb_mem_ops.open");
_Static_assert(offsetof(struct vb_mem_ops, close) == 4, "vb_mem_ops.close");
_Static_assert(offsetof(struct vb_mem_ops, total_size) == 8, "vb_mem_ops.total_size");
_Static_assert(offsetof(struct vb_mem_ops, palloc) == 12, "vb_mem_ops.palloc");
_Static_assert(offsetof(struct vb_mem_ops, palloc_no_cache) == 16, "vb_mem_ops.palloc_no_cache");
_Static_assert(offsetof(struct vb_mem_ops, pfree) == 20, "vb_mem_ops.pfree");
_Static_assert(offsetof(struct vb_mem_ops, flush_cache) == 24, "vb_mem_ops.flush_cache");
_Static_assert(offsetof(struct vb_mem_ops, ve_get_phyaddr) == 28, "vb_mem_ops.ve_get_phyaddr");
_Static_assert(offsetof(struct vb_mem_ops, ve_get_viraddr) == 32, "vb_mem_ops.ve_get_viraddr");
_Static_assert(offsetof(struct vb_mem_ops, cpu_get_phyaddr) == 36, "vb_mem_ops.cpu_get_phyaddr");
_Static_assert(offsetof(struct vb_mem_ops, cpu_get_viraddr) == 40, "vb_mem_ops.cpu_get_viraddr");
_Static_assert(offsetof(struct vb_mem_ops, mem_set) == 44, "vb_mem_ops.mem_set");
_Static_assert(offsetof(struct vb_mem_ops, mem_cpy) == 48, "vb_mem_ops.mem_cpy");
_Static_assert(offsetof(struct vb_mem_ops, mem_read) == 52, "vb_mem_ops.mem_read");
_Static_assert(offsetof(struct vb_mem_ops, mem_write) == 56, "vb_mem_ops.mem_write");
_Static_assert(offsetof(struct vb_mem_ops, setup) == 60, "vb_mem_ops.setup");
_Static_assert(offsetof(struct vb_mem_ops, shutdown) == 64, "vb_mem_ops.shutdown");
_Static_assert(offsetof(struct vb_mem_ops, palloc_secure) == 68, "vb_mem_ops.palloc_secure");
_Static_assert(offsetof(struct vb_mem_ops, get_ve_addr_offset) == 72, "vb_mem_ops.get_ve_addr_offset");

/* C.4 kernel ION records */
_Static_assert(sizeof(struct vb_ion_alloc_data) == 20, "vb_ion_alloc_data is 20 bytes");
_Static_assert(offsetof(struct vb_ion_alloc_data, len) == 0, "vb_ion_alloc_data.len");
_Static_assert(offsetof(struct vb_ion_alloc_data, align) == 4, "vb_ion_alloc_data.align");
_Static_assert(offsetof(struct vb_ion_alloc_data, heap_id_mask) == 8, "vb_ion_alloc_data.heap_id_mask");
_Static_assert(offsetof(struct vb_ion_alloc_data, flags) == 12, "vb_ion_alloc_data.flags");
_Static_assert(offsetof(struct vb_ion_alloc_data, handle) == 16, "vb_ion_alloc_data.handle");
_Static_assert(sizeof(struct vb_ion_handle_data) == 4, "vb_ion_handle_data is 4 bytes");
_Static_assert(sizeof(struct vb_ion_fd_data) == 8, "vb_ion_fd_data is 8 bytes");
_Static_assert(offsetof(struct vb_ion_fd_data, handle) == 0, "vb_ion_fd_data.handle");
_Static_assert(offsetof(struct vb_ion_fd_data, fd) == 4, "vb_ion_fd_data.fd");
_Static_assert(sizeof(struct vb_ion_custom_data) == 8, "vb_ion_custom_data is 8 bytes");
_Static_assert(offsetof(struct vb_ion_custom_data, cmd) == 0, "vb_ion_custom_data.cmd");
_Static_assert(offsetof(struct vb_ion_custom_data, arg) == 4, "vb_ion_custom_data.arg");
_Static_assert(sizeof(struct vb_ion_phys_data) == 12, "vb_ion_phys_data is 12 bytes");
_Static_assert(offsetof(struct vb_ion_phys_data, handle) == 0, "vb_ion_phys_data.handle");
_Static_assert(offsetof(struct vb_ion_phys_data, phys_addr) == 4, "vb_ion_phys_data.phys_addr");
_Static_assert(offsetof(struct vb_ion_phys_data, size) == 8, "vb_ion_phys_data.size");
_Static_assert(sizeof(struct vb_ion_cache_range) == 8, "vb_ion_cache_range is 8 bytes");
_Static_assert(offsetof(struct vb_ion_cache_range, start) == 0, "vb_ion_cache_range.start");
_Static_assert(offsetof(struct vb_ion_cache_range, end) == 4, "vb_ion_cache_range.end");
_Static_assert(sizeof(vb_ion_handle) == 4, "vb_ion_handle is 4 bytes");
#endif

/* exported functions (the build marker: mk/venc_base.mk derives the shared
 * library's export list from the text after these words) */
typedef struct vb_bitstream_manager vb_bitstream_manager;
typedef struct vb_frame_manager     vb_frame_manager;

struct vb_mem_ops *MemAdapterGetOpsS(void);
int   EncAdapterInitializeMem(struct vb_mem_ops *memops);
void  EncAdpaterRelease(struct vb_mem_ops *memops);
unsigned int EncAdapterGetICVersion(void *ve_top_base);
void *__EncAdapterMemPalloc(struct vb_mem_ops *memops, int size, void *ve_ops, void *ve_self);
void  __EncAdapterMemPfree(struct vb_mem_ops *memops, void *mem, void *ve_ops, void *ve_self);
void  __EncAdapterMemFlushCache(struct vb_mem_ops *memops, void *mem, int size);
void *__EncAdapterMemGetPhysicAddress(struct vb_mem_ops *memops, void *vir);
unsigned int __EncAdapterMemGetVeAddrOffset(struct vb_mem_ops *memops);

int           CdcIonOpen(void);
int           CdcIonClose(int fd);
int           CdcIonGetMemType(void);
unsigned long CdcIonGetPhyAdr(int fd, uintptr_t handle);
int           CdcIonGetFd(int fd, uintptr_t handle);
int           CdcIonImport(int fd, int share_fd, vb_ion_handle *handle);
int           CdcIonFree(int fd, vb_ion_handle handle);

vb_bitstream_manager *BitStreamCreate(unsigned char no_cache, int size,
                                      struct vb_mem_ops *memops,
                                      void *ve_ops, void *ve_self);
void  BitStreamDestroy(vb_bitstream_manager *bs);
void *BitStreamBaseAddress(vb_bitstream_manager *bs);
void *BitStreamBasePhyAddress(vb_bitstream_manager *bs);
void *BitStreamEndPhyAddress(vb_bitstream_manager *bs);
int   BitStreamBufferSize(vb_bitstream_manager *bs);
int   BitStreamFreeBufferSize(vb_bitstream_manager *bs);
int   BitStreamFrameNum(vb_bitstream_manager *bs);
int   BitStreamWriteOffset(vb_bitstream_manager *bs);
int   BitStreamAddOneBitstream(vb_bitstream_manager *bs, vb_stream_info *info);
vb_stream_info *BitStreamGetOneBitstream(vb_bitstream_manager *bs);
int   BitStreamReturnOneBitstream(vb_bitstream_manager *bs, vb_stream_info *info);
int   BitStreamReset(vb_bitstream_manager *bs, struct vb_mem_ops *memops);

vb_frame_manager *FrameBufferManagerCreate(int slots, struct vb_mem_ops *memops,
                                           void *ve_ops, void *ve_self);
void FrameBufferManagerDestroy(vb_frame_manager *fm);
int  AddInputBuffer(vb_frame_manager *fm, vb_input_buffer *buf);
int  GetInputBuffer(vb_frame_manager *fm, vb_input_buffer *buf);
int  AddUsedInputBuffer(vb_frame_manager *fm, vb_input_buffer *buf);
int  GetUsedInputBuffer(vb_frame_manager *fm, vb_input_buffer *buf);
int  AllocateInputBuffer(vb_frame_manager *fm, vb_alloc_param *param);
int  GetOneAllocateInputBuffer(vb_frame_manager *fm, vb_input_buffer *buf);
int  FlushCacheAllocateInputBuffer(vb_frame_manager *fm, vb_input_buffer *buf);
int  ReturnOneAllocateInputBuffer(vb_frame_manager *fm, vb_input_buffer *buf);
int  ResetFrameBuffer(vb_frame_manager *fm);
unsigned int GetUnencodedBufferNum(vb_frame_manager *fm);

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_VENC_BASE_ABI_H */
