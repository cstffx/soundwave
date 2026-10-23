#include "scene.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void tw_scene_cfg_default(tw_scene_cfg *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->width  = 720;
    cfg->height = 1280;

    cfg->background    = tw_rgb(10, 10, 12);    /* casi negro */
    cfg->background_lo = tw_rgb(22, 22, 28);
    cfg->foreground    = tw_rgb(255, 255, 255); /* barras blancas */
    cfg->foreground_dim = tw_rgba(255, 255, 255, 70);
    cfg->ring          = tw_rgba(255, 255, 255, 60);

    cfg->nbars            = 0;
    cfg->bars_shade       = 0;
    cfg->wave_mode        = 0;
    cfg->wave_span        = 2.5f;
    cfg->flow_env         = NULL;
    cfg->pulse            = NULL;
    cfg->pulse_frames     = 0;
    cfg->fps              = 24;
    cfg->font_artist_px   = 0;   /* 0 = automatico */
    cfg->font_title_px    = 0;
    cfg->artist           = NULL;
    cfg->title            = NULL;
    cfg->font_path        = NULL;
    cfg->credit           = NULL;
    cfg->credit_px        = 0;   /* 0 = automatico */
    cfg->avatar           = NULL;
}

/* Comparador para qsort. */
static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/*
 * Rango util de la envolvente para el sombreado: los percentiles 10 y 98.
 * La envolvente se concentra en una franja estrecha, asi que sin este
 * estirado de contraste las barras quedarian casi del mismo gris.
 */
static void shade_range(const float *v, int n, float *lo, float *hi)
{
    *lo = 0.0f;
    *hi = 1.0f;
    if (!v || n < 8)
        return;

    float *tmp = (float *)malloc((size_t)n * sizeof(float));
    if (!tmp)
        return;
    memcpy(tmp, v, (size_t)n * sizeof(float));
    qsort(tmp, (size_t)n, sizeof(float), cmp_float);

    float a = tmp[(int)(0.10f * (float)(n - 1))];
    float b = tmp[(int)(0.98f * (float)(n - 1))];
    free(tmp);

    /* Si la senal es muy plana no compensa estirar (amplificaria el ruido). */
    if (b - a < 0.05f) {
        float mid = 0.5f * (a + b);
        a = mid - 0.05f;
        b = mid + 0.05f;
    }
    if (a < 0.0f) a = 0.0f;
    if (b > 1.0f) b = 1.0f;
    if (b - a < 1e-4f)
        b = a + 1e-4f;

    *lo = a;
    *hi = b;
}

/* Remuestrea la envolvente a nbars barras (mezcla de maximo y media para que
 * los transitorios se noten sin perder la forma general). */
static void resample_bars(const tw_envelope *env, int nbars, float *out)
{
    if (env->n <= 0) {
        memset(out, 0, (size_t)nbars * sizeof(float));
        return;
    }

    for (int b = 0; b < nbars; b++) {
        int i0 = (int)((int64_t)b * env->n / nbars);
        int i1 = (int)((int64_t)(b + 1) * env->n / nbars);
        if (i1 <= i0)
            i1 = i0 + 1;
        if (i1 > env->n)
            i1 = env->n;

        float mx = 0.0f;
        double acc = 0.0;
        for (int i = i0; i < i1; i++) {
            if (env->v[i] > mx)
                mx = env->v[i];
            acc += env->v[i];
        }
        float mean = (float)(acc / (i1 - i0));
        out[b] = mx * 0.65f + mean * 0.35f;
    }
}

int tw_scene_nbars(int width, int nbars_cfg)
{
    int n = nbars_cfg > 0 ? nbars_cfg : width / 14;
    if (n < 8)   n = 8;
    if (n > 128) n = 128;
    return n;
}

