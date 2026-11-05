# embed_test

C++17以降のヘッダのみのテスト基盤。フレームワーク内で動的メモリを確保せず、
登録は各ケースの静的な連結リストで保持します。`NDEBUG` でも全検査を実行します。

```cpp
#include <embed_test/embed_test.hpp>
TEST_CASE(addition) {
    CHECK_EQ(2 + 3, 5);                  // 期待値と実測値を型に合わせて表示 (整数は10進+16進)
    CHECK_NEAR(0.1 + 0.2, 0.3, 1e-9);    // 絶対誤差。NaN/Inf・負の許容差は失敗
    CHECK_STR_EQ(name, "pump");          // NUL終端文字列。NULL同士は一致
    CHECK_MEM_EQ(frame, expected, 8);    // バイト列。最初の差分位置と前後16バイトを表示
    REQUIRE(ptr != nullptr);             // 失敗時は現在の void テストケースから戻る
    REQUIRE_EQ(len, 8u);                 // CHECK_EQ + 失敗時に戻る
    CHECK(true);                         // 失敗を記録し、ケースを続行
}
EMBED_TEST_MAIN()
```

| マクロ | 用途 |
| --- | --- |
| `CHECK(x)` / `REQUIRE(x)` | 真偽値 |
| `CHECK_EQ(a, e)` / `REQUIRE_EQ(a, e)` | 数値・列挙値・ポインタの `==` |
| `CHECK_NEAR(a, e, tol)` | 浮動小数点の絶対誤差 |
| `CHECK_STR_EQ(a, e)` | C文字列 |
| `CHECK_MEM_EQ(a, e, n)` | バイト列 (プロトコルのフレーム比較など) |

`TEST_CASE` は自動登録、`EMBED_TEST_MAIN()` は実行ファイルに一度だけ記述します。
1スレッドで登録順に実行し、TAP 13を標準出力へ出し、失敗があれば非0終了します。
複数翻訳単位間の登録順序には依存しないでください。独自診断行は `# ` で始めます。

## 実行時オプション

```sh
./test_canopen              # 全ケース
./test_canopen sdo segment  # 名前に "sdo" または "segment" を含むケースのみ
./test_canopen --list       # ケース名の一覧
```

フィルタがどのケースにも一致しない場合は失敗終了します (タイプミスで 0 件成功にしないため)。
CIのJUnit XMLは `ctest --output-junit results.xml` から生成します。

## 自己テスト

- `test_embed_test_failure`: 意図的な失敗をCTestの `WILL_FAIL` で検証 (NDEBUG付き)。
- `test_embed_test_self`: 各検査関数の成否判定・失敗カウント・フィルタを検証。
- `test_embed_test_filter_nomatch`: 一致しないフィルタが失敗終了することを `WILL_FAIL` で検証。
