#include "ffincludes.h"

#define STREAM_DURATION     10000.0
#define STREAM_FRAME_RATE   25 /* 25 images/s */
#define CAMERA_OUT_PIXFMT   AV_PIX_FMT_RGB24 /* camera input pix_fmt*/
#define ENCODER_IN_PIXFMT   AV_PIX_FMT_YUV420P /* default pix_fmt */
#define WIDTH               960
#define HEIGHT              540
#define VIDEO_BITRATE       4000000

#define SAMPLE_RATE         44100
#define AUDIO_BITRATE       128000 

#define RTMP_ADDRESS "rtmp://167.99.183.228/streaming"

#define SCALE_FLAGS SWS_BICUBIC
#define PRINTRET av_strerror(ret,errbuf,1024);printf("%s\n",errbuf);
#define ASSERT assert(ret==0)
char errbuf[1024];

// a wrapper around a single output AVStream
typedef struct OutputStream {
    AVStream *st;
    AVCodecContext *cdc_ctx;

    /* pts of the next frame that will be generated */
    int64_t next_pts;
    int samples_count;

    AVFrame *frame;     // the PCM/YUV frame to be encoded
    AVFrame *tmp_frame; // if resampling/rescaling is required

    AVPacket *tmp_pkt;

    float t, tincr, tincr2;

    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;
} OutputStream;


static int write_frame(AVFormatContext *fmt_ctx, AVCodecContext *cdc_ctx,
                       AVStream *st, AVFrame *frame, AVPacket *pkt)
{
    int ret;
    // send the frame to the encoder
    ret = avcodec_send_frame(cdc_ctx, frame);

    while (ret >= 0) {
        ret = avcodec_receive_packet(cdc_ctx, pkt);
        //PRINTRET;
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;

        /* rescale output packet timestamp values from codec to stream timebase */
        av_packet_rescale_ts(pkt, cdc_ctx->time_base, st->time_base);
        pkt->stream_index = st->index;
        // printf(
        //     "Writing frame %f with codec %s\n",
        //     (float)(frame->pts)/cdc_ctx->time_base.den,
        //     cdc_ctx->codec->long_name
        // );
        

        /* Write the compressed frame to the media file. */
        log_packet(fmt_ctx, pkt);
        ret = av_interleaved_write_frame(fmt_ctx, pkt);
    }

    return ret == AVERROR_EOF ? 1 : 0;
}

/* Add an output stream. */
static void add_stream(OutputStream *ost, AVFormatContext *fmt_ctx,
                        const AVCodec **codec, enum AVCodecID codec_id)
{   
    AVCodecContext *cdc_ctx;
    int i;

    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    printf("Adding stream with codec %s\n",(*codec)->long_name);

    ost->tmp_pkt = av_packet_alloc();

    ost->st = avformat_new_stream(fmt_ctx, NULL);
    ost->st->id = fmt_ctx->nb_streams-1;
    cdc_ctx = avcodec_alloc_context3(*codec);
    ost->cdc_ctx = cdc_ctx;

    switch ((*codec)->type) {
    case AVMEDIA_TYPE_AUDIO:
    {
        printf("AV_CODEC_ID_AAC %d\n",codec_id == AV_CODEC_ID_AAC);
        AVChannelLayout src = AV_CHANNEL_LAYOUT_STEREO;
        cdc_ctx->sample_fmt  = (*codec)->sample_fmts ? (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        printf("AUDIO CODEC %s, %s\n",cdc_ctx->codec->long_name, av_get_sample_fmt_name(cdc_ctx->sample_fmt));
        

        cdc_ctx->bit_rate    = AUDIO_BITRATE;
        cdc_ctx->sample_rate = SAMPLE_RATE;
        av_channel_layout_copy(&cdc_ctx->ch_layout, &src);
        ost->st->time_base = (AVRational){ 1, cdc_ctx->sample_rate };
        break;
    }

    case AVMEDIA_TYPE_VIDEO:
    {
        cdc_ctx->codec_id = codec_id;

        cdc_ctx->bit_rate = VIDEO_BITRATE;
        /* Resolution must be a multiple of two. */
        cdc_ctx->width    = WIDTH;
        cdc_ctx->height   = HEIGHT;
        /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
        ost->st->time_base = (AVRational){ 1, STREAM_FRAME_RATE };
        cdc_ctx->time_base       = (AVRational){ 1, STREAM_FRAME_RATE };

        cdc_ctx->gop_size      = 12; /* emit one intra frame every twelve frames at most */
        cdc_ctx->pix_fmt       = ENCODER_IN_PIXFMT;
        break;
    }

    default:
        break;
    }

    /* Some formats want stream headers to be separate. */
    if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        cdc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

/**************************************************************/
/* audio output */

static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
                                  const AVChannelLayout *channel_layout,
                                  int sample_rate, int nb_samples)
{
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Error allocating an audio frame\n");
        exit(1);
    }

    frame->format = sample_fmt;
    av_channel_layout_copy(&frame->ch_layout, channel_layout);
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;
    av_frame_get_buffer(frame, 1);

    return frame;
}

