#ifndef TYPES_H
#define TYPES_H

#include <libavcodec/avcodec.h>

typedef struct VideoCodec {
    AVCodecContext *decoder_context;
    AVCodecContext *encoder_context;
    AVFrame *frame;
    uint8_t *inverted_data[8];
} VideoCodec;

#endif /* TYPES_H */