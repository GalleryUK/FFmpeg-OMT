/*
 * libOMT  demuxer
 * Copyright (c) 2025 Open Media Transport Contributors <omt@gallery.co.uk>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavformat/avformat.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"    
#include "libomt_common.h"
#include "libavutil/channel_layout.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavformat/demux.h"
#include "libavformat/internal.h"
#include "avdevice.h"
#include "libavdevice/version.h"
#include "libavutil/mem.h"

#include <unistd.h>

struct OMTContext {
    const AVClass *class;  // MUST be first field for AVOptions!
    float reference_level;
    int find_sources;
    int tenbit;
    int nativevmx;
    omt_receive_t *recv;    
    AVStream *video_st, *audio_st;
};

static void convert_p216_to_yuv422p10le(uint16_t* src_p216,  int linesizeP216, uint16_t* tgt_y,  uint16_t* tgt_cb,  uint16_t* tgt_cr,  int width, int height)
{
    int y;
    int sourceYOffset,sourceUVOffset, targetYOffset, targetUVOffset;
    uint16_t *ylocal = tgt_y;
    uint16_t *ulocal = tgt_cb;
    uint16_t *vlocal = tgt_cr;
       
    sourceUVOffset =  height * linesizeP216;
    sourceYOffset = 0;
    targetYOffset = 0;
    targetUVOffset = 0; 
    
    /* LUMA FIRST */
    for (y=0;y<height*width;y+=2) {
        ylocal[targetYOffset++] = (src_p216[sourceYOffset++]) >> 6;
        ylocal[targetYOffset++] = (src_p216[sourceYOffset++]) >> 6;
    }
    
    /* CHROMA */
    sourceUVOffset =  height * width;
    targetUVOffset = 0;
    for (y=0;y<height*width;y+=2) {
        ulocal[targetUVOffset] = (src_p216[sourceUVOffset++]) >> 6;
        vlocal[targetUVOffset++] = (src_p216[sourceUVOffset++]) >> 6;
    }
    
    return;
}



static int omt_set_video_packet(AVFormatContext *avctx, OMTMediaFrame *v, AVPacket *pkt)
{

    av_log(avctx, AV_LOG_DEBUG, "omt_set_video_packet %dx%d stride=%d\n",v->Width,v->Height,v->Stride);

    int ret;
    struct OMTContext *ctx = (struct OMTContext *)avctx->priv_data;

    if (ctx->nativevmx && v->Codec == OMTCodec_VMX1)
        ret = av_new_packet(pkt, v->CompressedLength);
    else
        ret = av_new_packet(pkt, v->Height * v->Stride);
    
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "omt_set_video_packet av_new_packet failed error %d\n",ret);
        return ret;
    }
    
    pkt->dts = pkt->pts = av_rescale_q(v->Timestamp, OMT_TIME_BASE_Q, ctx->video_st->time_base);
    pkt->duration = av_rescale_q(1, (AVRational){v->FrameRateD, v->FrameRateN}, ctx->video_st->time_base);

    av_log(avctx, AV_LOG_DEBUG, "%s: pkt->dts = pkt->pts = %"PRId64", duration=%"PRId64", timecode=%"PRId64"\n",
        __func__, pkt->dts, pkt->duration, v->Timestamp);

    pkt->flags         |= AV_PKT_FLAG_KEY;
    pkt->stream_index   = ctx->video_st->index;
    
    switch (v->Codec)
    {
        case OMTCodec_VMX1:
            av_log(avctx, AV_LOG_DEBUG, "Got a native VMX Packet\n");
            av_log(avctx, AV_LOG_DEBUG, "copy %d bytes of VMX into AVPacket\n",v->CompressedLength);
            memcpy(pkt->data, v->CompressedData, v->CompressedLength);
        break;
        
        case OMTCodec_UYVY:case OMTCodec_UYVA:case OMTCodec_BGRA:
             memcpy(pkt->data, v->Data, pkt->size);
        break;
        
        case OMTCodec_P216:case OMTCodec_PA16:
        {
            int uOff =  v->Stride * v->Height; 
            int yOff = uOff+(v->Stride * v->Height >> 1) ; //2 bytes of every other pixel
            convert_p216_to_yuv422p10le((uint16_t*)v->Data, v->Stride, (uint16_t*)pkt->data, (uint16_t*)((uint8_t*)pkt->data+uOff),  (uint16_t*)((uint8_t*)pkt->data+yOff), v->Width, v->Height);
            av_log(avctx, AV_LOG_DEBUG, "convert_p216_to_yuv422p10le\n");
        }
        break;
        
        default:
                 memcpy(pkt->data, v->Data, pkt->size);
         break;
    }
    av_log(avctx, AV_LOG_DEBUG, "omt_set_video_packet memcpy %d bytes\n",pkt->size);
    return 0;
}


