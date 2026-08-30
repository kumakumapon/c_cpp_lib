#include "uart_protocol/uart_ringbuf.h"

void uart_ringbuf_init(uart_ringbuf_t *rb, uint8_t *storage, size_t capacity) {
    if (!rb) return;
    rb->buffer = storage;
    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
}

bool uart_ringbuf_put(uart_ringbuf_t *rb, uint8_t byte) {
    if (!rb || !rb->buffer || rb->capacity == 0) return false;

    size_t next_head = (rb->head + 1) % rb->capacity;
    if (next_head == rb->tail) {
        return false; /* バッファフル */
    }

    rb->buffer[rb->head] = byte;
    rb->head = next_head;
    return true;
}

size_t uart_ringbuf_write(uart_ringbuf_t *rb, const uint8_t *data, size_t length) {
    if (!rb || !data || length == 0) return 0;

    size_t written = 0;
    for (size_t i = 0; i < length; ++i) {
        if (!uart_ringbuf_put(rb, data[i])) {
            break;
        }
        written++;
    }
    return written;
}

bool uart_ringbuf_get(uart_ringbuf_t *rb, uint8_t *byte) {
    if (!rb || !rb->buffer || rb->head == rb->tail || !byte) {
        return false; /* バッファエンプティ */
    }

    *byte = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % rb->capacity;
    return true;
}

size_t uart_ringbuf_read(uart_ringbuf_t *rb, uint8_t *dest, size_t max_length) {
    if (!rb || !dest || max_length == 0) return 0;

    size_t count = 0;
    while (count < max_length && uart_ringbuf_get(rb, &dest[count])) {
        count++;
    }
    return count;
}

size_t uart_ringbuf_available(const uart_ringbuf_t *rb) {
    if (!rb || !rb->buffer) return 0;
    if (rb->head >= rb->tail) {
        return rb->head - rb->tail;
    }
    return rb->capacity - (rb->tail - rb->head);
}

size_t uart_ringbuf_free_space(const uart_ringbuf_t *rb) {
    if (!rb || !rb->buffer || rb->capacity == 0) return 0;
    return (rb->capacity - 1) - uart_ringbuf_available(rb);
}

void uart_ringbuf_clear(uart_ringbuf_t *rb) {
    if (rb) {
        rb->head = 0;
        rb->tail = 0;
    }
}
