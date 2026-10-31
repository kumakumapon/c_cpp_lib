// 確保ブロックの非重複・内容保持、解放後の統合、統計の整合、分割条件、アライメント、
// realloc、不正な解放、ロックの対応をモデルと突き合わせて固定するテスト。
#include "embed_test/embed_test.hpp"
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include "embed_heap/embed_heap.h"

namespace {
constexpr size_t kDefaultAlign = EMBED_HEAP_DEFAULT_ALIGNMENT;
constexpr size_t kBufSize = 16384;

struct Block {
    bool free;
    size_t payload;
};

void sink(void *user, const char *fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    static_cast<std::string *>(user)->append(tmp);
}

// embed_heap_dump の出力から物理順のブロック列を取り出す
std::vector<Block> blocks_of(const embed_heap_t &h) {
    std::string out;
    embed_heap_dump(&h, sink, &out);
    std::vector<Block> v;
    size_t pos = 0;
    while (pos < out.size()) {
        size_t eol = out.find('\n', pos);
        if (eol == std::string::npos) eol = out.size();
        const std::string line = out.substr(pos, eol - pos);
        pos = eol + 1;
        const size_t lb = line.find('[');
        const size_t pl = line.find("Payload:");
        if (lb == std::string::npos || pl == std::string::npos) continue;
        Block b;
        b.payload = (size_t)std::strtoull(line.c_str() + pl + 8, nullptr, 10);
        b.free = line.find("FREE") != std::string::npos;
        v.push_back(b);
    }
    return v;
}

struct Env {
    embed_heap_t h;
    alignas(64) uint8_t buf[kBufSize];
    size_t H = 0; // ブロックヘッダサイズ (初期化直後のオーバーヘッドから求める)

    explicit Env(size_t size = kBufSize, size_t offset = 0) {
        std::memset(buf, 0xCD, sizeof(buf));
        if (!embed_heap_init(&h, buf + offset, size)) {
            CHECK(false);
            return;
        }
        embed_heap_stats_t s;
        embed_heap_get_stats(&h, &s);
        H = s.overhead_size;
    }
    embed_heap_stats_t stats() const {
        embed_heap_stats_t s;
        CHECK(embed_heap_get_stats(&h, &s));
        return s;
    }
    size_t free_blocks() const {
        size_t n = 0;
        for (const Block &b : blocks_of(h)) n += b.free;
        return n;
    }
};

// 統計の恒等式: used + free + overhead == total、ブロック数・最大空きがダンプと一致
void check_consistent(const Env &e) {
    const embed_heap_stats_t s = e.stats();
    CHECK_EQ(s.used_size + s.free_size + s.overhead_size, s.total_size);
    const std::vector<Block> bl = blocks_of(e.h);
    CHECK_EQ(s.overhead_size, bl.size() * e.H);
    size_t used = 0, fr = 0, mx = 0, cnt = 0;
    bool prev_free = false;
    for (const Block &b : bl) {
        if (b.free) {
            fr += b.payload;
            mx = std::max(mx, b.payload);
            CHECK(!prev_free); // 空きブロックは必ず統合されている
        } else {
            used += b.payload;
            ++cnt;
        }
        prev_free = b.free;
    }
    CHECK_EQ(s.used_size, used);
    CHECK_EQ(s.free_size, fr);
    CHECK_EQ(s.max_free_block, mx);
    CHECK_EQ(s.alloc_count, cnt);
}

void fill(void *p, size_t n, unsigned seed) {
    uint8_t *b = static_cast<uint8_t *>(p);
    for (size_t i = 0; i < n; ++i) b[i] = (uint8_t)(seed * 31u + i * 7u + 1u);
}
bool verify(const void *p, size_t n, unsigned seed) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    for (size_t i = 0; i < n; ++i)
        if (b[i] != (uint8_t)(seed * 31u + i * 7u + 1u)) return false;
    return true;
}
} // namespace

TEST_CASE(init_validation) {
    alignas(64) uint8_t buf[512];
    embed_heap_t h;
    CHECK(!embed_heap_init(nullptr, buf, sizeof(buf)));
    CHECK(!embed_heap_init(&h, nullptr, sizeof(buf)));
    CHECK(!embed_heap_init(&h, buf, 0));

    // 初期ブロックヘッダ + 最小ペイロード + 1 バイトが最小サイズ
    Env probe;
    const size_t need = probe.H + kDefaultAlign;
    CHECK(!embed_heap_init(&h, buf, need));
    CHECK(embed_heap_init(&h, buf, need + 1));
    CHECK(!embed_heap_init(&h, buf + 1, need + 1)); // 先頭の切り捨てで足りなくなる
    CHECK(embed_heap_init(&h, buf + 1, need + 1 + kDefaultAlign));
}

