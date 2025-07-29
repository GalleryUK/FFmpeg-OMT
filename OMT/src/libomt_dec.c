/*
 * libOMT  demuxer
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

#include <unistd.h>


typedef  struct OMTContext {
    const AVClass *cclass;

    float reference_level;
    int find_sources;
	omt_receive_t* recv;	

    /* Streams */
    AVStream *video_st, *audio_st;
} OMTContext;


static int omt_set_video_packet(AVFormatContext *avctx, OMTMediaFrame *v, AVPacket *pkt)
{

    av_log(avctx, AV_LOG_DEBUG, "omt_set_video_packet %dx%d stride=%d\n",v->Width,v->Height,v->Stride);

    int ret;
    struct OMTContext *ctx = avctx->priv_data;

    ret = av_new_packet(pkt, v->Height * v->Stride);
    if (ret < 0)
    {
        av_log(avctx, AV_LOG_ERROR, "omt_set_video_packet av_new_packet failed error %d\n",ret);
        return ret;
	}
	
    pkt->dts = pkt->pts = av_rescale_q(v->Timestamp, OMT_TIME_BASE_Q, ctx->video_st->time_base);
    pkt->duration = av_rescale_q(1, (AVRational){v->FrameRateD, v->FrameRateN}, ctx->video_st->time_base);

    av_log(avctx, AV_LOG_DEBUG, "%s: pkt->dts = pkt->pts = %"PRId64", duration=%"PRId64", timecode=%"PRId64"\n",
        __func__, pkt->dts, pkt->duration, v->Timestamp);

    pkt->flags         |= AV_PKT_FLAG_KEY;
    pkt->stream_index   = ctx->video_st->index;
    memcpy(pkt->data, v->Data, pkt->size);
    
        av_log(avctx, AV_LOG_DEBUG, "omt_set_video_packet memcpy %d bytes\n",pkt->size);


    return 0;
}


static int	convertPlanarFloatToInterleavedShorts(float * floatData,int sourceChannels,int sourceSamplesPerChannel,uint8_t * outputData,float referenceLevel)
{
     // Cast output to int16_t for easier assignment
    int16_t *out = (int16_t *)outputData;

    // For each sample across all channels
    for (int sampleIdx = 0; sampleIdx < sourceSamplesPerChannel; ++sampleIdx) {
        for (int ch = 0; ch < sourceChannels; ++ch) {
            // Planar layout: [ch0][ch1][ch2]...
            // Each plane is sourceSamplesPerChannel floats
            float val = floatData[ch * sourceSamplesPerChannel + sampleIdx];
            // Scale by referenceLevel (assumed fullscale is +/-referenceLevel)
            float scaled = val / referenceLevel;
            // Clamp to [-1, 1]
            if (scaled > 1.0f) scaled = 1.0f;
            else if (scaled < -1.0f) scaled = -1.0f;
            // Convert to int16_t range
            int16_t sample = (int16_t)lrintf(scaled * 32767.0f);
            // Interleaved output order
            out[sampleIdx * sourceChannels + ch] = sample;
        }
    }
    // Returns the total number of output bytes written
    return sourceSamplesPerChannel * sourceChannels * (int)sizeof(int16_t);
}


static int omt_set_audio_packet(AVFormatContext *avctx, OMTMediaFrame *a, AVPacket *pkt)
{

    av_log(avctx, AV_LOG_DEBUG, "omt_set_audio_packet \n");


    int ret;
    struct OMTContext *ctx = avctx->priv_data;

  //  OMTlib_audio_frame_interleaved_16s_t dst;

    ret = av_new_packet(pkt, 2 * a->SamplesPerChannel * a->Channels);
    if (ret < 0)
        return ret;

    pkt->dts = pkt->pts = av_rescale_q(a->Timestamp, OMT_TIME_BASE_Q, ctx->audio_st->time_base);
    pkt->duration = av_rescale_q(1, (AVRational){a->SamplesPerChannel, a->SampleRate}, ctx->audio_st->time_base);

    av_log(avctx, AV_LOG_DEBUG, "%s: pkt->dts = pkt->pts = %"PRId64", duration=%"PRId64", timecode=%"PRId64"\n",
        __func__, pkt->dts, pkt->duration, a->Timestamp);

    pkt->flags       |= AV_PKT_FLAG_KEY;
    pkt->stream_index = ctx->audio_st->index;

	convertPlanarFloatToInterleavedShorts(a->Data,a->Channels,a->SamplesPerChannel,pkt->data, 1.0);

    return 0;
}


static int omt_find_sources(AVFormatContext *avctx, const char *name)
{

	int OMTcount = 0;
	char ** omtSources = NULL;
	OMTcount = 0;
	omtSources = omt_discovery_getaddresses(&OMTcount);

	usleep(1000000);
	
	OMTcount = 0;
	omtSources = omt_discovery_getaddresses(&OMTcount);

	if (OMTcount > 0)
	{
		printf("--------------OMT Sources-------------\n");

		int z;
		for (z=0;z<OMTcount > 0;z++)
		{
			printf("%s\n", omtSources[z]);
		}
		
		printf("--------------------------------------\n");

	}
	else
	{
		printf("No OMT Sources found\n");
	}
	

    return 0;
}




