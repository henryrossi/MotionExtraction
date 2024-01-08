#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

/* Restart from here */
// Assume only one video stream in each container
// Only need codec information for video stream since that is the only stream
// I will be decoding/encoding

typedef struct VideoStreamCodec {
    AVCodecContext *decoderContext;
    AVCodecContext *encoderContext;
    // AVFrame *prevDecoderFrame;
    AVFrame *frame;
} VideoStreamCodec;

int main(int argc, char *argv[]) {
    // if (argc != 3) {
    // fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
    //     return -1;
    // }

    AVFormatContext *inputFormatContext = NULL;

    int ret = avformat_open_input(&inputFormatContext, "pong.mov", NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to open input file\n");
        return ret;
    }

    ret = avformat_find_stream_info(inputFormatContext, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to read input file and find stream information\n");
        return ret;
    }

    av_dump_format(inputFormatContext, 0, "pong.mov", 0);

    /* Must set the following before muxing: 
       AVFormatContext.oformat (selects the muxer that will be used) (set using avformat_alloc_output_context2()?)
       AVFormatContext.pb to an open I/O context (unless format is AVFMT_NOFILE type)
       Streams using  avformat_new_stream()
            Must fill stream with:
                AVStream.codecpar
                    AVCodecParameters.codec_type
                    AVCodecParameters.codec_id
                    width / height
                    the pixel or sample format
                    AVStream.time_base (desired timebase which may or may not be used)
                * There is a warning to set AVCodecParameters manually and not use
                  avcodec_parameters_copy() since there is no guarantee that the codec context 
                  values remain valid for both input and output format contexts, but I am always
                  remuxing to the same format. 
                  I assume it is fine then to use avcodec_parameters_copy().
    */
    AVFormatContext *outputFormatContext = NULL;
    ret = avformat_alloc_output_context2(&outputFormatContext, NULL, NULL, "newtest.mov");
    if (ret < 0){
        fprintf(stderr, "ERROR:   Failed to allocate output context\n");
        return ret;
    }

    if (!(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outputFormatContext->pb, "newtest.mov", AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "ERROR:   Could not open output file '%s'", "newtest.mov");
            return ret;
        }
    }

    for (int i = 0; i < inputFormatContext->nb_streams; i++) {
        AVStream *stream = avformat_new_stream(outputFormatContext, NULL); // May need codec as second param
        if (stream == NULL) {
            fprintf(stderr, "ERROR:   Failed to add a new stream to the output file\n");
            return -1;
        }

        ret = avcodec_parameters_copy(stream->codecpar, inputFormatContext->streams[i]->codecpar);
        if (ret < 0) {
            fprintf(stderr, "ERROR:   Failed to copy codec params form the input to the output stream\n");
            return ret;
        }
    }

    av_dump_format(outputFormatContext, 0, "newtest.mov", 1);

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

    /* Steps for setting up a codecContext:
            Allocate codecContext with avcodec_alloc_context3() (free with avcodec_free_context())
            Retrieve a codec using avcodec_find_decoder/encoder_by_name() or avcodec_find_decoder/encoder()
            Parameters to Context?
            Open codecContext with avcodec_open2()
    */
    for (int i = 0; i < inputFormatContext->nb_streams; i++)
        if (inputFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            const AVCodec *decoder = avcodec_find_decoder(inputFormatContext->streams[i]->codecpar->codec_id);
            if (decoder == NULL) {
                fprintf(stderr, "ERROR:   Couldn't find the encoder/decoder\n");
                return -1;
            }

            AVCodecContext *decoderContext = avcodec_alloc_context3(decoder);
            if (decoderContext == NULL) {
                fprintf(stderr, "ERROR:   Couldn't allocate the decoder context with the given decoder\n");
                return -1;
            }

            ret = avcodec_parameters_to_context(decoderContext, inputFormatContext->streams[i]->codecpar);
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


            const AVCodec *encoder = avcodec_find_encoder(inputFormatContext->streams[i]->codecpar->codec_id);
            if (encoder == NULL) {
                fprintf(stderr, "ERROR:   Couldn't find the encoder/decoder\n");
                return -1;
            }

            AVCodecContext *encoderContext = avcodec_alloc_context3(encoder);
            if (encoderContext == NULL) {
                fprintf(stderr, "ERROR:   Couldn't allocate the encoder context with the given codec\n");
                return -1;
            }

            ret = avcodec_parameters_to_context(encoderContext, inputFormatContext->streams[i]->codecpar);
            if (ret < 0) {
                fprintf(stderr, "ERROR:   Failed to fill the encoder context based on the values from codec parameters\n");
                return ret;
            }

            // encoder timebase is not set error
            /* this is a "theoretical" time base, before writing a frame you must convert
               its timebase to the one actually used by the stream using av_packet_rescale_ts() */
            encoderContext->time_base = av_inv_q(av_guess_frame_rate(inputFormatContext, inputFormatContext->streams[i], NULL));

            if (outputFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
                encoderContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            ret = avcodec_open2(encoderContext, encoder, NULL);
            if (ret < 0) {
                fprintf(stderr, "ERROR:   Failed to open the encoder context\n");
                return ret;
            }

            ret = avcodec_parameters_from_context(outputFormatContext->streams[i]->codecpar, encoderContext);
            if (ret < 0) {
                fprintf(stderr, "ERROR:   Failed avcodec_parameters_from_context()\n");
                return ret;
            }

            videoStreamCodec->encoderContext = encoderContext;
        }


    ret = avformat_write_header(outputFormatContext, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to write header to the output file\n");
        return ret;
    }

    AVPacket *packet = av_packet_alloc();
    if (packet == NULL) {
        fprintf(stderr, "ERROR:   Couldn't allocate packet\n");
        return -1;
    }

    while (av_read_frame(inputFormatContext, packet) >= 0) {
        int streamIndex = packet->stream_index;

        if (inputFormatContext->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            /* Steps for decoding and reencoding:
                    Call avcodec_send_packet() to give the decoder raw compressed data in an AVPacket.
                    In a loop:
                        Call avcodec_receive_frame(). On success, it will return an AVFrame containing 
                        uncompressed audio or video data.
                        Repeat this call until it returns:
                            AVERROR(EAGAIN) -> continue with sending input with avcodec_send_packet()
                            Error -> stop program
                        Now we have an AVFrame
                        Call avcodec_send_frame() to give the encoder the AVFrame.
                        In a loop:
                            Call avcodec_receive_packet(). On success, it will return an AVPacket with a
                            compressed frame.
                            Repeat this call until it returns:
                                AVERROR(EAGAIN) -> continue with sending input with avcodec_send_frame()
                                Error -> stop program
                            Now we have an AVPacket
                            Remux that packet
            */
            ret = avcodec_send_packet(videoStreamCodec->decoderContext, packet);
            if (ret < 0) {
                fprintf(stderr, "ERROR:   Failed sending packet to decoder\n");
                return ret;
            }
            while (1) {
                ret = avcodec_receive_frame(videoStreamCodec->decoderContext, videoStreamCodec->frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) {
                    fprintf(stderr, "ERROR:   Failed recieving frame from decoder\n");
                    return ret;
                }

                videoStreamCodec->frame->pts = videoStreamCodec->frame->best_effort_timestamp;

                ret = avcodec_send_frame(videoStreamCodec->encoderContext, videoStreamCodec->frame);
                if (ret < 0) {
                    fprintf(stderr, "ERROR:   Failed sending frame to encoder, (error: %s)\n", av_err2str(ret));
                    return ret;
                }

                while (1) {
                    av_packet_unref(packet);

                    ret = avcodec_receive_packet(videoStreamCodec->encoderContext, packet);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    if (ret < 0) {
                        fprintf(stderr, "ERROR:   Failed recieving packet from encoder\n");
                        return ret;
                    }

                    av_packet_rescale_ts(packet, inputFormatContext->streams[streamIndex]->time_base, 
                             outputFormatContext->streams[streamIndex]->time_base);

                    ret = av_interleaved_write_frame(outputFormatContext, packet);
                    if (ret < 0) {
                        fprintf(stderr, "ERROR:   Failed to write packet to output file\n");
                        return ret;
                    }
                }

                av_frame_unref(videoStreamCodec->frame);
            }
        } else {
            av_packet_rescale_ts(packet, inputFormatContext->streams[streamIndex]->time_base, 
                             outputFormatContext->streams[streamIndex]->time_base);

            ret = av_interleaved_write_frame(outputFormatContext, packet);
            if (ret < 0) {
                fprintf(stderr, "ERROR:   Failed to write packet to output file\n");
                return ret;
            }
        }

        av_packet_unref(packet);
    }

    ret = av_write_trailer(outputFormatContext);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Couldn't wirte output file trailer\n");
        return ret;
    }

    av_packet_free(&packet);
    av_free(videoStreamCodec);
    if (outputFormatContext && !(outputFormatContext->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outputFormatContext->pb);
    avformat_free_context(outputFormatContext);
    avformat_close_input(&inputFormatContext);

    return 0;
}














