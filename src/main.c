#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "types.h"
#include "algorithms.h"



int process_video(AVFormatContext *iformat_context, AVFormatContext *oformat_context, 
                  VideoCodec *video_codec , AVPacket *packet, int stream_index);

int mux(AVFormatContext *iformat_context, AVFormatContext *oformat_context, 
        AVPacket *packet, int stream_index);

int encode_video(AVFormatContext *iformat_context, AVFormatContext *oformat_context, 
                 VideoCodec *video_codec , AVPacket *packet, int stream_index);




int main(int argc, char *argv[]) {
    if (argc != 3) {
    fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        return -1;
    }

    AVFormatContext *iformat_context = NULL;
    AVFormatContext *oformat_context = NULL;
    VideoCodec video_codec = { 0 };
    AVPacket *packet = NULL;

    int ret = avformat_open_input(&iformat_context, argv[1], NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to open input file\n");
        goto cleanup;
    }

    ret = avformat_find_stream_info(iformat_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to read input file and find stream information\n");
        goto cleanup;
    }

    av_dump_format(iformat_context, 0, argv[1], 0);

    ret = avformat_alloc_output_context2(&oformat_context, NULL, NULL, argv[2]);
    if (ret < 0){
        fprintf(stderr, "ERROR:   Failed to allocate output context\n");
        goto cleanup;
    }

    if (!(oformat_context->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&oformat_context->pb, argv[2], AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "ERROR:   Could not open output file '%s'", argv[2]);
            goto cleanup;
        }
    }

    video_codec.frame = av_frame_alloc();
    if (video_codec.frame == NULL) {
        fprintf(stderr, "ERROR:   Couldn't allocate frame\n");
        goto cleanup;
    }

    int video_stream = -1;

    for (unsigned int i = 0; i < iformat_context->nb_streams; i++) {
        AVStream *stream = avformat_new_stream(oformat_context, NULL);
        if (stream == NULL) {
            fprintf(stderr, "ERROR:   Failed to add a new stream to the output file\n");
            goto cleanup;
        }

        ret = avcodec_parameters_copy(stream->codecpar, iformat_context->streams[i]->codecpar);
        if (ret < 0) {
            fprintf(stderr, "ERROR:   Failed to copy codec params form the input to the output stream\n");
            goto cleanup;
        }

        if (iformat_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream = i;
        }
    }
    
    if (video_stream < 0) {
        fprintf(stderr, "ERROR:   Program only supports video files with one video stream\n");
        goto cleanup;
    }

    const AVCodec *decoder = avcodec_find_decoder(iformat_context->streams[video_stream]->codecpar->codec_id);
    if (decoder == NULL) {
        fprintf(stderr, "ERROR:   Couldn't find the encoder/decoder\n");
        goto cleanup;
    }

    AVCodecContext *decoder_context = avcodec_alloc_context3(decoder);
    if (decoder_context == NULL) {
        fprintf(stderr, "ERROR:   Couldn't allocate the decoder context with the given decoder\n");
        goto cleanup;
    }

    ret = avcodec_parameters_to_context(decoder_context, iformat_context->streams[video_stream]->codecpar);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to fill the decoder context based on the values from codec parameters\n");
        goto cleanup;
    }

    ret = avcodec_open2(decoder_context, decoder, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to open the decoder context\n");
        goto cleanup;
    }

    if (decoder_context->pix_fmt != AV_PIX_FMT_YUV420P) {
        fprintf(stderr, "ERROR:   Video has a pixel format that is not supported by this program\n");
        goto cleanup;
    }

    video_codec.decoder_context = decoder_context;

    const AVCodec *encoder = avcodec_find_encoder(iformat_context->streams[video_stream]->codecpar->codec_id);
    if (encoder == NULL) {
        fprintf(stderr, "ERROR:   Couldn't find the encoder/decoder\n");
        goto cleanup;
    }

    AVCodecContext *encoder_context = avcodec_alloc_context3(encoder);
    if (encoder_context == NULL) {
        fprintf(stderr, "ERROR:   Couldn't allocate the encoder context with the given codec\n");
        goto cleanup;
    }

    ret = avcodec_parameters_to_context(encoder_context, iformat_context->streams[video_stream]->codecpar);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to fill the encoder context based on the values from codec parameters\n");
        goto cleanup;
    }

    encoder_context->time_base = av_inv_q(av_guess_frame_rate(iformat_context, iformat_context->streams[video_stream], NULL));

    if (oformat_context->oformat->flags & AVFMT_GLOBALHEADER)
        encoder_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2(encoder_context, encoder, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to open the encoder context\n");
        goto cleanup;
    }

    ret = avcodec_parameters_from_context(oformat_context->streams[video_stream]->codecpar, encoder_context);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed avcodec_parameters_from_context()\n");
        goto cleanup;
    }

    video_codec.encoder_context = encoder_context;

    av_dump_format(oformat_context, 0, "newtest.mov", 1);

    ret = avformat_write_header(oformat_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to write header to the output file\n");
        goto cleanup;
    }

    packet = av_packet_alloc();
    if (packet == NULL) {
        fprintf(stderr, "ERROR:   Couldn't allocate packet\n");
        goto cleanup;
    }

    while (av_read_frame(iformat_context, packet) >= 0) {
        int streamIndex = packet->stream_index;

        if (iformat_context->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            ret = avcodec_send_packet(video_codec.decoder_context, packet);
            if (ret < 0) {
                fprintf(stderr, "ERROR:   Failed sending packet to decoder\n");
                goto cleanup;
            }

            ret = process_video(iformat_context, oformat_context, &video_codec, packet, streamIndex);
            if (ret < 0) goto cleanup;
        } 
        else {
            ret = mux(iformat_context, oformat_context, packet, streamIndex);
            if (ret < 0) goto cleanup;
        }

        av_packet_unref(packet);
    }

    ret = avcodec_send_packet(video_codec.decoder_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to flush decoder\n");
        goto cleanup;
    }
    ret = process_video(iformat_context, oformat_context, &video_codec, packet, video_stream);
    if (ret < 0) goto cleanup;
    

    ret = avcodec_send_frame(video_codec.encoder_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to flush encoder\n");
        goto cleanup;
    }
    ret = encode_video(iformat_context, oformat_context, &video_codec, packet, video_stream);
    if (ret < 0) goto cleanup;


    ret = av_write_trailer(oformat_context);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Couldn't wirte output file trailer\n");
        goto cleanup;
    }

