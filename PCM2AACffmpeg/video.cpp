#include "ffincludes.h"
#include "defines.h"

void rescale_rgb24_yuv444(AVFrame *videoFrameRGB, AVFrame *videoFrameYUV, struct SwsContext *rescaleContext)
{
// Perform RGB24 to YUV444 conversion
int ret = sws_scale(
        rescaleContext, 
        videoFrameRGB->data, 
        videoFrameRGB->linesize, 
        0, 
        videoFrameRGB->height,
        videoFrameYUV->data, 
        videoFrameYUV->linesize
    );
    // printf("rescaled %d\n",ret);
}

// generate dummy RGB24 buffer
void generate_rgb24(AVFrame *videoFrameRGB, int64_t frameIndex) 
{
    uint8_t* frameData = videoFrameRGB->data[0];
    int frameLinesize = videoFrameRGB->linesize[0];
    for (int y = 0; y < videoFrameRGB->height; ++y) {
        for (int x = 0; x < videoFrameRGB->width; ++x) {
            int offset = y * frameLinesize + x * 3;
            frameData[offset + 0] = x+y+frameIndex*3; // Red component
            frameData[offset + 1] = y+x+frameIndex*2;   // Green component
            frameData[offset + 2] = x+y+frameIndex*5;   // Blue component
        }
    }
}

int main()
{

    AVFormatContext *formatContextFLV;
    ret = avformat_alloc_output_context2(&formatContextFLV, NULL, FORMAT_NAME, RTMP_ADDRESS);
    assert(ret >= 0);
    
    AVCodec *videoCodecH264 = const_cast<AVCodec*>(avcodec_find_encoder(VIDEO_CODECID));
    assert(videoCodecH264);

    AVPacket *videoPacketH264 = av_packet_alloc();
    assert(videoPacketH264);


    // init AVCodecContext, set args, open AVCodeccontext
    AVCodecContext *videoCodecContextH264 = avcodec_alloc_context3(videoCodecH264);
    assert(videoCodecContextH264);
    videoCodecContextH264->bit_rate = VIDEO_BITRATE;
    videoCodecContextH264->width = VIDEO_WIDTH;
    videoCodecContextH264->height = VIDEO_HEIGHT;
    videoCodecContextH264->time_base = (AVRational){1, VIDEO_FRAMERATE};
    videoCodecContextH264->gop_size = 12;
    videoCodecContextH264->pix_fmt = VIDEO_PIXFMT_OUT;
    if(formatContextFLV->oformat->flags & AVFMT_GLOBALHEADER)
        videoCodecContextH264->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    ret = avcodec_open2(videoCodecContextH264,videoCodecH264,NULL);
    assert(ret == 0);


    // init AVFrame, set args for AVFrame, allocate buffer space for AVFrame
    AVFrame *videoFrameRGB24 = av_frame_alloc();
    assert(videoFrameRGB24);
    videoFrameRGB24->format      = VIDEO_PIXFMT_IN;
    videoFrameRGB24->width       = VIDEO_WIDTH;
    videoFrameRGB24->height      = VIDEO_HEIGHT;
    videoFrameRGB24->time_base   = (AVRational){1,VIDEO_FRAMERATE};
    ret                     = av_frame_get_buffer(videoFrameRGB24,0); 
    assert(ret == 0);

    
    // AVFrame for resampled video RGB24-->YUV444
    AVFrame *videoFrameYUV444 = av_frame_alloc();
    assert(videoFrameYUV444);
    videoFrameYUV444->format      = VIDEO_PIXFMT_OUT;
    videoFrameYUV444->width       = VIDEO_WIDTH;
    videoFrameYUV444->height      = VIDEO_HEIGHT;
    videoFrameYUV444->time_base   = (AVRational){1,VIDEO_FRAMERATE};
    ret                     = av_frame_get_buffer(videoFrameYUV444,0); 
    assert(ret == 0);
    

    AVStream *videoStreamH264 = avformat_new_stream(formatContextFLV ,NULL);
    assert(videoStreamH264);
    videoStreamH264->id = formatContextFLV->nb_streams-1;
    videoStreamH264->time_base = (AVRational){1,VIDEO_FRAMERATE};
    // copy some parameters from AVCodecContext to AVStream
    ret = avcodec_parameters_from_context(videoStreamH264->codecpar,videoCodecContextH264);
    assert(ret >= 0);

    // print details about the stream#0 within formatContextFLV
    av_dump_format(formatContextFLV,0,RTMP_ADDRESS,1); 

    // open the flv container
    if(!(formatContextFLV->oformat->flags & AVFMT_NOFILE)){
        ret = avio_open(&formatContextFLV->pb,RTMP_ADDRESS,AVIO_FLAG_WRITE);
        assert(ret >= 0);
    }
    
    // write the headers
    ret = avformat_write_header(formatContextFLV,NULL);

    // initialize the RGB24-->YUV444 rescaler
    struct SwsContext *rescaleContext = sws_getContext(
        VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_PIXFMT_IN,
        VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_PIXFMT_OUT,
        VIDEO_SCALEFLAGS, NULL,NULL,NULL
    );

    int videoNextPTS = 0;

    while(1)
    {
        printf("outer while\n");
        
        generate_rgb24(videoFrameRGB24,videoNextPTS);

        rescale_rgb24_yuv444(videoFrameRGB24,videoFrameYUV444,rescaleContext);
        videoFrameYUV444->pts = videoNextPTS++;

        ret = avcodec_send_frame(videoCodecContextH264,videoFrameYUV444);
        CONTINUE;
        while(ret >= 0)
        {
            printf("inner while\n");
            ret = avcodec_receive_packet(videoCodecContextH264,videoPacketH264);
            BREAK;
            av_packet_rescale_ts(videoPacketH264,videoCodecContextH264->time_base,videoStreamH264->time_base);
            videoPacketH264->stream_index = videoStreamH264->index;
            
            ret = av_interleaved_write_frame(formatContextFLV,videoPacketH264);
            if(ret == -22) break;

            BREAK;
            PRINTRET;
        }
    }
}