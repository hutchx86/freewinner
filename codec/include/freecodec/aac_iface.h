/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* AAC-LC encoder binary interface (spec r2/04). The consumer owns the PCM ring,
 * stream parameters and control block and supplies the two PCM pull functions.
 * Names marked "fixed" are link/ABI facts; layouts (32-bit ARM EABI) are
 * checked at the end. */

#ifndef FREECODEC_AAC_IFACE_H
#define FREECODEC_AAC_IFACE_H

#include "freecodec/abi_check.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------ result codes */

/* Return values of the object's operations (spec r2/04 section 4). Only the
 * success name is fixed; the numeric values are what the consumer compares. */
typedef enum fc_aac_result {
    FC_AAC_INPUT_ENDED     = -2,  /* drain requested, < 1 frame of PCM left  */
    FC_AAC_FAILED          = -1,  /* bad parameters, codec or buffer failure */
    ERR_AUDIO_ENC_NONE     = 0,   /* success (fixed name)                     */
    FC_AAC_WANT_PCM        = 1,   /* not yet a whole frame of PCM; retry      */
    FC_AAC_OUTPUT_STARVED  = 2    /* output-side underflow; never produced   */
} fc_aac_result;

/* Consumer commands in the control block's command word. */
#define FC_AAC_CMD_RUN    0u
#define FC_AAC_CMD_DRAIN  5u      /* input has ended: flush, then report -2 */

/* Capacity of the stream-header scratch in the control block. */
#define FC_AAC_HEADER_CAP 1024

/* --------------------------------------------------------------- records */

/* PCM ring descriptor, consumer-owned (32 bytes); the encoder writes only the
 * status word. Type and first six member names are fixed. */
typedef struct __pcm_buf_manager {
    unsigned char *pBufStart;     /* ring storage                        */
    int            uBufTotalLen;  /* ring capacity, bytes                */
    unsigned char *pBufReadPtr;   /* consumer's read position            */
    int            uDataLen;      /* bytes currently held                */
    unsigned char *pBufWritPtr;   /* consumer's write position           */
    int            uFreeBufSize;  /* free bytes                          */
    int            status;        /* encoder writes 0 after each frame   */
    void          *owner;         /* consumer back-pointer; not touched  */
} __pcm_buf_manager_t;

/* Stream parameters, filled by the consumer before the first frame
 * (28 bytes). All names fixed. */
typedef struct __audio_enc_inf {
    int InSamplerate;   /* input sample rate, Hz: one of the 12 AAC rates    */
    int InChan;         /* input channels, 1 or 2                            */
    int bitrate;        /* whole-stream bit rate, bit/s; <= 0: default       */
    int SamplerBits;    /* bits per input sample; only 16 is accepted        */
    int OutSamplerate;  /* output sample rate (no resampling is offered)     */
    int OutChan;        /* output channels                                   */
    int frame_style;    /* 0: 7-byte ADTS header per frame; 1: raw frames    */
} __audio_enc_inf_t;

/* Control block shared by consumer and encoder (1048 bytes). Type name fixed;
 * members are ours. */
typedef struct __com_internal_prameter {
    unsigned int  pts_ms;       /* time of the latest frame, ms (encoder)     */
    unsigned int  gain_word;    /* reserved; not interpreted                  */
    unsigned int  frames_out;   /* frames produced so far (encoder)           */
    unsigned int  command;      /* FC_AAC_CMD_* (consumer)                    */
    unsigned char header[FC_AAC_HEADER_CAP]; /* after open: ADTS template     */
    unsigned int  header_len;   /* valid bytes in header[]                    */
    void         *codec_state;  /* encoder-private; opaque to the consumer    */
} __com_internal_prameter_t;

/* Encoder object returned by AudioAACENCEncInit (28 bytes). Struct tag and
 * all names except `backend_open` are fixed. */
struct __AudioENC_AC320 {
    __pcm_buf_manager_t       *pPcmBufManager;  /* set by the consumer */
    __audio_enc_inf_t         *AudioBsEncInf;   /* set by the consumer */
    __com_internal_prameter_t *EncoderCom;      /* set by the consumer */
    int                        backend_open;    /* 1 once the codec is open
                                                   (lazily, first frame) */

