#ifndef EXTRACTION_H
#define EXTRACTION_H

#include <libavcodec/avcodec.h>

#include "queue.h"
#include "utils.h"

AVFrame *overlay_frames_yuv420p(AVFrame **cur, struct frame_queue *q);

#endif /* EXTRACTION_H */