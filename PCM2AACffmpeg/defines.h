#define FORMAT_NAME "flv"
#define RTMP_ADDRESS "rtmp://167.99.183.228/streaming"

// #define FORMAT_NAME "hls"
// #define RTMP_ADDRESS "index.m3u8"


// RGB24 --transcoding--> YUV444 --encoding--> H264
#define VIDEO_PIXFMT_IN         AV_PIX_FMT_RGB24    // camera pix_fmt
#define VIDEO_PIXFMT_OUT        AV_PIX_FMT_YUV420P  // rescaled for encoder
#define VIDEO_BPP               3                  // bytes per pixel
#define VIDEO_CODECID           AV_CODEC_ID_H264    // video-codec
#define VIDEO_FRAMERATE         25                  // 25 frames per second
#define VIDEO_BITRATE           4000000              
#define VIDEO_WIDTH             1920
#define VIDEO_HEIGHT            1080
#define VIDEO_SCALEFLAGS        SWS_BICUBIC         // resample algorithm for RGB24-->YUV444

#define AUDIO_SMPFMT_IN         AV_SAMPLE_FMT_S16   /* microphone sample fmt */
#define AUDIO_SMPFMT_OUT        AV_SAMPLE_FMT_FLTP  /* resampled for encoder  */
#define AUDIO_CODECID           AV_CODEC_ID_AAC     // audio-codec
#define AUDIO_SAMPLERATE        44100
#define AUDIO_BITRATE           128000
#define AUDIO_CHLAYOUT          AV_CHANNEL_LAYOUT_STEREO


#define PRINTRET av_strerror(ret,errbuf,1024);printf("%d: %s\n",ret,errbuf)
#define CONTINUE if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) continue
#define BREAK if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break

char errbuf[1024];
int ret;