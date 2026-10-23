/*
 * video_enc.h - encoder H.264 sobre OpenH264 (BSD-2).
 *
 * Entrega NAL units en formato Annex-B (con start codes), que es justo lo que
 * consume el muxer minimp4.
 */
#ifndef TW_VIDEO_ENC_H
#define TW_VIDEO_ENC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tw_h264enc tw_h264enc;

typedef struct {
    int width, height;      /* deben ser pares */
    int fps;
    int bitrate;            /* bits por segundo */
    int complexity;         /* 0=baja 1=media 2=alta (esfuerzo del encoder) */
    int profile;            /* 0=baseline 1=main 2=high */
    int rc_mode;            /* 0=calidad (por defecto) 1=bitrate estricto */
    int qp_min;             /* calidad maxima permitida (0 = 10) */
    int keyframe_period_s;  /* segundos entre IDR (0 = 5) */
    int qp_max;             /* tope de QP (calidad minima), 0 = 51 */
    int threads;
} tw_h264_cfg;

/* annexb = NALs concatenadas con start code. */
typedef int (*tw_nal_sink_fn)(void *ctx, const uint8_t *annexb, int len, int keyframe);

int  tw_h264_open(tw_h264enc **out, const tw_h264_cfg *cfg);
void tw_h264_close(tw_h264enc *enc);

/* Codifica un cuadro I420 (Y, U, V planar). frame_no es informativo. */
int  tw_h264_encode(tw_h264enc *enc, const uint8_t *i420, int64_t frame_no,
                    tw_nal_sink_fn sink, void *ctx);

/* Fuerza que el proximo cuadro sea IDR. */
void tw_h264_force_keyframe(tw_h264enc *enc);

#ifdef __cplusplus
}
#endif

#endif /* TW_VIDEO_ENC_H */
