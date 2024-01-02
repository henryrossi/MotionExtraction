#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Please supply input file\n");
        return -1;
    }

    AVFormatContext *formatContext = NULL;

    // Open Video File
    if (avformat_open_input(&formatContext, argv[1], NULL, NULL) < 0) {
        printf("Couldn't open video file\n");
        return -1;
    }

    // Retrieve Stream Information
    if (avformat_find_stream_info(formatContext, NULL) < 0) {
        printf("Couldn't retrieve stream information of video file\n");
        return -1;
    }

    // Dump information about file into standard error
    av_dump_format(formatContext, 0, argv[1], 0);

    // Now FormatContext->streams is an array of pointers, of size FormatContext->nb_streams
    int i;
    AVCodecContext *codecContextOriginal = NULL;
    AVCodecContext *codecContext = NULL;
    AVCodec *decoder;

    // Find the "best" video stream 
    int ret = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (ret < 0) {
        printf("Cannot find a video stream in the input file\n");
        return -1;
    }
    int videoStreamIndex = ret;

    // The resulting struct should be freed with avcodec_free_context().
    codecContext = avcodec_alloc_context3(decoder);

    if (avcodec_parameters_to_context(codecContext, 
        formatContext->streams[videoStreamIndex]->codecpar) < 0) {
        printf("Failed to fill Codec Context");
        return -1;
    }

    // Initialize the AVCodecContext to use the given AVCodec
    ret = avcodec_open2(codecContext, decoder, NULL);
    if (ret < 0) {
        printf("Cannot open video decoder\n");
        return -1;
    }

    AVFrame *Frame = NULL;
    AVFrame *FrameRGB = NULL;

    // Allocate video frame
    Frame = av_frame_alloc();
    FrameRGB = av_frame_alloc();

    return 0;
}