#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "embed_modbus/modbus_slave.h"
#define EMBED_MODBUS_TCP_MAX_ADU 260
#define EMBED_MODBUS_TCP_PENDING 16
#define EMBED_MODBUS_TCP_PORT 502
/* Modbus TCP で装置を直接指定するときの Unit ID (Implementation Guide V1.0b 4.4.2) */
#define EMBED_MODBUS_TCP_UNIT_DIRECT 0xFF
typedef struct {
    uint16_t transaction, length;
    uint8_t unit;
} embed_mbap;
size_t embed_mbap_encode(const embed_mbap *h, uint8_t *dst, size_t cap);
size_t embed_mbap_decode(const uint8_t *src, size_t len, embed_mbap *h);

/* ---- サーバ ---- */
/* 完全な ADU を 1 つ処理し、応答 ADU 長を返す (応答なし・不正なら 0)。
 * Unit ID は slave->unit_id のほか、直接指定の 0xFF と 0 も受け付け、要求の値をそのまま返す。 */
size_t embed_modbus_tcp_handle(const modbus_slave_t *slave, const uint8_t *src, size_t len,
                               uint8_t *dst, size_t cap);
/* Connection state is caller-owned: one instance per socket. Feed may contain fragments or multiple
 * ADUs. */
typedef struct {
    uint8_t data[EMBED_MODBUS_TCP_MAX_ADU];
    size_t used;
    bool failed;
} embed_modbus_tcp_connection;
typedef void (*embed_modbus_tcp_send)(const uint8_t *, size_t, void *);
bool embed_modbus_tcp_feed(embed_modbus_tcp_connection *c, const modbus_slave_t *slave,
                           const uint8_t *data, size_t len, embed_modbus_tcp_send send, void *user);
/* RTU-over-TCP requires the caller to supply a complete RTU ADU (no MBAP). */
size_t embed_modbus_rtu_tcp_handle(const modbus_slave_t *, const uint8_t *, size_t, uint8_t *,
                                   size_t);

/* ---- ストリームの分割 (サーバ・クライアント共通) ---- */
/* TCP の受信バイト列を ADU 単位に分け、完全な ADU ごとに on_adu を呼ぶ。
 * 不正な MBAP を受けると failed を保持して false を返す (接続を切って状態を初期化すること)。 */
typedef void (*embed_modbus_tcp_adu_fn)(const uint8_t *adu, size_t len, void *user);
bool embed_modbus_tcp_frame(embed_modbus_tcp_connection *c, const uint8_t *data, size_t len,
                            embed_modbus_tcp_adu_fn on_adu, void *user);

/* ---- クライアント ---- */
/* 要求 PDU (modbus_master_req_* で作る) に MBAP を付けて ADU を作り、長さを返す。不正なら 0。 */
size_t embed_modbus_tcp_build_request(uint16_t transaction, uint8_t unit, const uint8_t *pdu,
                                      size_t pdu_len, uint8_t *dst, size_t cap);
/* 応答 ADU を検証して PDU を取り出す。PDU は modbus_master_parse_* で解釈する。
 * MODBUS_OK / MODBUS_ERR_INVALID_RESPONSE (MBAP・長さ不正) /
 * MODBUS_ERR_TRANSACTION_MISMATCH / MODBUS_ERR_ADDRESS_MISMATCH (Unit ID 不一致)。 */
modbus_status_t embed_modbus_tcp_parse_response(const uint8_t *adu, size_t len,
                                                uint16_t expected_transaction,
                                                uint8_t expected_unit, const uint8_t **pdu,
                                                size_t *pdu_len);

/* ---- Transaction ID の管理 (クライアント) ---- */
typedef struct {
    uint16_t next, ids[EMBED_MODBUS_TCP_PENDING];
    bool used[EMBED_MODBUS_TCP_PENDING];
    bool timed[EMBED_MODBUS_TCP_PENDING];
    uint32_t started[EMBED_MODBUS_TCP_PENDING];
} embed_modbus_transactions;
bool embed_modbus_transaction_begin(embed_modbus_transactions *, uint16_t *id);
/* 開始時刻を記録して begin する。embed_modbus_transaction_expire の対象になる。 */
bool embed_modbus_transaction_begin_at(embed_modbus_transactions *, uint32_t now_ms, uint16_t *id);
bool embed_modbus_transaction_end(embed_modbus_transactions *, uint16_t id);
/* now_ms - 開始時刻 >= timeout_ms の未完了 ID を解放し、最大 cap 件を expired に入れて件数を返す。
 * expired が NULL なら解放だけ行う。32bit ミリ秒の周回を考慮する。 */
size_t embed_modbus_transaction_expire(embed_modbus_transactions *, uint32_t now_ms,
                                       uint32_t timeout_ms, uint16_t *expired, size_t cap);
size_t embed_modbus_transaction_pending(const embed_modbus_transactions *);

#ifdef __cplusplus
}
#endif
