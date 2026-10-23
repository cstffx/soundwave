#include "spectrum.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void tw_spectrum_cfg_default(tw_spectrum_cfg *cfg)
{
    cfg->fft_size = 2048;
    cfg->f_min    = 40.0f;
    cfg->f_max    = 0.0f;      /* automatico */
    cfg->range_db = 24.0f;
    cfg->gate_db  = -42.0f;
    cfg->gamma    = 0.8f;
    cfg->attack   = 0.70f;
    cfg->release  = 0.22f;
}

/*
 * FFT radix-2 iterativa (Cooley-Tukey), in-place. n debe ser potencia de 2.
 * twr/twi son la tabla de giros e^(-2*pi*i*k/n) para k < n/2.
 */
static void fft(float *re, float *im, int n, const float *twr, const float *twi)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float t;
            t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }

    for (int len = 2; len <= n; len <<= 1) {
        int half = len >> 1;
        int step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int k = 0; k < half; k++) {
                int t = k * step;
                float wr = twr[t], wi = twi[t];
                int ia = i + k, ib = ia + half;
                float vr = re[ib] * wr - im[ib] * wi;
                float vi = re[ib] * wi + im[ib] * wr;

                re[ib] = re[ia] - vr;
                im[ib] = im[ia] - vi;
                re[ia] += vr;
                im[ia] += vi;
            }
        }
    }
}

/* Magnitud de las componentes que caen en [f0, f1) (media cuadratica). */
static float band_energy(const float *mag, int nbins, float bin_hz,
                         float f0, float f1)
{
    int i0 = (int)floorf(f0 / bin_hz);
    int i1 = (int)ceilf(f1 / bin_hz);

    if (i0 < 1)
        i0 = 1;                    /* la componente DC no aporta nada visual */
    if (i1 <= i0)
        i1 = i0 + 1;
    if (i1 > nbins)
        i1 = nbins;
    if (i0 >= nbins)
        return 0.0f;

    double acc = 0.0;
    for (int i = i0; i < i1; i++)
        acc += (double)mag[i] * (double)mag[i];

    return (float)sqrt(acc / (double)(i1 - i0));
}

/* Comparador para qsort. */
static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

