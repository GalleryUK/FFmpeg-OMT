/*
 * libOMT muxer
 * Copyright (c) 2025 Open Media Transport Contributors
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
#include "libavformat/internal.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"    // For AVClass and logging utilities
#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "libomt_common.h"
#include "libavutil/channel_layout.h"
#include "libavutil/internal.h"
#include "libavutil/frame.h"
#include "libavutil/internal.h"
#include "libavutil/time.h"
#include "libavformat/internal.h"
#include "libavformat/mux.h"
#include "avdevice.h"
#include <unistd.h>


#define kCacheBuffer 0

typedef struct OMTContext {
    const AVClass *cclass;

    /* Options */
    float reference_level;
    int clock_output;

    OMTMediaFrame video; 
    OMTMediaFrame audio;
	float * floataudio;
	#if kCacheBuffer
		uint8_t* submittedVideoData[2];
		int whichBuff;
	#endif
    omt_send_t * omt_send;
    struct AVFrame *last_avframe;
}OMTContext ;



static int omt_write_trailer(AVFormatContext *avctx)
{

 	av_log(avctx, AV_LOG_DEBUG, "omt_write_trailer.\n");

    struct OMTContext *ctx = avctx->priv_data;
    if (ctx->omt_send) 
    {
        omt_send_destroy(ctx->omt_send);
        av_frame_free(&ctx->last_avframe);
    }
    
    
    #if kCacheBuffer
        // make sure everyone is finished.
  		 usleep(100000);
		if (ctx->submittedVideoData[0])
		{
			free(ctx->submittedVideoData[0]);
			ctx->submittedVideoData[0] = 0;
		}
		if (ctx->submittedVideoData[1])
		{
			free(ctx->submittedVideoData[1]);
			ctx->submittedVideoData[1] = 0;
		} 
    #endif
         
    if (ctx->floataudio)
    {
    	free(ctx->floataudio);
    	ctx->floataudio = 0;
    }
 
 
    return 0;
}


static int dumpOMTMediaFrameInfo(AVFormatContext *avctx,OMTMediaFrame * video)
{
    av_log(avctx, AV_LOG_DEBUG, "dumpOMTMediaFrameInfo OMTMediaFrame = %llu\n",video);
    if (video)
    {
        if (video->Type == OMTFrameType_Video)
        {
                av_log(avctx, AV_LOG_DEBUG, "VIDEO FRAME:\n");
                av_log(avctx, AV_LOG_DEBUG, "Timestamp=%llu\n", video->Timestamp);
                av_log(avctx, AV_LOG_DEBUG, "Codec=%d\n", video->Codec);
                av_log(avctx, AV_LOG_DEBUG, "Width=%d\n", video->Width);
                av_log(avctx, AV_LOG_DEBUG, "Height=%d\n", video->Height);
                av_log(avctx, AV_LOG_DEBUG, "Stride=%d\n", video->Stride);
                av_log(avctx, AV_LOG_DEBUG, "Flags=%d\n", video->Flags);
                av_log(avctx, AV_LOG_DEBUG, "FrameRateN=%d\n", video->FrameRateN);
                av_log(avctx, AV_LOG_DEBUG, "FrameRateD=%d\n", video->FrameRateD);
                av_log(avctx, AV_LOG_DEBUG, "AspectRatio=%.2f\n", video->AspectRatio);
                av_log(avctx, AV_LOG_DEBUG, "ColorSpace=%d\n", video->ColorSpace);
                av_log(avctx, AV_LOG_DEBUG, "Data=%llu\n", video->Data);
                av_log(avctx, AV_LOG_DEBUG, "DataLength=%d\n", video->DataLength);
                av_log(avctx, AV_LOG_DEBUG, "CompressedData=%llu\n", video->CompressedData);
                av_log(avctx, AV_LOG_DEBUG, "CompressedLength=%llu\n", video->CompressedLength);
                av_log(avctx, AV_LOG_DEBUG, "FrameMetadata=%llu\n", video->FrameMetadata);
                av_log(avctx, AV_LOG_DEBUG, "FrameMetadataLength=%llu\n", video->FrameMetadataLength);
        }
        
        if (video->Type ==  OMTFrameType_Audio)
        {
                av_log(avctx, AV_LOG_DEBUG, "AUDIO FRAME:\n");
                av_log(avctx, AV_LOG_DEBUG, "Timestamp=%llu\n", video->Timestamp);
                av_log(avctx, AV_LOG_DEBUG, "Codec=%d\n", video->Codec);
                av_log(avctx, AV_LOG_DEBUG, "Flags=%d\n", video->Flags);
                av_log(avctx, AV_LOG_DEBUG, "SampleRate=%d\n", video->SampleRate);
                av_log(avctx, AV_LOG_DEBUG, "Channels=%d\n", video->Channels);
                av_log(avctx, AV_LOG_DEBUG, "SamplesPerChannel=%d\n", video->SamplesPerChannel);
                av_log(avctx, AV_LOG_DEBUG, "Data=%llu\n", video->Data);
                av_log(avctx, AV_LOG_DEBUG, "DataLength=%d\n", video->DataLength);
                av_log(avctx, AV_LOG_DEBUG, "CompressedData=%llu\n", video->CompressedData);
                av_log(avctx, AV_LOG_DEBUG, "CompressedLength=%llu\n", video->CompressedLength);
                av_log(avctx, AV_LOG_DEBUG, "FrameMetadata=%llu\n", video->FrameMetadata);
                av_log(avctx, AV_LOG_DEBUG, "FrameMetadataLength=%llu\n", video->FrameMetadataLength);
        }
    }
	return 0;
}


