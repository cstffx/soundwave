/*
 * main.c - soundwave: de un audio a un video vertical para estados de WhatsApp.
 *
 * Pipeline: decodificar audio -> recortar el segmento pedido -> calcular la
 * envolvente -> pre-renderizar fondo/avatar -> por cada cuadro dibujar la onda
 * -> codificar H.264 (OpenH264) y AAC (fdk-aac) -> empaquetar MP4 (minimp4).
 */
#include "audio.h"
#include "envelope.h"
#include "spectrum.h"
#include "image.h"
#include "draw.h"
#include "scene.h"
#include "audio_enc.h"
#include "video_enc.h"
#include "mux.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *input;
    const char *output;
    const char *avatar;
    const char *artist;
    const char *title;
    const char *font;
    const char *credit;

    double start_sec;
    double end_sec;     /* < 0 = hasta el final */

    int width, height, fps;
    int video_kbps;
    int audio_kbps;
    int aac_he;         /* HE-AAC (SBR) */
    int channels;       /* 1 o 2 */
    int bars;           /* 0 = automatico */
    int bars_shade;     /* 1 = color de barra segun su intensidad */
    int wave_mode;      /* 0 = estatica, 1 = fluyente */
    float wave_span;    /* segundos visibles en modo fluyente */
    int profile;        /* 0 baseline, 1 main, 2 high */
    int complexity;     /* 0 baja, 1 media, 2 alta */
    int keyframe_s;
    int qp_min;         /* calidad maxima del video (menor = mejor) */

    tw_color bg, bg_lo, fg;
    long long max_bytes;   /* 0 = sin limite */
} tw_opts;

/* ------------------------------------------------------------------ */

static void usage(const char *argv0)
{
    printf(
"soundwave - video vertical (estado de WhatsApp) a partir de un audio\n"
"\n"
"Uso:\n"
"  %s -i track.mp3 -o status.mp4 --avatar foto.jpg --artist \"Nombre\" [opciones]\n"
"\n"
"Entrada / salida:\n"
"  -i, --input <archivo>     audio de entrada (wav, mp3, flac, ogg)  [req]\n"
"  -o, --output <archivo>    mp4 de salida                        [status.mp4]\n"
"      --avatar <imagen>     foto del artista (png/jpg)\n"
"      --artist <texto>      nombre del artista\n"
"      --title <texto>       titulo o subtitulo\n"
"      --font <archivo.ttf>  tipografia para los textos\n"
"                            (si se omite, se busca una del sistema)\n"
"      --credit <texto>      credito al pie del video; \"\" lo quita\n"
"                            [Made with soundwave by cstffx]\n"
"\n"
"Recorte del audio:\n"
"      --start <tiempo>      donde empieza el video                [0]\n"
"      --end <tiempo>        donde termina                         [fin]\n"
"                            acepta segundos (45), m:s (1:30) o h:m:s (1:02:03)\n"
"                            ejemplo: --start 0:45 --end 1:15\n"
"\n"
"Apariencia:\n"
"      --background-color <#RRGGBB>  fondo (default #0A0A0C)       [casi negro]\n"
"      --foreground <#RRGGBB>        color de las barras (default blanco)\n"
"      --bars <n>                    cantidad de barras             [auto]\n"
"      --bars-shade                  cada barra toma un gris segun su intensidad\n"
"      --wave <static|flow|pulse>    static = toda la pista; flow = una ventana\n"
"                                    que se desplaza; pulse = cada barra late\n"
"                                    con su banda de frecuencia         [static]\n"
"      --wave-span <seg>             segundos visibles en modo flow  [2.5]\n"
"\n"
"Formato de video:\n"
"      --preset <min|balanced>   min = 480x854@15, balanced = 720x1280@24\n"
"      --size <WxH>              resolucion, p.ej. 1080x1920\n"
"      --fps <n>                 cuadros por segundo\n"
"      --profile <baseline|main|high>\n"
"      --complexity <low|medium|high>\n"
"      --keyframe <seg>          intervalo entre keyframes         [5]\n"
"      --qp-min <n>              calidad maxima del video (1 = casi sin perdida,\n"
"                                51 = peor). Mas bajo = mejor y mas pesado   [10]\n"
"\n"
"Audio de salida:\n"
"      --audio-bitrate <kbps>    default segun preset (32 o 48)\n"
"      --audio-mode <he|lc>      HE-AAC o AAC-LC                 [he]\n"
"      --stereo                  conserva estereo (default: mono)\n"
"\n"
"Tamano:\n"
"      --video-bitrate <kbps>    bitrate objetivo de video\n"
"      --max-size <N[K|M]>       ajusta el video para entrar en el presupuesto\n"
"                                (el audio nunca se degrada para ahorrar)\n"
"  -h, --help                    esta ayuda\n"
"\n", argv0);
}