static int    convertPlanarFloatToInterleavedShorts(float * floatData,int sourceChannels,int sourceSamplesPerChannel,uint8_t * outputData,float referenceLevel)
{
     /* Cast output to int16_t for easier assignment */
    int16_t *out = (int16_t *)outputData;
    /* For each sample across all channels */
    for (int sampleIdx = 0; sampleIdx < sourceSamplesPerChannel; ++sampleIdx) {
        for (int ch = 0; ch < sourceChannels; ++ch) {
            /* Planar layout: [ch0][ch1][ch2]...
            Each plane is sourceSamplesPerChannel floats */
            float val = floatData[ch * sourceSamplesPerChannel + sampleIdx];
            /* Scale by referenceLevel (assumed fullscale is +/-referenceLevel) */
            float scaled = val / referenceLevel;
            /* Clamp to [-1, 1] */
            if (scaled > 1.0f) scaled = 1.0f;
            else if (scaled < -1.0f) scaled = -1.0f;
            /* Convert to int16_t range */
            int16_t sample = (int16_t)lrintf(scaled * 32767.0f);
            /* Interleaved output order */
            out[sampleIdx * sourceChannels + ch] = sample;
        }
    }
    /* Returns the total number of output bytes written */
    return sourceSamplesPerChannel * sourceChannels * (int)sizeof(int16_t);
}


static int omt_set_audio_packet(AVFormatContext *avctx, OMTMediaFrame *a, AVPacket *pkt)
{
    av_log(avctx, AV_LOG_DEBUG, "omt_set_audio_packet \n");

    int ret;
    struct OMTContext *ctx = (struct OMTContext *)avctx->priv_data;
    ret = av_new_packet(pkt, 2 * a->SamplesPerChannel * a->Channels);
    if (ret < 0)
        return ret;

    pkt->dts = pkt->pts = av_rescale_q(a->Timestamp, OMT_TIME_BASE_Q, ctx->audio_st->time_base);
    pkt->duration = av_rescale_q(1, (AVRational){a->SamplesPerChannel, a->SampleRate}, ctx->audio_st->time_base);

    av_log(avctx, AV_LOG_DEBUG, "%s: pkt->dts = pkt->pts = %"PRId64", duration=%"PRId64", timecode=%"PRId64"\n",
        __func__, pkt->dts, pkt->duration, a->Timestamp);

    pkt->flags       |= AV_PKT_FLAG_KEY;
    pkt->stream_index = ctx->audio_st->index;

    convertPlanarFloatToInterleavedShorts(a->Data,a->Channels,a->SamplesPerChannel,pkt->data, ctx->reference_level);

    return 0;
}

