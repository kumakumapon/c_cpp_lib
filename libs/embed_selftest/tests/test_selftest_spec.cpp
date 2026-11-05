// メモリ診断が各故障モデルを実際に検出することを、故障を注入した仮想メモリで確かめる。
// あわせて ROM の分割検査・スタック水位・時計の ppm 判定・ウォッチドッグ窓の境界を固定する。
#include "embed_selftest/embed_selftest.h"
#include "embed_test/embed_test.hpp"
#include <vector>

namespace {
/* 故障を注入できる 32bit 語のメモリ */
struct FaultyMemory {
    enum Kind {
        none,
        stuck_at,        // cell の bit が value に固着
        transition_up,   // cell の bit が 0 -> 1 に遷移できない
        coupling_up,     // aggressor の bit が 0 -> 1 になると victim の bit を 1 にする
        alias,           // アドレス a へのアクセスが b の物理セルに行く (デコーダ故障)
        data_short,      // 全語で bit と bit+1 が短絡 (wired-OR)
        address_stuck_0, // アドレス線 bit が 0 に固着
        address_stuck_1, // アドレス線 bit が 1 に固着
    };
    std::vector<uint32_t> cells;
    Kind kind = none;
    size_t cell = 0, other = 0;
    unsigned bit = 0;
    bool value = false;

    explicit FaultyMemory(size_t n) : cells(n, 0x12345678u) {}
    size_t map(size_t i) const {
        if (kind == alias && i == cell)
            return other;
        if (kind == address_stuck_0)
            return i & ~((size_t)1 << bit);
        if (kind == address_stuck_1)
            return (i | ((size_t)1 << bit)) % cells.size();
        return i;
    }
    uint32_t read(size_t i) const {
        uint32_t v = cells[map(i)];
        if (kind == stuck_at && map(i) == cell)
            v = value ? (v | (1u << bit)) : (v & ~(1u << bit));
        return v;
    }
    void write(size_t i, uint32_t v) {
        size_t p = map(i);
        const uint32_t m = 1u << bit;
        if (kind == transition_up && p == cell && !(cells[p] & m))
            v &= ~m;
        if (kind == data_short && (v & (m | (m << 1))))
            v |= m | (m << 1);
        if (kind == coupling_up && p == cell && !(cells[p] & m) && (v & m))
            cells[other] |= m;
        cells[p] = v;
    }
    embed_selftest_mem_io io() {
        return {[](void *c, size_t i) { return static_cast<FaultyMemory *>(c)->read(i); },
                [](void *c, size_t i, uint32_t v) { static_cast<FaultyMemory *>(c)->write(i, v); },
                this};
    }
};

bool march_detects(FaultyMemory &m, size_t *where = nullptr) {
    auto io = m.io();
    size_t idx = 9999;
    bool ok = embed_selftest_march_c_io(&io, m.cells.size(), &idx);
    if (where)
        *where = idx;
    return !ok;
}
} // namespace

TEST_CASE(march_passes_fault_free_memory_and_leaves_zero) {
    FaultyMemory m(37); // 2 の累乗でない語数
    auto io = m.io();
    CHECK(embed_selftest_march_c_io(&io, m.cells.size(), nullptr));
    for (uint32_t v : m.cells)
        CHECK_EQ(v, 0u);
    // 内蔵 RAM アダプタ経由でも同じ
    volatile uint32_t ram[16];
    embed_selftest_mem_io rio{};
    embed_selftest_ram_io(&rio, ram);
    CHECK(embed_selftest_march_c_io(&rio, 16, nullptr));
    CHECK_EQ(ram[15], 0u);
}

TEST_CASE(march_detects_stuck_at_faults) {
    for (bool v : {false, true}) {
        for (unsigned bit : {0u, 17u, 31u}) {
            FaultyMemory m(32);
            m.kind = FaultyMemory::stuck_at;
            m.cell = 20;
            m.bit = bit;
            m.value = v;
            size_t where = 0;
            CHECK(march_detects(m, &where));
            CHECK_EQ(where, 20u);
        }
    }
}

TEST_CASE(march_detects_transition_and_coupling_faults) {
    FaultyMemory t(16);
    t.kind = FaultyMemory::transition_up;
    t.cell = 3;
    t.bit = 5;
    CHECK(march_detects(t));
    // 結合故障: 被害セルが加害セルより前でも後ろでも検出する (⇑ と ⇓ の両方があるため)
    for (auto [aggr, victim] : {std::pair<size_t, size_t>{2, 9}, {9, 2}}) {
        FaultyMemory c(16);
        c.kind = FaultyMemory::coupling_up;
        c.cell = aggr;
        c.other = victim;
        c.bit = 7;
        CHECK(march_detects(c));
    }
}

