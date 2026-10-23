/*
 * spectrum.h - energia por banda de frecuencia (requiere FFT).
 *
 * Se usa para el modo "pulse": cada barra representa una banda y su altura
 * sigue la energia de esa banda en el instante actual. A diferencia de la
 * envolvente, aqui si hace falta pasar al dominio de la frecuencia.
 */
#ifndef TW_SPECTRUM_H
#define TW_SPECTRUM_H

#include "audio.h"

typedef struct {
    int    nbands;    /* cantidad de bandas (= barras) */
    int    nframes;   /* un espectro por cuadro de video */
    int    fps;
    float *v;         /* nframes * nbands, valores en [0,1] */
} tw_spectrum;

typedef struct {
    int   fft_size;    /* 0 = automatico (2048) */
    float f_min;       /* 0 = automatico (40 Hz) */
    float f_max;       /* 0 = automatico (16 kHz o Nyquist) */
    float range_db;    /* rango dinamico por banda (36 dB tipico) */
    float gate_db;     /* una banda cuyo pico quede por debajo de esto respecto
                        * al pico global se considera vacia y no se dibuja */
    float gamma;       /* realce de la parte baja (0.8 tipico) */
    float attack;      /* 0..1: subida rapida */
    float release;     /* 0..1: caida mas lenta */
} tw_spectrum_cfg;

void tw_spectrum_cfg_default(tw_spectrum_cfg *cfg);

/* Construye el espectro. fps = cuadros por segundo del video. */
int  tw_spectrum_build(const tw_audio *a, int nbands, int fps,
                       const tw_spectrum_cfg *cfg, tw_spectrum *out);
void tw_spectrum_free(tw_spectrum *s);

#endif /* TW_SPECTRUM_H */