TEST_CASE(init_stats_and_alignment) {
    Env e;
    const embed_heap_stats_t s = e.stats();
    CHECK_EQ(s.total_size, sizeof(e.buf));
    CHECK_EQ(s.used_size, 0u);
    CHECK_EQ(s.free_size, sizeof(e.buf) - e.H);
    CHECK_EQ(s.max_free_block, sizeof(e.buf) - e.H);
    CHECK_EQ(s.overhead_size, e.H);
    CHECK_EQ(s.alloc_count, 0u);
    CHECK_EQ(s.total_alloc_ops, 0u);
    CHECK_EQ(s.total_free_ops, 0u);
    CHECK_EQ(e.H % kDefaultAlign, 0u);

    // 先頭が揃っていないバッファは切り詰めて使う
    Env m(4000, 3);
    const size_t skip = (kDefaultAlign - 3 % kDefaultAlign) % kDefaultAlign;
    const embed_heap_stats_t t = m.stats();
    CHECK_EQ(t.total_size, 4000 - skip);
    CHECK_EQ(t.free_size, 4000 - skip - m.H);
    void *p = embed_heap_alloc(&m.h, 10);
    CHECK(p != nullptr);
    CHECK_EQ((uintptr_t)p % kDefaultAlign, 0u);

    // 初期化するたびにロックとカウンタは初期値に戻る
    embed_heap_stats_t u;
    embed_heap_get_stats(&e.h, &u);
    embed_heap_alloc(&e.h, 8);
    CHECK(embed_heap_init(&e.h, e.buf, sizeof(e.buf)));
    embed_heap_get_stats(&e.h, &u);
    CHECK_EQ(u.total_alloc_ops, 0u);
    CHECK_EQ(u.alloc_count, 0u);
}

TEST_CASE(get_stats_argument_checks) {
    Env e;
    embed_heap_stats_t s;
    CHECK(!embed_heap_get_stats(nullptr, &s));
    CHECK(!embed_heap_get_stats(&e.h, nullptr));
}

TEST_CASE(alloc_returns_distinct_aligned_blocks_with_intact_data) {
    Env e;
    const size_t sizes[] = {1, 7, 8, 9, 31, 100, 257, 1000};
    std::vector<void *> ptrs;
    for (size_t i = 0; i < 8; ++i) {
        void *p = embed_heap_alloc(&e.h, sizes[i]);
        REQUIRE(p != nullptr);
        CHECK_EQ((uintptr_t)p % kDefaultAlign, 0u);
        CHECK((uint8_t *)p >= e.buf + e.H);
        CHECK((uint8_t *)p + sizes[i] <= e.buf + sizeof(e.buf));
        fill(p, sizes[i], (unsigned)i);
        ptrs.push_back(p);
    }
    // 後から書いても先の内容が壊れない (重なりなし)
    for (size_t i = 0; i < 8; ++i) CHECK(verify(ptrs[i], sizes[i], (unsigned)i));
    // 隣り合うブロックはヘッダを挟んで並ぶ
    for (size_t i = 0; i + 1 < 8; ++i) {
        const size_t padded = (sizes[i] + kDefaultAlign - 1) / kDefaultAlign * kDefaultAlign;
        CHECK_EQ((size_t)((uint8_t *)ptrs[i + 1] - (uint8_t *)ptrs[i]), padded + e.H);
    }
    check_consistent(e);
}

TEST_CASE(alloc_size_rounding_in_stats) {
    Env e;
    embed_heap_alloc(&e.h, 1); // 最小ペイロードに切り上げ
    CHECK_EQ(e.stats().used_size, kDefaultAlign);
    embed_heap_alloc(&e.h, kDefaultAlign + 1);
    CHECK_EQ(e.stats().used_size, kDefaultAlign + 2 * kDefaultAlign);
    CHECK_EQ(e.stats().alloc_count, 2u);
    CHECK_EQ(e.stats().total_alloc_ops, 2u);
    check_consistent(e);
}

TEST_CASE(alloc_failure_cases) {
    Env e;
    CHECK(embed_heap_alloc(&e.h, 0) == nullptr);
    CHECK(embed_heap_alloc(nullptr, 8) == nullptr);
    CHECK(embed_heap_alloc(&e.h, SIZE_MAX) == nullptr);
    CHECK(embed_heap_alloc(&e.h, SIZE_MAX - kDefaultAlign + 2) == nullptr); // 切り上げでオーバーフロー
    CHECK_EQ(e.stats().total_alloc_ops, 0u);

    embed_heap_t blank;
    std::memset(&blank, 0, sizeof(blank));
    CHECK(embed_heap_alloc(&blank, 8) == nullptr);

    // ちょうど入る最大サイズは成功し、1 バイトでも超えると失敗
    const size_t cap = e.stats().max_free_block;
    CHECK(embed_heap_alloc(&e.h, cap + 1) == nullptr);
    CHECK(embed_heap_alloc(&e.h, cap + kDefaultAlign) == nullptr);
    void *all = embed_heap_alloc(&e.h, cap);
    CHECK(all != nullptr);
    CHECK_EQ(e.stats().free_size, 0u);
    CHECK_EQ(e.stats().max_free_block, 0u);
    // 満杯時は失敗し、その後も健全
    CHECK(embed_heap_alloc(&e.h, 1) == nullptr);
    check_consistent(e);
    embed_heap_free(&e.h, all);
    CHECK_EQ(e.stats().free_size, cap);
    CHECK(embed_heap_alloc(&e.h, 64) != nullptr);
}

