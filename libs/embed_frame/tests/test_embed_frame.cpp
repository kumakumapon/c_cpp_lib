// COBS は Cheshire & Baker の論文 / Wikipedia の符号化例、SLIP は RFC 1055 の定義でバイト列を固定する。
#include "embed_frame/embed_frame.h"
#include "embed_test/embed_test.hpp"
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;
Bytes cobs_enc(const Bytes &in) {
    Bytes out(EMBED_COBS_MAX_ENCODED(in.size()));
    size_t n = 0;
    if (!CHECK(embed_cobs_encode(in.data(), in.size(), out.data(), out.size(), &n)))
        return {};
    out.resize(n);
    return out;
}
Bytes seq(unsigned from, unsigned to) { // from..to (両端含む)
    Bytes b;
    for (unsigned v = from; v <= to; ++v)
        b.push_back((uint8_t)v);
    return b;
}
Bytes cat(std::initializer_list<Bytes> parts) {
    Bytes b;
    for (const auto &p : parts)
        b.insert(b.end(), p.begin(), p.end());
    return b;
}
} // namespace

TEST_CASE(cobs_reference_examples) {
    struct Ex {
        Bytes in, out;
    };
    const Ex examples[] = {
        {{0x00}, {0x01, 0x01}},
        {{0x00, 0x00}, {0x01, 0x01, 0x01}},
        {{0x00, 0x11, 0x00}, {0x01, 0x02, 0x11, 0x01}},
        {{0x11, 0x22, 0x00, 0x33}, {0x03, 0x11, 0x22, 0x02, 0x33}},
        {{0x11, 0x22, 0x33, 0x44}, {0x05, 0x11, 0x22, 0x33, 0x44}},
        {{0x11, 0x00, 0x00, 0x00}, {0x02, 0x11, 0x01, 0x01, 0x01}},
        {seq(0x01, 0xFE), cat({{0xFF}, seq(0x01, 0xFE)})},                 // 254 バイト
        {seq(0x00, 0xFE), cat({{0x01, 0xFF}, seq(0x01, 0xFE)})},           // 先頭 0 + 254
        {seq(0x01, 0xFF), cat({{0xFF}, seq(0x01, 0xFE), {0x02, 0xFF}})},   // 255 バイト
        {cat({seq(0x02, 0xFF), {0x00}}), cat({{0xFF}, seq(0x02, 0xFF), {0x01, 0x01}})},
        {cat({seq(0x03, 0xFF), {0x00, 0x01}}), cat({{0xFE}, seq(0x03, 0xFF), {0x02, 0x01}})},
        {{}, {0x01}}, // 空のデータ
    };
    for (const auto &ex : examples) {
        Bytes enc = cobs_enc(ex.in);
        REQUIRE_EQ(enc.size(), ex.out.size());
        CHECK_MEM_EQ(enc.data(), ex.out.data(), enc.size());
        CHECK(enc.size() <= EMBED_COBS_MAX_ENCODED(ex.in.size()));
        Bytes dec(ex.in.size() + 1);
        size_t n = 99;
        REQUIRE(embed_cobs_decode(enc.data(), enc.size(), dec.data(), dec.size(), &n));
        REQUIRE_EQ(n, ex.in.size());
        if (n)
            CHECK_MEM_EQ(dec.data(), ex.in.data(), n);
    }
}

TEST_CASE(cobs_capacity_and_invalid_input) {
    const uint8_t in[] = {0x11, 0x22, 0x00, 0x33};
    uint8_t out[8];
    size_t n = 0;
    CHECK(!embed_cobs_encode(in, 4, out, 4, &n)); // 5 バイト必要
    CHECK(embed_cobs_encode(in, 4, out, 5, &n));
    CHECK(!embed_cobs_encode(in, 4, out, 0, &n));
    CHECK(!embed_cobs_encode(nullptr, 4, out, 8, &n));
    CHECK(!embed_cobs_encode(in, 4, nullptr, 8, &n));
    CHECK(embed_cobs_encode(nullptr, 0, out, 1, &n));
    CHECK_EQ(n, 1u);
    // 復号の不正入力
    const uint8_t zero_inside[] = {0x03, 0x11, 0x00};
    const uint8_t truncated[] = {0x05, 0x11, 0x22};
    const uint8_t code_zero[] = {0x00};
    uint8_t dec[8];
    CHECK(!embed_cobs_decode(zero_inside, 3, dec, 8, &n));
    CHECK(!embed_cobs_decode(truncated, 3, dec, 8, &n));
    CHECK(!embed_cobs_decode(code_zero, 1, dec, 8, &n));
    CHECK(!embed_cobs_decode(in, 0, dec, 8, &n)); // 空は COBS として不正
    const uint8_t enc[] = {0x03, 0x11, 0x22, 0x02, 0x33};
    CHECK(!embed_cobs_decode(enc, 5, dec, 3, &n)); // 復号先が足りない
    CHECK(embed_cobs_decode(enc, 5, dec, 4, &n));
    // その場復号
    uint8_t inplace[] = {0x03, 0x11, 0x22, 0x02, 0x33};
    REQUIRE(embed_cobs_decode(inplace, 5, inplace, 5, &n));
    const uint8_t expect[] = {0x11, 0x22, 0x00, 0x33};
    CHECK_MEM_EQ(inplace, expect, 4);
}