static int omt_write_video_packet(AVFormatContext *avctx, AVStream *st, AVPacket *pkt)
{

    av_log(avctx, AV_LOG_DEBUG, "omt_write_video_packet START.\n");

    struct OMTContext *ctx = avctx->priv_data;
    AVFrame *avframe, *tmp = (AVFrame *)pkt->data;


// init the frame

 // fill in everything every time
    switch(tmp->format) 
    {
        case AV_PIX_FMT_UYVY422:
            ctx->video.Codec = OMTCodec_UYVY;
        break;
        case AV_PIX_FMT_BGRA:
            ctx->video.Codec = OMTCodec_BGRA;
        break;
     }

    ctx->video.Width = tmp->width;
    ctx->video.Height = tmp->height;
    ctx->video.FrameRateN = st->avg_frame_rate.num;
    ctx->video.FrameRateD = st->avg_frame_rate.den;
    
    if (st->codecpar->field_order != AV_FIELD_PROGRESSIVE)
    {
  	   ctx->video.Flags = OMTVideoFlags_Interlaced;
    }
    
    if (st->sample_aspect_ratio.num)
     {
        AVRational display_aspect_ratio;
        av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                  st->codecpar->width  * (int64_t)st->sample_aspect_ratio.num,
                  st->codecpar->height * (int64_t)st->sample_aspect_ratio.den,
                  1024 * 1024);
        ctx->video.AspectRatio = av_q2d(display_aspect_ratio);
    }
    else
        ctx->video.AspectRatio = (double)st->codecpar->width/st->codecpar->height;

	ctx->video.ColorSpace = OMTColorSpace_BT709;
	ctx->video.CompressedData = NULL;
	ctx->video.CompressedLength = 0;
	ctx->video.FrameMetadata = NULL;
	ctx->video.FrameMetadataLength =0 ;
	
     if (tmp->format != AV_PIX_FMT_UYVY422 && tmp->format != AV_PIX_FMT_BGRA) {
        av_log(avctx, AV_LOG_ERROR, "Got a frame with invalid pixel format.\n");
        return AVERROR(EINVAL);
    }

    if (tmp->linesize[0] < 0) {
        av_log(avctx, AV_LOG_ERROR, "Got a frame with negative linesize.\n");
        return AVERROR(EINVAL);
    }

    if (tmp->width  != ctx->video.Width ||
        tmp->height != ctx->video.Height) {
        av_log(avctx, AV_LOG_ERROR, "Got a frame with invalid dimension.\n");
        av_log(avctx, AV_LOG_ERROR, "tmp->width=%d, tmp->height=%d, ctx->video.Width=%d, ctx->video.Height=%d\n",
            tmp->width, tmp->height, ctx->video.Width, ctx->video.Height);
        return AVERROR(EINVAL);
    }

    avframe = av_frame_clone(tmp);
    if (!avframe)
        return AVERROR(ENOMEM);

    ctx->video.Timestamp = av_rescale_q(pkt->pts, st->time_base, OMT_TIME_BASE_Q);
	ctx->video.Type = OMTFrameType_Video;
    ctx->video.Stride = avframe->linesize[0];
    ctx->video.DataLength = ctx->video.Stride * ctx->video.Height;
    
	#if kCacheBuffer 
		memcpy(ctx->submittedVideoData[ctx->whichBuff], (void *)(avframe->data[0]), ctx->video.DataLength);
		ctx->video.Data = ctx->submittedVideoData[ctx->whichBuff]; 
		ctx->whichBuff = !ctx->whichBuff;
	#else
		 ctx->video.Data = (void *)(avframe->data[0]); 
	#endif
	
    av_log(avctx, AV_LOG_DEBUG, "%s: pkt->pts=%"PRId64", timecode=%"PRId64", st->time_base=%d/%d\n",
        __func__, pkt->pts, ctx->video.Timestamp, st->time_base.num, st->time_base.den);

	// do we need to be doing any clocking here OMT can do it if we set Timecode to -1 ???
	if (ctx->clock_output)
	{
		ctx->video.Timestamp = -1;
	}
	       
	av_log(avctx, AV_LOG_DEBUG, "omt_send \n");
	
	dumpOMTMediaFrameInfo(avctx,&ctx->video);
 	omt_send(ctx->omt_send, &ctx->video);
	
    av_frame_free(&ctx->last_avframe);
    ctx->last_avframe = avframe;
  
    return 0;
}


