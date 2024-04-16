#ifndef QUEUE_H
#define QUEUE_H

#include <libavcodec/avcodec.h>

struct frame_queue {
        struct queue_node *head;
        struct queue_node *tail;
        int cap;
	int size;
};

struct queue_node {
        struct queue_node *next, *prev;
        AVFrame *frame;
};

struct frame_queue *init_queue(int cap);

void free_queue(struct frame_queue *q);

AVFrame *push_pop_queue(struct frame_queue *q, AVFrame *frame);

#endif /* QUEUE_H */