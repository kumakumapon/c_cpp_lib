# embed_filter - 組み込み向けセンサ信号処理 & 姿勢推定フィルタ集

1次元カルマンフィルタ、6軸 IMU 姿勢推定（Madgwick アルゴリズム）、スパイクノイズ除去用メディアンフィルタ、1次 IIR 低域通過フィルタ（LPF）を提供する軽量 DSP フィルタライブラリです。

---

## 🌟 特徴 (Key Features)

- **1次元カルマンフィルタ (1D Kalman Filter)**:
  - プロセスノイズ $Q$ と観測ノイズ $R$ による最適状態推定。
- **IMU 姿勢推定 (Madgwick / 相補フィルタ)**:
  - 3軸加速度 + 3軸ジャイロからクォータニオンおよびオイラー角（Roll, Pitch, Yaw）をリアルタイム計算。
- **メディアンフィルタ (Median Filter)**:
  - スパイクノイズや通信エラーによる突発異常値を完全に除去。
- **1次 IIR 低域通過フィルタ (First-order LPF)**:
  - カットオフ周波数指定による高周波センサノイズの減衰。
- **ゼロ動的メモリ確保 (Zero Allocation)**:
  - 高速サンプリング周期（100Hz〜10kHz）で決定論的に動作。

---

## 📁 ディレクトリ構成

```text
libs/embed_filter/
├── include/embed_filter/
│   ├── filter_types.h     # クォータニオン、オイラー角、フィルタ構造体定義
│   ├── embed_filter.h     # C言語 API
│   └── embed_filter.hpp   # C++ クラスラッパー
├── src/
│   └── embed_filter.c
├── tests/
│   ├── CMakeLists.txt
│   └── test_filter.cpp
└── README.md
```
