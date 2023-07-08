#include "ffincludes.h"
#include "defines.h"

void generate_s16(AVFrame *audioFrame, int64_t frameIndex);
void resample_s16_fltp(AVFrame *audioFrameS16, AVFrame *audioFrameFLTP, struct SwrContext *resampleContext);
void rescale_rgb24_yuv444(AVFrame *videoFrameRGB, AVFrame *videoFrameYUV, struct SwsContext *rescaleContext);
void generate_rgb24(AVFrame *videoFrameRGB, int64_t frameIndex);


int main()
{
    AVFormatContext *flvFormatContext;
    ret = avformat_alloc_output_context2(&flvFormatContext, NULL, FORMAT_NAME, RTMP_ADDRESS);
    assert(ret >= 0);

/** audio output */
/************************************/
    AVCodec *audioCodecAAC = const_cast<AVCodec*>(avcodec_find_encoder(AUDIO_CODECID));
    assert(audioCodecAAC);

    AVPacket *audioPacketAAC = av_packet_alloc();
    assert(audioPacketAAC);

    AVChannelLayout src AV_CHANNEL_LAYOUT_STEREO;
    AVCodecContext *audioCodecContextAAC = avcodec_alloc_context3(audioCodecAAC);
    assert(audioCodecContextAAC);
    audioCodecContextAAC->sample_fmt = AUDIO_SMPFMT_OUT;
    audioCodecContextAAC->bit_rate = AUDIO_BITRATE;
    audioCodecContextAAC->sample_rate = AUDIO_SAMPLERATE;
    audioCodecContextAAC->time_base = (AVRational){1,AUDIO_SAMPLERATE};
    av_channel_layout_copy(&audioCodecContextAAC->ch_layout,&src);
    if(flvFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
        audioCodecContextAAC->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    ret = avcodec_open2(audioCodecContextAAC,audioCodecAAC,NULL);
    assert(ret == 0);

    AVFrame *audioFrameS16 = av_frame_alloc();
    assert(audioFrameS16);
    audioFrameS16->format = AUDIO_SMPFMT_IN;
    audioFrameS16->nb_samples = audioCodecContextAAC->frame_size;
    audioFrameS16->sample_rate = AUDIO_SAMPLERATE;
    audioFrameS16->time_base = (AVRational){1,AUDIO_SAMPLERATE};
    av_channel_layout_copy(&audioFrameS16->ch_layout, &src);
    av_frame_get_buffer(audioFrameS16,0);
    assert(ret == 0);

    AVFrame *audioFrameFLTP = av_frame_alloc();
    assert(audioFrameFLTP);
    audioFrameFLTP->format = AUDIO_SMPFMT_OUT;
    audioFrameFLTP->nb_samples = audioCodecContextAAC->frame_size;
    audioFrameFLTP->sample_rate = AUDIO_SAMPLERATE;
    audioFrameFLTP->time_base = (AVRational){1,AUDIO_SAMPLERATE};
    av_channel_layout_copy(&audioFrameFLTP->ch_layout, &src);
    av_frame_get_buffer(audioFrameFLTP,0);
    assert(ret == 0);

    // initialize S16->FLTP resampler
    struct SwrContext *resampler = swr_alloc();
    av_opt_set_chlayout(resampler, "in_chlayout", &audioCodecContextAAC->ch_layout, 0);
    av_opt_set_chlayout(resampler, "out_chlayout", &audioCodecContextAAC->ch_layout, 0);
    av_opt_set_int(resampler, "in_sample_rate", AUDIO_SAMPLERATE, 0);
    av_opt_set_int(resampler, "out_sample_rate", AUDIO_SAMPLERATE, 0);
    av_opt_set_sample_fmt(resampler, "in_sample_fmt", AUDIO_SMPFMT_IN, 0);
    av_opt_set_sample_fmt(resampler, "out_sample_fmt", AUDIO_SMPFMT_OUT, 0);
    ret = swr_init(resampler);
    assert(ret >= 0);

    AVStream *audioStreamAAC = avformat_new_stream(flvFormatContext, NULL);
    assert(audioStreamAAC);
    audioStreamAAC->id = flvFormatContext->nb_streams-1;
    audioStreamAAC->time_base = (AVRational){1,AUDIO_SAMPLERATE};
    ret = avcodec_parameters_from_context(audioStreamAAC->codecpar, audioCodecContextAAC);
    assert(ret >= 0);





/** video output */
/************************************/
    AVCodec *videoCodecH264 = const_cast<AVCodec*>(avcodec_find_encoder(VIDEO_CODECID));
    assert(videoCodecH264);

    AVPacket *videoPacketH264 = av_packet_alloc();
    assert(videoPacketH264);

    AVCodecContext *videoCodecContextH264 = avcodec_alloc_context3(videoCodecH264);
    assert(videoCodecContextH264);
    videoCodecContextH264->bit_rate = VIDEO_BITRATE;
    videoCodecContextH264->width = VIDEO_WIDTH;
    videoCodecContextH264->height = VIDEO_HEIGHT;
    videoCodecContextH264->time_base = (AVRational){1, VIDEO_FRAMERATE};
    // videoCodecContextH264->gop_size = 12;
    videoCodecContextH264->pix_fmt = VIDEO_PIXFMT_OUT;
    if(flvFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
        videoCodecContextH264->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    // ret = avcodec_open2(videoCodecContextH264, videoCodecH264, NULL);
    
    printf("CREATING H264 DICTIONARY\n");
    AVDictionary *videoCodecOptionsH264 = nullptr;
    av_dict_set(&videoCodecOptionsH264, "profile", "high444", 0);
    av_dict_set(&videoCodecOptionsH264, "preset", "superfast", 0);
    av_dict_set(&videoCodecOptionsH264, "tune", "zerolatency", 0);
    ret = avcodec_open2(videoCodecContextH264, videoCodecH264, &videoCodecOptionsH264);
    // assert(ret==0);
    // av_dict_free(&videoCodecOptionsH264);
    printf("CREATED H264 DICTIONARY\n");



    AVFrame *videoFrameRGB24 = av_frame_alloc();
    assert(videoFrameRGB24);
    videoFrameRGB24->format = VIDEO_PIXFMT_IN;
    videoFrameRGB24->width = VIDEO_WIDTH;
    videoFrameRGB24->height = VIDEO_HEIGHT;
    videoFrameRGB24->time_base = (AVRational){1, VIDEO_FRAMERATE};
    ret = av_frame_get_buffer(videoFrameRGB24,0);
    assert(ret == 0);

    AVFrame *videoFrameYUV444 = av_frame_alloc();
    assert(videoFrameYUV444);
    videoFrameYUV444->format = VIDEO_PIXFMT_OUT;
    videoFrameYUV444->width = VIDEO_WIDTH;
    videoFrameYUV444->height = VIDEO_HEIGHT;
    videoFrameYUV444->time_base = (AVRational){1, VIDEO_FRAMERATE};
    ret = av_frame_get_buffer(videoFrameYUV444,0);
    assert(ret == 0);

    // initialize the RGB24-->YUV444 rescaler
    struct SwsContext *rescaler = sws_getContext(
        VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_PIXFMT_IN,
        VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_PIXFMT_OUT,
        VIDEO_SCALEFLAGS, NULL,NULL,NULL
    );

    AVStream *videoStreamH264 = avformat_new_stream(flvFormatContext, NULL);
    assert(videoStreamH264);
    videoStreamH264->id = flvFormatContext->nb_streams-1;
    videoStreamH264->time_base = (AVRational){1, VIDEO_FRAMERATE};
    ret = avcodec_parameters_from_context(videoStreamH264->codecpar, videoCodecContextH264);
    assert(ret >= 0);






    // open the FLV container and write headers
    if(!(flvFormatContext->oformat->flags & AVFMT_NOFILE)){
        ret = avio_open(&flvFormatContext->pb, RTMP_ADDRESS, AVIO_FLAG_WRITE);
        assert(ret >= 0);
    }
    ret = avformat_write_header(flvFormatContext, NULL);
    assert(ret >= 0);

    // print info about each stream in the FLV container
    av_dump_format(flvFormatContext, audioStreamAAC->index, RTMP_ADDRESS, 1);
    av_dump_format(flvFormatContext, videoStreamH264->index, RTMP_ADDRESS, 1);
    
    uint64_t audioPTS = 0;
    uint64_t videoPTS = 0;

    while(1)
    {
        // printf("*");
        generate_s16(audioFrameS16,audioPTS); // no need to maintain PTS for audioFrameS16
        resample_s16_fltp(audioFrameS16,audioFrameFLTP,resampler);
        audioPTS+=audioFrameFLTP->nb_samples;
        audioFrameFLTP->pts = audioPTS;
        
        generate_rgb24(videoFrameRGB24,videoPTS);
        rescale_rgb24_yuv444(videoFrameRGB24,videoFrameYUV444,rescaler);
        videoPTS++;
        videoFrameYUV444->pts = videoPTS;


        ret = avcodec_send_frame(audioCodecContextAAC, audioFrameFLTP);
        PRINTRET;
        ret = avcodec_send_frame(videoCodecContextH264,videoFrameYUV444);
        PRINTRET;
        while(ret >= 0)
        {
            // printf("-");
            ret = avcodec_receive_packet(videoCodecContextH264,videoPacketH264);
            BREAK; // this is crucial to prevent empty H264 packets from being sent to the FLV container

            ret = avcodec_receive_packet(audioCodecContextAAC,audioPacketAAC);
            BREAK; // this is crucial to prevent empty AAC packets from being sent to the FLV container

            // PRINTRET;
            av_packet_rescale_ts(audioPacketAAC, audioCodecContextAAC->time_base, audioStreamAAC->time_base);
            audioPacketAAC->stream_index = audioStreamAAC->index;
            
            av_packet_rescale_ts(videoPacketH264,videoCodecContextH264->time_base, videoStreamH264->time_base);
            videoPacketH264->stream_index = videoStreamH264->index;
            
            ret = av_interleaved_write_frame(flvFormatContext,audioPacketAAC);
            ret = av_interleaved_write_frame(flvFormatContext,videoPacketH264);
        }
    }
}



















float t     = 0;
float tincr = 2 * M_PI * 110.0 / 44100;
float tincr2 = 2 * M_PI * 110.0 / 44100/44100;
void generate_s16(AVFrame *audioFrame, int64_t frameIndex)
{
    int i,j,val;
    int16_t *q = (int16_t*)audioFrame->data[0];
    uint32_t nSamples = audioFrame->nb_samples;
    uint32_t nChannels = audioFrame->ch_layout.nb_channels;

    for(i = 0; i < nSamples; i++)
    {
        for (j = 0; j < nChannels; j++)
        {
            val = (int16_t)(sin(t)*10000);
            *q++ = val;
        }
        t       += tincr;
        tincr   += tincr2;
    }
}

// generate dummy RGB24 buffer
void generate_rgb24(AVFrame *videoFrameRGB, int64_t frameIndex) 
{
    uint8_t* frameData = videoFrameRGB->data[0];
    int frameLinesize = videoFrameRGB->linesize[0];
    for (int y = 0; y < videoFrameRGB->height; ++y) {
        for (int x = 0; x < videoFrameRGB->width; ++x) {
            int offset = y * frameLinesize + x * 3;
            frameData[offset + 0] = x + y + frameIndex*3; // Red component
            frameData[offset + 1] = 128 + y + frameIndex*2;   // Green component
            frameData[offset + 2] = 64 + x + frameIndex*5;   // Blue component
        }
    }
}

// resample interleaved S16 audio into planar FLT audio
void resample_s16_fltp(AVFrame *audioFrameS16, AVFrame *audioFrameFLTP, struct SwrContext *resampleContext)
{
    ret = swr_convert(
        resampleContext,
        audioFrameFLTP->data,
        audioFrameFLTP->nb_samples,
        (const uint8_t **)(audioFrameS16->data),
        audioFrameS16->nb_samples
    );
}

// Perform RGB24 to YUV444 conversion
void rescale_rgb24_yuv444(AVFrame *videoFrameRGB, AVFrame *videoFrameYUV, struct SwsContext *rescaleContext)
{
int ret = sws_scale(
        rescaleContext, 
        videoFrameRGB->data, 
        videoFrameRGB->linesize, 
        0, 
        videoFrameRGB->height,
        videoFrameYUV->data, 
        videoFrameYUV->linesize
    );
}

