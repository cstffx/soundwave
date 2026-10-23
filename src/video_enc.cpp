/*
 * video_enc.cpp - encoder H.264 sobre OpenH264.
 *
 * OpenH264 expone su API como una clase C++ (ISVCEncoder), asi que este
 * archivo se compila con g++ aunque el resto del proyecto sea C.
 */
#include "video_enc.h"

#include <wels/codec_api.h>

#include <stdlib.h>
#include <string.h>

struct tw_h264enc {
    ISVCEncoder *enc;
    int          width, height, fps;
    int          force_idr;
};

static ECOMPLEXITY_MODE map_complexity(int c)
{
    switch (c) {
    case 0:  return LOW_COMPLEXITY;
    case 2:  return HIGH_COMPLEXITY;
    default: return MEDIUM_COMPLEXITY;
    }
}

static EProfileIdc map_profile(int p)
{
    switch (p) {
    case 0:  return PRO_BASELINE;
    case 1:  return PRO_MAIN;
    default: return PRO_HIGH;
    }
}

int tw_h264_open(tw_h264enc **out, const tw_h264_cfg *cfg)
{
    *out = NULL;

    tw_h264enc *e = (tw_h264enc *)calloc(1, sizeof *e);
    if (!e)
        return -1;

    e->width  = cfg->width  & ~1;
    e->height = cfg->height & ~1;
    e->fps    = cfg->fps > 0 ? cfg->fps : 25;

    if (WelsCreateSVCEncoder(&e->enc) != 0 || !e->enc) {
        free(e);
        return -1;
    }

    /* Sin ruido en la salida estandar. */
    int trace = WELS_LOG_QUIET;
    e->enc->SetOption(ENCODER_OPTION_TRACE_LEVEL, &trace);

    SEncParamExt p;
    memset(&p, 0, sizeof p);
    e->enc->GetDefaultParams(&p);

    const int fps = e->fps;
    int kf_s = cfg->keyframe_period_s > 0 ? cfg->keyframe_period_s : 5;

    p.iUsageType          = CAMERA_VIDEO_REAL_TIME;
    p.iPicWidth           = e->width;
    p.iPicHeight          = e->height;
    p.fMaxFrameRate       = (float)fps;
    p.iRCMode             = cfg->rc_mode == 1 ? RC_BITRATE_MODE : RC_QUALITY_MODE;
    p.iTargetBitrate      = cfg->bitrate;
    p.iTemporalLayerNum   = 1;
    p.iSpatialLayerNum    = 1;
    p.iMultipleThreadIdc  = cfg->threads > 0 ? cfg->threads : 1;
    p.iComplexityMode     = map_complexity(cfg->complexity);
    p.bEnableFrameSkip    = false;             /* fps constante */
    p.bEnableDenoise      = false;
    p.bEnableBackgroundDetection = true;
    p.bEnableAdaptiveQuant = true;
    p.uiIntraPeriod       = (unsigned)(kf_s * fps);
    p.iNumRefFrame        = 2;
    /* IDs fijos de SPS/PPS: si el encoder los incrementa en cada IDR, el
     * muxer tiene que reescribir cabeceras y es una fuente de corrupcion. */
    p.eSpsPpsIdStrategy   = CONSTANT_ID;
    p.bPrefixNalAddingCtrl = false;
    p.iEntropyCodingModeFlag = (cfg->profile == 0) ? 0 : 1;  /* CABAC salvo baseline */
    p.iMinQp              = (cfg->qp_min > 0 && cfg->qp_min < 52) ? cfg->qp_min : 10;
    p.iMaxQp              = cfg->qp_max > 0 ? cfg->qp_max : 51;
    if (p.iMaxQp < p.iMinQp)
        p.iMaxQp = p.iMinQp;

    SSpatialLayerConfig *sl = &p.sSpatialLayers[0];
    sl->iVideoWidth       = e->width;
    sl->iVideoHeight      = e->height;
    sl->fFrameRate        = (float)fps;
    sl->iSpatialBitrate   = cfg->bitrate;
    sl->iMaxSpatialBitrate = cfg->bitrate + cfg->bitrate / 2;
    sl->uiProfileIdc      = map_profile(cfg->profile);
    sl->uiLevelIdc        = LEVEL_UNKNOWN;     /* automatico */
    sl->iDLayerQp         = 0;
    sl->sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;

    if (e->enc->InitializeExt(&p) != 0) {
        WelsDestroySVCEncoder(e->enc);
        free(e);
        return -1;
    }

    int fmt = videoFormatI420;
    e->enc->SetOption(ENCODER_OPTION_DATAFORMAT, &fmt);

    e->force_idr = 1;
    *out = e;
    return 0;
}

void tw_h264_close(tw_h264enc *enc)
{
    if (!enc)
        return;
    if (enc->enc) {
        enc->enc->Uninitialize();
        WelsDestroySVCEncoder(enc->enc);
    }
    free(enc);
}

void tw_h264_force_keyframe(tw_h264enc *enc)
{
    if (enc)
        enc->force_idr = 1;
}

int tw_h264_encode(tw_h264enc *enc, const uint8_t *i420, int64_t frame_no,
                   tw_nal_sink_fn sink, void *ctx)
{
    (void)frame_no;

    if (enc->force_idr) {
        enc->enc->ForceIntraFrame(true);
        enc->force_idr = 0;
    }

    const int w = enc->width, h = enc->height;
    const uint8_t *Y = i420;
    const uint8_t *U = Y + (size_t)w * h;
    const uint8_t *V = U + (size_t)(w / 2) * (h / 2);

    SSourcePicture pic;
    memset(&pic, 0, sizeof pic);
    pic.iPicWidth    = w;
    pic.iPicHeight   = h;
    pic.iColorFormat = videoFormatI420;
    pic.iStride[0]   = w;
    pic.iStride[1]   = w / 2;
    pic.iStride[2]   = w / 2;
    pic.pData[0] = (unsigned char *)Y;
    pic.pData[1] = (unsigned char *)U;
    pic.pData[2] = (unsigned char *)V;

    SFrameBSInfo info;
    memset(&info, 0, sizeof info);

    if (enc->enc->EncodeFrame(&pic, &info) != cmResultSuccess)
        return -1;
    if (info.eFrameType == videoFrameTypeSkip)
        return 0;

    const int key = (info.eFrameType == videoFrameTypeIDR ||
                     info.eFrameType == videoFrameTypeI);

    for (int i = 0; i < info.iLayerNum; i++) {
        const SLayerBSInfo *layer = &info.sLayerInfo[i];
        int total = 0;
        for (int j = 0; j < layer->iNalCount; j++)
            total += layer->pNalLengthInByte[j];
        if (total <= 0)
            continue;
        if (sink(ctx, layer->pBsBuf, total, key) != 0)
            return -1;
    }
    return 0;
}