// typedef struct StreamCodecContext {
//     AVCodecContext *decoderContext;
//     AVCodecContext *encoderContext;
//     AVFrame *prevDecoderFrame;
//     AVFrame *decoderFrame;
// } StreamCodecContext;



// int setUpDecoders(StreamCodecContext *streamContext, AVFormatContext *inputFormatContext, const char *filename) {
//     int ret;
//     /* Setting up decoder codecContext for each stream */
//     for (int i = 0; i < inputFormatContext->nb_streams; i++) {
//         AVStream *stream = inputFormatContext->streams[i];

//         const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
//         if (!decoder) {
//             fprintf(stderr, "Couldn't find decoder\n");
//             return AVERROR_DECODER_NOT_FOUND;
//         }

//         AVCodecContext *codecContext = NULL;
//         codecContext = avcodec_alloc_context3(decoder);
//         if (!codecContext) { 
//             fprintf(stderr, "Couldn't allocate decoder context\n");
//             return AVERROR(ENOMEM);
//         }
        
//         ret = avcodec_parameters_to_context(codecContext, stream->codecpar);
//         if (ret < 0) {
//             fprintf(stderr, "Couldn't fill decoder context using stream codec parameters\n");
//             return ret;
//         }

//         /* Inform the decoder about the timebase for the packet timestamps.
//          * This is highly recommended, but not mandatory. */
//         codecContext->pkt_timebase = stream->time_base;

