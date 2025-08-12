/*
 * libOMT muxer
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

#include <unistd.h>

#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"   
#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "libavutil/channel_layout.h"
#include "libavutil/internal.h"
#include "libavutil/frame.h"
#include "libavutil/internal.h"
#include "libavutil/time.h"
#include "libavformat/internal.h"
#include "libavformat/mux.h"
#include "avdevice.h"
#include "libavdevice/version.h"

#include "libomt_common.h"


struct OMTContext {
    const AVClass *class;  // MUST be first field for AVOptions!
 //   void *ctx;
    float reference_level;
    int clock_output;
    //int tenbit;
    OMTMediaFrame video; 
    OMTMediaFrame audio;
    float * floataudio;
    omt_send_t * omt_send;
    uint8_t * uyvyflip[2];
    int whichFlipBuff;
    struct AVFrame *last_avframe;
};



static int omt_write_trailer(AVFormatContext *avctx)
{
     av_log(avctx, AV_LOG_DEBUG, "omt_write_trailer.\n");

    struct OMTContext *ctx = (struct OMTContext *)avctx->priv_data;
    
    if (ctx->omt_send) {
        omt_send_destroy(ctx->omt_send);
        if(ctx->last_avframe)
            av_frame_free(&ctx->last_avframe);
    }
    
    if (ctx->floataudio) {
        av_free(ctx->floataudio);
        ctx->floataudio = 0;
    }
 
    if (ctx->uyvyflip[0]) {
        av_free(ctx->uyvyflip[0]);
        ctx->uyvyflip[0] = 0;
    }
    if (ctx->uyvyflip[1]) {
        av_free(ctx->uyvyflip[1]);
        ctx->uyvyflip[1] = 0;
    }
 
    return 0;
}


static int dumpOMTMediaFrameInfo(AVFormatContext *avctx,OMTMediaFrame * video)
{
    av_log(avctx, AV_LOG_DEBUG, "dumpOMTMediaFrameInfo OMTMediaFrame = %llu\n",(unsigned long long)video);
    if (video)  {
        if (video->Type == OMTFrameType_Video)  {
                av_log(avctx, AV_LOG_DEBUG, "VIDEO FRAME:\n");
                av_log(avctx, AV_LOG_DEBUG, "Timestamp=%llu\n",(unsigned long long) video->Timestamp);
                av_log(avctx, AV_LOG_DEBUG, "Codec=%d\n", video->Codec);
                av_log(avctx, AV_LOG_DEBUG, "Width=%d\n", video->Width);
                av_log(avctx, AV_LOG_DEBUG, "Height=%d\n", video->Height);
                av_log(avctx, AV_LOG_DEBUG, "Stride=%d\n", video->Stride);
                av_log(avctx, AV_LOG_DEBUG, "Flags=%d\n", video->Flags);
                av_log(avctx, AV_LOG_DEBUG, "FrameRateN=%d\n", video->FrameRateN);
                av_log(avctx, AV_LOG_DEBUG, "FrameRateD=%d\n", video->FrameRateD);
                av_log(avctx, AV_LOG_DEBUG, "AspectRatio=%.2f\n", video->AspectRatio);
                av_log(avctx, AV_LOG_DEBUG, "ColorSpace=%d\n", video->ColorSpace);
                av_log(avctx, AV_LOG_DEBUG, "Data=%llu\n", (unsigned long long)video->Data);
                av_log(avctx, AV_LOG_DEBUG, "DataLength=%d\n", video->DataLength);
                av_log(avctx, AV_LOG_DEBUG, "CompressedData=%llu\n",(unsigned long long) video->CompressedData);
                av_log(avctx, AV_LOG_DEBUG, "CompressedLength=%llu\n", (unsigned long long)video->CompressedLength);
                av_log(avctx, AV_LOG_DEBUG, "FrameMetadata=%llu\n", (unsigned long long)video->FrameMetadata);
                av_log(avctx, AV_LOG_DEBUG, "FrameMetadataLength=%llu\n", (unsigned long long)video->FrameMetadataLength);
        }
        
        if (video->Type ==  OMTFrameType_Audio) {
                av_log(avctx, AV_LOG_DEBUG, "AUDIO FRAME:\n");
                av_log(avctx, AV_LOG_DEBUG, "Timestamp=%llu\n", (unsigned long long)video->Timestamp);
                av_log(avctx, AV_LOG_DEBUG, "Codec=%d\n", video->Codec);
                av_log(avctx, AV_LOG_DEBUG, "Flags=%d\n", video->Flags);
                av_log(avctx, AV_LOG_DEBUG, "SampleRate=%d\n", video->SampleRate);
                av_log(avctx, AV_LOG_DEBUG, "Channels=%d\n", video->Channels);
                av_log(avctx, AV_LOG_DEBUG, "SamplesPerChannel=%d\n", video->SamplesPerChannel);
                av_log(avctx, AV_LOG_DEBUG, "Data=%llu\n", (unsigned long long)video->Data);
                av_log(avctx, AV_LOG_DEBUG, "DataLength=%d\n", video->DataLength);
                av_log(avctx, AV_LOG_DEBUG, "CompressedData=%llu\n", (unsigned long long)video->CompressedData);
                av_log(avctx, AV_LOG_DEBUG, "CompressedLength=%llu\n",(unsigned long long) video->CompressedLength);
                av_log(avctx, AV_LOG_DEBUG, "FrameMetadata=%llu\n", (unsigned long long)video->FrameMetadata);
                av_log(avctx, AV_LOG_DEBUG, "FrameMetadataLength=%llu\n", (unsigned long long)video->FrameMetadataLength);
        }
    }
    return 0;
}


static void convert_yuv422p10le_to_p216_PAD 
(
    uint16_t* src_y, int linesizeY, uint16_t* src_cb,  int linesizeU, uint16_t* src_cr, int linesizeV,
    uint16_t* dst_p216,  int linesizeP216,
    int width, int height)
    {
        uint16_t* dst_p216local = dst_p216;
        uint16_t* localY = src_y;
        uint16_t* localCB = src_cb;
        uint16_t* localCR = src_cr;
        uint16_t* dst_p216UVlocal =  (uint16_t*)((uint8_t*)dst_p216 + (height * linesizeY));  
            
        // is yuv422p10le 10bit packed ?  need to use 40 bit blocks (5 bytes for 4 pixels)
        for (int y = 0; y < height >> 1 ; y++) {
            for (int x = 0; x < width *2; x += 2) {
                // Load 2 pixels worth of YUV422P10LE data
                // Pack the data into the P216 format
                 dst_p216local[x] = localY[x] << 6;
                 dst_p216local[x + 1] = localY[x + 1]<< 6;
                 dst_p216UVlocal[x] = localCB[x / 2] << 6 ;
                 dst_p216UVlocal[x + 1] = localCR[x / 2] << 6;
            }

            // Advance the pointers in bytes for each line
            localY += linesizeY;
            localCB += linesizeU;
            localCR += linesizeV;
            dst_p216local += linesizeP216;
            dst_p216UVlocal += linesizeP216;
        } 
        return;   
    }
    

static int omt_write_video_packet(AVFormatContext *avctx, AVStream *st, AVPacket *pkt)
{
    av_log(avctx, AV_LOG_DEBUG, "omt_write_video_packet START.\n");
    
    int frameIsTenBitPlanar = 0;
    struct OMTContext *ctx = (struct OMTContext *)avctx->priv_data;

    if (st->codecpar->codec_id == AV_CODEC_ID_VMIX) {
        ctx->video.Codec = OMTCodec_VMX1;
        ctx->video.Width = st->codecpar->width;
        ctx->video.Height = st->codecpar->height;
        ctx->video.FrameRateN = st->avg_frame_rate.num;
        ctx->video.FrameRateD = st->avg_frame_rate.den;
        if (st->codecpar->field_order != AV_FIELD_PROGRESSIVE)  
            ctx->video.Flags = OMTVideoFlags_Interlaced;
        else
             ctx->video.Flags = OMTVideoFlags_None;
        
        if (st->sample_aspect_ratio.num) {
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
        ctx->video.FrameMetadata = NULL;
        ctx->video.FrameMetadataLength = 0 ;
        ctx->video.Timestamp = av_rescale_q(pkt->pts, st->time_base, OMT_TIME_BASE_Q);
        ctx->video.Type = OMTFrameType_Video;
        ctx->video.Codec = OMTCodec_VMX1;
        ctx->video.Stride = 0; 
        ctx->video.Data = pkt->data;
        ctx->video.DataLength = pkt->size;
        ctx->video.CompressedData = NULL;
        ctx->video.CompressedLength = 0;
 
        av_log(avctx, AV_LOG_DEBUG, "%s: pkt->pts=%"PRId64", timecode=%"PRId64", st->time_base=%d/%d\n",
            __func__, pkt->pts, ctx->video.Timestamp, st->time_base.num, st->time_base.den);


        if (ctx->clock_output == 1) 
            ctx->video.Timestamp = -1;
      
           
        av_log(avctx, AV_LOG_DEBUG, "omt_send native\n");
    
        dumpOMTMediaFrameInfo(avctx,&ctx->video);
        omt_send(ctx->omt_send, &ctx->video);
    
        ctx->last_avframe = NULL;
        
        av_log(avctx, AV_LOG_DEBUG, "Compressed Data SENT %d bytes\n", ctx->video.CompressedLength);
        for (int i=0;i<64;i++) {
            av_log(avctx, AV_LOG_DEBUG, "%02x ",((uint8_t*)(ctx->video.Data))[i]);
        }
        av_log(avctx, AV_LOG_DEBUG, "\n");

    }
    else  {
        AVFrame *avframe, *tmp = (AVFrame *)pkt->data;
        switch(tmp->format) 
        {
            case AV_PIX_FMT_YUV422P10LE:
                ctx->video.Codec = OMTCodec_P216;
                frameIsTenBitPlanar = 1;
            break;
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
           ctx->video.Flags = OMTVideoFlags_Interlaced;
      
    
        if (st->sample_aspect_ratio.num) {
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
        
         if (tmp->format != AV_PIX_FMT_UYVY422 && tmp->format != AV_PIX_FMT_BGRA && tmp->format !=AV_PIX_FMT_YUV422P10LE) {
            av_log(avctx, AV_LOG_ERROR, "Got a frame with invalid pixel format.\n");
            return AVERROR(EINVAL);
         }
    
         if (tmp->linesize[0] < 0) {
            av_log(avctx, AV_LOG_ERROR, "Got a frame with negative linesize.\n");
            return AVERROR(EINVAL);
         }
        

        if (tmp->width  != ctx->video.Width || tmp->height != ctx->video.Height) {
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
        ctx->video.Stride = avframe->linesize[0]; // is this still correct with P216 ?
        ctx->video.DataLength = ctx->video.Stride * ctx->video.Height;    
            
        if (frameIsTenBitPlanar) {
            convert_yuv422p10le_to_p216_PAD ((uint16_t*)avframe->data[0],avframe->linesize[0], (uint16_t*)avframe->data[1],avframe->linesize[1], ( uint16_t*)avframe->data[2], avframe->linesize[2],
            (uint16_t*)(ctx->uyvyflip[ctx->whichFlipBuff]),ctx->video.Stride,ctx->video.Width, ctx->video.Height);
            ctx->video.Data  = (void *)ctx->uyvyflip[ctx->whichFlipBuff];
            ctx->whichFlipBuff = !ctx->whichFlipBuff;
        }
        else
             ctx->video.Data = (void *)(avframe->data[0]); 
    
        av_log(avctx, AV_LOG_DEBUG, "%s: pkt->pts=%"PRId64", timecode=%"PRId64", st->time_base=%d/%d\n",
            __func__, pkt->pts, ctx->video.Timestamp, st->time_base.num, st->time_base.den);

        if (ctx->clock_output == 1)
            ctx->video.Timestamp = -1;
           
        av_log(avctx, AV_LOG_DEBUG, "omt_send \n");
    
        dumpOMTMediaFrameInfo(avctx,&ctx->video);
        omt_send(ctx->omt_send, &ctx->video);
    
        av_frame_free(&ctx->last_avframe);
        ctx->last_avframe = avframe;
    }
    return 0;
}

/*
Convert interleaved int16_t (in a uint8_t buffer) to planar float (per-channel contiguous).
'interleaveShortData' is interleaved shorts (uint8_t * pointing to int16_t data).
'planarFloatData' is planar float (per channel, NSamples per channel).
Returns number of output floats written (should be sourceChannels*sourceSamplesPerChannel).
*/
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
    struct OMTContext *ctx = (struct OMTContext *)avctx->priv_data;
    ctx->audio.Type = OMTFrameType_Audio;
    ctx->audio.Timestamp = av_rescale_q(pkt->pts, st->time_base, OMT_TIME_BASE_Q);
    ctx->audio.SamplesPerChannel = pkt->size / (ctx->audio.Channels << 1);
    ctx->audio.DataLength = sizeof(float) * convertInterleavedShortsToPlanarFloat((uint8_t *)pkt->data, ctx->audio.Channels, ctx->audio.SamplesPerChannel, ctx->floataudio,ctx->reference_level);
    ctx->audio.Data = (short *)ctx->floataudio;

    av_log(avctx, AV_LOG_DEBUG, "%s: pkt->pts=%"PRId64", timecode=%"PRId64", st->time_base=%d/%d\n",
        __func__, pkt->pts, ctx->audio.Timestamp, st->time_base.num, st->time_base.den);

    if (ctx->clock_output == 1)
         ctx->audio.Timestamp = -1;

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
    
    struct OMTContext *ctx = (struct OMTContext *)avctx->priv_data;
    AVCodecParameters *c = st->codecpar;

    if (ctx->audio.Type != 0) {
        av_log(avctx, AV_LOG_ERROR, "Only one audio stream is supported!\n");
        return AVERROR(EINVAL);
    }

    memset(&ctx->audio,0,sizeof(ctx->audio));

    ctx->audio.SampleRate = c->sample_rate;
    ctx->audio.Channels = count_channels_from_mask(&c->ch_layout);

    // buffer for conversion from int to float
    ctx->floataudio = (float *)av_malloc(6144000); // 1/2 second at 32ch floats

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
    av_log(avctx, AV_LOG_DEBUG, "omt_setup_video avctx->priv_data=%llu\n", (unsigned long long) avctx->priv_data);

    struct OMTContext *ctx = (struct OMTContext *)avctx->priv_data;
    AVCodecParameters *c = st->codecpar;

    if (ctx->video.Type != 0) {
        av_log(avctx, AV_LOG_ERROR, "Only one video stream is supported!\n");
        return AVERROR(EINVAL);
    }
    
    if (c->codec_id == AV_CODEC_ID_VMIX) {
        // native VMX pass through
        av_log(avctx, AV_LOG_DEBUG, "native VMX pass arrived OMT encoder\n");
        if (c->format == AV_PIX_FMT_YUV422P)
            av_log(avctx, AV_LOG_DEBUG, "AV_PIX_FMT_YUV422P format VMX\n");
    }
    else
    {
        if (c->codec_id != AV_CODEC_ID_WRAPPED_AVFRAME) {
            av_log(avctx, AV_LOG_ERROR, "Unsupported codec format! (%d)"
                   " Only AV_CODEC_ID_WRAPPED_AVFRAME is supported (-vcodec wrapped_avframe).\n",c->codec_id);
            return AVERROR(EINVAL);
        }
        
        if (c->format != AV_PIX_FMT_UYVY422 && c->format != AV_PIX_FMT_BGRA && c->format != AV_PIX_FMT_YUV422P10LE) {
                av_log(avctx, AV_LOG_ERROR, "Unsupported pixel format! (%d)"
               " Only AV_PIX_FMT_UYVY422, AV_PIX_FMT_BGRA, AV_PIX_FMT_YUV422P10LE is supported.\n",c->format);
                return AVERROR(EINVAL);
        }
    }

    if (c->field_order == AV_FIELD_BB || c->field_order == AV_FIELD_BT) {
        av_log(avctx, AV_LOG_ERROR, "Lower field-first disallowed");
        return AVERROR(EINVAL);
    }

    memset(&ctx->video,0,sizeof(ctx->video));

    switch(c->format) 
    {
        case  AV_PIX_FMT_YUV422P:
            ctx->video.Codec = OMTCodec_UYVY;
        break;
            
        case AV_PIX_FMT_YUV422P10LE:
            ctx->video.Codec = OMTCodec_P216;
            ctx->uyvyflip[0] = (uint8_t*)av_malloc(c->width*c->height*8); // in theory 4 should be enough
            ctx->uyvyflip[1] = (uint8_t*)av_malloc(c->width*c->height*8);
            ctx->whichFlipBuff = 0;
        break;
        
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
         ctx->video.Flags = OMTVideoFlags_Interlaced;

 
    if (st->sample_aspect_ratio.num)  {
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
    struct OMTContext *ctx = (struct OMTContext *)avctx->priv_data;    
    
    av_log(avctx, AV_LOG_DEBUG, "omt_write_header.\n");
 
    /* check if streams compatible */
    for (n = 0; n < avctx->nb_streams; n++) {
        AVStream *st = avctx->streams[n];
        AVCodecParameters *c = st->codecpar;
        if (c->codec_type == AVMEDIA_TYPE_AUDIO) {
            if ((ret = omt_setup_audio(avctx, st)))
                goto error;
        }
        else if (c->codec_type == AVMEDIA_TYPE_VIDEO) {
            if ((ret = omt_setup_video(avctx, st)))
                goto error;
        } 
        else {
            av_log(avctx, AV_LOG_ERROR, "Unsupported stream type.\n");
            ret = AVERROR(EINVAL);
            goto error;
        }
    }

    av_log(avctx, AV_LOG_DEBUG, "calling omt_send_create....\n");
    ctx->omt_send = omt_send_create(avctx->url, OMTQuality_Default);
    if (!ctx->omt_send) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create OMT output %s\n", avctx->url);
        ret = AVERROR_EXTERNAL;
    }
    
     av_log(avctx, AV_LOG_DEBUG, "libomt reference_level = %.2f clock_output = %d\n",ctx->reference_level,ctx->clock_output);


error:

    av_log(avctx, AV_LOG_DEBUG, "omt_write_header completed\n");

    return ret;
}

