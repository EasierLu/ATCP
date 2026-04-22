#ifndef ATCP_RING_BUFFER_H
#define ATCP_RING_BUFFER_H
#include "../../include/atcp/types.h"

typedef struct {
    float *buffer;
    int capacity;
    int head;      /* 写入位置 */
    int tail;      /* 读取位置 */
    int count;     /* 当前元素数 */
} atcp_ringbuf_t;

atcp_status_t atcp_ringbuf_init(atcp_ringbuf_t *rb, float *storage, int capacity);
atcp_status_t atcp_ringbuf_write(atcp_ringbuf_t *rb, const float *data, int n);
atcp_status_t atcp_ringbuf_read(atcp_ringbuf_t *rb, float *data, int n);
atcp_status_t atcp_ringbuf_peek(atcp_ringbuf_t *rb, float *data, int n, int offset);
int atcp_ringbuf_available(const atcp_ringbuf_t *rb);    /* 可读数量 */
int atcp_ringbuf_free_space(const atcp_ringbuf_t *rb);   /* 可写数量 */
void atcp_ringbuf_reset(atcp_ringbuf_t *rb);

#endif