//         if (codecContext->codec_type == AVMEDIA_TYPE_VIDEO) {
//             ret = avcodec_open2(codecContext, decoder, NULL);
//             if (ret < 0) {
//                 printf("Couldn't open decoder\n");
//                 return ret;
//             }
//         }

//         streamContext[i].decoderContext = codecContext;
 
//         streamContext[i].prevDecoderFrame = av_frame_alloc();
//         streamContext[i].decoderFrame = av_frame_alloc();
//         if (!streamContext[i].prevDecoderFrame || !streamContext[i].decoderFrame) {
//             fprintf(stderr, "Couldn't allocate decoder frames\n");
//             return AVERROR(ENOMEM);
//         }
//     }

//     // Dump information about file into standard error
//     av_dump_format(inputFormatContext, 0, filename, 0);

//     return 0;
// }



// int setUpEncoders(StreamCodecContext *streamContext, AVFormatContext *inputFormatContext, 
//                   AVFormatContext *outputFormatContext, const char *filename) {
//     int ret;
//     for (int i = 0; i < inputFormatContext->nb_streams; i++) {
//         AVStream *outputStream = avformat_new_stream(outputFormatContext, NULL);
//         if (!outputStream) {
//             fprintf(stderr, "Couldn't create new output stream\n");
//             return -1;
//         }

//         AVStream *inputStream = inputFormatContext->streams[i];
//         AVCodecContext *decoderContext = streamContext[i].decoderContext;

//         /* I am ONLY decoding/encoding video */

//         /* Check for unknown AVMEDIA TYPE */
//         if (decoderContext->codec_type == AVMEDIA_TYPE_UNKNOWN) {
//             fprintf(stderr, "Elementary stream #%d is of unknown type, cannot proceed\n", i);
//             return AVERROR_INVALIDDATA;
//         }

