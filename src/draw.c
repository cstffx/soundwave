#include "draw.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* framebuffer                                                         */
/* ------------------------------------------------------------------ */

int tw_canvas_init(tw_canvas *c, int w, int h)
{
    memset(c, 0, sizeof *c);
    if (w <= 0 || h <= 0)
        return -1;
    c->rgba = (uint8_t *)malloc((size_t)w * h * 4);
    if (!c->rgba)
        return -1;
    c->w = w;
    c->h = h;
    return 0;
}

void tw_canvas_free(tw_canvas *c)
{
    free(c->rgba);
    memset(c, 0, sizeof *c);
}

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void tw_px_blend(tw_canvas *c, int x, int y, tw_color col, float cov)
{
    if (x < 0 || y < 0 || x >= c->w || y >= c->h)
        return;

    float a = (col.a / 255.0f) * cov;
    if (a <= 0.0009f)
        return;
    if (a > 1.0f)
        a = 1.0f;

    uint8_t *p = c->rgba + ((size_t)y * c->w + x) * 4;
    float ia = 1.0f - a;
    p[0] = (uint8_t)(col.r * a + p[0] * ia + 0.5f);
    p[1] = (uint8_t)(col.g * a + p[1] * ia + 0.5f);
    p[2] = (uint8_t)(col.b * a + p[2] * ia + 0.5f);
    p[3] = (uint8_t)(255 * a + p[3] * ia + 0.5f);
}

void tw_canvas_clear(tw_canvas *c, tw_color col)
{
    for (size_t i = 0, n = (size_t)c->w * c->h; i < n; i++) {
        c->rgba[i * 4 + 0] = col.r;
        c->rgba[i * 4 + 1] = col.g;
        c->rgba[i * 4 + 2] = col.b;
        c->rgba[i * 4 + 3] = col.a;
    }
}

void tw_fill_rect(tw_canvas *c, int x0, int y0, int w, int h, tw_color col)
{
    int x1 = x0 + w, y1 = y0 + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;

    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            tw_px_blend(c, x, y, col, 1.0f);
}

void tw_canvas_vgradient(tw_canvas *c, tw_color top, tw_color bottom)
{
    for (int y = 0; y < c->h; y++) {
        float t = (c->h > 1) ? (float)y / (float)(c->h - 1) : 0.0f;
        tw_color col = tw_lerp_color(top, bottom, t);
        uint8_t *row = c->rgba + (size_t)y * c->w * 4;
        for (int x = 0; x < c->w; x++) {
            row[x * 4 + 0] = col.r;
            row[x * 4 + 1] = col.g;
            row[x * 4 + 2] = col.b;
            row[x * 4 + 3] = col.a;
        }
    }
}

void tw_radial_glow(tw_canvas *c, float cx, float cy, float radius,
                    tw_color col, float peak_alpha)
{
    if (radius <= 0.0f)
        return;

    int x0 = (int)floorf(cx - radius), x1 = (int)ceilf(cx + radius);
    int y0 = (int)floorf(cy - radius), y1 = (int)ceilf(cy + radius);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float dx = (x + 0.5f) - cx;
            float dy = (y + 0.5f) - cy;
            float d = sqrtf(dx * dx + dy * dy) / radius;
            if (d >= 1.0f)
                continue;
            float f = 1.0f - d;
            /* tw_px_blend ya multiplica por col.a; aca solo va la forma */
            tw_px_blend(c, x, y, col, peak_alpha * f * f);
        }
    }
}

/* distancia firmada a un rectangulo redondeado centrado en (cx,cy) */
static inline float round_rect_sdf(float px, float py, float cx, float cy,
                                   float hw, float hh, float r)
{
    float dx = fabsf(px - cx) - (hw - r);
    float dy = fabsf(py - cy) - (hh - r);
    float outside = sqrtf(fmaxf(dx, 0.0f) * fmaxf(dx, 0.0f) +
                          fmaxf(dy, 0.0f) * fmaxf(dy, 0.0f));
    return fminf(fmaxf(dx, dy), 0.0f) + outside - r;
}

void tw_fill_round_rect(tw_canvas *c, float x, float y, float w, float h,
                        float r, tw_color col)
{
    if (w <= 0.0f || h <= 0.0f)
        return;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;

    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float hw = w * 0.5f, hh = h * 0.5f;

    int x0 = (int)floorf(x) - 1, x1 = (int)ceilf(x + w) + 1;
    int y0 = (int)floorf(y) - 1, y1 = (int)ceilf(y + h) + 1;

    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float d = round_rect_sdf(px + 0.5f, py + 0.5f, cx, cy, hw, hh, r);
            float cov = clampf(0.5f - d, 0.0f, 1.0f);
            if (cov > 0.0f)
                tw_px_blend(c, px, py, col, cov);
        }
    }
}

