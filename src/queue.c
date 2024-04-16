#include "queue.h"

struct queue_node *create_node(AVFrame *frame) {
        struct queue_node *node = malloc(sizeof(*node));
        if (node == NULL) {
                fprintf(stderr, "ERROR:   Failed to allocate list node");
                return node;
        }

        node->next = NULL;
        node->prev = NULL;
        node->frame = frame;

        return node;
}

void free_node(struct queue_node *node) {
        if (node->frame == NULL) {
                av_frame_unref(node->frame);
                av_frame_free(&node->frame);
        }
        free(node);
}

struct frame_queue *init_queue(int cap) {
        struct frame_queue *q = malloc(sizeof(*q));
        if (q == NULL) {
                fprintf(stderr, "ERROR:   Failed to allocate list head");
                return q;
        }

        q->head = NULL;
        q->tail = NULL;
        q->cap = cap;
        q->size = 0;

        return q;
}

void free_queue(struct frame_queue *q) {
        while (q->head != NULL) {
                struct queue_node *node = q->head;
                q->head = node->next;
                free_node(node);
        }
        free(q);
}

AVFrame *frozen(struct frame_queue *q, AVFrame *frame) {
        if (q->head == NULL) {
                struct queue_node *node = create_node(frame);
                q->head = node;
                q->tail = node;
                q->size = 1;
        }
        return deep_copy_frame(q->head->frame);
}


// alwasys returns deep copy
AVFrame *push_pop_queue(struct frame_queue *q, AVFrame *frame) {
        if (q->cap == 0) {
                return frozen(q, frame);
        }

        struct queue_node *node = create_node(frame);
        if (q->head == NULL) {
                q->tail = node;
        } else {
                node->next = q->head;
                q->head->prev = node;
        }
        q->head = node;
        q->size += 1;

        if (q->size <= q->cap) {
                return deep_copy_frame(q->tail->frame);
        }

        struct queue_node *prev_tail = q->tail;
        AVFrame *res = prev_tail-> frame;
        prev_tail->frame = NULL;

        q->tail = q->tail->prev;
        q->tail->next = NULL;
	q->size -= 1;

        free_node(prev_tail);

        return res;
}