# embed_modbus_tcp

ソケットに依存しないModbus TCPフレーミング層です。MBAPのProtocol ID=0、Length=2〜254、
Transaction IDとUnit IDを検証・保持し、既存 `modbus_slave_process_pdu` を再利用します。
通信の接続・送受信・タイムアウト・認証はアプリケーションが担当します。

- `embed_mbap_encode/decode`: 7バイトのヘッダ。decodeは完全なペイロードの到着を要求しません。
- `embed_modbus_tcp_handle`: 完全なADUを1つ処理。宛先不一致・不正長は0。
  Unit ID は `slave->unit_id` に加え、TCPで装置を直接指定する `0xFF`（`EMBED_MODBUS_TCP_UNIT_DIRECT`、
  Implementation Guide V1.0b）と `0` も受け付け、応答には要求のUnit IDをそのまま入れます。
  副作用を起こすPDU処理の前に、出力容量260バイト以上を要求します。
- `embed_modbus_tcp_feed`: ソケットごとにゼロ初期化した `embed_modbus_tcp_connection` を保持。
  分割受信・連結ADUを処理し、応答をsendコールバックへ通知します。
  応答配列はコールバック中のみ有効です。非同期送信ならコピーしてください。
  不正MBAPではfailedを保持してfalseを返します。接続を切り、状態を初期化して再接続します。
- `embed_modbus_transaction_begin/end`: ゼロ初期化した管理構造体で最大16件を同時追跡。
  16bitの周回時も未完了IDと衝突しません。応答・タイムアウト・切断時にendしてください。
- `embed_modbus_rtu_tcp_handle`: CRC付きRTU ADUをそのまま処理。出力容量256バイト以上。
  RTU-over-TCPにはMBAP長がないため、呼び出し側で完全なフレーム境界を決定します。

独立したconnectionを用意すれば複数接続を処理できます。同一slaveの共有レジスタへの
並行アクセスはアプリケーションで直列化してください。コールバックから同一connectionを再入しないでください。

bridgeでは `bridge_router_bind_modbus` の後に `bridge_router_init_modbus_slave` を呼ぶと、
FC03/06/16がルーターと同じ保持レジスタへ接続されます。router・配列はslaveより長生きする必要があります。

## クライアント (マスタ) 側

```c
uint8_t pdu[5], adu[EMBED_MODBUS_TCP_MAX_ADU];
size_t pdu_len;
uint16_t tid;
modbus_master_req_read_holding_registers(0x006B, 3, pdu, &pdu_len);
embed_modbus_transaction_begin_at(&transactions, millis(), &tid);
size_t n = embed_modbus_tcp_build_request(tid, EMBED_MODBUS_TCP_UNIT_DIRECT, pdu, pdu_len,
                                          adu, sizeof(adu));
send(sock, adu, n, 0);

/* 受信: TCP のバイト列を ADU に分割 (分割受信・連結受信に対応) */
void on_adu(const uint8_t *adu, size_t len, void *user) {
    embed_mbap h;
    embed_mbap_decode(adu, len, &h);
    const uint8_t *rpdu;
    size_t rlen;
    if (embed_modbus_tcp_parse_response(adu, len, h.transaction, 0xFF, &rpdu, &rlen) == MODBUS_OK &&
        embed_modbus_transaction_end(&transactions, h.transaction)) {
        uint16_t values[3];
        modbus_master_parse_read_registers_resp(rpdu, rlen, values, 3); /* 例外応答も判定 */
    }
}
embed_modbus_tcp_frame(&conn, rx, rx_len, on_adu, NULL);

/* タイムアウト: 応答のない Transaction ID を解放 */
uint16_t expired[4];
size_t k = embed_modbus_transaction_expire(&transactions, millis(), 1000, expired, 4);
```

- `embed_modbus_tcp_frame`: サーバ・クライアント共通のストリーム分割。`embed_modbus_tcp_feed` はこれの上に実装しています。
- `embed_modbus_tcp_parse_response`: MBAP・長さ・Transaction ID・Unit ID を検証して PDU を返します
  (`MODBUS_ERR_TRANSACTION_MISMATCH` は `embed_modbus` の `modbus_status_t` に追加)。
- `embed_modbus_transaction_begin_at` / `expire`: 開始時刻を記録し、期限切れの ID を解放します。
  時刻は単調増加で渡してください（32bit ミリ秒の周回は考慮済み）。
  `begin` で始めた ID は時刻を持たないため期限切れになりません。

実際のソケットで動くサーバ / クライアントは `apps/modbus_tcp_demo` を参照してください。