int tw_spectrum_build(const tw_audio *a, int nbands, int fps,
                      const tw_spectrum_cfg *cfg, tw_spectrum *out)
{
    memset(out, 0, sizeof *out);
    if (nbands < 1 || fps <= 0)
        return -1;

    uint64_t nsamples = 0;
    float *mono = tw_audio_to_mono(a, &nsamples);
    if (!mono || !nsamples) {
        free(mono);
        return -1;
    }

    const double sr = (double)a->sample_rate;

    /* tamano de ventana: potencia de 2 */
    int nfft = cfg->fft_size > 0 ? cfg->fft_size : 2048;
    int pw = 1;
    while (pw * 2 <= nfft)
        pw *= 2;
    nfft = pw;
    if (nfft < 64)
        nfft = 64;

    const int   nbins  = nfft / 2 + 1;
    const float bin_hz = (float)(sr / (double)nfft);

    /* bandas repartidas geometricamente: los graves ocupan menos ancho */
    float fmin = cfg->f_min > 0.0f ? cfg->f_min : 40.0f;
    float fmax = cfg->f_max > 0.0f ? cfg->f_max
                                   : fminf((float)sr * 0.45f, 16000.0f);
    if (fmax > (float)sr * 0.5f)
        fmax = (float)sr * 0.5f;
    if (fmax <= fmin)
        fmax = fmin * 4.0f;

    float *edges = (float *)malloc((size_t)(nbands + 1) * sizeof(float));
    float *win   = (float *)malloc((size_t)nfft * sizeof(float));
    float *twr   = (float *)malloc((size_t)(nfft / 2) * sizeof(float));
    float *twi   = (float *)malloc((size_t)(nfft / 2) * sizeof(float));
    float *re    = (float *)malloc((size_t)nfft * sizeof(float));
    float *im    = (float *)malloc((size_t)nfft * sizeof(float));
    float *mag   = (float *)malloc((size_t)nbins * sizeof(float));

    int nframes = (int)ceil((double)nsamples / sr * (double)fps);
    if (nframes < 1)
        nframes = 1;

    float *raw = (float *)malloc((size_t)nframes * (size_t)nbands * sizeof(float));
    float *v   = (float *)malloc((size_t)nframes * (size_t)nbands * sizeof(float));

    if (!edges || !win || !twr || !twi || !re || !im || !mag || !raw || !v) {
        free(edges); free(win); free(twr); free(twi);
        free(re); free(im); free(mag); free(raw); free(v); free(mono);
        return -1;
    }

    for (int b = 0; b <= nbands; b++)
        edges[b] = fmin * powf(fmax / fmin, (float)b / (float)nbands);

    /* ventana de Hann */
    for (int i = 0; i < nfft; i++)
        win[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (float)i / (float)(nfft - 1));

    for (int k = 0; k < nfft / 2; k++) {
        double ang = -2.0 * M_PI * (double)k / (double)nfft;
        twr[k] = (float)cos(ang);
        twi[k] = (float)sin(ang);
    }

    /* --- analisis: un espectro por cuadro --- */
    const float norm = 2.0f / (float)nfft;   /* compensa la ventana y la FFT */
    for (int f = 0; f < nframes; f++) {
        int64_t center = (int64_t)((double)f / (double)fps * sr);
        int64_t start  = center - nfft / 2;

        for (int i = 0; i < nfft; i++) {
            int64_t s = start + i;
            float x = (s >= 0 && s < (int64_t)nsamples) ? mono[s] : 0.0f;
            re[i] = x * win[i];
            im[i] = 0.0f;
        }

        fft(re, im, nfft, twr, twi);

        for (int k = 0; k < nbins; k++)
            mag[k] = sqrtf(re[k] * re[k] + im[k] * im[k]) * norm;

        float *row = raw + (size_t)f * (size_t)nbands;
        for (int b = 0; b < nbands; b++)
            row[b] = band_energy(mag, nbins, bin_hz, edges[b], edges[b + 1]);
    }

    free(mono);
    free(win); free(twr); free(twi); free(re); free(im); free(mag);

    /*
     * Dos referencias, cada una para un problema distinto:
     *
     *  - pico global (gmax): da la escala absoluta y sirve de puerta. Una banda
     *    cuyo pico quede muy por debajo (por ejemplo por encima del corte de un
     *    MP3) es silencio, y normalizada por si misma se amplificaria hasta
     *    parecer llena.
     *
     *  - percentil alto de cada banda (ref): da vida, porque los graves tienen
     *    muchisima mas energia que los agudos y con una sola escala los agudos
     *    quedarian muertos.
     */
    float gmax = 0.0f;
    for (size_t i = 0; i < (size_t)nframes * (size_t)nbands; i++)
        if (raw[i] > gmax)
            gmax = raw[i];
    if (gmax <= 1e-9f)
        gmax = 1e-9f;

    float *ref   = (float *)malloc((size_t)nbands * sizeof(float));
    char  *empty = (char *)calloc((size_t)nbands, 1);
    float *tmp   = (float *)malloc((size_t)nframes * sizeof(float));
    float *env   = (float *)calloc((size_t)nbands, sizeof(float));
    if (!ref || !empty || !tmp || !env) {
        free(edges); free(raw); free(v);
        free(ref); free(empty); free(tmp); free(env);
        return -1;
    }

    const float gate_db = cfg->gate_db < 0.0f ? cfg->gate_db : -42.0f;

    for (int b = 0; b < nbands; b++) {
        for (int f = 0; f < nframes; f++)
            tmp[f] = raw[(size_t)f * (size_t)nbands + b];
        qsort(tmp, (size_t)nframes, sizeof(float), cmp_float);

        float p = tmp[(int)(0.98f * (float)(nframes - 1))];
        ref[b] = p > 1e-9f ? p : 1e-9f;

        float rel_db = 20.0f * log10f(ref[b] / gmax);
        empty[b] = (rel_db < gate_db);
    }
    free(tmp);

    const float range_db = cfg->range_db > 1.0f ? cfg->range_db : 36.0f;
    const float inv_range = 1.0f / range_db;

    for (int f = 0; f < nframes; f++) {
        const float *row = raw + (size_t)f * (size_t)nbands;
        float *dst = v + (size_t)f * (size_t)nbands;

        for (int b = 0; b < nbands; b++) {
            float t = 0.0f;

            if (!empty[b]) {
                float db = 20.0f * log10f(row[b] / ref[b] + 1e-9f);
                t = (db + range_db) * inv_range;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                if (cfg->gamma > 0.0f && cfg->gamma != 1.0f)
                    t = powf(t, cfg->gamma);
            }

            float coeff = (t > env[b]) ? cfg->attack : cfg->release;
            env[b] += coeff * (t - env[b]);
            if (env[b] < 0.0f) env[b] = 0.0f;
            if (env[b] > 1.0f) env[b] = 1.0f;

            dst[b] = env[b];
        }
    }

    free(env);
    free(ref);
    free(empty);
    free(raw);
    free(edges);

    out->nbands  = nbands;
    out->nframes = nframes;
    out->fps     = fps;
    out->v       = v;
    return 0;
}

void tw_spectrum_free(tw_spectrum *s)
{
    if (!s)
        return;
    free(s->v);
    memset(s, 0, sizeof *s);
}