int tw_scene_init(tw_scene *s, const tw_scene_cfg *cfg, const tw_envelope *env)
{
    memset(s, 0, sizeof *s);
    s->width = cfg->width & ~1;
    s->height = cfg->height & ~1;

    s->nbars = tw_scene_nbars(s->width, cfg->nbars);

    s->bars = (float *)malloc((size_t)s->nbars * sizeof(float));
    if (!s->bars)
        return -1;
    resample_bars(env, s->nbars, s->bars);

    s->fg = cfg->foreground;
    s->fg_dim = cfg->foreground_dim;

    /* --- modo de onda --- */
    s->fps = cfg->fps > 0 ? cfg->fps : 24;

    if (cfg->wave_mode == 1 && cfg->flow_env && cfg->flow_env->n > 0) {
        /* fluyente: ventana de columnas que se desplaza */
        s->mode = 1;
        s->cols = cfg->flow_env->v;
        s->ncols = cfg->flow_env->n;
        s->cols_per_sec = (float)cfg->flow_env->fps;
    } else if (cfg->wave_mode == 2 && cfg->pulse && cfg->pulse_frames > 0) {
        /* pulse: cada barra sigue su banda de frecuencia */
        s->mode = 2;
        s->pulse = cfg->pulse;
        s->pulse_frames = cfg->pulse_frames;
    }

    s->shade = cfg->bars_shade;
    /* Extremo de baja intensidad: una mezcla del fondo con el color de las
     * barras, para que la barra "nazca" del fondo en vez de aparecer de golpe.
     * Con el fondo casi negro y las barras blancas esto es una escala de grises. */
    s->shade_lo = tw_lerp_color(cfg->background, cfg->foreground, 0.30f);
    s->shade_v0 = 0.0f;
    s->shade_v1 = 1.0f;
    if (s->shade) {
        /* La intensidad sale de la fuente que se esta dibujando. */
        if (s->mode == 1 && cfg->flow_env)
            shade_range(cfg->flow_env->v, cfg->flow_env->n,
                        &s->shade_v0, &s->shade_v1);
        else if (s->mode == 2)
            shade_range(s->pulse, s->pulse_frames * s->nbars,
                        &s->shade_v0, &s->shade_v1);
        else
            shade_range(env->v, env->n, &s->shade_v0, &s->shade_v1);
    }
    /* En los modos que muestran "ahora" el cursor no aporta nada. */
    s->has_playhead_line = (s->mode == 0);

    if (tw_canvas_init(&s->bg, s->width, s->height) != 0) {
        free(s->bars);
        memset(s, 0, sizeof *s);
        return -1;
    }

    const float W = (float)s->width, H = (float)s->height;

    /* --- fondo --- */
    tw_canvas_vgradient(&s->bg, cfg->background, cfg->background_lo);

    /* --- avatar --- */
    float avatar_r = 0.20f * W;
    float avatar_cx = 0.5f * W;
    float avatar_cy = 0.24f * H;
    float ring_w = 0.010f * W;

    tw_radial_glow(&s->bg, avatar_cx, avatar_cy, avatar_r * 2.6f, s->fg, 0.10f);
    tw_draw_avatar(&s->bg, cfg->avatar, avatar_cx, avatar_cy, avatar_r, ring_w, cfg->ring);

    /* --- textos --- */
    float font_artist_px = cfg->font_artist_px > 0 ? cfg->font_artist_px : 0.052f * W;
    float font_title_px  = cfg->font_title_px  > 0 ? cfg->font_title_px  : 0.032f * W;

    if (cfg->font_path && cfg->artist) {
        tw_font font;
        if (tw_font_load(&font, cfg->font_path, font_artist_px) == 0) {
            float w = tw_text_width(&font, cfg->artist);
            float baseline = avatar_cy + avatar_r + 0.075f * H;
            tw_draw_text(&s->bg, &font, cfg->artist, (W - w) * 0.5f, baseline, s->fg);
            tw_font_free(&font);
        }
    }

    if (cfg->font_path && cfg->title) {
        tw_font font;
        if (tw_font_load(&font, cfg->font_path, font_title_px) == 0) {
            tw_color dim = tw_rgba(s->fg.r, s->fg.g, s->fg.b, 140);
            float w = tw_text_width(&font, cfg->title);
            float baseline = avatar_cy + avatar_r + 0.075f * H + font_artist_px * 1.5f;
            tw_draw_text(&s->bg, &font, cfg->title, (W - w) * 0.5f, baseline, dim);
            tw_font_free(&font);
        }
    }

    /* --- credito al pie (estatico) --- */
    if (cfg->font_path && cfg->credit && cfg->credit[0]) {
        float credit_px = cfg->credit_px > 0.0f ? cfg->credit_px : 0.022f * W;
        tw_font font;
        if (tw_font_load(&font, cfg->font_path, credit_px) == 0) {
            tw_color dim = tw_rgba(s->fg.r, s->fg.g, s->fg.b, 110);
            float w = tw_text_width(&font, cfg->credit);
            tw_draw_text(&s->bg, &font, cfg->credit, (W - w) * 0.5f,
                         0.955f * H, dim);
            tw_font_free(&font);
        }
    }

    /* --- geometria de la onda --- */
    s->center_y = 0.70f * H;
    s->max_half = 0.145f * H;

    float margin = 0.08f * W;
    float usable = W - 2.0f * margin;
    s->pitch  = usable / (float)s->nbars;
    s->bar_w  = s->pitch * 0.62f;
    s->wave_x0 = margin;

    return 0;
}

void tw_scene_free(tw_scene *s)
{
    tw_canvas_free(&s->bg);
    free(s->bars);
    memset(s, 0, sizeof *s);
}

/* Color de una barra segun su intensidad (0..1). Sin --bars-shade todas las
 * barras usan el color de frente; con el flag se interpola desde un gris bajo
 * hasta ese color. */
