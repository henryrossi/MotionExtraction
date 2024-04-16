#include "utils.h"

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