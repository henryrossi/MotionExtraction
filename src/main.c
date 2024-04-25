#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "extraction.h"
#include "queue.h"

struct parameters {
        char *ifile;
        char *ofile;
        int delay;
};

static int64_t duration;
static AVRational time_base;
static int64_t max_pts = -1;
static time_t last_time = -1;
static time_t cur_time = -1;

static int help_check(int argc, char *argv[]) {
        for (int i = 0; i < argc; i++) {
                if (strcmp("-h", argv[i]) == 0 ||
                    strcmp("--help", argv[i]) == 0) {
                        // "Usage: %s <input file> <output file>\n",
                        printf("Here's the help display...\n");
                        return 1;
                }
        }
        if (argc == 1) {
                printf("Here's the help display...\n");
                return 1;
        }
        return 0;
}

static int parse_args(int argc, char *argv[], struct parameters *params) {
        // moex

        // ./ME, ./ME -h, and ./ME --help all display help info
        // --freeze, -f set delay to 0
        // --delay <num> sets delay to num

        // if user doesn't pass an argument prompt them!

        // prompt for conformation if output file is being overwritten

        // If possible, make arguments, flags and subcommands order-independent.

        int frozen = 0;
        int delay = 0;

        int ret = help_check(argc, argv);
        if (ret != 0) {
                return ret;
        }

        for (int i = 1; i < argc; i++) {
                if (argv[i][0] == '-') {
                        if (strcmp("-f", argv[i]) == 0 ||
                            strcmp("--freeze", argv[i]) == 0) {
                                frozen = 1;
                                params->delay = 0;
                        } else if (strcmp("--delay", argv[i]) == 0) {
                                if (i + 1 >= argc || argv[i + 1][0] == '-') {
                                        // do nothing?
                                        // prompt user for number
                                        printf("\033[93mWarning! \033[0m --delay flag is ignored since it's not followed by a number\n");
                                        continue;
                                }

                                if (strcmp("0", argv[i + 1]) == 0) {
                                        delay = 1;
                                        params->delay = 0;
                                        i++;
                                        continue;
                                }

                                int val = strtol(argv[i + 1], NULL, 10);
                                if (val == 0) {
                                        fprintf(stderr, "\033[91mError!\033[0m --delay flag must be followed by a number, for example: --delay 10\n"
                                               "You entered: --delay %s\n", argv[i + 1]);
                                        return -1;
                                }
                                delay = 1;
                                params->delay = val;
                                i++;
                        }
                } else {
                        if (params->ifile == NULL) {
                                params->ifile = argv[i];
                        } else if (params->ofile == NULL) {
                                params->ofile = argv[i];
                        }
                }
        }

        if (frozen == 1 && delay == 1) {
                params->delay = 0;
                printf("\033[93mWarning! \033[0mThe --frozen (-f) flag takes precedent over the --delay flag when both are used.\n");
        }

        return 0;
}

static int open_input_file(AVFormatContext **ifmt, const char *filename) {
        int ret = avformat_open_input(ifmt, filename, NULL, NULL);
        if (ret < 0) {
                fprintf(stderr, "ERROR:   Failed to open input file\n");
                return ret;
        }

        ret = avformat_find_stream_info(*ifmt, NULL);
        if (ret < 0) {
                fprintf(stderr,
                        "ERROR:   Failed to read and analyze input file\n");
                return ret;
        }

        return 0;
}

static int open_output_file(AVFormatContext **ofmt, const char *filename) {
        int ret = avformat_alloc_output_context2(ofmt, NULL, NULL, filename);
        if (ret < 0) {
                fprintf(stderr, "ERROR:   Failed to allocate output context\n");
                return ret;
        }

        if (!((*ofmt)->oformat->flags & AVFMT_NOFILE)) {
                ret = avio_open(&(*ofmt)->pb, filename, AVIO_FLAG_WRITE);
                if (ret < 0) {
                        fprintf(stderr,
                                "ERROR:   Could not open output file '%s'",
                                filename);
                        return ret;
                }
        }

        return 0;
}

