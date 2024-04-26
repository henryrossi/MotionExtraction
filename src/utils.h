#ifndef UTILS_H
#define UTILS_H

#include <libavcodec/avcodec.h>

AVFrame *deep_copy_frame(const AVFrame *src);

#endif /* UTILS_H */