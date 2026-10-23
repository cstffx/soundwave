/*
 * mux.h - empaquetado MP4 (minimp4).
 *
 * Track 0: video H.264. Track 1: audio AAC.
 * Las llamadas se intercalan en orden temporal (el orden de llamada define el
 * orden dentro del 'mdat').
 */
#ifndef TW_MUX_H
#define TW_MUX_H

#include <stdint.h>

typedef struct tw_muxer tw_muxer;

typedef struct {
    const char    *path;
    int            width, height;
    int            fps;
    int            sample_rate;
    int            channels;
    const uint8_t *aac_dsi;
    int            aac_dsi_len;
    int            aac_frame_samples;
} tw_mux_cfg;

int tw_mux_open(tw_muxer **out, const tw_mux_cfg *cfg);
int tw_mux_close(tw_muxer *m);

/* NALs en Annex-B (pueden venir varias concatenadas). */
int tw_mux_write_video(tw_muxer *m, const uint8_t *annexb, int len);

/* Un access unit AAC crudo (sin ADTS). */
int tw_mux_write_audio(tw_muxer *m, const uint8_t *aac, int len);

/* Tamano actual del archivo en bytes. */
int64_t tw_mux_bytes(const tw_muxer *m);

#endif /* TW_MUX_H */
