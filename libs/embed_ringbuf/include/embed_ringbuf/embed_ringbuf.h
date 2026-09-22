#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint8_t *buffer;
    size_t capacity;
    volatile size_t head;
    volatile size_t tail;
} embed_ringbuf_t;

/**
 * @brief リングバッファを初期化します。
 */
void embed_ringbuf_init(embed_ringbuf_t *rb, uint8_t *storage, size_t capacity);

/**
 * @brief 1バイト書き込みます。
 */
bool embed_ringbuf_put(embed_ringbuf_t *rb, uint8_t byte);

/**
 * @brief 複数バイト書き込みます。
 */
size_t embed_ringbuf_write(embed_ringbuf_t *rb, const uint8_t *data, size_t length);

/**
 * @brief 1バイト読み出します。
 */
bool embed_ringbuf_get(embed_ringbuf_t *rb, uint8_t *byte);

/**
 * @brief 複数バイト読み出します。
 */
size_t embed_ringbuf_read(embed_ringbuf_t *rb, uint8_t *dest, size_t max_length);

/**
 * @brief 格納されているデータバイト数を取得します。
 */
size_t embed_ringbuf_available(const embed_ringbuf_t *rb);

/**
 * @brief 空き容量（バイト数）を取得します。
 */
size_t embed_ringbuf_free_space(const embed_ringbuf_t *rb);

/**
 * @brief バッファをクリア（空）にします。
 */
void embed_ringbuf_clear(embed_ringbuf_t *rb);

#ifdef __cplusplus
}
#endif
