#include "embed_ringbuf/embed_ringbuf.h"
#include <string.h>

/* データの読み書きと位置 (head/tail) の更新の順序を、コンパイラにも CPU にも入れ替えさせない。
 * volatile だけでは、volatile でないバッファ書き込みが head 更新の後ろへ移動しうる。 */
#if defined(__GNUC__) || defined(__clang__)
#define RB_ACQUIRE() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define RB_RELEASE() __atomic_thread_fence(__ATOMIC_RELEASE)
#elif defined(_MSC_VER)
#include <intrin.h>
#if defined(_M_ARM) || defined(_M_ARM64)
#define RB_ACQUIRE() __dmb(0xB) /* ISH */
#define RB_RELEASE() __dmb(0xB)
#else
/* x86/x64 は TSO なのでコンパイラバリアで足りる */
#define RB_ACQUIRE() _ReadWriteBarrier()
#define RB_RELEASE() _ReadWriteBarrier()
#endif
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
#define RB_ACQUIRE() atomic_thread_fence(memory_order_acquire)
#define RB_RELEASE() atomic_thread_fence(memory_order_release)
#else
#error "embed_ringbuf: no memory fence available for this compiler"
#endif

static bool usable(const embed_ringbuf_t *rb) { return rb && rb->buffer && rb->capacity >= 2; }

/* i + n を容量で折り返す (n <= capacity)。除算は使わない。 */
static size_t wrap_add(size_t i, size_t n, size_t capacity) {
    size_t room = capacity - i;
    return n < room ? i + n : n - room;
}

static size_t count_between(size_t head, size_t tail, size_t capacity) {
    return head >= tail ? head - tail : capacity - (tail - head);
}

void embed_ringbuf_init(embed_ringbuf_t *rb, uint8_t *storage, size_t capacity) {
    if (!rb)
        return;
    rb->buffer = storage;
    rb->capacity = storage && capacity >= 2 ? capacity : 0;
    rb->head = 0;
    rb->tail = 0;
}

/* ---------------- 生産者側 ---------------- */

size_t embed_ringbuf_write(embed_ringbuf_t *rb, const uint8_t *data, size_t length) {
    if (!usable(rb) || !data || length == 0)
        return 0;
    const size_t cap = rb->capacity;
    const size_t head = rb->head;
    const size_t tail = rb->tail;
    RB_ACQUIRE(); /* 消費者が読み終えた領域だけを上書きする */
    const size_t space = (cap - 1) - count_between(head, tail, cap);
    const size_t n = length < space ? length : space;
    if (n == 0)
        return 0;
    const size_t first = n < cap - head ? n : cap - head;
    memcpy(rb->buffer + head, data, first);
    memcpy(rb->buffer, data + first, n - first);
    RB_RELEASE(); /* データを書いてから head を進める */
    rb->head = wrap_add(head, n, cap);
    return n;
}

bool embed_ringbuf_put(embed_ringbuf_t *rb, uint8_t byte) {
    return embed_ringbuf_write(rb, &byte, 1) == 1;
}

size_t embed_ringbuf_write_linear(embed_ringbuf_t *rb, uint8_t **ptr) {
    if (!usable(rb) || !ptr)
        return 0;
    const size_t head = rb->head;
    const size_t tail = rb->tail;
    RB_ACQUIRE();
    *ptr = rb->buffer + head;
    /* 満杯と空を区別するため、tail の直前の 1 バイトは常に空けておく */
    if (tail > head)
        return tail - head - 1;
    return rb->capacity - head - (tail == 0 ? 1u : 0u);
}

bool embed_ringbuf_write_commit(embed_ringbuf_t *rb, size_t n) {
    uint8_t *unused;
    if (!usable(rb) || n > embed_ringbuf_write_linear(rb, &unused))
        return false;
    RB_RELEASE();
    rb->head = wrap_add(rb->head, n, rb->capacity);
    return true;
}

/* ---------------- 消費者側 ---------------- */

