# embed_test

C++17以降のヘッダのみのテスト基盤。フレームワーク内で動的メモリを確保せず、
登録は各ケースの静的な連結リストで保持します。`NDEBUG` でも全検査を実行します。

```cpp
#include <embed_test/embed_test.hpp>
TEST_CASE(addition) {
    CHECK_EQ(2 + 3, 5); // 数値の期待値と実測値を表示
    CHECK_NEAR(0.1 + 0.2, 0.3, 1e-9); // 絶対誤差。NaN/Inf は失敗
    REQUIRE(true); // 失敗時は現在の void テストケースから戻る
    CHECK(true);   // 失敗を記録し、ケースを続行
}
EMBED_TEST_MAIN()
```

`TEST_CASE` は自動登録、`EMBED_TEST_MAIN()` は実行ファイルに一度だけ記述します。
1スレッドで登録順に実行し、TAP 13を標準出力へ出し、失敗があれば非0終了します。
複数翻訳単位間の登録順序には依存しないでください。独自診断行は `# ` で始めます。
`CHECK_EQ` は数値型用です。文字列は `CHECK(strcmp(a,b)==0)` などで検査します。
CIのJUnit XMLは `ctest --output-junit results.xml` から生成します。
`test_embed_test_failure` は意図的な失敗をCTestの `WILL_FAIL` で検証する回帰テストです。