// Convert interleaved int16_t (in a uint8_t buffer) to planar float (per-channel contiguous).
// 'interleaveShortData' is interleaved shorts (uint8_t * pointing to int16_t data).
// 'planarFloatData' is planar float (per channel, NSamples per channel).
// Returns number of output floats written (should be sourceChannels*sourceSamplesPerChannel).
static int convertInterleavedShortsToPlanarFloat(uint8_t *interleaveShortData,
                                                   int sourceChannels,
                                                   int sourceSamplesPerChannel,
                                                   float *planarFloatData,
                                                   float referenceLevel)
{
    const int16_t *in = (const int16_t *)interleaveShortData;
    int totalSamples = sourceChannels * sourceSamplesPerChannel;
    for (int sampleIdx = 0; sampleIdx < sourceSamplesPerChannel; ++sampleIdx) {
        for (int ch = 0; ch < sourceChannels; ++ch) {
            // Interleaved order: [ch0sam0][ch1sam0]...[chNsam0][ch0sam1]...
            int srcIdx = sampleIdx * sourceChannels + ch;
            int16_t sample = in[srcIdx];
            // Convert int16 sample to float in [-1,1],
            // then rescale by referenceLevel. (referenceLevel==1.0f: full range.)
            float val = (float)sample / 32767.0f * referenceLevel;
            // Planar output index
            int dstIdx = ch * sourceSamplesPerChannel + sampleIdx;
            planarFloatData[dstIdx] = val;
        }
    }
    return totalSamples; // number of floats written
}


static int omt_write_audio_packet(AVFormatContext *avctx, AVStream *st, AVPacket *pkt)
{
    struct OMTContext *ctx = avctx->priv_data;
	ctx->audio.Type = OMTFrameType_Audio;
    ctx->audio.Timestamp = av_rescale_q(pkt->pts, st->time_base, OMT_TIME_BASE_Q);
    ctx->audio.SamplesPerChannel = pkt->size / (ctx->audio.Channels << 1);
    ctx->audio.DataLength = sizeof(float) * convertInterleavedShortsToPlanarFloat((uint8_t *)pkt->data, ctx->audio.Channels, ctx->audio.SamplesPerChannel, ctx->floataudio,ctx->reference_level);
    ctx->audio.Data = (short *)ctx->floataudio;

    av_log(avctx, AV_LOG_DEBUG, "%s: pkt->pts=%"PRId64", timecode=%"PRId64", st->time_base=%d/%d\n",
        __func__, pkt->pts, ctx->audio.Timestamp, st->time_base.num, st->time_base.den);

	// do we need to be doing any clocking here OMT can do it if we set Timecode to -1 ???
	if (ctx->clock_output)
	{
		 ctx->audio.Timestamp = -1;
	}
	omt_send(ctx->omt_send,&ctx->audio);

    return 0;
}

