// 容量 (2 / 3 / 非 2 の累乗 / 2 の累乗)・折り返し点・満杯と空の連続操作・複数バイト操作の境界を、
// std::deque のモデルと突き合わせて固定するテスト。
#include "embed_ringbuf/embed_ringbuf.h"
#include "embed_test/embed_test.hpp"
#include <algorithm>
#include <cstring>
#include <deque>
#include <vector>

namespace {
// 有効容量は capacity - 1 (満杯と空を区別するため 1 要素を空けておく)
size_t usable(size_t capacity) { return capacity - 1; }

void check_counts(const embed_ringbuf_t &rb, size_t capacity, size_t stored) {
    CHECK_EQ(embed_ringbuf_available(&rb), stored);
    CHECK_EQ(embed_ringbuf_free_space(&rb), usable(capacity) - stored);
}
} // namespace

TEST_CASE(init_validation) {
    uint8_t st[16];
    embed_ringbuf_t rb;
    embed_ringbuf_init(nullptr, st, 8); // 落ちない

    for (size_t cap : {size_t(0), size_t(1)}) { // 容量 2 未満は使えない
        embed_ringbuf_init(&rb, st, cap);
        uint8_t b = 0;
        CHECK(!embed_ringbuf_put(&rb, 1));
        CHECK(!embed_ringbuf_get(&rb, &b));
        CHECK_EQ(embed_ringbuf_available(&rb), 0u);
        CHECK_EQ(embed_ringbuf_free_space(&rb), 0u);
        CHECK_EQ(embed_ringbuf_write(&rb, st, 4), 0u);
    }
    embed_ringbuf_init(&rb, nullptr, 8); // ストレージなし: 容量も 0 になる
    CHECK_EQ(rb.capacity, 0u);
    CHECK(!embed_ringbuf_put(&rb, 1));
    CHECK_EQ(embed_ringbuf_available(&rb), 0u);
    CHECK_EQ(embed_ringbuf_free_space(&rb), 0u);

    // 再初期化で状態が戻る
    embed_ringbuf_init(&rb, st, 2);
    CHECK(embed_ringbuf_put(&rb, 9));
    embed_ringbuf_init(&rb, st, 2);
    CHECK_EQ(embed_ringbuf_available(&rb), 0u);
    CHECK_EQ(rb.head, 0u);
    CHECK_EQ(rb.tail, 0u);
    CHECK_EQ(rb.capacity, 2u);
}

TEST_CASE(null_arguments) {
    uint8_t st[8], out = 0;
    embed_ringbuf_t rb;
    embed_ringbuf_init(&rb, st, 8);
    CHECK(!embed_ringbuf_put(nullptr, 1));
    CHECK(!embed_ringbuf_get(nullptr, &out));
    CHECK(!embed_ringbuf_get(&rb, nullptr));
    CHECK_EQ(embed_ringbuf_write(nullptr, st, 2), 0u);
    CHECK_EQ(embed_ringbuf_write(&rb, nullptr, 2), 0u);
    CHECK_EQ(embed_ringbuf_write(&rb, st, 0), 0u);
    CHECK_EQ(embed_ringbuf_read(nullptr, st, 2), 0u);
    CHECK_EQ(embed_ringbuf_read(&rb, nullptr, 2), 0u);
    CHECK_EQ(embed_ringbuf_read(&rb, st, 0), 0u);
    CHECK_EQ(embed_ringbuf_available(nullptr), 0u);
    CHECK_EQ(embed_ringbuf_free_space(nullptr), 0u);
    embed_ringbuf_clear(nullptr);
    // 失敗した get は何も消費しない
    embed_ringbuf_put(&rb, 5);
    CHECK(!embed_ringbuf_get(&rb, nullptr));
    CHECK_EQ(embed_ringbuf_available(&rb), 1u);
}

