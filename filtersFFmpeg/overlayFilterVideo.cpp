extern "C"
{
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavfilter/buffersink.h>
    #include <libavfilter/buffersrc.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/opt.h>
    #include <libavutil/pixdesc.h>
    #include <libavutil/pixfmt.h>
}

AVFormatContext *iFmtCtx1;
AVFormatContext *iFmtCtx2;
AVCodecContext  *vDecCtx1;
AVCodecContext  *vDecCtx2;
AVFormatContext *oFmtCtx;
AVCodecContext  *vEncCtx;

AVPacket        *iPkt1;
AVPacket        *iPkt2;
AVPacket        *oPkt;
AVFrame         *iFrame1;
AVFrame         *iFrame2;
AVFrame         *oFrame;

AVFilterGraph *filter_graph;

AVFilterContext *bufferSrc_ctx1;
AVFilterContext *bufferSrc_ctx2;
AVFilterContext *color_ctx;
AVFilterContext *overlay_ctx;
AVFilterContext *bufferSink_ctx;


static int open_input_file1(const char *filename);
static int open_input_file2(const char *filename);
static int open_output_file(const char *filename, AVCodecContext *decCtx);
static int init_filter_graph(AVCodecContext *dec_ctx,AVCodecContext *enc_ctx);

static int init_filter_graph(AVCodecContext *decCtx,AVCodecContext *encCtx)
{
    char args[512];
    int ret = 0;
    const AVFilter *bufferSrc  = avfilter_get_by_name("buffer");
    const AVFilter *bufferOvr  = avfilter_get_by_name("buffer");
    const AVFilter *bufferSink = avfilter_get_by_name("buffersink");
    const AVFilter *ovrFilter  = avfilter_get_by_name("overlay");
    const AVFilter *colorFilter  = avfilter_get_by_name("colorchannelmixer");
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_RGBA, AV_PIX_FMT_NONE };

    filter_graph = avfilter_graph_alloc();

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    snprintf(args, sizeof(args),
         "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
         decCtx->width, decCtx->height, 
         decCtx->pix_fmt,
         decCtx->time_base.num, decCtx->time_base.den,
         decCtx->sample_aspect_ratio.num, decCtx->sample_aspect_ratio.den);
    ret = avfilter_graph_create_filter(&bufferSrc_ctx1, bufferSrc, "in0", args, NULL, filter_graph);

    /* buffer video overlay source: the overlayed frame from the file will be inserted here. */
    snprintf(args, sizeof(args),
         "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
         decCtx->width, decCtx->height, 
         decCtx->pix_fmt,
         decCtx->time_base.num, decCtx->time_base.den,
         decCtx->sample_aspect_ratio.num, decCtx->sample_aspect_ratio.den);
    ret = avfilter_graph_create_filter(&bufferSrc_ctx2, bufferOvr, "in1", args, NULL, filter_graph);

    /* color filter */
    snprintf(args, sizeof(args), "aa=0.4");
    ret = avfilter_graph_create_filter(&color_ctx, colorFilter, "colorFilter", args, NULL, filter_graph);

    /* overlay filter */
    // snprintf(args, sizeof(args), "x=%d:y=%d:repeatlast=1", 500, 500);
    snprintf(args, sizeof(args), "x=(W-w)/2:y=(H-h)/2:repeatlast=1");
    ret = avfilter_graph_create_filter(&overlay_ctx, ovrFilter, "overlay", args, NULL, filter_graph);

    /* buffer sink - destination of the final video */
    ret = avfilter_graph_create_filter(&bufferSink_ctx, bufferSink, "out", NULL, NULL, filter_graph);

    ret = av_opt_set_int_list(bufferSink_ctx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);

    /*
     * Link all filters..
     */
    avfilter_link(bufferSrc_ctx1, 0, overlay_ctx, 0);
    avfilter_link(bufferSrc_ctx2, 0, color_ctx, 0);
    avfilter_link(color_ctx, 0, overlay_ctx, 1);
    avfilter_link(overlay_ctx, 0, bufferSink_ctx, 0);
    
    ret = avfilter_graph_config(filter_graph, NULL);
    
    return ret;
}

