/*
 * image.h - carga y escalado de imagenes (avatar) a RGBA8.
 */
#ifndef TW_IMAGE_H
#define TW_IMAGE_H

#include <stdint.h>

typedef struct {
    uint8_t *rgba;   /* w*h*4, orden R,G,B,A */
    int      w, h;
} tw_image;

int  tw_image_load(const char *path, tw_image *out);
void tw_image_free(tw_image *img);

/* Genera un placeholder solido (para cuando no hay avatar). */
int  tw_image_solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, tw_image *out);

/* Recorta al cuadrado central y reescala a size x size. */
int  tw_image_fit_square(const tw_image *src, int size, tw_image *out);

#endif /* TW_IMAGE_H */
