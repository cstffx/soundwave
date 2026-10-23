#include "image.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#include <stdlib.h>
#include <string.h>

int tw_image_load(const char *path, tw_image *out)
{
    memset(out, 0, sizeof *out);

    int w = 0, h = 0, comp = 0;
    unsigned char *pixels = stbi_load(path, &w, &h, &comp, 4);
    if (!pixels || w <= 0 || h <= 0)
        return -1;

    out->rgba = (uint8_t *)pixels;
    out->w = w;
    out->h = h;
    return 0;
}

void tw_image_free(tw_image *img)
{
    if (img->rgba)
        stbi_image_free(img->rgba);
    memset(img, 0, sizeof *img);
}

int tw_image_solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, tw_image *out)
{
    memset(out, 0, sizeof *out);
    if (w <= 0 || h <= 0)
        return -1;

    out->rgba = (uint8_t *)malloc((size_t)w * h * 4);
    if (!out->rgba)
        return -1;

    for (size_t i = 0, n = (size_t)w * h; i < n; i++) {
        out->rgba[i * 4 + 0] = r;
        out->rgba[i * 4 + 1] = g;
        out->rgba[i * 4 + 2] = b;
        out->rgba[i * 4 + 3] = 255;
    }
    out->w = w;
    out->h = h;
    return 0;
}

int tw_image_fit_square(const tw_image *src, int size, tw_image *out)
{
    memset(out, 0, sizeof *out);
    if (!src->rgba || size <= 0)
        return -1;

    int side = (src->w < src->h) ? src->w : src->h;
    int ox = (src->w - side) / 2;
    int oy = (src->h - side) / 2;

    out->rgba = (uint8_t *)malloc((size_t)size * size * 4);
    if (!out->rgba)
        return -1;

    const unsigned char *in = src->rgba + ((size_t)oy * src->w + ox) * 4;
    unsigned char *r = stbir_resize_uint8_srgb(in, side, side, src->w * 4,
                                               out->rgba, size, size, size * 4,
                                               STBIR_RGBA);
    if (!r) {
        free(out->rgba);
        memset(out, 0, sizeof *out);
        return -1;
    }
    out->w = size;
    out->h = size;
    return 0;
}
