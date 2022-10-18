#ifndef _RING_BUFFER_H_
#define _RING_BUFFER_H_

#include <stdbool.h>
#include <stdint.h>

#include "context.h"

struct ring_buffer_t
{
    uint8_t* buffer;
    size_t   head;
    size_t   tail;
    size_t   max;
    bool     full;
};

static inline void
ring_buffer_init(struct ring_buffer_t* rb, uint8_t* buffer, size_t size)
{
    ASSERT(rb != NULL && "rb is NULL");
    ASSERT(buffer != NULL && "buffer is NULL");
    ASSERT(size > 0 && "size is 0");

    rb->buffer = buffer;
    rb->head   = 0;
    rb->tail   = 0;
    rb->max    = size;
    rb->full   = false;
}

static inline void
ring_buffer_reset(struct ring_buffer_t* rb)
{
    ASSERT(rb != NULL && "rb is NULL");
    rb->head = 0;
    rb->tail = 0;
    rb->full = false;
}

static inline bool
ring_buffer_is_empty(struct ring_buffer_t* rb)
{
    ASSERT(rb != NULL && "rb is NULL");
    return (!rb->full && (rb->head == rb->tail));
}

static inline bool
ring_buffer_is_full(struct ring_buffer_t* rb)
{
    ASSERT(rb != NULL && "rb is NULL");
    return rb->full;
}

static inline void
ring_buffer_push(struct ring_buffer_t* rb, uint8_t byte)
{
    ASSERT(rb != NULL && "rb is NULL");
    rb->buffer[rb->head] = byte;
    rb->head             = (rb->head + 1) % rb->max;
    if (rb->head == rb->tail)
    {
        rb->full = true;
    }
    if (rb->full)
    {
        rb->tail = rb->head;
    }
}

static inline uint8_t
ring_buffer_pop(struct ring_buffer_t* rb)
{
    ASSERT(rb != NULL && "rb is NULL");
    if (ring_buffer_is_empty(rb))
    {
        return 0;
    }

    uint8_t byte = rb->buffer[rb->tail];
    rb->tail     = (rb->tail + 1) % rb->max;
    rb->full     = false;
    return byte;
}

static inline size_t
ring_buffer_size(struct ring_buffer_t* rb)
{
    ASSERT(rb != NULL && "rb is NULL");
    size_t size = rb->max;
    if (!rb->full)
    {
        if (rb->head >= rb->tail)
        {
            size = rb->head - rb->tail;
        }
        else
        {
            size = rb->max + rb->head - rb->tail;
        }
    }
    return size;
}

static inline size_t
ring_buffer_capacity(struct ring_buffer_t* rb)
{
    ASSERT(rb != NULL && "rb is NULL");
    return rb->max;
}

static inline size_t
ring_buffer_available(struct ring_buffer_t* rb)
{
    ASSERT(rb != NULL && "rb is NULL");
    return rb->max - ring_buffer_size(rb);
}

static inline size_t
ring_buffer_copy_to(struct ring_buffer_t* rb, uint8_t* buffer, size_t size)
{
    ASSERT(rb != NULL && "rb is NULL");
    ASSERT(buffer != NULL && "buffer is NULL");
    ASSERT(size > 0 && "size is 0");

    size_t i = 0;
    while (i < size && !ring_buffer_is_empty(rb))
    {
        buffer[i++] = rb->buffer[rb->tail];
        rb->tail    = (rb->tail + 1) % rb->max;
        rb->full    = false;
    }
    return i;
}

static inline void
ring_buffer_copy_from(struct ring_buffer_t* rb, uint8_t* buffer, size_t size)
{
    ASSERT(rb != NULL && "rb is NULL");
    ASSERT(buffer != NULL && "buffer is NULL");
    ASSERT(size > 0 && "size is 0");

    size_t i = 0;
    while (i < size)
    {
        rb->buffer[rb->head] = buffer[i++];
        rb->head             = (rb->head + 1) % rb->max;
        if (rb->head == rb->tail)
        {
            rb->full = true;
        }
        if (rb->full)
        {
            rb->tail = rb->head;
        }
    }
}

#endif /* _RING_BUFFER_H_ */
