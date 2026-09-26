/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* AAC-LC backend (spec/aac-lc.md): frames ADTS itself and drives FAAC's public
 * encoder API for the AAC-LC core. */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "freecodec/aac_iface.h"
#include "faac.h"

#define FC_FRAME_SAMPLES 1024u
#define FC_ADTS_HEADER   7u
#define FC_OUT_CAP       4096u   /* consumer's OUT_ENCODE_BUFFER_SIZE */

typedef struct fc_aac_priv {
    faac_encoder *enc;
    uint32_t      frame_samples;
    uint32_t      max_out;
    uint32_t      channels;
    uint32_t      bits;
    int           adts;          /* 1 = prepend ADTS, 0 = raw frames */
    uint8_t      *pcm;           /* staging for exactly one frame    */
    size_t        pcm_cap;
    uint8_t      *enc_out;       /* backend output buffer            */
} fc_aac_priv;

static int fc_sf_index(unsigned fs)
{
    switch (fs) {
    case 96000: return 0;
    case 88200: return 1;
    case 64000: return 2;
    case 48000: return 3;
    case 44100: return 4;
    case 32000: return 5;
    case 24000: return 6;
    case 22050: return 7;
    case 16000: return 8;
    case 12000: return 9;
    case 11025: return 10;
    case 8000:  return 11;
    default:    return -1;
    }
}

static int fc_chan_config(unsigned c)
{
    if (c >= 1 && c <= 2) return (int)c;
    return -1;
}

/* 7-byte no-CRC ADTS header; profile 1 = AAC-LC. */
static void fc_adts_header(uint8_t h[FC_ADTS_HEADER], unsigned profile,
                           int sfidx, int chan, size_t frame_len)
{
    h[0] = 0xFF;
    h[1] = 0xF1;
    h[2] = (uint8_t)(((profile & 3u) << 6) |
                     (((unsigned)sfidx & 0xFu) << 2) |
                     (((unsigned)chan >> 2) & 1u));
    h[3] = (uint8_t)((((unsigned)chan & 3u) << 6) |
                     ((frame_len >> 11) & 0x3u));
    h[4] = (uint8_t)((frame_len >> 3) & 0xFFu);
    h[5] = (uint8_t)(((frame_len & 7u) << 5) | 0x1Fu);
    h[6] = 0xFC;
}

static int fc_backend_open(struct __AudioENC_AC320 *p, fc_aac_priv *pv)
{
    const __audio_enc_inf_t *inf = p->AudioBsEncInf;
    faac_params params;
    faac_encoder_info info;
    faac_status st;
    int sfidx, chan;

    if (inf->SamplerBits != 16 || inf->InChan < 1 || inf->InChan > 2)
        return -1;
    sfidx = fc_sf_index((unsigned)inf->InSamplerate);
    chan  = fc_chan_config((unsigned)inf->InChan);
    if (sfidx < 0 || chan < 0)
        return -1;

    faac_params_init(&params, sizeof(params));
    params.sample_rate   = (uint32_t)inf->InSamplerate;
    params.num_channels  = (uint32_t)inf->InChan;
    params.mpeg_version  = FAAC_MPEG4;
    params.object_type   = FAAC_OBJ_LOW;     /* AAC-LC, matches the bridge */
    params.output_format = FAAC_STREAM_RAW;  /* we frame ADTS ourselves    */
    params.input_format  = FAAC_INPUT_16BIT;
    params.rate_control  = FAAC_RC_AUTO;
    if (inf->bitrate > 0)
        params.bit_rate = (uint32_t)inf->bitrate / (uint32_t)inf->InChan;

    st = faac_encoder_open(&params, &pv->enc);
    if (st != FAAC_OK || pv->enc == NULL) {
        pv->enc = NULL;
        return -1;
    }

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    if (faac_encoder_get_info(pv->enc, &info) != FAAC_OK) {
        faac_encoder_close(&pv->enc);
        return -1;
    }

    pv->frame_samples = info.frame_samples ? info.frame_samples : FC_FRAME_SAMPLES;
    pv->max_out       = info.max_output_bytes ? info.max_output_bytes : FC_OUT_CAP;
    pv->channels      = (uint32_t)inf->InChan;
    pv->bits          = 16;
    pv->adts          = (inf->frame_style == 0);
    pv->pcm_cap       = (size_t)pv->frame_samples * pv->channels * (pv->bits / 8u);
    pv->pcm           = malloc(pv->pcm_cap);
    pv->enc_out       = malloc(pv->max_out);
    if (!pv->pcm || !pv->enc_out) {
        free(pv->pcm);
        free(pv->enc_out);
        pv->pcm = pv->enc_out = NULL;
        faac_encoder_close(&pv->enc);
        return -1;
    }
    return 0;
}