static tw_color bar_color(const tw_scene *s, float v)
{
    if (!s->shade)
        return s->fg;

    float t = (v - s->shade_v0) / (s->shade_v1 - s->shade_v0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    return tw_lerp_color(s->shade_lo, s->fg, t);
}

/* Barra centrada en bx con intensidad v (0..1). Una barra sin contenido
 * (v = 0, p.ej. una banda por encima del corte del MP3) no se dibuja: en vez
 * de quedar como un punto minimo, desaparece. */
static void put_bar(const tw_scene *s, tw_canvas *out, float bx, float v,
                    float min_half, tw_color col)
{
    if (v <= 0.0f)
        return;

    float half = v * s->max_half;
    if (half < min_half)
        half = min_half;

    tw_draw_bar(out, bx, s->center_y - half, s->center_y + half,
                s->bar_w * 0.5f, s->bar_w * 0.5f, col);
}

/* Lee la envolvente densa en una posicion fraccionaria de columna.
 * Devuelve < 0 cuando esa columna todavia no tiene audio. */
static float col_value(const tw_scene *s, float pos)
{
    if (pos < 0.0f)
        return -1.0f;
    if (pos > (float)(s->ncols - 1))
        pos = (float)(s->ncols - 1);

    int i = (int)pos;
    if (i >= s->ncols - 1)
        return s->cols[s->ncols - 1];

    float f = pos - (float)i;
    return s->cols[i] + (s->cols[i + 1] - s->cols[i]) * f;
}

/* Modo estatico: la pista completa repartida en nbars, con cursor. */
static void render_static(tw_scene *s, int frame, int total, tw_canvas *out,
                          float min_half)
{
    float playhead = (total > 1) ? (float)frame / (float)(total - 1) : 1.0f;

    for (int b = 0; b < s->nbars; b++) {
        float bx = s->wave_x0 + ((float)b + 0.5f) * s->pitch;
        float v = s->bars[b];

        int played = ((float)(b + 1) / (float)s->nbars) <= playhead;

        /* La barra bajo el cursor late levemente con la energia actual. */
        if (b == (int)(playhead * (float)s->nbars))
            v *= 1.04f;

        tw_color col = bar_color(s, s->bars[b]);
        if (!played)
            col.a = s->fg_dim.a;

        put_bar(s, out, bx, v, min_half, col);
    }

    if (s->has_playhead_line) {
        float px = s->wave_x0 + playhead * s->pitch * (float)s->nbars;
        tw_color line = tw_rgba(s->fg.r, s->fg.g, s->fg.b, 110);
        tw_fill_rect(out, (int)px, (int)(s->center_y - s->max_half * 1.12f), 3,
                     (int)(s->max_half * 2.24f), line);
    }
}

/*
 * Modo fluyente: se muestra una ventana de nbars columnas que se desplaza.
 * Las barras ocupan ranuras fijas y cada una lee la columna que le toca; al
 * avanzar el tiempo todas leen una columna mas nueva, de modo que la onda se
 * corre hacia la izquierda y las muestras nuevas entran por la derecha.
 */
static void render_flow(tw_scene *s, int frame, tw_canvas *out, float min_half)
{
    float p = (float)frame / (float)s->fps * s->cols_per_sec;

    for (int b = 0; b < s->nbars; b++) {
        float pos = p - (float)(s->nbars - 1 - b);
        float v = col_value(s, pos);
        if (v < 0.0f)
            continue;   /* todavia no empieza el audio: la onda crece desde la derecha */

        float bx = s->wave_x0 + ((float)b + 0.5f) * s->pitch;
        put_bar(s, out, bx, v, min_half, bar_color(s, v));
    }
}

/*
 * Modo pulse: cada barra es una banda de frecuencia y su altura sigue la
 * energia de esa banda en el cuadro actual. Las barras no se mueven: laten.
 */
static void render_pulse(tw_scene *s, int frame, tw_canvas *out, float min_half)
{
    int f = frame;
    if (f < 0)
        f = 0;
    if (f >= s->pulse_frames)
        f = s->pulse_frames - 1;

    const float *row = s->pulse + (size_t)f * (size_t)s->nbars;

    for (int b = 0; b < s->nbars; b++) {
        float bx = s->wave_x0 + ((float)b + 0.5f) * s->pitch;
        put_bar(s, out, bx, row[b], min_half, bar_color(s, row[b]));
    }
}

void tw_scene_render(tw_scene *s, int frame, int total, tw_canvas *out)
{
    /* fondo pre-renderizado (memcpy del frame completo) */
    memcpy(out->rgba, s->bg.rgba, (size_t)s->width * s->height * 4);

    float min_half = 0.0035f * (float)s->height;
    if (min_half < 1.5f)
        min_half = 1.5f;

    if (s->mode == 1)
        render_flow(s, frame, out, min_half);
    else if (s->mode == 2)
        render_pulse(s, frame, out, min_half);
    else
        render_static(s, frame, total, out, min_half);
}