//         /* Setting up encoder codecContext for each audio and video stream 
//            I'm still very unfamiliar with what is going on here */
//         if (decoderContext->codec_type == AVMEDIA_TYPE_VIDEO || decoderContext->codec_type == AVMEDIA_TYPE_AUDIO) {
//             const AVCodec *encoder = avcodec_find_encoder(decoderContext->codec_id);
//             if (!encoder) { 
//                 fprintf(stderr, "Couldn't find encoder\n");
//                 return -1;
//             }

//             AVCodecContext *encoderContext = avcodec_alloc_context3(encoder);
//             if (!encoderContext) { 
//                 fprintf(stderr, "Couldn't allocate encoder\n");
//                 AVERROR(ENOMEM);
//             }

//             if (decoderContext->codec_type == AVMEDIA_TYPE_VIDEO) {
//                 encoderContext->height = decoderContext->height;
//                 encoderContext->width = decoderContext->width;
//                 encoderContext->sample_aspect_ratio = decoderContext->sample_aspect_ratio;
//                 /* take first format from list of supported formats */
//                 if (encoder->pix_fmts)
//                     encoderContext->pix_fmt = encoder->pix_fmts[0];
//                 else
//                     encoderContext->pix_fmt = decoderContext->pix_fmt;
//                 /* video time_base can be set to whatever is handy and supported by encoder */
//                 /* this is a "theoretical" time base, before writing a frame you must convert
//                 its timebase to the one actually used by the stream using av_packet_rescale_ts() */
//                 encoderContext->time_base = av_inv_q(av_guess_frame_rate(inputFormatContext, inputStream, NULL));
//             } else {
//                 encoderContext->sample_rate = decoderContext->sample_rate;
//                 ret = av_channel_layout_copy(&encoderContext->ch_layout, &decoderContext->ch_layout);
//                 if (ret < 0) {
//                     fprintf(stderr, "Couldn't copy channel layout\n");
//                     return ret;
//                 }
//                 /* take first format from list of supported formats */
//                 encoderContext->sample_fmt = encoder->sample_fmts[0];
//                 encoderContext->time_base = (AVRational){1, encoderContext->sample_rate};
//             }
 
//             if (outputFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
//                 encoderContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

//             ret = avcodec_open2(encoderContext, encoder, NULL);
//             if (ret < 0) {
//                 fprintf(stderr, "Couldn't open encoder\n");
//                 return ret;
//             }

//             // THIS IS NOT WORKING 
//             // ret = avcodec_parameters_from_context(outputStream->codecpar, encoderContext);
//             // if (ret < 0) return ret;

//             /* Copying Stream codec parameters 
//             Documentation says to set this manually but I hope it is fine since I'm remuxing to
//             the same format */
//             ret = avcodec_parameters_copy(outputStream->codecpar, inputStream->codecpar);
//             if (ret < 0) {
//                 fprintf(stderr, "Copying parameters for stream #%d failed\n", i);
//                 return ret;
//             }
 
//             outputStream->time_base = encoderContext->time_base;
//             streamContext[i].encoderContext = encoderContext;
//         } 
//     }

//     // Dump information about file into standard error
//     av_dump_format(outputFormatContext, 0, filename, 1);

//     if (!(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
//         ret = avio_open(&outputFormatContext->pb, filename, AVIO_FLAG_WRITE);
//         if (ret < 0) {
//             fprintf(stderr, "Could not open output file '%s'", filename);
//             return ret;
//         }
//     }
 
//     return 0;
// }



// int decodeVideo(AVPacket *packet, AVFormatContext *inputFormatContext,AVFormatContext *outputFormatContext, 
//            StreamCodecContext *stream, int streamIndex) {
//     int ret = avcodec_send_packet(stream->decoderContext, packet);
//     if (ret < 0) {
//         printf("Sending Packet: The error code returned is %d\n", ret);
//         return ret;
//     }

