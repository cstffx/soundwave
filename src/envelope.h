/*
 * envelope.h - analisis de envolvente (dominio del tiempo).
 *
 * Por cada frame de video produce un valor en [0,1] que representa "cuanta
 * energia" hay en el audio durante ese instante. No requiere FFT.
 */
#ifndef TW_ENVELOPE_H
#define TW_ENVELOPE_H

#include "audio.h"

typedef struct {
    float *v;   /* n valores en [0,1] */
    int    n;
    int    fps;
} tw_envelope;

typedef struct {
    int   use_rms;      /* 1 = RMS (recomendado), 0 = pico */
    float attack;       /* 0..1: rapidez al subir   (0.55 tipico) */
    float release;      /* 0..1: rapidez al bajar   (0.12 tipico) */
    float floor_db;     /* piso dinamico, p.ej. -48 dB */
    float gain_db;      /* ajuste fino en dB */
    float gamma;        /* realce visual de la parte baja (0.7 tipico) */
} tw_env_cfg;

void tw_env_cfg_default(tw_env_cfg *cfg);

/* Construye la envolvente. fps = frames por segundo del video. */
int  tw_envelope_build(const tw_audio *a, int fps, const tw_env_cfg *cfg, tw_envelope *out);
void tw_envelope_free(tw_envelope *e);

#endif /* TW_ENVELOPE_H */