TEST_CASE(capacity_two_holds_one_byte) {
    uint8_t st[2], out = 0;
    embed_ringbuf_t rb;
    embed_ringbuf_init(&rb, st, 2);
    for (int round = 0; round < 5; ++round) {
        check_counts(rb, 2, 0);
        CHECK(!embed_ringbuf_get(&rb, &out));
        CHECK(embed_ringbuf_put(&rb, (uint8_t)(10 + round)));
        check_counts(rb, 2, 1);
        CHECK(!embed_ringbuf_put(&rb, 99)); // 満杯
        CHECK(embed_ringbuf_get(&rb, &out));
        CHECK_EQ(out, 10 + round);
    }
}

TEST_CASE(fill_drain_wraps_for_every_capacity) {
    // 2 の累乗 (2, 4, 8, 16) と そうでない容量 (3, 5, 7, 9, 10, 17) で、複数周する
    for (size_t cap : {size_t(2), size_t(3), size_t(4), size_t(5), size_t(7), size_t(8), size_t(9),
                       size_t(10), size_t(16), size_t(17)}) {
        std::vector<uint8_t> st(cap + 2, 0xEE); // 範囲外書き込み検出用の余白
        embed_ringbuf_t rb;
        embed_ringbuf_init(&rb, st.data(), cap);
        uint8_t next = 0, expect = 0, out = 0;
        for (unsigned round = 0; round < 4 * (unsigned)cap; ++round) {
            // 周ごとに 0 .. usable 個ずつ出し入れして、読み書き位置をずらす
            const size_t n = round % (usable(cap) + 1);
            for (size_t i = 0; i < n; ++i) CHECK(embed_ringbuf_put(&rb, next++));
            check_counts(rb, cap, n);
            CHECK(rb.head < cap);
            CHECK(rb.tail < cap);
            if (n == usable(cap)) CHECK(!embed_ringbuf_put(&rb, 0xFF)); // 満杯
            for (size_t i = 0; i < n; ++i) {
                CHECK(embed_ringbuf_get(&rb, &out));
                CHECK_EQ(out, expect);
                ++expect;
            }
            CHECK(!embed_ringbuf_get(&rb, &out));
            check_counts(rb, cap, 0);
        }
        CHECK_EQ(st[cap], 0xEE);
        CHECK_EQ(st[cap + 1], 0xEE);
    }
}

TEST_CASE(head_and_tail_wrap_exactly_at_capacity) {
    for (size_t cap : {size_t(5), size_t(8)}) {
        uint8_t st[8], out = 0;
        embed_ringbuf_t rb;
        embed_ringbuf_init(&rb, st, cap);
        for (size_t i = 0; i < cap - 1; ++i) embed_ringbuf_put(&rb, 1);
        CHECK_EQ(rb.head, cap - 1); // 最後のスロットを指す
        for (size_t i = 0; i < cap - 1; ++i) embed_ringbuf_get(&rb, &out);
        CHECK_EQ(rb.tail, cap - 1);
        embed_ringbuf_put(&rb, 2); // 最後のスロットに書き、位置は 0 に戻る
        CHECK_EQ(rb.head, 0u);
        CHECK_EQ(st[cap - 1], 2);
        CHECK_EQ(embed_ringbuf_available(&rb), 1u); // head < tail の状態でも件数が正しい
        CHECK_EQ(embed_ringbuf_free_space(&rb), cap - 2);
        embed_ringbuf_get(&rb, &out);
        CHECK_EQ(rb.tail, 0u);
        CHECK_EQ(out, 2);
    }
}

