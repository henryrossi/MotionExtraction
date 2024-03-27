#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

typedef struct VideoCodec {
    AVCodecContext *decoder_context;
    AVCodecContext *encoder_context;
    AVFrame *frame;
    uint8_t *inverted_data[8];
} VideoCodec;

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

void overlay_frames_yuv420p(VideoCodec *video_codec) {
    /* Y */
    for (int y = 0; y < video_codec->frame->height; y++) {
        for (int x = 0; x < video_codec->frame->linesize[0]; x++) {
            int byte = (video_codec->inverted_data[0][y * video_codec->frame->linesize[0] + x] +
                video_codec->frame->data[0][y * video_codec->frame->linesize[0] + x]) / 2;
            if (byte > 255) {
                byte = 255;
            }
            video_codec->frame->data[0][y * video_codec->frame->linesize[0] + x] = byte;
        }
    }

    /* Cb and Cr */
    for (int y = 0; y < video_codec->frame->height/2; y++) {
        for (int x = 0; x < video_codec->frame->linesize[1]; x++) {
            int byte = (video_codec->inverted_data[1][y * video_codec->frame->linesize[1] + x] +
                video_codec->frame->data[1][y * video_codec->frame->linesize[1] + x]) / 2;
            if (byte > 255) {
                byte = 255;
            }   
            video_codec->frame->data[1][y * video_codec->frame->linesize[1] + x] = byte;

            byte = (video_codec->inverted_data[2][y * video_codec->frame->linesize[2] + x] +
                video_codec->frame->data[2][y * video_codec->frame->linesize[2] + x]) / 2;
            if (byte > 255) {
                byte = 255;
            }
            video_codec->frame->data[2][y * video_codec->frame->linesize[2] + x] = byte;
        }
    }
}