//     while (ret >= 0) { 
//         ret = avcodec_receive_frame(stream->decoderContext, stream->decoderFrame);
//         if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
//             break;
//         else if (ret < 0) {
//             fprintf(stderr, "Recieving Frame: The error code returned is %d\n", ret);
//             return ret;
//         }
//         printf("INFO:   This frame has a width of %d, a height of %d, and is of type %d/%d\n",
//                stream->decoderFrame->width, stream->decoderFrame->height, 
//                stream->decoderContext->framerate.num, stream->decoderContext->framerate.den);
//     }

//     return 0;
    
//     ret = avcodec_send_packet(stream->decoderContext, packet);
//     if (ret < 0) {
//         printf("Sending Packet: The error code returned is %d\n", ret);
//         return ret;
//     }

//     while (ret >= 0) {
//         av_frame_unref(stream->decoderFrame);
//         ret = avcodec_receive_frame(stream->decoderContext, stream->decoderFrame);
//         if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
//             break;
//         else if (ret < 0) {
//             fprintf(stderr, "Recieving Frame: The error code returned is %d\n", ret);
//             return ret;
//         }
                
//         stream->decoderFrame->pts = stream->decoderFrame->best_effort_timestamp;

//         av_packet_unref(packet);
//         ret = avcodec_send_frame(stream->encoderContext, stream->decoderFrame);
//         if (ret < 0) {
//             fprintf(stderr, "Sending Frame: The error code returned is %d\n", ret);
//             return ret;
//         }

//         while (ret >= 0) {
//             ret = avcodec_receive_packet(stream->encoderContext, packet);
//             if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) 
//                 break;
//             else if (ret < 0) {
//                 fprintf(stderr, "Receiving Packet from encoder returned error code %d\n", ret);
//                 return ret;
//             }

//             packet->stream_index = streamIndex;
//             av_packet_rescale_ts(packet, stream->encoderContext->time_base, 
//                                  outputFormatContext->streams[streamIndex]->time_base);
//             ret = av_interleaved_write_frame(outputFormatContext, packet);
//             if (ret < 0) {
//                 fprintf(stderr, "Writing Packet: The error code returned is %d\n", ret);
//                  return ret;
//             }
//         }

//         av_frame_unref(stream->decoderFrame);
//     }
//     return 0;
// }



// int remux(AVPacket *packet, AVFormatContext *inputFormatContext, 
//           AVFormatContext *outputFormatContext, int streamIndex) {
//     av_packet_rescale_ts(packet, inputFormatContext->streams[streamIndex]->time_base, 
//                          outputFormatContext->streams[streamIndex]->time_base);
//     return av_interleaved_write_frame(outputFormatContext, packet);
// }



// int main(int argc, char *argv[]) {
//     // if (argc != 3) {
//     //     fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
//     //     return -1;
//     // }
//     int ret;

//     AVFormatContext *inputFormatContext = NULL;

//     /* Open Video File */
//     ret = avformat_open_input(&inputFormatContext, "pong.mov", NULL, NULL);
//     if (ret < 0) {
//         fprintf(stderr, "Couldn't open input file: %s\n", "pong.mov");
//         return ret;
//     }

//     /* Analyze File To Recieve Stream Information */
//     ret = avformat_find_stream_info(inputFormatContext, NULL);
//     if (ret < 0) {
//         fprintf(stderr, "Couldn't retrieve stream information of file\n");
//         return ret;
//     }

//     StreamCodecContext *streamContext = av_calloc(inputFormatContext->nb_streams, sizeof(StreamCodecContext));
//     if (!streamContext) {
//         fprintf(stderr, "Couldn't allocate StreamCodecContext object\n");
//         return AVERROR(ENOMEM);
//     }

//     if (setUpDecoders(streamContext, inputFormatContext, "pong.mov") < 0)
//         goto cleanUp;

//     /* Allocate output format context */
//     AVFormatContext *outputFormatContext = NULL;
//     avformat_alloc_output_context2(&outputFormatContext, NULL, NULL, "test.mov");
//     if (!outputFormatContext) {
//         printf("Couldn't allocate output context\n");
//         return -1;
//     }

//     if (setUpEncoders(streamContext, inputFormatContext, outputFormatContext, "test.mov") < 0)
//         goto cleanUp;


//     /* init muxer, write output file header */
//     ret = avformat_write_header(outputFormatContext, NULL);
//     if (ret < 0) {
//         fprintf(stderr, "Error occurred when opening output file\n");
//         return ret;
//     }