TEST_CASE(multi_byte_write_read_limits) {
    uint8_t st[8];
    embed_ringbuf_t rb;
    embed_ringbuf_init(&rb, st, 8);
    const uint8_t data[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    uint8_t out[16];

    CHECK_EQ(embed_ringbuf_write(&rb, data, 10), 7u); // 入るだけ書く (有効容量 7)
    check_counts(rb, 8, 7);
    CHECK_EQ(embed_ringbuf_write(&rb, data, 1), 0u);
    CHECK_EQ(embed_ringbuf_read(&rb, out, 3), 3u); // 要求より少ない読み出し
    CHECK_EQ(out[0], 0);
    CHECK_EQ(out[2], 2);
    CHECK_EQ(embed_ringbuf_write(&rb, data + 7, 3), 3u); // 折り返しをまたいで書く
    check_counts(rb, 8, 7);
    std::memset(out, 0xAA, sizeof(out));
    CHECK_EQ(embed_ringbuf_read(&rb, out, 16), 7u); // 要求より多く残っていなければある分だけ
    const uint8_t expect[7] = {3, 4, 5, 6, 7, 8, 9};
    CHECK(std::memcmp(out, expect, 7) == 0);
    CHECK_EQ(out[7], 0xAA); // 余りには触らない
    CHECK_EQ(embed_ringbuf_read(&rb, out, 4), 0u);
    check_counts(rb, 8, 0);
}

TEST_CASE(clear_resets) {
    uint8_t st[8], out = 0;
    embed_ringbuf_t rb;
    embed_ringbuf_init(&rb, st, 8);
    for (int i = 0; i < 5; ++i) embed_ringbuf_put(&rb, (uint8_t)i);
    embed_ringbuf_get(&rb, &out);
    embed_ringbuf_clear(&rb);
    CHECK_EQ(rb.head, 0u);
    CHECK_EQ(rb.tail, 0u);
    check_counts(rb, 8, 0);
    CHECK(!embed_ringbuf_get(&rb, &out));
    CHECK(embed_ringbuf_put(&rb, 77));
    CHECK(embed_ringbuf_get(&rb, &out));
    CHECK_EQ(out, 77);
}

TEST_CASE(randomized_against_deque) {
    for (size_t cap : {size_t(2), size_t(3), size_t(8), size_t(11), size_t(32), size_t(33)}) {
        std::vector<uint8_t> st(cap);
        embed_ringbuf_t rb;
        embed_ringbuf_init(&rb, st.data(), cap);
        std::deque<uint8_t> model;
        uint32_t rng = 2024u + (uint32_t)cap;
        auto next = [&rng]() {
            rng = rng * 1664525u + 1013904223u;
            return rng >> 8;
        };
        for (int step = 0; step < 4000; ++step) {
            const unsigned op = next() % 6;
            if (op < 2) {
                const uint8_t v = (uint8_t)next();
                const bool ok = embed_ringbuf_put(&rb, v);
                CHECK(ok == (model.size() < usable(cap)));
                if (ok) model.push_back(v);
            } else if (op < 4) {
                uint8_t v = 0;
                const bool ok = embed_ringbuf_get(&rb, &v);
                CHECK(ok == !model.empty());
                if (ok) {
                    CHECK_EQ(v, model.front());
                    model.pop_front();
                }
            } else if (op == 4) {
                uint8_t buf[12];
                const size_t n = 1 + next() % 12;
                for (size_t i = 0; i < n; ++i) buf[i] = (uint8_t)next();
                const size_t w = embed_ringbuf_write(&rb, buf, n);
                CHECK_EQ(w, std::min(n, usable(cap) - model.size()));
                for (size_t i = 0; i < w; ++i) model.push_back(buf[i]);
            } else {
                uint8_t buf[12];
                const size_t n = 1 + next() % 12;
                const size_t r = embed_ringbuf_read(&rb, buf, n);
                CHECK_EQ(r, std::min(n, model.size()));
                for (size_t i = 0; i < r; ++i) {
                    CHECK_EQ(buf[i], model.front());
                    model.pop_front();
                }
            }
            CHECK_EQ(embed_ringbuf_available(&rb), model.size());
            CHECK_EQ(embed_ringbuf_free_space(&rb), usable(cap) - model.size());
        }
    }
}

EMBED_TEST_MAIN()
