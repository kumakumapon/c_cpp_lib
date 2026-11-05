#pragma once
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
namespace embed_test {
struct Case {
    const char *name;
    void (*run)();
    Case *next;
};
inline Case *first = nullptr, *last = nullptr;
inline unsigned failures = 0;
struct Register {
    Register(Case *c) {
        if (last)
            last->next = c;
        else
            first = c;
        last = c;
    }
};
inline bool check(bool ok, const char *expression, const char *file, int line) {
    if (!ok) {
        ++failures;
        std::printf("# %s:%d: %s\n", file, line, expression);
    }
    return ok;
}
/* 値を型に合った形式で表示する (整数は丸めずに 10 進 + 16 進で出す)。 */
template <class T> void print_value(const char *label, T v) {
    if constexpr (std::is_same_v<T, bool>) {
        std::printf("# %s %s\n", label, v ? "true" : "false");
    } else if constexpr (std::is_enum_v<T>) {
        print_value(label, static_cast<std::underlying_type_t<T>>(v));
    } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
        std::printf("# %s %lld\n", label, static_cast<long long>(v));
    } else if constexpr (std::is_integral_v<T>) {
        std::printf("# %s %llu (0x%llx)\n", label, static_cast<unsigned long long>(v),
                    static_cast<unsigned long long>(v));
    } else if constexpr (std::is_floating_point_v<T>) {
        std::printf("# %s %.17Lg\n", label, static_cast<long double>(v));
    } else if constexpr (std::is_pointer_v<T> && std::is_object_v<std::remove_pointer_t<T>>) {
        std::printf("# %s %p\n", label, static_cast<const volatile void *>(v));
    } else {
        std::printf("# %s <unprintable>\n", label);
    }
}
template <class A, class B> bool equal(A actual, B expected, const char *file, int line) {
    bool ok = actual == expected;
    if (!check(ok, "equality", file, line)) {
        print_value("expected", expected);
        print_value("actual  ", actual);
    }
    return ok;
}
inline bool near(double actual, double expected, double tolerance, const char *file, int line) {
    bool ok = tolerance >= 0 && std::isfinite(actual) && std::isfinite(expected) &&
              std::fabs(actual - expected) <= tolerance;
    if (!check(ok, "absolute tolerance", file, line))
        std::printf("# expected %.17g +/- %.17g, actual %.17g\n", expected, tolerance, actual);
    return ok;
}
/* NUL 終端文字列の比較。NULL 同士は等しく、片方だけ NULL は不一致。 */
inline bool str_equal(const char *actual, const char *expected, const char *file, int line) {
    bool ok = (actual == nullptr || expected == nullptr) ? actual == expected
                                                         : std::strcmp(actual, expected) == 0;
    if (!check(ok, "string equality", file, line))
        std::printf("# expected \"%s\"\n# actual   \"%s\"\n", expected ? expected : "(null)",
                    actual ? actual : "(null)");
    return ok;
}
/* バイト列の比較。最初に異なるオフセットと、その前後 16 バイトを表示する。 */
inline bool mem_equal(const void *actual, const void *expected, std::size_t len, const char *file,
                      int line) {
    const auto *a = static_cast<const unsigned char *>(actual);
    const auto *e = static_cast<const unsigned char *>(expected);
    std::size_t diff = len;
    if (len != 0 && (a == nullptr || e == nullptr)) {
        diff = 0;
    } else {
        for (std::size_t i = 0; i < len; ++i)
            if (a[i] != e[i]) {
                diff = i;
                break;
            }
    }
    bool ok = diff == len;
    if (!check(ok, "memory equality", file, line)) {
        if (a == nullptr || e == nullptr) {
            std::printf("# null buffer with length %zu\n", len);
            return false;
        }
        std::size_t from = diff >= 8 ? diff - 8 : 0;
        std::size_t to = from + 16 < len ? from + 16 : len;
        std::printf("# first difference at offset %zu (length %zu)\n# expected", diff, len);
        for (std::size_t i = from; i < to; ++i)
            std::printf(i == diff ? " [%02x]" : " %02x", e[i]);
        std::printf("\n# actual  ");
        for (std::size_t i = from; i < to; ++i)
            std::printf(i == diff ? " [%02x]" : " %02x", a[i]);
        std::printf("\n");
    }
    return ok;
}
inline bool selected(const char *name, int argc, char **argv) {
    bool any = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-')
            continue;
        any = true;
        if (std::strstr(name, argv[i]))
            return true;
    }
    return !any;
}
/* 引数なし: 全ケース。引数あり: 名前に引数のいずれかを含むケースのみ。--list で一覧表示。 */
inline int run(int argc = 0, char **argv = nullptr) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--list") == 0) {
            for (auto *c = first; c; c = c->next)
                std::printf("%s\n", c->name);
            return EXIT_SUCCESS;
        }
    unsigned count = 0;
    for (auto *c = first; c; c = c->next)
        count += selected(c->name, argc, argv) ? 1u : 0u;
    std::printf("TAP version 13\n1..%u\n", count);
    unsigned i = 0;
    for (auto *c = first; c; c = c->next) {
        if (!selected(c->name, argc, argv))
            continue;
        unsigned before = failures;
        c->run();
        std::printf("%s %u - %s\n", before == failures ? "ok" : "not ok", ++i, c->name);
    }
    if (count == 0 && argc > 1)
        std::printf("# no test case matched the filter\n");
    return failures || (count == 0 && argc > 1) ? EXIT_FAILURE : EXIT_SUCCESS;
}
} // namespace embed_test
#define CHECK(x) (::embed_test::check(!!(x), #x, __FILE__, __LINE__))
#define CHECK_EQ(actual, expected) (::embed_test::equal((actual), (expected), __FILE__, __LINE__))
#define CHECK_NEAR(actual, expected, tolerance)                                                    \
    (::embed_test::near((actual), (expected), (tolerance), __FILE__, __LINE__))
#define CHECK_STR_EQ(actual, expected)                                                             \
    (::embed_test::str_equal((actual), (expected), __FILE__, __LINE__))
#define CHECK_MEM_EQ(actual, expected, len)                                                        \
    (::embed_test::mem_equal((actual), (expected), (len), __FILE__, __LINE__))
#define REQUIRE(x)                                                                                 \
    do {                                                                                           \
        if (!CHECK(x))                                                                             \
            return;                                                                                \
    } while (0)
#define REQUIRE_EQ(actual, expected)                                                               \
    do {                                                                                           \
        if (!CHECK_EQ(actual, expected))                                                           \
            return;                                                                                \
    } while (0)
#define TEST_CASE(name)                                                                            \
    static void name();                                                                            \
    static ::embed_test::Case name##_case{#name, name, nullptr};                                   \
    static ::embed_test::Register name##_registration{&name##_case};                               \
    static void name()
#define EMBED_TEST_MAIN()                                                                          \
    int main(int argc, char **argv) { return ::embed_test::run(argc, argv); }
