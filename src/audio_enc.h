/*
 * audio_enc.h - encoder AAC (fdk-aac).
 *
 * Salida: access units AAC crudos (transmux TT_MP4_RAW), es decir sin cabecera
 * ADTS, que es lo que espera un track AAC dentro de MP4.
 */
#ifndef TW_AUDIO_ENC_H
#define TW_AUDIO_ENC_H

#include <stdint.h>

typedef struct tw_aacenc tw_aacenc;

typedef struct {
    int sample_rate;   /* 44100, 48000... */
    int channels;      /* 1 o 2 */
    int bitrate;       /* bits por segundo, p.ej. 32000 o 48000 */
    int he_aac;        /* 1 = HE-AAC (SBR), 0 = AAC-LC */
} tw_aac_cfg;

/* Se llama una vez por cada access unit generada. */
typedef int (*tw_aac_sink_fn)(void *ctx, const uint8_t *data, int len);

int  tw_aac_open(tw_aacenc **out, const tw_aac_cfg *cfg);
void tw_aac_close(tw_aacenc *enc);

/* AudioSpecificConfig (DecoderSpecificInfo) para el muxer. */
const uint8_t *tw_aac_dsi(tw_aacenc *enc, int *len);

/* Muestras que consume cada access unit (1024 en LC, 2048 en HE-AAC). */
int tw_aac_frame_samples(const tw_aacenc *enc);

/* Alimenta PCM interleazado int16. Emite los access units completos. */
int tw_aac_encode(tw_aacenc *enc, const int16_t *pcm, int sample_count,
                  tw_aac_sink_fn sink, void *ctx);

/* Drena lo que quede en el buffer interno. */
int tw_aac_flush(tw_aacenc *enc, tw_aac_sink_fn sink, void *ctx);

#endif /* TW_AUDIO_ENC_H */
