// using the drawtext filter in libavfilter to make a scrolling new ticker

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

AVFormatContext     *iFmtCtx;
AVCodecContext      *vDecCtx;
AVFormatContext     *oFmtCtx;
AVCodecContext      *vEncCtx;
AVPacket            *iPkt;
AVFrame             *iFrame;
AVPacket            *oPkt;
AVFrame             *oFrame;
AVFilterGraph       *filter_graph;
AVFilterContext     *bufferSrcCtx;
AVFilterContext     *textCtx;
AVFilterContext     *boxCtx;
AVFilterContext     *bufferSinkCtx;

int inputVideoStreamIndex = -1;

static int open_input_file(const char *filename);
static int open_output_file(const char *filename, AVCodecContext *decCtx);
static int init_filter_graph(AVCodecContext *decCtx, AVCodecContext *encCtx);

static int init_filter_graph(AVCodecContext *decCtx, AVCodecContext *encCtx)
{
    int ret = -1;
    char args[1024];

    filter_graph = avfilter_graph_alloc();
    
    // setup source to accept our video frame
    snprintf(args, sizeof(args), 
        "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
        decCtx->width, decCtx->height,
        decCtx->pix_fmt,
        decCtx->time_base.num, decCtx->time_base.den, // this determines the scroll speed
        decCtx->sample_aspect_ratio.num,decCtx->sample_aspect_ratio.den
    );
    ret = avfilter_graph_create_filter(&bufferSrcCtx, avfilter_get_by_name("buffer"), "in", args, NULL, filter_graph);

    // setup scrolling text
    const char text[] = "Hello world. Hello world. Hello world. Hello world. Hello world. Hello world. Hello world. Hello world. Hello world. Hello world. Hello world. Hello world. Hello world. Hello world. Hello world. Hello world.";
    float xrate = -0.04; // this number was picked based on video's time_base.
    const char *fontcolor="white";
    int fontsize=22;
    int ypos = decCtx->height - fontsize;
    snprintf(args, sizeof(args), "text=%s:x=%f*t:y=%d:fontcolor=%s:fontsize=%d",text,xrate,ypos-5,fontcolor,fontsize);
    ret = avfilter_graph_create_filter(&textCtx, avfilter_get_by_name("drawtext"), "text", args, NULL, filter_graph);
    
    // setup a black box as a background
    const char *bgcolor = "black";
    snprintf(args, sizeof(args),"x=%d:y=%d:w=%d:h=%d:color=%s:t=fill",0,ypos-10,decCtx->width,fontsize+10,bgcolor);
    ret = avfilter_graph_create_filter(&boxCtx, avfilter_get_by_name("drawbox"),"box",args,NULL,filter_graph);

    //  setup the sink to produce YUV420P frames
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
    ret = avfilter_graph_create_filter(&bufferSinkCtx, avfilter_get_by_name("buffersink"), "out", NULL, NULL, filter_graph);
    ret = av_opt_set_int_list(bufferSinkCtx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);

    ret = avfilter_link(bufferSrcCtx, 0, boxCtx, 0);
    ret = avfilter_link(boxCtx, 0, textCtx, 0);
    ret = avfilter_link(textCtx, 0, bufferSinkCtx, 0);
    
    ret = avfilter_graph_config(filter_graph, NULL);

    return ret;
}