int main(int argc, char *argv[])
{
    // av_log_set_level(AV_LOG_DEBUG);

    if(argc != 4)
    {
        printf("USAGE: %s <INFILE1> <INFILE2> <OUTFILE>\n",argv[0]);
        exit(1);
    }
    const char *infilename1 = argv[1];
    const char *infilename2 = argv[2];
    const char *outfilename = argv[3];

    printf("-----------------------\n");
    open_input_file1(infilename1);
    printf("-----------------------\n");
    open_input_file2(infilename2);
    printf("-----------------------\n");
    open_output_file(outfilename,vDecCtx1);
    printf("-----------------------\n");
    init_filter_graph(vDecCtx1,vEncCtx);
    printf("-----------------------\n");

    int ret = -1;

    iPkt1 = av_packet_alloc();  iPkt2   = av_packet_alloc(); oPkt = av_packet_alloc();
    iFrame1 = av_frame_alloc(); iFrame2 = av_frame_alloc(); oFrame = av_frame_alloc();

    int i = 0;
    i++;

        ret = av_read_frame(iFmtCtx1, iPkt1);
        printf("read ret=%s\n",av_err2str(ret));
        ret = avcodec_send_packet(vDecCtx1,iPkt1);
        printf("send ret=%s\n",av_err2str(ret));
        ret = avcodec_receive_frame(vDecCtx1, iFrame1);
        printf("recv ret=%s\n",av_err2str(ret));

        ret = av_read_frame(iFmtCtx2, iPkt2);
        printf("ret=%s\n",av_err2str(ret));
        ret = avcodec_send_packet(vDecCtx2,iPkt2);
        printf("ret=%s\n",av_err2str(ret));
        ret = avcodec_receive_frame(vDecCtx2, iFrame2);
        printf("ret=%s\n",av_err2str(ret));
        
        printf("FILTERING\n");
        ret = av_buffersrc_add_frame(bufferSrc_ctx2, iFrame2);
        printf("add2 ret = %s\n",av_err2str(ret));
        ret = av_buffersrc_add_frame(bufferSrc_ctx1, iFrame1);
        printf("add1 ret = %s\n",av_err2str(ret));
        ret = av_buffersink_get_frame(bufferSink_ctx,oFrame);
        printf("get ret = %s\n",av_err2str(ret));

        ret = avcodec_send_frame(vEncCtx, oFrame);
        printf("send frame %s\n",av_err2str(ret));
        ret = avcodec_receive_packet(vEncCtx, oPkt);
        printf("recv pkt %s\n",av_err2str(ret));
        ret = av_interleaved_write_frame(oFmtCtx,oPkt);
        printf("write frame %s\n",av_err2str(ret));

    av_write_trailer(oFmtCtx);

}


static int open_input_file1(const char *filename)
{
    int ret = -1;
    ret = avformat_open_input(&iFmtCtx1, filename, NULL, NULL);
    ret = avformat_find_stream_info(iFmtCtx1,NULL);

    for(int i = 0; i < iFmtCtx1->nb_streams; i++)
    {
        if(iFmtCtx1->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            const AVCodec *decoder = avcodec_find_decoder(iFmtCtx1->streams[i]->codecpar->codec_id);
            vDecCtx1 = avcodec_alloc_context3(decoder);
            ret = avcodec_parameters_to_context(vDecCtx1, iFmtCtx1->streams[i]->codecpar);
            vDecCtx1->framerate = av_guess_frame_rate(iFmtCtx1, iFmtCtx1->streams[i],NULL);
            vDecCtx1->time_base = av_inv_q(vDecCtx1->framerate);
            ret = avcodec_open2(vDecCtx1,decoder,nullptr);
            break;
        }
    }
    printf("%d/%d\n",vDecCtx1->sample_aspect_ratio.num,vDecCtx1->sample_aspect_ratio.den);
    printf("%s\n",avcodec_get_name(vDecCtx1->codec_id));
    printf("%s\n",av_get_pix_fmt_name(vDecCtx1->pix_fmt));
    printf("%d/%d\n",vDecCtx1->width,vDecCtx1->height);
    av_dump_format(iFmtCtx1, 0, filename, 0);
    return 0;
}

