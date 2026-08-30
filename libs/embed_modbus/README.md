# embed_modbus (組み込み向けModbusプロトコルスタック)

`embed_modbus` は、産業用制御機器やIoTセンサノード向けに設計された、ゼロ動的確保・軽量・高信頼な **Modbus RTU / ASCII** 通信プロトコルスタックライブラリです。

---

## 📑 目次
1. [特徴とメリット](#-特徴とメリット)
2. [サポートしているファンクションコード](#-サポートしているファンクションコード)
3. [フレーム構造とCRC仕様](#-フレーム構造とcrc仕様)
4. [C API リファレンス](#-c-api-リファレンス)
5. [C++ API リファレンス](#-c-api-リファレンス-1)
6. [実装例・ユースケース](#-実装例ユースケース)

---

## 🎯 特徴とメリット

- **Master (Client) & Slave (Server) 両対応**: PLCやホスト側からの要求処理、センサノード側の応答生成を完備。
- **ゼロ動的メモリ確保**: `malloc` / `free` を一切使用せず、固定スタックバッファおよびコールバックベースで動作。
- **標準例外コード完全準拠**: アドレス範囲外 (`0x02`) や不正データ値 (`0x03`)、未サポート機能 (`0x01`) に対して自動で適切な例外PDUを返却。
- **高速CRC-16計算**: テーブル駆動による高速な Modbus RTU CRC-16 (多項式 `0xA001`) 計算。
- **C/C++両対応**: 組み込みC言語APIに加え、マップベースで簡単にレジスタを定義できるモダンC++ラッパー (`embed_modbus::Slave`) を提供。

---

## 📋 サポートしているファンクションコード

| FC | 名称 | 種別 | 説明 |
| :--- | :--- | :--- | :--- |
| `0x01` | Read Coils | ビット読み出し | コイル (0x) のON/OFF状態を読み出し |
| `0x02` | Read Discrete Inputs | ビット読み出し | 離散入力 (1x) の状態を読み出し |
| `0x03` | Read Holding Registers | ワード読み出し | 保持レジスタ (4x, 16bit) を読み出し |
| `0x04` | Read Input Registers | ワード読み出し | 入力レジスタ (3x, 16bit) を読み出し |
| `0x05` | Write Single Coil | ビット書き込み | 単一コイルのON/OFFを設定 (`0xFF00` / `0x0000`) |
| `0x06` | Write Single Register | ワード書き込み | 単一保持レジスタ (16bit) を書き込み |
| `0x0F` | Write Multiple Coils | ビット一括書き込み | 連続する複数コイルを一括書き込み |
| `0x10` | Write Multiple Registers | ワード一括書き込み | 連続する複数保持レジスタを一括書き込み |

---

## 📦 フレーム構造とCRC仕様

### Modbus RTU フレーム (ADU)
```text
+----------------+-------------------------------+----------------------+
| Slave ID (1B)  | Function Code & Data (PDU)    | CRC-16 (2B)          |
| 1 〜 247       | 可変長 (最大 253 バイト)       | リトルエンディアン    |
+----------------+-------------------------------+----------------------+
```

- **CRC-16 計算**: 多項式 `0xA001` (反転 `0x8005`)、初期値 `0xFFFF`
- **エンディアン**: CRCはフレーム末尾に **下位バイト先** (Little-Endian) で格納

---

## 📖 C API リファレンス

### スレーブ (Server) 側
```c
#include "embed_modbus/modbus_slave.h"
#include "embed_modbus/modbus_rtu.h"

// コールバックの定義
static modbus_exception_t my_read_holding(uint16_t addr, uint16_t count, uint16_t *dest, void *user_data) {
    for (uint16_t i = 0; i < count; ++i) {
        dest[i] = read_sensor_register(addr + i);
    }
    return MODBUS_EX_NONE;
}

modbus_slave_t slave;
modbus_slave_callbacks_t cbs = { .read_holding_registers = my_read_holding };
modbus_slave_init(&slave, 1 /* Unit ID */, &cbs, NULL);

// 受信RTUフレームの処理
uint8_t resp_adu[256];
size_t resp_len = 0;
if (modbus_rtu_slave_handle(&slave, rx_buf, rx_len, resp_adu, &resp_len)) {
    rs485_transmit(resp_adu, resp_len);
}
```

### マスター (Client) 側
```c
#include "embed_modbus/modbus_master.h"
#include "embed_modbus/modbus_rtu.h"

// 保持レジスタ読み出しリクエスト作成
uint8_t req_pdu[5];
size_t req_pdu_len = 0;
modbus_master_req_read_holding_registers(0x0000, 2, req_pdu, &req_pdu_len);

// RTUフレーム化
uint8_t req_adu[256];
size_t req_adu_len = 0;
modbus_rtu_encode_frame(1 /* 宛先Unit ID */, req_pdu, req_pdu_len, req_adu, &req_adu_len);
rs485_transmit(req_adu, req_adu_len);
```

---

## ⚡ C++ API リファレンス

```cpp
#include "embed_modbus/embed_modbus.hpp"

embed_modbus::Slave slave(1 /* Unit ID */);

// レジスタの直接登録
slave.set_holding_register(0x0000, 255); // 25.5 ℃
slave.set_coil(0x0010, true);           // リレーON

// RTUリクエストの処理
std::vector<uint8_t> request = receive_from_uart();
std::vector<uint8_t> response;

if (slave.handle_rtu_request(request, response)) {
    send_to_uart(response);
}
```