static void open_audio(const AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
    AVCodecContext *c;
    int nb_samples;
    int ret;
    AVDictionary *opt = NULL;

    c = ost->cdc_ctx;

    /* open it */
    av_dict_copy(&opt, opt_arg, 0);
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "Could not open audio codec: %s\n", av_err2str(ret));
        exit(1);
    }

    /* init signal generator */
    ost->t     = 0;
    ost->tincr = 2 * M_PI * 110.0 / c->sample_rate;
    /* increment frequency by 110 Hz per second */
    ost->tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;

    nb_samples = c->frame_size;

    ost->frame     = alloc_audio_frame(c->sample_fmt, &c->ch_layout,
                                       c->sample_rate, nb_samples);
    ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_FLT, &c->ch_layout,
                                       c->sample_rate, nb_samples);

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }

    /* create resampler context */
    ost->swr_ctx = swr_alloc();

    /* set options */
    av_opt_set_chlayout  (ost->swr_ctx, "in_chlayout",      &c->ch_layout,      0);
    av_opt_set_int       (ost->swr_ctx, "in_sample_rate",   c->sample_rate,    0);
    av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt",    AV_SAMPLE_FMT_FLT, 0);
    av_opt_set_chlayout  (ost->swr_ctx, "out_chlayout",     &c->ch_layout,      0);
    av_opt_set_int       (ost->swr_ctx, "out_sample_rate",  c->sample_rate,    0);
    av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt",   c->sample_fmt,     0);

    /* initialize the resampling context */
    if ((ret = swr_init(ost->swr_ctx)) < 0) {
        fprintf(stderr, "Failed to initialize the resampling context\n");
        exit(1);
    }
}

/* Prepare a 16 bit dummy audio frame of 'frame_size' samples and 'nb_channels' channels. */
// static AVFrame *get_audio_frame(OutputStream *ost)
// {
//     // printf("Generating FLTP %d,%d\n",ost->cdc_ctx->sample_fmt,AV_SAMPLE_FMT_FLTP);
//     AVFrame *frame = ost->tmp_frame;
//     int j, i, v;
//     int16_t *q = (int16_t*)frame->data[0];

//     /* check if we want to generate more frames */
//     if (av_compare_ts(ost->next_pts, ost->cdc_ctx->time_base,
//                       STREAM_DURATION, (AVRational){ 1, 1 }) > 0)
//         return NULL;

//     printf("GENERATING %d\n",frame->nb_samples);
//     for (j = 0; j <frame->nb_samples; j++) {
//         v = (int)(sin(ost->t) * 10000);
//         for (i = 0; i < ost->cdc_ctx->ch_layout.nb_channels; i++)
//             *q++ = v;
//         ost->t     += ost->tincr;
//         ost->tincr += ost->tincr2;
//     }

//     frame->pts = ost->next_pts;
//     ost->next_pts  += frame->nb_samples;

//     return frame;
// }

AVPacket *dec_pkt;
AVFormatContext *infmt_ctx;
const AVCodec *dec;
AVCodecContext *dec_ctx;

