# embed_pid - 組み込み向け高機能・軽量 PID 制御ライブラリ

産業用制御機器、モータサーボ制御、インバータ、ヒータ温度調整、バルブ開度制御、ドローン姿勢制御などに適した、ゼロ動的メモリ確保・高精度の **PID (Proportional-Integral-Derivative) コントローラライブラリ** です。

---

## 🌟 特徴 (Key Features)

- **ゼロ動的メモリ確保 (Zero Allocation)**:
  - `malloc`/`free` を一切使用せず、静的構造体のみで動作。
  - マイコンのリアルタイム割込ハンドラ（ISR）や制御タスク内から極めて高速に計算可能。
- **多彩なアルゴリズム形式**:
  - **位置型 PID (Positional PID)**: 一般的な閉ループフィードバック制御。
  - **速度型 / 増分型 PID (Velocity / Incremental PID)**: ステッピングモータや積分型アクチュエータに最適な増分出力 ($\Delta u$)。
  - **微分先行型 (I-PD / PI-D 制御)**: 目標値急変（ステップ変化）時の微分キック（Derivative Kick）を完全に排除。
- **アンチワインドアップ (Anti-Windup)**:
  - **積分クランプ (Integral Clamping)**: 出力飽和時の積分値蓄積を停止し、オーバーシュートを強力に抑制。
  - **バックカリキュレーション (Back-Calculation / Tracking)**: 飽和差分を積分項にフィードバック追従。
- **微分ローパスフィルタ (Derivative Low-Pass Filter)**:
  - センサノイズによる微分項のスパイク・高周波ノイズを抑える1次ローパスフィルタ ($T_f$)。
- **デッドバンド (不感帯 / Deadband)**:
  - 目標値近傍での微小振動（チャタリング）やアクチュエータの機械的消耗を防止。
- **バプレス転送 (Bumpless Transfer)**:
  - 手動操作 (Manual) から自動制御 (Auto) への切り替え時に出力ショックを起こさないシームレス追従。
- **Ziegler-Nichols 限界感度法 チューニング計算内蔵**:
  - 限界ゲイン $K_u$ と限界周期 $T_u$ から古典PID、PI、オーバーシュート抑制PIDゲインを自動算出。
- **C11 / C++20 デュアルAPI**:
  - 純C言語コア (`embed_pid_t`, `pid_compute`) と、型安全で流暢なモダン C++ ラッパー (`embed_pid::Controller`) を提供。

---

## 📐 制御アルゴリズムと数式

### 1. 位置型 PID (Positional PID)
$$u(t) = K_p e(t) + K_i \int_0^t e(\tau) d\tau + K_d \frac{de(t)}{dt}$$

### 2. 微分先行型 PID (Derivative on PV)
微分キックを防止するため、目標値 $r(t)$ ではなく測定値 $y(t)$ (PV) の変化率を微分項とします：
$$u(t) = K_p (r(t) - y(t)) + K_i \int_0^t e(\tau) d\tau - K_d \frac{dy(t)}{dt}$$

### 3. 微分ローパスフィルタ
$$D_k = \frac{T_f}{T_f + \Delta t} D_{k-1} + \frac{\Delta t}{T_f + \Delta t} D_{\text{raw}}$$

---

## 📁 ディレクトリ構成

```text
libs/embed_pid/
├── include/embed_pid/
│   ├── pid_types.h        # 制御モード、アンチワインドアップ、設定構造体定義
│   ├── pid_controller.h   # C言語コアPIDコントローラAPI
│   └── embed_pid.hpp      # C++ PIDコントローラクラス
├── src/
│   └── pid_controller.c   # PID計算コア実装
└── tests/
    └── test_pid.cpp       # 単体テストスイート
```

---

## 🚀 使い方 (Usage Examples)

### C++ API によるヒータ温度制御

```cpp
#include "embed_pid/embed_pid.hpp"

int main() {
    // 1. PIDコントローラの初期化 (Kp=4.0, Ki=0.15, Kd=8.0, 制御周期 dt=0.1s)
    embed_pid::Controller pid(4.0f, 0.15f, 8.0f, 0.1f);

    // 2. 出力範囲 (PWM 0〜100%) とアンチワインドアップの設定
    pid.set_output_limits(0.0f, 100.0f)
       .set_anti_windup(PID_ANTI_WINDUP_CLAMPING)
       .set_deadband(0.2f); // 0.2℃の不感帯

    float target_temp = 150.0f; // 目標温度 (SP)

    while (true) {
        float current_temp = read_temperature_sensor(); // 現在温度 (PV)

        // 3. 制御出力の計算 (MV)
        float heater_pwm = pid.update(target_temp, current_temp);

        set_heater_pwm(heater_pwm);
        sleep_ms(100);
    }
}
```

### C言語 API によるモータ速度制御 (微分先行型)

```c
#include "embed_pid/pid_controller.h"

embed_pid_t motor_pid;

void setup() {
    pid_config_t cfg;
    pid_get_default_config(&cfg);
    cfg.kp = 0.02f;
    cfg.ki = 0.05f;
    cfg.kd = 0.001f;
    cfg.dt = 0.01f; // 10ms
    cfg.out_min = -12.0f; // -12V〜+12V
    cfg.out_max = 12.0f;
    cfg.mode = PID_MODE_DERIVATIVE_ON_PV; // 微分キック防止
    cfg.tau_d = 0.02f; // 20ms微分ローパスフィルタ

    pid_init(&motor_pid, &cfg);
}

void isr_10ms_timer() {
    float target_rpm = 2000.0f;
    float current_rpm = read_encoder_rpm();

    float voltage = pid_compute(&motor_pid, target_rpm, current_rpm);
    set_motor_voltage(voltage);
}
```

---

## 🛠️ ビルドとテスト

```bash
# ビルド
cmake -B build -G "NMake Makefiles"
cmake --build build

# 単体テスト実行
ctest --test-dir build -R test_pid --output-on-failure

# デモアプリケーション実行
./build/bin/pid_demo.exe
```
