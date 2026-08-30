# embed_opcua (組み込み向けOPC-UAバイナリプロトコルスタック)

`embed_opcua` は、インダストリー4.0・スマートファクトリ・産業用IoT（IIoT）機器向けに設計された、ゼロ動的確保・軽量・高信頼な **OPC-UA (IEC 62541)** バイナリプロトコルスタックライブラリです。

---

## 📑 目次
1. [特徴とメリット](#-特徴とメリット)
2. [サポートしているデータ型 & サービス](#-サポートしているデータ型--サービス)
3. [トランスポート層 (`opc.tcp`)](#-トランスポート層-opctcp)
4. [C API リファレンス](#-c-api-リファレンス)
5. [C++ API リファレンス](#-c-api-リファレンス-1)
6. [スマートファクトリ導入例](#-スマートファクトリ導入例)

---

## 🎯 特徴とメリット

- **OPC-UA バイナリエンコーディング (`opc.tcp`)**: 仕様に準拠したリトルエンディアンのシリアライズ/デシリアライズ。
- **ゼロ動的メモリ確保**: `malloc` / `free` を一切使用せず、静的アドレス空間（最大ノード数設定可能）で安全に動作。
- **標準サービス実装**: Read / Write サービス、Hello / Acknowledge / Error ハンドシェイクに対応。
- **リッチなデータ型サポート**: Boolean, SByte, Byte, Int16, UInt16, Int32, UInt32, Int64, UInt64, Float, Double, String, StatusCode, DateTime をサポート。
- **C/C++両対応**: 組み込みC言語コアに加え、直感的なアドレス空間構築ができるモダンC++ラッパー (`embed_opcua::Server`) を提供。

---

## 📊 サポートしているデータ型 & サービス

### データ型 (Variant)
- `Boolean`, `Byte`, `SByte`
- `Int16`, `UInt16`, `Int32`, `UInt32`, `Int64`, `UInt64`
- `Float` (単精度浮動小数点), `Double` (倍精度浮動小数点)
- `String` (UTF-8文字列)
- `StatusCode` (OPC-UA標準ステータスコード)

### サービス & ハンドシェイク
- `Hello (HEL)` / `Acknowledge (ACK)` / `Error (ERR)` ハンドシェイク
- `Read` サービス (`OPCUA_ATTR_VALUE`, `OPCUA_ATTR_NODE_ID`, `OPCUA_ATTR_DISPLAY_NAME` 等)
- `Write` サービス (`OPCUA_ATTR_VALUE` 書き込み & アクセス権限チェック)

---

## 🌐 トランスポート層 (`opc.tcp`)

```text
+-----------------------+-----------------------+-------------------------------+
| MessageType (3 Bytes) | IsFinal (1 Byte)      | MessageSize (4 Bytes, uint32) |
| "HEL", "ACK", "MSG"   | 'F' (Final)           | 総バイト数 (リトルエンディアン)|
+-----------------------+-----------------------+-------------------------------+
| Payload (バイナリエンコードされたメッセージ本体)                             |
+-------------------------------------------------------------------------------+
```

---

## 📖 C API リファレンス

```c
#include "embed_opcua/opcua_server.h"

void app_main(void) {
    opcua_server_t server;
    opcua_server_init(&server, "opc.tcp://192.168.1.10:4840");

    // ノードの追加
    opcua_nodeid_t nid = { .namespace_index = 1, .id_type = OPCUA_NODEID_NUMERIC, .id.numeric = 1001 };
    opcua_variant_t init_val = { .type = OPCUA_TYPE_DOUBLE, .value.double_val = 25.4 };
    opcua_server_add_variable(&server, &nid, "Temperature", &init_val, 0x03 /* Read/Write */);

    // 値の更新
    opcua_variant_t new_val = { .type = OPCUA_TYPE_DOUBLE, .value.double_val = 26.1 };
    opcua_server_write_value(&server, &nid, &new_val);
}
```

---

## ⚡ C++ API リファレンス

```cpp
#include "embed_opcua/embed_opcua.hpp"

embed_opcua::Server server("opc.tcp://localhost:4840");

// アドレス空間へのノード登録
server.add_variable(1, 1001, "MotorSpeed_RPM", 1750.0);
server.add_variable(1, 1002, "InverterStatus", std::string("RUNNING"));

// 値の更新 & 読み出し
server.set_value(1, 1001, 1800.0);

opcua_datavalue_t dv{};
server.get_value(1, 1001, dv);
```
