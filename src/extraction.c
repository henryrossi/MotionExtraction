#include "extraction.h"

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

int write_inverted_frame_yuv420p(AVFrame *original, AVFrame **inverted) {
        (*inverted) = deep_copy_frame(original);

        if ((*inverted) == NULL) {
                fprintf(
                    stderr,
                    "ERROR:   Failed to allocate memory for inverted frame\n");
                return -1;
        }

        uint8_t *Y_inv = (*inverted)->data[0];
        int Y_ls = (*inverted)->linesize[0];
        /* Y */
        for (int y = 0; y < (*inverted)->height; y++) {
                for (int x = 0; x < (*inverted)->width; x++) {
                        Y_inv[y * Y_ls + x] = 255 - Y_inv[y * Y_ls + x];
                }
        }

        uint8_t *Cb_inv = (*inverted)->data[1];
        int Cb_ls = (*inverted)->linesize[1];
        uint8_t *Cr_inv = (*inverted)->data[2];
        int Cr_ls = (*inverted)->linesize[2];
        /* Cb and Cr */
        for (int y = 0; y < (*inverted)->height / 2; y++) {
                for (int x = 0; x < (*inverted)->width / 2; x++) {
                        Cb_inv[y * Cb_ls + x] = 255 - Cb_inv[y * Cb_ls + x];
                        Cr_inv[y * Cr_ls + x] = 255 - Cr_inv[y * Cr_ls + x];
                }
        }
        return 0;
}

AVFrame *overlay_frames_yuv420p(AVFrame *original, AVFrame **inverted) {
        if ((*inverted) == NULL) {
                int ret = write_inverted_frame_yuv420p(original, inverted);
                if (ret < 0)
                        return NULL;
        }

        AVFrame *new_frame = deep_copy_frame(original);
        if (new_frame == NULL) {
                return NULL;
        }
        // ret = av_frame_make_writable(video_codec->frame);

        uint8_t *Y_inv = (*inverted)->data[0];
        int Y_ls = new_frame->linesize[0];
        /* Y */
        for (int y = 0; y < new_frame->height; y++) {
                for (int x = 0; x < new_frame->width; x++) {
                        int byte = 0;
                        byte = (Y_inv[y * Y_ls + x] +
                                new_frame->data[0][y * Y_ls + x]) /
                               2;
                        if (byte > 255) {
                                byte = 255;
                        }
                        new_frame->data[0][y * Y_ls + x] = byte;
                }
        }

        uint8_t *Cb_inv = (*inverted)->data[1];
        int Cb_ls = new_frame->linesize[1];
        uint8_t *Cr_inv = (*inverted)->data[2];
        int Cr_ls = new_frame->linesize[2];
        /* Cb and Cr */
        for (int y = 0; y < new_frame->height / 2; y++) {
                for (int x = 0; x < new_frame->width / 2; x++) {
                        int byte = 0;
                        byte = (Cb_inv[y * Cb_ls + x] +
                                new_frame->data[1][y * Cb_ls + x]) /
                               2;
                        if (byte > 255) {
                                byte = 255;
                        }
                        new_frame->data[1][y * Cb_ls + x] = byte;

                        byte = (Cr_inv[y * Cr_ls + x] +
                                new_frame->data[2][y * Cr_ls + x]) /
                               2;
                        if (byte > 255) {
                                byte = 255;
                        }
                        new_frame->data[2][y * Cr_ls + x] = byte;
                }
        }

        return new_frame;
}