/* ------------------------------------------------------------------ */

static void apply_preset(tw_opts *o, const char *name)
{
    if (name && !strcmp(name, "min")) {
        o->width = 480;  o->height = 854;
        o->fps = 15;     o->video_kbps = 300;
        o->audio_kbps = 32;
    } else { /* balanced */
        o->width = 720;  o->height = 1280;
        o->fps = 24;     o->video_kbps = 700;
        o->audio_kbps = 48;
    }
    o->aac_he = 1;
    o->channels = 1;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_color(const char *s, tw_color *out)
{
    if (*s == '#') s++;
    size_t n = strlen(s);
    int r, g, b;

    if (n == 3) {
        r = hexval(s[0]); g = hexval(s[1]); b = hexval(s[2]);
        if (r < 0 || g < 0 || b < 0) return -1;
        r *= 17; g *= 17; b *= 17;
    } else if (n == 6) {
        r = hexval(s[0]) * 16 + hexval(s[1]);
        g = hexval(s[2]) * 16 + hexval(s[3]);
        b = hexval(s[4]) * 16 + hexval(s[5]);
        if (r < 0 || g < 0 || b < 0) return -1;
    } else {
        return -1;
    }

    out->r = (uint8_t)r; out->g = (uint8_t)g; out->b = (uint8_t)b; out->a = 255;
    return 0;
}

static long long parse_bytes(const char *s)
{
    char *end = NULL;
    double v = strtod(s, &end);
    if (end) {
        if (*end == 'k' || *end == 'K') v *= 1024.0;
        else if (*end == 'm' || *end == 'M') v *= 1024.0 * 1024.0;
        else if (*end == 'g' || *end == 'G') v *= 1024.0 * 1024.0 * 1024.0;
    }
    return (long long)v;
}

static int parse_size(const char *s, int *w, int *h)
{
    return sscanf(s, "%dx%d", w, h) == 2 ? 0 : -1;
}

/*
 * Acepta tiempos en segundos ("90"), en formato m:s ("1:30") o h:m:s
 * ("1:02:03"). Tambien admite fracciones ("1:30.25", ":45").
 * Devuelve 0 si pudo interpretar todo el texto, -1 si no.
 */
static int parse_time(const char *s, double *out)
{
    if (!s || !*s)
        return -1;

    double part[3] = { 0.0, 0.0, 0.0 };
    int n = 0;
    const char *p = s;

    for (;;) {
        char *end = NULL;

        if (n >= 3)
            return -1;

        if (*p != ':' && *p != '\0') {   /* campo vacio (":30") vale 0 */
            double v = strtod(p, &end);
            if (end == p || v < 0.0)
                return -1;
            part[n] = v;
            p = end;
        }
        n++;

        if (*p == '\0')
            break;
        if (*p != ':')
            return -1;
        p++;
        if (*p == '\0')      /* "1:" o "1:2:" estan incompletos */
            return -1;
    }

    /* de mas significativo a menos: los campos se leen como h:m:s o m:s */
    double total = 0.0;
    for (int i = 0; i < n; i++)
        total = total * 60.0 + part[i];

    *out = total;
    return 0;
}

/* Formatea segundos como m:ss (o h:mm:ss si pasa de una hora). */
static void fmt_time(double sec, char *buf, size_t n)
{
    if (sec < 0.0)
        sec = 0.0;

    int h = (int)(sec / 3600.0);
    sec -= (double)h * 3600.0;
    int m = (int)(sec / 60.0);
    double s = sec - (double)m * 60.0;

    char sbuf[8];
    if (s - (double)(int)s < 0.05)
        snprintf(sbuf, sizeof sbuf, "%02d", (int)s);
    else
        snprintf(sbuf, sizeof sbuf, "%04.1f", s);

    if (h > 0)
        snprintf(buf, n, "%d:%02d:%s", h, m, sbuf);
    else
        snprintf(buf, n, "%d:%s", m, sbuf);
}

static long long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return (long long)n;
}

