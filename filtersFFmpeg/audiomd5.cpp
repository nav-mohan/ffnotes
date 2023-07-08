#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

extern "C"
{
#include "libavutil/channel_layout.h"
#include "libavutil/md5.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"
};

#define INPUT_SAMPLE_RATE       48000
#define INPUT_FORMAT            AV_SAMPLE_FMT_FLTP
#define INPUT_CHANNEL_LAYOUT    (AVChannelLayout)AV_CHANNEL_LAYOUT_5POINT0
#define FRAME_SIZE              1024

#define VOLUME_VAL 0.90

static int init_filter_graph(AVFilterGraph **graph, AVFilterContext **src, AVFilterContext **sink);
static int process_output(struct AVMD5 *md5, AVFrame *frame);
static int get_input(AVFrame *frame, int frame_num);


int main(int argc, char *argv[])
{
    struct AVMD5 *md5;
    AVFilterGraph *graph;
    AVFilterContext *src, *sink;
    AVFrame *frame;
    uint8_t errstr[1024];
    float duration;
    int err, nb_frames, i;

    if(argc < 2){
        fprintf(stderr, "Usage: %s <duration>\n", argv[0]);
        return 1;
    }

    duration = atof(argv[1]);
    nb_frames = duration * INPUT_SAMPLE_RATE/FRAME_SIZE;
    frame = av_frame_alloc();
    md5 = av_md5_alloc();
    err = init_filter_graph(&graph, &src, &sink);

    for(i = 0; i < nb_frames; i++)
    {
        err = get_input(frame,i);
        err = av_buffersrc_add_frame(src, frame);

        while ((err = av_buffersink_get_frame(sink,frame)) >= 0)
        {
            err = process_output(md5, frame);
            av_frame_unref(frame);
        }

        if(err == AVERROR(EAGAIN))        
            continue;
        else if(err == AVERROR_EOF)
            break;
        else if (err < 0){
            printf("FAILED\n");
            exit(1);
        }
    }

    return 0;
}


static int init_filter_graph(AVFilterGraph **graph, AVFilterContext **src, AVFilterContext **sink)
{
    int err;

    AVFilterGraph *filter_graph;
    
    AVFilterContext *abuffer_ctx;
    const AVFilter *abuffer;
    
    AVFilterContext *volume_ctx;
    const AVFilter *volume;

    AVFilterContext *aformat_ctx;
    const AVFilter *aformat;
    
    AVFilterContext *abuffersink_ctx;
    const AVFilter *abuffersink;


    uint8_t ch_layout[64];
    AVChannelLayout fivePointZero = INPUT_CHANNEL_LAYOUT;
    filter_graph    = avfilter_graph_alloc();
    abuffer         = avfilter_get_by_name("abuffer");
    abuffer_ctx     = avfilter_graph_alloc_filter(filter_graph, abuffer, "src");
    av_channel_layout_describe(&fivePointZero, (char*)ch_layout, sizeof(ch_layout));
    av_opt_set(abuffer_ctx,     "channel_layout",   (char*)ch_layout,                       AV_OPT_SEARCH_CHILDREN);
    av_opt_set(abuffer_ctx,     "sample_fmt",       av_get_sample_fmt_name(INPUT_FORMAT),   AV_OPT_SEARCH_CHILDREN);
    av_opt_set_q(abuffer_ctx,   "time_base",        (AVRational){1,INPUT_SAMPLE_RATE},      AV_OPT_SEARCH_CHILDREN);
    av_opt_set_int(abuffer_ctx, "sample_rate",      INPUT_SAMPLE_RATE,                      AV_OPT_SEARCH_CHILDREN);
    err             = avfilter_init_str(abuffer_ctx, NULL);

    
    AVDictionary *options_dict = NULL;
    volume          = avfilter_get_by_name("volume");
    volume_ctx      = avfilter_graph_alloc_filter(filter_graph, volume, "volume");
    av_dict_set(&options_dict, "volume", AV_STRINGIFY(VOLUME_VAL), 0);
    err             = avfilter_init_dict(volume_ctx, &options_dict);
    av_dict_free(&options_dict);

    
    uint8_t options_str[1024];
    aformat         = avfilter_get_by_name("aformat");
    aformat_ctx     = avfilter_graph_alloc_filter(filter_graph, aformat, "aformat");
    snprintf((char*)options_str, sizeof(options_str), "sample_fmts=%s:sample_rates=%d:channel_layouts=stereo",av_get_sample_fmt_name(AV_SAMPLE_FMT_S16),44100);
    err = avfilter_init_str(aformat_ctx, (char*)options_str);

    abuffersink     = avfilter_get_by_name("abuffersink");
    abuffersink_ctx = avfilter_graph_alloc_filter(filter_graph, abuffersink, "sink");
    err = avfilter_init_str(abuffersink_ctx, NULL);

    /* connect the filters */
    err             = avfilter_link(abuffer_ctx, 0, volume_ctx, 0);
    err             = avfilter_link(volume_ctx, 0, aformat_ctx, 0);
    err             = avfilter_link(aformat_ctx, 0, abuffersink_ctx, 0);

    err             = avfilter_graph_config(filter_graph,NULL);
    *graph          = filter_graph;
    *src            = abuffer_ctx,
    *sink           = abuffersink_ctx;
    
    return 0;

}

static int process_output(struct AVMD5 *md5, AVFrame *frame)
{
    int planar      = av_sample_fmt_is_planar((AVSampleFormat)frame->format);
    int channels    = frame->ch_layout.nb_channels;
    int planes      = planar ? channels : 1;
    int bps         = av_get_bytes_per_sample((AVSampleFormat)frame->format);
    int plane_size  = bps * frame->nb_samples * (planar ? 1 : channels);
    int i, j;

    for(i = 0; i < planes; i++){
        uint8_t checksum[16];
        av_md5_init(md5);
        av_md5_sum(checksum, frame->extended_data[i], plane_size);
        fprintf(stdout, "plane %d: 0x", i);
        for(j = 0; j < sizeof(checksum); j++)
            fprintf(stdout, "%02x", checksum[j]);
        fprintf(stdout, "\n");
    }
    fprintf(stdout, "\n");
    return 0;
}

static int get_input(AVFrame *frame, int frame_num)
{
    int err, i, j;
    AVChannelLayout fivePointZero   = INPUT_CHANNEL_LAYOUT;
    av_channel_layout_copy(&frame->ch_layout, &fivePointZero);
    frame->sample_rate              = INPUT_SAMPLE_RATE;
    frame->format                   = INPUT_FORMAT;
    frame->nb_samples               = FRAME_SIZE;
    frame->pts                      = frame_num * FRAME_SIZE;

    err                             =  av_frame_get_buffer(frame,0);

    for(i = 0; i < 5; i++){
        float *data = (float*)frame->extended_data[i];
        for (j = 0; j < frame->nb_samples; j++)
            data[j] = sin(2*M_PI * (frame_num + j) * (i+1)/FRAME_SIZE);
    }
    return 0;
}