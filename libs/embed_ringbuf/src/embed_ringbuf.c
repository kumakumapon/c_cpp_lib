#include "embed_ringbuf/embed_ringbuf.h"
static size_t advance(size_t i, size_t n) { return (n & (n-1)) == 0 ? (i+1) & (n-1) : (i+1 == n ? 0 : i+1); }

void embed_ringbuf_init(embed_ringbuf_t *rb, uint8_t *storage, size_t capacity) {
    if (!rb) return;
    rb->buffer = storage;
    rb->capacity = storage && capacity >= 2 ? capacity : 0;
    rb->head = 0;
    rb->tail = 0;
}

bool embed_ringbuf_put(embed_ringbuf_t *rb, uint8_t byte) {
    if (!rb || !rb->buffer || rb->capacity == 0) return false;

    size_t next_head = advance(rb->head, rb->capacity);
    if (next_head == rb->tail) {
        return false; /* バッファフル */
    }

    rb->buffer[rb->head] = byte;
    rb->head = next_head;
    return true;
}

size_t embed_ringbuf_write(embed_ringbuf_t *rb, const uint8_t *data, size_t length) {
    if (!rb || !data || length == 0) return 0;

    size_t written = 0;
    for (size_t i = 0; i < length; ++i) {
        if (!embed_ringbuf_put(rb, data[i])) {
            break;
        }
        written++;
    }
    return written;
}

bool embed_ringbuf_get(embed_ringbuf_t *rb, uint8_t *byte) {
    if (!rb || !rb->buffer || rb->head == rb->tail || !byte) {
        return false; /* バッファエンプティ */
    }

    *byte = rb->buffer[rb->tail];
    rb->tail = advance(rb->tail, rb->capacity);
    return true;
}

size_t embed_ringbuf_read(embed_ringbuf_t *rb, uint8_t *dest, size_t max_length) {
    if (!rb || !dest || max_length == 0) return 0;

    size_t count = 0;
    while (count < max_length && embed_ringbuf_get(rb, &dest[count])) {
        count++;
    }
    return count;
}

size_t embed_ringbuf_available(const embed_ringbuf_t *rb) {
    if (!rb || !rb->buffer) return 0;
    if (rb->head >= rb->tail) {
        return rb->head - rb->tail;
    }
    return rb->capacity - (rb->tail - rb->head);
}

size_t embed_ringbuf_free_space(const embed_ringbuf_t *rb) {
    if (!rb || !rb->buffer || rb->capacity == 0) return 0;
    return (rb->capacity - 1) - embed_ringbuf_available(rb);
}

void embed_ringbuf_clear(embed_ringbuf_t *rb) {
    if (rb) {
        rb->head = 0;
        rb->tail = 0;
    }
}
