#include "mux.h"

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct tw_muxer {
    FILE             *f;
    MP4E_mux_t       *mux;
    mp4_h26x_writer_t vw;
    int               audio_track;
    int               fps;
    int               aac_frame_samples;
    int               video_ready;
};

static int write_cb(int64_t offset, const void *buffer, size_t size, void *token)
{
    FILE *f = (FILE *)token;
    if (fseeko(f, (off_t)offset, SEEK_SET) != 0)
        return 1;
    return fwrite(buffer, 1, size, f) != size;
}

int tw_mux_open(tw_muxer **out, const tw_mux_cfg *cfg)
{
    *out = NULL;

    tw_muxer *m = (tw_muxer *)calloc(1, sizeof *m);
    if (!m)
        return -1;

    m->f = fopen(cfg->path, "wb");
    if (!m->f) {
        free(m);
        return -1;
    }

    /* modo no secuencial: permite el callback con offsets (fseek) */
    m->mux = MP4E_open(0, 0, m->f, write_cb);
    if (!m->mux) {
        fclose(m->f);
        free(m);
        return -1;
    }

    if (mp4_h26x_write_init(&m->vw, m->mux, cfg->width, cfg->height, 0) != MP4E_STATUS_OK) {
        MP4E_close(m->mux);
        fclose(m->f);
        free(m);
        return -1;
    }
    m->video_ready = 1;

    MP4E_track_t at;
    memset(&at, 0, sizeof at);
    at.track_media_kind = e_audio;
    at.object_type_indication = MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3;
    at.language[0] = 'u';
    at.language[1] = 'n';
    at.language[2] = 'd';
    at.time_scale = (unsigned)cfg->sample_rate;
    at.default_duration = (unsigned)cfg->aac_frame_samples;
    at.u.a.channelcount = (unsigned)cfg->channels;

    int aid = MP4E_add_track(m->mux, &at);
    if (aid < 0) {
        MP4E_close(m->mux);
        fclose(m->f);
        free(m);
        return -1;
    }
    m->audio_track = aid;

    if (cfg->aac_dsi && cfg->aac_dsi_len > 0) {
        if (MP4E_set_dsi(m->mux, aid, cfg->aac_dsi, cfg->aac_dsi_len) != MP4E_STATUS_OK) {
            MP4E_close(m->mux);
            fclose(m->f);
            free(m);
            return -1;
        }
    }

    m->fps = cfg->fps > 0 ? cfg->fps : 25;
    m->aac_frame_samples = cfg->aac_frame_samples;

    *out = m;
    return 0;
}

int tw_mux_write_video(tw_muxer *m, const uint8_t *annexb, int len)
{
    if (!m->video_ready || len <= 0)
        return -1;
    int dur = 90000 / m->fps;
    return mp4_h26x_write_nal(&m->vw, annexb, len, dur);
}

int tw_mux_write_audio(tw_muxer *m, const uint8_t *aac, int len)
{
    if (len <= 0)
        return -1;
    return MP4E_put_sample(m->mux, m->audio_track, aac, len,
                           m->aac_frame_samples, MP4E_SAMPLE_DEFAULT);
}

int64_t tw_mux_bytes(const tw_muxer *m)
{
    long cur = ftell(m->f);
    if (cur < 0)
        return -1;
    return (int64_t)cur;
}

int tw_mux_close(tw_muxer *m)
{
    if (!m)
        return -1;

    if (m->video_ready)
        mp4_h26x_write_close(&m->vw);

    int rc = MP4E_close(m->mux);
    if (m->f)
        fclose(m->f);
    free(m);
    return rc == MP4E_STATUS_OK ? 0 : -1;
}
