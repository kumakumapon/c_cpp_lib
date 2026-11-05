/* embed_canopen の内部インタフェース (公開しない) */
#ifndef CANOPEN_INTERNAL_H
#define CANOPEN_INTERNAL_H

#include "embed_canopen/canopen_node.h"
#include <string.h>

#define CANOPEN_PDO_INVALID 0x80000000u
#define CANOPEN_PDO_EXTENDED 0x20000000u

enum { CANOPEN_SDO_IDLE = 0, CANOPEN_SDO_DOWNLOAD = 1, CANOPEN_SDO_UPLOAD = 2 };

/* ---- バイト順 (CANopen はリトルエンディアン) ---- */
static inline bool co_host_le(void) {
    const uint16_t one = 1;
    uint8_t b;
    memcpy(&b, &one, 1);
    return b == 1;
}
/* 数値型はホスト順 <-> リトルエンディアンを変換してコピー (文字列などはそのまま) */
static inline void co_copy(uint8_t *dst, const uint8_t *src, size_t n, bool numeric) {
    if (!numeric || co_host_le()) {
        if (n)
            memcpy(dst, src, n);
        return;
    }
    for (size_t i = 0; i < n; ++i)
        dst[i] = src[n - 1 - i];
}
static inline void co_put_le(uint8_t *p, uint32_t v, size_t n) {
    for (size_t i = 0; i < n; ++i)
        p[i] = (uint8_t)(v >> (8 * i));
}
static inline uint32_t co_get_le(const uint8_t *p, size_t n) {
    uint32_t v = 0;
    for (size_t i = 0; i < n; ++i)
        v |= (uint32_t)p[i] << (8 * i);
    return v;
}
/* 仮想オブジェクトの値を書き出す。容量不足なら OUT_OF_MEMORY */
static inline uint32_t co_put_value(uint8_t *buf, size_t cap, size_t *len, uint32_t v, size_t n) {
    if (cap < n)
        return CANOPEN_SDO_ABORT_OUT_OF_MEMORY;
    co_put_le(buf, v, n);
    if (len)
        *len = n;
    return 0;
}
/* 仮想オブジェクトへの書き込み長の検査 */
static inline uint32_t co_len_code(size_t len, size_t expected) {
    return len > expected ? CANOPEN_SDO_ABORT_LENGTH_HIGH
           : len < expected ? CANOPEN_SDO_ABORT_LENGTH_LOW
                            : 0;
}

/* ---- canopen_node.c ---- */
void canopen_queue_tx(canopen_node_t *node, uint32_t id, const uint8_t *data, uint8_t len);
/* 通信プロファイル・PDO の仮想オブジェクトを含むアクセス (ワイヤ形式) */
uint32_t canopen_obj_read(canopen_node_t *node, uint16_t index, uint8_t sub, uint8_t *buf,
                          size_t cap, size_t *len);
uint32_t canopen_obj_write(canopen_node_t *node, uint16_t index, uint8_t sub, const uint8_t *data,
                           size_t len);
/* 書き込みの事前検査 (分割ダウンロードの開始時)。size_known でなければ長さは見ない */
uint32_t canopen_obj_probe_write(canopen_node_t *node, uint16_t index, uint8_t sub,
                                 bool size_known, size_t size);
/* オブジェクトの本来の長さ (不明なら 0)。サイズ指定なしの加速型ダウンロードで使う */
size_t canopen_obj_size(canopen_node_t *node, uint16_t index, uint8_t sub);

/* ---- canopen_sdo.c ---- */
void canopen_sdo_process(canopen_node_t *node, const can_frame_t *rx);
void canopen_sdo_tick(canopen_node_t *node, uint32_t elapsed_ms);
void canopen_sdo_reset(canopen_node_t *node);

/* ---- canopen_pdo.c ---- */
void canopen_pdo_defaults(canopen_node_t *node);
void canopen_pdo_reset_runtime(canopen_node_t *node);
void canopen_pdo_on_operational(canopen_node_t *node);
void canopen_pdo_sync(canopen_node_t *node);
void canopen_pdo_tick(canopen_node_t *node, uint32_t elapsed_ms);
bool canopen_pdo_rx(canopen_node_t *node, const can_frame_t *rx);
/* PDO パラメータの仮想オブジェクト。扱ったら true を返し、結果を *code に入れる */
bool canopen_pdo_obj_read(canopen_node_t *node, uint16_t index, uint8_t sub, uint8_t *buf,
                          size_t cap, size_t *len, uint32_t *code);
bool canopen_pdo_obj_write(canopen_node_t *node, uint16_t index, uint8_t sub,
                           const uint8_t *data, size_t len, uint32_t *code);
bool canopen_pdo_is_obj(uint16_t index);
size_t canopen_pdo_obj_size(uint16_t index, uint8_t sub);

#endif /* CANOPEN_INTERNAL_H */
