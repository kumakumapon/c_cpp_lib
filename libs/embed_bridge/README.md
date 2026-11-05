# embed_bridge - 産業用マルチプロトコル・データルーター / ゲートウェイ

Modbus, OPC-UA, EtherNet/IP (CIP), CANopen (CiA 301), MQTT 間のデータ同期・ルーティングを行う軽量 **産業用プロトコルコンバータ / データルーター** ライブラリです。

---

## 🌟 特徴 (Key Features)

- **マルチプロトコル・相互接続 (Any-to-Any Tag Routing)**:
  - **Modbus (RTU/TCP)**: 保持レジスタ (Holding Registers) / 入力レジスタ
  - **OPC-UA**: 変数ノード (Variable Nodes)
  - **EtherNet/IP (CIP)**: I/O アセンブリ (Assembly Instance + Byte Offset)
  - **CANopen (CiA 301)**: オブジェクトディクショナリ (Index / SubIndex)
  - **MQTT**: トピック発行 (JSON / テキスト / バイナリペイロード)
  - **Raw Memory**: アプリケーション内直接変数
- **スケーリング & 単位変換 (Scaling & Offset)**:
  - $Y = (X \times Scale) + Offset$ によるエンジニアリング単位変換（例: ADC値 0〜1023 ⇔ 温度 0.0〜100.0 ℃）。
- **多彩な同期トリガー方式**:
  - **`BRIDGE_SYNC_ALWAYS`**: 毎サイクル全同期
  - **`BRIDGE_SYNC_ON_CHANGE`**: 値変化検知 (COV: Change of Value) 時のみ転送
  - **`BRIDGE_SYNC_PERIODIC`**: 指定周期 (ミリ秒) ごとの定期転送
- **ゼロ動的メモリ確保 (Zero Allocation)**:
  - 静的固定長ルーティングテーブルにより組み込みマイコンでも安全・高速に動作。

---

## 📁 ディレクトリ構成

```text
libs/embed_bridge/
├── include/embed_bridge/
│   ├── bridge_types.h     # エンドポイント定義、データ型、マッピングルール
│   ├── bridge_router.h    # ルーターエンジン (C API)
│   └── embed_bridge.hpp   # C++ ルーターラッパークラス
├── src/
│   └── bridge_router.c
├── tests/
│   ├── CMakeLists.txt
│   └── test_bridge.cpp    # 単体テストスイート
└── README.md
```

---

## 🚀 使い方 (Usage Examples)

### C++ API による産業用ゲートウェイ構築

```cpp
#include "embed_bridge/embed_bridge.hpp"
#include "embed_opcua/embed_opcua.hpp"
#include "embed_eip/embed_eip.hpp"
#include "embed_canopen/embed_canopen.hpp"

int main() {
    embed_bridge::Router router;

    // 1. 各プロトコルインスタンスのバインド
    uint16_t modbus_regs[100];
    router.bind_modbus(modbus_regs, 100);

    embed_opcua::Server opcua_server("opc.tcp://localhost:4840");
    router.bind_opcua(opcua_server.raw_handle());

    embed_eip::Adapter eip_adapter(0x002A, 0x000C, 0x0001, "EIP-Gateway");
    router.bind_eip(eip_adapter.raw_handle());

    embed_canopen::Node can_node(1, "CANopen-Axis1");
    router.bind_canopen(can_node.raw_handle());

    // 2. ルーティングルールの設定
    // (a) Modbus レジスタ 0 (温度 x 10) -> OPC-UA ノード (ID 2001) へ実数変換転送
    opcua_nodeid_t temp_node{OPCUA_ID_NUMERIC, 1, {.numeric = 2001}};
    router.map_modbus_to_opcua(0, temp_node, BRIDGE_TYPE_FLOAT, 0.1f, 0.0f);

    // (b) EtherNet/IP アセンブリ 100 オフセット 0 -> MQTT トピック "factory/line1/pressure"
    router.map_eip_to_mqtt(100, 0, "factory/line1/pressure", BRIDGE_TYPE_FLOAT, BRIDGE_SYNC_ON_CHANGE);

    // (c) CANopen モータ速度 (0x606C.0) -> Modbus レジスタ 10
    router.map_canopen_to_modbus(0x606C, 0x00, 10, BRIDGE_TYPE_UINT16);

    // 3. ループ内での定期同期
    while (true) {
        router.tick(10); // 10ms 周期処理
        // 各プロトコル通信処理
    }
}
```

---

## 🛠️ ビルドとテスト

```bash
# テスト実行
ctest --test-dir build -R test_bridge --output-on-failure
```

---

## 🆕 不感帯・ルール管理・MQTT からの書き込み

| 機能 | API / フィールド | 内容 |
| --- | --- | --- |
| 不感帯 | `bridge_rule_t.deadband` | `BRIDGE_SYNC_ON_CHANGE` で、前回転送値との差 (スケール変換後の宛先の単位) がこれ以下なら転送しない |
| 有効 / 無効 | `bridge_router_set_rule_active` | 再開時は値が同じでも 1 回転送 |
| 統計 | `bridge_router_get_rule` → `sync_count` / `error_count` | 読み出し・書き込み失敗を数える (ゲートウェイの診断用) |
| MQTT → 機器 | `bridge_router_on_mqtt_message` | 送信元が MQTT トピックのルールに、受信ペイロード (数値テキスト / true・false・on・off) を流す |

```c
/* クラウドから設定温度を受け、Modbus レジスタ 10 へ 0.1 ℃ 単位で書く */
bridge_rule_t sp = {0};
sp.src.proto = BRIDGE_PROTO_MQTT_TOPIC;
strcpy(sp.src.target.mqtt_topic, "plant/setpoint");
sp.src_type = BRIDGE_TYPE_FLOAT;
sp.dst.proto = BRIDGE_PROTO_MODBUS_REG;
sp.dst.target.modbus_addr = 10;
sp.dst_type = BRIDGE_TYPE_UINT16;
sp.scale = 10.0f;
bridge_router_add_rule(&router, &sp);

void on_message(mqtt_client_t *c, const mqtt_message_t *m, void *user) {
    /* mqtt_message_t の topic は NUL 終端でないので _n 版を使う */
    bridge_router_on_mqtt_message_n(&router, m->topic, m->topic_len, m->payload, m->payload_len);
}
```

送信元が MQTT のルールは `sync_all` / `tick` では読みません (受信時だけ動きます)。

### 修正した不具合

- 宛先が Modbus で型が BOOL のとき、何も書かずに成功を返していた → 0 / 1 を書く。
- `BRIDGE_TYPE_RAW_BYTES` が `data_size` に関係なく 2 バイトしかコピーしなかった → `data_size` バイトをコピー
  (生バイトと数値型の混在、長さ 0 は登録時に拒否)。Modbus へは 1 レジスタ 2 バイト (上位が先)。
- 周期ルールのタイマを 0 に戻していたため、呼び出し間隔の端数だけ周期がずれていった → 位相を保持。
- EtherNet/IP のアセンブリを超える書き込みが、黙って切り捨てられて成功扱いだった → 失敗。
- CANopen の OD を長さの違う型で読むと、先頭バイトだけを読んでいた → 長さが一致しなければ失敗。

`test_bridge_spec` で各エンドポイントの変換と、上記の回帰を固定しています。
