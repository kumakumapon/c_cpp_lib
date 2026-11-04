// peek / skip / find / ゼロコピー (linear + commit) / 状態問い合わせ と、
// 生産者・消費者を別スレッドで動かした SPSC 動作を検証する。
#include "embed_ringbuf/embed_ringbuf.h"
#include "embed_test/embed_test.hpp"
#include <atomic>
#include <cstring>
#include <deque>
#include <thread>
#include <vector>

namespace {
void fill(embed_ringbuf_t &rb, uint8_t from, size_t n) {
    for (size_t i = 0; i < n; ++i)
        embed_ringbuf_put(&rb, (uint8_t)(from + i));
}
} // namespace

TEST_CASE(state_queries) {
    uint8_t st[5];
    embed_ringbuf_t rb;
    embed_ringbuf_init(&rb, st, 5);
    CHECK_EQ(embed_ringbuf_capacity(&rb), 4u);
    CHECK(embed_ringbuf_is_empty(&rb));
    CHECK(!embed_ringbuf_is_full(&rb));
    fill(rb, 0, 4);
    CHECK(embed_ringbuf_is_full(&rb));
    CHECK(!embed_ringbuf_is_empty(&rb));
    CHECK_EQ(embed_ringbuf_capacity(nullptr), 0u);
    CHECK(embed_ringbuf_is_empty(nullptr));
    CHECK(!embed_ringbuf_is_full(nullptr));
    embed_ringbuf_t bad;
    embed_ringbuf_init(&bad, st, 1);
    CHECK_EQ(embed_ringbuf_capacity(&bad), 0u);
    CHECK(!embed_ringbuf_is_full(&bad)); // 使えないバッファは満杯とは言わない
}

TEST_CASE(peek_does_not_consume_and_handles_wrap) {
    uint8_t st[6], out[8];
    embed_ringbuf_t rb;
    embed_ringbuf_init(&rb, st, 6);
    fill(rb, 10, 4);       // 10 11 12 13
    embed_ringbuf_skip(&rb, 3); // 13 だけ残る (tail = 3)
    fill(rb, 20, 4);       // 13 20 21 22 23 (折り返し)
    uint8_t b = 0;
    CHECK(embed_ringbuf_peek(&rb, 0, &b));
    CHECK_EQ(b, 13u);
    CHECK(embed_ringbuf_peek(&rb, 4, &b));
    CHECK_EQ(b, 23u);
    CHECK(!embed_ringbuf_peek(&rb, 5, &b)); // 格納数ちょうどは範囲外
    CHECK_EQ(embed_ringbuf_peek_bytes(&rb, 1, out, 8), 4u);
    const uint8_t expect[] = {20, 21, 22, 23};
    CHECK_MEM_EQ(out, expect, 4);
    CHECK_EQ(embed_ringbuf_peek_bytes(&rb, 2, out, 1), 1u);
    CHECK_EQ(out[0], 21u);
    CHECK_EQ(embed_ringbuf_available(&rb), 5u); // 何も消費していない
    CHECK_EQ(embed_ringbuf_peek_bytes(&rb, 0, nullptr, 3), 0u);
    CHECK_EQ(embed_ringbuf_peek_bytes(&rb, 0, out, 0), 0u);
    CHECK(!embed_ringbuf_peek(nullptr, 0, &b));
}

TEST_CASE(skip_discards_at_most_stored) {
    uint8_t st[8], b = 0;
    embed_ringbuf_t rb;
    embed_ringbuf_init(&rb, st, 8);
    fill(rb, 1, 5);
    CHECK_EQ(embed_ringbuf_skip(&rb, 2), 2u);
    CHECK(embed_ringbuf_get(&rb, &b));
    CHECK_EQ(b, 3u);
    CHECK_EQ(embed_ringbuf_skip(&rb, 100), 2u);
    CHECK(embed_ringbuf_is_empty(&rb));
    CHECK_EQ(embed_ringbuf_skip(&rb, 1), 0u);
    CHECK_EQ(embed_ringbuf_skip(nullptr, 1), 0u);
    CHECK_EQ(embed_ringbuf_skip(&rb, 0), 0u);
}

