// 出力の書式・レベル境界・バッファ境界・ヘキサダンプ・バックエンド・ロックの順序を完全一致で固定するテスト。
#include "embed_test/embed_test.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "embed_log/embed_log.h"

namespace {
// 制御文字を見える形にして不一致を表示する文字列比較 (embed_test の CHECK_EQ は数値専用)
std::string show(const std::string &s) {
    std::string out;
    for (unsigned char c : s) {
        char b[8];
        if (c == '\n') {
            out += "\\n";
        } else if (c == 0x1b) {
            out += "\\e";
        } else if (c < 32 || c > 126) {
            std::snprintf(b, sizeof(b), "\\x%02X", c);
            out += b;
        } else {
            out += char(c);
        }
    }
    return out;
}
bool same(const std::string &a, const std::string &e, const char *file, int line) {
    const bool ok = embed_test::check(a == e, "string equality", file, line);
    if (!ok)
        std::printf("# expected \"%s\"\n# actual   \"%s\"\n", show(e).c_str(), show(a).c_str());
    return ok;
}
} // namespace
#define CHECK_STR(actual, expected) same((actual), (expected), __FILE__, __LINE__)

namespace {
std::vector<std::string> g_lines;
std::string g_events; // L = lock, U = unlock, B = バックエンド呼び出し
uint32_t g_time = 0;

uint32_t fake_time() { return g_time; }

void capture(const char *str, size_t len, void *) {
    CHECK_EQ(std::strlen(str), len); // 長さ引数はヌル終端までの長さ
    g_lines.emplace_back(str, len);
    g_events += 'B';
}

void lock_fn(void *) { g_events += 'L'; }
void unlock_fn(void *) { g_events += 'U'; }

// 色なし・DEBUG レベル・捕捉用バックエンドだけの状態にする
void reset(embed_log_level_t level = EMBED_LOG_LEVEL_DEBUG, bool color = false) {
    embed_log_init();
    embed_log_clear_backends();
    embed_log_add_backend(capture, nullptr);
    embed_log_set_color_enabled(color);
    embed_log_set_level(level);
    embed_log_set_timestamp_fn(fake_time);
    embed_log_set_lock(nullptr, nullptr, nullptr);
    g_lines.clear();
    g_events.clear();
    g_time = 0;
}
} // namespace

// ---- 書式 --------------------------------------------------------------------

TEST_CASE(line_format_is_exact) {
    reset();
    g_time = 42;
    embed_log_write(EMBED_LOG_LEVEL_INFO, "NET", "src/net.c", 7, "hello %d", 7);
    embed_log_write(EMBED_LOG_LEVEL_WARN, "NET", "src/net.c", 7, "careful");
    embed_log_write(EMBED_LOG_LEVEL_ERROR, "NET", "src/net.c", 7, "bad");
    embed_log_write(EMBED_LOG_LEVEL_FATAL, "NET", "src/net.c", 7, "dead");
    REQUIRE(g_lines.size() == 4);
    // タイムスタンプは 8 桁ゼロ詰め、レベル名は 5 桁に揃える。INFO 以上は場所を付けない
    CHECK_STR(g_lines[0], std::string("[00000042][INFO ][NET] hello 7\n"));
    CHECK_STR(g_lines[1], std::string("[00000042][WARN ][NET] careful\n"));
    CHECK_STR(g_lines[2], std::string("[00000042][ERROR][NET] bad\n"));
    CHECK_STR(g_lines[3], std::string("[00000042][FATAL][NET] dead\n"));
}