static int omt_write_packet(AVFormatContext *avctx, AVPacket *pkt)
{

 	av_log(avctx, AV_LOG_DEBUG, "omt_write_packet.\n");


    AVStream *st = avctx->streams[pkt->stream_index];

    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        return omt_write_video_packet(avctx, st, pkt);
    else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        return omt_write_audio_packet(avctx, st, pkt);

    return AVERROR_BUG;
}


static int count_channels_from_mask(const AVChannelLayout *ch_layout) {
    int count = 0;
    uint64_t mask = ch_layout->u.mask;

    while (mask) {
        if (mask & 1)
            count++;
        mask >>= 1;
    }
    return count;
}



static int omt_setup_audio(AVFormatContext *avctx, AVStream *st)
{

	av_log(avctx, AV_LOG_DEBUG, "omt_setup_audio.\n");
	
	
    struct OMTContext *ctx = avctx->priv_data;
    AVCodecParameters *c = st->codecpar;

    if (ctx->audio.Type != 0) {
        av_log(avctx, AV_LOG_ERROR, "Only one audio stream is supported!\n");
        return AVERROR(EINVAL);
    }

	memset(&ctx->audio,0,sizeof(ctx->audio));

    ctx->audio.SampleRate = c->sample_rate;
    ctx->audio.Channels = count_channels_from_mask(&c->ch_layout);

	// buffer for conversion from int to float
	ctx->floataudio = (float *)malloc(6144000); // 1/2 second at 32ch floats

	ctx->audio.CompressedData = NULL;
	ctx->audio.CompressedLength = 0;
	ctx->audio.FrameMetadata = NULL;
	ctx->audio.FrameMetadataLength =0 ;
	
    avpriv_set_pts_info(st, 64, 1, OMT_TIME_BASE);

	av_log(avctx, AV_LOG_DEBUG, "omt_setup_audio completed\n");

    return 0;
}

static int omt_setup_video(AVFormatContext *avctx, AVStream *st)
{
	av_log(avctx, AV_LOG_DEBUG, "omt_setup_video avctx->priv_data=%llu\n", avctx->priv_data);

    struct OMTContext *ctx = avctx->priv_data;
    AVCodecParameters *c = st->codecpar;

    if (ctx->video.Type != 0) 
    {
        av_log(avctx, AV_LOG_ERROR, "Only one video stream is supported!\n");
        return AVERROR(EINVAL);
    }

    if (c->codec_id != AV_CODEC_ID_WRAPPED_AVFRAME) 
    {
        av_log(avctx, AV_LOG_ERROR, "Unsupported codec format!"
               " Only AV_CODEC_ID_WRAPPED_AVFRAME is supported (-vcodec wrapped_avframe).\n");
        return AVERROR(EINVAL);
    }

    if (c->format != AV_PIX_FMT_UYVY422 && c->format != AV_PIX_FMT_BGRA) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported pixel format!"
               " Only AV_PIX_FMT_UYVY422, AV_PIX_FMT_BGRA,  is supported.\n");
        return AVERROR(EINVAL);
    }

    if (c->field_order == AV_FIELD_BB || c->field_order == AV_FIELD_BT) 
    {
        av_log(avctx, AV_LOG_ERROR, "Lower field-first disallowed");
        return AVERROR(EINVAL);
    }

	memset(&ctx->video,0,sizeof(ctx->video));

#if kCacheBuffer 
	ctx->submittedVideoData[0] = malloc(132710400);// 8k RGB
	ctx->submittedVideoData[1] = malloc(132710400);// 8k RGB
	ctx->whichBuff = 0;   
