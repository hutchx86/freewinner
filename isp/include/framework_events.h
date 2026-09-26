// SPDX-License-Identifier: AGPL-3.0-only
/* framework_events.h - V4L2 control/event ids used by the framework and the
 * event-loop declarations (20 §4.7, §5, §11).  Kept file name (owner Q2).
 */
#ifndef FRAMEWORK_EVENTS_H
#define FRAMEWORK_EVENTS_H

#include <linux/videodev2.h>
#include <stdint.h>

/* Legacy/vendor V4L2 control ids (20 §4.7, §11.2): user class 0x00980900,
 * camera class 0x009a0900, platform private 0x00981950.  Each is defined only
 * if the kernel headers did not already provide it, so the framework builds
 * against both the target's vendor headers and a host's upstream ones (some
 * of these -- EXPOSURE_BIAS, WHITE_BALANCE_PRESET, AUTO_BRIGHTNESS, WDR --
 * are absent from upstream v4l2-controls.h). */
#ifndef V4L2_CID_BRIGHTNESS
#define V4L2_CID_BRIGHTNESS             (0x00980900)
#endif
#ifndef V4L2_CID_CONTRAST
#define V4L2_CID_CONTRAST               (0x00980901)
#endif
#ifndef V4L2_CID_SATURATION
#define V4L2_CID_SATURATION             (0x00980902)
#endif
#ifndef V4L2_CID_HUE
#define V4L2_CID_HUE                    (0x00980903)
#endif
#ifndef V4L2_CID_AUTO_WHITE_BALANCE
#define V4L2_CID_AUTO_WHITE_BALANCE     (0x0098090c)
#endif
#ifndef V4L2_CID_GAMMA
#define V4L2_CID_GAMMA                  (0x00980910)
#endif
#ifndef V4L2_CID_EXPOSURE
#define V4L2_CID_EXPOSURE               (0x00980911)
#endif
#ifndef V4L2_CID_AUTOGAIN
#define V4L2_CID_AUTOGAIN               (0x00980912)
#endif
#ifndef V4L2_CID_GAIN
#define V4L2_CID_GAIN                   (0x00980913)
#endif
#ifndef V4L2_CID_POWER_LINE_FREQUENCY
#define V4L2_CID_POWER_LINE_FREQUENCY   (0x00980918)
#endif
#ifndef V4L2_CID_HUE_AUTO
#define V4L2_CID_HUE_AUTO               (0x00980919)
#endif
#ifndef V4L2_CID_WHITE_BALANCE_TEMPERATURE
#define V4L2_CID_WHITE_BALANCE_TEMPERATURE (0x0098091a)
#endif
#ifndef V4L2_CID_SHARPNESS
#define V4L2_CID_SHARPNESS              (0x0098091b)
#endif
#ifndef V4L2_CID_CHROMA_AGC
#define V4L2_CID_CHROMA_AGC             (0x0098091d)
#endif
#ifndef V4L2_CID_COLORFX
#define V4L2_CID_COLORFX                (0x0098091f)
#endif
#ifndef V4L2_CID_AUTO_BRIGHTNESS
#define V4L2_CID_AUTO_BRIGHTNESS        (0x00980920)
#endif
#ifndef V4L2_CID_BAND_STOP_FILTER
#define V4L2_CID_BAND_STOP_FILTER       (0x00980921)
#endif
#ifndef V4L2_CID_ILLUMINATORS_1
#define V4L2_CID_ILLUMINATORS_1         (0x00980925)
#endif
#ifndef V4L2_CID_ILLUMINATORS_2
#define V4L2_CID_ILLUMINATORS_2         (0x00980926)
#endif
#ifndef V4L2_CID_EXPOSURE_AUTO
#define V4L2_CID_EXPOSURE_AUTO          (0x009a0901)
#endif
#ifndef V4L2_CID_EXPOSURE_ABSOLUTE
#define V4L2_CID_EXPOSURE_ABSOLUTE      (0x009a0902)
#endif
#ifndef V4L2_CID_EXPOSURE_AUTO_PRIORITY
#define V4L2_CID_EXPOSURE_AUTO_PRIORITY (0x009a0903)
#endif
#ifndef V4L2_CID_FOCUS_ABSOLUTE
#define V4L2_CID_FOCUS_ABSOLUTE         (0x009a090a)
#endif
#ifndef V4L2_CID_FOCUS_RELATIVE
#define V4L2_CID_FOCUS_RELATIVE         (0x009a090b)
#endif
#ifndef V4L2_CID_FOCUS_AUTO
#define V4L2_CID_FOCUS_AUTO             (0x009a090c)
#endif
#ifndef V4L2_CID_EXPOSURE_BIAS
#define V4L2_CID_EXPOSURE_BIAS          (0x009a0913)
#endif
#ifndef V4L2_CID_WHITE_BALANCE_PRESET
#define V4L2_CID_WHITE_BALANCE_PRESET   (0x009a0914)
#endif
#ifndef V4L2_CID_WDR
#define V4L2_CID_WDR                    (0x009a0915)
#endif
#ifndef V4L2_CID_STABILISATION
#define V4L2_CID_STABILISATION          (0x009a0916)
#endif
#ifndef V4L2_CID_ISO_SENSITIVITY
#define V4L2_CID_ISO_SENSITIVITY        (0x009a0917)
#endif
#ifndef V4L2_CID_ISO_SENSITIVITY_AUTO
#define V4L2_CID_ISO_SENSITIVITY_AUTO   (0x009a0918)
#endif
#ifndef V4L2_CID_EXPOSURE_METERING
#define V4L2_CID_EXPOSURE_METERING      (0x009a0919)
#endif
#ifndef V4L2_CID_SCENE_MODE
#define V4L2_CID_SCENE_MODE             (0x009a091a)
#endif
#ifndef V4L2_CID_3A_LOCK
#define V4L2_CID_3A_LOCK                (0x009a091b)
#endif
#ifndef V4L2_CID_AUTO_FOCUS_START
#define V4L2_CID_AUTO_FOCUS_START       (0x009a091c)
#endif
#ifndef V4L2_CID_AUTO_FOCUS_STOP
#define V4L2_CID_AUTO_FOCUS_STOP        (0x009a091d)
#endif
#ifndef V4L2_CID_AUTO_FOCUS_RANGE
#define V4L2_CID_AUTO_FOCUS_RANGE       (0x009a091f)
#endif
#ifndef V4L2_CID_FLASH_LED_MODE
#define V4L2_CID_FLASH_LED_MODE         (0x009c0901)
#endif

