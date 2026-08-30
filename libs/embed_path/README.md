# embed_path - 自律ロボット向け経路計画 & 追従コントローラ

2Dグリッドマップ上の **A\* (A-Star) 最適経路探索** と、移動ロボットの **Pure Pursuit (純追従) 軌道制御** を提供する軽量ナビゲーションライブラリです。

---

## 🌟 特徴 (Key Features)

- **A\* グリッド経路探索 (A-Star Path Planning)**:
  - ゼロ動的メモリ確保（静的ノードテーブル）による高速な最適経路探索。
  - 8方向移動対応（斜め移動コスト計算）。
- **Pure Pursuit 経路追従コントローラ**:
  - 前方注視距離（Look-ahead Distance）に基づく曲率 $\kappa$ および旋回角速度 $\omega$ のリアルタイム算出。
  - ゴール到達自動検知 & 停止制御。
- **C11 / C++20 デュアル API**:
  - `embed_path::GridPlanner` および `embed_path::PurePursuit`。

---

## 📁 ディレクトリ構成

```text
libs/embed_path/
├── include/embed_path/
│   ├── path_types.h       # ウェイポイント、グリッドマップ、制御コマンド定義
│   ├── embed_path.h       # C言語 API
│   └── embed_path.hpp     # C++ クラスラッパー
├── src/
│   └── embed_path.c
├── tests/
│   ├── CMakeLists.txt
│   └── test_path.cpp
└── README.md
```
