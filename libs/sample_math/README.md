# sample_math

新しいライブラリを追加する際のひな形となる、最小構成のC++サンプルライブラリです。
ディレクトリ構成（`include/` `src/` `tests/`）と `CMakeLists.txt` の書き方の参考にしてください。
手順は [ルートREADME](../../README.md) の「新しいライブラリの追加方法」を参照してください。

| 関数 | 説明 |
| --- | --- |
| `int sample_math::add(int a, int b)` | 2つの整数の加算 |
| `int sample_math::multiply(int a, int b)` | 2つの整数の乗算 |

```cpp
#include <sample_math/math_utils.hpp>

int sum = sample_math::add(2, 3);       // 5
int product = sample_math::multiply(3, 4); // 12
```

どちらの関数も `int` の通常の演算をそのまま行います。結果が `int` に収まらない場合は
符号付き整数オーバーフロー（未定義動作）になるため、範囲は呼び出し側で保証してください。
