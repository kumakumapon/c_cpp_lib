#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* 単一生産者・単一消費者 (SPSC) のバイトリング。
 * 生産者 (put/write/write_commit) は head だけを、消費者 (get/read/skip/read_commit) は tail だけを
 * 更新し、データと位置の間にメモリバリアを置くので、ISR とメインループの間はロックなしで使える。
 * 位置 (size_t) の読み書きが 1 命令で行えない 8bit MCU では、呼び出し側で割込み禁止が必要。 */
typedef struct {
    uint8_t *buffer;
    size_t capacity;
    volatile size_t head; /* 次に書く位置 (生産者のみ更新) */
    volatile size_t tail; /* 次に読む位置 (消費者のみ更新) */
} embed_ringbuf_t;

/** 初期化。capacity は 2 以上、有効容量は capacity - 1。 */
void embed_ringbuf_init(embed_ringbuf_t *rb, uint8_t *storage, size_t capacity);

/* ---- 生産者側 ---- */
/** 1 バイト書き込む。満杯なら false。 */
bool embed_ringbuf_put(embed_ringbuf_t *rb, uint8_t byte);
/** 入るだけ書き込み、書いたバイト数を返す。 */
size_t embed_ringbuf_write(embed_ringbuf_t *rb, const uint8_t *data, size_t length);
/** 連続した空き領域の先頭を *ptr に返し、その長さを返す (DMA 受信・ゼロコピー用)。
 *  書き終えたら embed_ringbuf_write_commit で確定する。 */
size_t embed_ringbuf_write_linear(embed_ringbuf_t *rb, uint8_t **ptr);
/** write_linear で得た領域のうち先頭 n バイトを確定する。n が連続空き長を超えれば false。 */
bool embed_ringbuf_write_commit(embed_ringbuf_t *rb, size_t n);

/* ---- 消費者側 ---- */
/** 1 バイト読み出す。空なら false。 */
bool embed_ringbuf_get(embed_ringbuf_t *rb, uint8_t *byte);
/** 最大 max_length バイト読み出し、読んだバイト数を返す。 */
size_t embed_ringbuf_read(embed_ringbuf_t *rb, uint8_t *dest, size_t max_length);
/** 消費せずに先頭から offset 番目の 1 バイトを読む。範囲外なら false。 */
bool embed_ringbuf_peek(const embed_ringbuf_t *rb, size_t offset, uint8_t *byte);
/** 消費せずに先頭 offset から最大 max_length バイトをコピーし、コピー数を返す。 */
size_t embed_ringbuf_peek_bytes(const embed_ringbuf_t *rb, size_t offset, uint8_t *dest,
                                size_t max_length);
/** 先頭から最大 n バイトを捨て、捨てたバイト数を返す。 */
size_t embed_ringbuf_skip(embed_ringbuf_t *rb, size_t n);
/** 先頭から value を探し、見つかれば先頭からの位置を *offset に入れて true (行区切りの検出等)。 */
bool embed_ringbuf_find(const embed_ringbuf_t *rb, uint8_t value, size_t *offset);
/** 連続した格納済み領域の先頭を *ptr に返し、その長さを返す (DMA 送信・ゼロコピー用)。 */
size_t embed_ringbuf_read_linear(const embed_ringbuf_t *rb, const uint8_t **ptr);
/** read_linear で得た領域のうち先頭 n バイトを消費する。n が連続格納長を超えれば false。 */
bool embed_ringbuf_read_commit(embed_ringbuf_t *rb, size_t n);

/* ---- 状態 ---- */
/** 格納されているデータバイト数。 */
size_t embed_ringbuf_available(const embed_ringbuf_t *rb);
/** 空き容量 (バイト数)。 */
size_t embed_ringbuf_free_space(const embed_ringbuf_t *rb);
/** 有効容量 (capacity - 1)。未初期化・不正なら 0。 */
size_t embed_ringbuf_capacity(const embed_ringbuf_t *rb);
bool embed_ringbuf_is_empty(const embed_ringbuf_t *rb);
bool embed_ringbuf_is_full(const embed_ringbuf_t *rb);
/** バッファを空にする。生産者・消費者の双方が止まっているときだけ呼ぶこと。 */
void embed_ringbuf_clear(embed_ringbuf_t *rb);

#ifdef __cplusplus
}
#endif
