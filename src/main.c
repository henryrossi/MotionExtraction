#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>



typedef struct StreamCodecContext {
    AVCodecContext *decoderContext;
    AVCodecContext *encoderContext;
    AVFrame *prevDecoderFrame;
    AVFrame *decoderFrame;
} StreamCodecContext;



int setUpDecoders(StreamCodecContext *streamContext, AVFormatContext *inputFormatContext, const char *filename) {
    int ret;
    /* Setting up decoder codecContext for each stream */
    for (int i = 0; i < inputFormatContext->nb_streams; i++) {
        AVStream *stream = inputFormatContext->streams[i];

        const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!decoder) {
            fprintf(stderr, "Couldn't find decoder\n");
            return AVERROR_DECODER_NOT_FOUND;
        }

        AVCodecContext *codecContext = NULL;
        codecContext = avcodec_alloc_context3(decoder);
        if (!codecContext) { 
            fprintf(stderr, "Couldn't allocate decoder context\n");
            return AVERROR(ENOMEM);
        }
        
        ret = avcodec_parameters_to_context(codecContext, stream->codecpar);
        if (ret < 0) {
            fprintf(stderr, "Couldn't fill decoder context using stream codec parameters\n");
            return ret;
        }

        /* Inform the decoder about the timebase for the packet timestamps.
         * This is highly recommended, but not mandatory. */
        codecContext->pkt_timebase = stream->time_base;

        if (codecContext->codec_type == AVMEDIA_TYPE_VIDEO) {
            ret = avcodec_open2(codecContext, decoder, NULL);
            if (ret < 0) {
                printf("Couldn't open decoder\n");
                return ret;
            }
        }

        streamContext[i].decoderContext = codecContext;
 
        streamContext[i].prevDecoderFrame = av_frame_alloc();
        streamContext[i].decoderFrame = av_frame_alloc();
        if (!streamContext[i].prevDecoderFrame || !streamContext[i].decoderFrame) {
            fprintf(stderr, "Couldn't allocate decoder frames\n");
            return AVERROR(ENOMEM);
        }
    }

    // Dump information about file into standard error
    av_dump_format(inputFormatContext, 0, filename, 0);

    return 0;
}



int setUpEncoders(StreamCodecContext *streamContext, AVFormatContext *inputFormatContext, 
                  AVFormatContext *outputFormatContext, const char *filename) {
    int ret;
    for (int i = 0; i < inputFormatContext->nb_streams; i++) {
        AVStream *outputStream = avformat_new_stream(outputFormatContext, NULL);
        if (!outputStream) {
            fprintf(stderr, "Couldn't create new output stream\n");
            return -1;
        }

        AVStream *inputStream = inputFormatContext->streams[i];
        AVCodecContext *decoderContext = streamContext[i].decoderContext;

        /* I am ONLY decoding/encoding video */

        /* Check for unknown AVMEDIA TYPE */
        if (decoderContext->codec_type == AVMEDIA_TYPE_UNKNOWN) {
            fprintf(stderr, "Elementary stream #%d is of unknown type, cannot proceed\n", i);
            return AVERROR_INVALIDDATA;
        }

        /* Setting up encoder codecContext for each audio and video stream 
           I'm still very unfamiliar with what is going on here */
        if (decoderContext->codec_type == AVMEDIA_TYPE_VIDEO || decoderContext->codec_type == AVMEDIA_TYPE_AUDIO) {
            const AVCodec *encoder = avcodec_find_encoder(decoderContext->codec_id);
            if (!encoder) { 
                fprintf(stderr, "Couldn't find encoder\n");
                return -1;
            }

            AVCodecContext *encoderContext = avcodec_alloc_context3(encoder);
            if (!encoderContext) { 
                fprintf(stderr, "Couldn't allocate encoder\n");
                AVERROR(ENOMEM);
            }

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
                if (ret < 0) {
                    fprintf(stderr, "Couldn't copy channel layout\n");
                    return ret;
                }
                /* take first format from list of supported formats */
                encoderContext->sample_fmt = encoder->sample_fmts[0];
                encoderContext->time_base = (AVRational){1, encoderContext->sample_rate};
            }
 
            if (outputFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
                encoderContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            ret = avcodec_open2(encoderContext, encoder, NULL);
            if (ret < 0) {
                fprintf(stderr, "Couldn't open encoder\n");
                return ret;
            }

            // THIS IS NOT WORKING 
            // ret = avcodec_parameters_from_context(outputStream->codecpar, encoderContext);
            // if (ret < 0) return ret;

            /* Copying Stream codec parameters 
            Documentation says to set this manually but I hope it is fine since I'm remuxing to
            the same format */
            ret = avcodec_parameters_copy(outputStream->codecpar, inputStream->codecpar);
            if (ret < 0) {
                fprintf(stderr, "Copying parameters for stream #%d failed\n", i);
                return ret;
            }
 
            outputStream->time_base = encoderContext->time_base;
            streamContext[i].encoderContext = encoderContext;
        } 
    }

    // Dump information about file into standard error
    av_dump_format(outputFormatContext, 0, filename, 1);

    if (!(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outputFormatContext->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open output file '%s'", filename);
            return ret;
        }
    }
 
    return 0;
}



