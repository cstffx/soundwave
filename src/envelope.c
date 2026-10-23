#include "envelope.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void tw_env_cfg_default(tw_env_cfg *cfg)
{
    cfg->use_rms  = 1;
    cfg->attack   = 0.55f;
    cfg->release  = 0.12f;
    cfg->floor_db = -48.0f;
    cfg->gain_db  = 0.0f;
    cfg->gamma    = 0.7f;
}

/* Valor crudo (RMS o pico) de la ventana [start,end) sobre la senal mono. */
static float window_value(const float *mono, uint64_t start, uint64_t end, int use_rms)
{
    if (end <= start)
        return 0.0f;

    if (!use_rms) {
        float peak = 0.0f;
        for (uint64_t i = start; i < end; i++) {
            float a = fabsf(mono[i]);
            if (a > peak)
                peak = a;
        }
        return peak;
    }

    double acc = 0.0;
    for (uint64_t i = start; i < end; i++)
        acc += (double)mono[i] * (double)mono[i];
    return (float)sqrt(acc / (double)(end - start));
}

int tw_envelope_build(const tw_audio *a, int fps, const tw_env_cfg *cfg, tw_envelope *out)
{
    memset(out, 0, sizeof *out);
    if (fps <= 0)
        return -1;

    uint64_t nframes = 0;
    float *mono = tw_audio_to_mono(a, &nframes);
    if (!mono || !nframes) {
        free(mono);
        return -1;
    }

    const double spf = (double)a->sample_rate / (double)fps;  /* samples per frame */
    const int n = (int)ceil((double)nframes / spf);
    if (n <= 0) {
        free(mono);
        return -1;
    }

    float *raw = (float *)malloc((size_t)n * sizeof(float));
    if (!raw) {
        free(mono);
        return -1;
    }

    for (int i = 0; i < n; i++) {
        uint64_t s = (uint64_t)((double)i * spf);
        uint64_t e = (uint64_t)((double)(i + 1) * spf);
        if (e <= s)
            e = s + 1;
        if (e > nframes)
            e = nframes;
        if (s >= nframes)
            s = nframes - 1;
        raw[i] = window_value(mono, s, e, cfg->use_rms);
    }
    free(mono);

    /* Normalizacion: percentil alto para no dejar que un pico aislado aplaste
     * toda la onda. */
    float peak = 0.0f;
    for (int i = 0; i < n; i++)
        if (raw[i] > peak)
            peak = raw[i];
    if (peak <= 1e-9f)
        peak = 1e-9f;

    const float floor_lin = powf(10.0f, cfg->floor_db / 20.0f);
    const float gain_lin  = powf(10.0f, cfg->gain_db / 20.0f);

    float *v = (float *)malloc((size_t)n * sizeof(float));
    if (!v) {
        free(raw);
        return -1;
    }

    const float inv_range = 1.0f / (1.0f - floor_lin);
    float env = 0.0f;
    for (int i = 0; i < n; i++) {
        float x = raw[i] / peak * gain_lin;
        if (x > 1.0f)
            x = 1.0f;
        if (x < floor_lin)
            x = floor_lin;

        /* Mapeo a dB: hace visible la parte baja sin que domine el pico. */
        float t = (x - floor_lin) * inv_range;
        if (cfg->gamma != 1.0f && cfg->gamma > 0.0f)
            t = powf(t, cfg->gamma);

        /* Follower asimetrico: sube rapido, baja lento. */
        float coeff = (t > env) ? cfg->attack : cfg->release;
        env += coeff * (t - env);
        if (env < 0.0f)
            env = 0.0f;
        if (env > 1.0f)
            env = 1.0f;
        v[i] = env;
    }
    free(raw);

    out->v = v;
    out->n = n;
    out->fps = fps;
    return 0;
}

void tw_envelope_free(tw_envelope *e)
{
    free(e->v);
    memset(e, 0, sizeof *e);
}
