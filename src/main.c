#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "algorithms.h"
#include "types.h"

static int open_input_file(AVFormatContext **ifmt, const char *filename) {
  int ret = avformat_open_input(ifmt, filename, NULL, NULL);
  if (ret < 0) {
    fprintf(stderr, "ERROR:   Failed to open input file\n");
    return ret;
  }

  ret = avformat_find_stream_info(*ifmt, NULL);
  if (ret < 0) {
    fprintf(stderr, "ERROR:   Failed to read and analyze input file\n");
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
      fprintf(stderr, "ERROR:   Could not open output file '%s'", filename);
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
      fprintf(stderr, "ERROR:   Failed to add stream to the output file\n");
      return -1;
    }

    int ret =
        avcodec_parameters_copy(stream->codecpar, ifmt->streams[i]->codecpar);
    if (ret < 0) {
      fprintf(stderr, "ERROR:   Failed to copy codec params to out stream\n");
      return -1;
    }

    if (ifmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      *video_stream = i;
    }
  }

  if (*video_stream < 0) {
    fprintf(
        stderr,
        "ERROR:   Program only supports video files with one video stream\n");
    return -1;
  }

  return 0;
}

static int configure_decoder(AVFormatContext *ifmt, int video, VideoCodec *vc) {
  const AVCodec *decoder =
      avcodec_find_decoder(ifmt->streams[video]->codecpar->codec_id);
  if (decoder == NULL) {
    fprintf(stderr, "ERROR:   Couldn't find the encoder/decoder\n");
    return -1;
  }

  AVCodecContext *decoder_context = avcodec_alloc_context3(decoder);
  if (decoder_context == NULL) {
    fprintf(stderr, "ERROR:   Couldn't allocate the decoder context with the "
                    "given decoder\n");
    return -1;
  }

  int ret = avcodec_parameters_to_context(decoder_context,
                                          ifmt->streams[video]->codecpar);
  if (ret < 0) {
    fprintf(stderr, "ERROR:   Failed to fill the decoder context based on the "
                    "values from codec parameters\n");
    return ret;
  }

  ret = avcodec_open2(decoder_context, decoder, NULL);
  if (ret < 0) {
    fprintf(stderr, "ERROR:   Failed to open the decoder context\n");
    return ret;
  }

  if (decoder_context->pix_fmt != AV_PIX_FMT_YUV420P) {
    fprintf(stderr, "ERROR:   Video has a pixel format that is not supported "
                    "by this program\n");
    return -1;
  }

  vc->decoder_context = decoder_context;

  return 0;
}

static int configure_encoder(AVFormatContext *ifmt, AVFormatContext *ofmt,
                             int video, VideoCodec *vc) {
  const AVCodec *encoder =
      avcodec_find_encoder(ifmt->streams[video]->codecpar->codec_id);
  if (encoder == NULL) {
    fprintf(stderr, "ERROR:   Couldn't find the encoder/decoder\n");
    return -1;
  }

  AVCodecContext *encoder_context = avcodec_alloc_context3(encoder);
  if (encoder_context == NULL) {
    fprintf(stderr, "ERROR:   Couldn't allocate the encoder context with the "
                    "given codec\n");
    return -1;
  }

  int ret = avcodec_parameters_to_context(encoder_context,
                                          ifmt->streams[video]->codecpar);
  if (ret < 0) {
    fprintf(stderr, "ERROR:   Failed to fill the encoder context based on the "
                    "values from codec parameters\n");
    return ret;
  }

  encoder_context->time_base =
      av_inv_q(av_guess_frame_rate(ifmt, ifmt->streams[video], NULL));

  if (ofmt->oformat->flags & AVFMT_GLOBALHEADER)
    encoder_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  ret = avcodec_open2(encoder_context, encoder, NULL);
  if (ret < 0) {
    fprintf(stderr, "ERROR:   Failed to open the encoder context\n");
    return ret;
  }

  ret = avcodec_parameters_from_context(ofmt->streams[video]->codecpar,
                                        encoder_context);
  if (ret < 0) {
    fprintf(stderr, "ERROR:   Failed avcodec_parameters_from_context()\n");
    return ret;
  }

  vc->encoder_context = encoder_context;

  return 0;
}

int process_video(AVFormatContext *iformat_context,
                  AVFormatContext *oformat_context, VideoCodec *video_codec,
                  AVPacket *packet, int stream_index);

int mux(AVFormatContext *iformat_context, AVFormatContext *oformat_context,
        AVPacket *packet, int stream_index);

int encode_video(AVFormatContext *iformat_context,
                 AVFormatContext *oformat_context, VideoCodec *video_codec,
                 AVPacket *packet, int stream_index);

