// フラッシュのシミュレータ (書き込みは 1 -> 0 のみ、書き込み単位の検査、途中での電源断) を使い、
// 保存中のあらゆる時点で電源が切れても「前の値」か「新しい値」に必ず戻ることを確かめる。
#include "embed_nvparam/embed_nvparam.h"
#include "embed_test/embed_test.hpp"
#include <cstring>
#include <vector>

namespace {
struct SimFlash {
    std::vector<uint8_t> mem;
    size_t align = 1;
    long budget = -1;      // 残りのバイト操作数 (-1: 無制限)。0 になったら電源断
    bool misaligned = false;
    bool stuck_bit = false; // 書いた値の bit0 が 1 のまま残る故障
    unsigned erases = 0;

    bool step() {
        if (budget == 0)
            return false;
        if (budget > 0)
            --budget;
        return true;
    }
    static bool read(void *ctx, uint32_t addr, void *dst, size_t len) {
        auto *f = static_cast<SimFlash *>(ctx);
        if (addr + len > f->mem.size())
            return false;
        std::memcpy(dst, f->mem.data() + addr, len);
        return true;
    }
    static bool write(void *ctx, uint32_t addr, const void *src, size_t len) {
        auto *f = static_cast<SimFlash *>(ctx);
        if (addr % f->align || len % f->align)
            f->misaligned = true;
        const auto *s = static_cast<const uint8_t *>(src);
        for (size_t i = 0; i < len; ++i) {
            if (!f->step())
                return false;
            uint8_t v = s[i];
            if (f->stuck_bit)
                v |= 1u;
            f->mem[addr + i] &= v; // フラッシュは消去 (0xFF) からビットを落とすことしかできない
        }
        return true;
    }
    static bool erase(void *ctx, uint32_t addr, size_t len) {
        auto *f = static_cast<SimFlash *>(ctx);
        ++f->erases;
        for (size_t i = 0; i < len; ++i) {
            if (!f->step())
                return false; // 消去の途中で電源断: 残りは古い内容のまま
            f->mem[addr + i] = 0xFF;
        }
        return true;
    }
};

struct Params {
    uint32_t magic_number;
    float gain;
    int16_t offset;
    char name[7]; // 13 バイトの構造体ではないが、長さは書き込み単位の倍数でない
};

embed_nvparam_flash_t geometry(SimFlash &sim, size_t bank, size_t align) {
    sim.mem.assign(2 * bank + 64, 0x00); // 未消去の領域 (0x00) から始める
    sim.align = align;
    embed_nvparam_flash_t f{};
    f.read = SimFlash::read;
    f.write = SimFlash::write;
    f.erase = SimFlash::erase;
    f.ctx = &sim;
    f.bank_addr[0] = 0;
    f.bank_addr[1] = (uint32_t)(bank + 32); // 隙間を空けて配置
    f.bank_size = bank;
    f.write_align = align;
    return f;
}
Params make(uint32_t n) {
    Params p{};
    p.magic_number = n;
    p.gain = 1.5f * (float)n;
    p.offset = (int16_t)(-(int)n);
    std::snprintf(p.name, sizeof(p.name), "cfg%u", (unsigned)(n % 1000));
    return p;
}
bool same(const Params &a, const Params &b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }
} // namespace

TEST_CASE(first_boot_is_empty_and_data_untouched) {
    SimFlash sim;
    auto f = geometry(sim, 256, 8);
    embed_nvparam_t nv;
    REQUIRE(embed_nvparam_init(&nv, &f, 1));
    Params p = make(7), before = p;
    CHECK_EQ(embed_nvparam_load(&nv, &p, sizeof(p)), EMBED_NVPARAM_EMPTY);
    CHECK(same(p, before));
    CHECK_EQ(nv.active, -1);
    CHECK_EQ(embed_nvparam_capacity(&nv), 256u - 16u);
}