static AVFrame *get_audio_frame(OutputStream *ost)
{
    if (av_compare_ts(ost->next_pts, ost->cdc_ctx->time_base, STREAM_DURATION, (AVRational){1,1}) > 0)
        return NULL;

    // av_packet_unref(dec_pkt);
    int ret = -1;
    // ost->cdc_ctx->frame_size = 1024;
    printf("obtaining audio frame %d, %d\n",ost->tmp_frame->nb_samples, ost->cdc_ctx->frame_size);
    av_read_frame(infmt_ctx, dec_pkt);
    avcodec_send_packet(dec_ctx,dec_pkt);
    avcodec_receive_frame(dec_ctx, ost->tmp_frame);

DONE:
    ost->tmp_frame->pts = ost->next_pts;
    ost->next_pts += ost->tmp_frame->nb_samples;
    printf("obtained audio frame %d, %d\n",ost->tmp_frame->nb_samples, ost->cdc_ctx->frame_size);
        
    return ost->tmp_frame;
}

/* encode one audio frame and send it to the muxer return 1 when encoding is finished, 0 otherwise */
static int write_audio_frame(AVFormatContext *fmt_ctx, OutputStream *ost)
{
    AVCodecContext *cdc_ctx;
    AVFrame *frame;
    int ret;
    int dst_nb_samples;

    cdc_ctx = ost->cdc_ctx;

    frame = get_audio_frame(ost);

    if (frame) {
        /* convert samples from native format to destination codec format, using the resampler */
        /* compute destination number of samples */
        dst_nb_samples = av_rescale_rnd(
                            swr_get_delay(ost->swr_ctx, cdc_ctx->sample_rate) + frame->nb_samples,
                            cdc_ctx->sample_rate, 
                            cdc_ctx->sample_rate, 
                            AV_ROUND_UP
                            );
        av_assert0(dst_nb_samples == frame->nb_samples);

        /* convert to destination format */
        ret = swr_convert(
                ost->swr_ctx,       // resampler_context
                ost->frame->data,   // output frame buffer
                dst_nb_samples,     // output number samples
                (const uint8_t **)frame->data, // ipnut frame buffer 
                frame->nb_samples   // input number samples
            );
        frame = ost->frame; // replace input frame with output frame

        frame->pts = av_rescale_q(ost->samples_count, (AVRational){1, cdc_ctx->sample_rate}, cdc_ctx->time_base);
        ost->samples_count += dst_nb_samples;
    }

    return write_frame(fmt_ctx, cdc_ctx, ost->st, frame, ost->tmp_pkt);
}

/**************************************************************/
/* video output */

static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
    int ret;
    AVFrame *picture = av_frame_alloc();
    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;
    ret = av_frame_get_buffer(picture, 0);
    assert(ret==0);
    return picture;
}

static void open_video(const AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
    int ret;
    AVCodecContext *c = ost->cdc_ctx;
    AVDictionary *opt = NULL;
    av_dict_copy(&opt, opt_arg, 0);
    ret = avcodec_open2(c, codec, &opt);
    assert(ret==0);
    ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
    ost->tmp_frame = NULL;
    if (c->pix_fmt != AV_PIX_FMT_RGB24) /* ost->tmp_frame is filled only if encoder requires something other than YUV420 */
        ost->tmp_frame = alloc_picture(AV_PIX_FMT_RGB24, c->width, c->height);
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    assert(ret>=0);
}
// static void open_video(const AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
// {
//     int ret;
//     AVCodecContext *c = ost->cdc_ctx;
//     AVDictionary *opt = NULL;
//     av_dict_copy(&opt, opt_arg, 0);
//     ret = avcodec_open2(c, codec, &opt);
//     assert(ret==0);
//     ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
//     ost->tmp_frame = NULL;
//     if (c->pix_fmt != AV_PIX_FMT_YUV420P) /* ost->tmp_frame is filled only if encoder requires something other than YUV420 */
//         ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
//     ret = avcodec_parameters_from_context(ost->st->codecpar, c);
//     assert(ret>=0);
// }

/* Prepare a dummy image. */
// static void fill_yuv_image(AVFrame *pict, int frame_index, int, int)
// {
//     int x, y;
//     int width = pict->width;
//     int height = pict->height;

