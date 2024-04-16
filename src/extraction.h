#ifndef EXTRACTION_H
#define EXTRACTION_H

#include <libavcodec/avcodec.h>

AVFrame *overlay_frames_yuv420p(AVFrame *original, AVFrame **inverted);

#endif /* EXTRACTION_H */