TEST_CASE(find_locates_delimiter_across_wrap) {
    uint8_t st[8];
    embed_ringbuf_t rb;
    embed_ringbuf_init(&rb, st, 8);
    fill(rb, 0, 6);
    embed_ringbuf_skip(&rb, 6);
    const char line[] = "ab\ncd";
    embed_ringbuf_write(&rb, (const uint8_t *)line, 5); // 位置 6,7,0,1,2
    size_t off = 99;
    CHECK(embed_ringbuf_find(&rb, '\n', &off));
    CHECK_EQ(off, 2u);
    CHECK(embed_ringbuf_find(&rb, 'd', &off));
    CHECK_EQ(off, 4u);
    CHECK(!embed_ringbuf_find(&rb, 'z', &off));
    CHECK(!embed_ringbuf_find(&rb, 'a', nullptr));
    embed_ringbuf_skip(&rb, 5);
    CHECK(!embed_ringbuf_find(&rb, 'a', &off)); // 消費済みのデータは探さない
}

TEST_CASE(linear_write_spans_respect_reserved_slot) {
    uint8_t st[8];
    embed_ringbuf_t rb;
    embed_ringbuf_init(&rb, st, 8);
    uint8_t *w = nullptr;
    // 空・tail=0: 末尾 1 バイトは tail の直前なので使えない
    CHECK_EQ(embed_ringbuf_write_linear(&rb, &w), 7u);
    CHECK(w == st);
    std::memset(w, 0x5A, 3);
    CHECK(embed_ringbuf_write_commit(&rb, 3));
    CHECK_EQ(embed_ringbuf_available(&rb), 3u);
    CHECK_EQ(embed_ringbuf_write_linear(&rb, &w), 4u);
    CHECK(w == st + 3);
    CHECK(!embed_ringbuf_write_commit(&rb, 5)); // 連続空きを超える確定は拒否
    CHECK_EQ(embed_ringbuf_available(&rb), 3u);
    // tail を進めると、末尾まで全部 + 先頭側は次の呼び出しで得る
    embed_ringbuf_skip(&rb, 3); // tail = head = 3
    CHECK_EQ(embed_ringbuf_write_linear(&rb, &w), 5u); // 3..7
    CHECK(embed_ringbuf_write_commit(&rb, 5));          // head = 0 へ折り返し
    CHECK_EQ(rb.head, 0u);
    CHECK_EQ(embed_ringbuf_write_linear(&rb, &w), 2u); // 0,1 (2 は tail=3 の直前)
    CHECK(w == st);
    CHECK(embed_ringbuf_write_commit(&rb, 2));
    CHECK(embed_ringbuf_is_full(&rb));
    CHECK_EQ(embed_ringbuf_write_linear(&rb, &w), 0u);
    CHECK(embed_ringbuf_write_commit(&rb, 0)); // 0 バイトの確定は常に成功
    CHECK_EQ(embed_ringbuf_write_linear(nullptr, &w), 0u);
    CHECK_EQ(embed_ringbuf_write_linear(&rb, nullptr), 0u);
    CHECK(!embed_ringbuf_write_commit(nullptr, 1));
}

