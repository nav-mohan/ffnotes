#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

extern "C"
{
    #include <libavutil/avutil.h>
    #include <libavutil/avassert.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/opt.h>
    #include <libavutil/mathematics.h>
    #include <libavutil/timestamp.h>
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
};

#define STREAM_DURATION   10000.0
#define STREAM_FRAME_RATE 25 /* 25 images/s */
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P /* default pix_fmt */

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

    AVFrame *frame;
    AVFrame *tmp_frame;

    AVPacket *tmp_pkt;

    float t, tincr, tincr2;

    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;
} OutputStream;

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
    printf("%d,%d\n",time_base->num,time_base->den);
    printf("%p | %p\n",pkt->buf->data, pkt->data);
    printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}

static int write_frame(AVFormatContext *fmt_ctx, AVCodecContext *cdc_ctx,
                       AVStream *st, AVFrame *frame, AVPacket *pkt)
{
    int ret;
    // send the frame to the encoder
    ret = avcodec_send_frame(cdc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame to the encoder: %s\n",
                av_err2str(ret));
        exit(1);
    }

    while (ret >= 0) 
    {
        ret = avcodec_receive_packet(cdc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            fprintf(stderr, "Error encoding a frame: %s\n", av_err2str(ret));
            exit(1);
        }

        /* rescale output packet timestamp values from codec to stream timebase */
        printf("BEFORE %lld\n", pkt->pts);
        printf("RESCALING FROM %d/%d TO %d/%d\n", cdc_ctx->time_base.num,cdc_ctx->time_base.den, st->time_base.num,st->time_base.den);
        av_packet_rescale_ts(pkt, cdc_ctx->time_base, st->time_base);
        printf("AFTER %lld\n", pkt->pts);
        pkt->stream_index = st->index;

        /* Write the compressed frame to the media file. */
        // log_packet(fmt_ctx, pkt);
        PRINTRET;
        ret = av_interleaved_write_frame(fmt_ctx, pkt);
        /* pkt is now blank (av_interleaved_write_frame() takes ownership of
         * its contents and resets pkt), so that no unreferencing is necessary.
         * This would be different if one used av_write_frame(). */
        if (ret < 0) {
            fprintf(stderr, "Error while writing output packet: %s\n", av_err2str(ret));
            exit(1);
        }
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
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        exit(1);
    }

    ost->tmp_pkt = av_packet_alloc();
    if (!ost->tmp_pkt) {
        fprintf(stderr, "Could not allocate AVPacket\n");
        exit(1);
    }

    ost->st = avformat_new_stream(fmt_ctx, NULL);
    if (!ost->st) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }
    ost->st->id = fmt_ctx->nb_streams-1;
    cdc_ctx = avcodec_alloc_context3(*codec);
    if (!cdc_ctx) {
        fprintf(stderr, "Could not alloc an encoding context\n");
        exit(1);
    }
    ost->cdc_ctx = cdc_ctx;

    switch ((*codec)->type) {
    case AVMEDIA_TYPE_AUDIO:
    {
        AVChannelLayout src = AV_CHANNEL_LAYOUT_STEREO;
        cdc_ctx->sample_fmt  = (*codec)->sample_fmts ? (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        printf("AV_SAMPLE_FMT_S32P %d\n",cdc_ctx->sample_fmt == AV_SAMPLE_FMT_S32P);
        printf("AV_SAMPLE_FMT_S32 %d\n",cdc_ctx->sample_fmt == AV_SAMPLE_FMT_S32);
        printf("AV_SAMPLE_FMT_S16 %d\n",cdc_ctx->sample_fmt == AV_SAMPLE_FMT_S16);
        printf("AV_SAMPLE_FMT_S16P %d\n",cdc_ctx->sample_fmt == AV_SAMPLE_FMT_S16P);
        
        cdc_ctx->bit_rate    = 64000;
        cdc_ctx->sample_rate = 44100;
        if ((*codec)->supported_samplerates) {
            cdc_ctx->sample_rate = (*codec)->supported_samplerates[0];
            for (i = 0; (*codec)->supported_samplerates[i]; i++) {
                if ((*codec)->supported_samplerates[i] == 44100)
                    cdc_ctx->sample_rate = 44100;
            }
        }
        av_channel_layout_copy(&cdc_ctx->ch_layout, &src);
        ost->st->time_base = (AVRational){ 1, cdc_ctx->sample_rate };
        break;
    }

    case AVMEDIA_TYPE_VIDEO:
        cdc_ctx->codec_id = codec_id;

        cdc_ctx->bit_rate = 400000;
        /* Resolution must be a multiple of two. */
        cdc_ctx->width    = 352;
        cdc_ctx->height   = 288;
        /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
        ost->st->time_base = (AVRational){ 1, STREAM_FRAME_RATE };
        cdc_ctx->time_base       = ost->st->time_base;

        cdc_ctx->gop_size      = 12; /* emit one intra frame every twelve frames at most */
        cdc_ctx->pix_fmt       = STREAM_PIX_FMT;
        if (cdc_ctx->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            /* just for testing, we also add B-frames */
            cdc_ctx->max_b_frames = 2;
        }
        if (cdc_ctx->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
            /* Needed to avoid using macroblocks in which some coeffs overflow.
             * This does not happen with normal video, it just happens here as
             * the motion of the chroma plane does not match the luma plane. */
            cdc_ctx->mb_decision = 2;
        }
        break;

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

    if (nb_samples) {
        if (av_frame_get_buffer(frame, 0) < 0) {
            fprintf(stderr, "Error allocating an audio buffer\n");
            exit(1);
        }
    }

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

    if (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
        nb_samples = 10000;
    else
        nb_samples = c->frame_size;

    ost->frame     = alloc_audio_frame(c->sample_fmt, &c->ch_layout,
                                       c->sample_rate, nb_samples);
    ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, &c->ch_layout,
                                       c->sample_rate, nb_samples);

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }

    /* create resampler context */
    ost->swr_ctx = swr_alloc();
    if (!ost->swr_ctx) {
        fprintf(stderr, "Could not allocate resampler context\n");
        exit(1);
    }

    /* set options */
    av_opt_set_chlayout  (ost->swr_ctx, "in_chlayout",       &c->ch_layout,      0);
    av_opt_set_int       (ost->swr_ctx, "in_sample_rate",     c->sample_rate,    0);
    av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt",      AV_SAMPLE_FMT_S16, 0);
    av_opt_set_chlayout  (ost->swr_ctx, "out_chlayout",      &c->ch_layout,      0);
    av_opt_set_int       (ost->swr_ctx, "out_sample_rate",    c->sample_rate,    0);
    av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt",     c->sample_fmt,     0);

    /* initialize the resampling context */
    if ((ret = swr_init(ost->swr_ctx)) < 0) {
        fprintf(stderr, "Failed to initialize the resampling context\n");
        exit(1);
    }
}