#endif
    
    switch(c->format) {
        case AV_PIX_FMT_UYVY422:
            ctx->video.Codec = OMTCodec_UYVY;
        break;
        case AV_PIX_FMT_BGRA:
            ctx->video.Codec = OMTCodec_BGRA;
        break;
    }

    ctx->video.Width = c->width;
    ctx->video.Height = c->height;
    ctx->video.FrameRateN = st->avg_frame_rate.num;
    ctx->video.FrameRateD = st->avg_frame_rate.den;
    
    if (c->field_order != AV_FIELD_PROGRESSIVE)
    {
  	   ctx->video.Flags = OMTVideoFlags_Interlaced;
    }
 
    if (st->sample_aspect_ratio.num)
     {
        AVRational display_aspect_ratio;
        av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                  st->codecpar->width  * (int64_t)st->sample_aspect_ratio.num,
                  st->codecpar->height * (int64_t)st->sample_aspect_ratio.den,
                  1024 * 1024);
        ctx->video.AspectRatio = av_q2d(display_aspect_ratio);
    }
    else
        ctx->video.AspectRatio = (double)st->codecpar->width/st->codecpar->height;


	ctx->video.ColorSpace = OMTColorSpace_BT709;

	ctx->video.CompressedData = NULL;
	ctx->video.CompressedLength = 0;
	ctx->video.FrameMetadata = NULL;
	ctx->video.FrameMetadataLength = 0 ;
	
    avpriv_set_pts_info(st, 64, 1, OMT_TIME_BASE);

	av_log(avctx, AV_LOG_DEBUG, "omt_setup_video completed\n");

    return 0;
}

static int omt_write_header(AVFormatContext *avctx)
{
    int ret = 0;
    unsigned int n;
    struct OMTContext *ctx = avctx->priv_data;
    
 	av_log(avctx, AV_LOG_DEBUG, "omt_write_header.\n");
 
    /* check if streams compatible */
    for (n = 0; n < avctx->nb_streams; n++) 
    {
        AVStream *st = avctx->streams[n];
        AVCodecParameters *c = st->codecpar;
        if (c->codec_type == AVMEDIA_TYPE_AUDIO) 
        {
         

            if ((ret = omt_setup_audio(avctx, st)))
                goto error;
        }
        else if (c->codec_type == AVMEDIA_TYPE_VIDEO) 
        {
            if ((ret = omt_setup_video(avctx, st)))
                goto error;
        } 
        else 
        {
            av_log(avctx, AV_LOG_ERROR, "Unsupported stream type.\n");
            ret = AVERROR(EINVAL);
            goto error;
        }
    }

    av_log(avctx, AV_LOG_DEBUG, "calling omt_send_create....\n");
    ctx->omt_send = omt_send_create(avctx->url, OMTQuality_High);
    if (!ctx->omt_send) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create OMT output %s\n", avctx->url);
        ret = AVERROR_EXTERNAL;
    }

error:

	av_log(avctx, AV_LOG_DEBUG, "omt_write_header completed\n");


    return ret;
}

#define OFFSET(x) offsetof(struct OMTContext, x)
static const AVOption options[] = {
	{ "reference_level", "The audio reference level as floating point", OFFSET(reference_level), AV_OPT_TYPE_FLOAT, { .dbl = 1.0 }, 0.0, 20.0, AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM },
    { "clock_output", "These specify whether the output 'clocks' itself"  , OFFSET(clock_output), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM },
    { NULL },
};




static const AVClass libomt_muxer_class = {
    .class_name = "OMT muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
};

const FFOutputFormat ff_libomt_muxer = {
    .p.name           = "libomt",                      // short muxer name, no _muxer suffix
    .p.long_name      = NULL_IF_CONFIG_SMALL("OpenMediaTransport (OMT) output"),
    .p.audio_codec    = AV_CODEC_ID_PCM_S16LE,
    .p.video_codec    = AV_CODEC_ID_WRAPPED_AVFRAME,
    .p.subtitle_codec = AV_CODEC_ID_NONE,
    .p.flags          = AVFMT_NOFILE,
    .priv_data_size = sizeof(OMTContext),
    .p.priv_class     = &libomt_muxer_class,
    .write_header   = omt_write_header,
    .write_packet   = omt_write_packet,
    .write_trailer  = omt_write_trailer,
};


