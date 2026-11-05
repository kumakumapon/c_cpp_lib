#pragma once
/*
 * embed_frame: バイトストリーム (UART / USB CDC / TCP) 上でパケットの境界を作るフレーミング。
 *
 * - COBS (Consistent Overhead Byte Stuffing): データ中の 0x00 を取り除き、0x00 を区切りに使う。
 *   オーバーヘッドは最大 ceil(n / 254) バイトで、最悪でも約 0.4%。
 * - SLIP (RFC 1055): 0xC0 (END) を区切り、0xDB (ESC) でエスケープする。
 *
 * どちらも動的確保なし。ストリーム復号器は 1 バイトずつ渡せる (UART 割込みやリングバッファから)。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EMBED_FRAME_IN_PROGRESS = 0, /* フレーム受信中 */
    EMBED_FRAME_COMPLETE = 1,    /* 1 フレーム完成 (frame / len が有効) */
    EMBED_FRAME_ERROR = -1       /* 不正な符号化またはバッファ超過。そのフレームは捨てた */
} embed_frame_status;

/* ---------------- COBS ---------------- */
#define EMBED_COBS_DELIMITER 0x00
/* len バイトを符号化したときの最大長 (区切りの 0x00 は含まない) */
#define EMBED_COBS_MAX_ENCODED(len) ((len) + (len) / 254u + 1u)

/* src を符号化して dst へ書く (区切りの 0x00 は付けない)。容量不足なら false。 */
bool embed_cobs_encode(const uint8_t *src, size_t len, uint8_t *dst, size_t cap, size_t *out_len);
/* 区切りを含まない符号化データを復号する。dst == src のその場復号も可。不正な符号化なら false。 */
bool embed_cobs_decode(const uint8_t *src, size_t len, uint8_t *dst, size_t cap, size_t *out_len);

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
    bool overflow;
    uint32_t frames;
    uint32_t errors;
} embed_cobs_decoder_t;
/* buf はフレームの最大符号化長以上。完成したフレームは buf 上に復号される (次の feed まで有効)。 */
void embed_cobs_decoder_init(embed_cobs_decoder_t *d, uint8_t *buf, size_t cap);
embed_frame_status embed_cobs_decoder_feed(embed_cobs_decoder_t *d, uint8_t byte,
                                           const uint8_t **frame, size_t *len);
void embed_cobs_decoder_reset(embed_cobs_decoder_t *d);

/* ---------------- SLIP (RFC 1055) ---------------- */
#define EMBED_SLIP_END 0xC0
#define EMBED_SLIP_ESC 0xDB
#define EMBED_SLIP_ESC_END 0xDC
#define EMBED_SLIP_ESC_ESC 0xDD
/* 最大長: 先頭と末尾の END + 全バイトがエスケープされた場合 */
#define EMBED_SLIP_MAX_ENCODED(len) (2u * (len) + 2u)

/* 先頭と末尾に END を付けて符号化する (先頭の END は回線のノイズを区切るため: RFC 1055)。 */
bool embed_slip_encode(const uint8_t *src, size_t len, uint8_t *dst, size_t cap, size_t *out_len);

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
    bool escape;
    bool error;
    uint32_t frames;
    uint32_t errors;
} embed_slip_decoder_t;
void embed_slip_decoder_init(embed_slip_decoder_t *d, uint8_t *buf, size_t cap);
/* 空のフレーム (END の連続) は無視する。 */
embed_frame_status embed_slip_decoder_feed(embed_slip_decoder_t *d, uint8_t byte,
                                           const uint8_t **frame, size_t *len);
void embed_slip_decoder_reset(embed_slip_decoder_t *d);

#ifdef __cplusplus
}
#endif