static int create_output_streams(AVFormatContext *ifmt, AVFormatContext *ofmt,
                                 int *video_stream) {
        for (unsigned int i = 0; i < ifmt->nb_streams; i++) {
                AVStream *stream = avformat_new_stream(ofmt, NULL);
                if (stream == NULL) {
                        fprintf(stderr, "ERROR:   Failed to add stream to the "
                                        "output file\n");
                        return -1;
                }

                int ret = avcodec_parameters_copy(stream->codecpar,
                                                  ifmt->streams[i]->codecpar);
                if (ret < 0) {
                        fprintf(stderr, "ERROR:   Failed to copy codec params "
                                        "to out stream\n");
                        return -1;
                }

                if (ifmt->streams[i]->codecpar->codec_type ==
                    AVMEDIA_TYPE_VIDEO) {
                        *video_stream = i;
                }

                duration = ifmt->streams[i]->duration;
                time_base = ifmt->streams[i]->time_base;
        }

        if (*video_stream < 0) {
                fprintf(stderr, "ERROR:   Program only supports video files "
                                "with one video stream\n");
                return -1;
        }

        return 0;
}

static int configure_decoder(AVFormatContext *ifmt,
                             AVCodecContext **decoder_ctx, int video) {
        const AVCodec *decoder =
            avcodec_find_decoder(ifmt->streams[video]->codecpar->codec_id);
        if (decoder == NULL) {
                fprintf(stderr, "ERROR:   Couldn't find the encoder/decoder\n");
                return -1;
        }

        (*decoder_ctx) = avcodec_alloc_context3(decoder);
        if ((*decoder_ctx) == NULL) {
                fprintf(
                    stderr,
                    "ERROR:   Couldn't allocate the decoder context with the "
                    "given decoder\n");
                return -1;
        }

        int ret = avcodec_parameters_to_context((*decoder_ctx),
                                                ifmt->streams[video]->codecpar);
        if (ret < 0) {
                fprintf(
                    stderr,
                    "ERROR:   Failed to fill the decoder context based on the "
                    "values from codec parameters\n");
                return ret;
        }

        ret = avcodec_open2((*decoder_ctx), decoder, NULL);
        if (ret < 0) {
                fprintf(stderr,
                        "ERROR:   Failed to open the decoder context\n");
                return ret;
        }

        if ((*decoder_ctx)->pix_fmt != AV_PIX_FMT_YUV420P) {
                fprintf(
                    stderr,
                    "ERROR:   Video has a pixel format that is not supported "
                    "by this program\n");
                return -1;
        }

        return 0;
}

