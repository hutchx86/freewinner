// SPDX-License-Identifier: AGPL-3.0-only
/* isp_dev_uapi.h - device-layer types, kernel-facing API and the replaceable
 * syscall seam. Enum-like members are uint32_t so the layout is the same with or
 * without -fshort-enums. */
#ifndef ISP_DEV_UAPI_H
#define ISP_DEV_UAPI_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <linux/media.h>
#include <linux/videodev2.h>

#include "fwi_types.h"

#define HW_ISP_DEVICE_NUM 1
#define HW_VIDEO_DEVICE_NUM 4

#ifndef VIDEO_MAX_PLANES
#define VIDEO_MAX_PLANES 8
#endif

#define ISP_LOAD_REG_SIZE   0x1000u
#define ISP_LOAD_DRAM_SIZE  0x13240u
#define ISP_STAT_TOTAL_SIZE 0xea40u

/* legacy media entity type values the target kernel reports */
#define ISP_MEDIA_ENT_T_DEVNODE          0x00010000u
#define ISP_MEDIA_ENT_T_V4L2_SUBDEV      0x00020000u
#define ISP_MEDIA_ENT_T_V4L2_SUBDEV_SENSOR 0x00020001u

/* ------------------------------------------------------------------ */
/* private V4L2 ioctls                                                */
/* ------------------------------------------------------------------ */
struct fwi_top_clk {
    uint32_t clk_rate;
};

struct fwi_h3a_cfg {
    uint32_t buf_size;
    uint32_t config_counter;
};

struct fwi_stat_req {
    void *buf;
    uint32_t buf_size;
    uint32_t frame_number;
    uint32_t config_counter;
};

struct sensor_config {
    uint32_t width;
    uint32_t height;
    uint32_t hoffset;
    uint32_t voffset;
    uint32_t hts;
    uint32_t vts;
    uint32_t pclk;
    uint32_t fps_fixed;
    uint32_t bin_factor;
    uint32_t intg_min;
    uint32_t intg_max;
    uint32_t gain_min;
    uint32_t gain_max;
    uint32_t mbus_code;
    uint32_t wdr_mode;
};

struct sensor_exp_gain {
    int32_t exp_val;
    int32_t gain_val;
    int32_t r_gain;
    int32_t b_gain;
};

struct sensor_fps {
    int32_t fps;
};

struct sensor_temp {
    int32_t temp;
};

struct fwi_act_code {
    uint32_t code;
};

struct fwi_act_init {
    uint16_t code_min;
    uint16_t code_max;
};

struct fwi_table_reg_map {
    void *addr;
    unsigned int size;
};

#define VIDIOC_SET_TOP_CLK \
    _IOWR('V', 192 + 6, struct fwi_top_clk)
#define VIDIOC_VIN_ISP_H3A_CFG \
    _IOWR('V', 192 + 31, struct fwi_h3a_cfg)
#define VIDIOC_VIN_ISP_STAT_REQ \
    _IOWR('V', 192 + 32, struct fwi_stat_req)
#define VIDIOC_VIN_ISP_STAT_EN \
    _IOWR('V', 192 + 33, unsigned int)
#define VIDIOC_VIN_SENSOR_CFG_REQ \
    _IOWR('V', 192 + 60, struct sensor_config)
#define VIDIOC_VIN_SENSOR_EXP_GAIN \
    _IOWR('V', 192 + 61, struct sensor_exp_gain)
#define VIDIOC_VIN_SENSOR_SET_FPS \
    _IOWR('V', 192 + 62, struct sensor_fps)
#define VIDIOC_VIN_SENSOR_GET_TEMP \
    _IOWR('V', 192 + 63, struct sensor_temp)
#define VIDIOC_VIN_ACT_SET_CODE \
    _IOWR('V', 192 + 64, struct fwi_act_code)
#define VIDIOC_VIN_ACT_INIT \
    _IOWR('V', 192 + 65, struct fwi_act_init)
#define VIDIOC_VIN_ISP_LOAD_REG \
    _IOWR('V', 192 + 70, struct fwi_table_reg_map)

/* ------------------------------------------------------------------ */
/* replaceable syscall seam                                           */
/* ------------------------------------------------------------------ */
struct fwi_uapi_sys {
    int   (*open)(const char *path, int flags, unsigned int mode);
    int   (*close)(int fd);
    int   (*ioctl)(int fd, unsigned long request, void *arg);
    void *(*mmap)(void *addr, size_t len, int prot, int flags, int fd, long off);
    int   (*munmap)(void *addr, size_t len);
    int   (*select)(int nfds, fd_set *r, fd_set *w, fd_set *e,
                    struct timeval *tmo);
    int   (*access)(const char *path, int mode);
    int   (*system)(const char *cmd);
    long  (*readlink)(const char *path, char *buf, size_t bufsiz);
    int   (*stat_)(const char *path, struct stat *st);
};

extern const struct fwi_uapi_sys *isp_uapi_sys;
void isp_uapi_set_sys(const struct fwi_uapi_sys *sys);
const struct fwi_uapi_sys *isp_uapi_get_sys(void);

