// 1 ms ごとに生の入力を与え、イベントが「いつ」「何回」出るかを時間軸で確かめる。
#include "embed_input/embed_input.h"
#include "embed_test/embed_test.hpp"
#include <functional>
#include <utility>
#include <vector>

namespace {
using Events = std::vector<std::pair<uint32_t, uint8_t>>; // (時刻, イベント)

/* raw(t) を t = from .. to-1 で 1 ms ごとに与える */
Events run(embed_button_t &b, uint32_t from, uint32_t to, const std::function<bool(uint32_t)> &raw) {
    Events out;
    for (uint32_t t = from; t != to; ++t) {
        const uint8_t ev = embed_button_update(&b, raw(t), t);
        if (ev)
            out.emplace_back(t, ev);
    }
    return out;
}
embed_button_config_t cfg(uint16_t deb, uint16_t lng, uint16_t dbl, uint16_t rep) {
    embed_button_config_t c{};
    c.debounce_ms = deb;
    c.long_press_ms = lng;
    c.double_click_ms = dbl;
    c.repeat_ms = rep;
    return c;
}
bool between(uint32_t t, uint32_t a, uint32_t b) { return t >= a && t < b; }
} // namespace

TEST_CASE(bouncing_contact_yields_single_press) {
    embed_button_t b;
    const auto c = cfg(20, 0, 0, 0);
    embed_button_init(&b, &c, 0);
    // 100〜110 ms でチャタリング (2 ms ごとに反転)、その後 300 ms まで押しっぱなし
    auto ev = run(b, 0, 400, [](uint32_t t) {
        if (between(t, 100, 110))
            return ((t - 100) / 2) % 2 == 0;
        return between(t, 110, 300);
    });
    REQUIRE_EQ(ev.size(), 2u);
    CHECK_EQ(ev[0].first, 128u); // 最後の変化 (108 ms で押下側へ) から 20 ms
    CHECK_EQ(ev[0].second, (uint8_t)EMBED_BUTTON_PRESSED);
    CHECK_EQ(ev[1].first, 320u);
    CHECK_EQ(ev[1].second, (uint8_t)(EMBED_BUTTON_RELEASED | EMBED_BUTTON_CLICK)); // ダブルクリックなし: 即 CLICK
}

TEST_CASE(single_click_waits_for_double_click_window) {
    embed_button_t b;
    embed_button_init(&b, nullptr, 0); // 既定: 20 / 800 / 300 / 0
    auto ev = run(b, 0, 1000, [](uint32_t t) { return between(t, 100, 200); });
    REQUIRE_EQ(ev.size(), 3u);
    CHECK_EQ(ev[0].first, 120u);
    CHECK_EQ(ev[1].first, 220u);
    CHECK_EQ(ev[1].second, (uint8_t)EMBED_BUTTON_RELEASED);
    CHECK_EQ(ev[2].first, 520u); // 解放確定から 300 ms
    CHECK_EQ(ev[2].second, (uint8_t)EMBED_BUTTON_CLICK);
}

TEST_CASE(double_click) {
    embed_button_t b;
    embed_button_init(&b, nullptr, 0);
    auto ev = run(b, 0, 1200, [](uint32_t t) { return between(t, 100, 180) || between(t, 300, 380); });
    uint8_t all = 0;
    unsigned doubles = 0, clicks = 0;
    for (auto &e : ev) {
        all |= e.second;
        doubles += (e.second & EMBED_BUTTON_DOUBLE_CLICK) ? 1 : 0;
        clicks += (e.second & EMBED_BUTTON_CLICK) ? 1 : 0;
    }
    CHECK_EQ(doubles, 1u);
    CHECK_EQ(clicks, 0u);
    // 2 回目の解放確定 (400) で DOUBLE_CLICK
    bool found = false;
    for (auto &e : ev)
        if (e.second & EMBED_BUTTON_DOUBLE_CLICK) {
            CHECK_EQ(e.first, 400u);
            found = true;
        }
    CHECK(found);
    // 間隔が窓を超えたら 2 回のクリック
    embed_button_init(&b, nullptr, 0);
    ev = run(b, 0, 1500, [](uint32_t t) { return between(t, 100, 180) || between(t, 600, 680); });
    clicks = 0;
    for (auto &e : ev)
        clicks += (e.second & EMBED_BUTTON_CLICK) ? 1 : 0;
    CHECK_EQ(clicks, 2u);
}