/* Prepare a 16 bit dummy audio frame of 'frame_size' samples and 'nb_channels' channels. */
static AVFrame *get_audio_frame(OutputStream *ost)
{
    AVFrame *frame = ost->tmp_frame;
    int j, i, v;
    int16_t *q = (int16_t*)frame->data[0];

    /* check if we want to generate more frames */
    if (av_compare_ts(ost->next_pts, ost->cdc_ctx->time_base,
                      STREAM_DURATION, (AVRational){ 1, 1 }) > 0)
        return NULL;

    for (j = 0; j <frame->nb_samples; j++) {
        v = (int)(sin(ost->t) * 10000);
        for (i = 0; i < ost->cdc_ctx->ch_layout.nb_channels; i++)
            *q++ = v;
        ost->t     += ost->tincr;
        ost->tincr += ost->tincr2;
    }

    frame->pts = ost->next_pts;
    ost->next_pts  += frame->nb_samples;

    return frame;
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
        printf("NB_SAMPLES = %d\n",frame->nb_samples);
        dst_nb_samples = av_rescale_rnd(
                            swr_get_delay(ost->swr_ctx, cdc_ctx->sample_rate) + frame->nb_samples,
                            cdc_ctx->sample_rate, 
                            cdc_ctx->sample_rate, 
                            AV_ROUND_UP
                            );
        av_assert0(dst_nb_samples == frame->nb_samples);

        /* when we pass a frame to the encoder, it may keep a reference to it
         * internally;
         * make sure we do not overwrite it here
         */
        ret = av_frame_make_writable(ost->frame);
        if (ret < 0) exit(1);

        /* convert to destination format */
        ret = swr_convert(
                ost->swr_ctx,       // rescale_context
                ost->frame->data,   // output frame buffer
                dst_nb_samples,     // output number samples
                (const uint8_t **)frame->data, // ipnut frame buffer 
                frame->nb_samples   // input number samples
            );
        if (ret < 0) {
            fprintf(stderr, "Error while converting\n");
            exit(1);
        }
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
    AVFrame *picture;
    int ret;

    picture = av_frame_alloc();
    if (!picture) return NULL;

    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;

    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(picture, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame data.\n");
        exit(1);
    }

    return picture;
}

