#include "ffincludes.h"
#include "defines.h"
float t     = 0;
float tincr = 2 * M_PI * 110.0 / 44100;
float tincr2 = 2 * M_PI * 110.0 / 44100/44100;

// generate dummy S16 interleaved stereo buffer
void generate_s16(AVFrame *audioFrame, int64_t frameIndex) 
{
    int j,i,v;
    int16_t *q = (int16_t*)audioFrame->data[0];
    int32_t nSamples = audioFrame->nb_samples;
    int32_t nChannels = audioFrame->ch_layout.nb_channels;

    for(j = 0; j < audioFrame->nb_samples; j++)
    {
        for(i = 0; i < audioFrame->ch_layout.nb_channels; i++)
        {
            v = (int16_t)(sin(t) * 10000);
            *q++ = v;
        }
        t += tincr;
        tincr += tincr2;
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

int main()
{

    AVFormatContext *formatContextFLV;
    ret = avformat_alloc_output_context2(&formatContextFLV, NULL, FORMAT_NAME, RTMP_ADDRESS);
    assert(ret >= 0);
    
    AVCodec *audioCodecAAC = const_cast<AVCodec*>(avcodec_find_encoder(AUDIO_CODECID));
    assert(audioCodecAAC);

    AVPacket *audioPacketAAC = av_packet_alloc();
    assert(audioPacketAAC);


    AVChannelLayout src = AV_CHANNEL_LAYOUT_STEREO;
    AVCodecContext *audioCodecContextAAC = avcodec_alloc_context3(audioCodecAAC);
    assert(audioCodecContextAAC);
    audioCodecContextAAC->sample_fmt = AUDIO_SMPFMT_OUT;
    audioCodecContextAAC->bit_rate = AUDIO_BITRATE;
    audioCodecContextAAC->sample_rate = AUDIO_SAMPLERATE;
    audioCodecContextAAC->time_base = (AVRational){1,AUDIO_SAMPLERATE}; // do i need to set this?
    av_channel_layout_copy(&audioCodecContextAAC->ch_layout, &src);
    if(formatContextFLV->oformat->flags & AVFMT_GLOBALHEADER)
        audioCodecContextAAC->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    ret = avcodec_open2(audioCodecContextAAC,audioCodecAAC,NULL);
    assert(ret == 0);


    AVFrame *audioFrameS16 = av_frame_alloc();
    assert(audioFrameS16);
    audioFrameS16->format = AUDIO_SMPFMT_IN;
    audioFrameS16->nb_samples = audioCodecContextAAC->frame_size;
    audioFrameS16->sample_rate = AUDIO_SAMPLERATE;
    audioFrameS16->time_base = (AVRational){1,AUDIO_SAMPLERATE}; // do i need to set this?
    av_channel_layout_copy(&audioFrameS16->ch_layout, &src);
    av_frame_get_buffer(audioFrameS16,0);
    assert(ret==0);


    AVFrame *audioFrameFLTP = av_frame_alloc();
    assert(audioFrameFLTP);
    audioFrameFLTP->format = AUDIO_SMPFMT_OUT;
    audioFrameFLTP->nb_samples = audioCodecContextAAC->frame_size;
    audioFrameFLTP->sample_rate = AUDIO_SAMPLERATE;
    audioFrameFLTP->time_base = (AVRational){1,AUDIO_SAMPLERATE}; // do i need to set this?
    av_channel_layout_copy(&audioFrameFLTP->ch_layout, &src);
    av_frame_get_buffer(audioFrameFLTP,0);
    assert(ret==0);


    AVStream *audioStreamAAC = avformat_new_stream(formatContextFLV ,NULL);
    assert(audioStreamAAC);
    audioStreamAAC->id = formatContextFLV->nb_streams-1;
    audioStreamAAC->time_base = (AVRational){1,AUDIO_SAMPLERATE};
    ret = avcodec_parameters_from_context(audioStreamAAC->codecpar,audioCodecContextAAC);// copy some parameters from AVCodecContext to AVStream
    assert(ret >= 0);


    // initialize S16->FLTP resampler
    struct SwrContext *resampleContext = swr_alloc();
    av_opt_set_chlayout  (resampleContext, "in_chlayout",       &audioCodecContextAAC->ch_layout, 0);
    av_opt_set_chlayout  (resampleContext, "out_chlayout",      &audioCodecContextAAC->ch_layout, 0);
    av_opt_set_int       (resampleContext, "in_sample_rate",    audioCodecContextAAC->sample_rate, 0);
    av_opt_set_int       (resampleContext, "out_sample_rate",   audioCodecContextAAC->sample_rate, 0);
    av_opt_set_sample_fmt(resampleContext, "in_sample_fmt",     AUDIO_SMPFMT_IN, 0);
    av_opt_set_sample_fmt(resampleContext, "out_sample_fmt",    AUDIO_SMPFMT_OUT, 0);
    ret = swr_init(resampleContext);
    assert(ret>=0);

    int audioNextPTS = 0;

    printf("audioFrameS16->nb_samples = %d\n",audioFrameS16->nb_samples);
    printf("audioFrameFLTP->nb_samples = %d\n",audioFrameFLTP->nb_samples);


    // OPEN THE FLVCONTAINER
    // print details about the stream#0 within formatContextFLV
    av_dump_format(formatContextFLV,audioStreamAAC->index,RTMP_ADDRESS,1); 

    // open the flv container
    if(!(formatContextFLV->oformat->flags & AVFMT_NOFILE)){
        ret = avio_open(&formatContextFLV->pb,RTMP_ADDRESS,AVIO_FLAG_WRITE);
        assert(ret >= 0);
    }
    
    // write the headers
    ret = avformat_write_header(formatContextFLV,NULL);

    while(1)
    {
        printf("outer while\n");
        generate_s16(audioFrameS16,audioNextPTS); // no need to maintain PTS for audioFrameS16
        resample_s16_fltp(audioFrameS16,audioFrameFLTP,resampleContext);
        audioNextPTS+=audioFrameFLTP->nb_samples;
        audioFrameFLTP->pts = audioNextPTS;
        
        ret = avcodec_send_frame(audioCodecContextAAC, audioFrameFLTP);
        PRINTRET;
        CONTINUE;
        while(ret >= 0)
        {
            printf("inner while\n");
            ret = avcodec_receive_packet(audioCodecContextAAC,audioPacketAAC);
            PRINTRET;
            BREAK;
            av_packet_rescale_ts(audioPacketAAC, audioCodecContextAAC->time_base, audioStreamAAC->time_base);
            audioPacketAAC->stream_index = audioStreamAAC->index;
            ret = av_interleaved_write_frame(formatContextFLV,audioPacketAAC);
        }
    }
}