cleanup:
    av_packet_free(&packet);
    avcodec_free_context(&video_codec.decoder_context);
    avcodec_free_context(&video_codec.encoder_context);
    av_frame_free(&video_codec.frame);
    for (int i = 0; i < 8; i++) {
        free(video_codec.inverted_data[i]);
    }
    if (oformat_context && !(oformat_context->oformat->flags & AVFMT_NOFILE))
        avio_closep(&oformat_context->pb);
    avformat_free_context(oformat_context);
    avformat_close_input(&iformat_context);

    return ret;
}




int process_video(AVFormatContext *iformat_context, AVFormatContext *oformat_context, 
                  VideoCodec *video_codec , AVPacket *packet, int stream_index) {
    int ret;
    while (1) {

        if (video_codec->frame == NULL) {
            video_codec->frame = av_frame_alloc();
        }
        if (video_codec->frame == NULL) {
            fprintf(stderr, "ERROR:   Couldn't allocate frame\n");
            return AVERROR(ENOMEM);
        }

        ret = avcodec_receive_frame(video_codec->decoder_context, video_codec->frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            fprintf(stderr, "ERROR:   Failed recieving frame from decoder\n");
            return ret;
        }

        video_codec->frame->pts = video_codec->frame->best_effort_timestamp;

        overlay_frames_yuv420p(video_codec);

        ret = avcodec_send_frame(video_codec->encoder_context, video_codec->frame);
        if (ret < 0) {
            fprintf(stderr, "ERROR:   Failed sending frame to encoder, (error: %s)\n", av_err2str(ret));
            return ret;
        }

        ret = encode_video(iformat_context, oformat_context, video_codec, packet, stream_index);
        if (ret < 0) return ret;
    }

    return 0;
}



int encode_video(AVFormatContext *iformat_context, AVFormatContext *oformat_context, 
                 VideoCodec *video_codec , AVPacket *packet, int stream_index) {
    int ret;
    while (1) {
        av_packet_unref(packet);

        ret = avcodec_receive_packet(video_codec->encoder_context, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            fprintf(stderr, "ERROR:   Failed recieving packet from encoder\n");
            return ret;
        }

        ret = mux(iformat_context, oformat_context, packet, stream_index);
        if (ret < 0) return ret;
    }

    // av_frame_unref(video_codec->frame);
    av_frame_free(&video_codec->frame);
    video_codec->frame = NULL;

    return 0;
}



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