#define OFFSET(x) offsetof(struct OMTContext, x)
static const AVOption options[] = {
    { "clock_output", "These specify whether the output 'clocks' itself"  , OFFSET(clock_output), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM },
    { "reference_level", "The audio reference level as floating point full scale deflection", OFFSET(reference_level), AV_OPT_TYPE_FLOAT, { .dbl = 1.0 }, 0.0, 20.0, AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM },
    { NULL },
};


static const AVClass libomt_muxer_class = {
    .class_name = "OMT muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
};


#if LIBAVDEVICE_VERSION_MAJOR >= 62

    const FFOutputFormat ff_libomt_muxer = {
        .p.name           = "libomt",                     
        .p.long_name      = NULL_IF_CONFIG_SMALL("OpenMediaTransport (OMT) output"),
        .p.audio_codec    = AV_CODEC_ID_PCM_S16LE,
        .p.video_codec    = AV_CODEC_ID_WRAPPED_AVFRAME,
        .p.subtitle_codec = AV_CODEC_ID_NONE,
        .p.flags          = AVFMT_NOFILE,
        .priv_data_size   = sizeof(struct OMTContext),
        .p.priv_class     = &libomt_muxer_class,
        .write_header     = omt_write_header,
        .write_packet     = omt_write_packet,
        .write_trailer    = omt_write_trailer,
    };

#else

    const AVOutputFormat ff_libomt_muxer = {
        .name           = "libomt",                     
        .long_name      = NULL_IF_CONFIG_SMALL("OpenMediaTransport (OMT) output"),
        .audio_codec    = AV_CODEC_ID_PCM_S16LE,
        .video_codec    = AV_CODEC_ID_WRAPPED_AVFRAME,
        .subtitle_codec = AV_CODEC_ID_NONE,
        .flags          = AVFMT_NOFILE,
        .priv_data_size = sizeof(struct OMTContext),
        .priv_class     = &libomt_muxer_class,
        .write_header   = omt_write_header,
        .write_packet   = omt_write_packet,
        .write_trailer  = omt_write_trailer,
    };

#endif


