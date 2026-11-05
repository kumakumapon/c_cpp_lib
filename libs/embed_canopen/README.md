# embed_canopen - 組み込み向け軽量 CANopen (CiA 301) スタック

車載・ファクトリーオートメーション・ロボティクス（AGV、モータドライブ、サーボ、分散I/O）向けの
軽量 **CANopen (CiA 301)** スレーブスタックです。動的メモリは使いません。

---

## 🌟 機能

| 機能 | 内容 |
| --- | --- |
| NMT | Start / Stop / Pre-Operational / Reset Node / Reset Communication。DLC=2 のみ受理。初期化・リセット後にブートアップ (0x700+ID, 0x00) を送信 |
| SDO サーバ | 加速型・分割型のアップロード/ダウンロード、トグル検査、無通信タイムアウト、CiA 301 のアボートコード、長さ検査 |
| PDO | TPDO/RPDO 各4本、1 PDO に最大8オブジェクト (バイト単位)。SYNC 周期 (1〜240)・非周期同期 (0)・イベント (254/255: イベントタイマ、値の変化検出、送信禁止時間)。同期型 RPDO は次の SYNC で反映 |
| PDO 設定 | API (`canopen_node_config_tpdo/rpdo`) と、マスタからの SDO (0x1400/0x1600/0x1800/0x1A00) の両方。CiA 301 の手順 (無効化 → マッピング → 有効化) を検査 |
| EMCY | エラーコード・エラーレジスタ (0x1001)・メーカー固有5バイト。履歴 0x1003 (0 を書くと消去) |
| Heartbeat | 生産 (0x1017、遅延時も位相を保持)、監視 (0x1016、途絶でコールバック + EMCY 0x8130) |
| OD | 型・アクセス権・書き込み検証フック (値の範囲チェックなど) |
| 送信キュー | 1回の処理で複数フレームを生成できる (SYNC で複数 TPDO など)。溢れた数を `tx_overflows` で確認 |

対応しない機能: ブロック転送 (アボート 0x05040001 を返す)、29bit COB-ID、RTR、Node Guarding、
LSS、不揮発保存 (0x1010/0x1011)。通信の安全性・規格適合認証は保証しません。

---

## 🚀 使い方

### 受信・周期処理と送信キュー

`canopen_node_process_frame` / `canopen_node_timer_tick` は生成フレームを送信キューに積み、
先頭の1つを返します。残りは `canopen_node_pop_tx` で取り出してください。

```c
static canopen_node_t node;
canopen_node_init(&node, 5, "Drive-Axis1");     /* ブートアップを送信キューへ */

void on_can_rx(const can_frame_t *rx) {
    can_frame_t tx;
    if (canopen_node_process_frame(&node, rx, &tx)) {
        can_send(&tx);
        while (canopen_node_pop_tx(&node, &tx))
            can_send(&tx);
    }
}

void every_1ms(void) {
    can_frame_t tx;
    if (canopen_node_timer_tick(&node, 1, &tx)) {
        can_send(&tx);
        while (canopen_node_pop_tx(&node, &tx))
            can_send(&tx);
    }
}
```

C++ では `Node::handle(rx, send)` / `Node::tick(ms, send)` が全フレームを `send` へ渡します。

### オブジェクトと PDO

```cpp
embed_canopen::Node node(5, "Drive-Axis1");
int32_t actual_position = 0;
uint16_t statusword = 0, controlword = 0;
node.add_variable(0x6064, 0x00, actual_position, OD_ACCESS_RO);
node.add_variable(0x6041, 0x00, statusword, OD_ACCESS_RO);
node.add_variable(0x6040, 0x00, controlword, OD_ACCESS_RW);

/* TPDO1: 位置 + ステータスを SYNC ごとに送信 (COB-ID 0x185) */
node.config_tpdo(1, {{0x6064, 0, 32}, {0x6041, 0, 16}}, 1);
/* RPDO1: コントロールワードを受信即反映 (COB-ID 0x205) */
node.config_rpdo(1, {{0x6040, 0, 16}}, 254);
```

送信種別 254/255 の TPDO は、Operational に入ったとき・値が変わったとき・イベントタイマ満了時・
`canopen_node_trigger_tpdo` 呼び出し時に送信します (送信禁止時間内は保留)。

### 書き込み検証フック

```c
static uint32_t check_speed(const canopen_od_entry_t *e, const void *data, size_t len, void *user) {
    int16_t v;
    memcpy(&v, data, sizeof(v));
    return (v < -3000 || v > 3000) ? CANOPEN_SDO_ABORT_VALUE_RANGE : 0; /* 0 なら受け入れ */
}
canopen_od_set_write_hook(&node.od, 0x6042, 0, check_speed, NULL);
```

フックは SDO・RPDO・`canopen_od_write` のすべてに適用され、拒否された値は書き込まれません。

### EMCY と Heartbeat 監視

```c
canopen_node_emcy(&node, CANOPEN_EMCY_TEMPERATURE, CANOPEN_ERR_REG_TEMPERATURE, NULL);
canopen_node_emcy(&node, CANOPEN_EMCY_NO_ERROR, 0, NULL);          /* エラー解除 */

canopen_node_add_heartbeat_consumer(&node, 1 /* マスタ */, 300 /* ms */);
canopen_node_callbacks_t cb = {0};
cb.on_heartbeat_timeout = on_master_lost;                           /* 安全停止など */
canopen_node_set_callbacks(&node, &cb, NULL);
```

### ローカルからの SDO 相当アクセス

`canopen_node_sdo_read/write` は SDO サーバと同じ経路 (仮想オブジェクト、検査、フック) で読み書きします。
値は CANopen のバイト順 (リトルエンディアン) です。

---

## ⚠ 旧版からの変更点

- 初期化・リセット後にブートアップを送信キューへ積みます。最初の `process_frame` / `timer_tick` は
  ブートアップを返すので、送信キューを空になるまで取り出してください。
- 4バイトを超えるオブジェクト (例: 0x1008 デバイス名) は分割転送で全体を返します (旧版は4バイトに切り詰め)。
- 加速型でないダウンロードの初期要求のサイズ欄を、旧版はデータとして書き込んでいました。分割転送として正しく処理します。
- 数値型への長さ違いの書き込みは `LENGTH_HIGH` / `LENGTH_LOW` で拒否します (旧版は切り詰め・部分書き込み)。
  `canopen_od_write` も同じ検査を行います。
- 存在しないサブインデックスは 0x06090011、範囲外の値はフックで 0x06090030 などを返します。
- RPDO1 の受信データを 0x2000.0 へ書き込む固定動作は廃止しました。`canopen_node_config_rpdo` でマッピングしてください。
- 拡張フレーム・リモートフレーム・DLC が規定と違う NMT / SDO は無視します。
- `canopen_node_t` は名前付き構造体 `struct canopen_node` になり、`canopen_od_entry_t` に検証フックが加わりました。

---

## 🛠️ テスト

```bash
ctest --test-dir build -R test_canopen --output-on-failure
```

`test_canopen_spec` は CiA 301 の定義どおりのバイト列で、NMT とブートアップ、SDO の各コマンド指定子
(加速型の n ビット、分割転送のトグル・最終セグメント)、全アボートコード、PDO のマッピングと
COB-ID・リトルエンディアン、SYNC・イベントタイマ・変化検出・送信禁止時間、RPDO の長さ不足時の EMCY、
マスタ手順による PDO 設定、EMCY と 0x1003、Heartbeat 監視を固定しています。
