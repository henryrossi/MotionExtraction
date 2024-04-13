#include "algorithms.h"
#include "types.h"

AVFrame *deep_copy_frame(AVFrame *src) {
  AVFrame *new_frame = av_frame_alloc();
  if (new_frame == NULL) {
    return NULL;
  }

  new_frame->format = src->format;
  new_frame->width = src->width;
  new_frame->height = src->height;
  for (unsigned int i = 0; i < 8; i++) {
    new_frame->linesize[i] = src->linesize[i];
  }

  int ret = av_frame_get_buffer(new_frame, 0);
  if (ret < 0) {
    return NULL;
  }

  ret = av_frame_copy(new_frame, src);
  if (ret < 0) {
    av_frame_free(&new_frame);
    return NULL;
  }

  new_frame->pts = src->pts;
  new_frame->pkt_dts = src->pkt_dts;

  return new_frame;
}

int write_inverted_frame_yuv420p(VideoCodec *video_codec) {
  for (int i = 0; i < 3; i++) {
    printf("The frame->data[%d] has a width of %d, height of %d, and linesize "
           "of %d\n",
           i, video_codec->frame->width, video_codec->frame->height,
           video_codec->frame->linesize[i]);
  }

  // linesize is the width of your image in memory for each color channel.
  // It may greater or equal to w, for memory alignment issue.
  video_codec->inverted_data[0] =
      (uint8_t *)malloc(sizeof(uint8_t) * video_codec->frame->linesize[0] *
                        video_codec->frame->height);
  video_codec->inverted_data[1] =
      (uint8_t *)malloc(sizeof(uint8_t) * video_codec->frame->linesize[1] *
                        video_codec->frame->height);
  video_codec->inverted_data[2] =
      (uint8_t *)malloc(sizeof(uint8_t) * video_codec->frame->linesize[2] *
                        video_codec->frame->height);

  if (video_codec->inverted_data[0] == NULL ||
      video_codec->inverted_data[1] == NULL ||
      video_codec->inverted_data[2] == NULL) {
    fprintf(stderr, "ERROR:   Failed to allocate memory for inverted frames\n");
    return -1;
  }

  /* Y */
  for (int y = 0; y < video_codec->frame->height; y++) {
    for (int x = 0; x < video_codec->frame->linesize[0]; x++) {
      video_codec->inverted_data[0][y * video_codec->frame->linesize[0] + x] =
          255 -
          video_codec->frame->data[0][y * video_codec->frame->linesize[0] + x];
    }
  }

  /* Cb and Cr */
  for (int y = 0; y < video_codec->frame->height / 2; y++) {
    for (int x = 0; x < video_codec->frame->linesize[1]; x++) {
      video_codec->inverted_data[1][y * video_codec->frame->linesize[1] + x] =
          255 -
          video_codec->frame->data[1][y * video_codec->frame->linesize[1] + x];
      video_codec->inverted_data[2][y * video_codec->frame->linesize[2] + x] =
          255 -
          video_codec->frame->data[2][y * video_codec->frame->linesize[2] + x];
    }
  }
  return 0;
}

int overlay_frames_yuv420p(VideoCodec *video_codec) {
  int ret = 0;
  if (video_codec->inverted_data[0] == NULL) {
    ret = write_inverted_frame_yuv420p(video_codec);
    if (ret < 0)
      return ret;
  }

  AVFrame *new_frame = deep_copy_frame(video_codec->frame);
  if (new_frame == NULL) {
    return -1;
  }
  // ret = av_frame_make_writable(video_codec->frame);

  /* Y */
  for (int y = 0; y < new_frame->height; y++) {
    for (int x = 0; x < new_frame->linesize[0]; x++) {
      int byte = 0;
      byte = (video_codec->inverted_data[0][y * new_frame->linesize[0] + x] +
              new_frame->data[0][y * new_frame->linesize[0] + x]) /
             2;
      if (byte > 255) {
        byte = 255;
      }
      new_frame->data[0][y * new_frame->linesize[0] + x] = byte;
    }
  }

  /* Cb and Cr */
  for (int y = 0; y < new_frame->height / 2; y++) {
    for (int x = 0; x < new_frame->linesize[1]; x++) {
      int byte = 0;
      byte = (video_codec->inverted_data[1][y * new_frame->linesize[1] + x] +
              new_frame->data[1][y * new_frame->linesize[1] + x]) /
             2;
      if (byte > 255) {
        byte = 255;
      }
      new_frame->data[1][y * new_frame->linesize[1] + x] = byte;

      byte = (video_codec->inverted_data[2][y * new_frame->linesize[2] + x] +
              new_frame->data[2][y * new_frame->linesize[2] + x]) /
             2;
      if (byte > 255) {
        byte = 255;
      }
      new_frame->data[2][y * new_frame->linesize[2] + x] = byte;
    }
  }

  av_frame_unref(video_codec->frame);
  av_frame_free(&video_codec->frame);
  video_codec->frame = new_frame;

  return ret;
}