TEST_CASE(debug_and_trace_append_the_source_location) {
    reset(EMBED_LOG_LEVEL_TRACE);
    g_time = 1;
    embed_log_write(EMBED_LOG_LEVEL_DEBUG, "T", "src/dir/x.c", 12, "d");
    embed_log_write(EMBED_LOG_LEVEL_TRACE, "T", "C:\\work\\y.c", 345, "t");
    embed_log_write(EMBED_LOG_LEVEL_DEBUG, "T", "plain.c", 1, "p");
    embed_log_write(EMBED_LOG_LEVEL_DEBUG, "T", nullptr, 9, "n");
    embed_log_write(EMBED_LOG_LEVEL_INFO, "T", "src/dir/x.c", 12, "i"); // INFO には付けない
    REQUIRE(g_lines.size() == 5);
    CHECK_STR(g_lines[0], std::string("[00000001][DEBUG][T] d (x.c:12)\n")); // 区切りの後ろだけ
    CHECK_STR(g_lines[1], std::string("[00000001][TRACE][T] t (y.c:345)\n")); // Windows の区切りも
    CHECK_STR(g_lines[2], std::string("[00000001][DEBUG][T] p (plain.c:1)\n"));
    CHECK_STR(g_lines[3], std::string("[00000001][DEBUG][T] n (:9)\n"));
    CHECK_STR(g_lines[4], std::string("[00000001][INFO ][T] i\n"));
}

TEST_CASE(tag_and_timestamp_defaults) {
    reset();
    embed_log_write(EMBED_LOG_LEVEL_INFO, nullptr, "", 0, "no tag");
    CHECK_STR(g_lines.back(), std::string("[00000000][INFO ][SYS] no tag\n")); // タグ省略は SYS
    embed_log_set_timestamp_fn(nullptr);
    g_time = 99; // 時刻関数が無ければ 0
    embed_log_write(EMBED_LOG_LEVEL_INFO, "A", "", 0, "x");
    CHECK_STR(g_lines.back(), std::string("[00000000][INFO ][A] x\n"));
    embed_log_set_timestamp_fn(fake_time);
    g_time = 4294967295u; // 8 桁を超えても切らない
    embed_log_write(EMBED_LOG_LEVEL_INFO, "A", "", 0, "x");
    CHECK_STR(g_lines.back(), std::string("[4294967295][INFO ][A] x\n"));
    g_time = 12345678;
    embed_log_write(EMBED_LOG_LEVEL_INFO, "A", "", 0, "x");
    CHECK_STR(g_lines.back(), std::string("[12345678][INFO ][A] x\n"));
}

TEST_CASE(color_sequences_per_level) {
    reset(EMBED_LOG_LEVEL_TRACE, true);
    const struct {
        embed_log_level_t level;
        const char *color, *name;
    } cases[] = {{EMBED_LOG_LEVEL_FATAL, "\x1b[35m", "FATAL"}, {EMBED_LOG_LEVEL_ERROR, "\x1b[31m", "ERROR"},
                 {EMBED_LOG_LEVEL_WARN, "\x1b[33m", "WARN "},  {EMBED_LOG_LEVEL_INFO, "\x1b[32m", "INFO "},
                 {EMBED_LOG_LEVEL_DEBUG, "\x1b[36m", "DEBUG"}, {EMBED_LOG_LEVEL_TRACE, "\x1b[90m", "TRACE"}};
    for (const auto &c : cases) {
        g_lines.clear();
        embed_log_write(c.level, "C", "", 0, "m");
        REQUIRE(g_lines.size() == 1);
        // 先頭に色、末尾にリセット → 改行
        const std::string loc = (c.level >= EMBED_LOG_LEVEL_DEBUG) ? " (:0)" : "";
        CHECK_STR(g_lines[0], std::string(c.color) + "[00000000][" + c.name + "][C] m" + loc + "\x1b[0m\n");
    }
    embed_log_set_color_enabled(false); // 無効にすると一切付かない
    g_lines.clear();
    embed_log_write(EMBED_LOG_LEVEL_ERROR, "C", "", 0, "m");
    CHECK_STR(g_lines[0], std::string("[00000000][ERROR][C] m\n"));
}

TEST_CASE(init_defaults) {
    embed_log_init();
    CHECK_EQ(int(embed_log_get_level()), int(EMBED_LOG_LEVEL_DEBUG));
    embed_log_clear_backends();
    embed_log_add_backend(capture, nullptr);
    g_lines.clear();
    embed_log_write(EMBED_LOG_LEVEL_INFO, "D", "", 0, "m"); // 既定は色あり
    REQUIRE(g_lines.size() == 1);
    CHECK_STR(g_lines[0], std::string("\x1b[32m[00000000][INFO ][D] m\x1b[0m\n"));
}

// ---- レベル ------------------------------------------------------------------