size_t embed_ringbuf_peek_bytes(const embed_ringbuf_t *rb, size_t offset, uint8_t *dest,
                                size_t max_length) {
    if (!usable(rb) || !dest || max_length == 0)
        return 0;
    const size_t cap = rb->capacity;
    const size_t tail = rb->tail;
    const size_t head = rb->head;
    RB_ACQUIRE(); /* head を読んでからデータを読む */
    const size_t stored = count_between(head, tail, cap);
    if (offset >= stored)
        return 0;
    const size_t n = max_length < stored - offset ? max_length : stored - offset;
    const size_t start = wrap_add(tail, offset, cap);
    const size_t first = n < cap - start ? n : cap - start;
    memcpy(dest, rb->buffer + start, first);
    memcpy(dest + first, rb->buffer, n - first);
    return n;
}

bool embed_ringbuf_peek(const embed_ringbuf_t *rb, size_t offset, uint8_t *byte) {
    return embed_ringbuf_peek_bytes(rb, offset, byte, 1) == 1;
}

size_t embed_ringbuf_skip(embed_ringbuf_t *rb, size_t n) {
    if (!usable(rb) || n == 0)
        return 0;
    const size_t cap = rb->capacity;
    const size_t tail = rb->tail;
    const size_t stored = count_between(rb->head, tail, cap);
    const size_t k = n < stored ? n : stored;
    RB_RELEASE(); /* 読み終えてから tail を進める */
    rb->tail = wrap_add(tail, k, cap);
    return k;
}

size_t embed_ringbuf_read(embed_ringbuf_t *rb, uint8_t *dest, size_t max_length) {
    size_t n = embed_ringbuf_peek_bytes(rb, 0, dest, max_length);
    embed_ringbuf_skip(rb, n);
    return n;
}

bool embed_ringbuf_get(embed_ringbuf_t *rb, uint8_t *byte) {
    return embed_ringbuf_read(rb, byte, 1) == 1;
}

bool embed_ringbuf_find(const embed_ringbuf_t *rb, uint8_t value, size_t *offset) {
    if (!usable(rb) || !offset)
        return false;
    const size_t cap = rb->capacity;
    const size_t tail = rb->tail;
    const size_t head = rb->head;
    RB_ACQUIRE();
    const size_t stored = count_between(head, tail, cap);
    size_t pos = tail;
    for (size_t i = 0; i < stored; ++i) {
        if (rb->buffer[pos] == value) {
            *offset = i;
            return true;
        }
        pos = pos + 1 == cap ? 0 : pos + 1;
    }
    return false;
}

size_t embed_ringbuf_read_linear(const embed_ringbuf_t *rb, const uint8_t **ptr) {
    if (!usable(rb) || !ptr)
        return 0;
    const size_t tail = rb->tail;
    const size_t head = rb->head;
    RB_ACQUIRE();
    *ptr = rb->buffer + tail;
    return head >= tail ? head - tail : rb->capacity - tail;
}

bool embed_ringbuf_read_commit(embed_ringbuf_t *rb, size_t n) {
    const uint8_t *unused;
    if (!usable(rb) || n > embed_ringbuf_read_linear(rb, &unused))
        return false;
    RB_RELEASE();
    rb->tail = wrap_add(rb->tail, n, rb->capacity);
    return true;
}

/* ---------------- 状態 ---------------- */

size_t embed_ringbuf_available(const embed_ringbuf_t *rb) {
    if (!usable(rb))
        return 0;
    return count_between(rb->head, rb->tail, rb->capacity);
}

size_t embed_ringbuf_free_space(const embed_ringbuf_t *rb) {
    if (!usable(rb))
        return 0;
    return (rb->capacity - 1) - embed_ringbuf_available(rb);
}

size_t embed_ringbuf_capacity(const embed_ringbuf_t *rb) {
    return usable(rb) ? rb->capacity - 1 : 0;
}

bool embed_ringbuf_is_empty(const embed_ringbuf_t *rb) { return embed_ringbuf_available(rb) == 0; }

bool embed_ringbuf_is_full(const embed_ringbuf_t *rb) {
    return usable(rb) && embed_ringbuf_free_space(rb) == 0;
}

void embed_ringbuf_clear(embed_ringbuf_t *rb) {
    if (rb) {
        rb->head = 0;
        rb->tail = 0;
    }
}
