#include "ringbuf.h"

void ringbuf_init(cubesat_ringbuf_t *rb, uint8_t *storage, size_t capacity) {
    if (rb == NULL) {
        return;
    }

    rb->data = storage;
    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->used = 0;
}

size_t ringbuf_write(cubesat_ringbuf_t *rb, const uint8_t *data, size_t len) {
    if (rb == NULL || rb->data == NULL || data == NULL || rb->capacity == 0) {
        return 0;
    }

    size_t written = 0;
    while (written < len && rb->used < rb->capacity) {
        rb->data[rb->head] = data[written++];
        rb->head = (rb->head + 1) % rb->capacity;
        rb->used++;
    }
    return written;
}

size_t ringbuf_read(cubesat_ringbuf_t *rb, uint8_t *data, size_t len) {
    if (rb == NULL || rb->data == NULL || data == NULL || rb->capacity == 0) {
        return 0;
    }

    size_t read = 0;
    while (read < len && rb->used > 0) {
        data[read++] = rb->data[rb->tail];
        rb->tail = (rb->tail + 1) % rb->capacity;
        rb->used--;
    }
    return read;
}

size_t ringbuf_available(const cubesat_ringbuf_t *rb) {
    return rb == NULL ? 0 : rb->used;
}

size_t ringbuf_free_space(const cubesat_ringbuf_t *rb) {
    if (rb == NULL || rb->capacity < rb->used) {
        return 0;
    }
    return rb->capacity - rb->used;
}

bool ringbuf_is_empty(const cubesat_ringbuf_t *rb) {
    return rb == NULL || rb->used == 0;
}

void ringbuf_clear(cubesat_ringbuf_t *rb) {
    if (rb == NULL) {
        return;
    }
    rb->head = 0;
    rb->tail = 0;
    rb->used = 0;
}