TEST_CASE(level_threshold_is_inclusive_and_none_silences_everything) {
    for (int setting = EMBED_LOG_LEVEL_NONE; setting <= EMBED_LOG_LEVEL_TRACE; ++setting) {
        reset(embed_log_level_t(setting));
        CHECK_EQ(int(embed_log_get_level()), setting);
        for (int lv = EMBED_LOG_LEVEL_NONE; lv <= EMBED_LOG_LEVEL_TRACE; ++lv) {
            const size_t before = g_lines.size();
            embed_log_write(embed_log_level_t(lv), "L", "", 0, "m");
            // 設定レベルと同じレベルは出力、1つ詳細なレベルは抑止。NONE のメッセージは出さない
            const bool want = lv != EMBED_LOG_LEVEL_NONE && lv <= setting;
            CHECK_EQ(g_lines.size() - before, size_t(want ? 1 : 0));
        }
    }
    reset();
    embed_log_write(EMBED_LOG_LEVEL_INFO, "L", "", 0, nullptr); // 書式文字列が無ければ何もしない
    CHECK(g_lines.empty());
}

// ---- バッファ境界 ------------------------------------------------------------

TEST_CASE(overlong_lines_are_truncated_but_always_terminated) {
    static_assert(EMBED_LOG_MAX_LINE_LENGTH == 256, "境界の数値はバッファ 256 バイトを前提にしている");
    // 前置 "[00000000][INFO ][T] " は 21 文字。改行を含めて 255 文字までが 1 行に収まる(終端の NUL が 1 バイト)
    for (size_t m : {size_t(0), size_t(1), size_t(232), size_t(233), size_t(234), size_t(235), size_t(300),
                     size_t(5000)}) {
        reset();
        const std::string msg(m, 'x');
        embed_log_write(EMBED_LOG_LEVEL_INFO, "T", "", 0, "%s", msg.c_str());
        REQUIRE(g_lines.size() == 1);
        const std::string &l = g_lines[0];
        const size_t kept = m < 233 ? m : 233; // 本文は 233 文字まで
        CHECK_STR(l, std::string("[00000000][INFO ][T] ") + std::string(kept, 'x') + "\n");
        CHECK(l.size() <= EMBED_LOG_MAX_LINE_LENGTH - 1);
    }
}

TEST_CASE(overlong_colored_lines_keep_the_color_reset) {
    reset(EMBED_LOG_LEVEL_DEBUG, true);
    const std::string msg(1000, 'y');
    embed_log_write(EMBED_LOG_LEVEL_INFO, "T", "", 0, "%s", msg.c_str());
    REQUIRE(g_lines.size() == 1);
    const std::string &l = g_lines[0];
    CHECK(l.size() <= EMBED_LOG_MAX_LINE_LENGTH - 1);
    CHECK(l.compare(0, 5, "\x1b[32m") == 0);
    // 切り詰めても末尾のリセットと改行は必ず残る (端末に色が残らない)
    CHECK(l.size() > 5 && l.compare(l.size() - 5, 5, "\x1b[0m\n") == 0);
}

TEST_CASE(overlong_tag_is_truncated_without_overflow) {
    reset();
    const std::string tag(1000, 'g');
    embed_log_write(EMBED_LOG_LEVEL_INFO, tag.c_str(), "", 0, "msg");
    REQUIRE(g_lines.size() == 1);
    CHECK(g_lines[0].size() <= EMBED_LOG_MAX_LINE_LENGTH - 1);
    CHECK(g_lines[0].back() == '\n');
}

// ---- ヘキサダンプ ------------------------------------------------------------

namespace {
std::string hex_line(size_t offset, const std::string &bytes_hex, const std::string &ascii) {
    char head[16];
    std::snprintf(head, sizeof(head), "  %04zX: ", offset);
    std::string hex = bytes_hex;
    hex.resize(48, ' '); // 16 バイト分 (3 文字 x 16) に空白で桁揃え
    return std::string(head) + hex + " |" + ascii + "|\n";
}
} // namespace