//     /* Y */
//     for (y = 0; y < height; y++)
//         for (x = 0; x < width; x++)
//             pict->data[0][y * pict->linesize[0] + x] = x + y + frame_index * 3;

//     /* Cb and Cr */
//     for (y = 0; y < height / 2; y++) {
//         for (x = 0; x < width / 2; x++) {
//             pict->data[1][y * pict->linesize[1] + x] = 128 + y + frame_index * 2;
//             pict->data[2][y * pict->linesize[2] + x] = 64 + x + frame_index * 5;
//         }
//     }
// }

/** Prepare dummy RGB24 image */
static void fill_rgb_image(AVFrame *pict, int frame_index)
{
    printf("---------------------------\n");
    // printf("Generating RGB\n");
    uint8_t* pict_data = pict->data[0];
    int linesize = pict->linesize[0];
    for (int y = 0; y < pict->height; ++y) {
        for (int x = 0; x < pict->width; ++x) {
            int offset = y * linesize + x * 3;
            pict_data[offset + 0] = x + y + frame_index*3; // Red component
            pict_data[offset + 1] = 128 + y + frame_index*2;   // Green component
            pict_data[offset + 2] = 64 + x + frame_index*5;   // Blue component
        }
    }
}

static AVFrame *get_video_frame(OutputStream *oStream)
{
    AVCodecContext *cdc_ctx = oStream->cdc_ctx;
    fill_rgb_image(oStream->tmp_frame,oStream->next_pts);
    if(!oStream->sws_ctx){
        oStream->sws_ctx = sws_getContext(
            cdc_ctx->width,cdc_ctx->height,AV_PIX_FMT_RGB24,
            cdc_ctx->width,cdc_ctx->height,AV_PIX_FMT_YUV420P,
            SCALE_FLAGS, NULL, NULL, NULL
        );
    }
    sws_scale(
        oStream->sws_ctx,
        (const uint8_t *const *)(oStream->tmp_frame->data),
        oStream->tmp_frame->linesize,
        0, cdc_ctx->height,
        oStream->frame->data,
        oStream->frame->linesize
    );
    oStream->frame->pts = oStream->next_pts++;
    return oStream->frame;
}

// static AVFrame *get_video_frame(OutputStream *oStream)
// {
//     AVCodecContext *cdc_ctx = oStream->cdc_ctx;
//     if (cdc_ctx->pix_fmt != AV_PIX_FMT_YUV420P) {
//         printf("PERFORMING RESCALEING\n");
//         /* as we only generate a YUV420P picture, we must convert it to the codec pixel format if needed */
//         if (!oStream->sws_ctx) {
//             oStream->sws_ctx = sws_getContext(
//                 cdc_ctx->width, cdc_ctx->height,AV_PIX_FMT_YUV420P,
//                 cdc_ctx->width, cdc_ctx->height,cdc_ctx->pix_fmt,
//                 SCALE_FLAGS, NULL, NULL, NULL
//                 );
//         }
//         fill_yuv_image(oStream->tmp_frame, oStream->next_pts, cdc_ctx->width, cdc_ctx->height);
//         sws_scale(
//             oStream->sws_ctx, 
//             (const uint8_t * const *) oStream->tmp_frame->data, 
//             oStream->tmp_frame->linesize, 
//             0, cdc_ctx->height, 
//             oStream->frame->data, 
//             oStream->frame->linesize 
//             );
//     } else {
//         fill_yuv_image(oStream->frame, oStream->next_pts, cdc_ctx->width, cdc_ctx->height);
//     }

//     oStream->frame->pts = oStream->next_pts++;

//     return oStream->frame;
// }

/* encode one video frame and send it to the muxer return 1 when encoding is finished, 0 otherwise */
static int write_video_frame(AVFormatContext *fmt_ctx, OutputStream *ost)
{
    return write_frame(fmt_ctx, ost->cdc_ctx, ost->st, get_video_frame(ost), ost->tmp_pkt);
}