int decodeVideo(AVPacket *packet, AVFormatContext *inputFormatContext,AVFormatContext *outputFormatContext, 
           StreamCodecContext *stream, int streamIndex) {
    int ret = avcodec_send_packet(stream->decoderContext, packet);
    if (ret < 0) {
        printf("Sending Packet: The error code returned is %d\n", ret);
        return ret;
    }

    while (ret >= 0) {
        av_frame_unref(stream->decoderFrame);
        ret = avcodec_receive_frame(stream->decoderContext, stream->decoderFrame);
        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
            break;
        else if (ret < 0) {
            fprintf(stderr, "Recieving Frame: The error code returned is %d\n", ret);
            return ret;
        }
                
        stream->decoderFrame->pts = stream->decoderFrame->best_effort_timestamp;

        av_packet_unref(packet);
        ret = avcodec_send_frame(stream->encoderContext, stream->decoderFrame);
        if (ret < 0) {
            fprintf(stderr, "Sending Frame: The error code returned is %d\n", ret);
            return ret;
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(stream->encoderContext, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) 
                break;
            else if (ret < 0) {
                fprintf(stderr, "Receiving Packet from encoder returned error code %d\n", ret);
                return ret;
            }

            packet->stream_index = streamIndex;
            av_packet_rescale_ts(packet, stream->encoderContext->time_base, 
                                 outputFormatContext->streams[streamIndex]->time_base);
            ret = av_interleaved_write_frame(outputFormatContext, packet);
            if (ret < 0) {
                fprintf(stderr, "Writing Packet: The error code returned is %d\n", ret);
                 return ret;
            }
        }

        av_frame_unref(stream->decoderFrame);
    }
    return 0;
}



int remux(AVPacket *packet, AVFormatContext *inputFormatContext, 
          AVFormatContext *outputFormatContext, int streamIndex) {
    av_packet_rescale_ts(packet, inputFormatContext->streams[streamIndex]->time_base, 
                         outputFormatContext->streams[streamIndex]->time_base);
    return av_interleaved_write_frame(outputFormatContext, packet);
}



int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        return -1;
    }
    int ret;

    AVFormatContext *inputFormatContext = NULL;

    /* Open Video File */
    ret = avformat_open_input(&inputFormatContext, argv[1], NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Couldn't open input file: %s\n", argv[1]);
        return ret;
    }

    /* Analyze File To Recieve Stream Information */
    ret = avformat_find_stream_info(inputFormatContext, NULL);
    if (ret < 0) {
        fprintf(stderr, "Couldn't retrieve stream information of file\n");
        return ret;
    }

    StreamCodecContext *streamContext = av_calloc(inputFormatContext->nb_streams, sizeof(StreamCodecContext));
    if (!streamContext) {
        fprintf(stderr, "Couldn't allocate StreamCodecContext object\n");
        return AVERROR(ENOMEM);
    }

    if (setUpDecoders(streamContext, inputFormatContext, argv[1]) < 0)
        goto cleanUp;

    /* Allocate output format context */
    AVFormatContext *outputFormatContext = NULL;
    avformat_alloc_output_context2(&outputFormatContext, NULL, NULL, argv[2]);
    if (!outputFormatContext) {
        printf("Couldn't allocate output context\n");
        return -1;
    }

    if (setUpEncoders(streamContext, inputFormatContext, outputFormatContext, argv[2]) < 0)
        goto cleanUp;


    /* init muxer, write output file header */
    ret = avformat_write_header(outputFormatContext, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        return ret;
    }


    /* Read Packets */
    AVPacket *packet = av_packet_alloc();
    if (!packet) {
        fprintf(stderr, "Couldn't allocate packet\n");
        return -1;
    }

    while (av_read_frame(inputFormatContext, packet) >= 0) {
        int streamIndex = packet->stream_index;
        StreamCodecContext *stream = &streamContext[streamIndex];
        
        if (inputFormatContext->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            ret = decodeVideo(packet, inputFormatContext, outputFormatContext, stream, streamIndex);
            if (ret < 0) return ret;
        } else {
            ret = remux(packet, inputFormatContext, outputFormatContext, streamIndex);
            if (ret < 0) break;
        }

        av_packet_unref(packet);
    }









    printf("Time to flush the decoders and encoders\n");

    /* flush decoders and encoders */
    for (int i = 0; i < inputFormatContext->nb_streams; i++) {
        StreamCodecContext *stream = &streamContext[i];

        if (inputFormatContext->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
            continue;

        ret = avcodec_send_packet(stream->decoderContext, NULL);
        if (ret < 0)
            goto cleanUp;
        
        while (ret >= 0) {
            ret = avcodec_receive_frame(stream->decoderContext, stream->decoderFrame);
            if (ret == AVERROR_EOF)
                break;
            else if (ret < 0)
                goto cleanUp;

            stream->decoderFrame->pts = stream->decoderFrame->best_effort_timestamp;

            av_packet_unref(packet);
            ret = avcodec_send_frame(stream->encoderContext, stream->decoderFrame);
            if (ret < 0) return ret;

            while (ret >= 0) {
                ret = avcodec_receive_packet(stream->encoderContext, packet);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    break;
                packet->stream_index = i;
                av_packet_rescale_ts(packet, stream->encoderContext->time_base, 
                                 outputFormatContext->streams[i]->time_base);
                ret = av_interleaved_write_frame(outputFormatContext, packet);
            }

            av_frame_unref(stream->decoderFrame);
            if (ret < 0) return ret;
        }

        av_packet_unref(packet);
 
        ret = avcodec_send_frame(stream->encoderContext, NULL);
        if (ret < 0) return ret;

        while (ret >= 0) {
            ret = avcodec_receive_packet(stream->encoderContext, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            packet->stream_index = i;
            av_packet_rescale_ts(packet, stream->encoderContext->time_base, 
                                 outputFormatContext->streams[i]->time_base);
            ret = av_interleaved_write_frame(outputFormatContext, packet);
        }
    }





    av_write_trailer(outputFormatContext);

    printf("Done writing to outfile\n");

    /* Clean up */
cleanUp:
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