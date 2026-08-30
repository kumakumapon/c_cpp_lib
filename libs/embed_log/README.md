# embed_log (組み込み環境向け軽量ロガーライブラリ)

`embed_log` は、リソース制約のある組み込みマイコン（ベアメタル / RTOS）向けに設計された、ゼロ動的確保・マルチバックエンド・ANSIカラー対応の高速ロギングライブラリです。

---

## 📑 目次
1. [特徴とメリット](#-特徴とメリット)
2. [ログレベル](#-ログレベル)
3. [C API & マクロ](#-c-api--マクロ)
4. [C++ ストリームロギング](#-c-ストリームロギング)
5. [マルチ出力バックエンドの登録](#-マルチ出力バックエンドの登録)
6. [バイナリ・メモリダンプ (Hex Dump)](#-バイナリメモリダンプ-hex-dump)
7. [組み込み環境への導入例 (UART / SEGGER RTT)](#-組み込み環境への導入例-uart--segger-rtt)

---

## 🎯 特徴とメリット

- **ゼロ動的メモリ確保**: `malloc` や `free` を一切使用せず、固定長スタックバッファでフォーマットを実施。
- **コンパイル時 & 実行時フィルタリング**:
  - `EMBED_LOG_MAX_COMPILE_LEVEL` により、不要な詳細ログをコンパイル時にバイナリ（ROM/Flash）から完全消去。
  - `embed_log_set_level()` により、実行時に動的なログレベル変更が可能。
- **マルチ出力バックエンド**: UART、SEGGER RTT、ITM、リングバッファ、Flash/SDカード保存など複数の出力先を最大4箇所（設定変更可能）まで同時登録可能。
- **ANSIカラー表示**: ログレベルごとに見やすいカラー表示（ターミナル・シリアルモニタ対応）。
- **Hex Dump機能**: 通信パケットやメモリダンプを16進数+ASCII文字列形式で簡単に出力。
- **スレッド/割込みセーフ**: 排他制御コールバックを登録可能。

---

## 🚦 ログレベル

| レベル | マクロ | ANSIカラー | 用途 |
| :--- | :--- | :--- | :--- |
| `FATAL` | `LOG_F(tag, fmt, ...)` | マゼンタ | システム停止級の致命的エラー |
| `ERROR` | `LOG_E(tag, fmt, ...)` | 赤 | 復旧処理が必要な異常・エラー |
| `WARN` | `LOG_W(tag, fmt, ...)` | 黄 | 警告、想定外だが処理継続可能な事象 |
| `INFO` | `LOG_I(tag, fmt, ...)` | 緑 | システム起動、状態変化等の重要情報 |
| `DEBUG` | `LOG_D(tag, fmt, ...)` | シアン | 開発・デバッグ用詳細情報 (ファイル名・行番号付与) |
| `TRACE` | `LOG_T(tag, fmt, ...)` | グレー | 関数トレース、高頻度ログ (ファイル名・行番号付与) |

---

## 📖 C API & マクロ

### 基本的な出力
```c
#include "embed_log/embed_log.h"

void demo(void) {
    LOG_I("MAIN", "System initialized. Core Clock: %u MHz", 168);
    LOG_W("TEMP", "Temperature high: %d C", 75);
    LOG_E("COMM", "Packet timeout from sensor ID: 0x%02X", 0x1A);
    LOG_D("ADC",  "Raw ADC count: %u", 4095);
}
```

### 初期化と設定
```c
embed_log_init();                          // 初期化
embed_log_set_level(EMBED_LOG_LEVEL_INFO); // 実行時レベル設定
embed_log_set_color_enabled(true);         // カラー有効化
embed_log_set_timestamp_fn(get_tick_ms);   // タイムスタンプ関数登録
```

---

## ⚡ C++ ストリームロギング

```cpp
#include "embed_log/embed_log.hpp"

void cpp_demo() {
    CPP_LOG_I("MOTOR") << "Speed: " << 3000 << " rpm, Voltage: " << 12.4 << " V";
    CPP_LOG_E("NET")   << "Failed to connect to " << "192.168.1.1" << ":" << 8080;
}
```

---

## 🔌 マルチ出力バックエンドの登録

UART送信とFlash保存バックエンドを同時に動かす例：

```c
// UART出力バックエンド
static void uart_backend(const char *str, size_t len, void *user_data) {
    HAL_UART_Transmit(&huart1, (uint8_t*)str, len, 100);
}

// SEGGER RTT出力バックエンド
static void rtt_backend(const char *str, size_t len, void *user_data) {
    SEGGER_RTT_WriteString(0, str);
}

void setup_logging(void) {
    embed_log_init();
    embed_log_clear_backends(); // デフォルトstdoutを解除
    embed_log_add_backend(uart_backend, NULL);
    embed_log_add_backend(rtt_backend, NULL);
}
```

---

## 🔍 バイナリ・メモリダンプ (Hex Dump)

```c
uint8_t rx_packet[16] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
LOG_HEXDUMP("RX_PKT", rx_packet, sizeof(rx_packet));
```

出力結果：
```text
[00001234][DEBUG][RX_PKT] --- HexDump (16 bytes) --- (main.c:45)
  0000: AA 01 02 03 04 05 06 07 00 00 00 00 00 00 00 00  |........|
```