TEST_CASE(split_threshold) {
    // 空きブロックの余りが ヘッダ + 最小ペイロード 以上のときだけ分割する
    Env e(kBufSize);
    const size_t H = e.H;
    const size_t P = e.stats().max_free_block;

    Env a(H + 256);
    const size_t Pa = a.stats().max_free_block;
    CHECK_EQ(Pa, 256u);
    void *p = embed_heap_alloc(&a.h, Pa - (H + kDefaultAlign)); // 余りがちょうど H + 最小 → 分割
    CHECK(p != nullptr);
    CHECK_EQ(a.stats().overhead_size, 2 * H);
    CHECK_EQ(a.stats().free_size, kDefaultAlign);
    CHECK_EQ(a.stats().used_size, Pa - H - kDefaultAlign);
    check_consistent(a);

    Env b(H + 256);
    void *q = embed_heap_alloc(&b.h, Pa - H); // 余りが H だけ (最小に満たない) → 丸ごと割り当て
    CHECK(q != nullptr);
    CHECK_EQ(b.stats().overhead_size, H);
    CHECK_EQ(b.stats().free_size, 0u);
    CHECK_EQ(b.stats().used_size, Pa);
    check_consistent(b);
    (void)P;
}

TEST_CASE(free_coalesces_in_every_order) {
    // 4 ブロックを確保して、全 24 通りの順序で解放 -> 全体が 1 つの空きに戻り、大きな確保が通る
    std::vector<int> order = {0, 1, 2, 3};
    do {
        Env e;
        const size_t whole = e.stats().max_free_block;
        void *p[4];
        const size_t sz[4] = {100, 200, 50, 300};
        for (int i = 0; i < 4; ++i) {
            p[i] = embed_heap_alloc(&e.h, sz[i]);
            REQUIRE(p[i] != nullptr);
            fill(p[i], sz[i], (unsigned)i);
        }
        // 先頭を使い切らない (末尾に空きが続く) 状態で解放する
        std::vector<bool> freed(4, false);
        for (int k = 0; k < 4; ++k) {
            const int i = order[(size_t)k];
            embed_heap_free(&e.h, p[i]);
            freed[(size_t)i] = true;
            for (int j = 0; j < 4; ++j) {
                if (!freed[(size_t)j]) CHECK(verify(p[j], sz[j], (unsigned)j)); // 他は無傷
            }
            check_consistent(e);
        }
        CHECK_EQ(e.free_blocks(), 1u);
        CHECK_EQ(e.stats().max_free_block, whole);
        CHECK_EQ(e.stats().free_size, whole);
        CHECK_EQ(e.stats().overhead_size, e.H);
        void *big = embed_heap_alloc(&e.h, whole);
        CHECK(big != nullptr);
    } while (std::next_permutation(order.begin(), order.end()));
}

TEST_CASE(free_merge_directions) {
    Env e;
    void *a = embed_heap_alloc(&e.h, 64);
    void *b = embed_heap_alloc(&e.h, 64);
    void *c = embed_heap_alloc(&e.h, 64);
    void *d = embed_heap_alloc(&e.h, 64); // 末尾に空きが続く

    // 前方統合: a を解放してから b を解放すると a と b が 1 つに (2*64 + ヘッダ)
    embed_heap_free(&e.h, a);
    embed_heap_free(&e.h, b);
    std::vector<Block> bl = blocks_of(e.h);
    CHECK(bl[0].free);
    CHECK_EQ(bl[0].payload, 64 + e.H + 64);
    CHECK(!bl[1].free);
    // 後方統合: c を解放すると d が使用中のまま、先頭の空きに c が吸収される
    embed_heap_free(&e.h, c);
    bl = blocks_of(e.h);
    CHECK(bl[0].free);
    CHECK_EQ(bl[0].payload, 3 * 64 + 2 * e.H);
    // 両側統合: d を解放すると先頭の空き・d・末尾の空きがすべて 1 つに
    embed_heap_free(&e.h, d);
    CHECK_EQ(e.free_blocks(), 1u);
    CHECK_EQ(blocks_of(e.h).size(), 1u);
}

TEST_CASE(fragmentation_statistics) {
    Env e;
    std::vector<void *> p;
    for (int i = 0; i < 20; ++i) {
        p.push_back(embed_heap_alloc(&e.h, 100));
        REQUIRE(p.back() != nullptr);
    }
    for (int i = 0; i < 20; i += 2) embed_heap_free(&e.h, p[(size_t)i]); // 1 つおきに解放
    check_consistent(e);
    const embed_heap_stats_t s = e.stats();
    const size_t blk = (100 + kDefaultAlign - 1) / kDefaultAlign * kDefaultAlign;
    CHECK_EQ(s.alloc_count, 10u);
    CHECK_EQ(s.used_size, 10 * blk);
    CHECK_EQ(s.total_free_ops, 10u);
    CHECK_EQ(s.total_alloc_ops, 20u);
    // 穴は 100 バイト級 (blk) に分かれ、最大空きは末尾の大きな空き
    CHECK_EQ(e.free_blocks(), 11u);
    // 穴に収まる確保は穴に入り、収まらない確保は末尾に回る
    void *small = embed_heap_alloc(&e.h, blk);
    CHECK((uint8_t *)small == (uint8_t *)p[0]);
    void *big = embed_heap_alloc(&e.h, blk + 1);
    CHECK((uint8_t *)big > (uint8_t *)p[19]);
    check_consistent(e);
}