TEST_CASE(cobs_stream_decoder) {
    uint8_t buf[16];
    embed_cobs_decoder_t d;
    embed_cobs_decoder_init(&d, buf, sizeof(buf));
    // 0 0 [frame A] 0 [frame B] 0 : 先頭の区切りは同期用で無視
    const Bytes stream = {0x00, 0x00, 0x03, 0x11, 0x22, 0x02, 0x33, 0x00, 0x01, 0x01, 0x00};
    std::vector<Bytes> frames;
    for (uint8_t b : stream) {
        const uint8_t *f = nullptr;
        size_t n = 0;
        embed_frame_status st = embed_cobs_decoder_feed(&d, b, &f, &n);
        if (st == EMBED_FRAME_COMPLETE)
            frames.emplace_back(f, f + n);
        CHECK(st != EMBED_FRAME_ERROR);
    }
    REQUIRE_EQ(frames.size(), 2u);
    CHECK((frames[0] == Bytes{0x11, 0x22, 0x00, 0x33}));
    CHECK((frames[1] == Bytes{0x00}));
    CHECK_EQ(d.frames, 2u);
    // 長すぎるフレームは区切りまで捨ててエラー 1 回、その後は正常に戻る
    const uint8_t *f = nullptr;
    size_t n = 0;
    for (int i = 0; i < 20; ++i)
        CHECK_EQ(embed_cobs_decoder_feed(&d, 0x05, &f, &n), EMBED_FRAME_IN_PROGRESS);
    CHECK_EQ(embed_cobs_decoder_feed(&d, 0x00, &f, &n), EMBED_FRAME_ERROR);
    CHECK_EQ(d.errors, 1u);
    // 不正な符号化
    embed_cobs_decoder_feed(&d, 0x05, &f, &n);
    embed_cobs_decoder_feed(&d, 0x11, &f, &n);
    CHECK_EQ(embed_cobs_decoder_feed(&d, 0x00, &f, &n), EMBED_FRAME_ERROR);
    for (uint8_t b : {uint8_t(0x02), uint8_t(0x44)})
        embed_cobs_decoder_feed(&d, b, &f, &n);
    REQUIRE_EQ(embed_cobs_decoder_feed(&d, 0x00, &f, &n), EMBED_FRAME_COMPLETE);
    CHECK_EQ(n, 1u);
    CHECK_EQ(f[0], 0x44u);
    CHECK_EQ(embed_cobs_decoder_feed(nullptr, 0, &f, &n), EMBED_FRAME_ERROR);
}

TEST_CASE(cobs_random_round_trip_never_emits_zero) {
    uint32_t rng = 99;
    auto rnd = [&rng]() {
        rng = rng * 1664525u + 1013904223u;
        return rng >> 16;
    };
    for (int t = 0; t < 300; ++t) {
        Bytes in(rnd() % 700);
        const unsigned zero_rate = rnd() % 4; // 0 の多いデータ・少ないデータを両方
        for (auto &b : in)
            b = (rnd() % 8) < zero_rate ? 0 : (uint8_t)(1 + rnd() % 255);
        Bytes enc = cobs_enc(in);
        for (uint8_t b : enc)
            CHECK(b != 0);
        CHECK(enc.size() <= EMBED_COBS_MAX_ENCODED(in.size()));
        Bytes dec(in.size() + 1);
        size_t n = 0;
        REQUIRE(embed_cobs_decode(enc.data(), enc.size(), dec.data(), dec.size(), &n));
        dec.resize(n);
        CHECK(dec == in);
    }
}