TEST_CASE(linear_read_spans_and_commit) {
    uint8_t st[8];
    embed_ringbuf_t rb;
    embed_ringbuf_init(&rb, st, 8);
    const uint8_t *r = nullptr;
    CHECK_EQ(embed_ringbuf_read_linear(&rb, &r), 0u);
    fill(rb, 0, 6);
    embed_ringbuf_skip(&rb, 5); // tail = 5, 中身は [5]
    fill(rb, 10, 3);            // 5 10 11 12 を位置 5,6,7,0 に
    CHECK_EQ(embed_ringbuf_read_linear(&rb, &r), 3u); // 位置 5..7
    CHECK(r == st + 5);
    CHECK_EQ(r[0], 5u);
    CHECK_EQ(r[2], 11u);
    CHECK(!embed_ringbuf_read_commit(&rb, 4));
    CHECK(embed_ringbuf_read_commit(&rb, 3));
    CHECK_EQ(rb.tail, 0u);
    CHECK_EQ(embed_ringbuf_read_linear(&rb, &r), 1u);
    CHECK_EQ(r[0], 12u);
    CHECK(embed_ringbuf_read_commit(&rb, 1));
    CHECK(embed_ringbuf_is_empty(&rb));
    CHECK_EQ(embed_ringbuf_read_linear(nullptr, &r), 0u);
    CHECK(!embed_ringbuf_read_commit(nullptr, 0));
}

TEST_CASE(dma_style_transfer_matches_model) {
    // DMA を模して、連続領域へまとめて書き・まとめて読む操作を乱数で繰り返す
    for (size_t cap : {size_t(2), size_t(3), size_t(7), size_t(16), size_t(31)}) {
        std::vector<uint8_t> st(cap);
        embed_ringbuf_t rb;
        embed_ringbuf_init(&rb, st.data(), cap);
        std::deque<uint8_t> model;
        uint32_t rng = 77u + (uint32_t)cap;
        uint8_t next_value = 0;
        auto rnd = [&rng]() {
            rng = rng * 1103515245u + 12345u;
            return rng >> 16;
        };
        for (int step = 0; step < 3000; ++step) {
            if (rnd() & 1) {
                uint8_t *w = nullptr;
                size_t span = embed_ringbuf_write_linear(&rb, &w);
                CHECK(span <= cap - 1 - model.size());
                size_t n = span ? rnd() % (span + 1) : 0;
                for (size_t i = 0; i < n; ++i) {
                    w[i] = next_value;
                    model.push_back(next_value++);
                }
                CHECK(embed_ringbuf_write_commit(&rb, n));
            } else {
                const uint8_t *r = nullptr;
                size_t span = embed_ringbuf_read_linear(&rb, &r);
                CHECK(span <= model.size());
                CHECK(span > 0 || model.empty());
                size_t n = span ? rnd() % (span + 1) : 0;
                for (size_t i = 0; i < n; ++i) {
                    CHECK_EQ(r[i], model.front());
                    model.pop_front();
                }
                CHECK(embed_ringbuf_read_commit(&rb, n));
            }
            CHECK_EQ(embed_ringbuf_available(&rb), model.size());
        }
    }
}

TEST_CASE(spsc_threads_preserve_order) {
    // ISR (生産者) とメインループ (消費者) を 2 スレッドで模擬する
    static uint8_t st[64];
    embed_ringbuf_t rb;
    embed_ringbuf_init(&rb, st, sizeof(st));
    constexpr uint32_t kTotal = 200000;
    std::atomic<bool> ok{true};
    std::thread producer([&rb] {
        uint32_t sent = 0;
        while (sent < kTotal) {
            uint8_t chunk[7];
            size_t n = 0;
            for (; n < sizeof(chunk) && sent + n < kTotal; ++n)
                chunk[n] = (uint8_t)((sent + n) * 31u);
            size_t w = embed_ringbuf_write(&rb, chunk, n);
            sent += (uint32_t)w;
            if (w == 0)
                std::this_thread::yield();
        }
    });
    uint32_t received = 0;
    while (received < kTotal) {
        uint8_t buf[13];
        size_t n = embed_ringbuf_read(&rb, buf, sizeof(buf));
        for (size_t i = 0; i < n; ++i)
            if (buf[i] != (uint8_t)((received + i) * 31u))
                ok = false;
        received += (uint32_t)n;
        if (n == 0)
            std::this_thread::yield();
    }
    producer.join();
    CHECK(ok.load());
    CHECK_EQ(received, kTotal);
    CHECK(embed_ringbuf_is_empty(&rb));
}

EMBED_TEST_MAIN()