int main(int argc, char *argv[]) {
  // if (argc != 3) {
  //   fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
  //   return -1;
  // }
  const char *ifile = "world.mov";
  const char *ofile = "new.mov";

  AVFormatContext *iformat_context = NULL;
  AVFormatContext *oformat_context = NULL;
  VideoCodec video_codec = {0};
  AVPacket *packet = NULL;

  video_codec.frame = av_frame_alloc();
  if (video_codec.frame == NULL) {
    fprintf(stderr, "ERROR:   Couldn't allocate frame\n");
    goto cleanup;
  }
  video_codec.inverted_data[0] = NULL;

  int video_stream = -1;

  int ret = open_input_file(&iformat_context, ifile);
  if (ret < 0) {
    goto cleanup;
  }

  av_dump_format(iformat_context, 0, ifile, 0);

  ret = open_output_file(&oformat_context, ofile);
  if (ret < 0) {
    goto cleanup;
  }

  ret = create_output_streams(iformat_context, oformat_context, &video_stream);
  if (ret < 0) {
    goto cleanup;
  }

  ret = configure_decoder(iformat_context, video_stream, &video_codec);
  if (ret < 0) {
    goto cleanup;
  }

  ret = configure_encoder(iformat_context, oformat_context, video_stream,
                          &video_codec);
  if (ret < 0) {
    goto cleanup;
  }

  av_dump_format(oformat_context, 0, ofile, 1);

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

    if (iformat_context->streams[streamIndex]->codecpar->codec_type ==
        AVMEDIA_TYPE_VIDEO) {
      ret = avcodec_send_packet(video_codec.decoder_context, packet);
      if (ret < 0) {
        fprintf(stderr, "ERROR:   Failed sending packet to decoder\n");
        goto cleanup;
      }

      ret = process_video(iformat_context, oformat_context, &video_codec,
                          packet, streamIndex);
      if (ret < 0)
        goto cleanup;
    } else {
      ret = mux(iformat_context, oformat_context, packet, streamIndex);
      if (ret < 0)
        goto cleanup;
    }

    av_packet_unref(packet);
  }

  ret = avcodec_send_packet(video_codec.decoder_context, NULL);
  if (ret < 0) {
    fprintf(stderr, "ERROR:   Failed to flush decoder\n");
    goto cleanup;
  }
  ret = process_video(iformat_context, oformat_context, &video_codec, packet,
                      video_stream);
  if (ret < 0)
    goto cleanup;

  ret = avcodec_send_frame(video_codec.encoder_context, NULL);
  if (ret < 0) {
    fprintf(stderr, "ERROR:   Failed to flush encoder\n");
    goto cleanup;
  }
  ret = encode_video(iformat_context, oformat_context, &video_codec, packet,
                     video_stream);
  if (ret < 0)
    goto cleanup;

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

int process_video(AVFormatContext *iformat_context,
                  AVFormatContext *oformat_context, VideoCodec *video_codec,
                  AVPacket *packet, int stream_index) {
  int ret;
  while (1) {

    if (video_codec->frame == NULL) {
      video_codec->frame = av_frame_alloc();
    }
    if (video_codec->frame == NULL) {
      fprintf(stderr, "ERROR:   Couldn't allocate frame\n");
      return AVERROR(ENOMEM);
    }

    ret =
        avcodec_receive_frame(video_codec->decoder_context, video_codec->frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break;
    if (ret < 0) {
      fprintf(stderr, "ERROR:   Failed recieving frame from decoder\n");
      return ret;
    }

    video_codec->frame->pts = video_codec->frame->best_effort_timestamp;

    overlay_frames_yuv420p(video_codec);

    ret = avcodec_send_frame(video_codec->encoder_context, video_codec->frame);
    if (ret < 0) {
      fprintf(stderr, "ERROR:   Failed sending frame to encoder, (error: %s)\n",
              av_err2str(ret));
      return ret;
    }

    ret = encode_video(iformat_context, oformat_context, video_codec, packet,
                       stream_index);
    if (ret < 0)
      return ret;
  }

  return 0;
}

int encode_video(AVFormatContext *iformat_context,
                 AVFormatContext *oformat_context, VideoCodec *video_codec,
                 AVPacket *packet, int stream_index) {
  int ret;
  while (1) {
    av_packet_unref(packet);

    ret = avcodec_receive_packet(video_codec->encoder_context, packet);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break;
    if (ret < 0) {
      fprintf(stderr, "ERROR:   Failed recieving packet from encoder\n");
      return ret;
    }

    ret = mux(iformat_context, oformat_context, packet, stream_index);
    if (ret < 0)
      return ret;
  }

  // av_frame_unref(video_codec->frame);
  av_frame_free(&video_codec->frame);
  video_codec->frame = NULL;

  return 0;
}

int mux(AVFormatContext *iformat_context, AVFormatContext *oformat_context,
        AVPacket *packet, int stream_index) {
  av_packet_rescale_ts(packet,
                       iformat_context->streams[stream_index]->time_base,
                       oformat_context->streams[stream_index]->time_base);

  int ret = av_interleaved_write_frame(oformat_context, packet);
  if (ret < 0) {
    fprintf(stderr, "ERROR:   Failed to write packet to output file\n");
    return ret;
  }
  return 0;
}