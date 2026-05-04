#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t used;
} cubesat_ringbuf_t;

void ringbuf_init(cubesat_ringbuf_t *rb, uint8_t *storage, size_t capacity);
size_t ringbuf_write(cubesat_ringbuf_t *rb, const uint8_t *data, size_t len);
size_t ringbuf_read(cubesat_ringbuf_t *rb, uint8_t *data, size_t len);
size_t ringbuf_available(const cubesat_ringbuf_t *rb);
size_t ringbuf_free_space(const cubesat_ringbuf_t *rb);
bool ringbuf_is_empty(const cubesat_ringbuf_t *rb);
void ringbuf_clear(cubesat_ringbuf_t *rb);