int main(int argc, char *argv[])
{
    av_log_set_level(AV_LOG_DEBUG);

    int ret = -1;

    if(argc != 3)
    {
        fprintf(stderr, "USAGE: %s <INFILE> <OUTFILE>\n",argv[0]);
        return 1;
    }

    const char *infilename = argv[1];
    const char *outfilename = argv[2];

    open_input_file(infilename);
    open_output_file(outfilename, vDecCtx);
    init_filter_graph(vDecCtx, vEncCtx);

    iPkt = av_packet_alloc(); oPkt = av_packet_alloc();
    iFrame = av_frame_alloc(); oFrame = av_frame_alloc(); 
    int i = 0;
    while(1)
    {
        i++;
        char args[1024];
        snprintf(args, sizeof(args),"x=%d",i);
        if(i == 300) // change the text at the 10 seconds
        {
            const char text[] = "blablablablablablablablablablablablablablablablablabla";
            snprintf(args, sizeof(args),"text=%s",text);
            ret = avfilter_graph_queue_command(filter_graph, "text", "reinit", args, 0, 0);
        }
        
        /** read in video packets, decode them*/
        ret = av_read_frame(iFmtCtx, iPkt);
        if(iPkt->stream_index != inputVideoStreamIndex) continue;
        printf("av_read_frame ret = %s\n",av_err2str(ret));
        if(ret == AVERROR_EOF) break;
        ret = avcodec_send_packet(vDecCtx, iPkt);
        printf("avcodec_send_packet ret = %s\n",av_err2str(ret));
        ret = avcodec_receive_frame(vDecCtx, iFrame);
        printf("avcodec_receive_frame ret = %s\n",av_err2str(ret));

        /** pass videoFrame through filtergraph*/
        ret = av_buffersrc_add_frame(bufferSrcCtx, iFrame);
        ret = av_buffersink_get_frame(bufferSinkCtx, oFrame);
        ret = avcodec_send_frame(vEncCtx, oFrame);
        printf("avcodec_send_frame ret = %s\n",av_err2str(ret));
        ret = avcodec_receive_packet(vEncCtx, oPkt);
        printf("addavcodec_receive_packet ret = %s\n",av_err2str(ret));
        if(ret < 0) break;

        /** write to file */
        ret = av_interleaved_write_frame(oFmtCtx, oPkt);
        printf("av_interleaved_write_frame ret = %s\n",av_err2str(ret));
    }
    av_write_trailer(oFmtCtx);

    return ret;
}

static int open_input_file(const char *filename)
{
    int ret = -1;
    ret = avformat_open_input(&iFmtCtx, filename, NULL, NULL);
    ret = avformat_find_stream_info(iFmtCtx, NULL);

    for(int i = 0; i < iFmtCtx->nb_streams; i++)
    {
        if(iFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            inputVideoStreamIndex = i;
            const AVCodec *decoder = avcodec_find_decoder(iFmtCtx->streams[i]->codecpar->codec_id);
            vDecCtx = avcodec_alloc_context3(decoder);
            ret = avcodec_parameters_to_context(vDecCtx, iFmtCtx->streams[i]->codecpar);
            vDecCtx->framerate = av_guess_frame_rate(iFmtCtx, iFmtCtx->streams[i],NULL);
            vDecCtx->time_base = av_inv_q(vDecCtx->framerate);
            ret = avcodec_open2(vDecCtx, decoder, NULL);
            break;
        }
    }
    av_dump_format(iFmtCtx, 0, filename, 0);
    return ret;
}

static int open_output_file(const char *filename, AVCodecContext *decCtx)
{
    int ret = -1;
    ret = avformat_alloc_output_context2(&oFmtCtx, NULL, NULL, filename);
    AVStream *vStream = avformat_new_stream(oFmtCtx, NULL);
    const AVCodec *encoder = avcodec_find_encoder(decCtx->codec_id);
    vEncCtx = avcodec_alloc_context3(encoder);
    vStream->id = oFmtCtx->nb_streams-1;
    vEncCtx->width = decCtx->width;
    vEncCtx->height = decCtx->height;
    vEncCtx->framerate = decCtx->framerate;
    vEncCtx->time_base = decCtx->time_base;
    vEncCtx->pix_fmt = decCtx->pix_fmt;
    if(oFmtCtx->oformat->flags & AV_CODEC_FLAG_GLOBAL_HEADER)
        vEncCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    ret = avcodec_open2(vEncCtx, encoder, NULL);
    ret = avcodec_parameters_from_context(vStream->codecpar, vEncCtx);
    vStream->time_base = vEncCtx->time_base;

    if (!(oFmtCtx->oformat->flags & AVFMT_NOFILE))
        ret = avio_open(&oFmtCtx->pb, filename, AVIO_FLAG_WRITE);

    /* init muxer, write output file header */
    ret = avformat_write_header(oFmtCtx, NULL);
    printf("avformat_write_header ret = %s\n",av_err2str(ret));

    av_dump_format(oFmtCtx, 0, filename, 1);

    return ret;

}