int write_inverted_frame_yuv420p(VideoCodec *video_codec) {
    for (int i = 0; i < 3; i++) {
        printf("The frame->data[%d] has a width of %d, height of %d, and linesize of %d\n",
                i, video_codec->frame->width, video_codec->frame->height, 
                video_codec->frame->linesize[i]);
    }

    // linesize is the width of your image in memory for each color channel. 
    // It may greater or equal to w, for memory alignment issue.
    video_codec->inverted_data[0] = (uint8_t *) malloc(sizeof(uint8_t) * 
                    video_codec->frame->linesize[0] * video_codec->frame->height);
    video_codec->inverted_data[1] = (uint8_t *) malloc(sizeof(uint8_t) * 
                    video_codec->frame->linesize[1] * video_codec->frame->height);
    video_codec->inverted_data[2] = (uint8_t *) malloc(sizeof(uint8_t) * 
                    video_codec->frame->linesize[2] * video_codec->frame->height);
    
    if (video_codec->inverted_data[0] == NULL || video_codec->inverted_data[1] == NULL || 
        video_codec->inverted_data[2] == NULL) {
        fprintf(stderr, "ERROR:   Failed to allocate memory for inverted frames\n");
        return -1;
    }

    /* Y */
    for (int y = 0; y < video_codec->frame->height; y++) {
        for (int x = 0; x < video_codec->frame->linesize[0]; x++) {
            video_codec->inverted_data[0][y * video_codec->frame->linesize[0] + x] =
                255 - video_codec->frame->data[0][y * video_codec->frame->linesize[0] + x];
        }
    }

    /* Cb and Cr */
    for (int y = 0; y < video_codec->frame->height/2; y++) {
        for (int x = 0; x < video_codec->frame->linesize[1]; x++) {
            video_codec->inverted_data[1][y * video_codec->frame->linesize[1] + x] =
                255 - video_codec->frame->data[1][y * video_codec->frame->linesize[1] + x];
            video_codec->inverted_data[2][y * video_codec->frame->linesize[2] + x] =
                255 - video_codec->frame->data[2][y * video_codec->frame->linesize[2] + x];
        }
    }
    return 0;
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

        if (video_codec->inverted_data[0] == NULL) {
            ret = write_inverted_frame_yuv420p(video_codec);
            if (ret < 0) return ret;
        }

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

int main(int argc, char *argv[]) {
    if (argc != 3) {
    fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        return -1;
    }

    AVFormatContext *iformat_context = NULL;

    int ret = avformat_open_input(&iformat_context, argv[1], NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to open input file\n");
        return ret;
    }

    ret = avformat_find_stream_info(iformat_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to read input file and find stream information\n");
        return ret;
    }

    av_dump_format(iformat_context, 0, argv[1], 0);

    AVFormatContext *oformat_context = NULL;
    ret = avformat_alloc_output_context2(&oformat_context, NULL, NULL, argv[2]);
    if (ret < 0){
        fprintf(stderr, "ERROR:   Failed to allocate output context\n");
        return ret;
    }

    if (!(oformat_context->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&oformat_context->pb, argv[2], AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "ERROR:   Could not open output file '%s'", argv[2]);
            return ret;
        }
    }

    VideoCodec video_codec = { 0 };
    video_codec.frame = av_frame_alloc();
    if (video_codec.frame == NULL) {
        fprintf(stderr, "ERROR:   Couldn't allocate frame\n");
        return AVERROR(ENOMEM);
    }

    int video_stream = -1;

    for (unsigned int i = 0; i < iformat_context->nb_streams; i++) {
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
    
    if (video_stream < 0) {
        fprintf(stderr, "ERROR:   Program only supports video files with one video stream\n");
        return -1;
    }

    const AVCodec *decoder = avcodec_find_decoder(iformat_context->streams[video_stream]->codecpar->codec_id);
    if (decoder == NULL) {
        fprintf(stderr, "ERROR:   Couldn't find the encoder/decoder\n");
        return -1;
    }

    AVCodecContext *decoder_context = avcodec_alloc_context3(decoder);
    if (decoder_context == NULL) {
        fprintf(stderr, "ERROR:   Couldn't allocate the decoder context with the given decoder\n");
        return -1;
    }

    ret = avcodec_parameters_to_context(decoder_context, iformat_context->streams[video_stream]->codecpar);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to fill the decoder context based on the values from codec parameters\n");
        return ret;
    }

    ret = avcodec_open2(decoder_context, decoder, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to open the decoder context\n");
        return ret;
    }

    if (decoder_context->pix_fmt != AV_PIX_FMT_YUV420P) {
        fprintf(stderr, "ERROR:   Video has a pixel format that is not supported by this program\n");
        return -1;
    }

    video_codec.decoder_context = decoder_context;

    const AVCodec *encoder = avcodec_find_encoder(iformat_context->streams[video_stream]->codecpar->codec_id);
    if (encoder == NULL) {
        fprintf(stderr, "ERROR:   Couldn't find the encoder/decoder\n");
        return -1;
    }

    AVCodecContext *encoder_context = avcodec_alloc_context3(encoder);
    if (encoder_context == NULL) {
        fprintf(stderr, "ERROR:   Couldn't allocate the encoder context with the given codec\n");
        return -1;
    }

    ret = avcodec_parameters_to_context(encoder_context, iformat_context->streams[video_stream]->codecpar);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to fill the encoder context based on the values from codec parameters\n");
        return ret;
    }

    encoder_context->time_base = av_inv_q(av_guess_frame_rate(iformat_context, iformat_context->streams[video_stream], NULL));

    if (oformat_context->oformat->flags & AVFMT_GLOBALHEADER)
        encoder_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2(encoder_context, encoder, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to open the encoder context\n");
        return ret;
    }

    ret = avcodec_parameters_from_context(oformat_context->streams[video_stream]->codecpar, encoder_context);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed avcodec_parameters_from_context()\n");
        return ret;
    }

    video_codec.encoder_context = encoder_context;

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
            ret = avcodec_send_packet(video_codec.decoder_context, packet);
            if (ret < 0) {
                fprintf(stderr, "ERROR:   Failed sending packet to decoder\n");
                return ret;
            }

            ret = process_video(iformat_context, oformat_context, &video_codec, packet, streamIndex);
            if (ret < 0) return ret;
        } 
        else {
            ret = mux(iformat_context, oformat_context, packet, streamIndex);
            if (ret < 0) return ret;
        }

        av_packet_unref(packet);
    }

    ret = avcodec_send_packet(video_codec.decoder_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to flush decoder\n");
        return ret;
    }
    ret = process_video(iformat_context, oformat_context, &video_codec, packet, video_stream);
    if (ret < 0) return ret;
    

    ret = avcodec_send_frame(video_codec.encoder_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed to flush encoder\n");
        return ret;
    }
    ret = encode_video(iformat_context, oformat_context, &video_codec, packet, video_stream);
    if (ret < 0) return ret;


    ret = av_write_trailer(oformat_context);
    if (ret < 0) {
        fprintf(stderr, "ERROR:   Couldn't wirte output file trailer\n");
        return ret;
    }

    av_packet_free(&packet);
    avcodec_free_context(&video_codec.decoder_context);
    avcodec_free_context(&video_codec.encoder_context);
    av_frame_free(&video_codec.frame);
    for (int i = 0; i < 8; i++) {
        printf("%p\n", video_codec.inverted_data[i]);
        if (video_codec.inverted_data[i] != NULL)
            free(video_codec.inverted_data[i]);
    }
    if (oformat_context && !(oformat_context->oformat->flags & AVFMT_NOFILE))
        avio_closep(&oformat_context->pb);
    avformat_free_context(oformat_context);
    avformat_close_input(&iformat_context);

    return 0;
}