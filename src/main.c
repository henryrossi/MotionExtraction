#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h> //?

typedef struct StreamCodecContext {
    AVCodecContext *decoderContext;
    AVCodecContext *encoderContext;
    AVFrame *prevDecoderFrame;
    AVFrame *decoderFrame;
} StreamCodecContext;

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Please supply input and output file\n");
        return -1;
    }
    int ret;

    AVFormatContext *inputFormatContext = NULL;

    /* Open Video File */
    if (avformat_open_input(&inputFormatContext, argv[1], NULL, NULL) < 0) {
        printf("Couldn't open video file\n");
        return -1;
    }

    /* Analyze File */
    // Retrieve Stream Information
    if (avformat_find_stream_info(inputFormatContext, NULL) < 0) {
        printf("Couldn't retrieve stream information of video file\n");
        return -1;
    }

    /* Configure decoders */
    // Now FormatContext->streams is an array of pointers, of size FormatContext->nb_streams
    StreamCodecContext *streamContext = av_calloc(inputFormatContext->nb_streams, sizeof(*streamContext));
    if (!streamContext) return AVERROR(ENOMEM);

    for (int i = 0; i < inputFormatContext->nb_streams; i++) {
        AVStream *stream = inputFormatContext->streams[i];

        const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!decoder) return AVERROR_DECODER_NOT_FOUND;

        AVCodecContext *codecContext = NULL;
        codecContext = avcodec_alloc_context3(decoder);
        if (!codecContext) return AVERROR(ENOMEM);

        if (avcodec_parameters_to_context(codecContext, stream->codecpar) < 0) return -1;

        /* Inform the decoder about the timebase for the packet timestamps.
         * This is highly recommended, but not mandatory. */
        codecContext->pkt_timebase = stream->time_base;

        if (codecContext->codec_type == AVMEDIA_TYPE_VIDEO || codecContext->codec_type == AVMEDIA_TYPE_AUDIO) {
            ret = avcodec_open2(codecContext, decoder, NULL);
            if (ret < 0) {
                printf("Cannot open video decoder\n");
                return -1;
            }
        }

        streamContext[i].decoderContext = codecContext;
 
        streamContext[i].prevDecoderFrame = av_frame_alloc();
        streamContext[i].decoderFrame = av_frame_alloc();
        if (!streamContext[i].prevDecoderFrame || !streamContext[i].decoderFrame)
            return AVERROR(ENOMEM);
    }

    // Dump information about file into standard error
    av_dump_format(inputFormatContext, 0, argv[1], 0);

    AVFormatContext *outputFormatContext = NULL;
    avformat_alloc_output_context2(&outputFormatContext, NULL, NULL, argv[2]);
    if (!outputFormatContext) {
        printf("Could not create output context\n");
        return -1;
    }

    for (int i = 0; i < inputFormatContext->nb_streams; i++) {
        AVStream *outputStream = avformat_new_stream(outputFormatContext, NULL);
        if (!outputStream) return -1;

        AVStream *inputStream = inputFormatContext->streams[i];
        AVCodecContext *decoderContext = streamContext[i].decoderContext;

        if (decoderContext->codec_type == AVMEDIA_TYPE_VIDEO || decoderContext->codec_type == AVMEDIA_TYPE_AUDIO) {
            const AVCodec *encoder = avcodec_find_encoder(decoderContext->codec_id);
            if (!encoder) return -1;

            AVCodecContext *encoderContext = avcodec_alloc_context3(encoder);
            if (!encoderContext) AVERROR(ENOMEM);

            if (decoderContext->codec_type == AVMEDIA_TYPE_VIDEO) {
                encoderContext->height = decoderContext->height;
                encoderContext->width = decoderContext->width;
                encoderContext->sample_aspect_ratio = decoderContext->sample_aspect_ratio;
                /* take first format from list of supported formats */
                if (encoder->pix_fmts)
                    encoderContext->pix_fmt = encoder->pix_fmts[0];
                else
                    encoderContext->pix_fmt = decoderContext->pix_fmt;
                /* video time_base can be set to whatever is handy and supported by encoder */
                /* this is a "theoretical" time base, before writing a frame you must convert
                its timebase to the one actually used by the stream using av_packet_rescale_ts() */
                encoderContext->time_base = av_inv_q(av_guess_frame_rate(inputFormatContext, inputStream, NULL));
            } else {
                encoderContext->sample_rate = decoderContext->sample_rate;
                ret = av_channel_layout_copy(&encoderContext->ch_layout, &decoderContext->ch_layout);
                if (ret < 0)
                    return ret;
                /* take first format from list of supported formats */
                encoderContext->sample_fmt = encoder->sample_fmts[0];
                encoderContext->time_base = (AVRational){1, encoderContext->sample_rate};
            }
 
            if (outputFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
                encoderContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            ret = avcodec_open2(encoderContext, encoder, NULL);
            if (ret < 0) return ret;

            ret = avcodec_parameters_from_context(outputStream->codecpar, encoderContext);
            if (ret < 0) return ret;
 
            outputStream->time_base = encoderContext->time_base;
            streamContext[i].encoderContext = encoderContext;

            ret = avcodec_parameters_copy(outputStream->codecpar, inputStream->codecpar);
            if (ret < 0) {
                printf("Copying parameters for stream #%u failed\n", i);
                return ret;
            }
            outputStream->time_base = inputStream->time_base;

        } else if (decoderContext->codec_type == AVMEDIA_TYPE_UNKNOWN) {
            printf("Elementary stream #%d is of unknown type, cannot proceed\n", i);
            return AVERROR_INVALIDDATA;
        } else {
            /* if this stream must be remuxed */ // ????
            ret = avcodec_parameters_copy(outputStream->codecpar, inputStream->codecpar);
            if (ret < 0) {
                printf("Copying parameters for stream #%u failed\n", i);
                return ret;
            }
            outputStream->time_base = inputStream->time_base;
        }
    }

    // Dump information about file into standard error
    av_dump_format(outputFormatContext, 0, argv[2], 1);

    if (!(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outputFormatContext->pb, argv[2], AVIO_FLAG_WRITE);
        if (ret < 0) {
            printf("Could not open output file '%s'", argv[2]);
            return ret;
        }
    }
 
    /* init muxer, write output file header */
    ret = avformat_write_header(outputFormatContext, NULL);
    if (ret < 0) {
        printf("Error occurred when opening output file\n");
        return ret;
    }


    /* Read Packets */
    AVPacket *packet = av_packet_alloc();
    if (!packet) return -1;

    while (1) {
        ret = av_read_frame(inputFormatContext, packet);
        if (ret < 0) break;

        int streamIndex = packet->stream_index;
        if (inputFormatContext->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            av_packet_rescale_ts(packet, inputFormatContext->streams[streamIndex]->time_base, 
                                 outputFormatContext->streams[streamIndex]->time_base);
            ret = av_interleaved_write_frame(outputFormatContext, packet);
            if (ret < 0) break;
        } else {
            av_packet_rescale_ts(packet, inputFormatContext->streams[streamIndex]->time_base, 
                                 outputFormatContext->streams[streamIndex]->time_base);
            ret = av_interleaved_write_frame(outputFormatContext, packet);
            if (ret < 0) break;
        }

        av_packet_unref(packet);
    }


    // av_write_frame();
    av_write_trailer(outputFormatContext);

    /* Clean up */
    av_packet_free(&packet);

    for (int i = 0; i < inputFormatContext->nb_streams; i++) {
        avcodec_free_context(&streamContext[i].decoderContext);
        if (outputFormatContext && outputFormatContext->nb_streams > i && 
            outputFormatContext->streams[i] && streamContext[i].encoderContext)
            avcodec_free_context(&streamContext[i].encoderContext);
        av_frame_free(&streamContext[i].prevDecoderFrame);
        av_frame_free(&streamContext[i].decoderFrame);
    }
    av_free(streamContext);

    avformat_close_input(&inputFormatContext);

    if (outputFormatContext && !(outputFormatContext->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outputFormatContext->pb);

    avformat_free_context(outputFormatContext);

    return 0;
}