TEST_CASE(long_press_and_repeat) {
    embed_button_t b;
    const auto c = cfg(10, 500, 300, 100);
    embed_button_init(&b, &c, 0);
    auto ev = run(b, 0, 1200, [](uint32_t t) { return between(t, 100, 900); });
    // 押下確定 110、長押し 610、リピート 710, 810、解放確定 910 (クリックなし)
    std::vector<std::pair<uint32_t, uint8_t>> expect = {
        {110, EMBED_BUTTON_PRESSED}, {610, EMBED_BUTTON_LONG_PRESS}, {710, EMBED_BUTTON_REPEAT},
        {810, EMBED_BUTTON_REPEAT},  {910, EMBED_BUTTON_RELEASED}};
    REQUIRE_EQ(ev.size(), expect.size());
    for (size_t i = 0; i < ev.size(); ++i) {
        CHECK_EQ(ev[i].first, expect[i].first);
        CHECK_EQ(ev[i].second, expect[i].second);
    }
    CHECK(!embed_button_is_pressed(&b));
    CHECK_EQ(embed_button_held_ms(&b, 2000), 0u);
}

TEST_CASE(click_followed_by_long_press_reports_both) {
    embed_button_t b;
    const auto c = cfg(10, 400, 300, 0);
    embed_button_init(&b, &c, 0);
    auto ev = run(b, 0, 1500, [](uint32_t t) { return between(t, 100, 150) || between(t, 250, 1000); });
    // 2 回目の押下が長押しになった時点で、1 回目をクリックとして確定する
    bool long_with_click = false;
    unsigned clicks = 0, doubles = 0;
    for (auto &e : ev) {
        if ((e.second & EMBED_BUTTON_LONG_PRESS) && (e.second & EMBED_BUTTON_CLICK))
            long_with_click = true;
        clicks += (e.second & EMBED_BUTTON_CLICK) ? 1 : 0;
        doubles += (e.second & EMBED_BUTTON_DOUBLE_CLICK) ? 1 : 0;
    }
    CHECK(long_with_click);
    CHECK_EQ(clicks, 1u);
    CHECK_EQ(doubles, 0u);
}

TEST_CASE(held_time_and_clock_wrap) {
    embed_button_t b;
    const auto c = cfg(5, 100, 0, 0);
    const uint32_t start = 0xFFFFFFF0u;
    embed_button_init(&b, &c, start);
    Events ev = run(b, start, start + 200, [start](uint32_t t) { return (uint32_t)(t - start) >= 10; });
    REQUIRE_EQ(ev.size(), 2u);
    CHECK_EQ(ev[0].first, start + 15);
    CHECK_EQ(ev[1].first, start + 115); // 周回をまたいで長押し
    CHECK_EQ(ev[1].second, (uint8_t)EMBED_BUTTON_LONG_PRESS);
    CHECK(embed_button_is_pressed(&b));
    CHECK_EQ(embed_button_held_ms(&b, start + 200), 185u);
    CHECK_EQ(embed_button_update(nullptr, true, 0), 0u);
}

TEST_CASE(zero_debounce_is_immediate) {
    embed_button_t b;
    const auto c = cfg(0, 0, 0, 0);
    embed_button_init(&b, &c, 0);
    CHECK_EQ(embed_button_update(&b, true, 5), (uint8_t)EMBED_BUTTON_PRESSED);
    CHECK_EQ(embed_button_update(&b, false, 6), (uint8_t)(EMBED_BUTTON_RELEASED | EMBED_BUTTON_CLICK));
}

TEST_CASE(multi_input_debounce) {
    embed_debounce_t d;
    embed_debounce_init(&d, 3, 0x0000000Fu);
    // bit4 は 3 回連続で 1 になった時点で確定、bit0 の 2 回だけの 0 (グリッチ) は無視
    CHECK_EQ(embed_debounce_update(&d, 0x0000001Eu), 0u);
    CHECK_EQ(embed_debounce_update(&d, 0x0000001Eu), 0u);
    const uint32_t changed = embed_debounce_update(&d, 0x0000001Fu); // bit4: 3 回目、bit0 は戻った
    CHECK_EQ(changed, 0x10u);
    CHECK_EQ(d.count[0], 0u); // bit0 のカウントはやり直し
    CHECK_EQ(d.state, 0x1Fu);
    CHECK_EQ(embed_debounce_rising(&d, changed), 0x10u);
    CHECK_EQ(embed_debounce_falling(&d, changed), 0u);
    for (int i = 0; i < 2; ++i)
        CHECK_EQ(embed_debounce_update(&d, 0x80000000u | 0x1Eu), 0u);
    const uint32_t c2 = embed_debounce_update(&d, 0x80000000u | 0x1Eu);
    CHECK_EQ(c2, 0x80000001u); // bit31 立ち上がり、bit0 立ち下がり
    CHECK_EQ(embed_debounce_rising(&d, c2), 0x80000000u);
    CHECK_EQ(embed_debounce_falling(&d, c2), 0x1u);
    embed_debounce_t one;
    embed_debounce_init(&one, 0, 0); // 0 は 1 として扱う
    CHECK_EQ(embed_debounce_update(&one, 5u), 5u);
    CHECK_EQ(embed_debounce_update(nullptr, 1u), 0u);
}

EMBED_TEST_MAIN()