TEST_CASE(save_load_alternates_banks) {
    SimFlash sim;
    auto f = geometry(sim, 256, 8);
    embed_nvparam_t nv;
    REQUIRE(embed_nvparam_init(&nv, &f, 3));
    for (uint32_t i = 1; i <= 5; ++i) {
        Params p = make(i);
        REQUIRE_EQ(embed_nvparam_save(&nv, &p, sizeof(p)), EMBED_NVPARAM_OK);
        CHECK_EQ(nv.active, (int)((i - 1) % 2)); // 0, 1, 0, 1, ...
        CHECK_EQ(nv.seq, i);
        // 再起動して読む
        embed_nvparam_t again;
        REQUIRE(embed_nvparam_init(&again, &f, 3));
        Params q{};
        REQUIRE_EQ(embed_nvparam_load(&again, &q, sizeof(q)), EMBED_NVPARAM_OK);
        CHECK(same(p, q));
    }
    CHECK(!sim.misaligned);
    CHECK_EQ(nv.saves, 5u);
}

TEST_CASE(power_loss_at_every_byte_operation) {
    for (size_t align : {size_t(1), size_t(8), size_t(32)}) {
        SimFlash sim;
        auto f = geometry(sim, 128, align);
        embed_nvparam_t nv;
        REQUIRE(embed_nvparam_init(&nv, &f, 1));
        // 両方のバンクに記録がある状態 (次の保存は古い方を消して書く) を作る
        Params a = make(100), b = make(200), c = make(300);
        REQUIRE_EQ(embed_nvparam_save(&nv, &a, sizeof(a)), EMBED_NVPARAM_OK);
        REQUIRE_EQ(embed_nvparam_save(&nv, &b, sizeof(b)), EMBED_NVPARAM_OK);
        const std::vector<uint8_t> image = sim.mem;
        // 電源断なしで保存に要するバイト操作数を数える
        long total = 0;
        {
            SimFlash probe = sim;
            embed_nvparam_flash_t pf = f;
            pf.ctx = &probe;
            probe.budget = 1L << 30;
            embed_nvparam_t pn;
            REQUIRE(embed_nvparam_init(&pn, &pf, 1));
            Params tmp{};
            REQUIRE_EQ(embed_nvparam_load(&pn, &tmp, sizeof(tmp)), EMBED_NVPARAM_OK);
            REQUIRE_EQ(embed_nvparam_save(&pn, &c, sizeof(c)), EMBED_NVPARAM_OK);
            total = (1L << 30) - probe.budget;
        }
        REQUIRE(total > 0);
        unsigned old_count = 0, new_count = 0;
        long first_new = -1;
        for (long cut = 0; cut <= total; ++cut) {
            sim.mem = image;
            sim.budget = cut;
            embed_nvparam_t run;
            REQUIRE(embed_nvparam_init(&run, &f, 1));
            Params cur{};
            REQUIRE_EQ(embed_nvparam_load(&run, &cur, sizeof(cur)), EMBED_NVPARAM_OK);
            const embed_nvparam_status st = embed_nvparam_save(&run, &c, sizeof(c));
            CHECK_EQ(st == EMBED_NVPARAM_OK, cut >= total);
            // 再起動
            sim.budget = -1;
            embed_nvparam_t boot;
            REQUIRE(embed_nvparam_init(&boot, &f, 1));
            Params got{};
            if (!CHECK_EQ(embed_nvparam_load(&boot, &got, sizeof(got)), EMBED_NVPARAM_OK)) {
                std::printf("# align %zu, cut at %ld of %ld\n", align, cut, total);
                continue;
            }
            if (same(got, b)) {
                ++old_count;
                CHECK(first_new < 0); // 一度新しい値に切り替わったら、古い値には戻らない
            } else if (same(got, c)) {
                ++new_count;
                if (first_new < 0)
                    first_new = cut;
            } else {
                CHECK(!"recovered data is neither the previous nor the new record");
            }
        }
        // 切り替わるのはヘッダ (先頭 16 バイト) を書き終えた時点。書き込み単位が 16 を超えると、
        // 残りの 0xFF の埋め草を書く途中で切れても記録は完成している (保存の戻り値は IO になる)
        CHECK_EQ(old_count + new_count, (unsigned)total + 1u);
        CHECK_EQ((size_t)new_count, align > 16 ? align - 16 + 1 : 1u);
        CHECK(!sim.misaligned);
    }
}