//     /* Read Packets */
//     AVPacket *packet = av_packet_alloc();
//     if (!packet) {
//         fprintf(stderr, "Couldn't allocate packet\n");
//         return -1;
//     }

//     while (av_read_frame(inputFormatContext, packet) >= 0) {
//         int streamIndex = packet->stream_index;
//         StreamCodecContext *stream = &streamContext[streamIndex];
        
//         if (inputFormatContext->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
//             // try to decode one image frame
//             ret = decodeVideo(packet, inputFormatContext, outputFormatContext, stream, streamIndex);
//             if (ret < 0) return ret;
//             // ret = remux(packet, inputFormatContext, outputFormatContext, streamIndex);
//             // if (ret < 0) break;
//         } else {
//             ret = remux(packet, inputFormatContext, outputFormatContext, streamIndex);
//             if (ret < 0) break;
//         }

//         av_packet_unref(packet);
//     }









//     printf("Time to flush the decoders and encoders\n");

//     /* flush decoders and encoders */
//     // for (int i = 0; i < inputFormatContext->nb_streams; i++) {
//     //     StreamCodecContext *stream = &streamContext[i];

//     //     if (inputFormatContext->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
//     //         continue;

//     //     ret = avcodec_send_packet(stream->decoderContext, NULL);
//     //     if (ret < 0)
//     //         goto cleanUp;
        
//     //     while (ret >= 0) {
//     //         ret = avcodec_receive_frame(stream->decoderContext, stream->decoderFrame);
//     //         if (ret == AVERROR_EOF)
//     //             break;
//     //         else if (ret < 0)
//     //             goto cleanUp;

//     //         stream->decoderFrame->pts = stream->decoderFrame->best_effort_timestamp;

//     //         av_packet_unref(packet);
//     //         ret = avcodec_send_frame(stream->encoderContext, stream->decoderFrame);
//     //         if (ret < 0) return ret;

//     //         while (ret >= 0) {
//     //             ret = avcodec_receive_packet(stream->encoderContext, packet);
//     //             if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
//     //                 break;
//     //             packet->stream_index = i;
//     //             av_packet_rescale_ts(packet, stream->encoderContext->time_base, 
//     //                              outputFormatContext->streams[i]->time_base);
//     //             ret = av_interleaved_write_frame(outputFormatContext, packet);
//     //         }

//     //         av_frame_unref(stream->decoderFrame);
//     //         if (ret < 0) return ret;
//     //     }

//     //     av_packet_unref(packet);
 
//     //     ret = avcodec_send_frame(stream->encoderContext, NULL);
//     //     if (ret < 0) return ret;

//     //     while (ret >= 0) {
//     //         ret = avcodec_receive_packet(stream->encoderContext, packet);
//     //         if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
//     //             break;
//     //         packet->stream_index = i;
//     //         av_packet_rescale_ts(packet, stream->encoderContext->time_base, 
//     //                              outputFormatContext->streams[i]->time_base);
//     //         ret = av_interleaved_write_frame(outputFormatContext, packet);
//     //     }
//     // }





//     av_write_trailer(outputFormatContext);

//     printf("Done writing to outfile\n");

//     /* Clean up */
// cleanUp:
//     av_packet_free(&packet);

//     for (int i = 0; i < inputFormatContext->nb_streams; i++) {
//         avcodec_free_context(&streamContext[i].decoderContext);
//         if (outputFormatContext && outputFormatContext->nb_streams > i && 
//             outputFormatContext->streams[i] && streamContext[i].encoderContext)
//             avcodec_free_context(&streamContext[i].encoderContext);
//         av_frame_free(&streamContext[i].prevDecoderFrame);
//         av_frame_free(&streamContext[i].decoderFrame);
//     }
//     av_free(streamContext);

//     avformat_close_input(&inputFormatContext);

//     if (outputFormatContext && !(outputFormatContext->oformat->flags & AVFMT_NOFILE))
//         avio_closep(&outputFormatContext->pb);

//     avformat_free_context(outputFormatContext);

//     return 0;
// }