TEST_CASE(slip_rfc1055_escaping) {
    const uint8_t in[] = {0x01, EMBED_SLIP_END, 0x02, EMBED_SLIP_ESC, 0x03};
    uint8_t out[EMBED_SLIP_MAX_ENCODED(5)];
    size_t n = 0;
    REQUIRE(embed_slip_encode(in, 5, out, sizeof(out), &n));
    const uint8_t expect[] = {0xC0, 0x01, 0xDB, 0xDC, 0x02, 0xDB, 0xDD, 0x03, 0xC0};
    REQUIRE_EQ(n, sizeof(expect));
    CHECK_MEM_EQ(out, expect, n);
    CHECK(!embed_slip_encode(in, 5, out, 8, &n)); // 9 バイト必要
    CHECK(embed_slip_encode(in, 5, out, 9, &n));
    CHECK(embed_slip_encode(nullptr, 0, out, 2, &n));
    CHECK_EQ(n, 2u);
    CHECK(!embed_slip_encode(nullptr, 1, out, 9, &n));
    // ストリーム復号: 前後の END、連続 END は空フレームとして無視
    uint8_t buf[8];
    embed_slip_decoder_t d;
    embed_slip_decoder_init(&d, buf, sizeof(buf));
    std::vector<Bytes> frames;
    const Bytes stream = cat({{0xC0, 0xC0}, Bytes(expect, expect + sizeof(expect)), {0xAA, 0xC0}});
    for (uint8_t b : stream) {
        const uint8_t *f = nullptr;
        size_t len = 0;
        if (embed_slip_decoder_feed(&d, b, &f, &len) == EMBED_FRAME_COMPLETE)
            frames.emplace_back(f, f + len);
    }
    REQUIRE_EQ(frames.size(), 2u);
    CHECK((frames[0] == Bytes(in, in + 5)));
    CHECK((frames[1] == Bytes{0xAA}));
}

TEST_CASE(slip_decoder_errors) {
    uint8_t buf[4];
    embed_slip_decoder_t d;
    embed_slip_decoder_init(&d, buf, sizeof(buf));
    const uint8_t *f = nullptr;
    size_t n = 0;
    // 未定義のエスケープ
    for (uint8_t b : {uint8_t(0x01), uint8_t(0xDB), uint8_t(0x41)})
        embed_slip_decoder_feed(&d, b, &f, &n);
    CHECK_EQ(embed_slip_decoder_feed(&d, 0xC0, &f, &n), EMBED_FRAME_ERROR);
    // ESC の直後に END
    embed_slip_decoder_feed(&d, 0xDB, &f, &n);
    CHECK_EQ(embed_slip_decoder_feed(&d, 0xC0, &f, &n), EMBED_FRAME_ERROR);
    // バッファ超過
    for (int i = 0; i < 6; ++i)
        embed_slip_decoder_feed(&d, 0x11, &f, &n);
    CHECK_EQ(embed_slip_decoder_feed(&d, 0xC0, &f, &n), EMBED_FRAME_ERROR);
    CHECK_EQ(d.errors, 3u);
    // 回復
    for (uint8_t b : {uint8_t(0xDB), uint8_t(0xDD), uint8_t(0x22)})
        embed_slip_decoder_feed(&d, b, &f, &n);
    REQUIRE_EQ(embed_slip_decoder_feed(&d, 0xC0, &f, &n), EMBED_FRAME_COMPLETE);
    REQUIRE_EQ(n, 2u);
    CHECK_EQ(f[0], 0xDBu);
    CHECK_EQ(f[1], 0x22u);
    embed_slip_decoder_reset(&d);
    CHECK_EQ(embed_slip_decoder_feed(nullptr, 0, &f, &n), EMBED_FRAME_ERROR);
}

TEST_CASE(slip_random_round_trip) {
    uint32_t rng = 5;
    for (int t = 0; t < 200; ++t) {
        Bytes in(1 + (t * 37) % 300);
        for (auto &b : in) {
            rng = rng * 1103515245u + 12345u;
            b = (rng >> 20) % 3 == 0 ? (uint8_t)(0xC0 + (rng >> 8) % 2 * 0x1B) : (uint8_t)(rng >> 24);
        }
        Bytes enc(EMBED_SLIP_MAX_ENCODED(in.size()));
        size_t n = 0;
        REQUIRE(embed_slip_encode(in.data(), in.size(), enc.data(), enc.size(), &n));
        for (size_t i = 1; i + 1 < n; ++i)
            CHECK(enc[i] != EMBED_SLIP_END); // 区切り以外に END は現れない
        std::vector<uint8_t> buf(in.size());
        embed_slip_decoder_t d;
        embed_slip_decoder_init(&d, buf.data(), buf.size());
        Bytes got;
        for (size_t i = 0; i < n; ++i) {
            const uint8_t *f = nullptr;
            size_t len = 0;
            if (embed_slip_decoder_feed(&d, enc[i], &f, &len) == EMBED_FRAME_COMPLETE)
                got.assign(f, f + len);
        }
        CHECK(got == in);
    }
}

EMBED_TEST_MAIN()