static int open_input_file2(const char *filename)
{
    int ret = -1;
    ret = avformat_open_input(&iFmtCtx2, filename, NULL, NULL);
    ret = avformat_find_stream_info(iFmtCtx2,NULL);

    for(int i = 0; i < iFmtCtx2->nb_streams; i++)
    {
        if(iFmtCtx2->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            const AVCodec *decoder = avcodec_find_decoder(iFmtCtx2->streams[i]->codecpar->codec_id);
            vDecCtx2 = avcodec_alloc_context3(decoder);
            ret = avcodec_parameters_to_context(vDecCtx2, iFmtCtx2->streams[i]->codecpar);
            vDecCtx2->framerate = av_guess_frame_rate(iFmtCtx2, iFmtCtx2->streams[i],NULL);
            vDecCtx2->time_base = av_inv_q(vDecCtx2->framerate);
            ret = avcodec_open2(vDecCtx2,decoder,nullptr);
            break;
        }
    }
    printf("%d/%d\n",vDecCtx2->sample_aspect_ratio.num,vDecCtx2->sample_aspect_ratio.den);
    printf("%s\n",avcodec_get_name(vDecCtx2->codec_id));
    printf("%s\n",av_get_pix_fmt_name(vDecCtx2->pix_fmt));
    printf("%d/%d\n",vDecCtx2->width,vDecCtx2->height);
    av_dump_format(iFmtCtx2, 0, filename, 0);
    return 0;
}

static int open_output_file(const char *filename, AVCodecContext *decCtx)
{
    int ret = -1;
    avformat_alloc_output_context2(&oFmtCtx, NULL, NULL, filename);
    AVStream *vStream = avformat_new_stream(oFmtCtx,nullptr);
    const AVCodec *encoder = avcodec_find_encoder(decCtx->codec_id);
    vEncCtx = avcodec_alloc_context3(encoder);
    vStream->id = oFmtCtx->nb_streams-1;
    vEncCtx->width = decCtx->width;
    vEncCtx->height = decCtx->height;
    vEncCtx->sample_aspect_ratio = decCtx->sample_aspect_ratio;
    printf("HLWO %d\n",decCtx->width);
    vEncCtx->pix_fmt = decCtx->pix_fmt;
    vEncCtx->framerate = decCtx->framerate;
    vEncCtx->time_base = decCtx->time_base;
    vEncCtx->bit_rate = 2000000;
    if(oFmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
        vEncCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    ret = avcodec_open2(vEncCtx, encoder, nullptr);
    ret = avcodec_parameters_from_context(vStream->codecpar, vEncCtx);
    vStream->time_base = vEncCtx->time_base;

    printf("HLWO\n");
    printf("%d/%d\n",vEncCtx->sample_aspect_ratio.num,vEncCtx->sample_aspect_ratio.den);
    printf("%s\n",avcodec_get_name(vEncCtx->codec_id));
    printf("%s\n",av_get_pix_fmt_name(vEncCtx->pix_fmt));
    printf("%d/%d\n",vEncCtx->width,vEncCtx->height);
    av_dump_format(oFmtCtx,0,filename,1);

    if (!(oFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&oFmtCtx->pb, filename, AVIO_FLAG_WRITE);
    }

    /* init muxer, write output file header */
    ret = avformat_write_header(oFmtCtx, NULL);

    return ret;
}