static int omt_find_sources(AVFormatContext *avctx, const char *name)
{
    int OMTcount = 0;
    char **omtSources = NULL;
    OMTcount = 0;
    omtSources = omt_discovery_getaddresses(&OMTcount);
	/* give it some time and check again */
    usleep(1000000);
    
    OMTcount = 0;
    omtSources = omt_discovery_getaddresses(&OMTcount);
    if (OMTcount > 0) 
    {
        av_log(avctx, AV_LOG_INFO, "-------------- %d OMT Sources-------------\n",OMTcount);
        for (int z=0;z<OMTcount > 0;z++) 
            av_log(avctx, AV_LOG_INFO, "%s\n", omtSources[z]);
        av_log(avctx, AV_LOG_INFO, "-------------------------------------------\n");
    }
    else
    {
        av_log(avctx, AV_LOG_INFO,"No OMT Sources found\n");
    }
    return 0;
}




static int omt_read_header(AVFormatContext *avctx)
{

    av_log(avctx, AV_LOG_DEBUG, "omt_read_header for URL=%s.\n",avctx->url);

    const OMTTally tally_state = { .program = 1, .preview = 1 };
    struct OMTContext *ctx = (struct OMTContext *)avctx->priv_data;
    if (ctx->find_sources) {
        omt_find_sources(avctx, avctx->url);
        return  AVERROR(EIO);
    }
    
    if (ctx->nativevmx) {
        ctx->recv = omt_receive_create(avctx->url, (OMTFrameType)(OMTFrameType_Video | OMTFrameType_Audio | OMTFrameType_Metadata), (OMTPreferredVideoFormat)OMTPreferredVideoFormat_UYVYorUYVAorP216orPA16, (OMTReceiveFlags)OMTReceiveFlags_CompressedOnly);
    }  
    else {
        if (ctx->tenbit)
            ctx->recv = omt_receive_create(avctx->url, (OMTFrameType)(OMTFrameType_Video | OMTFrameType_Audio | OMTFrameType_Metadata), (OMTPreferredVideoFormat)OMTPreferredVideoFormat_UYVYorUYVAorP216orPA16, (OMTReceiveFlags)OMTReceiveFlags_None);
        else
            ctx->recv = omt_receive_create(avctx->url, (OMTFrameType)(OMTFrameType_Video | OMTFrameType_Audio | OMTFrameType_Metadata), (OMTPreferredVideoFormat)OMTPreferredVideoFormat_UYVYorBGRA, (OMTReceiveFlags)OMTReceiveFlags_None);
    }
    
    if (!ctx->recv) {
        av_log(avctx, AV_LOG_ERROR, "omt_receive_create failed.\n");
        return AVERROR(EIO);
    }

    /* Set tally */
    omt_receive_settally(ctx->recv, (OMTTally *)&tally_state);

    avctx->ctx_flags |= AVFMTCTX_NOHEADER;

    return 0; 
}


