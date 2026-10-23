#include "audio.h"

#define DR_MP3_IMPLEMENTATION
#define DR_WAV_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#include "dr_mp3.h"
#include "dr_wav.h"
#include "dr_flac.h"

/* stb_vorbis.c es autocontenido: se compila aqui (no agregar al build aparte). */
#include "stb_vorbis.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FMT_UNKNOWN = 0, FMT_WAV, FMT_MP3, FMT_FLAC, FMT_OGG };

static int name_has_ext(const char *path, const char *ext)
{
    size_t lp = strlen(path), le = strlen(ext);
    if (lp < le)
        return 0;
    for (size_t i = 0; i < le; i++) {
        char a = path[lp - le + i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b)
            return 0;
    }
    return 1;
}

/* Deteccion por magic bytes; si no alcanza, por extension. */
static int detect_format(const char *path)
{
    unsigned char h[12];
    FILE *f = fopen(path, "rb");
    if (f) {
        size_t n = fread(h, 1, sizeof h, f);
        fclose(f);
        if (n >= 12 && !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4))
            return FMT_WAV;
        if (n >= 4 && !memcmp(h, "fLaC", 4))
            return FMT_FLAC;
        if (n >= 4 && !memcmp(h, "OggS", 4))
            return FMT_OGG;
        if (n >= 3 && !memcmp(h, "ID3", 3))
            return FMT_MP3;
        if (n >= 2 && h[0] == 0xFF && (h[1] & 0xE0) == 0xE0)
            return FMT_MP3;
    }
    if (name_has_ext(path, ".wav") || name_has_ext(path, ".wave"))
        return FMT_WAV;
    if (name_has_ext(path, ".mp3"))
        return FMT_MP3;
    if (name_has_ext(path, ".flac"))
        return FMT_FLAC;
    if (name_has_ext(path, ".ogg") || name_has_ext(path, ".oga"))
        return FMT_OGG;
    return FMT_UNKNOWN;
}

static int load_wav(const char *path, tw_audio *out)
{
    unsigned int ch = 0, sr = 0;
    drwav_uint64 total = 0;
    float *p = drwav_open_file_and_read_pcm_frames_f32(path, &ch, &sr, &total, NULL);
    if (!p)
        return -1;
    out->samples = p;
    out->channels = (int)ch;
    out->sample_rate = (int)sr;
    out->frames = total;
    return 0;
}

static int load_mp3(const char *path, tw_audio *out)
{
    drmp3_config cfg;
    drmp3_uint64 total = 0;
    memset(&cfg, 0, sizeof cfg);
    float *p = drmp3_open_file_and_read_pcm_frames_f32(path, &cfg, &total, NULL);
    if (!p)
        return -1;
    out->samples = p;
    out->channels = (int)cfg.channels;
    out->sample_rate = (int)cfg.sampleRate;
    out->frames = total;
    return 0;
}

static int load_flac(const char *path, tw_audio *out)
{
    unsigned int ch = 0, sr = 0;
    drflac_uint64 total = 0;
    float *p = drflac_open_file_and_read_pcm_frames_f32(path, &ch, &sr, &total, NULL);
    if (!p)
        return -1;
    out->samples = p;
    out->channels = (int)ch;
    out->sample_rate = (int)sr;
    out->frames = total;
    return 0;
}

static int load_ogg(const char *path, tw_audio *out)
{
    int err = 0;
    stb_vorbis *v = stb_vorbis_open_filename(path, &err, NULL);
    if (!v)
        return -1;

    stb_vorbis_info info = stb_vorbis_get_info(v);
    if (info.channels <= 0) {
        stb_vorbis_close(v);
        return -1;
    }

    unsigned int total = stb_vorbis_stream_length_in_samples(v);
    if (total == 0) {
        /* Vorbis sin granule al inicio: estimar por duracion. */
        total = (unsigned int)(stb_vorbis_stream_length_in_seconds(v) * info.sample_rate);
    }
    if (total == 0)
        total = (unsigned int)(info.sample_rate * 60);

    size_t nfloat = (size_t)total * (size_t)info.channels;
    float *buf = (float *)malloc(nfloat * sizeof(float));
    if (!buf) {
        stb_vorbis_close(v);
        return -1;
    }

    int frames = stb_vorbis_get_samples_float_interleaved(v, info.channels, buf, (int)nfloat);
    stb_vorbis_close(v);
    if (frames <= 0) {
        free(buf);
        return -1;
    }

    out->samples = buf;
    out->channels = info.channels;
    out->sample_rate = (int)info.sample_rate;
    out->frames = (uint64_t)frames;
    return 0;
}

int tw_audio_load(const char *path, tw_audio *out)
{
    memset(out, 0, sizeof *out);
    int fmt = detect_format(path);
    int rc;

    switch (fmt) {
    case FMT_WAV:  rc = load_wav(path, out);  break;
    case FMT_MP3:  rc = load_mp3(path, out);  break;
    case FMT_FLAC: rc = load_flac(path, out); break;
    case FMT_OGG:  rc = load_ogg(path, out);  break;
    default:       rc = -1;                   break;
    }

    if (rc != 0 || out->frames == 0 || out->channels == 0) {
        memset(out, 0, sizeof *out);
        return -1;
    }
    return 0;
}

void tw_audio_free(tw_audio *a)
{
    /* Los tres decodificadores de dr_libs usan malloc/free por defecto. */
    free(a->samples);
    memset(a, 0, sizeof *a);
}

float *tw_audio_to_mono(const tw_audio *a, uint64_t *frames_out)
{
    if (!a->samples || !a->frames)
        return NULL;

    float *mono = (float *)malloc((size_t)a->frames * sizeof(float));
    if (!mono)
        return NULL;

    int ch = a->channels;
    if (ch == 1) {
        memcpy(mono, a->samples, (size_t)a->frames * sizeof(float));
    } else {
        const float inv = 1.0f / (float)ch;
        for (uint64_t i = 0; i < a->frames; i++) {
            const float *s = a->samples + i * (uint64_t)ch;
            float acc = 0.0f;
            for (int c = 0; c < ch; c++)
                acc += s[c];
            mono[i] = acc * inv;
        }
    }

    if (frames_out)
        *frames_out = a->frames;
    return mono;
}

double tw_audio_duration(const tw_audio *a)
{
    if (!a->sample_rate)
        return 0.0;
    return (double)a->frames / (double)a->sample_rate;
}
