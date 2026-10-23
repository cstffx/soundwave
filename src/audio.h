/*
 * audio.h - decodificacion de audio a PCM float entrelazado.
 *
 * Formatos: WAV, MP3, FLAC (dr_libs) y OGG Vorbis (stb_vorbis).
 */
#ifndef TW_AUDIO_H
#define TW_AUDIO_H

#include <stdint.h>

typedef struct {
    float   *samples;      /* channels * frames, entrelazado, rango [-1,1] */
    int      channels;
    int      sample_rate;
    uint64_t frames;       /* muestras por canal */
} tw_audio;

/* Carga y decodifica el archivo completo. Devuelve 0 en exito. */
int tw_audio_load(const char *path, tw_audio *out);
void tw_audio_free(tw_audio *a);

/* Mezcla a mono. El buffer devuelto se libera con free(). */
float *tw_audio_to_mono(const tw_audio *a, uint64_t *frames_out);

double tw_audio_duration(const tw_audio *a);

#endif /* TW_AUDIO_H */