TEST_CASE(calloc_behaviour) {
    Env e;
    // 以前の内容が残っていても 0 で返す
    void *dirty = embed_heap_alloc(&e.h, 200);
    std::memset(dirty, 0xEE, 200);
    embed_heap_free(&e.h, dirty);
    uint8_t *z = (uint8_t *)embed_heap_calloc(&e.h, 20, 10);
    REQUIRE(z != nullptr);
    for (int i = 0; i < 200; ++i) CHECK_EQ((int)z[i], 0);

    CHECK(embed_heap_calloc(&e.h, 0, 10) == nullptr);
    CHECK(embed_heap_calloc(&e.h, 10, 0) == nullptr);
    CHECK(embed_heap_calloc(&e.h, SIZE_MAX / 2 + 1, 2) == nullptr); // 乗算オーバーフロー
    CHECK(embed_heap_calloc(&e.h, 1u << 20, 1u << 20) == nullptr);
    check_consistent(e);
}

TEST_CASE(aligned_alloc_boundaries) {
    Env e;
    const size_t aligns[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 1024, 4096};
    std::vector<std::pair<void *, size_t>> live;
    unsigned seed = 1;
    for (size_t a : aligns) {
        void *p = embed_heap_aligned_alloc(&e.h, 50, a);
        REQUIRE(p != nullptr);
        CHECK_EQ((uintptr_t)p % std::max(a, kDefaultAlign), 0u);
        fill(p, 50, seed);
        live.push_back({p, seed++});
        check_consistent(e);
    }
    for (auto &l : live) CHECK(verify(l.first, 50, (unsigned)l.second));

    // 既定の境界 (8) 以上で 2 の累乗でない値は拒否
    for (size_t bad : {12u, 24u, 48u, 100u, 1000u}) {
        CHECK(embed_heap_aligned_alloc(&e.h, 16, bad) == nullptr);
    }
    CHECK_EQ(e.stats().alloc_count, live.size());
    // 既定より小さい値は 2 の累乗かどうかに関係なく既定の境界に引き上げられる (0 も同様)
    for (size_t low : {0u, 3u, 5u, 6u}) {
        void *p = embed_heap_aligned_alloc(&e.h, 16, low);
        CHECK(p != nullptr);
        CHECK_EQ((uintptr_t)p % kDefaultAlign, 0u);
        embed_heap_free(&e.h, p);
    }

    // 解放するとパディング用の空きも含めて全部 1 つに戻る
    for (auto &l : live) embed_heap_free(&e.h, l.first);
    CHECK_EQ(e.stats().alloc_count, 0u);
    CHECK_EQ(e.free_blocks(), 1u);
    check_consistent(e);
}

TEST_CASE(aligned_alloc_restores_after_free) {
    Env e;
    const size_t whole = e.stats().max_free_block;
    void *a = embed_heap_aligned_alloc(&e.h, 100, 256);
    void *b = embed_heap_aligned_alloc(&e.h, 100, 512);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK_EQ((uintptr_t)a % 256, 0u);
    CHECK_EQ((uintptr_t)b % 512, 0u);
    check_consistent(e);
    embed_heap_free(&e.h, b);
    embed_heap_free(&e.h, a);
    check_consistent(e);
    CHECK_EQ(e.free_blocks(), 1u);
    CHECK_EQ(e.stats().max_free_block, whole);
}

TEST_CASE(aligned_alloc_too_large_alignment_fails_cleanly) {
    Env e(1024);
    void *p = embed_heap_aligned_alloc(&e.h, 64, 1u << 20);
    CHECK(p == nullptr);
    check_consistent(e);
    CHECK(embed_heap_alloc(&e.h, 64) != nullptr);
}

TEST_CASE(free_invalid_pointers_are_ignored) {
    Env e;
    void *a = embed_heap_alloc(&e.h, 100);
    void *b = embed_heap_alloc(&e.h, 100);
    fill(a, 100, 1);
    fill(b, 100, 2);
    const embed_heap_stats_t before = e.stats();

    embed_heap_free(&e.h, nullptr);
    embed_heap_free(nullptr, a);
    int on_stack = 0;
    embed_heap_free(&e.h, &on_stack);                 // ヒープ外
    embed_heap_free(&e.h, e.buf + sizeof(e.buf));     // 末尾の外
    embed_heap_free(&e.h, (uint8_t *)a + 8);          // ブロックの途中
    embed_heap_free(&e.h, (uint8_t *)a + 1);
    CHECK(verify(a, 100, 1));
    CHECK(verify(b, 100, 2));
    const embed_heap_stats_t after = e.stats();
    CHECK_EQ(after.alloc_count, before.alloc_count);
    CHECK_EQ(after.used_size, before.used_size);
    CHECK_EQ(after.total_free_ops, before.total_free_ops);
    check_consistent(e);
}