static int configure_encoder(AVFormatContext *ifmt, AVFormatContext *ofmt,
                             AVCodecContext **encoder_ctx,
                             AVCodecContext *decoder_ctx, int video) {
        const AVCodec *encoder =
            avcodec_find_encoder(ofmt->streams[video]->codecpar->codec_id);
        if (encoder == NULL) {
                fprintf(stderr, "ERROR:   Couldn't find the encoder/decoder\n");
                return -1;
        }

        (*encoder_ctx) = avcodec_alloc_context3(encoder);
        if ((*encoder_ctx) == NULL) {
                fprintf(
                    stderr,
                    "ERROR:   Couldn't allocate the encoder context with the "
                    "given codec\n");
                return -1;
        }

        (*encoder_ctx)->height = decoder_ctx->height;
        (*encoder_ctx)->width = decoder_ctx->width;
        (*encoder_ctx)->sample_aspect_ratio = decoder_ctx->sample_aspect_ratio;
        (*encoder_ctx)->pix_fmt = decoder_ctx->pix_fmt;
        (*encoder_ctx)->time_base =
            av_inv_q(av_guess_frame_rate(ifmt, ifmt->streams[video], NULL));

        if (ofmt->oformat->flags & AVFMT_GLOBALHEADER)
                (*encoder_ctx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        int ret = avcodec_open2((*encoder_ctx), encoder, NULL);
        if (ret < 0) {
                fprintf(stderr,
                        "ERROR:   Failed to open the encoder context\n");
                return ret;
        }

        ret = avcodec_parameters_from_context(ofmt->streams[video]->codecpar,
                                              (*encoder_ctx));
        if (ret < 0) {
                fprintf(stderr,
                        "ERROR:   Failed avcodec_parameters_from_context()\n");
                return ret;
        }

        return 0;
}

static void print_report(int64_t pts) {
        if (pts > max_pts) {
                max_pts = pts;
        } else {
                return;
        }

        time(&cur_time);
        if (cur_time - last_time < 1) {
                return;
        }


        int perc = pts * 100 / duration;
        printf("\033[1A\33[2K\rProgress: %02d%%   ", perc);

        int us = pts * time_base.num % time_base.den;
        int secs = pts * time_base.num / time_base.den % 60;
        int mins = pts * time_base.num / time_base.den / 60 % 60;
        int hours = pts * time_base.num / time_base.den / 3600;
        printf("Time: %02d:%02d:%02d.%02d\n", hours, mins, secs,
               (100 * us * time_base.num) / time_base.den);

        last_time = cur_time;
}

static int mux(AVFormatContext *iformat_context,
               AVFormatContext *oformat_context, AVPacket *packet) {
        print_report(packet->pts);

        av_packet_rescale_ts(
            packet, iformat_context->streams[packet->stream_index]->time_base,
            oformat_context->streams[packet->stream_index]->time_base);

        int ret = av_interleaved_write_frame(oformat_context, packet);
        if (ret < 0) {
                fprintf(stderr,
                        "ERROR:   Failed to write packet to output file\n");
                return ret;
        }
        return 0;
}

static int recieve_packets(AVCodecContext *encoder_ctx, AVPacket *packet,
                           AVFormatContext *ifmt_ctx,
                           AVFormatContext *ofmt_ctx) {
        int ret;
        while (1) {
                av_packet_unref(packet);

                ret = avcodec_receive_packet(encoder_ctx, packet);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        return 0;
                if (ret < 0) {
                        fprintf(stderr, "ERROR:   Failed recieving "
                                        "packet from encoder\n");
                        return ret;
                }

                ret = mux(ifmt_ctx, ofmt_ctx, packet);
                if (ret < 0)
                        return ret;
        }
}

static int recieve_frames(AVCodecContext *decoder_ctx,
                          AVCodecContext *encoder_ctx, AVFrame **frame,
                          struct frame_queue *q, AVPacket *packet,
                          AVFormatContext *ifmt_ctx,
                          AVFormatContext *ofmt_ctx) {
        int ret;
        while (1) {
                ret = avcodec_receive_frame(decoder_ctx, (*frame));
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        return 0;
                if (ret < 0) {
                        fprintf(stderr, "ERROR:   Failed recieving "
                                        "frame from decoder\n");
                        return ret;
                }

                (*frame)->pts = (*frame)->best_effort_timestamp;

                (*frame) = overlay_frames_yuv420p(frame, q);

                ret = avcodec_send_frame(encoder_ctx, (*frame));
                if (ret < 0) {
                        fprintf(stderr,
                                "ERROR:   Failed sending frame "
                                "to encoder, "
                                "(error: %s)\n",
                                av_err2str(ret));
                        return ret;
                }

                ret = recieve_packets(encoder_ctx, packet, ifmt_ctx, ofmt_ctx);
                if (ret < 0) {
                        return ret;
                }

                av_frame_unref((*frame));
        }
}

int main(int argc, char *argv[]) {
        av_log_set_level(AV_LOG_QUIET);

        struct parameters params = {.delay = 2};

        int ret = parse_args(argc, argv, &params);
        switch (ret) {
        case 1:
                return 0;
        case 0:
                break;
        default:
                return ret;
        }

        printf("\033[1mMotion Extracting\033[0m\n"
               "  Input Video: %s\n"
               "  Output Video: %s\n",
               params.ifile, params.ofile);
        char *fr_msg = "";
        if (params.delay == 0) {
                fr_msg = "(Frozen)";
        }
        printf("  delay: %d %s\n\n", params.delay, fr_msg);

        AVFormatContext *ifmt_ctx = NULL;
        AVFormatContext *ofmt_ctx = NULL;
        AVCodecContext *decoder_ctx = NULL;
        AVCodecContext *encoder_ctx = NULL;
        struct frame_queue *q = init_queue(params.delay);
        AVPacket *packet = NULL;
        AVFrame *frame = NULL;
        int video_stream = -1;

        ret = open_input_file(&ifmt_ctx, params.ifile);
        if (ret < 0) {
                goto cleanup;
        }

        // av_log_set_level(AV_LOG_INFO);
        // av_dump_format(ifmt_ctx, 0, params.ifile, 0);
        // av_log_set_level(AV_LOG_FATAL);

        ret = open_output_file(&ofmt_ctx, params.ofile);
        if (ret < 0) {
                goto cleanup;
        }

        ret = create_output_streams(ifmt_ctx, ofmt_ctx, &video_stream);
        if (ret < 0) {
                goto cleanup;
        }

        ret = configure_decoder(ifmt_ctx, &decoder_ctx, video_stream);
        if (ret < 0) {
                goto cleanup;
        }

        ret = configure_encoder(ifmt_ctx, ofmt_ctx, &encoder_ctx, decoder_ctx,
                                video_stream);
        if (ret < 0) {
                goto cleanup;
        }

        // av_log_set_level(AV_LOG_INFO);
        // av_dump_format(ofmt_ctx, 0, params.ofile, 1);
        // av_log_set_level(AV_LOG_FATAL);

        ret = avformat_write_header(ofmt_ctx, NULL);
        if (ret < 0) {
                fprintf(stderr,
                        "ERROR:   Failed to write header to the output file\n");
                goto cleanup;
        }

        packet = av_packet_alloc();
        if (packet == NULL) {
                fprintf(stderr, "ERROR:   Couldn't allocate packet\n");
                goto cleanup;
        }
        frame = av_frame_alloc();
        if (frame == NULL) {
                fprintf(stderr, "ERROR:   Couldn't allocate frame\n");
                goto cleanup;
        }

        while (av_read_frame(ifmt_ctx, packet) >= 0) {
                int stream_index = packet->stream_index;

                if (ifmt_ctx->streams[stream_index]->codecpar->codec_type !=
                    AVMEDIA_TYPE_VIDEO) {
                        ret = mux(ifmt_ctx, ofmt_ctx, packet);
                        if (ret < 0) {
                                goto cleanup;
                        }
                } else {
                        ret = avcodec_send_packet(decoder_ctx, packet);
                        if (ret < 0) {
                                fprintf(stderr, "ERROR:   Failed sending "
                                                "packet to decoder\n");
                                goto cleanup;
                        }
                        ret = recieve_frames(decoder_ctx, encoder_ctx, &frame,
                                             q, packet, ifmt_ctx, ofmt_ctx);
                        if (ret < 0) {
                                goto cleanup;
                        }
                }

                av_packet_unref(packet);
        }

        ret = avcodec_send_packet(decoder_ctx, NULL);
        if (ret < 0) {
                fprintf(stderr, "ERROR:   Failed to flush decoder\n");
                goto cleanup;
        }
        ret = recieve_frames(decoder_ctx, encoder_ctx, &frame, q, packet,
                             ifmt_ctx, ofmt_ctx);
        if (ret < 0)
                goto cleanup;

        ret = avcodec_send_frame(encoder_ctx, NULL);
        if (ret < 0) {
                fprintf(stderr, "ERROR:   Failed to flush encoder\n");
                goto cleanup;
        }
        ret = recieve_packets(encoder_ctx, packet, ifmt_ctx, ofmt_ctx);
        if (ret < 0)
                goto cleanup;

        ret = av_write_trailer(ofmt_ctx);
        if (ret < 0) {
                fprintf(stderr,
                        "ERROR:   Couldn't wirte output file trailer\n");
                goto cleanup;
        }


        int perc = 100;
        printf("\033[1A\33[2K\rProgress: %02d%%   ", perc);

        int us = duration * time_base.num % time_base.den;
        int secs = duration * time_base.num / time_base.den % 60;
        int mins = duration * time_base.num / time_base.den / 60 % 60;
        int hours = duration * time_base.num / time_base.den / 3600;
        printf("Time: %02d:%02d:%02d.%02d\n", hours, mins, secs,
               (100 * us * time_base.num) / time_base.den);


cleanup:
        av_packet_free(&packet);
        av_frame_free(&frame);
        free_queue(q);
        avcodec_free_context(&decoder_ctx);
        avcodec_free_context(&encoder_ctx);
        if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
                avio_closep(&ofmt_ctx->pb);
        avformat_free_context(ofmt_ctx);
        avformat_close_input(&ifmt_ctx);

        return ret;
}
