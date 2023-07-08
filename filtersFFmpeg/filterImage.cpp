/**
 * @file demuxing, decoding, filtering, encoding and muxing API usage example
 * @example transcode.c
 *
 * Convert input to output file, applying some hard-coded filter-graph on both
 * audio and video streams.
 */
extern "C"
{
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavfilter/buffersink.h>
    #include <libavfilter/buffersrc.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/opt.h>
    #include <libavutil/pixdesc.h>
}

static AVFormatContext *ifmt_ctx;
static AVFormatContext *ofmt_ctx;
typedef struct FilteringContext {
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph *filter_graph;

    AVPacket *enc_pkt;
    AVFrame *filtered_frame;
} FilteringContext;
static FilteringContext *filter_ctx;

typedef struct StreamContext {
    AVCodecContext *dec_ctx;
    AVCodecContext *enc_ctx;

    AVFrame *dec_frame;
} StreamContext;
static StreamContext *stream_ctx;

static int open_input_file(const char *filename)
{
    int ret;
    unsigned int i;

    ifmt_ctx = NULL;
    ret = avformat_open_input(&ifmt_ctx, filename, NULL, NULL);
    ret = avformat_find_stream_info(ifmt_ctx, NULL);

    stream_ctx = (StreamContext*)av_calloc(ifmt_ctx->nb_streams, sizeof(*stream_ctx));

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *stream = ifmt_ctx->streams[i];
        const AVCodec *dec = avcodec_find_decoder(stream->codecpar->codec_id);
        AVCodecContext *codec_ctx;
        codec_ctx = avcodec_alloc_context3(dec);
        ret = avcodec_parameters_to_context(codec_ctx, stream->codecpar);
        /* Reencode video & audio and remux subtitles etc. */
        if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO || codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) 
        {
            if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                codec_ctx->framerate = av_guess_frame_rate(ifmt_ctx, stream, NULL);
                codec_ctx->time_base = av_inv_q(codec_ctx->framerate);
            }
            /* Open decoder */
            ret = avcodec_open2(codec_ctx, dec, NULL);
        }
        stream_ctx[i].dec_ctx = codec_ctx;
        stream_ctx[i].dec_frame = av_frame_alloc();
    }

    av_dump_format(ifmt_ctx, 0, filename, 0);
    
    return 0;
}

static int open_output_file(const char *filename)
{
    AVStream *out_stream;
    AVStream *in_stream;
    AVCodecContext *dec_ctx, *enc_ctx;
    const AVCodec *encoder;
    int ret;
    unsigned int i;

    ofmt_ctx = NULL;
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, filename);

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        out_stream = avformat_new_stream(ofmt_ctx, NULL);

        in_stream = ifmt_ctx->streams[i];
        dec_ctx = stream_ctx[i].dec_ctx;

        if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO || dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) 
        {
            /* in this example, we choose transcoding to same codec */
            encoder = avcodec_find_encoder(dec_ctx->codec_id);
            enc_ctx = avcodec_alloc_context3(encoder);

            /* In this example, we transcode to same properties (picture size,
             * sample rate etc.). These properties can be changed for output
             * streams easily using filters */
            if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) 
            {
                enc_ctx->height = dec_ctx->height;
                enc_ctx->width = dec_ctx->width;
                enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
                /* take first format from list of supported formats */
                if (encoder->pix_fmts)
                    enc_ctx->pix_fmt = encoder->pix_fmts[0];
                else
                    enc_ctx->pix_fmt = dec_ctx->pix_fmt;
                /* video time_base can be set to whatever is handy and supported by encoder */
                enc_ctx->time_base = av_inv_q(dec_ctx->framerate);
            }
            else 
            {
                enc_ctx->sample_rate = dec_ctx->sample_rate;
                ret = av_channel_layout_copy(&enc_ctx->ch_layout, &dec_ctx->ch_layout);
                if (ret < 0) return ret;
                /* take first format from list of supported formats */
                enc_ctx->sample_fmt = encoder->sample_fmts[0];
                enc_ctx->time_base = (AVRational){1, enc_ctx->sample_rate};
            }

            if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            /* Third parameter can be used to pass settings to encoder */
            ret = avcodec_open2(enc_ctx, encoder, NULL);
            ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);

            out_stream->time_base = enc_ctx->time_base;
            stream_ctx[i].enc_ctx = enc_ctx;
        } 
        else if (dec_ctx->codec_type == AVMEDIA_TYPE_UNKNOWN) 
        {
            av_log(NULL, AV_LOG_FATAL, "Elementary stream #%d is of unknown type, cannot proceed\n", i);
            return AVERROR_INVALIDDATA;
        } 
        else 
        {
            /* if this stream must be remuxed */
            ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
            out_stream->time_base = in_stream->time_base;
        }

    }
    av_dump_format(ofmt_ctx, 0, filename, 1);

    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, filename, AVIO_FLAG_WRITE);
    }

    /* init muxer, write output file header */
    ret = avformat_write_header(ofmt_ctx, NULL);

    return 0;
}