static void close_stream(AVFormatContext *fmt_ctx, OutputStream *ost)
{
    avcodec_free_context(&ost->cdc_ctx);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    av_packet_free(&ost->tmp_pkt);
    sws_freeContext(ost->sws_ctx);
    swr_free(&ost->swr_ctx);
}

/**************************************************************/
/* media file output */

int main(int argc, char **argv)
{
    OutputStream video_st = { 0 }, audio_st = { 0 };
    const AVOutputFormat *out_fmt;
    const char *filename;
    AVFormatContext *fmt_ctx;
    const AVCodec *audio_codec, *video_codec;
    int ret;
    int have_video = 0, have_audio = 0;
    int encode_video = 0, encode_audio = 0;
    AVDictionary *opt = NULL;
    int i;

    av_log_set_level(AV_LOG_DEBUG);


    avdevice_register_all();
    infmt_ctx = avformat_alloc_context();
    AVDictionary *iopt = NULL;
    av_dict_set(&iopt, "audio_device_index", "0", 0);
    avformat_open_input(&infmt_ctx, NULL , av_find_input_format("avfoundation"), &iopt);
    av_dict_free(&iopt);
    dec_pkt = av_packet_alloc();
    dec_ctx = avcodec_alloc_context3(avcodec_find_decoder(infmt_ctx->streams[0]->codecpar->codec_id));
    avcodec_parameters_to_context(dec_ctx,infmt_ctx->streams[0]->codecpar);
    avcodec_open2(dec_ctx, avcodec_find_decoder(infmt_ctx->streams[0]->codecpar->codec_id), NULL);

    av_dump_format(infmt_ctx,0,NULL,0);
    printf("DECODER: %s,%s\n",dec_ctx->codec->long_name,av_get_sample_fmt_name(dec_ctx->sample_fmt));

    filename = RTMP_ADDRESS;

    /* allocate the output media context */
    avformat_alloc_output_context2(&fmt_ctx, NULL, "flv", filename);
    out_fmt = fmt_ctx->oformat;

    /* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if (out_fmt->video_codec != AV_CODEC_ID_NONE) {
        add_stream(&video_st, fmt_ctx, &video_codec, AV_CODEC_ID_H264);
        have_video = 1;
        encode_video = 1;
    }
    if (out_fmt->audio_codec != AV_CODEC_ID_NONE) {
        add_stream(&audio_st, fmt_ctx, &audio_codec, AV_CODEC_ID_AAC);
        have_audio = 1;
        encode_audio = 1;
    }
    printf("CODEC ID %d , %d\n",video_codec->id, AV_CODEC_ID_H264);


    /* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
    if (have_video) open_video(video_codec, &video_st, opt);

    if (have_audio) open_audio(audio_codec, &audio_st, opt);

    av_dump_format(fmt_ctx, 0, filename, 1);

    /* open the output file, if needed */
    if (!(out_fmt->flags & AVFMT_NOFILE))
        ret = avio_open(&fmt_ctx->pb, filename, AVIO_FLAG_WRITE);

    /* Write the stream header, if any. */
    ret = avformat_write_header(fmt_ctx, &opt);

    while (encode_video || encode_audio) {
        /* select the stream to encode */
        
        // time_diff == -1 if video is lagging behind. its == +1 if audio is lagging behind.
        int time_diff = av_compare_ts(video_st.next_pts, 
                    video_st.cdc_ctx->time_base,
                    audio_st.next_pts, 
                    audio_st.cdc_ctx->time_base );
        printf("time_diff = %d\n",time_diff);
        if (encode_video && (!encode_audio || time_diff <= 0)) {
            // printf("WRITINTG VIDEO\n");
            encode_video = !write_video_frame(fmt_ctx, &video_st);
        } else {
            // printf("WRITINTG AUDIO\n");
            encode_audio = !write_audio_frame(fmt_ctx, &audio_st);
        }
    }

    av_write_trailer(fmt_ctx);

    /* Close each codec. */
    if (have_video) close_stream(fmt_ctx, &video_st);
    if (have_audio) close_stream(fmt_ctx, &audio_st);

    if (!(out_fmt->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&fmt_ctx->pb);

    /* free the stream */
    avformat_free_context(fmt_ctx);

    return 0;
}
