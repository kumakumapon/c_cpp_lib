# embed_fixed

Q1.15 / Q1.31（符号付き、範囲[-1,1)）の飽和add/sub/mul/divとsin/cos/atan2/sqrt。
実行時の浮動小数点演算・動的確保を使いません。C11 APIとC++数値型アダプターを提供します。

- 乗算・除算は0方向へ丸め、オーバーフローは上下限へ飽和します。
- 0除算は分子の符号に応じて飽和、0/0は0です。
- 角度はπで正規化します。Q31の`1073741824`はπ/2、`INT32_MIN`は-π。
  atan2(y,x)の+πも同じ位相の-πを返し、atan2(0,0)=0です。
- sin/cosは256区間の四分波、atan2は256区間の比率テーブルを線形補間します。
  Q31の絶対誤差目安はsin/cos 6e-6、atan2はπ正規化値で6e-6以下。
  Q15には変換による1LSB程度の追加誤差があります。
- sqrtは整数平方根で下方向に丸めます。負数は0。

```cpp
#include <embed_fixed/embed_fixed.hpp>
using embed_fixed::Q15;
embed_fixed::LowPass<Q15> filter;
filter.alpha = Q15::from_raw(16384); // 0.5
Q15 value = filter.update(Q15::from_raw(16384)); // 0.25
```

`Q15` / `Q31`は四則演算・比較を提供します。`LowPass<Number>` と `PID<Number>` は
float/Q15/Q31を型として選べる制御アダプターです。PIDではkp/ki/kd、low/highを設定し、
dt>0を渡します。入力・係数・内部状態を[-1,1)にスケーリングしてください。
飽和による中間結果の精度低下を考慮する必要があります。
既存 `embed_pid` 等のC ABIとfloat実装は維持し、型選択はこの汎用層で行います。
