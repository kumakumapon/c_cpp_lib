# embed_kinematics - ロボット運動学 & オドメトリ計算ライブラリ

差動2輪 (Differential Drive)、4輪メカナム (Mecanum)、2自由度ロボットアーム向けの **順運動学 (FK) / 逆運動学 (IK) / オドメトリ計算** ライブラリです。

---

## 🌟 特徴 (Key Features)

- **差動2輪 (Differential Drive)**:
  - 左右車輪回転速度 ⇔ 車体並進・旋回速度 $(v, \omega)$ の順/逆運動学変換。
  - エンコーダ移動量による高精度2Dオドメトリ姿勢推定。
- **4輪メカナムホイール (4-Wheel Mecanum Drive)**:
  - 前後・左右・超信地旋回 $(v_x, v_y, \omega_z)$ の全方向移動計算。
- **2自由度平面ロボットアーム (2-DOF Planar Arm)**:
  - 順運動学（関節角 $\theta_1, \theta_2 \to (x, y)$）および解析的逆運動学（目標位置 $(x, y) \to \theta_1, \theta_2$、Elbow-up / Elbow-down 対応）。
- **ゼロ動的メモリ確保 (Zero Allocation)**:
  - マイコンのリアルタイム周期制御ループで安全に使用可能。

---

## 📁 ディレクトリ構成

```text
libs/embed_kinematics/
├── include/embed_kinematics/
│   ├── kinematics_types.h  # Pose2D, Twist2D, 各種パラメータ定義
│   ├── kinematics.h        # C言語 API
│   └── embed_kinematics.hpp # C++ クラスラッパー
├── src/
│   └── kinematics.c
├── tests/
│   ├── CMakeLists.txt
│   └── test_kinematics.cpp
└── README.md
```
