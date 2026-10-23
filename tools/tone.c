/*
 * tone.c - genera un WAV de prueba (20 s) para el demo.
 *
 * No forma parte del programa: es una herramienta para no depender de ffmpeg
 * ni de tener una cancion a mano. Toca una melodia corta con acentos, para que
 * la onda del video muestre altibajos y no una linea plana.
 *
 *   cc -O2 -o tone tools/tone.c -lm
 *   ./tone salida.wav [segundos]
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void put32(FILE *f, uint32_t v)
{
    fputc(v & 255, f); fputc((v >> 8) & 255, f);
    fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f);
}
static void put16(FILE *f, uint16_t v)
{
    fputc(v & 255, f); fputc((v >> 8) & 255, f);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "tone.wav";
    double secs = argc > 2 ? atof(argv[2]) : 20.0;

    const int sr = 44100;
    long n = (long)(secs * sr);

    /* melodia en semitonos respecto de A2 (110 Hz) */
    static const int notes[] = { 0, 3, 7, 10, 7, 3, 5, 8, 12, 8, 5, 0 };
    const int nnotes = (int)(sizeof notes / sizeof notes[0]);
    double note_len = secs / 8.0;   /* 8 pulsos */

    int16_t *pcm = (int16_t *)malloc((size_t)n * sizeof(int16_t));
    if (!pcm) return 1;

    for (long i = 0; i < n; i++) {
        double t = (double)i / sr;

        /* pulso actual */
        int step = (int)(t / note_len);
        int idx = ((step / 2) % nnotes + nnotes) % nnotes;
        double freq = 110.0 * pow(2.0, notes[idx] / 12.0);
        double local = fmod(t, note_len) / note_len;   /* 0..1 dentro del pulso */

        /* envolvente de la nota: ataque rapido y caida */
        double env = exp(-4.0 * local);

        /* "bombo" cada dos pulsos */
        double beat = fmod(t, note_len * 2.0);
        double kick = exp(-18.0 * beat) * sin(2.0 * M_PI * 55.0 * beat) * 0.7;

        double v = sin(2.0 * M_PI * freq * t) * env * 0.45
                 + sin(2.0 * M_PI * freq * 2.0 * t) * env * 0.12
                 + kick;

        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;
        pcm[i] = (int16_t)lrint(v * 30000.0);
    }

    FILE *f = fopen(path, "wb");
    if (!f) { free(pcm); return 1; }

    uint32_t data_bytes = (uint32_t)(n * 2);
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + data_bytes);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    put32(f, 16);
    put16(f, 1);              /* PCM */
    put16(f, 1);              /* mono */
    put32(f, (uint32_t)sr);
    put32(f, (uint32_t)(sr * 2));   /* bytes por segundo */
    put16(f, 2);              /* alineacion */
    put16(f, 16);             /* bits por muestra */

    fwrite("data", 1, 4, f);
    put32(f, data_bytes);
    fwrite(pcm, 1, data_bytes, f);
    fclose(f);

    printf("escrito %s (%.1f s, %d Hz, mono)\n", path, secs, sr);
    free(pcm);
    return 0;
}