static int omt_create_video_stream(AVFormatContext *avctx, OMTMediaFrame *v)
{

    av_log(avctx, AV_LOG_DEBUG, "omt_create_video_stream \n");


    AVStream *st;
    AVRational tmp;
    struct OMTContext *ctx = (struct OMTContext *)avctx->priv_data;
    st = avformat_new_stream(avctx, NULL);
    if (!st) {
        av_log(avctx, AV_LOG_ERROR, "Cannot add video stream\n");
        return AVERROR(ENOMEM);
    }

    st->time_base                   = OMT_TIME_BASE_Q;
    st->r_frame_rate                = av_make_q(v->FrameRateN, v->FrameRateD);

    tmp = av_mul_q(av_d2q(v->AspectRatio, INT_MAX), (AVRational){v->Height, v->Width});
    av_reduce(&st->sample_aspect_ratio.num, &st->sample_aspect_ratio.den, tmp.num, tmp.den, 1000);
    st->codecpar->sample_aspect_ratio = st->sample_aspect_ratio;

    av_log(avctx, AV_LOG_DEBUG, "Video Stream frame_rate = %d/%d (approx %.3f fps)\n", 
    st->r_frame_rate.num, st->r_frame_rate.den,
    (double)st->r_frame_rate.num / st->r_frame_rate.den);
    
    av_log(avctx, AV_LOG_DEBUG, "Video Stream sample_aspect_ratio = %d/%d (approx %.3f)\n",
       st->codecpar->sample_aspect_ratio.num,
       st->codecpar->sample_aspect_ratio.den,
       (double)st->codecpar->sample_aspect_ratio.num / st->codecpar->sample_aspect_ratio.den);
       
    st->codecpar->codec_type        = AVMEDIA_TYPE_VIDEO;
    st->codecpar->width             = v->Width;
    st->codecpar->height            = v->Height;
    st->codecpar->codec_id          = AV_CODEC_ID_RAWVIDEO;
    st->codecpar->bit_rate          = av_rescale(v->Width * v->Height * 16, v->FrameRateN, v->FrameRateD);
    st->codecpar->field_order       = (v->Flags & OMTVideoFlags_Interlaced) ? AV_FIELD_TT : AV_FIELD_PROGRESSIVE;

    switch(v->Codec)
    {
    
        case OMTCodec_VMX1: 
            st->codecpar->codec_id  = AV_CODEC_ID_VMIX;
            st->codecpar->codec_tag  = MKTAG('V', 'M', 'X', '1');
        break;
        
        case OMTCodec_UYVY:case OMTCodec_UYVA:
            st->codecpar->format        = AV_PIX_FMT_UYVY422;
            st->codecpar->codec_tag     = MKTAG('U', 'Y', 'V', 'Y');
            if (OMTCodec_UYVA == v->Codec)
                av_log(avctx, AV_LOG_WARNING, "Alpha channel ignored\n");
        break;
        
        case OMTCodec_BGRA:
            st->codecpar->format        = AV_PIX_FMT_BGRA;
            st->codecpar->codec_tag     = MKTAG('B', 'G', 'R', 'A');
        break;
        
        case OMTCodec_P216:case OMTCodec_PA16:
            st->codecpar->format        = AV_PIX_FMT_YUV422P10LE;
            st->codecpar->codec_tag     = MKTAG('Y', '3', 10 , 10);
            st->codecpar->bits_per_coded_sample = 16;
            st->codecpar->bits_per_raw_sample = 16;
            if (OMTCodec_PA16 == v->Codec)
                av_log(avctx, AV_LOG_WARNING, "Alpha channel ignored\n");
        break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Unsupported video stream format, v->Codec=%d\n", v->Codec);
            return AVERROR(EINVAL);
        break;
      }
    
    avpriv_set_pts_info(st, 64, 1, OMT_TIME_BASE);

    ctx->video_st = st;

    return 0;
}

static int omt_create_audio_stream(AVFormatContext *avctx, OMTMediaFrame *a)
{
    av_log(avctx, AV_LOG_DEBUG, "omt_create_audio_stream \n");

    AVStream *st;
    struct OMTContext *ctx = (struct OMTContext *)avctx->priv_data;
    st = avformat_new_stream(avctx, NULL);
    if (!st) {
        av_log(avctx, AV_LOG_ERROR, "Cannot add audio stream\n");
        return AVERROR(ENOMEM);
    }

    st->codecpar->codec_type        = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id          = AV_CODEC_ID_PCM_S16LE;
    st->codecpar->sample_rate       = a->SampleRate;
    av_channel_layout_default(&st->codecpar->ch_layout, a->Channels);

    avpriv_set_pts_info(st, 64, 1, OMT_TIME_BASE);

    ctx->audio_st = st;

    return 0;
}

