#include <unistd.h>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

static const char *filter_desc  = "aresample=8000,aformat=sample_fmts=s16:channel_layouts=mono";
static const char *player       = "ffplay -f s16le -ar 8000 -ac 1";

static AVFormatContext *fmt_ctx;
static AVCodecContext *dec_ctx;
AVFilterContext *buffersink_ctx;
AVFilterContext *buffersrc_ctx;
AVFilterGraph *filter_graph;
static int audio_stream_index   = -1;

static int open_input_file(const char *filename)
{
    const AVCodec *dec;
    int ret;

    ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL);
    ret = avformat_find_stream_info(fmt_ctx, NULL);
    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
    audio_stream_index = ret;
    dec_ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[audio_stream_index]->codecpar);
    avcodec_open2(dec_ctx, dec, NULL);
    return 0;
}

static int init_filters(const char *filters_desc)
{
    char args[512];
    int ret = 0;
    const AVFilter *abuffersrc   = avfilter_get_by_name("abuffer");
    const AVFilter *abuffersink  = avfilter_get_by_name("abuffersink");
    AVFilterInOut *outputs      = avfilter_inout_alloc();
    AVFilterInOut *inputs       = avfilter_inout_alloc();
    static const enum AVSampleFormat out_sample_fmts[] = {AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE};
    static const int out_sample_rates[] = { 8000, -1 };
    const AVFilterLink *outlink;
    AVRational time_base        = fmt_ctx->streams[audio_stream_index]->time_base;
    filter_graph = avfilter_graph_alloc();

    /** buffer audio source: the decoded frames from the decoder will be inserted here */
    if(dec_ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&dec_ctx->ch_layout, dec_ctx->ch_layout.nb_channels);
    ret = snprintf(args, sizeof(args), 
        "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=",
        time_base.num, time_base.den, dec_ctx->sample_rate, av_get_sample_fmt_name(dec_ctx->sample_fmt)
    );
    av_channel_layout_describe(&dec_ctx->ch_layout, args+ret, sizeof(args)-ret);
    ret = avfilter_graph_create_filter(&buffersrc_ctx, abuffersrc, "in", args, NULL, filter_graph);
    ret = avfilter_graph_create_filter(&buffersink_ctx, abuffersink, "out", NULL, NULL, filter_graph);

    av_opt_set_int_list(buffersink_ctx, "sample_fmts",  out_sample_fmts, -1, AV_OPT_SEARCH_CHILDREN);
    av_opt_set(         buffersink_ctx, "ch_layouts",   "mono", AV_OPT_SEARCH_CHILDREN);
    av_opt_set_int_list(buffersink_ctx, "sample_rates", out_sample_rates, -1, AV_OPT_SEARCH_CHILDREN);

    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = NULL;
    
    avfilter_graph_parse_ptr(filter_graph, filters_desc, &inputs, &outputs, NULL);
    avfilter_graph_config(filter_graph, NULL);
    outlink = buffersink_ctx->inputs[0];
    av_channel_layout_describe(&outlink->ch_layout, args, sizeof(args));
    
    return ret;
}

static void print_frame(const AVFrame *frame)
{
    const int n = frame->nb_samples * frame->ch_layout.nb_channels;
    const uint16_t *p = (uint16_t*)frame->data[0];
    const uint16_t *p_end = p + n;
    while(p < p_end){
        fputc(*p & 0xff, stdout);
        fputc(*p>>8 & 0xff, stdout);
        p++;
    }
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    int ret;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *filt_frame = av_frame_alloc();
    if(argc != 2){
        printf("Usage: %s file | %s\n",argv[0],player);
        exit(1);
    }
    ret = open_input_file(argv[1]);
    ret = init_filters(filter_desc);
    while(1){
        ret = av_read_frame(fmt_ctx, packet);
        if(ret < 0) break;
        if(packet->stream_index == audio_stream_index){
            ret = avcodec_send_packet(dec_ctx, packet);
            if(ret < 0) break;
            while(ret >= 0){
                ret = avcodec_receive_frame(dec_ctx, frame);
                if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                else if(ret<0) exit(1);

                ret = av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
                if(ret < 0) break;
                
                while(1){
                    ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
                    if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    if(ret < 0) exit(1);
                    print_frame(filt_frame);
                    av_frame_unref(filt_frame);
                }
            }
        }
        av_packet_unref(packet);
    }   
    return 0;
}