static int omt_read_header(AVFormatContext *avctx)
{
//printf("omt_read_header for URL=%s.\n",avctx->url);

    av_log(avctx, AV_LOG_DEBUG, "omt_read_header for URL=%s.\n",avctx->url);

    
    
    

    int ret;
 //   OMTlib_recv_create_t recv_create_desc;
    const OMTTally tally_state = { .program = 1, .preview = 1 };
    struct OMTContext *ctx = avctx->priv_data;
      
      
    if (ctx->find_sources) 
    {
    	omt_find_sources(avctx, avctx->url);
        return AVERROR_EXIT;
    }




    ctx->recv = omt_receive_create(avctx->url, (OMTFrameType)(OMTFrameType_Video | OMTFrameType_Audio | OMTFrameType_Metadata), (OMTPreferredVideoFormat)OMTPreferredVideoFormat_UYVYorBGRA, (OMTReceiveFlags)OMTReceiveFlags_None);
    if (!ctx->recv) {
        av_log(avctx, AV_LOG_ERROR, "omt_receive_create failed.\n");
        return AVERROR(EIO);
    }

    /* Set tally */
    omt_receive_settally(ctx->recv,&tally_state);

    avctx->ctx_flags |= AVFMTCTX_NOHEADER;

    return 0; // success
}


static int omt_create_video_stream(AVFormatContext *avctx, OMTMediaFrame *v)
{

    av_log(avctx, AV_LOG_DEBUG, "omt_create_video_stream \n");


    AVStream *st;
    AVRational tmp;
    struct OMTContext *ctx = avctx->priv_data;

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

    if (OMTCodec_UYVY == v->Codec || OMTCodec_UYVA == v->Codec) 
    {
        st->codecpar->format        = AV_PIX_FMT_UYVY422;
        st->codecpar->codec_tag     = MKTAG('U', 'Y', 'V', 'Y');
        if (OMTCodec_UYVA == v->Codec)
            av_log(avctx, AV_LOG_WARNING, "Alpha channel ignored\n");
    } 
    else if (OMTCodec_BGRA == v->Codec)
    {
        st->codecpar->format        = AV_PIX_FMT_BGRA;
        st->codecpar->codec_tag     = MKTAG('B', 'G', 'R', 'A');
    } 
    else 
    {
        av_log(avctx, AV_LOG_ERROR, "Unsupported video stream format, v->Codec=%d\n", v->Codec);
        return AVERROR(EINVAL);
    }

    avpriv_set_pts_info(st, 64, 1, OMT_TIME_BASE);

    ctx->video_st = st;

    return 0;
}

static int omt_create_audio_stream(AVFormatContext *avctx, OMTMediaFrame *a)
{

    av_log(avctx, AV_LOG_DEBUG, "omt_create_audio_stream \n");

    AVStream *st;
    struct OMTContext *ctx = avctx->priv_data;

    st = avformat_new_stream(avctx, NULL);
    if (!st) {
        av_log(avctx, AV_LOG_ERROR, "Cannot add audio stream\n");
        return AVERROR(ENOMEM);
    }

    st->codecpar->codec_type        = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id          = AV_CODEC_ID_PCM_S16LE;
    st->codecpar->sample_rate       = a->SampleRate;
 //   st->codecpar->channels          = a->Channels;
	av_channel_layout_default(&st->codecpar->ch_layout, a->Channels);


    avpriv_set_pts_info(st, 64, 1, OMT_TIME_BASE);

    ctx->audio_st = st;

    return 0;
}

static int omt_read_packet(AVFormatContext *avctx, AVPacket *pkt)
{

    av_log(avctx, AV_LOG_DEBUG, "omt_read_packet \n");


    int ret = 0;
    struct OMTContext *ctx = avctx->priv_data;

  //  while (!ret) 
    {
        OMTMediaFrame * theOMTFrame;
        OMTFrameType t;

        theOMTFrame = omt_receive(ctx->recv, (OMTFrameType) (OMTFrameType_Video|OMTFrameType_Audio/*|OMTFrameType_Metadata*/), 40);
      	if (theOMTFrame)
      	{
      		t = theOMTFrame->Type;
      	}
      	
		switch (t)
		{
       		case  OMTFrameType_Video:
       		
			     av_log(avctx, AV_LOG_DEBUG, "omt_received video\n");

				if (!ctx->video_st)
					ret = omt_create_video_stream(avctx, theOMTFrame);
				if (!ret)
					ret = omt_set_video_packet(avctx, theOMTFrame, pkt);
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
    };

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
   		{ "find_sources", "Find available sources"  , OFFSET(find_sources), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, DEC },
	   { "reference_level", "The audio reference level as floating point", OFFSET(reference_level), AV_OPT_TYPE_FLOAT, { .dbl = 1.0 }, 0.0, 20.0, DEC },
//     { "find_sources", "Find available sources"  , OFFSET(find_sources), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, DEC },
//     { "wait_sources", "Time to wait until the number of online sources have changed"  , OFFSET(wait_sources), AV_OPT_TYPE_DURATION, { .i64 = 1000000 }, 100000, 20000000, DEC },
//     { "allow_video_fields", "When this flag is FALSE, all video that you receive will be progressive"  , OFFSET(allow_video_fields), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, DEC },
//     { "extra_ips", "List of comma separated ip addresses to scan for remote sources",       OFFSET(extra_ips), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC },
    { NULL },
};



static const AVClass libomt_demuxer_class = {
    .class_name = "OMT demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
};


const FFInputFormat ff_libomt_demuxer = {
    .p.name          = "libomt",
    .p.long_name     = NULL_IF_CONFIG_SMALL("OpenMediaTransport(OMT) input using libomt library"),
    .p.priv_class     = &libomt_demuxer_class,
    .p.flags          = AVFMT_NOFILE,
    .priv_data_size = sizeof(OMTContext),
    .read_header      = omt_read_header,    
    .read_packet      = omt_read_packet,
    .read_close       = omt_read_close,
};



