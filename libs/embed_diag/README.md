# embed_diag - 組み込み向け障害診断・ブラックボックスレコーダー

マイコンのスタックオーバーフロー検知、CPU使用率モニタリング、およびクラッシュ時のレジスタ・直前巡回ログを記録する軽量 **障害診断 & ブラックボックスレコーダー** ライブラリです。

---

## 🌟 特徴 (Key Features)

- **スタックオーバーフロー検知 & 最大使用量測定 (Stack Canary)**:
  - スタック初期化時のカナリア充填（0xA5）と走査により、安全マージンおよび最大消費スタック量（High Water Mark）を計測。
- **CPU 使用率モニタリング (CPU Usage)**:
  - アクティブ時間・アイドル時間から CPU 負荷率を正確に算出。
- **ブラックボックス障害ログレコーダー (Crash Recorder)**:
  - ゼロ動的メモリで動作するリングバッファ構造。
  - クラッシュ直前の最新イベント・警告・エラーログを不揮発保存可能。
- **ハードフォールト・レジスタダンプ (Fault Dump Context)**:
  - ARM Cortex-M / 各種マイコンの PC, LR, SP, CFSR, HFSR 等のフォールトレジスタ保存。

---

## 📁 ディレクトリ構成

```text
libs/embed_diag/
├── include/embed_diag/
│   ├── diag_types.h       # ログエントリ、フォールトコンテキスト、スタック構造体
│   ├── embed_diag.h       # C言語 API
│   └── embed_diag.hpp      # C++ ラッパークラス
├── src/
│   └── embed_diag.c
├── tests/
│   ├── CMakeLists.txt
│   └── test_diag.cpp
└── README.md
```