TEST_CASE(free_pointer_into_first_header_is_ignored) {
    // 先頭付近を指すポインタでヒープより前を読まない (ヒープを malloc で確保し ASan で検出する)
    const size_t size = 1024;
    uint8_t *raw = static_cast<uint8_t *>(std::malloc(size + 64));
    REQUIRE(raw != nullptr);
    embed_heap_t h;
    REQUIRE(embed_heap_init(&h, raw, size));
    void *p = embed_heap_alloc(&h, 32);
    REQUIRE(p != nullptr);
    uint8_t *base = h.buffer;
    embed_heap_free(&h, base);       // ヘッダより手前になるポインタ
    embed_heap_free(&h, base + 1);
    embed_heap_free(&h, base + 7);
    CHECK(embed_heap_realloc(&h, base + 1, 16) == nullptr);
    embed_heap_stats_t s;
    CHECK(embed_heap_get_stats(&h, &s));
    CHECK_EQ(s.alloc_count, 1u);
    CHECK_EQ(s.total_free_ops, 0u);
    std::free(raw);
}

TEST_CASE(double_free_is_ignored) {
    Env e;
    void *a = embed_heap_alloc(&e.h, 100);
    void *b = embed_heap_alloc(&e.h, 100);
    void *c = embed_heap_alloc(&e.h, 100);
    fill(c, 100, 3);
    embed_heap_free(&e.h, b);     // 周囲が使用中で統合なし
    embed_heap_free(&e.h, b);     // 空きブロックの再解放
    CHECK_EQ(e.stats().total_free_ops, 1u);
    embed_heap_free(&e.h, a);     // 統合で b は a に吸収される
    embed_heap_free(&e.h, b);     // 吸収済み (マジック無効) の再解放
    embed_heap_free(&e.h, a);
    CHECK_EQ(e.stats().total_free_ops, 2u);
    CHECK(verify(c, 100, 3));
    check_consistent(e);
    // 二重確保になっていない: 続けて確保したブロックが重ならない
    void *x = embed_heap_alloc(&e.h, 100);
    void *y = embed_heap_alloc(&e.h, 100);
    REQUIRE(x != nullptr);
    REQUIRE(y != nullptr);
    fill(x, 100, 10);
    fill(y, 100, 11);
    CHECK(verify(x, 100, 10));
    CHECK(verify(c, 100, 3));
    CHECK(x != y);
}

TEST_CASE(corrupted_header_is_detected) {
    Env e;
    void *a = embed_heap_alloc(&e.h, 64);
    void *b = embed_heap_alloc(&e.h, 64);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    uint32_t *magic_b = reinterpret_cast<uint32_t *>((uint8_t *)b - e.H);
    CHECK_EQ(*magic_b, (uint32_t)EMBED_HEAP_MAGIC);
    *magic_b = 0xDEADBEEF;
    embed_heap_stats_t s;
    CHECK(!embed_heap_get_stats(&e.h, &s));
    CHECK(embed_heap_alloc(&e.h, 16) == nullptr);   // 走査が壊れたブロックで止まる
    embed_heap_free(&e.h, b);                       // 不正ヘッダは解放しない
    CHECK_EQ(e.h.total_free_ops, 0u);
    *magic_b = EMBED_HEAP_MAGIC;
    CHECK(embed_heap_get_stats(&e.h, &s));
}

// ---- realloc ----
TEST_CASE(realloc_null_and_zero) {
    Env e;
    void *p = embed_heap_realloc(&e.h, nullptr, 40);
    REQUIRE(p != nullptr);
    CHECK_EQ(e.stats().alloc_count, 1u);
    CHECK(embed_heap_realloc(&e.h, nullptr, 0) == nullptr);
    CHECK(embed_heap_realloc(nullptr, p, 10) == nullptr);
    CHECK(embed_heap_realloc(&e.h, p, 0) == nullptr); // free と同じ
    CHECK_EQ(e.stats().alloc_count, 0u);
    CHECK_EQ(e.stats().total_free_ops, 1u);
    CHECK_EQ(e.free_blocks(), 1u);
}

TEST_CASE(realloc_invalid_pointers) {
    Env e;
    void *p = embed_heap_alloc(&e.h, 64);
    fill(p, 64, 5);
    int outside = 0;
    CHECK(embed_heap_realloc(&e.h, &outside, 32) == nullptr);
    CHECK(embed_heap_realloc(&e.h, (uint8_t *)p + 8, 32) == nullptr);
    void *gone = embed_heap_alloc(&e.h, 64);
    embed_heap_free(&e.h, gone);
    CHECK(embed_heap_realloc(&e.h, gone, 32) == nullptr);
    CHECK(embed_heap_realloc(&e.h, p, SIZE_MAX) == nullptr);
    CHECK(embed_heap_realloc(&e.h, p, SIZE_MAX - kDefaultAlign + 2) == nullptr);
    CHECK(verify(p, 64, 5));
    check_consistent(e);
}

