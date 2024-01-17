#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

typedef struct VideoStreamCodec {
    // stream index?
    AVCodecContext *decoderContext;
    AVCodecContext *encoderContext;
    // AVFrame *prevDecoderFrame;
    AVFrame *frame;
} VideoStreamCodec;

int mux(AVFormatContext *iformat_context, AVFormatContext *oformat_context, 
        AVPacket *packet, int stream_index) {
    av_packet_rescale_ts(packet, iformat_context->streams[stream_index]->time_base, 
                             oformat_context->streams[stream_index]->time_base);

    int ret = av_interleaved_write_frame(oformat_context, packet);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to write packet to output file\n");
        return ret;
    }
    return 0;
}

int encode_video(AVFormatContext *iformat_context, AVFormatContext *oformat_context, 
                 VideoStreamCodec *video_codec , AVPacket *packet, int stream_index) {
    int ret;
    while (1) {
        av_packet_unref(packet);

        ret = avcodec_receive_packet(video_codec->encoderContext, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            fprintf(stderr, "ERROR:   Failed recieving packet from encoder\n");
            return ret;
        }

        ret = mux(iformat_context, oformat_context, packet, stream_index);
        if (ret < 0) return ret;
    }

    av_frame_unref(video_codec->frame);

    return 0;
}

