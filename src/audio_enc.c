#include "audio_enc.h"

#include <aacenc_lib.h>

#include <stdlib.h>
#include <string.h>

#define TW_AAC_OUTBUF 16384

struct tw_aacenc {
    HANDLE_AACENCODER handle;
    int   sample_rate;
    int   channels;
    int   frame_samples;
    int   in_use;                 /* cuantos samples (interleazados) quedan pendientes */
    int16_t *pending;
    uint8_t dsi[64];
    int     dsi_len;
    uint8_t outbuf[TW_AAC_OUTBUF];
};

int tw_aac_open(tw_aacenc **out, const tw_aac_cfg *cfg)
{
    *out = NULL;
    if (cfg->channels < 1 || cfg->channels > 2)
        return -1;

    tw_aacenc *e = (tw_aacenc *)calloc(1, sizeof *e);
    if (!e)
        return -1;

    if (aacEncOpen(&e->handle, 0, (UINT)cfg->channels) != AACENC_OK) {
        free(e);
        return -1;
    }

    /* AOT: 2 = AAC-LC, 5 = HE-AAC (SBR) */
    UINT aot = cfg->he_aac ? 5 : 2;
    UINT chmode = (cfg->channels == 1) ? MODE_1 : MODE_2;

    if (aacEncoder_SetParam(e->handle, AACENC_AOT, aot) != AACENC_OK ||
        aacEncoder_SetParam(e->handle, AACENC_SAMPLERATE, (UINT)cfg->sample_rate) != AACENC_OK ||
        aacEncoder_SetParam(e->handle, AACENC_CHANNELMODE, chmode) != AACENC_OK ||
        aacEncoder_SetParam(e->handle, AACENC_BITRATE, (UINT)cfg->bitrate) != AACENC_OK ||
        aacEncoder_SetParam(e->handle, AACENC_TRANSMUX, TT_MP4_RAW) != AACENC_OK ||
        aacEncoder_SetParam(e->handle, AACENC_AFTERBURNER, 1) != AACENC_OK) {
        aacEncClose(&e->handle);
        free(e);
        return -1;
    }

    /* Inicializa y consulta el bloque de configuracion (DSI). */
    if (aacEncEncode(e->handle, NULL, NULL, NULL, NULL) != AACENC_OK) {
        aacEncClose(&e->handle);
        free(e);
        return -1;
    }

    AACENC_InfoStruct info;
    memset(&info, 0, sizeof info);
    if (aacEncInfo(e->handle, &info) != AACENC_OK) {
        aacEncClose(&e->handle);
        free(e);
        return -1;
    }

    e->sample_rate   = cfg->sample_rate;
    e->channels      = cfg->channels;
    e->frame_samples = (int)info.frameLength;

    e->dsi_len = (int)info.confSize;
    if (e->dsi_len > 0 && e->dsi_len <= (int)sizeof e->dsi)
        memcpy(e->dsi, info.confBuf, (size_t)e->dsi_len);

    *out = e;
    return 0;
}

void tw_aac_close(tw_aacenc *enc)
{
    if (!enc)
        return;
    if (enc->handle)
        aacEncClose(&enc->handle);
    free(enc->pending);
    free(enc);
}

const uint8_t *tw_aac_dsi(tw_aacenc *enc, int *len)
{
    if (len)
        *len = enc->dsi_len;
    return enc->dsi;
}

int tw_aac_frame_samples(const tw_aacenc *enc)
{
    return enc->frame_samples;
}

static int encode_once(tw_aacenc *e, const int16_t *pcm, int samples,
                       tw_aac_sink_fn sink, void *ctx)
{
    void *in_bufs[1] = { (void *)pcm };
    INT  in_ids[1]   = { IN_AUDIO_DATA };
    INT  in_sizes[1] = { samples * (int)sizeof(int16_t) };
    INT  in_el[1]    = { (int)sizeof(int16_t) };

    void *out_bufs[1] = { e->outbuf };
    INT  out_ids[1]   = { OUT_BITSTREAM_DATA };
    INT  out_sizes[1] = { TW_AAC_OUTBUF };
    INT  out_el[1]    = { 1 };

    AACENC_BufDesc in_desc, out_desc;
    AACENC_InArgs  in_args;
    AACENC_OutArgs out_args;

    memset(&in_desc, 0, sizeof in_desc);
    in_desc.numBufs = 1;
    in_desc.bufs = in_bufs;
    in_desc.bufferIdentifiers = in_ids;
    in_desc.bufSizes = in_sizes;
    in_desc.bufElSizes = in_el;

    memset(&out_desc, 0, sizeof out_desc);
    out_desc.numBufs = 1;
    out_desc.bufs = out_bufs;
    out_desc.bufferIdentifiers = out_ids;
    out_desc.bufSizes = out_sizes;
    out_desc.bufElSizes = out_el;

    memset(&in_args, 0, sizeof in_args);
    in_args.numInSamples = samples;

    memset(&out_args, 0, sizeof out_args);

    if (aacEncEncode(e->handle, &in_desc, &out_desc, &in_args, &out_args) != AACENC_OK)
        return -1;

    if (out_args.numOutBytes > 0) {
        if (sink && sink(ctx, e->outbuf, out_args.numOutBytes) != 0)
            return -1;
    }
    return out_args.numInSamples; /* interleazados consumidos, o -1 al drenar */
}

int tw_aac_encode(tw_aacenc *e, const int16_t *pcm, int sample_count,
                  tw_aac_sink_fn sink, void *ctx)
{
    int offset = 0;
    while (offset < sample_count) {
        int consumed = encode_once(e, pcm + offset, sample_count - offset, sink, ctx);
        if (consumed < 0)
            return -1;
        if (consumed == 0)
            break;   /* no acepta mas entrada: espera a flush */
        offset += consumed;
    }
    return 0;
}

int tw_aac_flush(tw_aacenc *e, tw_aac_sink_fn sink, void *ctx)
{
    /* numInSamples = -1 le dice al encoder que drene su buffer interno. */
    for (int guard = 0; guard < 64; guard++) {
        int r = encode_once(e, NULL, -1, sink, ctx);
        if (r < 0)
            return -1;
        if (r == 0)
            break;
    }
    return 0;
}