TEST_CASE(corrupted_newest_record_falls_back_to_previous) {
    SimFlash sim;
    auto f = geometry(sim, 128, 4);
    embed_nvparam_t nv;
    REQUIRE(embed_nvparam_init(&nv, &f, 1));
    Params a = make(1), b = make(2);
    REQUIRE_EQ(embed_nvparam_save(&nv, &a, sizeof(a)), EMBED_NVPARAM_OK);
    REQUIRE_EQ(embed_nvparam_save(&nv, &b, sizeof(b)), EMBED_NVPARAM_OK);
    sim.mem[f.bank_addr[1] + 16 + 3] ^= 0x10; // 最新 (バンク 1) のデータを 1 ビット化けさせる
    embed_nvparam_t boot;
    REQUIRE(embed_nvparam_init(&boot, &f, 1));
    Params got{};
    REQUIRE_EQ(embed_nvparam_load(&boot, &got, sizeof(got)), EMBED_NVPARAM_OK);
    CHECK(same(got, a));
    CHECK_EQ(boot.active, 0);
    // 次の保存は壊れたバンク 1 を消して書く
    Params c = make(3);
    REQUIRE_EQ(embed_nvparam_save(&boot, &c, sizeof(c)), EMBED_NVPARAM_OK);
    CHECK_EQ(boot.active, 1);
    // 両方壊れたら EMPTY
    sim.mem[f.bank_addr[0] + 1] ^= 0xFF;
    sim.mem[f.bank_addr[1] + 12] ^= 0x01; // CRC フィールド
    embed_nvparam_t dead;
    REQUIRE(embed_nvparam_init(&dead, &f, 1));
    CHECK_EQ(embed_nvparam_load(&dead, &got, sizeof(got)), EMBED_NVPARAM_EMPTY);
}

TEST_CASE(version_and_size_mismatch_and_migration) {
    SimFlash sim;
    auto f = geometry(sim, 128, 8);
    embed_nvparam_t v1;
    REQUIRE(embed_nvparam_init(&v1, &f, 1));
    const uint8_t old_layout[5] = {1, 2, 3, 4, 5};
    REQUIRE_EQ(embed_nvparam_save(&v1, old_layout, sizeof(old_layout)), EMBED_NVPARAM_OK);
    embed_nvparam_t v2;
    REQUIRE(embed_nvparam_init(&v2, &f, 2));
    Params p = make(9), before = p;
    CHECK_EQ(embed_nvparam_load(&v2, &p, sizeof(p)), EMBED_NVPARAM_VERSION);
    CHECK(same(p, before));
    uint16_t version = 0;
    size_t len = 0;
    REQUIRE_EQ(embed_nvparam_peek(&v2, &version, &len), EMBED_NVPARAM_OK);
    CHECK_EQ(version, 1u);
    CHECK_EQ(len, 5u);
    uint8_t raw[8] = {};
    CHECK_EQ(embed_nvparam_load_raw(&v2, raw, 4, &len), EMBED_NVPARAM_SIZE);
    REQUIRE_EQ(embed_nvparam_load_raw(&v2, raw, sizeof(raw), &len), EMBED_NVPARAM_OK);
    CHECK_MEM_EQ(raw, old_layout, 5);
    // 移行して新しい版で保存
    REQUIRE_EQ(embed_nvparam_save(&v2, &p, sizeof(p)), EMBED_NVPARAM_OK);
    embed_nvparam_t boot;
    REQUIRE(embed_nvparam_init(&boot, &f, 2));
    Params got{};
    REQUIRE_EQ(embed_nvparam_load(&boot, &got, sizeof(got)), EMBED_NVPARAM_OK);
    CHECK(same(got, p));
    uint8_t shorter[4];
    CHECK_EQ(embed_nvparam_load(&boot, shorter, sizeof(shorter)), EMBED_NVPARAM_SIZE);
}

