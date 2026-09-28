/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* Daemon view of the encoder framework: forwards the types to freecodec/venc_types.h
 * and declares the entry points (fixed link symbols). Built without -fshort-enums. */
#ifndef FMW_VENCODER_H
#define FMW_VENCODER_H

#include "freecodec/venc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *fwm_venc_handle_t;

fwm_venc_handle_t *VideoEncCreate(fwm_venc_codec_e codec);
void               VideoEncDestroy(fwm_venc_handle_t *encoder);
int                VideoEncInit(fwm_venc_handle_t *encoder, fwm_venc_base_config_t *config);
int                VideoEncGetParameter(fwm_venc_handle_t *encoder,
                                        fwm_venc_param_e index, void *param);
int                VideoEncSetParameter(fwm_venc_handle_t *encoder,
                                        fwm_venc_param_e index, void *param);
int                VideoEncoderReset(fwm_venc_handle_t *encoder);

int AddOneInputBuffer(fwm_venc_handle_t *encoder, fwm_venc_input_picture_t *buffer);
int VideoEncodeOneFrame(fwm_venc_handle_t *encoder);
int AlreadyUsedInputBuffer(fwm_venc_handle_t *encoder, fwm_venc_input_picture_t *buffer);

int ValidBitstreamFrameNum(fwm_venc_handle_t *encoder);
int GetOneBitstreamFrame(fwm_venc_handle_t *encoder, fwm_venc_output_frame_t *buffer);
int FreeOneBitStreamFrame(fwm_venc_handle_t *encoder, fwm_venc_output_frame_t *buffer);

#ifdef __cplusplus
}
#endif

#endif /* FMW_VENCODER_H */