TEST_CASE(realloc_same_and_smaller_keep_pointer_and_prefix) {
    Env e;
    void *a = embed_heap_alloc(&e.h, 400);
    void *b = embed_heap_alloc(&e.h, 64); // 後続は使用中
    fill(a, 400, 1);
    // 同じサイズ・同じ丸めのサイズ: 何も変わらない
    CHECK(embed_heap_realloc(&e.h, a, 400) == a);
    const size_t blocks_before = blocks_of(e.h).size();
    CHECK(embed_heap_realloc(&e.h, a, 399) == a);
    CHECK_EQ(blocks_of(e.h).size(), blocks_before);

    // 小さな縮小は余りが小さすぎて分割しない: ブロックは元のまま
    CHECK(embed_heap_realloc(&e.h, a, 400 - kDefaultAlign) == a);
    CHECK_EQ(blocks_of(e.h).size(), blocks_before);

    // 大きな縮小は余りを空きブロックにする
    CHECK(embed_heap_realloc(&e.h, a, 100) == a);
    CHECK(verify(a, 100, 1));
    std::vector<Block> bl = blocks_of(e.h);
    CHECK_EQ(bl.size(), blocks_before + 1);
    CHECK(!bl[0].free);
    CHECK_EQ(bl[0].payload, (100 + kDefaultAlign - 1) / kDefaultAlign * kDefaultAlign);
    CHECK(bl[1].free);
    CHECK_EQ(bl[1].payload, 400 - bl[0].payload - e.H);
    check_consistent(e);

    // 縮小後の空きは、続く確保に使える
    void *c = embed_heap_alloc(&e.h, bl[1].payload);
    CHECK((uint8_t *)c == (uint8_t *)a + bl[0].payload + e.H);
    CHECK(b != nullptr);
}

TEST_CASE(realloc_shrink_merges_with_following_free_block) {
    Env e;
    void *a = embed_heap_alloc(&e.h, 400); // 後ろは大きな空き
    embed_heap_realloc(&e.h, a, 64);
    CHECK_EQ(e.free_blocks(), 1u);          // 余りは末尾の空きと 1 つになる
    check_consistent(e);
    CHECK_EQ(e.stats().used_size, 64u);
}

TEST_CASE(realloc_grow_in_place_when_next_is_free) {
    Env e;
    void *a = embed_heap_alloc(&e.h, 100);
    fill(a, 100, 4);
    void *r = embed_heap_realloc(&e.h, a, 600);
    CHECK(r == a); // 後続が空きなので位置は動かない
    CHECK(verify(r, 100, 4));
    const embed_heap_stats_t s = e.stats();
    CHECK_EQ(s.alloc_count, 1u);
    CHECK_EQ(s.used_size, (600 + kDefaultAlign - 1) / kDefaultAlign * kDefaultAlign);
    CHECK_EQ(e.free_blocks(), 1u);
    check_consistent(e);

    // 後続の空きをほぼ使い切る拡張 (余りがヘッダ分だけで分割に足りない)
    const size_t total_payload = e.stats().used_size + e.stats().free_size + e.H;
    void *r2 = embed_heap_realloc(&e.h, r, total_payload - e.H);
    CHECK(r2 == r);
    CHECK_EQ(e.stats().free_size, 0u);
    CHECK_EQ(e.free_blocks(), 0u);
    check_consistent(e);
}

TEST_CASE(realloc_grow_moves_when_blocked_and_preserves_data) {
    Env e;
    void *a = embed_heap_alloc(&e.h, 100);
    void *b = embed_heap_alloc(&e.h, 100);
    fill(a, 100, 1);
    fill(b, 100, 2);
    void *r = embed_heap_realloc(&e.h, a, 500);
    REQUIRE(r != nullptr);
    CHECK(r != a);
    CHECK(verify(r, 100, 1)); // 旧内容 (旧サイズ分) が保たれる
    CHECK(verify(b, 100, 2));
    CHECK_EQ(e.stats().alloc_count, 2u);
    check_consistent(e);
    // 旧ブロックは空きに戻っている
    void *reuse = embed_heap_alloc(&e.h, 100);
    CHECK((uint8_t *)reuse == (uint8_t *)a);
}

TEST_CASE(realloc_failure_keeps_original) {
    Env e(2048);
    const size_t cap = e.stats().max_free_block;
    void *a = embed_heap_alloc(&e.h, cap / 2 - e.H);
    void *b = embed_heap_alloc(&e.h, cap / 2 - e.H);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    fill(a, 64, 7);
    const embed_heap_stats_t before = e.stats();
    CHECK(embed_heap_realloc(&e.h, a, cap * 2) == nullptr);
    CHECK(embed_heap_realloc(&e.h, a, cap) == nullptr);
    CHECK(verify(a, 64, 7));
    const embed_heap_stats_t after = e.stats();
    CHECK_EQ(after.used_size, before.used_size);
    CHECK_EQ(after.alloc_count, before.alloc_count);
    CHECK_EQ(after.total_free_ops, before.total_free_ops);
    check_consistent(e);
    // 元のブロックは有効なまま解放できる
    embed_heap_free(&e.h, a);
    CHECK_EQ(e.stats().alloc_count, before.alloc_count - 1);
}