TEST_CASE(hexdump_one_byte) {
    reset();
    const uint8_t b = 0x41;
    embed_log_hexdump(EMBED_LOG_LEVEL_DEBUG, "H", &b, 1);
    REQUIRE(g_lines.size() == 2);
    CHECK(g_lines[0].find("[H] --- HexDump (1 bytes) ---") != std::string::npos);
    CHECK_STR(g_lines[1], hex_line(0, "41 ", "A"));
}

TEST_CASE(hexdump_wraps_every_16_bytes_and_aligns_the_last_row) {
    reset();
    uint8_t data[17];
    for (int i = 0; i < 17; ++i)
        data[i] = uint8_t(0x30 + i);
    embed_log_hexdump(EMBED_LOG_LEVEL_DEBUG, "H", data, 16);
    REQUIRE(g_lines.size() == 2); // 見出し + 1 行 (16 バイトちょうどで折り返さない)
    CHECK_STR(g_lines[1], hex_line(0, "30 31 32 33 34 35 36 37 38 39 3A 3B 3C 3D 3E 3F ", "0123456789:;<=>?"));

    g_lines.clear();
    embed_log_hexdump(EMBED_LOG_LEVEL_DEBUG, "H", data, 17);
    REQUIRE(g_lines.size() == 3); // 見出し + 2 行
    CHECK(g_lines[0].find("--- HexDump (17 bytes) ---") != std::string::npos);
    CHECK_STR(g_lines[1], hex_line(0, "30 31 32 33 34 35 36 37 38 39 3A 3B 3C 3D 3E 3F ", "0123456789:;<=>?"));
    CHECK_STR(g_lines[2], hex_line(0x10, "40 ", "@")); // 端数行の桁揃え
}

TEST_CASE(hexdump_ascii_column_replaces_non_printables) {
    reset();
    const uint8_t data[] = {0x00, 0x1F, 0x20, 0x41, 0x7E, 0x7F, 0x80, 0xFF};
    embed_log_hexdump(EMBED_LOG_LEVEL_DEBUG, "H", data, sizeof(data));
    REQUIRE(g_lines.size() == 2);
    // 0x20 〜 0x7E だけがそのまま。それ以外は '.'
    CHECK_STR(g_lines[1], hex_line(0, "00 1F 20 41 7E 7F 80 FF ", ".. A~..."));
}

TEST_CASE(hexdump_offsets_and_line_count_for_larger_data) {
    reset();
    std::vector<uint8_t> data(300, 0xAB);
    embed_log_hexdump(EMBED_LOG_LEVEL_DEBUG, "H", data.data(), data.size());
    REQUIRE(g_lines.size() == 1 + 19); // 300 / 16 = 18.75 → 19 行
    CHECK(g_lines[1].compare(0, 9, "  0000: A") == 0);
    CHECK(g_lines[2].compare(0, 9, "  0010: A") == 0);
    CHECK(g_lines[19].compare(0, 9, "  0120: A") == 0);
    CHECK_STR(g_lines[19], hex_line(0x120, "AB AB AB AB AB AB AB AB AB AB AB AB ", "............")); // 端数 12 バイト
}

TEST_CASE(hexdump_respects_level_and_rejects_empty_input) {
    reset(EMBED_LOG_LEVEL_WARN);
    const uint8_t b[4] = {1, 2, 3, 4};
    embed_log_hexdump(EMBED_LOG_LEVEL_INFO, "H", b, 4); // 設定より詳細なレベルは出さない
    CHECK(g_lines.empty());
    embed_log_hexdump(EMBED_LOG_LEVEL_WARN, "H", b, 4); // 設定と同じレベルは出す
    CHECK_EQ(g_lines.size(), size_t(2));
    g_lines.clear();
    embed_log_hexdump(EMBED_LOG_LEVEL_NONE, "H", b, 4);
    embed_log_hexdump(EMBED_LOG_LEVEL_WARN, "H", nullptr, 4);
    embed_log_hexdump(EMBED_LOG_LEVEL_WARN, "H", b, 0);
    CHECK(g_lines.empty());
}

// ---- バックエンドとロック ----------------------------------------------------

namespace {
std::vector<int> g_order;
void backend_a(const char *, size_t, void *u) { g_order.push_back(*static_cast<int *>(u)); }
} // namespace

