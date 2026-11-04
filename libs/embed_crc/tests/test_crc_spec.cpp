// CRC RevEng カタログの check / residue と、データシート・RFC の実例で CRC を固定する。
// 期待値はすべてライブラリの外 (カタログ・規格書) から取った値。
#include "embed_crc/embed_crc.h"
#include "embed_test/embed_test.hpp"
#include <cstring>

namespace {
const uint8_t kCheck[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

struct Catalog {
    embed_crc_kind kind;
    const char *name;
    uint32_t check;   // CRC("123456789")
    uint32_t residue; // 正しい CRC を後置した列を処理した後の内部状態 (xor-out 前)
};
const Catalog kCatalog[] = {
    {EMBED_CRC8_J1850, "CRC-8/SAE-J1850", 0x4B, 0xC4},
    {EMBED_CRC16_CCITT, "CRC-16/IBM-3740", 0x29B1, 0x0000},
    {EMBED_CRC16_MODBUS, "CRC-16/MODBUS", 0x4B37, 0x0000},
    {EMBED_CRC16_XMODEM, "CRC-16/XMODEM", 0x31C3, 0x0000},
    {EMBED_CRC32_IEEE, "CRC-32/ISO-HDLC", 0xCBF43926u, 0xDEBB20E3u},
    {EMBED_CRC32C, "CRC-32/ISCSI", 0xE3069283u, 0xB798B438u},
    {EMBED_CRC8_SMBUS, "CRC-8/SMBUS", 0xF4, 0x00},
    {EMBED_CRC8_MAXIM, "CRC-8/MAXIM-DOW", 0xA1, 0x00},
    {EMBED_CRC8_NRSC5, "CRC-8/NRSC-5", 0xF7, 0x00},
    {EMBED_CRC16_KERMIT, "CRC-16/KERMIT", 0x2189, 0x0000},
    {EMBED_CRC16_X25, "CRC-16/IBM-SDLC", 0x906E, 0xF0B8},
    {EMBED_CRC32_MPEG2, "CRC-32/MPEG-2", 0x0376E6E7u, 0x00000000u},
};
} // namespace

TEST_CASE(catalog_check_values) {
    static_assert(sizeof(kCatalog) / sizeof(kCatalog[0]) == EMBED_CRC_KIND_COUNT);
    for (const auto &c : kCatalog) {
        if (!CHECK_EQ(embed_crc(c.kind, kCheck, sizeof(kCheck)), c.check))
            std::printf("# %s\n", c.name);
    }
}

TEST_CASE(catalog_residue_after_appended_crc) {
    for (const auto &c : kCatalog) {
        uint8_t frame[16];
        std::memcpy(frame, kCheck, sizeof(kCheck));
        size_t n = embed_crc_append(c.kind, c.check, frame + 9, sizeof(frame) - 9);
        embed_crc_params_t p{};
        REQUIRE(embed_crc_get_params(c.kind, &p));
        CHECK_EQ(n, (size_t)p.width / 8);
        uint32_t state = embed_crc_update(c.kind, embed_crc_init(c.kind), frame, 9 + n);
        if (!CHECK_EQ(state, c.residue))
            std::printf("# %s\n", c.name);
        CHECK(embed_crc_verify(c.kind, frame, 9 + n));
        // どのビットを反転しても検出する (単一ビット誤りは全 CRC で検出可能)
        for (size_t bit = 0; bit < (9 + n) * 8; ++bit) {
            frame[bit / 8] ^= (uint8_t)(1u << (bit % 8));
            CHECK(!embed_crc_verify(c.kind, frame, 9 + n));
            frame[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        }
    }
}

TEST_CASE(streaming_matches_one_shot_at_every_split) {
    for (const auto &c : kCatalog) {
        for (size_t split = 0; split <= sizeof(kCheck); ++split) {
            uint32_t s = embed_crc_init(c.kind);
            s = embed_crc_update(c.kind, s, kCheck, split);
            s = embed_crc_update(c.kind, s, kCheck + split, sizeof(kCheck) - split);
            CHECK_EQ(embed_crc_finish(c.kind, s), c.check);
        }
        // 1 バイトずつ
        uint32_t s = embed_crc_init(c.kind);
        for (uint8_t b : kCheck)
            s = embed_crc_update(c.kind, s, &b, 1);
        CHECK_EQ(embed_crc_finish(c.kind, s), c.check);
    }
}

TEST_CASE(empty_input_is_init_xor_out) {
    for (const auto &c : kCatalog) {
        embed_crc_params_t p{};
        REQUIRE(embed_crc_get_params(c.kind, &p));
        uint32_t mask = p.width == 32 ? 0xFFFFFFFFu : (1u << p.width) - 1u;
        CHECK_EQ(embed_crc(c.kind, nullptr, 0), (p.init ^ p.xorout) & mask);
    }
    CHECK_EQ(embed_crc(EMBED_CRC32_IEEE, "", 0), 0u);
    CHECK_EQ(embed_crc(EMBED_CRC16_X25, nullptr, 0), 0x0000u); // FFFF ^ FFFF
}

TEST_CASE(datasheet_and_rfc_examples) {
    // Sensirion SHT3x データシート: 0xBEEF -> 0x92
    const uint8_t sht[] = {0xBE, 0xEF};
    CHECK_EQ(embed_crc(EMBED_CRC8_NRSC5, sht, 2), 0x92u);
    // Maxim AN27: 1-Wire ROM 02 1C B8 01 00 00 00 -> A2
    const uint8_t rom[] = {0x02, 0x1C, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xA2};
    CHECK_EQ(embed_crc(EMBED_CRC8_MAXIM, rom, 7), 0xA2u);
    CHECK(embed_crc_verify(EMBED_CRC8_MAXIM, rom, 8));
    // Modbus RTU: 01 03 00 00 00 0A の CRC は C5 CD の順で送る
    const uint8_t mb[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD};
    CHECK_EQ(embed_crc(EMBED_CRC16_MODBUS, mb, 6), 0xCDC5u);
    CHECK(embed_crc_verify(EMBED_CRC16_MODBUS, mb, 8));
    uint8_t out[2];
    CHECK_EQ(embed_crc_append(EMBED_CRC16_MODBUS, 0xCDC5u, out, 2), 2u);
    CHECK_MEM_EQ(out, mb + 6, 2);
    // よく知られた CRC-32 の例
    const char *fox = "The quick brown fox jumps over the lazy dog";
    CHECK_EQ(embed_crc(EMBED_CRC32_IEEE, fox, std::strlen(fox)), 0x414FA339u);
    // RFC 3720 B.4 (iSCSI CRC32C)
    uint8_t buf[32];
    std::memset(buf, 0, sizeof(buf));
    CHECK_EQ(embed_crc(EMBED_CRC32C, buf, 32), 0x8A9136AAu);
    std::memset(buf, 0xFF, sizeof(buf));
    CHECK_EQ(embed_crc(EMBED_CRC32C, buf, 32), 0x62A8AB43u);
    for (int i = 0; i < 32; ++i)
        buf[i] = (uint8_t)i;
    CHECK_EQ(embed_crc(EMBED_CRC32C, buf, 32), 0x46DD794Eu);
    // 非反転 CRC はビッグエンディアンで後置する
    uint8_t be[4];
    CHECK_EQ(embed_crc_append(EMBED_CRC32_MPEG2, 0x0376E6E7u, be, 4), 4u);
    const uint8_t be_expected[] = {0x03, 0x76, 0xE6, 0xE7};
    CHECK_MEM_EQ(be, be_expected, 4);
}

TEST_CASE(custom_parameters_cover_odd_widths) {
    struct Custom {
        embed_crc_params_t p;
        uint32_t check;
    };
    const Custom cases[] = {
        {{16, true, 0x8005, 0x0000, 0x0000}, 0xBB3D},       // CRC-16/ARC
        {{5, true, 0x05, 0x1F, 0x1F}, 0x19},                // CRC-5/USB
        {{7, false, 0x09, 0x00, 0x00}, 0x75},               // CRC-7/MMC (SD カード)
        {{3, false, 0x03, 0x00, 0x07}, 0x4},                // CRC-3/GSM
        {{10, false, 0x233, 0x000, 0x000}, 0x199},          // CRC-10/ATM
        {{24, false, 0x864CFB, 0xB704CE, 0x000000}, 0x21CF02}, // CRC-24/OPENPGP
    };
    for (const auto &c : cases) {
        CHECK_EQ(embed_crc_params_compute(&c.p, kCheck, sizeof(kCheck)), c.check);
        uint32_t s = embed_crc_params_init(&c.p);
        s = embed_crc_params_update(&c.p, s, kCheck, 4);
        s = embed_crc_params_update(&c.p, s, kCheck + 4, 5);
        CHECK_EQ(embed_crc_params_finish(&c.p, s), c.check);
    }
    // カタログのパラメータを汎用実装に通しても同じ (テーブル版との一致も兼ねる)
    for (const auto &c : kCatalog) {
        embed_crc_params_t p{};
        REQUIRE(embed_crc_get_params(c.kind, &p));
        CHECK_EQ(embed_crc_params_compute(&p, kCheck, sizeof(kCheck)), c.check);
    }
}

TEST_CASE(table_and_generic_agree_on_all_byte_values) {
    uint8_t all[256];
    for (int i = 0; i < 256; ++i)
        all[i] = (uint8_t)(255 - i);
    for (const auto &c : kCatalog) {
        embed_crc_params_t p{};
        REQUIRE(embed_crc_get_params(c.kind, &p));
        CHECK_EQ(embed_crc(c.kind, all, sizeof(all)), embed_crc_params_compute(&p, all, sizeof(all)));
    }
}

TEST_CASE(invalid_arguments) {
    const auto bad = (embed_crc_kind)EMBED_CRC_KIND_COUNT;
    CHECK_EQ(embed_crc_init(bad), 0u);
    CHECK_EQ(embed_crc_update(bad, 0x1234u, kCheck, 9), 0x1234u);
    CHECK_EQ(embed_crc_finish(bad, 0x1234u), 0u);
    CHECK_EQ(embed_crc(bad, kCheck, 9), 0u);
    embed_crc_params_t p{};
    CHECK(!embed_crc_get_params(bad, &p));
    CHECK(!embed_crc_get_params(EMBED_CRC32_IEEE, nullptr));
    // NULL かつ非 0 長は状態を変えない
    CHECK_EQ(embed_crc_update(EMBED_CRC32_IEEE, 0xABCDu, nullptr, 3), 0xABCDu);
    // 幅 0 / 33 は不正
    embed_crc_params_t zero{0, false, 1, 0, 0}, wide{33, false, 1, 0, 0};
    CHECK_EQ(embed_crc_params_compute(&zero, kCheck, 9), 0u);
    CHECK_EQ(embed_crc_params_compute(&wide, kCheck, 9), 0u);
    CHECK_EQ(embed_crc_params_update(&wide, 7u, kCheck, 9), 7u);
    CHECK_EQ(embed_crc_params_compute(nullptr, kCheck, 9), 0u);
    // append / verify の境界
    uint8_t out[4];
    CHECK_EQ(embed_crc_append(EMBED_CRC32_IEEE, 0, out, 3), 0u);
    CHECK_EQ(embed_crc_append(EMBED_CRC32_IEEE, 0, out, 4), 4u);
    CHECK_EQ(embed_crc_append(EMBED_CRC32_IEEE, 0, nullptr, 4), 0u);
    CHECK_EQ(embed_crc_append(bad, 0, out, 4), 0u);
    CHECK(!embed_crc_verify(EMBED_CRC16_MODBUS, kCheck, 1)); // CRC 長未満
    CHECK(!embed_crc_verify(EMBED_CRC16_MODBUS, nullptr, 2));
    CHECK(!embed_crc_verify(bad, kCheck, 9));
    // CRC だけのフレーム: 空データの CRC と一致すれば真
    uint8_t only[2];
    embed_crc_append(EMBED_CRC16_MODBUS, embed_crc(EMBED_CRC16_MODBUS, nullptr, 0), only, 2);
    CHECK(embed_crc_verify(EMBED_CRC16_MODBUS, only, 2));
}

TEST_CASE(checksums) {
    // Adler-32 (RFC 1950): 初期値 1
    CHECK_EQ(embed_adler32(1, "Wikipedia", 9), 0x11E60398u);
    CHECK_EQ(embed_adler32(1, nullptr, 0), 1u);
    CHECK_EQ(embed_adler32(77u, nullptr, 5), 77u);
    // NMAX (5552) を跨ぐ長さ。期待値は zlib.adler32 で計算
    static uint8_t big[100000];
    for (size_t i = 0; i < sizeof(big); ++i)
        big[i] = (uint8_t)(i * 7 + 3);
    CHECK_EQ(embed_adler32(1, big, sizeof(big)), 0x2DFB940Fu);
    uint32_t a = 1;
    for (size_t off = 0; off < sizeof(big); off += 3333) {
        size_t n = sizeof(big) - off < 3333 ? sizeof(big) - off : 3333;
        a = embed_adler32(a, big + off, n);
    }
    CHECK_EQ(a, 0x2DFB940Fu);
    CHECK_EQ(embed_crc(EMBED_CRC32_IEEE, big, sizeof(big)), 0xF730CAA8u); // zlib.crc32
    // Fletcher-16: 初期値 0
    CHECK_EQ(embed_fletcher16(0, "abcde", 5), 0xC8F0u);
    CHECK_EQ(embed_fletcher16(0, "abcdef", 6), 0x2057u);
    CHECK_EQ(embed_fletcher16(0, "abcdefgh", 8), 0x0627u);
    CHECK_EQ(embed_fletcher16(embed_fletcher16(0, "abc", 3), "defgh", 5), 0x0627u);
    CHECK_EQ(embed_fletcher16(0x1234u, nullptr, 1), 0x1234u);
}

EMBED_TEST_MAIN()
