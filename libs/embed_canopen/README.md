# embed_canopen - 組み込み向け軽量 CANopen (CiA 301 / CiA 402) スタック

車載・ファクトリーオートメーション・ロボティクス（AGV、モータドライブ、サーボ、分散I/O）向けの軽量 **CANopen (CiA 301)** プロトコルスタックライブラリです。

---

## 🌟 特徴 (Key Features)

- **ゼロ動的メモリ確保 (Zero Allocation)**:
  - 固定長オブジェクトディクショナリ（OD）構造により動的メモリを一切使用せず、決定論的な高速動作を実現。
- **CiA 301 通信プロファイル準拠**:
  - **NMT (Network Management)**: Initialising, Pre-Operational, Operational, Stopped の状態遷移とマスター指示応答。
  - **OD (Object Dictionary)**: 標準通信オブジェクト（0x1000〜0x1018）の自動初期化とカスタムインデックス（0x2000〜/0x6000〜）登録。
  - **SDO (Service Data Object)**: 加速型 (Expedited) アップロード（読み出し）・ダウンロード（書き込み）およびエラーアボートコード生成。
  - **PDO (Process Data Object)**: 高速リアルタイム通信用 TPDO1 / RPDO1 送受信処理。
  - **Heartbeat / Producer Timer**: 周期的な状態報知メッセージ (0x700 + NodeID) の自動生成。
- **C11 / C++20 デュアルAPI**:
  - 純C言語コア実装 (`canopen_node_t`) と直感的なモダン C++ ラッパー (`embed_canopen::Node`) を提供。

---

## 📁 ディレクトリ構成

```text
libs/embed_canopen/
├── include/embed_canopen/
│   ├── canopen_types.h    # CANフレーム、COB-ID、NMT状態、ODデータ型定義
│   ├── canopen_od.h       # オブジェクトディクショナリ (OD) 管理
│   ├── canopen_node.h     # CANopen ノード & 通信ステートマシン (C API)
│   └── embed_canopen.hpp  # C++ ノードラッパークラス
├── src/
│   ├── canopen_od.c
│   └── canopen_node.c
├── tests/
│   ├── CMakeLists.txt
│   └── test_canopen.cpp   # 単体テストスイート
└── README.md
```

---

## 🚀 使い方 (Usage Examples)

### C++ API による CANopen スレーブノードの構築

```cpp
#include "embed_canopen/embed_canopen.hpp"

int main() {
    // 1. ノード初期化 (NodeID: 1, デバイス名: "Drive-Axis1")
    embed_canopen::Node node(1, "Drive-Axis1");

    // 2. モータ目標位置 (0x607A.0) と現在位置 (0x6064.0) を OD に登録
    int32_t target_position = 0;
    int32_t actual_position = 1000;
    node.add_variable(0x607A, 0x00, target_position, OD_ACCESS_RW);
    node.add_variable(0x6064, 0x00, actual_position, OD_ACCESS_RO);

    // 3. 受信CANフレームのディスパッチ (SDO / NMT / RPDO)
    can_frame_t rx_frame{ /* CANバスから受信 */ };
    can_frame_t tx_frame{};
    if (node.process_frame(rx_frame, tx_frame)) {
        // tx_frame を CAN バスに送信 (SDO 応答等)
    }

    // 4. 周期タイマー処理 (Heartbeat 生成)
    if (node.timer_tick(10, tx_frame)) {
        // Heartbeat フレーム送信
    }

    // 5. リアルタイム高速データ送信 (TPDO1: 現在位置)
    if (node.nmt_state() == CANOPEN_NMT_STATE_OPERATIONAL) {
        node.send_tpdo1((const uint8_t*)&actual_position, sizeof(actual_position), tx_frame);
    }
}
```

---

## 🛠️ ビルドとテスト

```bash
# テスト実行
ctest --test-dir build -R test_canopen --output-on-failure
```