TEST_CASE(free_ignores_fake_headers_around_the_heap) {
    // ヒープの手前・末尾直後・揃っていない位置に「それらしい」ヘッダを置いても、解放は無視される
    Env probe;
    const size_t H = probe.H;
    alignas(64) static uint8_t mem[4096];
    std::memset(mem, 0, sizeof(mem));
    embed_heap_block_t fake;
    std::memset(&fake, 0, sizeof(fake));
    fake.magic = EMBED_HEAP_MAGIC;
    fake.size = 8;
    fake.is_free = false;

    const size_t off = 128;
    const size_t size = 1024;
    embed_heap_t h;
    REQUIRE(embed_heap_init(&h, mem + off, size));
    // (1) 先頭ポインタの直前 (ヒープの外側)
    std::memcpy(mem + off - H, &fake, sizeof(fake));
    embed_heap_free(&h, h.buffer);
    CHECK_EQ(h.total_free_ops, 0u);
    CHECK(embed_heap_realloc(&h, h.buffer, 16) == nullptr);
    CHECK(!reinterpret_cast<embed_heap_block_t *>(mem + off - H)->is_free);

    // 全域を使い切って、末尾直後ポインタの手前 (最後のブロックの内側) に偽ヘッダを置く
    void *all = embed_heap_alloc(&h, size - H);
    REQUIRE(all != nullptr);
    std::memcpy(h.buffer + size - H, &fake, sizeof(fake));
    embed_heap_free(&h, h.buffer + size); // ヒープ末尾の外
    CHECK_EQ(h.total_free_ops, 0u);
    CHECK(!reinterpret_cast<embed_heap_block_t *>(h.buffer + size - H)->is_free);

    // (2) 境界に揃っていないポインタ
    std::memcpy((uint8_t *)all + 61, &fake, sizeof(fake));
    embed_heap_free(&h, (uint8_t *)all + 61 + H);
    CHECK_EQ(h.total_free_ops, 0u);
    CHECK(embed_heap_realloc(&h, (uint8_t *)all + 61 + H, 16) == nullptr);
    CHECK_EQ(h.total_free_ops, 0u);
}

TEST_CASE(realloc_split_threshold_exact) {
    Env e;
    const size_t H = e.H;
    // 縮小: 余りがちょうど ヘッダ + 最小ペイロード なら分割する
    void *a = embed_heap_alloc(&e.h, 256);
    void *b = embed_heap_alloc(&e.h, 64);
    REQUIRE(b != nullptr);
    CHECK(embed_heap_realloc(&e.h, a, 256 - (H + kDefaultAlign)) == a);
    std::vector<Block> bl = blocks_of(e.h);
    CHECK(bl[1].free);
    CHECK_EQ(bl[1].payload, kDefaultAlign);
    check_consistent(e);

    // 拡張 (後続の空きに収める): 余りがちょうど ヘッダ + 最小ペイロード なら再分割する
    Env g;
    void *p = embed_heap_alloc(&g.h, 64);
    void *x = embed_heap_alloc(&g.h, 128);
    void *c = embed_heap_alloc(&g.h, 64);
    REQUIRE(c != nullptr);
    embed_heap_free(&g.h, x);
    const size_t combined = 64 + H + 128;
    CHECK(embed_heap_realloc(&g.h, p, combined - (H + kDefaultAlign)) == p);
    bl = blocks_of(g.h);
    CHECK_EQ(bl[0].payload, combined - (H + kDefaultAlign));
    CHECK(bl[1].free);
    CHECK_EQ(bl[1].payload, kDefaultAlign);
    check_consistent(g);

    // 拡張: 後続の空きをちょうど使い切る (結合後のサイズと要求が等しい)
    Env k;
    void *q = embed_heap_alloc(&k.h, 64);
    void *y = embed_heap_alloc(&k.h, 128);
    void *z = embed_heap_alloc(&k.h, 64);
    REQUIRE(z != nullptr);
    embed_heap_free(&k.h, y);
    CHECK(embed_heap_realloc(&k.h, q, combined) == q);
    CHECK_EQ(k.free_blocks(), 1u); // 手前の空きは消え、末尾の空きだけが残る
    CHECK_EQ(blocks_of(k.h)[0].payload, combined);
    check_consistent(k);
}

TEST_CASE(dump_reports_buffer_range) {
    Env e;
    std::string out;
    embed_heap_dump(&e.h, sink, &out);
    const size_t at = out.find("Buffer: ");
    REQUIRE(at != std::string::npos);
    char *end1 = nullptr;
    const unsigned long long lo = std::strtoull(out.c_str() + at + 8, &end1, 16);
    const size_t dash = out.find(" - ", at);
    REQUIRE(dash != std::string::npos);
    const unsigned long long hi = std::strtoull(out.c_str() + dash + 3, nullptr, 16);
    CHECK_EQ((unsigned long long)(uintptr_t)e.h.buffer, lo);
    CHECK_EQ(hi - lo, (unsigned long long)sizeof(e.buf));
}

// ---- ロック ----
namespace {
struct LockLog {
    int enter = 0, leave = 0, depth = 0, max_depth = 0;
    void *seen_user = nullptr;
    bool mismatched = false;
};
void lk(void *u) {
    LockLog *l = static_cast<LockLog *>(u);
    ++l->enter;
    ++l->depth;
    l->max_depth = std::max(l->max_depth, l->depth);
}
void ulk(void *u) {
    LockLog *l = static_cast<LockLog *>(u);
    ++l->leave;
    --l->depth;
}
} // namespace