/* ------------------------------------------------------------------ */
/* project device-layer records                                        */
/* ------------------------------------------------------------------ */
struct media_pad {
    unsigned int index;
    unsigned int flags;
    struct media_entity *entity;
};

struct media_link {
    struct media_pad *source;
    struct media_pad *sink;
    unsigned int flags;
};

struct media_entity {
    struct media_entity_desc info;
    char devname[32];
    int fd;
    struct media_pad *pads;
    unsigned int num_pads;
    struct media_link *links;
    unsigned int num_links;
};

struct media_device {
    struct media_device_info info;
    int fd;
    struct media_entity *entities;
    unsigned int num_entities;
};

struct video_plane {
    unsigned int size;
    int dma_fd;
    void *mem;
    unsigned int mem_phy;
};

struct video_buffer {
    unsigned int index;
    unsigned int bytesused;
    unsigned int frame_cnt;
    unsigned int exp_time;
    struct timeval timestamp;
    int error;
    int allocated;
    unsigned int nplanes;
    struct video_plane *planes;
};

struct buffers_pool {
    unsigned int nbufs;
    struct video_buffer *buffers;
};

struct video_fmt {
    uint32_t type;
    uint32_t memtype;
    struct v4l2_pix_format_mplane format;
    uint32_t nbufs;
    uint32_t nplanes;
    uint32_t fps;
    uint32_t capturemode;
    uint32_t use_current_win;
    uint32_t wdr_mode;
    uint32_t drop_frame_num;
    uint32_t index;
};

struct fwi_video_device {
    unsigned int id;
    int isp_id;
    struct media_entity *entity;
    uint32_t type;
    uint32_t memtype;
    struct v4l2_pix_format_mplane format;
    uint32_t nbufs;
    uint32_t nplanes;
    uint32_t capturemode;
    uint32_t use_current_win;
    uint32_t wdr_mode;
    struct buffers_pool *pool;
    uint32_t fps;
    uint32_t drop_frame_num;
};

struct hw_isp_device {
    struct media_entity sensor;
    struct media_entity subdev;
    struct media_entity stat;
    unsigned char *stats_buf;
    unsigned int stats_size;
    int load_type;
};

struct hw_isp_media_dev {
    struct media_device *mdev;
    struct hw_isp_device *isp_dev[HW_ISP_DEVICE_NUM];
    struct fwi_video_device *video_dev[HW_VIDEO_DEVICE_NUM];
    pthread_t isp_tid[HW_ISP_DEVICE_NUM];
    unsigned int isp_use_cnt[HW_ISP_DEVICE_NUM];
    int isp_sync_mode;
    int ir_flag;
    int wdr_flag;
};

/* ------------------------------------------------------------------ */
/* device-layer API                                                   */
/* ------------------------------------------------------------------ */
struct media_device *media_open(const char *path, int verbose);
void media_close(struct media_device *md);
int media_refresh_links(struct media_device *md);

struct hw_isp_media_dev *isp_md_open(const char *devname);
void isp_md_close(struct hw_isp_media_dev *isp_md);
int isp_dev_open(struct hw_isp_media_dev *md, int id);
void isp_dev_close(struct hw_isp_media_dev *md, int id);
int isp_video_open(struct hw_isp_media_dev *md, unsigned int id);
void isp_video_close(struct hw_isp_media_dev *md, unsigned int id);

int video_to_isp_id(struct fwi_video_device *video);
int video_set_fmt(struct fwi_video_device *video, struct video_fmt *vfmt);
int video_get_fmt(struct fwi_video_device *video, struct video_fmt *vfmt);
struct buffers_pool *buffers_pool_new(struct fwi_video_device *video);
void buffers_pool_delete(struct fwi_video_device *video);
int video_req_buffers(struct fwi_video_device *video, struct buffers_pool *pool);
int video_free_buffers(struct fwi_video_device *video);
int video_wait_buffer(struct fwi_video_device *video, int timeout_ms);
int video_dequeue_buffer(struct fwi_video_device *video,
                         struct video_buffer *buffer);
int video_queue_buffer(struct fwi_video_device *video, unsigned int buf_id);
int video_stream_on(struct fwi_video_device *video);
int video_stream_off(struct fwi_video_device *video);
int video_set_control(struct fwi_video_device *video, int cid, int value);
int video_get_control(struct fwi_video_device *video, int cid, int *value);
int video_set_top_clk(struct fwi_video_device *video, unsigned int rate);

int isp_sensor_get_configs(struct hw_isp_device *isp, struct sensor_config *cfg);
int isp_sensor_set_fps(struct hw_isp_device *isp, struct sensor_fps *fps);
int isp_sensor_get_temp(struct hw_isp_device *isp, struct sensor_temp *temp);
int isp_set_load_reg(struct hw_isp_device *isp, struct fwi_table_reg_map *reg);

/* graph walks */
struct media_entity *media_pipeline_head(struct media_device *md,
                                         struct media_entity *entity);
int isp_entity_to_isp_id(struct media_device *md, struct media_entity *entity);

#endif /* ISP_DEV_UAPI_H */