/*
 * Tipografia por defecto: se usa cuando el usuario no pasa --font, para que
 * --artist y --title se dibujen igual. Devuelve la primera ruta existente.
 */
static const char *default_font(void)
{
    static const char *cand[] = {
        /* Linux */
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-B.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
        "/usr/local/share/fonts/DejaVuSans-Bold.ttf",
        /* macOS */
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        /* Windows */
        "C:/Windows/Fonts/arialbd.ttf",
        "C:/Windows/Fonts/arial.ttf",
        NULL
    };

    for (int i = 0; cand[i]; i++) {
        FILE *f = fopen(cand[i], "rb");
        if (f) {
            fclose(f);
            return cand[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* recorte del audio al segmento [start, end)                          */
/* ------------------------------------------------------------------ */

static int build_segment(const tw_audio *a, double start_s, double end_s,
                         int out_channels, float **out_f, int16_t **out_i16,
                         int64_t *out_frames)
{
    int sr = a->sample_rate;
    int64_t s0 = (int64_t)(start_s * sr);
    int64_t s1 = (end_s < 0.0) ? (int64_t)a->frames : (int64_t)(end_s * sr);

    if (s0 < 0) s0 = 0;
    if (s0 > (int64_t)a->frames) s0 = (int64_t)a->frames;
    if (s1 > (int64_t)a->frames) s1 = (int64_t)a->frames;
    if (s1 <= s0) {
        fprintf(stderr, "error: el segmento pedido esta vacio o fuera del audio\n");
        return -1;
    }

    int64_t frames = s1 - s0;
    float *fbuf = (float *)malloc((size_t)frames * out_channels * sizeof(float));
    int16_t *ibuf = (int16_t *)malloc((size_t)frames * out_channels * sizeof(int16_t));
    if (!fbuf || !ibuf) {
        free(fbuf); free(ibuf);
        return -1;
    }

    for (int64_t i = 0; i < frames; i++) {
        const float *src = a->samples + (s0 + i) * a->channels;
        for (int c = 0; c < out_channels; c++) {
            float v;
            if (out_channels == 1) {
                float acc = 0.0f;
                for (int k = 0; k < a->channels; k++) acc += src[k];
                v = acc / (float)a->channels;
            } else {
                v = src[c < a->channels ? c : a->channels - 1];
            }
            fbuf[i * out_channels + c] = v;

            float cl = v;
            if (cl > 1.0f) cl = 1.0f;
            if (cl < -1.0f) cl = -1.0f;
            ibuf[i * out_channels + c] = (int16_t)lrintf(cl * 32767.0f);
        }
    }

    *out_f = fbuf;
    *out_i16 = ibuf;
    *out_frames = frames;
    return 0;
}

/* ------------------------------------------------------------------ */
/* una pasada de codificacion completa                                 */
/* ------------------------------------------------------------------ */

static int vid_sink(void *ctx, const uint8_t *nal, int len, int key)
{
    (void)key;
    return tw_mux_write_video((tw_muxer *)ctx, nal, len);
}

static int aud_sink(void *ctx, const uint8_t *data, int len)
{
    return tw_mux_write_audio((tw_muxer *)ctx, data, len);
}

static int run_encode(const tw_opts *o, tw_scene *scene, const int16_t *pcm,
                      int64_t pcm_frames, int sample_rate, int total_frames,
                      int video_bitrate, const char *path)
{
    int rc = -1;
    tw_aacenc *aenc = NULL;
    tw_h264enc *venc = NULL;
    tw_muxer *mux = NULL;
    tw_canvas canvas;
    uint8_t *yuv = NULL;

    tw_aac_cfg ac;
    memset(&ac, 0, sizeof ac);
    ac.sample_rate = sample_rate;
    ac.channels = o->channels;
    ac.bitrate = o->audio_kbps * 1000;
    ac.he_aac = o->aac_he;
    if (tw_aac_open(&aenc, &ac) != 0) {
        fprintf(stderr, "error: no se pudo abrir el encoder AAC\n");
        goto done;
    }

    int dsi_len = 0;
    const uint8_t *dsi = tw_aac_dsi(aenc, &dsi_len);

    tw_mux_cfg mc;
    memset(&mc, 0, sizeof mc);
    mc.path = path;
    mc.width = o->width;
    mc.height = o->height;
    mc.fps = o->fps;
    mc.sample_rate = sample_rate;
    mc.channels = o->channels;
    mc.aac_dsi = dsi;
    mc.aac_dsi_len = dsi_len;
    mc.aac_frame_samples = tw_aac_frame_samples(aenc);
    if (tw_mux_open(&mux, &mc) != 0) {
        fprintf(stderr, "error: no se pudo abrir %s para escritura\n", path);
        goto done;
    }

    tw_h264_cfg vc;
    memset(&vc, 0, sizeof vc);
    vc.width = o->width;
    vc.height = o->height;
    vc.fps = o->fps;
    vc.bitrate = video_bitrate * 1000;   /* el CLI usa kbps */
    vc.complexity = o->complexity;
    vc.profile = o->profile;
    vc.keyframe_period_s = o->keyframe_s;
    vc.qp_min = o->qp_min;
    if (tw_h264_open(&venc, &vc) != 0) {
        fprintf(stderr, "error: no se pudo abrir el encoder de video (OpenH264)\n");
        goto done;
    }

    if (tw_canvas_init(&canvas, o->width, o->height) != 0) {
        fprintf(stderr, "error: sin memoria para el lienzo\n");
        goto done;
    }
    yuv = (uint8_t *)malloc(tw_i420_size(&canvas));
    if (!yuv) {
        fprintf(stderr, "error: sin memoria para el cuadro I420\n");
        goto done;
    }

    const double spf = (double)sample_rate / (double)o->fps;   /* samples por cuadro */
    int64_t fed = 0;

    for (int frame = 0; frame < total_frames; frame++) {
        tw_scene_render(scene, frame, total_frames, &canvas);
        tw_canvas_to_i420_into(&canvas, yuv);

        if (tw_h264_encode(venc, yuv, frame, vid_sink, mux) != 0) {
            fprintf(stderr, "\nerror: fallo la codificacion de video\n");
            goto done;
        }

        int64_t upto = (int64_t)((double)(frame + 1) * spf);
        if (upto > pcm_frames)
            upto = pcm_frames;
        if (upto > fed) {
            int n = (int)(upto - fed);
            if (tw_aac_encode(aenc, pcm + fed * o->channels, n * o->channels,
                              aud_sink, mux) != 0) {
                fprintf(stderr, "\nerror: fallo la codificacion de audio\n");
                goto done;
            }
            fed = upto;
        }

        if ((frame % 12) == 0)
            fprintf(stderr, "\r  codificando  %d%%   ", (frame + 1) * 100 / total_frames);
    }

    if (tw_aac_flush(aenc, aud_sink, mux) != 0) {
        fprintf(stderr, "\nerror: fallo al cerrar el audio\n");
        goto done;
    }

    rc = 0;

done:
    if (venc) tw_h264_close(venc);
    if (aenc) tw_aac_close(aenc);
    if (mux) tw_mux_close(mux);
    free(yuv);
    return rc;
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    tw_opts o;
    memset(&o, 0, sizeof o);
    o.output = "status.mp4";
    o.start_sec = 0.0;
    o.end_sec = -1.0;
    o.bars = 0;
    o.bars_shade = 0;
    o.wave_mode = 0;
    o.wave_span = 2.5f;
    o.credit = "Made with soundwave by cstffx";
    o.profile = 2;      /* high */
    o.complexity = 1;   /* media */
    o.keyframe_s = 5;
    o.qp_min = 10;
    o.max_bytes = 0;
    apply_preset(&o, "balanced");

    /* primera pasada: --preset, para que los flags explicitos lo sobreescriban */
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--preset")) {
            apply_preset(&o, argv[i + 1]);
            break;
        }
    }

    o.bg = tw_rgb(10, 10, 12);
    o.bg_lo = tw_rgb(22, 22, 28);
    o.fg = tw_rgb(255, 255, 255);

    int size_set = 0, fps_set = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;

        /* las unicas opciones que no llevan valor son --stereo, --bars-shade
         * y -h/--help */
        if (!v && strcmp(a, "--stereo") && strcmp(a, "--bars-shade") &&
            strcmp(a, "-h") && strcmp(a, "--help")) {
            fprintf(stderr, "error: '%s' necesita un valor\n", a);
            return 2;
        }

        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(a, "-i") || !strcmp(a, "--input")) {
            o.input = v; i++;
        } else if (!strcmp(a, "-o") || !strcmp(a, "--output")) {
            o.output = v; i++;
        } else if (!strcmp(a, "--avatar")) {
            o.avatar = v; i++;
        } else if (!strcmp(a, "--artist")) {
            o.artist = v; i++;
        } else if (!strcmp(a, "--title")) {
            o.title = v; i++;
        } else if (!strcmp(a, "--font")) {
            o.font = v; i++;
        } else if (!strcmp(a, "--credit")) {
            o.credit = v; i++;
        } else if (!strcmp(a, "--start")) {
            if (parse_time(v, &o.start_sec) != 0) {
                fprintf(stderr, "error: --start espera segundos (45) o m:s (1:30)\n");
                return 2;
            }
            i++;
        } else if (!strcmp(a, "--end")) {
            if (parse_time(v, &o.end_sec) != 0) {
                fprintf(stderr, "error: --end espera segundos (45) o m:s (1:30)\n");
                return 2;
            }
            i++;
        } else if (!strcmp(a, "--preset")) {
            i++;   /* ya aplicado */
        } else if (!strcmp(a, "--size")) {
            if (parse_size(v, &o.width, &o.height) != 0) {
                fprintf(stderr, "error: --size espera algo como 720x1280\n");
                return 2;
            }
            size_set = 1; i++;
        } else if (!strcmp(a, "--fps")) {
            o.fps = atoi(v); fps_set = 1; i++;
        } else if (!strcmp(a, "--background-color")) {
            if (parse_color(v, &o.bg) != 0) {
                fprintf(stderr, "error: color invalido '%s'\n", v);
                return 2;
            }
            i++;
        } else if (!strcmp(a, "--foreground")) {
            if (parse_color(v, &o.fg) != 0) {
                fprintf(stderr, "error: color invalido '%s'\n", v);
                return 2;
            }
            i++;
        } else if (!strcmp(a, "--bars")) {
            o.bars = atoi(v); i++;
        } else if (!strcmp(a, "--bars-shade")) {
            o.bars_shade = 1;
        } else if (!strcmp(a, "--wave")) {
            if (!strcmp(v, "flow"))
                o.wave_mode = 1;
            else if (!strcmp(v, "pulse"))
                o.wave_mode = 2;
            else if (!strcmp(v, "static"))
                o.wave_mode = 0;
            else {
                fprintf(stderr, "error: --wave espera 'static', 'flow' o 'pulse'\n");
                return 2;
            }
            i++;
        } else if (!strcmp(a, "--wave-span")) {
            o.wave_span = (float)atof(v);
            if (!(o.wave_span > 0.0f)) {
                fprintf(stderr, "error: --wave-span espera segundos (p.ej. 2.5)\n");
                return 2;
            }
            i++;
        } else if (!strcmp(a, "--video-bitrate")) {
            o.video_kbps = atoi(v); i++;
        } else if (!strcmp(a, "--audio-bitrate")) {
            o.audio_kbps = atoi(v); i++;
        } else if (!strcmp(a, "--audio-mode")) {
            o.aac_he = (v && !strcmp(v, "lc")) ? 0 : 1; i++;
        } else if (!strcmp(a, "--stereo")) {
            o.channels = 2;
        } else if (!strcmp(a, "--profile")) {
            o.profile = (!v || !strcmp(v, "baseline")) ? 0 : (!strcmp(v, "main") ? 1 : 2); i++;
        } else if (!strcmp(a, "--complexity")) {
            o.complexity = (!v || !strcmp(v, "low")) ? 0 : (!strcmp(v, "high") ? 2 : 1); i++;
        } else if (!strcmp(a, "--keyframe")) {
            o.keyframe_s = atoi(v); i++;
        } else if (!strcmp(a, "--qp-min")) {
            o.qp_min = atoi(v);
            if (o.qp_min < 1 || o.qp_min > 51) {
                fprintf(stderr, "error: --qp-min va de 1 a 51\n");
                return 2;
            }
            i++;
        } else if (!strcmp(a, "--max-size")) {
            o.max_bytes = parse_bytes(v); i++;
        } else {
            fprintf(stderr, "error: opcion desconocida '%s' (usa --help)\n", a);
            return 2;
        }
    }
    (void)size_set; (void)fps_set;

    /* sin --font buscamos una tipografia del sistema para los textos */
    if (!o.font)
        o.font = default_font();

    if (!o.input) {
        usage(argv[0]);
        return 2;
    }

    /* fondo: el extremo inferior del degradado es un pelo mas claro */
    o.bg_lo.r = (uint8_t)(o.bg.r + 12 > 255 ? 255 : o.bg.r + 12);
    o.bg_lo.g = (uint8_t)(o.bg.g + 12 > 255 ? 255 : o.bg.g + 12);
    o.bg_lo.b = (uint8_t)(o.bg.b + 16 > 255 ? 255 : o.bg.b + 16);

    /* --- audio --- */
    tw_audio audio;
    if (tw_audio_load(o.input, &audio) != 0) {
        fprintf(stderr, "error: no se pudo decodificar '%s' (wav/mp3/flac/ogg)\n", o.input);
        return 1;
    }

    double dur_total = tw_audio_duration(&audio);
    if (o.end_sec < 0.0 || o.end_sec > dur_total)
        o.end_sec = dur_total;
    if (o.start_sec >= o.end_sec) {
        char t0[32], t1[32];
        fmt_time(o.start_sec, t0, sizeof t0);
        fmt_time(o.end_sec, t1, sizeof t1);
        fprintf(stderr, "error: --start (%s) debe ser menor que --end (%s)\n", t0, t1);
        return 2;
    }

    float *seg_f = NULL;
    int16_t *seg_i = NULL;
    int64_t seg_frames = 0;
    if (build_segment(&audio, o.start_sec, o.end_sec, o.channels,
                      &seg_f, &seg_i, &seg_frames) != 0)
        return 1;

    double seg_sec = (double)seg_frames / (double)audio.sample_rate;

    char t0[32], t1[32];
    fmt_time(o.start_sec, t0, sizeof t0);
    fmt_time(o.end_sec, t1, sizeof t1);

    printf("soundwave\n");
    printf("  audio        %s  (%.2f s totales)\n", o.input, dur_total);
    printf("  segmento     %s -> %s   (%.2f s)\n", t0, t1, seg_sec);
    printf("  video        %dx%d @ %d fps, perfil %s, %s\n",
           o.width, o.height, o.fps,
           o.profile == 0 ? "baseline" : (o.profile == 1 ? "main" : "high"),
           o.channels == 1 ? "mono" : "estereo");
    printf("  audio out    %s %d kbps\n", o.aac_he ? "HE-AAC" : "AAC-LC", o.audio_kbps);
    if (o.artist || o.title || (o.credit && o.credit[0]))
        printf("  tipografia   %s\n",
               o.font ? o.font : "(no se encontro ninguna TTF: el texto se omite)");

    if (seg_sec > 60.0)
        printf("  aviso        el estado dura %.1f s; WhatsApp acepta hasta 30 s\n", seg_sec);

    /* --- envolvente --- */
    tw_audio seg_view;
    memset(&seg_view, 0, sizeof seg_view);
    seg_view.samples = seg_f;
    seg_view.channels = o.channels;
    seg_view.sample_rate = audio.sample_rate;
    seg_view.frames = (uint64_t)seg_frames;

    tw_env_cfg ec;
    tw_env_cfg_default(&ec);
    tw_envelope env;
    if (tw_envelope_build(&seg_view, o.fps, &ec, &env) != 0) {
        fprintf(stderr, "error: no se pudo calcular la envolvente\n");
        return 1;
    }

    /* --- envolvente densa para el modo fluyente --- */
    tw_envelope flow_env;
    memset(&flow_env, 0, sizeof flow_env);
    int flow_cps = 0;
    if (o.wave_mode == 1) {
        int nbars = tw_scene_nbars(o.width & ~1, o.bars);
        flow_cps = (int)lround((double)nbars / (double)o.wave_span);
        if (flow_cps < 4)
            flow_cps = 4;

        /* Seguidor mas rapido que en la vista estatica: aqui interesa el
         * detalle para que el desplazamiento se note. */
        tw_env_cfg fc = ec;
        fc.attack  = 0.80f;
        fc.release = 0.40f;
        if (tw_envelope_build(&seg_view, flow_cps, &fc, &flow_env) != 0) {
            fprintf(stderr, "error: no se pudo calcular la envolvente fluyente\n");
            return 1;
        }
    }

    /* --- espectro (solo para el modo pulse) --- */
    tw_spectrum spec;
    memset(&spec, 0, sizeof spec);
    if (o.wave_mode == 2) {
        int nbands = tw_scene_nbars(o.width & ~1, o.bars);
        tw_spectrum_cfg scfg;
        tw_spectrum_cfg_default(&scfg);
        if (tw_spectrum_build(&seg_view, nbands, o.fps, &scfg, &spec) != 0) {
            fprintf(stderr, "error: no se pudo calcular el espectro\n");
            return 1;
        }
    }

    /* --- avatar --- */
    tw_image av_sq;
    memset(&av_sq, 0, sizeof av_sq);
    if (o.avatar) {
        tw_image raw;
        if (tw_image_load(o.avatar, &raw) == 0) {
            if (tw_image_fit_square(&raw, 512, &av_sq) != 0) {
                fprintf(stderr, "aviso: no se pudo escalar el avatar\n");
                memset(&av_sq, 0, sizeof av_sq);
            }
            tw_image_free(&raw);
        } else {
            fprintf(stderr, "aviso: no se pudo leer el avatar '%s'\n", o.avatar);
        }
    }
    if (!av_sq.rgba)
        tw_image_solid(512, 512, 36, 36, 42, &av_sq);

    /* --- escena --- */
    tw_scene_cfg sc;
    tw_scene_cfg_default(&sc);
    sc.width = o.width;
    sc.height = o.height;
    sc.background = o.bg;
    sc.background_lo = o.bg_lo;
    sc.foreground = o.fg;
    sc.foreground_dim = tw_rgba(o.fg.r, o.fg.g, o.fg.b, 70);
    sc.ring = tw_rgba(o.fg.r, o.fg.g, o.fg.b, 60);
    sc.artist = o.artist;
    sc.title = o.title;
    sc.font_path = o.font;
    sc.credit = o.credit;
    sc.nbars = o.bars;
    sc.bars_shade = o.bars_shade;
    sc.wave_mode = o.wave_mode;
    sc.wave_span = o.wave_span;
    sc.flow_env = (o.wave_mode == 1) ? &flow_env : NULL;
    sc.pulse = (o.wave_mode == 2) ? spec.v : NULL;
    sc.pulse_frames = (o.wave_mode == 2) ? spec.nframes : 0;
    sc.fps = o.fps;
    sc.avatar = &av_sq;

    tw_scene scene;
    if (tw_scene_init(&scene, &sc, &env) != 0) {
        fprintf(stderr, "error: no se pudo preparar la escena\n");
        return 1;
    }

    int total_frames = (int)llround(seg_sec * o.fps);
    if (total_frames < 1)
        total_frames = 1;

    printf("  onda         ");
    if (scene.mode == 1)
        printf("%d barras fluyentes (%.2f s visibles, %.1f col/s)\n",
               scene.nbars, (float)scene.nbars / scene.cols_per_sec,
               scene.cols_per_sec);
    else if (scene.mode == 2)
        printf("%d bandas (pulse)\n", scene.nbars);
    else
        printf("%d barras\n", scene.nbars);
    printf("  cuadros      %d\n", total_frames);

    /* --- codificacion (con ajuste por --max-size) --- */
    int vbr = o.video_kbps;
    if (o.max_bytes > 0) {
        double budget_bps = (double)o.max_bytes * 8.0 / seg_sec;
        double guess = budget_bps - (double)o.audio_kbps * 1000.0;
        if (guess < 10000.0) {
            fprintf(stderr,
                    "aviso: el audio (%.0f kbps) ya consume casi todo el presupuesto de %lld bytes\n",
                    o.audio_kbps * 1000.0 * seg_sec / 8.0 / o.max_bytes * 100.0,
                    o.max_bytes);
            guess = 10000.0;
        }
        vbr = (int)(guess / 1000.0);
        printf("  objetivo     <= %lld bytes  ->  ~%d kbps de video\n", o.max_bytes, vbr);
    }
    printf("  video bitrate %d kbps\n", vbr);

    int ok = 0;
    for (int attempt = 0; attempt < 4; attempt++) {
        if (run_encode(&o, &scene, seg_i, seg_frames, audio.sample_rate,
                       total_frames, vbr, o.output) != 0) {
            fprintf(stderr, "\n");
            break;
        }

        long long size = file_size(o.output);
        fprintf(stderr, "\r  codificando  100%%   \n");
        printf("  salida       %s  (%.2f MB, %.0f kbps de video)\n",
               o.output, (double)size / 1048576.0, (double)vbr);

        if (o.max_bytes <= 0 || size <= o.max_bytes) {
            ok = 1;
            break;
        }

        double scale = (double)o.max_bytes / (double)size * 0.92;
        int nv = (int)((double)vbr * scale);
        if (nv < 40) nv = 40;
        if (nv >= vbr) nv = vbr - 1;
        if (nv < 40) {
            fprintf(stderr, "error: no se puede entrar en %lld bytes sin degradar el audio\n",
                    o.max_bytes);
            break;
        }
        printf("  ajuste       %.2f MB > %.2f MB, reintento a %d kbps\n",
               (double)size / 1048576.0, (double)o.max_bytes / 1048576.0, nv);
        vbr = nv;
    }

    tw_scene_free(&scene);
    tw_image_free(&av_sq);
    tw_envelope_free(&env);
    tw_envelope_free(&flow_env);
    tw_spectrum_free(&spec);
    free(seg_f);
    free(seg_i);
    tw_audio_free(&audio);

    if (!ok && o.max_bytes > 0) {
        fprintf(stderr, "error: no se logro cumplir el presupuesto de tamano\n");
        return 1;
    }
    return 0;
}