TEST_CASE(march_detects_address_decoder_faults) {
    FaultyMemory a(16);
    a.kind = FaultyMemory::alias;
    a.cell = 5;
    a.other = 11;
    CHECK(march_detects(a));
    FaultyMemory s(16);
    s.kind = FaultyMemory::address_stuck_0;
    s.bit = 2;
    CHECK(march_detects(s));
}

TEST_CASE(data_bus_walking_one_detects_shorted_lines) {
    FaultyMemory ok(4);
    auto io = ok.io();
    CHECK(embed_selftest_data_bus(&io, 2, nullptr));
    FaultyMemory m(4);
    m.kind = FaultyMemory::data_short;
    m.bit = 12;
    auto bad = m.io();
    uint32_t failed = 0;
    CHECK(!embed_selftest_data_bus(&bad, 1, &failed));
    CHECK_EQ(failed, 1u << 12); // bit 12 を書くと bit 13 も立つ
    // 同じ短絡は値が全 0 / 全 1 の March C- では見えない (データバス検査が要る理由)
    FaultyMemory m2(8);
    m2.kind = FaultyMemory::data_short;
    m2.bit = 12;
    CHECK(!march_detects(m2));
    // 固着したデータ線
    FaultyMemory st(4);
    st.kind = FaultyMemory::stuck_at;
    st.cell = 0;
    st.bit = 30;
    st.value = false;
    auto sio = st.io();
    CHECK(!embed_selftest_data_bus(&sio, 0, &failed));
    CHECK_EQ(failed, 1u << 30);
}

TEST_CASE(address_bus_detects_stuck_address_lines) {
    FaultyMemory ok(64);
    auto io = ok.io();
    CHECK(embed_selftest_address_bus(&io, 64, nullptr));
    CHECK(embed_selftest_address_bus(&io, 2, nullptr));
    for (unsigned bit : {0u, 3u, 5u}) {
        FaultyMemory lo(64);
        lo.kind = FaultyMemory::address_stuck_0;
        lo.bit = bit;
        auto l = lo.io();
        size_t where = 0;
        CHECK(!embed_selftest_address_bus(&l, 64, &where));
        CHECK_EQ(where, (size_t)1 << bit);
        FaultyMemory hi(64);
        hi.kind = FaultyMemory::address_stuck_1;
        hi.bit = bit;
        auto h = hi.io();
        CHECK(!embed_selftest_address_bus(&h, 64, &where));
    }
    CHECK(!embed_selftest_address_bus(&io, 1, nullptr)); // 2 語未満は不可
}

TEST_CASE(memory_io_argument_validation) {
    FaultyMemory m(4);
    auto io = m.io();
    embed_selftest_mem_io broken = io;
    broken.read = nullptr;
    CHECK(!embed_selftest_march_c_io(nullptr, 4, nullptr));
    CHECK(!embed_selftest_march_c_io(&broken, 4, nullptr));
    CHECK(!embed_selftest_march_c_io(&io, 0, nullptr));
    CHECK(!embed_selftest_data_bus(&broken, 0, nullptr));
    CHECK(!embed_selftest_address_bus(nullptr, 4, nullptr));
    embed_selftest_ram_io(nullptr, nullptr);
    CHECK(!embed_selftest_march_c(nullptr, 4));
    uint32_t one[1];
    CHECK(!embed_selftest_march_c(one, 0));
}

TEST_CASE(rom_incremental_check_matches_one_shot) {
    const char *rom = "123456789"; // CRC-32 = CBF43926
    embed_selftest_rom_ctx ctx;
    REQUIRE(embed_selftest_rom_begin(&ctx, rom, 9, 0xCBF43926u));
    CHECK_EQ(embed_selftest_rom_step(&ctx, 4), EMBED_SELFTEST_BUSY);
    CHECK_EQ(embed_selftest_rom_step(&ctx, 4), EMBED_SELFTEST_BUSY);
    CHECK_EQ(embed_selftest_rom_step(&ctx, 4), EMBED_SELFTEST_PASS); // 残り 1 バイト
    // 1 周したら先頭から繰り返す
    CHECK_EQ(embed_selftest_rom_step(&ctx, 9), EMBED_SELFTEST_PASS);
    CHECK_EQ(embed_selftest_rom_step(&ctx, 100), EMBED_SELFTEST_PASS);
    REQUIRE(embed_selftest_rom_begin(&ctx, rom, 9, 0xCBF43927u));
    embed_selftest_status st = EMBED_SELFTEST_BUSY;
    int steps = 0;
    while ((st = embed_selftest_rom_step(&ctx, 1)) == EMBED_SELFTEST_BUSY)
        ++steps;
    CHECK_EQ(steps, 8);
    CHECK_EQ(st, EMBED_SELFTEST_FAIL);
    CHECK_EQ(embed_selftest_rom_step(&ctx, 0), EMBED_SELFTEST_FAIL);
    CHECK_EQ(embed_selftest_rom_step(nullptr, 1), EMBED_SELFTEST_FAIL);
    CHECK(!embed_selftest_rom_begin(&ctx, nullptr, 9, 0));
    CHECK(!embed_selftest_rom_begin(&ctx, rom, 0, 0));
    CHECK(!embed_selftest_rom_begin(nullptr, rom, 9, 0));
    CHECK(!embed_selftest_rom(rom, 0, 0));
    CHECK(!embed_selftest_rom(nullptr, 9, 0xCBF43926u));
}