TEST_CASE(lock_is_balanced_and_not_nested) {
    Env e;
    LockLog log;
    embed_heap_set_lock(&e.h, lk, ulk, &log);
    void *a = embed_heap_alloc(&e.h, 64);
    CHECK_EQ(log.enter, 1);
    void *b = embed_heap_aligned_alloc(&e.h, 64, 128);
    CHECK_EQ(log.enter, 2);
    void *c = embed_heap_calloc(&e.h, 4, 16);
    CHECK_EQ(log.enter, 3);
    embed_heap_free(&e.h, c);
    CHECK_EQ(log.enter, 4);
    void *a2 = embed_heap_realloc(&e.h, a, 1000); // 移動を伴う: ロック中に確保と解放を行う
    CHECK(a2 != nullptr);
    embed_heap_realloc(&e.h, a2, 8);
    embed_heap_realloc(&e.h, nullptr, 8);
    embed_heap_realloc(&e.h, (uint8_t *)b + 1, 8);  // 不正
    embed_heap_free(&e.h, nullptr);
    embed_heap_alloc(&e.h, 0);
    embed_heap_alloc(&e.h, SIZE_MAX);
    CHECK(log.enter > 8);
    CHECK_EQ(log.enter, log.leave);
    CHECK_EQ(log.depth, 0);
    CHECK_EQ(log.max_depth, 1);

    // 解除するとそれ以降は呼ばれない
    embed_heap_set_lock(&e.h, nullptr, nullptr, nullptr);
    const int n = log.enter;
    embed_heap_alloc(&e.h, 8);
    CHECK_EQ(log.enter, n);
    embed_heap_set_lock(nullptr, lk, ulk, &log);
}

// ---- 乱択ストレス (モデルとの突き合わせ) ----
TEST_CASE(randomized_model_check) {
    Env e;
    struct Live {
        uint8_t *p;
        size_t n;
        unsigned seed;
    };
    std::vector<Live> live;
    uint32_t rng = 12345;
    auto next = [&rng]() {
        rng = rng * 1664525u + 1013904223u;
        return rng >> 8;
    };
    unsigned seed = 1;
    for (int step = 0; step < 3000; ++step) {
        const unsigned op = next() % 10;
        if (op < 4 || live.empty()) {
            const size_t n = 1 + next() % 300;
            const size_t al = (next() % 5 == 0) ? (size_t)(16u << (next() % 4)) : 0;
            uint8_t *p = static_cast<uint8_t *>(al ? embed_heap_aligned_alloc(&e.h, n, al)
                                                   : embed_heap_alloc(&e.h, n));
            if (p) {
                if (al) CHECK_EQ((uintptr_t)p % al, 0u);
                CHECK_EQ((uintptr_t)p % kDefaultAlign, 0u);
                fill(p, n, seed);
                live.push_back({p, n, seed++});
            }
        } else if (op < 7) {
            const size_t i = next() % live.size();
            CHECK(verify(live[i].p, live[i].n, live[i].seed));
            embed_heap_free(&e.h, live[i].p);
            live.erase(live.begin() + (long)i);
        } else {
            const size_t i = next() % live.size();
            const size_t n = 1 + next() % 400;
            uint8_t *q = static_cast<uint8_t *>(embed_heap_realloc(&e.h, live[i].p, n));
            if (q) {
                CHECK(verify(q, std::min(n, live[i].n), live[i].seed));
                fill(q, n, live[i].seed);
                live[i].p = q;
                live[i].n = n;
            } else {
                CHECK(verify(live[i].p, live[i].n, live[i].seed)); // 失敗しても元は有効
            }
        }
        if (step % 50 == 0) {
            check_consistent(e);
            // 全生存ブロックが互いに重ならず内容も無傷
            std::vector<Live> sorted = live;
            std::sort(sorted.begin(), sorted.end(), [](const Live &a, const Live &b) { return a.p < b.p; });
            for (size_t k = 0; k < sorted.size(); ++k) {
                CHECK(verify(sorted[k].p, sorted[k].n, sorted[k].seed));
                if (k + 1 < sorted.size()) CHECK(sorted[k].p + sorted[k].n <= sorted[k + 1].p);
            }
            CHECK_EQ(e.stats().alloc_count, live.size());
        }
    }
    for (Live &l : live) embed_heap_free(&e.h, l.p);
    check_consistent(e);
    CHECK_EQ(e.free_blocks(), 1u);
    CHECK_EQ(e.stats().free_size, sizeof(e.buf) - e.H);
    CHECK_EQ(e.stats().alloc_count, 0u);
}

TEST_CASE(dump_format) {
    Env e;
    void *a = embed_heap_alloc(&e.h, 64);
    (void)a;
    std::string out;
    embed_heap_dump(&e.h, sink, &out);
    CHECK(out.find("=== EmbedHeap Dump ===") == 0);
    CHECK(out.find("Total 16384 bytes") != std::string::npos);
    CHECK(out.find("[000]") != std::string::npos);
    CHECK(out.find("USED") != std::string::npos);
    CHECK(out.find("FREE") != std::string::npos);
    CHECK(out.find("======================") != std::string::npos);
    embed_heap_dump(nullptr, sink, &out);
    embed_heap_dump(&e.h, nullptr, &out);
}

EMBED_TEST_MAIN()
