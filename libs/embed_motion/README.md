# embed_motion - 組み込み向けモータ加減速・軌道生成ライブラリ

産業用ロボット、CNC工作機械、3Dプリンタ、ステッピングモータ、サーボモータ向けに、滑らかで機械的ショックの少ない軌道を実時間生成する **モーションプロファイル（加減速・補間）ライブラリ** です。

---

## 🌟 特徴 (Key Features)

- **ゼロ動的確保 (Zero Allocation)**:
  - `malloc`/`free` 不使用。固定メモリのみで動作し、高速タイマー割込（ISR）内でのパルス生成に直結可能。
- **多彩な加減速プロファイル**:
  - **台形加減速 (Trapezoidal)**: 高速・最短時間移動を実現。
  - **S字加減速 (S-Curve / ジャーク制限)**: 5次多項式 (Minimum Jerk) による躍度連続軌道で、振動や機械共振を抑制。
- **多軸直線補間 (Multi-Axis Linear Interpolation)**:
  - 最大4軸（XY/XYZ/XYZW）の合成ベクトル速度・加速度拘束に基づく同期直線移動。
- **C11 / C++20 デュアルAPI**:
  - 純C言語コア実装 (`motion_profile_t`, `motion_profile_step`) と、型安全なモダン C++ ラッパー (`embed_motion::SingleAxis`, `embed_motion::MultiAxis<N>`) を提供。

---

## 📁 ディレクトリ構成

```text
libs/embed_motion/
├── include/embed_motion/
│   ├── motion_types.h     # 拘束条件、状態量、プロファイル構造体
│   ├── motion_profile.h   # C言語コアプロファイル生成・補間API
│   └── embed_motion.hpp   # C++ プロファイルラッパークラス
├── src/
│   └── motion_profile.c   # 軌道計算実装
└── tests/
    └── test_motion.cpp    # 単体テストスイート
```

---

## 🚀 使い方 (Usage Examples)

### C++ API による S字加減速軌道生成

```cpp
#include "embed_motion/embed_motion.hpp"

int main() {
    // 1. 軸の初期化 (Vmax=100mm/s, Amax=200mm/s^2, Dmax=200mm/s^2, Jmax=1000mm/s^3)
    embed_motion::SingleAxis axis(100.0f, 200.0f, 200.0f, 1000.0f);

    // 2. 移動計画 (0 mm -> 150 mm, S字加減速)
    axis.plan(0.0f, 150.0f, MOTION_PROFILE_SCURVE);

    float dt = 0.01f; // 10ms 制御周期
    while (!axis.is_finished()) {
        const motion_state_t &st = axis.step(dt);
        
        // モータ指令位置・速度を更新
        set_motor_target(st.position, st.velocity);
        delay_ms(10);
    }
}
```

---

## 🛠️ ビルドとテスト

```bash
cmake -B build -G "NMake Makefiles"
cmake --build build
ctest --test-dir build -R test_motion --output-on-failure
./build/bin/motion_demo.exe
```
