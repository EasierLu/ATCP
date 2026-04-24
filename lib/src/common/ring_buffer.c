#include "ring_buffer.h"
#include <string.h>

atcp_status_t atcp_ringbuf_init(atcp_ringbuf_t *rb, float *storage, int capacity)
{
    if (!rb || !storage || capacity <= 0)
        return ATCP_ERR_INVALID_PARAM;
    rb->buffer   = storage;
    rb->capacity = capacity;
    rb->head     = 0;
    rb->tail     = 0;
    rb->count    = 0;
    return ATCP_OK;
}

atcp_status_t atcp_ringbuf_write(atcp_ringbuf_t *rb, const float *data, int n)
{
    int first, second;
    if (!rb || !data || n <= 0)
        return ATCP_ERR_INVALID_PARAM;
    if (n > rb->capacity - rb->count)
        return ATCP_ERR_BUFFER_FULL;

    /* 从 head 写入，可能需要两段拷贝 */
    first = rb->capacity - rb->head;
    if (first >= n) {
        memcpy(rb->buffer + rb->head, data, (size_t)n * sizeof(float));
    } else {
        memcpy(rb->buffer + rb->head, data, (size_t)first * sizeof(float));
        second = n - first;
        memcpy(rb->buffer, data + first, (size_t)second * sizeof(float));
    }
    rb->head = (rb->head + n) % rb->capacity;
    rb->count += n;
    return ATCP_OK;
}

atcp_status_t atcp_ringbuf_read(atcp_ringbuf_t *rb, float *data, int n)
{
    int first, second;
    if (!rb || !data || n <= 0)
        return ATCP_ERR_INVALID_PARAM;
    if (n > rb->count)
        return ATCP_ERR_BUFFER_EMPTY;

    first = rb->capacity - rb->tail;
    if (first >= n) {
        memcpy(data, rb->buffer + rb->tail, (size_t)n * sizeof(float));
    } else {
        memcpy(data, rb->buffer + rb->tail, (size_t)first * sizeof(float));
        second = n - first;
        memcpy(data + first, rb->buffer, (size_t)second * sizeof(float));
    }
    rb->tail = (rb->tail + n) % rb->capacity;
    rb->count -= n;
    return ATCP_OK;
}

atcp_status_t atcp_ringbuf_peek(atcp_ringbuf_t *rb, float *data, int n, int offset)
{
    int pos, first, second;
    if (!rb || !data || n <= 0 || offset < 0)
        return ATCP_ERR_INVALID_PARAM;
    if (offset + n > rb->count)
        return ATCP_ERR_BUFFER_EMPTY;

    pos = (rb->tail + offset) % rb->capacity;
    first = rb->capacity - pos;
    if (first >= n) {
        memcpy(data, rb->buffer + pos, (size_t)n * sizeof(float));
    } else {
        memcpy(data, rb->buffer + pos, (size_t)first * sizeof(float));
        second = n - first;
        memcpy(data + first, rb->buffer, (size_t)second * sizeof(float));
    }
    return ATCP_OK;
}

int atcp_ringbuf_available(const atcp_ringbuf_t *rb)
{
    if (!rb) return 0;
    return rb->count;
}

int atcp_ringbuf_free_space(const atcp_ringbuf_t *rb)
{
    if (!rb) return 0;
    return rb->capacity - rb->count;
}

void atcp_ringbuf_reset(atcp_ringbuf_t *rb)
{
    if (!rb) return;
    rb->head  = 0;
    rb->tail  = 0;
    rb->count = 0;
}
