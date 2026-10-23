/*
 * scene.h - composicion de los cuadros: fondo + avatar + onda.
 *
 * El fondo, el avatar y los textos son estaticos, asi que se pre-renderizan
 * una vez. Por cuadro solo se copia ese fondo y se dibujan las barras de la
 * onda (mas el avance de reproduccion).
 */
#ifndef TW_SCENE_H
#define TW_SCENE_H

#include "draw.h"
#include "envelope.h"
#include "image.h"

typedef struct {
    int        width, height;
    tw_color   background;      /* por defecto casi negro */
    tw_color   background_lo;   /* extremo inferior del degradado */
    tw_color   foreground;      /* color de las barras: blanco por defecto */
    tw_color   foreground_dim;  /* barras aun no reproducidas */
    tw_color   ring;            /* anillo del avatar */
    int        nbars;           /* 0 = automatico segun el ancho */
    int        bars_shade;      /* 1 = color de cada barra segun su intensidad */
    int        wave_mode;       /* 0 = estatica, 1 = fluyente, 2 = pulse */
    float      wave_span;       /* segundos visibles en modo fluyente */
    const tw_envelope *flow_env;/* envolvente densa para el modo fluyente */
    const float *pulse;         /* espectro: [cuadro * nbars + banda] */
    int        pulse_frames;
    int        fps;             /* cuadros por segundo del video */

    const char *artist;
    const char *title;
    const char *font_path;      /* ttf/otf opcional */
    float       font_artist_px;
    float       font_title_px;
    const char *credit;         /* texto pequeño al pie; NULL = sin credito */
    float       credit_px;      /* 0 = automatico */

    const tw_image *avatar;     /* puede ser NULL */
} tw_scene_cfg;

typedef struct {
    tw_canvas      bg;
    int            width, height;
    int            nbars;
    float         *bars;        /* nbars valores en [0,1] */
    float          max_half;    /* semialtura maxima de una barra, en px */
    float          center_y;
    float          wave_x0, pitch, bar_w;
    tw_color       fg, fg_dim;
    int            shade;       /* 1 = interpolar fg segun la intensidad */
    tw_color       shade_lo;    /* extremo de baja intensidad */
    float          shade_v0, shade_v1;   /* rango de intensidad estirado */
    int            has_playhead_line;

    /* modo fluyente: ventana de columnas que se desplaza */
    int            mode;        /* 0 estatica, 1 fluyente, 2 pulse */
    const float   *cols;        /* un valor por columna */
    int            ncols;
    float          cols_per_sec;
    const float   *pulse;       /* espectro: [cuadro * nbars + banda] */
    int            pulse_frames;
    int            fps;
} tw_scene;

/* Cantidad de barras para un ancho dado (0 = automatico). */
int tw_scene_nbars(int width, int nbars_cfg);

void tw_scene_cfg_default(tw_scene_cfg *cfg);

int  tw_scene_init(tw_scene *s, const tw_scene_cfg *cfg, const tw_envelope *env);
void tw_scene_free(tw_scene *s);

/* Dibuja el cuadro `frame` (0..total-1) sobre `out` (debe medir w x h). */
void tw_scene_render(tw_scene *s, int frame, int total, tw_canvas *out);

#endif /* TW_SCENE_H */