/* muestreo bilineal de una imagen RGBA en coordenadas normalizadas [0,1] */
static void sample_bilinear(const tw_image *img, float u, float v, uint8_t out[4])
{
    float fx = u * (float)(img->w - 1);
    float fy = v * (float)(img->h - 1);
    fx = clampf(fx, 0.0f, (float)(img->w - 1));
    fy = clampf(fy, 0.0f, (float)(img->h - 1));

    int x0 = (int)fx, y0 = (int)fy;
    int x1 = (x0 + 1 < img->w) ? x0 + 1 : x0;
    int y1 = (y0 + 1 < img->h) ? y0 + 1 : y0;
    float tx = fx - x0, ty = fy - y0;

    const uint8_t *p00 = img->rgba + ((size_t)y0 * img->w + x0) * 4;
    const uint8_t *p10 = img->rgba + ((size_t)y0 * img->w + x1) * 4;
    const uint8_t *p01 = img->rgba + ((size_t)y1 * img->w + x0) * 4;
    const uint8_t *p11 = img->rgba + ((size_t)y1 * img->w + x1) * 4;

    for (int k = 0; k < 4; k++) {
        float a = p00[k] + (p10[k] - p00[k]) * tx;
        float b = p01[k] + (p11[k] - p01[k]) * tx;
        out[k] = (uint8_t)(a + (b - a) * ty + 0.5f);
    }
}

void tw_draw_avatar(tw_canvas *c, const tw_image *img, float cx, float cy,
                    float radius, float ring_w, tw_color ring)
{
    if (radius <= 0.0f)
        return;

    int x0 = (int)floorf(cx - radius) - 1, x1 = (int)ceilf(cx + radius) + 1;
    int y0 = (int)floorf(cy - radius) - 1, y1 = (int)ceilf(cy + radius) + 1;

    if (img && img->rgba) {
        for (int py = y0; py < y1; py++) {
            for (int px = x0; px < x1; px++) {
                float dx = (px + 0.5f) - cx;
                float dy = (py + 0.5f) - cy;
                float d = sqrtf(dx * dx + dy * dy);
                float cov = clampf(radius + 0.5f - d, 0.0f, 1.0f);
                if (cov <= 0.0f)
                    continue;

                float u = (dx / radius + 1.0f) * 0.5f;
                float v = (dy / radius + 1.0f) * 0.5f;
                uint8_t s[4];
                sample_bilinear(img, u, v, s);

                tw_color col = tw_rgba(s[0], s[1], s[2], 255);
                tw_px_blend(c, px, py, col, cov * (s[3] / 255.0f));
            }
        }
    }

    if (ring_w > 0.0f) {
        float inner = radius - ring_w;
        for (int py = y0; py < y1; py++) {
            for (int px = x0; px < x1; px++) {
                float dx = (px + 0.5f) - cx;
                float dy = (py + 0.5f) - cy;
                float d = sqrtf(dx * dx + dy * dy);
                float outer_cov = clampf(radius + 0.5f - d, 0.0f, 1.0f);
                float inner_cov = clampf(inner + 0.5f - d, 0.0f, 1.0f);
                float cov = outer_cov - inner_cov;
                if (cov > 0.0f)
                    tw_px_blend(c, px, py, ring, cov);
            }
        }
    }
}

void tw_draw_bar(tw_canvas *c, float cx, float y0, float y1, float half_w,
                 float radius, tw_color col)
{
    float top = fminf(y0, y1);
    float bot = fmaxf(y0, y1);
    float h = bot - top;
    if (h < 1.0f)
        h = 1.0f;

    /* capsula: rectangulo redondeado con radio = min(half_w, h/2) */
    float r = half_w < h * 0.5f ? half_w : h * 0.5f;
    tw_fill_round_rect(c, cx - half_w, top, half_w * 2.0f, h, r, col);

    if (radius > 0.0f) { /* reservado */ }
}

/* ------------------------------------------------------------------ */
/* conversion a YUV420 planar (I420)                                   */
/* ------------------------------------------------------------------ */

size_t tw_i420_size(const tw_canvas *c)
{
    int w = c->w & ~1, h = c->h & ~1;
    size_t ys = (size_t)w * h;
    size_t cs = (size_t)(w / 2) * (h / 2);
    return ys + cs * 2;
}