    /* Validate the parameters and write the header template. */
    int (*EncInit) (struct __AudioENC_AC320 *enc);
    /* Encode one frame into out; *out_len is the capacity on entry and the
     * bytes written on return. */
    int (*EncFrame)(struct __AudioENC_AC320 *enc, char *out, int *out_len);
    /* Release the codec state; the object itself stays valid. */
    int (*EncExit) (struct __AudioENC_AC320 *enc);
};

/* ------------------------------------------------------------- functions */

/* Exported (fixed): allocate a zeroed object with its operations set. */
struct __AudioENC_AC320 *AudioAACENCEncInit(void);
/* Exported (fixed): release the object and everything it owns; returns 0. */
int AudioAACENCEncExit(struct __AudioENC_AC320 *enc);

/* Provided by the consumer (fixed): PCM bytes available in the ring, and a
 * copy of up to `len` of them into `dst` (returns the bytes copied). */
extern int GetPcmDataSize(__pcm_buf_manager_t *ring);
extern int ReadPcmDataForEnc(void *dst, int len, __pcm_buf_manager_t *ring);

/* ------------------------------------------------------- layout checks */

#if FC_ABI_CHECK_ALL
FC_ABI_SIZE(fc_aac_result, 4);
FC_ABI_SIZE(__audio_enc_inf_t, 28);
FC_ABI_OFFSET(__audio_enc_inf_t, InSamplerate, 0);
FC_ABI_OFFSET(__audio_enc_inf_t, InChan, 4);
FC_ABI_OFFSET(__audio_enc_inf_t, bitrate, 8);
FC_ABI_OFFSET(__audio_enc_inf_t, SamplerBits, 12);
FC_ABI_OFFSET(__audio_enc_inf_t, OutSamplerate, 16);
FC_ABI_OFFSET(__audio_enc_inf_t, OutChan, 20);
FC_ABI_OFFSET(__audio_enc_inf_t, frame_style, 24);
FC_ABI_OFFSET(__com_internal_prameter_t, pts_ms, 0);
FC_ABI_OFFSET(__com_internal_prameter_t, gain_word, 4);
FC_ABI_OFFSET(__com_internal_prameter_t, frames_out, 8);
FC_ABI_OFFSET(__com_internal_prameter_t, command, 12);
FC_ABI_OFFSET(__com_internal_prameter_t, header, 16);
FC_ABI_OFFSET(__com_internal_prameter_t, header_len, 1040);
#endif

#if FC_ABI_CHECK_PTR32
FC_ABI_SIZE(__pcm_buf_manager_t, 32);
FC_ABI_OFFSET(__pcm_buf_manager_t, pBufStart, 0);
FC_ABI_OFFSET(__pcm_buf_manager_t, uBufTotalLen, 4);
FC_ABI_OFFSET(__pcm_buf_manager_t, pBufReadPtr, 8);
FC_ABI_OFFSET(__pcm_buf_manager_t, uDataLen, 12);
FC_ABI_OFFSET(__pcm_buf_manager_t, pBufWritPtr, 16);
FC_ABI_OFFSET(__pcm_buf_manager_t, uFreeBufSize, 20);
FC_ABI_OFFSET(__pcm_buf_manager_t, status, 24);
FC_ABI_OFFSET(__pcm_buf_manager_t, owner, 28);
FC_ABI_SIZE(__com_internal_prameter_t, 1048);
FC_ABI_OFFSET(__com_internal_prameter_t, codec_state, 1044);
FC_ABI_SIZE(struct __AudioENC_AC320, 28);
FC_ABI_OFFSET(struct __AudioENC_AC320, pPcmBufManager, 0);
FC_ABI_OFFSET(struct __AudioENC_AC320, AudioBsEncInf, 4);
FC_ABI_OFFSET(struct __AudioENC_AC320, EncoderCom, 8);
FC_ABI_OFFSET(struct __AudioENC_AC320, backend_open, 12);
FC_ABI_OFFSET(struct __AudioENC_AC320, EncInit, 16);
FC_ABI_OFFSET(struct __AudioENC_AC320, EncFrame, 20);
FC_ABI_OFFSET(struct __AudioENC_AC320, EncExit, 24);
#endif

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_AAC_IFACE_H */