static void open_video(const AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
    int ret;
    AVCodecContext *c = ost->cdc_ctx;
    AVDictionary *opt = NULL;

    av_dict_copy(&opt, opt_arg, 0);

    /* open the codec */
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
        exit(1);
    }

    /* allocate and init a re-usable frame */
    ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!ost->frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    /* If the output format is not YUV420P, then a temporary YUV420P
     * picture is needed too. It is then converted to the required
     * output format. */
    ost->tmp_frame = NULL;
    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
        if (!ost->tmp_frame) {
            fprintf(stderr, "Could not allocate temporary picture\n");
            exit(1);
        }
    }

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }
}

/* Prepare a dummy image. */
static void fill_yuv_image(AVFrame *pict, int frame_index, int width, int height)
{
    int x, y, i;

    i = frame_index;

    /* Y */
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;

    /* Cb and Cr */
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
            pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
        }
    }
}

static AVFrame *get_video_frame(OutputStream *oStream)
{
    AVCodecContext *cdc_ctx = oStream->cdc_ctx;
    int ret;
    /* check if we want to generate more frames */
    ret = av_compare_ts(oStream->next_pts, 
                cdc_ctx->time_base, 
                STREAM_DURATION, 
                (AVRational){ 1, 1 }
                );
    if ( ret > 0) return NULL;

    /* when we pass a frame to the encoder, it may keep a reference to it
     * internally; make sure we do not overwrite it here */
    if (av_frame_make_writable(oStream->frame) < 0)
        exit(1);

    if (cdc_ctx->pix_fmt != AV_PIX_FMT_YUV420P) {
        printf("PERFORMING RESCALEING\n");
        /* as we only generate a YUV420P picture, we must convert it to the codec pixel format if needed */
        if (!oStream->sws_ctx) {
            oStream->sws_ctx = sws_getContext(cdc_ctx->width, cdc_ctx->height,
                                          AV_PIX_FMT_YUV420P,
                                          cdc_ctx->width, cdc_ctx->height,
                                          cdc_ctx->pix_fmt,
                                          SCALE_FLAGS, NULL, NULL, NULL);
            if (!oStream->sws_ctx) {
                fprintf(stderr,
                        "Could not initialize the conversion context\n");
                exit(1);
            }
        }
        fill_yuv_image(oStream->tmp_frame, oStream->next_pts, cdc_ctx->width, cdc_ctx->height);
        sws_scale(oStream->sws_ctx, (const uint8_t * const *) oStream->tmp_frame->data,
                  oStream->tmp_frame->linesize, 0, cdc_ctx->height, oStream->frame->data,
                  oStream->frame->linesize);
    } else {
        fill_yuv_image(oStream->frame, oStream->next_pts, cdc_ctx->width, cdc_ctx->height);
    }

    oStream->frame->pts = oStream->next_pts++;

    return oStream->frame;
}

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
    av_log_set_level( AV_LOG_DEBUG );
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

    filename = RTMP_ADDRESS;
    for (i = 2; i+1 < argc; i+=2) {
        if (!strcmp(argv[i], "-flags") || !strcmp(argv[i], "-fflags"))
            av_dict_set(&opt, argv[i]+1, argv[i+1], 0);
    }

    /* allocate the output media context */
    avformat_alloc_output_context2(&fmt_ctx, NULL, "flv", filename);
    out_fmt = fmt_ctx->oformat;

    /* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if (out_fmt->video_codec != AV_CODEC_ID_NONE) {
        add_stream(&video_st, fmt_ctx, &video_codec, out_fmt->video_codec);
        have_video = 1;
        encode_video = 1;
    }
    if (out_fmt->audio_codec != AV_CODEC_ID_NONE) {
        add_stream(&audio_st, fmt_ctx, &audio_codec, out_fmt->audio_codec);
        have_audio = 1;
        encode_audio = 1;
    }

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
        // encode_video = 0;
        if (encode_video &&
            (!encode_audio || av_compare_ts(video_st.next_pts, video_st.cdc_ctx->time_base,
                                            audio_st.next_pts, audio_st.cdc_ctx->time_base) <= 0)) {
            printf("WRITINTG VIDEO\n");
            encode_video = !write_video_frame(fmt_ctx, &video_st);
        } else {
            printf("WRITINTG AUDIO\n");
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