TEST_CASE(stack_watermark_edges) {
    uint8_t st[8];
    embed_selftest_stack_fill(st, 8, 0xCC);
    CHECK_EQ(embed_selftest_stack_unused(st, 8, 0xCC, true), 8u); // 全く使っていない
    CHECK_EQ(embed_selftest_stack_unused(st, 8, 0xCC, false), 8u);
    st[0] = 0;
    st[7] = 0;
    CHECK_EQ(embed_selftest_stack_unused(st, 8, 0xCC, true), 0u); // 下向き: 先頭から数える
    CHECK_EQ(embed_selftest_stack_unused(st, 8, 0xCC, false), 0u);
    st[0] = 0xCC;
    CHECK_EQ(embed_selftest_stack_unused(st, 8, 0xCC, true), 7u);
    CHECK_EQ(embed_selftest_stack_unused(st, 0, 0xCC, true), 0u);
    CHECK_EQ(embed_selftest_stack_unused(nullptr, 8, 0xCC, true), 0u);
    embed_selftest_stack_fill(nullptr, 8, 0); // 落ちない
}

TEST_CASE(clock_tolerance_boundaries) {
    // 1 MHz 期待 ±100 ppm = ±100 Hz
    CHECK(embed_selftest_clock(1000100, 1000000, 100));
    CHECK(!embed_selftest_clock(1000101, 1000000, 100));
    CHECK(embed_selftest_clock(999900, 1000000, 100));
    CHECK(!embed_selftest_clock(999899, 1000000, 100));
    CHECK(embed_selftest_clock(5, 5, 0));
    CHECK(!embed_selftest_clock(6, 5, 0));
    CHECK(!embed_selftest_clock(1, 0, 100));        // 期待値 0 は不可
    CHECK(!embed_selftest_clock(1, 1, 1000001));    // 100% 超の許容は不可
    CHECK(embed_selftest_clock(0, 1, 1000000));     // 100% ちょうど
    // 大きな値でも途中であふれない: 2^63 の 1 ppm
    const uint64_t big = 1ull << 63;
    CHECK(embed_selftest_clock(big + big / 1000000, big, 1));
    CHECK(!embed_selftest_clock(big + big / 1000000 + 1, big, 1));
}

namespace {
void feed_count(void *u) { ++*static_cast<unsigned *>(u); }
} // namespace

TEST_CASE(watchdog_window_edges) {
    unsigned feeds = 0;
    embed_watchdog w{};
    REQUIRE(embed_watchdog_init(&w, 100, 10, 50, feed_count, &feeds));
    CHECK(!embed_watchdog_poll(&w, 109, true)); // 早すぎる: 給餌せず待つ (故障ではない)
    CHECK(!w.fault);
    CHECK(embed_watchdog_poll(&w, 110, true)); // 最小間隔ちょうど
    CHECK(embed_watchdog_poll(&w, 160, true)); // 最大間隔ちょうど
    CHECK_EQ(feeds, 2u);
    CHECK(!embed_watchdog_poll(&w, 211, true)); // 51 > 50: 期限超過
    CHECK(w.fault);
    CHECK(!embed_watchdog_poll(&w, 220, true)); // 故障はラッチ
    REQUIRE(embed_watchdog_init(&w, 0, 0, 10, feed_count, &feeds));
    CHECK(!embed_watchdog_poll(&w, 5, false)); // healthy=false で故障
    CHECK(w.fault);
    REQUIRE(embed_watchdog_init(&w, 100, 0, 10, feed_count, &feeds));
    CHECK(!embed_watchdog_poll(&w, 99, true)); // 時計逆行で故障
    CHECK(w.fault);
    CHECK(!embed_watchdog_init(&w, 0, 11, 10, feed_count, &feeds)); // min > max
    CHECK(!embed_watchdog_init(&w, 0, 0, 0, feed_count, &feeds));   // max = 0
    CHECK(!embed_watchdog_init(&w, 0, 0, 10, nullptr, &feeds));
    CHECK(!embed_watchdog_init(nullptr, 0, 0, 10, feed_count, &feeds));
    CHECK(!embed_watchdog_poll(nullptr, 0, true));
}

EMBED_TEST_MAIN()
