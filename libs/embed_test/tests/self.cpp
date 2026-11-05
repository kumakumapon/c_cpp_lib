// embed_test 自身の検査関数が正しく成否を判定することを確かめる。
// 失敗させた検査は failures を戻して、このプログラム自体は成功させる。
#include "embed_test/embed_test.hpp"
#include <cstdint>

namespace {
/* 失敗を 1 件だけ記録したことを確認し、カウンタを戻す */
bool consumed_one_failure(unsigned before) {
    bool ok = embed_test::failures == before + 1;
    embed_test::failures = before;
    return ok;
}
enum class Color : uint8_t { red = 1, blue = 2 };
} // namespace

TEST_CASE(equal_reports_result_and_counts_failures) {
    unsigned before = embed_test::failures;
    CHECK(embed_test::equal(3, 3, __FILE__, __LINE__));
    CHECK_EQ(embed_test::failures, before);
    bool r = embed_test::equal(UINT64_MAX, UINT64_MAX - 1, __FILE__, __LINE__);
    CHECK(consumed_one_failure(before));
    CHECK(!r);
    r = embed_test::equal(Color::red, Color::blue, __FILE__, __LINE__);
    CHECK(consumed_one_failure(before));
    CHECK(!r);
    CHECK_EQ(Color::blue, Color::blue);
}

TEST_CASE(near_rejects_non_finite_and_negative_tolerance) {
    unsigned before = embed_test::failures;
    CHECK(embed_test::near(1.0, 1.0 + 1e-12, 1e-9, __FILE__, __LINE__));
    CHECK(!embed_test::near(1.0, 1.1, 0.05, __FILE__, __LINE__));
    CHECK(consumed_one_failure(before));
    CHECK(!embed_test::near(std::nan(""), 0.0, 1.0, __FILE__, __LINE__));
    CHECK(consumed_one_failure(before));
    CHECK(!embed_test::near(1.0, 1.0, -1.0, __FILE__, __LINE__));
    CHECK(consumed_one_failure(before));
    // 許容差ちょうどは合格 (<=)
    CHECK(embed_test::near(1.0, 1.5, 0.5, __FILE__, __LINE__));
}

TEST_CASE(string_equality_handles_null) {
    unsigned before = embed_test::failures;
    CHECK_STR_EQ("abc", "abc");
    CHECK(embed_test::str_equal(nullptr, nullptr, __FILE__, __LINE__));
    CHECK(!embed_test::str_equal("abc", "abd", __FILE__, __LINE__));
    CHECK(consumed_one_failure(before));
    CHECK(!embed_test::str_equal(nullptr, "", __FILE__, __LINE__));
    CHECK(consumed_one_failure(before));
    CHECK(!embed_test::str_equal("ab", "abc", __FILE__, __LINE__));
    CHECK(consumed_one_failure(before));
}

TEST_CASE(memory_equality_finds_first_difference) {
    unsigned before = embed_test::failures;
    const uint8_t a[20] = {1, 2, 3}, b[20] = {1, 2, 3};
    uint8_t c[20] = {1, 2, 3};
    CHECK_MEM_EQ(a, b, sizeof(a));
    c[19] = 9; // 最後のバイトだけ違う
    CHECK(!embed_test::mem_equal(c, a, sizeof(a), __FILE__, __LINE__));
    CHECK(consumed_one_failure(before));
    CHECK(embed_test::mem_equal(c, a, 19, __FILE__, __LINE__)); // 範囲外の差は見ない
    CHECK(embed_test::mem_equal(nullptr, nullptr, 0, __FILE__, __LINE__));
    CHECK(!embed_test::mem_equal(nullptr, a, 1, __FILE__, __LINE__));
    CHECK(consumed_one_failure(before));
}

TEST_CASE(filter_selects_by_substring) {
    char prog[] = "prog", f1[] = "memory", f2[] = "--verbose";
    char *none[] = {prog};
    char *one[] = {prog, f1};
    char *opt_only[] = {prog, f2};
    CHECK(embed_test::selected("anything", 1, none));
    CHECK(embed_test::selected("memory_equality", 2, one));
    CHECK(!embed_test::selected("string_equality", 2, one));
    CHECK(embed_test::selected("string_equality", 2, opt_only)); // オプションはフィルタではない
}

EMBED_TEST_MAIN()