static int omt_read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    av_log(avctx, AV_LOG_DEBUG, "omt_read_packet \n");

    int ret = 0;
    struct OMTContext *ctx = (struct OMTContext *)avctx->priv_data;
    OMTMediaFrame * theOMTFrame;
    OMTFrameType t = OMTFrameType_None;

    theOMTFrame = omt_receive(ctx->recv, (OMTFrameType) (OMTFrameType_Video|OMTFrameType_Audio|OMTFrameType_Metadata), 40);
    if (theOMTFrame)  {
        t = theOMTFrame->Type;
    }
    
    switch (t)
    {
        case OMTFrameType_Video:
        
             av_log(avctx, AV_LOG_DEBUG, "omt_received video\n");

            if (!ctx->video_st)
                ret = omt_create_video_stream(avctx, theOMTFrame);
            if (!ret)
                ret = omt_set_video_packet(avctx, theOMTFrame, pkt);
                
            if (ctx->nativevmx)  {
                av_log(avctx, AV_LOG_DEBUG, "Compressed Data RECVD %d bytes\n", theOMTFrame->CompressedLength);
                for (int i=0;i<64;i++)
                {
                    av_log(avctx, AV_LOG_DEBUG, "%02x ",((uint8_t*)(theOMTFrame->CompressedData))[i]);
                }
                av_log(avctx, AV_LOG_DEBUG, "\n");
            }

        break;
        
        case OMTFrameType_Audio:
             av_log(avctx, AV_LOG_DEBUG, "omt_received audio\n");
            if (!ctx->audio_st)
                ret = omt_create_audio_stream(avctx, theOMTFrame);
            if (!ret)
                ret = omt_set_audio_packet(avctx, theOMTFrame, pkt);
        break;
    
        case OMTFrameType_Metadata:
            av_log(avctx, AV_LOG_DEBUG, "omt_received metadata\n");
            ret = AVERROR(EAGAIN);
        break;
        
        case OMTFrameType_None: default:
            av_log(avctx, AV_LOG_DEBUG, "omt_received none, skipping\n");
            ret = AVERROR(EAGAIN);
        break;
    }
    
    return ret;
}


static int omt_read_close(AVFormatContext *avctx)
{
    av_log(avctx, AV_LOG_DEBUG, "omt_read_close \n");
    struct OMTContext *ctx = (struct OMTContext *)avctx->priv_data;
    if (ctx->recv)
        omt_receive_destroy(ctx->recv);

    return 0;
}


#define OFFSET(x) offsetof(struct OMTContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "find_sources", "Find available sources"  , OFFSET(find_sources), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, DEC },
    { "tenbit", "Decode into 10-bit if possible"  , OFFSET(tenbit), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, DEC },
    { "reference_level", "The audio reference level as floating point full scale deflection", OFFSET(reference_level), AV_OPT_TYPE_FLOAT, { .dbl = 1.0 }, 0.0, 20.0, DEC },
    { "nativevmx", "Ingest native VMX"  , OFFSET(nativevmx), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, DEC },
    { NULL },
};



static const AVClass libomt_demuxer_class = {
    .class_name = "OMT demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
};


#if LIBAVDEVICE_VERSION_MAJOR >= 62

    const FFInputFormat ff_libomt_demuxer = {
        .p.name          = "libomt",
        .p.long_name     = NULL_IF_CONFIG_SMALL("OpenMediaTransport(OMT) input using libomt library"),
        .p.priv_class     = &libomt_demuxer_class,
        .p.flags          = AVFMT_NOFILE,
        .priv_data_size = sizeof(struct OMTContext),
        .read_header      = omt_read_header,    
        .read_packet      = omt_read_packet,
        .read_close       = omt_read_close,
    };

#else

    const AVInputFormat ff_libomt_demuxer = {
        .name          = "libomt",
        .long_name     = NULL_IF_CONFIG_SMALL("OpenMediaTransport(OMT) input using libomt library"),
        .priv_class    = &libomt_demuxer_class,
        .flags         = AVFMT_NOFILE,
        .priv_data_size = sizeof(struct OMTContext),
        .read_header   = omt_read_header,
        .read_packet   = omt_read_packet,
        .read_close    = omt_read_close,
    };

#endif