int process_video(AVFormatContext *iformat_context, AVFormatContext *oformat_context, 
                  VideoStreamCodec *video_codec , AVPacket *packet, int stream_index) {
    int ret;
    while (1) {
        ret = avcodec_receive_frame(video_codec->decoderContext, video_codec->frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            fprintf(stderr, "ERROR:   Failed recieving frame from decoder\n");
            return ret;
        }

        video_codec->frame->pts = video_codec->frame->best_effort_timestamp;

        // change frame
        // struct SwsContext *sws_ctx = sws_getContext();

        ret = avcodec_send_frame(video_codec->encoderContext, video_codec->frame);
        if (ret < 0) {
            fprintf(stderr, "ERROR:   Failed sending frame to encoder, (error: %s)\n", av_err2str(ret));
            return ret;
        }

        ret = encode_video(iformat_context, oformat_context, video_codec, packet, stream_index);
        if (ret < 0) return ret;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    // if (argc != 3) {
    // fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
    //     return -1;
    // }

    AVFormatContext *iformat_context = NULL;

    int ret = avformat_open_input(&iformat_context, "pong.mov", NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to open input file\n");
        return ret;
    }

    ret = avformat_find_stream_info(iformat_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to read input file and find stream information\n");
        return ret;
    }

    av_dump_format(iformat_context, 0, "pong.mov", 0);

    AVFormatContext *oformat_context = NULL;
    ret = avformat_alloc_output_context2(&oformat_context, NULL, NULL, "newtest.mov");
    if (ret < 0){
        fprintf(stderr, "ERROR:   Failed to allocate output context\n");
        return ret;
    }

    if (!(oformat_context->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&oformat_context->pb, "newtest.mov", AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "ERROR:   Could not open output file '%s'", "newtest.mov");
            return ret;
        }
    }

    // Hardcoded to handle files with only 1 video stream for now
    VideoStreamCodec *videoStreamCodec = av_calloc(1, sizeof(VideoStreamCodec));
    if (videoStreamCodec == NULL) {
        fprintf(stderr, "ERROR:   Couldn't allocate StreamCodecContext object\n");
        return AVERROR(ENOMEM);
    }
    videoStreamCodec->frame = av_frame_alloc();
    if (videoStreamCodec->frame == NULL) {
        fprintf(stderr, "ERROR:   Couldn't allocate frame\n");
        return AVERROR(ENOMEM);
    }

    int video_stream = -1;

    for (int i = 0; i < iformat_context->nb_streams; i++) {
        AVStream *stream = avformat_new_stream(oformat_context, NULL);
        if (stream == NULL) {
            fprintf(stderr, "ERROR:   Failed to add a new stream to the output file\n");
            return -1;
        }

        ret = avcodec_parameters_copy(stream->codecpar, iformat_context->streams[i]->codecpar);
        if (ret < 0) {
            fprintf(stderr, "ERROR:   Failed to copy codec params form the input to the output stream\n");
            return ret;
        }

        if (iformat_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream = i;
        }
    }

    const AVCodec *decoder = avcodec_find_decoder(iformat_context->streams[video_stream]->codecpar->codec_id);
    if (decoder == NULL) {
        fprintf(stderr, "ERROR:   Couldn't find the encoder/decoder\n");
        return -1;
    }

    AVCodecContext *decoderContext = avcodec_alloc_context3(decoder);
    if (decoderContext == NULL) {
        fprintf(stderr, "ERROR:   Couldn't allocate the decoder context with the given decoder\n");
        return -1;
    }

    ret = avcodec_parameters_to_context(decoderContext, iformat_context->streams[video_stream]->codecpar);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to fill the decoder context based on the values from codec parameters\n");
        return ret;
    }

    ret = avcodec_open2(decoderContext, decoder, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to open the decoder context\n");
        return ret;
    }

    videoStreamCodec->decoderContext = decoderContext;


    const AVCodec *encoder = avcodec_find_encoder(iformat_context->streams[video_stream]->codecpar->codec_id);
    if (encoder == NULL) {
        fprintf(stderr, "ERROR:   Couldn't find the encoder/decoder\n");
        return -1;
    }

    AVCodecContext *encoderContext = avcodec_alloc_context3(encoder);
    if (encoderContext == NULL) {
        fprintf(stderr, "ERROR:   Couldn't allocate the encoder context with the given codec\n");
        return -1;
    }

    ret = avcodec_parameters_to_context(encoderContext, iformat_context->streams[video_stream]->codecpar);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to fill the encoder context based on the values from codec parameters\n");
        return ret;
    }

    encoderContext->time_base = av_inv_q(av_guess_frame_rate(iformat_context, iformat_context->streams[video_stream], NULL));

    if (oformat_context->oformat->flags & AVFMT_GLOBALHEADER)
        encoderContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2(encoderContext, encoder, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to open the encoder context\n");
        return ret;
    }

    ret = avcodec_parameters_from_context(oformat_context->streams[video_stream]->codecpar, encoderContext);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed avcodec_parameters_from_context()\n");
        return ret;
    }

    videoStreamCodec->encoderContext = encoderContext;

    av_dump_format(oformat_context, 0, "newtest.mov", 1);

    ret = avformat_write_header(oformat_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to write header to the output file\n");
        return ret;
    }

    AVPacket *packet = av_packet_alloc();
    if (packet == NULL) {
        fprintf(stderr, "ERROR:   Couldn't allocate packet\n");
        return -1;
    }

    while (av_read_frame(iformat_context, packet) >= 0) {
        int streamIndex = packet->stream_index;

        if (iformat_context->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            ret = avcodec_send_packet(videoStreamCodec->decoderContext, packet);
            if (ret < 0) {
                fprintf(stderr, "ERROR:   Failed sending packet to decoder\n");
                return ret;
            }

            ret = process_video(iformat_context, oformat_context, videoStreamCodec, packet, streamIndex);
            if (ret < 0) return ret;
        } 
        else {
            ret = mux(iformat_context, oformat_context, packet, streamIndex);
            if (ret < 0) return ret;
        }

        av_packet_unref(packet);
    }

    ret = avcodec_send_packet(videoStreamCodec->decoderContext, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to flush decoder\n");
        return ret;
    }
    ret = process_video(iformat_context, oformat_context, videoStreamCodec, packet, video_stream);
    if (ret < 0) return ret;
    
    ret = avcodec_send_frame(videoStreamCodec->encoderContext, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed sending frame to encoder, (error: %s)\n", av_err2str(ret));
        return ret;
    }
    ret = encode_video(iformat_context, oformat_context, videoStreamCodec, packet, video_stream);
    if (ret < 0) return ret;

    ret = av_write_trailer(oformat_context);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Couldn't wirte output file trailer\n");
        return ret;
    }

    av_packet_free(&packet);
    av_free(videoStreamCodec);
    if (oformat_context && !(oformat_context->oformat->flags & AVFMT_NOFILE))
        avio_closep(&oformat_context->pb);
    avformat_free_context(oformat_context);
    avformat_close_input(&iformat_context);

    return 0;
}