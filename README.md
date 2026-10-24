# c_cpp_lib

[![CI](https://github.com/kumakumapon/c_cpp_lib/actions/workflows/ci.yml/badge.svg)](https://github.com/kumakumapon/c_cpp_lib/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/github/license/kumakumapon/c_cpp_lib)](LICENSE)
![C11](https://img.shields.io/badge/C-11-blue)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![CMake](https://img.shields.io/badge/build-CMake-064F8C)

C/C++言語用のライブラリおよびアプリケーションを統合・管理するためのマルチモジュール・リポジトリです。

---

## 📁 ディレクトリ構造

```text
c_cpp_lib/
├── CMakeLists.txt              # ルートCMake設定（全体設定・サブディレクトリ取り込み）
├── cmake/                      # 共通CMakeスクリプト・モジュール
│   └── CompilerWarnings.cmake  # 警告レベル等の共通コンパイル設定
├── libs/                       # 独立したライブラリ群
│   ├── CMakeLists.txt          # 各ライブラリの自動読み込み設定
│   ├── embed_bridge/           # 産業用マルチプロトコル・データルーター / ゲートウェイ ([仕様・使い方ドキュメント](libs/embed_bridge/README.md))
│   ├── embed_canopen/          # 組み込み向けCANopen (CiA 301/402) プロトコルスタック ([仕様・使い方ドキュメント](libs/embed_canopen/README.md))
│   ├── embed_diag/             # 組み込み向け障害診断・ブラックボックスレコーダー ([仕様・使い方ドキュメント](libs/embed_diag/README.md))
│   ├── embed_eip/              # 産業用EtherNet/IP (CIP) プロトコルスタック ([仕様・使い方ドキュメント](libs/embed_eip/README.md))
│   ├── embed_filter/           # 組み込み向けセンサDSP & 姿勢推定フィルタ集 ([仕様・使い方ドキュメント](libs/embed_filter/README.md))
│   ├── embed_fsm/              # 組み込み向け階層型状態遷移マシン (HSM) ([仕様・使い方ドキュメント](libs/embed_fsm/README.md))
│   ├── embed_heap/             # 組み込み向け静的ヒープメモリアロケータ ([仕様・使い方ドキュメント](libs/embed_heap/README.md))
│   ├── embed_kinematics/       # ロボット運動学 & オドメトリ計算ライブラリ ([仕様・使い方ドキュメント](libs/embed_kinematics/README.md))
│   ├── embed_log/              # 組み込み向け軽量ロガーライブラリ ([仕様・使い方ドキュメント](libs/embed_log/README.md))
│   ├── embed_modbus/           # 組み込み向けModbus RTU/ASCIIプロトコルスタック ([仕様・使い方ドキュメント](libs/embed_modbus/README.md))
│   ├── embed_motion/           # モータ加減速・軌道生成プロファイル ([仕様・使い方ドキュメント](libs/embed_motion/README.md))
│   ├── embed_mqtt/             # 組み込み向けMQTT v3.1.1プロトコルスタック ([仕様・使い方ドキュメント](libs/embed_mqtt/README.md))
│   ├── embed_opcua/            # 産業用OPC-UAバイナリプロトコルスタック ([仕様・使い方ドキュメント](libs/embed_opcua/README.md))
│   ├── embed_path/             # 自律ロボット向け経路計画 & 追従コントローラ ([仕様・使い方ドキュメント](libs/embed_path/README.md))
│   ├── embed_pid/              # 組み込み向け高機能PID制御ライブラリ ([仕様・使い方ドキュメント](libs/embed_pid/README.md))
│   ├── uitron/                 # μITRON 4.0 仕様準拠 RTOS API ライブラリ ([仕様・使い方ドキュメント](libs/uitron/README.md))
│   ├── uart_protocol/          # 組み込み向けUART通信プロトコルスタック ([仕様・使い方ドキュメント](libs/uart_protocol/README.md))
│   └── sample_math/            # サンプルライブラリ（数学ユーティリティ）
├── apps/                       # 独立したアプリケーション群
│   ├── CMakeLists.txt          # 各アプリの自動読み込み設定
│   ├── bridge_demo/            # 産業用マルチプロトコルルーターのデモアプリ
│   ├── canopen_demo/           # CANopen スレーブノード & 通信デモアプリ
│   ├── cli_demo/               # サンプルCLIアプリケーション
│   ├── eip_demo/               # EtherNet/IP アダプタノードのデモアプリ
│   ├── embed_heap_demo/        # embed_heapのデモアプリ
│   ├── embed_log_demo/         # embed_logのデモアプリ
│   ├── modbus_demo/            # Modbus RTU Master/Slaveのデモアプリ
│   ├── motion_demo/            # モータ加減速プロファイル & 多軸補間デモアプリ
│   ├── mqtt_demo/              # MQTT v3.1.1 クライアントのデモアプリ
│   ├── opcua_demo/             # OPC-UA Serverのデモアプリ
│   ├── pid_demo/               # PID制御シミュレーション（温度・モータ速度）デモアプリ
│   ├── robotics_demo/          # 自律移動ロボット (A*/PurePursuit/FSM/Diag) 統合デモ
│   ├── uitron_demo/            # μITRON 4.0 APIのデモアプリ
│   └── uart_protocol_demo/     # UARTプロトコルスタックのデモアプリ
└── README.md
```

---

## 🚀 ビルド方法

### 必要環境
- CMake (3.20 以上)
- C/C++ コンパイラ (MSVC / GCC / Clang 等)
  - C標準: C11
  - C++標準: C++20

### 基本ビルド手順

```bash
# 1. ビルド設定 (Visual Studio の場合)
cmake -B build -S .

# 2. ビルド実行
cmake --build build --config Release

# 3. テストの実行
ctest --test-dir build -C Release --output-on-failure
```

ビルド成果物は以下に出力されます：
- 実行ファイル (.exe): `build/bin/` (または `build/bin/Release/`)
- ライブラリ (.lib/.a/.dll/.so): `build/lib/` (または `build/lib/Release/`)

---

## ➕ 新しいライブラリの追加方法

`libs/` 配下に新しいディレクトリを作成するだけで、CMakeが自動的に検出します。

1. `libs/<ライブラリ名>/` を作成
2. 以下の構成でファイルを配置：
   ```text
   libs/my_lib/
   ├── CMakeLists.txt
   ├── include/
   │   └── my_lib/
   │       └── my_lib.hpp
   ├── src/
   │       └── my_lib.cpp
   └── tests/                # 任意 (単体テスト)
       ├── CMakeLists.txt
       └── test_my_lib.cpp
   ```
3. `libs/my_lib/CMakeLists.txt` の例：
   ```cmake
   add_library(my_lib
       src/my_lib.cpp
   )

   target_include_directories(my_lib
       PUBLIC
           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<INSTALL_INTERFACE:include>
   )

   target_set_warnings(my_lib)

   if(BUILD_TESTING)
       add_subdirectory(tests)
   endif()
   ```

---

## ➕ 新しいアプリケーションの追加方法

`apps/` 配下に新しいディレクトリを作成するだけで、CMakeが自動的に検出します。

1. `apps/<アプリ名>/` を作成
2. 以下の構成でファイルを配置：
   ```text
   apps/my_app/
   ├── CMakeLists.txt
   └── src/
       └── main.cpp
   ```
3. `apps/my_app/CMakeLists.txt` の例（作成した `libs` をリンクする場合）：
   ```cmake
   add_executable(my_app
       src/main.cpp
   )

   # 作成したライブラリをリンク
   target_link_libraries(my_app
       PRIVATE
           sample_math
           my_lib
   )

   target_set_warnings(my_app)
   ```

---

## ⚙️ 主なビルドオプション

| オプション | デフォルト値 | 説明 |
| :--- | :--- | :--- |
| `BUILD_TESTING` | `ON` | 単体テストのビルドを有効化 |
| `BUILD_SHARED_LIBS` | `OFF` | 静的ライブラリではなく共有ライブラリ (.dll/.so) としてビルド |

## 品質基盤と追加ユーティリティ（#15）

全テストはReleaseでも検査を実行します。CIはLinux/Windows、Debug/Release、CRC方式、
共有ライブラリ、ASan/UBSan、インストール後の利用を検証します。

| ライブラリ | 用途 |
| --- | --- |
| [embed_test](libs/embed_test/README.md) | 自動登録・TAP出力・常時有効な検査 |
| [embed_crc](libs/embed_crc/README.md) | 6種類のCRC、Fletcher/Adler、ストリーミング |
| [embed_ringbuf](libs/embed_ringbuf/README.md) | UART非依存のバイトリング |
| [embed_modbus_tcp](libs/embed_modbus_tcp/README.md) | MBAP、接続別フレーミング、RTU-over-TCP |
| [embed_sched](libs/embed_sched/README.md) | タイマホイール、協調実行、期限監視 |
| [embed_selftest](libs/embed_selftest/README.md) | RAM/ROM/スタック/時計/ウォッチドッグ診断 |
| [embed_fixed](libs/embed_fixed/README.md) | Q15/Q31演算、数学関数、制御アダプター |

### 外部プロジェクトへのインストール

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix /path/to/prefix
```

利用側のCMake:

```cmake
find_package(c_cpp_lib CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE c_cpp_lib::embed_pid c_cpp_lib::embed_modbus_tcp)
```

利用側を `-DCMAKE_PREFIX_PATH=/path/to/prefix` で構成します。
`add_subdirectory` 利用でも同じ名前空間付きターゲットを使えます。
`BUILD_APPS=OFF` でデモを省略、`BUILD_TESTING=OFF` でテストを省略できます。
`EMBED_CRC_TABLES=ON` でCRCのROM/速度トレードオフを選びます。
共有ライブラリの場合はインストール先のbin/libを実行時ローダーの探索先へ追加します。

### OPC UA APIの移行

バイナリのencode全関数に末尾の出力容量、decodeの基本・複合型関数には
送信元ポインタの直後の入力長を追加しました。成功時は処理バイト数、不正値・残量不足では0です。
既存のTCP decode関数は従来どおり長さを受け取ります。

```c
size_t n = opcua_encode_variant(&value, buffer, sizeof(buffer));
if (n != 0) {
    size_t consumed = opcua_decode_variant(buffer, n, &decoded);
}
```

失敗時は出力内容が途中まで変更される場合があるため、戻り値0の出力は破棄してください。
文字列が格納先に収まらない場合も失敗とし、黙って切り詰めません。
配列Variant、拡張NodeId、未対応DataValueフィールドは拒否します。
この変更はメモリ安全性の修正であり、既存の簡略化されたOPC UAサービスを完全な仕様実装へ
拡張するものではありません。既存の呼び出し元は再コンパイルが必要です。
