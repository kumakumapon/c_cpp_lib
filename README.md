# c_cpp_lib

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
│   ├── embed_eip/              # 産業用EtherNet/IP (CIP) プロトコルスタック ([仕様・使い方ドキュメント](libs/embed_eip/README.md))
│   ├── embed_heap/             # 組み込み向け静的ヒープメモリアロケータ ([仕様・使い方ドキュメント](libs/embed_heap/README.md))
│   ├── embed_log/              # 組み込み向け軽量ロガーライブラリ ([仕様・使い方ドキュメント](libs/embed_log/README.md))
│   ├── embed_modbus/           # 組み込み向けModbus RTU/ASCIIプロトコルスタック ([仕様・使い方ドキュメント](libs/embed_modbus/README.md))
│   ├── embed_mqtt/             # 組み込み向けMQTT v3.1.1プロトコルスタック ([仕様・使い方ドキュメント](libs/embed_mqtt/README.md))
│   ├── embed_opcua/            # 産業用OPC-UAバイナリプロトコルスタック ([仕様・使い方ドキュメント](libs/embed_opcua/README.md))
│   ├── embed_pid/              # 組み込み向け高機能PID制御ライブラリ ([仕様・使い方ドキュメント](libs/embed_pid/README.md))
│   ├── uitron/                 # μITRON 4.0 仕様準拠 RTOS API ライブラリ ([仕様・使い方ドキュメント](libs/uitron/README.md))
│   ├── uart_protocol/          # 組み込み向けUART通信プロトコルスタック ([仕様・使い方ドキュメント](libs/uart_protocol/README.md))
│   └── sample_math/            # サンプルライブラリ（数学ユーティリティ）
├── apps/                       # 独立したアプリケーション群
│   ├── CMakeLists.txt          # 各アプリの自動読み込み設定
│   ├── eip_demo/               # EtherNet/IP アダプタノードのデモアプリ
│   ├── embed_heap_demo/        # embed_heapのデモアプリ
│   ├── embed_log_demo/         # embed_logのデモアプリ
│   ├── modbus_demo/            # Modbus RTU Master/Slaveのデモアプリ
│   ├── mqtt_demo/              # MQTT v3.1.1 クライアントのデモアプリ
│   ├── opcua_demo/             # OPC-UA Serverのデモアプリ
│   ├── pid_demo/               # PID制御シミュレーション（温度・モータ速度）デモアプリ
│   ├── uitron_demo/            # μITRON 4.0 APIのデモアプリ
│   ├── uart_protocol_demo/     # UARTプロトコルスタックのデモアプリ
│   └── cli_demo/               # サンプルCLIアプリケーション
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