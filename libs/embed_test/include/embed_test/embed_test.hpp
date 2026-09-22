#pragma once
#include <cstdio>
#include <cstdlib>
#include <cmath>
namespace embed_test {
struct Case { const char* name; void (*run)(); Case* next; };
inline Case *first = nullptr, *last = nullptr;
inline unsigned failures = 0;
struct Register {
    Register(Case* c) { if (last) last->next = c; else first = c; last = c; }
};
inline bool check(bool ok, const char* expression, const char* file, int line) {
    if (!ok) { ++failures; std::printf("# %s:%d: %s\n", file, line, expression); }
    return ok;
}
template<class A, class B> bool equal(A actual, B expected, const char* file, int line) {
    bool ok = actual == expected;
    if (!check(ok, "equality", file, line))
        std::printf("# expected %.17Lg, actual %.17Lg\n", (long double)expected, (long double)actual);
    return ok;
}
inline bool near(double actual, double expected, double tolerance, const char* file, int line) {
    bool ok = tolerance >= 0 && std::isfinite(actual) && std::isfinite(expected) && std::fabs(actual-expected) <= tolerance;
    if (!check(ok, "absolute tolerance", file, line))
        std::printf("# expected %.17g +/- %.17g, actual %.17g\n", expected, tolerance, actual);
    return ok;
}
inline int run() {
    unsigned count = 0; for (auto* c=first; c; c=c->next) ++count;
    std::printf("TAP version 13\n1..%u\n", count);
    unsigned i=0;
    for (auto* c=first; c; c=c->next) {
        unsigned before=failures; c->run();
        std::printf("%s %u - %s\n", before == failures ? "ok" : "not ok", ++i, c->name);
    }
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
}
#define CHECK(x) (::embed_test::check(!!(x), #x, __FILE__, __LINE__))
#define CHECK_EQ(actual, expected) (::embed_test::equal((actual), (expected), __FILE__, __LINE__))
#define CHECK_NEAR(actual, expected, tolerance) (::embed_test::near((actual), (expected), (tolerance), __FILE__, __LINE__))
#define REQUIRE(x) do { if (!CHECK(x)) return; } while (0)
#define TEST_CASE(name) static void name(); static ::embed_test::Case name##_case{#name, name, nullptr}; static ::embed_test::Register name##_registration{&name##_case}; static void name()
#define EMBED_TEST_MAIN() int main() { return ::embed_test::run(); }
