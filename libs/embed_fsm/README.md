# embed_fsm - 組み込み向け階層型状態遷移マシン (HSM)

UML ステートチャート仕様に準拠した、イベント駆動型の **階層型状態遷移マシン (Hierarchical State Machine - HSM)** フレームワークです。

---

## 🌟 特徴 (Key Features)

- **階層状態 (Composite / Substates)**:
  - 親子関係を持つ状態定義により、共通処理やエラーハンドリングの上位集約が可能。
- **イベントバブルアップ (Hierarchical Event Bubbling)**:
  - 子状態で未処理（`FSM_RET_UNHANDLED`）のイベントは、自動的に親（祖先）状態へ委譲。
- **正確な Entry / Exit アクション遷移**:
  - 共通の先祖（LCA: Lowest Common Ancestor）を自動判定し、必要な Exit / Entry のみを最小限に実行。
- **ゼロ動的メモリ確保 (Zero Allocation)**:
  - 静的固定長テーブル駆動により、決定論的かつ安全にマイコンで動作。

---

## 📁 ディレクトリ構成

```text
libs/embed_fsm/
├── include/embed_fsm/
│   ├── fsm_types.h        # 状態・イベント構造体定義
│   ├── embed_fsm.h        # C言語 API
│   └── embed_fsm.hpp      # C++ クラスラッパー
├── src/
│   └── embed_fsm.c
├── tests/
│   ├── CMakeLists.txt
│   └── test_fsm.cpp
└── README.md
```