void tw_canvas_to_i420_into(const tw_canvas *c, uint8_t *buf)
{
    int w = c->w & ~1, h = c->h & ~1;
    size_t ys = (size_t)w * h;
    size_t cs = (size_t)(w / 2) * (h / 2);

    uint8_t *Y = buf;
    uint8_t *U = buf + ys;
    uint8_t *V = U + cs;

    for (int y = 0; y < h; y++) {
        const uint8_t *row = c->rgba + (size_t)y * c->w * 4;
        for (int x = 0; x < w; x++) {
            const uint8_t *p = row + x * 4;
            int r = p[0], g = p[1], b = p[2];
            int yy = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            Y[(size_t)y * w + x] = (uint8_t)(yy < 0 ? 0 : (yy > 255 ? 255 : yy));
        }
    }

    for (int y = 0; y < h; y += 2) {
        for (int x = 0; x < w; x += 2) {
            int sr = 0, sg = 0, sb = 0;
            for (int dy = 0; dy < 2; dy++) {
                const uint8_t *row = c->rgba + (size_t)(y + dy) * c->w * 4;
                for (int dx = 0; dx < 2; dx++) {
                    const uint8_t *p = row + (x + dx) * 4;
                    sr += p[0]; sg += p[1]; sb += p[2];
                }
            }
            int r = sr / 4, g = sg / 4, b = sb / 4;
            int uu = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
            int vv = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
            U[(size_t)(y / 2) * (w / 2) + x / 2] = (uint8_t)(uu < 0 ? 0 : (uu > 255 ? 255 : uu));
            V[(size_t)(y / 2) * (w / 2) + x / 2] = (uint8_t)(vv < 0 ? 0 : (vv > 255 ? 255 : vv));
        }
    }
}

int tw_canvas_to_i420(const tw_canvas *c, uint8_t **out, size_t *out_size)
{
    size_t total = tw_i420_size(c);
    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf)
        return -1;
    tw_canvas_to_i420_into(c, buf);
    *out = buf;
    if (out_size)
        *out_size = total;
    return 0;
}

/* ------------------------------------------------------------------ */
/* texto UTF-8 con stb_truetype                                        */
/* ------------------------------------------------------------------ */

static int utf8_next(const char **s, int *cp)
{
    const unsigned char *p = (const unsigned char *)*s;
    if (!*p)
        return 0;

    int c = *p++;
    if (c < 0x80) {
        *cp = c;
    } else if ((c & 0xE0) == 0xC0 && (p[0] & 0xC0) == 0x80) {
        *cp = ((c & 0x1F) << 6) | (p[0] & 0x3F);
        p += 1;
    } else if ((c & 0xF0) == 0xE0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
        *cp = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
        p += 2;
    } else if ((c & 0xF8) == 0xF0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80 &&
               (p[2] & 0xC0) == 0x80) {
        *cp = ((c & 0x07) << 18) | ((p[0] & 0x3F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        p += 3;
    } else {
        *cp = '?';
    }

    *s = (const char *)p;
    return 1;
}

int tw_font_load(tw_font *f, const char *path, float pixel_height)
{
    memset(f, 0, sizeof *f);

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -1;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n <= 0) {
        fclose(fp);
        return -1;
    }

    f->data = (unsigned char *)malloc((size_t)n);
    if (!f->data) {
        fclose(fp);
        return -1;
    }
    if (fread(f->data, 1, (size_t)n, fp) != (size_t)n) {
        fclose(fp);
        free(f->data);
        memset(f, 0, sizeof *f);
        return -1;
    }
    fclose(fp);

    int offset = stbtt_GetFontOffsetForIndex(f->data, 0);
    if (!stbtt_InitFont(&f->info, f->data, offset)) {
        free(f->data);
        memset(f, 0, sizeof *f);
        return -1;
    }

    f->scale = stbtt_ScaleForPixelHeight(&f->info, pixel_height);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&f->info, &asc, &desc, &gap);
    f->ascent = asc * f->scale;
    f->descent = desc * f->scale;
    return 0;
}

void tw_font_free(tw_font *f)
{
    free(f->data);
    memset(f, 0, sizeof *f);
}

float tw_text_width(const tw_font *f, const char *utf8)
{
    float x = 0.0f;
    int prev = 0;
    const char *s = utf8;
    int cp;

    while (utf8_next(&s, &cp)) {
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
        if (prev)
            x += stbtt_GetCodepointKernAdvance(&f->info, prev, cp) * f->scale;
        x += adv * f->scale;
        prev = cp;
    }
    return x;
}

void tw_draw_text(tw_canvas *c, const tw_font *f, const char *utf8, float x,
                  float baseline, tw_color col)
{
    const char *s = utf8;
    int cp, prev = 0;
    float pen = x;

    while (utf8_next(&s, &cp)) {
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
        if (prev)
            pen += stbtt_GetCodepointKernAdvance(&f->info, prev, cp) * f->scale;

        int gw = 0, gh = 0, xoff = 0, yoff = 0;
        unsigned char *bmp = stbtt_GetCodepointBitmap(&f->info, f->scale, f->scale,
                                                      cp, &gw, &gh, &xoff, &yoff);
        if (bmp) {
            int gx = (int)floorf(pen + 0.5f) + xoff;
            int gy = (int)floorf(baseline + 0.5f) + yoff;
            for (int row = 0; row < gh; row++) {
                for (int colx = 0; colx < gw; colx++) {
                    unsigned char a = bmp[row * gw + colx];
                    if (a)
                        tw_px_blend(c, gx + colx, gy + row, col, a / 255.0f);
                }
            }
            stbtt_FreeBitmap(bmp, NULL);
        }

        pen += adv * f->scale;
        prev = cp;
    }
}