static int fc_enc_init(struct __AudioENC_AC320 *p)
{
    const __audio_enc_inf_t *inf;
    int sfidx, chan;

    if (p == NULL || p->AudioBsEncInf == NULL || p->EncoderCom == NULL)
        return FC_AAC_FAILED;

    inf = p->AudioBsEncInf;
    sfidx = fc_sf_index((unsigned)inf->InSamplerate);
    chan  = fc_chan_config((unsigned)inf->InChan);
    if (sfidx < 0 || chan < 0) {
        p->EncoderCom->header_len = 0;
        return FC_AAC_FAILED;
    }
    fc_adts_header(p->EncoderCom->header, 1, sfidx, chan, FC_ADTS_HEADER);
    p->EncoderCom->header_len = FC_ADTS_HEADER;
    return ERR_AUDIO_ENC_NONE;
}

static int fc_enc_frame(struct __AudioENC_AC320 *p, char *OutBuffer, int *OutBuffLen)
{
    fc_aac_priv *pv;
    unsigned need, cap;
    int got;
    uint32_t written = 0;
    uint8_t *payload;

    if (OutBuffLen == NULL)
        return FC_AAC_FAILED;
    *OutBuffLen = 0;
    if (p == NULL || OutBuffer == NULL || p->AudioBsEncInf == NULL ||
        p->EncoderCom == NULL || p->pPcmBufManager == NULL)
        return FC_AAC_FAILED;

    pv = (fc_aac_priv *)(uintptr_t)p->EncoderCom->codec_state;

    if (!p->backend_open) {
        pv = calloc(1, sizeof(*pv));
        if (pv == NULL)
            return FC_AAC_FAILED;
        if (fc_backend_open(p, pv) != 0) {
            free(pv);
            return FC_AAC_FAILED;
        }
        p->EncoderCom->codec_state = pv;
        p->backend_open = 1;
    }

    need = (unsigned)((size_t)pv->frame_samples * pv->channels * (pv->bits / 8u));

    if ((unsigned)GetPcmDataSize(p->pPcmBufManager) < need) {
        return (p->EncoderCom->command == FC_AAC_CMD_DRAIN) ? FC_AAC_INPUT_ENDED
                                              : FC_AAC_WANT_PCM;
    }

    p->EncoderCom->pts_ms = (unsigned)((uint64_t)p->EncoderCom->frames_out *
        pv->frame_samples * 1000u / (unsigned)p->AudioBsEncInf->InSamplerate);

    got = ReadPcmDataForEnc(pv->pcm, (int)need, p->pPcmBufManager);
    if (got < (int)need)
        return FC_AAC_WANT_PCM;

    payload = (uint8_t *)OutBuffer + (pv->adts ? FC_ADTS_HEADER : 0u);
    cap = FC_OUT_CAP - (pv->adts ? FC_ADTS_HEADER : 0u);

    if (faac_encoder_encode(pv->enc, pv->pcm,
                            (uint32_t)((size_t)pv->frame_samples * pv->channels),
                            pv->enc_out, pv->max_out, &written) != FAAC_OK)
        return FC_AAC_FAILED;

    if (written > cap)
        return FC_AAC_FAILED;   /* frame exceeds the consumer's buffer */

    memcpy(payload, pv->enc_out, written);

    if (written == 0) {
        *OutBuffLen = 0;
        return ERR_AUDIO_ENC_NONE;
    }

    if (pv->adts) {
        int sfidx = fc_sf_index((unsigned)p->AudioBsEncInf->InSamplerate);
        int chan  = fc_chan_config((unsigned)p->AudioBsEncInf->InChan);
        fc_adts_header((uint8_t *)OutBuffer, 1, sfidx, chan, FC_ADTS_HEADER + written);
    }

    *OutBuffLen = (int)((pv->adts ? FC_ADTS_HEADER : 0u) + written);
    p->EncoderCom->frames_out++;
    p->pPcmBufManager->status = 0;
    return ERR_AUDIO_ENC_NONE;
}

static int fc_enc_exit(struct __AudioENC_AC320 *p)
{
    fc_aac_priv *pv;

    if (p == NULL || p->EncoderCom == NULL)
        return ERR_AUDIO_ENC_NONE;

    pv = (fc_aac_priv *)(uintptr_t)p->EncoderCom->codec_state;
    if (pv != NULL) {
        if (pv->enc != NULL)
            faac_encoder_close(&pv->enc);
        free(pv->pcm);
        free(pv->enc_out);
        free(pv);
        p->EncoderCom->codec_state = NULL;
    }
    p->backend_open = 0;
    return ERR_AUDIO_ENC_NONE;
}

struct __AudioENC_AC320 *AudioAACENCEncInit(void)
{
    struct __AudioENC_AC320 *p = calloc(1, sizeof(*p));
    if (p == NULL)
        return NULL;
    p->EncInit  = fc_enc_init;
    p->EncFrame = fc_enc_frame;
    p->EncExit  = fc_enc_exit;
    return p;
}

int AudioAACENCEncExit(struct __AudioENC_AC320 *p)
{
    free(p);
    return 0;
}
