# embed_eip - 組み込み向け軽量 EtherNet/IP (CIP) プロトコルスタック

ODVA標準規格に準拠した、産業用オートメーション（PLC、分散I/O、センサ、アクチュエータ、インバータ等）向けの軽量 **EtherNet/IP (Common Industrial Protocol over TCP/IP)** プロトコルスタックライブラリです。

---

## 🌟 特徴 (Key Features)

- **ゼロ動的メモリ確保 (Zero Allocation)**:
  - `malloc` / `free` を一切使用せず、静的固定バッファのみで動作。
  - メモリ断片化やヒープ枯渇のリスクを排除し、高いリアルタイム性を実現。
- **カプセル化レイヤ (Encapsulation Layer)**:
  - 24バイトの EtherNet/IP カプセル化ヘッダ、Common Packet Format (CPF) に対応。
  - `RegisterSession`, `UnRegisterSession`, `SendRRData` (UCMM 明示的メッセージング) をサポート。
- **CIP (Common Industrial Protocol) コア**:
  - EPATH (8-bit / 16-bit Logical Class, Instance, Attribute) のエンコード・デコード。
  - `Get_Attribute_Single` (0x0E), `Set_Attribute_Single` (0x10) サービス。
- **標準CIPオブジェクト内蔵**:
  - **Identity Object (Class 0x01)**: Vendor ID, Device Type, Product Code, Revision, Status, Serial Number, Product Name
  - **Assembly Object (Class 0x04)**: 入力/出力/設定アセンブリのインスタンス管理とI/Oデータアクセス
- **C11 / C++20 デュアルAPI**:
  - 純C言語コア実装 (`eip_adapter_t`) と直感的なモダン C++ ラッパー (`embed_eip::Adapter`) を提供。

---

## 📁 ディレクトリ構成

```text
libs/embed_eip/
├── include/embed_eip/
│   ├── eip_types.h        # カプセル化コマンド、ステータス、CIPサービスコード、標準クラス定義
│   ├── eip_encap.h        # 24Bカプセル化ヘッダ & CPFパケットシリアライザ
│   ├── eip_cip.h          # CIP EPATH、Get/Set Attribute、要求・応答シリアライザ
│   ├── eip_adapter.h      # EtherNet/IP アダプタセッション・アセンブリ管理 (C API)
│   └── embed_eip.hpp      # C++ EtherNet/IP アダプタクラス
├── src/
│   ├── eip_encap.c
│   ├── eip_cip.c
│   └── eip_adapter.c
└── tests/
    └── test_eip.cpp       # 単体テストスイート
```

---

## 🚀 使い方 (Usage Examples)

### C++ API による EtherNet/IP アダプタ構築

```cpp
#include "embed_eip/embed_eip.hpp"

// センサ入力アセンブリ構造体
struct SensorData {
    float temperature;
    float pressure;
    uint16_t status;
};

// 制御出力アセンブリ構造体
struct MotorCommand {
    uint16_t target_rpm;
    uint8_t run_flag;
};

int main() {
    // 1. アダプタの初期化 (Vendor ID: 0x002A, Product Name: "IO-Node")
    embed_eip::Adapter adapter(0x002A, 0x000C, 0x0001, "IO-Node");

    // 2. I/O アセンブリの登録 (Input: Inst 100, Output: Inst 150)
    adapter.add_assembly(100, sizeof(SensorData));
    adapter.add_assembly(150, sizeof(MotorCommand));

    // 3. センサ入力データの更新
    SensorData sensor{25.4f, 101.3f, 0x0001};
    adapter.set_assembly_pod(100, sensor);

    // 4. TCP受信パケット (ポート44818) のディスパッチ処理
    // uint8_t rx_buf[1024]; int rx_len = socket.read(rx_buf, ...);
    // uint8_t tx_buf[1024];
    // size_t tx_len = eip_adapter_process_packet(adapter.raw_handle(), rx_buf, rx_len, tx_buf, sizeof(tx_buf));
    // socket.write(tx_buf, tx_len);

    // 5. PLCからの出力指示の取得
    MotorCommand cmd{};
    if (adapter.get_assembly_pod(150, cmd)) {
        // モータ制御を実行
    }
}
```

---

## 📊 サポートするCIPサービスとクラス

| クラスID | クラス名 | インスタンス | アトリビュート | サービス |
|---|---|---|---|---|
| `0x0001` | **Identity Object** | 1 | 1〜7 (Vendor ID, Device Type, Product Code, Revision, Status, Serial, Name) | `Get_Attribute_Single` (0x0E) |
| `0x0004` | **Assembly Object** | 1〜65535 | 3 (Data) | `Get_Attribute_Single` (0x0E)<br>`Set_Attribute_Single` (0x10) |

---

## 🛠️ ビルドとテスト

```bash
# ビルド
cmake -B build -G "NMake Makefiles"
cmake --build build

# 単体テスト実行
ctest --test-dir build -R test_eip --output-on-failure

# デモアプリケーション実行
./build/bin/eip_demo.exe
```