#define ISP_CID_AE_WIN_X1   (0x00981950 + 15)
#define ISP_CID_AE_WIN_Y1   (0x00981950 + 16)
#define ISP_CID_AE_WIN_X2   (0x00981950 + 17)
#define ISP_CID_AE_WIN_Y2   (0x00981950 + 18)
#define ISP_CID_AF_WIN_X1   (0x00981950 + 19)
#define ISP_CID_AF_WIN_Y1   (0x00981950 + 20)
#define ISP_CID_AF_WIN_X2   (0x00981950 + 21)
#define ISP_CID_AF_WIN_Y2   (0x00981950 + 22)
#define V4L2_CID_FLASH_LED_MODE_V1 (0x00981950 + 23)
#define V4L2_CID_TAKE_PICTURE      (0x00981950 + 6)

#ifndef V4L2_EVENT_ALL
#define V4L2_EVENT_ALL 0
#define V4L2_EVENT_CTRL 3
#define V4L2_EVENT_FRAME_SYNC 4
#endif

#define V4L2_EVENT_PRIVATE_START 0x08000000
#define V4L2_EVENT_VIN_H3A      (V4L2_EVENT_PRIVATE_START + 0x101)
#define V4L2_EVENT_VIN_ISP_OFF  (V4L2_EVENT_PRIVATE_START + 0x103)

/* stats event payload in event.u.data (20 §4.6) */
struct isp_stat_event {
    uint32_t frame_number;
    uint16_t config_counter;
    uint8_t buf_err;
};

#endif /* FRAMEWORK_EVENTS_H */
