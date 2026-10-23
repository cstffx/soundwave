/*
 * draw.h - framebuffer RGBA con primitivas de dibujo y conversion a I420.
 */
#ifndef TW_DRAW_H
#define TW_DRAW_H

#include <stddef.h>
#include <stdint.h>
#include "image.h"
#include "stb_truetype.h"

typedef struct {
    uint8_t *rgba;   /* w*h*4 */
    int      w, h;
} tw_canvas;

typedef struct { uint8_t r, g, b, a; } tw_color;

static inline tw_color tw_rgb(int r, int g, int b)
{
    tw_color c;
    c.r = (uint8_t)r; c.g = (uint8_t)g; c.b = (uint8_t)b; c.a = 255;
    return c;
}
static inline tw_color tw_rgba(int r, int g, int b, int a)
{
    tw_color c;
    c.r = (uint8_t)r; c.g = (uint8_t)g; c.b = (uint8_t)b; c.a = (uint8_t)a;
    return c;
}
static inline tw_color tw_lerp_color(tw_color a, tw_color b, float t)
{
    tw_color c;
    c.r = (uint8_t)(a.r + (b.r - a.r) * t + 0.5f);
    c.g = (uint8_t)(a.g + (b.g - a.g) * t + 0.5f);
    c.b = (uint8_t)(a.b + (b.b - a.b) * t + 0.5f);
    c.a = (uint8_t)(a.a + (b.a - a.a) * t + 0.5f);
    return c;
}

int  tw_canvas_init(tw_canvas *c, int w, int h);
void tw_canvas_free(tw_canvas *c);

void tw_px_blend(tw_canvas *c, int x, int y, tw_color col, float cov);
void tw_canvas_clear(tw_canvas *c, tw_color col);
void tw_fill_rect(tw_canvas *c, int x, int y, int w, int h, tw_color col);

/* Degradado vertical + resplandor radial suave detras del avatar. */
void tw_canvas_vgradient(tw_canvas *c, tw_color top, tw_color bottom);
void tw_radial_glow(tw_canvas *c, float cx, float cy, float radius, tw_color col, float peak_alpha);

/* Cuadrados/rectangulos con esquinas redondeadas y antialiasing. */
void tw_fill_round_rect(tw_canvas *c, float x, float y, float w, float h, float r, tw_color col);

/* Avatar circular (recorte AA) con anillo opcional. */
void tw_draw_avatar(tw_canvas *c, const tw_image *img, float cx, float cy, float radius,
                    float ring_w, tw_color ring);

/* Barra vertical con extremos redondeados (para la onda). */
void tw_draw_bar(tw_canvas *c, float cx, float y0, float y1, float half_w, float radius, tw_color col);

/* Conversion a I420 planar (Y, luego U, luego V).
 * _into escribe en un buffer del llamador de w*h*3/2 bytes. */
size_t tw_i420_size(const tw_canvas *c);
void   tw_canvas_to_i420_into(const tw_canvas *c, uint8_t *buf);
int    tw_canvas_to_i420(const tw_canvas *c, uint8_t **out, size_t *out_size);

/* --- texto --- */
typedef struct {
    stbtt_fontinfo info;
    unsigned char *data;
    float          scale;
    float          ascent, descent;
} tw_font;

int   tw_font_load(tw_font *f, const char *path, float pixel_height);
void  tw_font_free(tw_font *f);
float tw_text_width(const tw_font *f, const char *utf8);
void  tw_draw_text(tw_canvas *c, const tw_font *f, const char *utf8, float x, float baseline, tw_color col);

#endif /* TW_DRAW_H */
