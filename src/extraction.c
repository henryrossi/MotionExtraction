#include "extraction.h"

AVFrame *overlay_frames_yuv420p(AVFrame **cur, struct frame_queue *q) {
        AVFrame *cur_copy = deep_copy_frame(*cur);
        if (cur_copy == NULL) {
                return NULL;
        }

        AVFrame *delayed_frame = push_pop_queue(q, cur_copy);

        /* Y */
        int Y_ls = delayed_frame->linesize[0];
        for (int y = 0; y < delayed_frame->height; y++) {
                for (int x = 0; x < delayed_frame->width; x++) {
                        int byte = (255 - delayed_frame->data[0][y * Y_ls + x] +
                                    cur_copy->data[0][y * Y_ls + x]) /
                                   2;
                        if (byte > 255) {
                                byte = 255;
                        }
                        delayed_frame->data[0][y * Y_ls + x] = byte;
                }
        }

        /* Cb and Cr */
        int Cb_ls = delayed_frame->linesize[1];
        int Cr_ls = delayed_frame->linesize[2];
        for (int y = 0; y < delayed_frame->height / 2; y++) {
                for (int x = 0; x < delayed_frame->width / 2; x++) {
                        int byte =
                            (255 - delayed_frame->data[1][y * Cb_ls + x] +
                             cur_copy->data[1][y * Cb_ls + x]) /
                            2;
                        if (byte > 255) {
                                byte = 255;
                        }
                        delayed_frame->data[1][y * Cb_ls + x] = byte;

                        byte = (255 - delayed_frame->data[2][y * Cr_ls + x] +
                                cur_copy->data[2][y * Cr_ls + x]) /
                               2;
                        if (byte > 255) {
                                byte = 255;
                        }
                        delayed_frame->data[2][y * Cr_ls + x] = byte;
                }
        }

        av_frame_unref(*cur);
        av_frame_free(cur);

        delayed_frame->pts = cur_copy->pts;
        delayed_frame->pkt_dts = cur_copy->pkt_dts;

        return delayed_frame;
}