TEST_CASE(backends_are_dispatched_in_order_with_their_user_data_up_to_the_maximum) {
    embed_log_init(); // 既定の stdout バックエンドが 1 つ登録される
    embed_log_set_level(EMBED_LOG_LEVEL_DEBUG);
    CHECK(!embed_log_add_backend(nullptr, nullptr));
    int ids[4] = {1, 2, 3, 4};
    CHECK(embed_log_add_backend(backend_a, &ids[0]));
    CHECK(embed_log_add_backend(backend_a, &ids[1]));
    CHECK(embed_log_add_backend(backend_a, &ids[2]));
    CHECK(!embed_log_add_backend(backend_a, &ids[3])); // 既定 + 3 = 最大 4 で満杯
    g_order.clear();
    embed_log_set_color_enabled(false);
    // stdout にも 1 行出るが、ここでは登録順と user_data だけを確認する
    embed_log_write(EMBED_LOG_LEVEL_ERROR, "B", "", 0, "m");
    CHECK(g_order == (std::vector<int>{1, 2, 3}));

    embed_log_clear_backends();
    g_order.clear();
    embed_log_write(EMBED_LOG_LEVEL_ERROR, "B", "", 0, "m"); // 全解除後は誰にも届かない
    CHECK(g_order.empty());
    for (int i = 0; i < 4; ++i)
        CHECK(embed_log_add_backend(backend_a, &ids[i])); // 解除で枠が空く
    CHECK(!embed_log_add_backend(backend_a, &ids[0]));
    embed_log_write(EMBED_LOG_LEVEL_ERROR, "B", "", 0, "m");
    CHECK(g_order == (std::vector<int>{1, 2, 3, 4}));
}

TEST_CASE(lock_is_held_around_output_and_released_afterwards) {
    reset();
    embed_log_set_lock(lock_fn, unlock_fn, nullptr);
    embed_log_write(EMBED_LOG_LEVEL_INFO, "K", "", 0, "m");
    CHECK_STR(g_events, std::string("LBU")); // 取得 → 出力 → 解放
    g_events.clear();
    embed_log_write(EMBED_LOG_LEVEL_TRACE, "K", "", 0, "m"); // 抑止されたログではロックしない
    CHECK_STR(g_events, std::string(""));
    reset(EMBED_LOG_LEVEL_DEBUG);
    embed_log_set_lock(lock_fn, unlock_fn, nullptr);
    uint8_t d[40] = {};
    embed_log_hexdump(EMBED_LOG_LEVEL_DEBUG, "K", d, sizeof(d));
    // 見出し 1 行 (取得 → 出力 → 解放) のあと、本体の 3 行を 1 回のロックでまとめて出力
    CHECK_STR(g_events, std::string("LBU") + "LBBBU");
    g_events.clear();
    embed_log_add_backend(capture, nullptr);
    embed_log_clear_backends();
    CHECK_STR(g_events, std::string("LULU")); // 登録・解除も排他する
}

namespace {
int g_lock_marker = 0;
void* g_seen_lock_data = nullptr;
void* g_seen_unlock_data = nullptr;
void lock_data_fn(void *u) { g_seen_lock_data = u; }
void unlock_data_fn(void *u) { g_seen_unlock_data = u; }
} // namespace

TEST_CASE(lock_callbacks_receive_the_registered_user_data) {
    reset();
    embed_log_set_lock(lock_data_fn, unlock_data_fn, &g_lock_marker);
    embed_log_write(EMBED_LOG_LEVEL_INFO, "K", "", 0, "m");
    CHECK(g_seen_lock_data == &g_lock_marker);
    CHECK(g_seen_unlock_data == &g_lock_marker);
    embed_log_set_lock(nullptr, unlock_fn, nullptr); // 片方だけでも落ちない
    embed_log_write(EMBED_LOG_LEVEL_INFO, "K", "", 0, "m");
    embed_log_set_lock(lock_fn, nullptr, nullptr);
    embed_log_write(EMBED_LOG_LEVEL_INFO, "K", "", 0, "m");
    embed_log_set_lock(nullptr, nullptr, nullptr);
}

EMBED_TEST_MAIN()