static int init_filter(FilteringContext* fctx, AVCodecContext *dec_ctx, AVCodecContext *enc_ctx, const char *filter_spec)
{
    char args[512];
    int ret = 0;
    const AVFilter *bufferSrc = NULL;
    const AVFilter *bufferSink = NULL;
    AVFilterContext *buffersrc_ctx = NULL;
    AVFilterContext *buffersink_ctx = NULL;
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVFilterGraph *filter_graph = avfilter_graph_alloc();

    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) 
    {
        bufferSrc = avfilter_get_by_name("buffer");
        bufferSink = avfilter_get_by_name("buffersink");

        snprintf(args, sizeof(args),
                "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
                dec_ctx->time_base.num, dec_ctx->time_base.den,
                dec_ctx->sample_aspect_ratio.num,
                dec_ctx->sample_aspect_ratio.den);
        
        ret = avfilter_graph_create_filter(&buffersrc_ctx, bufferSrc, "in", args, NULL, filter_graph);
        ret = avfilter_graph_create_filter(&buffersink_ctx, bufferSink, "out",NULL, NULL, filter_graph);
        ret = av_opt_set_bin(buffersink_ctx, "pix_fmts",(uint8_t*)&enc_ctx->pix_fmt, sizeof(enc_ctx->pix_fmt),AV_OPT_SEARCH_CHILDREN);
    } 
    else if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) 
    {
        char buf[64];
        bufferSrc = avfilter_get_by_name("abuffer");
        bufferSink = avfilter_get_by_name("abuffersink");

        if (dec_ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_default(&dec_ctx->ch_layout, dec_ctx->ch_layout.nb_channels);
        av_channel_layout_describe(&dec_ctx->ch_layout, buf, sizeof(buf));
        printf("CHANNEL LAYOUT %s\n",buf);
        snprintf(args, sizeof(args),
            "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
            dec_ctx->time_base.num, dec_ctx->time_base.den, dec_ctx->sample_rate,
            av_get_sample_fmt_name(dec_ctx->sample_fmt),
            buf);
        ret = avfilter_graph_create_filter(&buffersrc_ctx, bufferSrc, "in",args, NULL, filter_graph);
        ret = avfilter_graph_create_filter(&buffersink_ctx, bufferSink, "out",NULL, NULL, filter_graph);
        ret = av_opt_set_bin(buffersink_ctx, "sample_fmts", (uint8_t*)&enc_ctx->sample_fmt, sizeof(enc_ctx->sample_fmt), AV_OPT_SEARCH_CHILDREN);
        av_channel_layout_describe(&enc_ctx->ch_layout, buf, sizeof(buf));
        ret = av_opt_set(buffersink_ctx, "ch_layouts", buf, AV_OPT_SEARCH_CHILDREN);
        ret = av_opt_set_bin(buffersink_ctx, "sample_rates", (uint8_t*)&enc_ctx->sample_rate, sizeof(enc_ctx->sample_rate), AV_OPT_SEARCH_CHILDREN);
    }
    else {
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    /* Endpoints for the filter graph. */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    ret = avfilter_graph_parse_ptr(filter_graph, filter_spec, &inputs, &outputs, NULL);
    ret = avfilter_graph_config(filter_graph, NULL);

    /* Fill FilteringContext */
    fctx->buffersrc_ctx = buffersrc_ctx;
    fctx->buffersink_ctx = buffersink_ctx;
    fctx->filter_graph = filter_graph;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

static int init_filters(void)
{
    const char *filter_spec;
    unsigned int i;
    int ret;
    filter_ctx = (FilteringContext*)av_malloc_array(ifmt_ctx->nb_streams, sizeof(*filter_ctx));
    if (!filter_ctx)
        return AVERROR(ENOMEM);

    for (i = 0; i < ifmt_ctx->nb_streams; i++) 
    {
        filter_ctx[i].buffersrc_ctx  = NULL;
        filter_ctx[i].buffersink_ctx = NULL;
        filter_ctx[i].filter_graph   = NULL;
        if (!(ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO || ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO))
            continue;


        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            filter_spec = "null"; /* passthrough (dummy) filter for video */
        else
            filter_spec = "anull"; /* passthrough (dummy) filter for audio */
        ret = init_filter(&filter_ctx[i], stream_ctx[i].dec_ctx, stream_ctx[i].enc_ctx, filter_spec);
        if (ret) return ret;

        filter_ctx[i].enc_pkt = av_packet_alloc();

        filter_ctx[i].filtered_frame = av_frame_alloc();
    }
    return 0;
}

static int encode_write_frame(unsigned int stream_index, int flush)
{
    StreamContext *stream = &stream_ctx[stream_index];
    FilteringContext *filter = &filter_ctx[stream_index];
    AVFrame *filt_frame = flush ? NULL : filter->filtered_frame;
    AVPacket *enc_pkt = filter->enc_pkt;
    int ret;

    av_log(NULL, AV_LOG_INFO, "Encoding frame\n");
    /* encode filtered frame */
    av_packet_unref(enc_pkt);

    ret = avcodec_send_frame(stream->enc_ctx, filt_frame);

    if (ret < 0) return ret;

    while (ret >= 0) {
        ret = avcodec_receive_packet(stream->enc_ctx, enc_pkt);

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return 0;

        /* prepare packet for muxing */
        enc_pkt->stream_index = stream_index;
        av_packet_rescale_ts(enc_pkt,
                             stream->enc_ctx->time_base,
                             ofmt_ctx->streams[stream_index]->time_base);

        av_log(NULL, AV_LOG_DEBUG, "Muxing frame\n");
        /* mux encoded frame */
        ret = av_interleaved_write_frame(ofmt_ctx, enc_pkt);
    }

    return ret;
}

static int filter_encode_write_frame(AVFrame *frame, unsigned int stream_index)
{
    FilteringContext *filter = &filter_ctx[stream_index];
    int ret;

    av_log(NULL, AV_LOG_INFO, "Pushing decoded frame to filters\n");
    /* push the decoded frame into the filtergraph */
    ret = av_buffersrc_add_frame_flags(filter->buffersrc_ctx, frame, 0);

    /* pull filtered frames from the filtergraph */
    while (1) {
        av_log(NULL, AV_LOG_INFO, "Pulling filtered frame from filters\n");
        ret = av_buffersink_get_frame(filter->buffersink_ctx, filter->filtered_frame);
        if (ret < 0) 
        {
            /* if no more frames for output - returns AVERROR(EAGAIN)
             * if flushed and no more frames for output - returns AVERROR_EOF
             * rewrite retcode to 0 to show it as normal procedure completion
             */
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) ret = 0;
            break;
        }

        filter->filtered_frame->pict_type = AV_PICTURE_TYPE_NONE;
        ret = encode_write_frame(stream_index, 0);
        av_frame_unref(filter->filtered_frame);
        if (ret < 0) break;
    }

    return ret;
}

static int flush_encoder(unsigned int stream_index)
{
    if (!(stream_ctx[stream_index].enc_ctx->codec->capabilities &
                AV_CODEC_CAP_DELAY))
        return 0;

    av_log(NULL, AV_LOG_INFO, "Flushing stream #%u encoder\n", stream_index);
    return encode_write_frame(stream_index, 1);
}

int main(int argc, char **argv)
{
    int ret;
    AVPacket *packet = NULL;
    unsigned int stream_index;
    unsigned int i;

    if (argc != 3) 
    {
        av_log(NULL, AV_LOG_ERROR, "Usage: %s <input file> <output file>\n", argv[0]);
        return 1;
    }

    if ((ret = open_input_file(argv[1])) < 0)   goto end;
    if ((ret = open_output_file(argv[2])) < 0)  goto end;
    if ((ret = init_filters()) < 0)             goto end;
    if (!(packet = av_packet_alloc()))          goto end;

    /* read all packets */
    while (1) 
    {
        ret = av_read_frame(ifmt_ctx, packet);
        stream_index = packet->stream_index;
        av_log(NULL, AV_LOG_DEBUG, "Demuxer gave frame of stream_index %u\n",
                stream_index);

        if (filter_ctx[stream_index].filter_graph) {
            StreamContext *stream = &stream_ctx[stream_index];

            av_log(NULL, AV_LOG_DEBUG, "Going to reencode&filter the frame\n");

            av_packet_rescale_ts(packet,
                                 ifmt_ctx->streams[stream_index]->time_base,
                                 stream->dec_ctx->time_base);
            ret = avcodec_send_packet(stream->dec_ctx, packet);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(stream->dec_ctx, stream->dec_frame);
                if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                    break;
                else if (ret < 0)
                    goto end;

                stream->dec_frame->pts = stream->dec_frame->best_effort_timestamp;
                ret = filter_encode_write_frame(stream->dec_frame, stream_index);
                if (ret < 0)
                    goto end;
            }
        } 
        else {
            /* remux this frame without reencoding */
            av_packet_rescale_ts(packet,
                                 ifmt_ctx->streams[stream_index]->time_base,
                                 ofmt_ctx->streams[stream_index]->time_base);

            ret = av_interleaved_write_frame(ofmt_ctx, packet);
            if (ret < 0)
                goto end;
        }
        av_packet_unref(packet);
    }

    /* flush filters and encoders */
    for (i = 0; i < ifmt_ctx->nb_streams; i++) 
    {
        /* flush filter */
        if (!filter_ctx[i].filter_graph) continue;
        ret = filter_encode_write_frame(NULL, i);
        /* flush encoder */
        ret = flush_encoder(i);
    }

    av_write_trailer(ofmt_ctx);
end:
    av_packet_free(&packet);
    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        avcodec_free_context(&stream_ctx[i].dec_ctx);
        if (ofmt_ctx && ofmt_ctx->nb_streams > i && ofmt_ctx->streams[i] && stream_ctx[i].enc_ctx)
            avcodec_free_context(&stream_ctx[i].enc_ctx);
        if (filter_ctx && filter_ctx[i].filter_graph) {
            avfilter_graph_free(&filter_ctx[i].filter_graph);
            av_packet_free(&filter_ctx[i].enc_pkt);
            av_frame_free(&filter_ctx[i].filtered_frame);
        }

        av_frame_free(&stream_ctx[i].dec_frame);
    }
    av_free(filter_ctx);
    av_free(stream_ctx);
    avformat_close_input(&ifmt_ctx);
    if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    if (ret < 0)
        av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));

    return ret ? 1 : 0;
}
