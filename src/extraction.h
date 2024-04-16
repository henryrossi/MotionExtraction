#ifndef ALGORITHMS_H
#define ALGORITHMS_H

#include <libavcodec/avcodec.h>

AVFrame *overlay_frames_yuv420p(AVFrame *original, AVFrame **inverted);

#endif /* ALGORITHMS_H */