TEST_CASE(sequence_number_wraps) {
    SimFlash sim;
    auto f = geometry(sim, 128, 4);
    embed_nvparam_t nv;
    REQUIRE(embed_nvparam_init(&nv, &f, 1));
    Params a = make(1), b = make(2), c = make(3);
    REQUIRE_EQ(embed_nvparam_save(&nv, &a, sizeof(a)), EMBED_NVPARAM_OK);
    nv.seq = 0xFFFFFFFEu; // 長年使った後の状態を作る
    REQUIRE_EQ(embed_nvparam_save(&nv, &b, sizeof(b)), EMBED_NVPARAM_OK); // seq 0xFFFFFFFF
    REQUIRE_EQ(embed_nvparam_save(&nv, &c, sizeof(c)), EMBED_NVPARAM_OK); // seq 0 (周回)
    CHECK_EQ(nv.seq, 0u);
    embed_nvparam_t boot;
    REQUIRE(embed_nvparam_init(&boot, &f, 1));
    Params got{};
    REQUIRE_EQ(embed_nvparam_load(&boot, &got, sizeof(got)), EMBED_NVPARAM_OK);
    CHECK(same(got, c)); // 0 は 0xFFFFFFFF より新しい
}

TEST_CASE(verify_detects_bad_flash_and_keeps_old_record) {
    SimFlash sim;
    auto f = geometry(sim, 128, 1);
    embed_nvparam_t nv;
    REQUIRE(embed_nvparam_init(&nv, &f, 1));
    Params a = make(10), b = make(20);
    REQUIRE_EQ(embed_nvparam_save(&nv, &a, sizeof(a)), EMBED_NVPARAM_OK);
    sim.stuck_bit = true; // 書き込みは成功を返すが値が正しく残らない
    CHECK_EQ(embed_nvparam_save(&nv, &b, sizeof(b)), EMBED_NVPARAM_IO);
    CHECK_EQ(nv.active, 0);
    sim.stuck_bit = false;
    embed_nvparam_t boot;
    REQUIRE(embed_nvparam_init(&boot, &f, 1));
    Params got{};
    REQUIRE_EQ(embed_nvparam_load(&boot, &got, sizeof(got)), EMBED_NVPARAM_OK);
    CHECK(same(got, a));
}

TEST_CASE(limits_and_validation) {
    SimFlash sim;
    auto f = geometry(sim, 64, 16);
    embed_nvparam_t nv;
    REQUIRE(embed_nvparam_init(&nv, &f, 1));
    CHECK_EQ(embed_nvparam_capacity(&nv), 48u);
    uint8_t big[49] = {};
    CHECK_EQ(embed_nvparam_save(&nv, big, 49), EMBED_NVPARAM_INVALID);
    REQUIRE_EQ(embed_nvparam_save(&nv, big, 48), EMBED_NVPARAM_OK); // ちょうど入る
    REQUIRE_EQ(embed_nvparam_save(&nv, nullptr, 0), EMBED_NVPARAM_OK); // 長さ 0 も保存できる
    embed_nvparam_t boot;
    REQUIRE(embed_nvparam_init(&boot, &f, 1));
    CHECK_EQ(embed_nvparam_load(&boot, nullptr, 0), EMBED_NVPARAM_OK);
    REQUIRE_EQ(embed_nvparam_erase(&boot), EMBED_NVPARAM_OK);
    CHECK_EQ(embed_nvparam_load(&boot, nullptr, 0), EMBED_NVPARAM_EMPTY);
    // 構成の検査
    embed_nvparam_flash_t bad = f;
    bad.write_align = 3;
    CHECK(!embed_nvparam_init(&nv, &bad, 1));
    bad = f;
    bad.bank_addr[1] = 10; // 重なる
    CHECK(!embed_nvparam_init(&nv, &bad, 1));
    bad = f;
    bad.bank_size = 16; // ヘッダ + 1 単位が入らない
    CHECK(!embed_nvparam_init(&nv, &bad, 1));
    bad = f;
    bad.erase = nullptr;
    CHECK(!embed_nvparam_init(&nv, &bad, 1));
    CHECK(!embed_nvparam_init(nullptr, &f, 1));
    CHECK_EQ(embed_nvparam_save(nullptr, big, 1), EMBED_NVPARAM_INVALID);
    CHECK_EQ(embed_nvparam_load(&boot, nullptr, 4), EMBED_NVPARAM_INVALID);
    CHECK_EQ(embed_nvparam_capacity(nullptr), 0u);
}

EMBED_TEST_MAIN()
