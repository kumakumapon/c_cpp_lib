// 端末に送られるバイト列ごと、行編集・履歴・補完・引数の分割・数値の解析を確かめる。
#include "embed_cli/embed_cli.h"
#include "embed_test/embed_test.hpp"
#include <climits>
#include <string>
#include <vector>

namespace {
struct Term {
    std::string out;
    std::vector<std::vector<std::string>> calls;
};
void term_write(const char *d, size_t n, void *u) { static_cast<Term *>(u)->out.append(d, n); }

int record(embed_cli_t *cli, int argc, char **argv) {
    auto *t = static_cast<Term *>(cli->app);
    std::vector<std::string> args(argv, argv + argc);
    t->calls.push_back(args);
    return argc;
}
int led(embed_cli_t *cli, int argc, char **argv) {
    bool on = false;
    if (argc != 2 || !embed_cli_parse_bool(argv[1], &on)) {
        embed_cli_print(cli, "usage: led <on|off>\n");
        return 1;
    }
    embed_cli_printf(cli, "led %s\n", on ? "ON" : "OFF");
    return 0;
}
const embed_cli_command_t kCommands[] = {
    {"led", "led <on|off>  switch the LED", led},
    {"set", "set <key> <value>", record},
    {"status", "show status", record},
    {"stats", "show statistics", record},
};

void setup(embed_cli_t &cli, Term &t) {
    embed_cli_init(&cli, kCommands, 4, term_write, &t, "> ");
    cli.app = &t;
}
} // namespace

TEST_CASE(execute_dispatches_and_reports_errors) {
    Term t;
    embed_cli_t cli;
    setup(cli, t);
    CHECK_EQ(embed_cli_execute(&cli, "set speed 1500"), 3);
    REQUIRE_EQ(t.calls.size(), 1u);
    CHECK(t.calls[0] == (std::vector<std::string>{"set", "speed", "1500"}));
    CHECK_EQ(embed_cli_execute(&cli, "led on"), 0);
    CHECK(t.out.find("led ON\r\n") != std::string::npos); // "\n" は "\r\n" で送る
    t.out.clear();
    CHECK_EQ(embed_cli_execute(&cli, "reboot"), EMBED_CLI_ERR_UNKNOWN);
    CHECK_STR_EQ(t.out.c_str(), "error: unknown command 'reboot' (type help)\r\n");
    CHECK_EQ(cli.last_status, EMBED_CLI_ERR_UNKNOWN);
    CHECK_EQ(embed_cli_execute(&cli, "   "), EMBED_CLI_EMPTY);
    t.out.clear();
    CHECK_EQ(embed_cli_execute(&cli, "set \"unterminated"), EMBED_CLI_ERR_SYNTAX);
    CHECK_STR_EQ(t.out.c_str(), "error: unterminated quote\r\n");
    std::string many = "set";
    for (int i = 0; i < EMBED_CLI_ARGV_MAX; ++i)
        many += " x";
    CHECK_EQ(embed_cli_execute(&cli, many.c_str()), EMBED_CLI_ERR_TOO_MANY);
    std::string longline(EMBED_CLI_LINE_MAX, 'a');
    CHECK_EQ(embed_cli_execute(&cli, longline.c_str()), EMBED_CLI_ERR_TOO_LONG);
}

TEST_CASE(builtin_help_lists_commands) {
    Term t;
    embed_cli_t cli;
    setup(cli, t);
    embed_cli_execute(&cli, "help");
    CHECK(t.out.find("help    show commands") != std::string::npos);
    CHECK(t.out.find("led     led <on|off>  switch the LED\r\n") != std::string::npos);
    CHECK(t.out.find("status  show status\r\n") != std::string::npos);
    t.out.clear();
    embed_cli_execute(&cli, "help set");
    CHECK_STR_EQ(t.out.c_str(), "set <key> <value>\r\n");
    t.out.clear();
    CHECK_EQ(embed_cli_execute(&cli, "help nope"), EMBED_CLI_ERR_UNKNOWN);
}

TEST_CASE(tokenizer_quotes_and_escapes) {
    char line[] = "  set   name \"Line A\" 'a \"b\"' x\\ y \"q\\\"t\" ''";
    char *argv[8];
    REQUIRE_EQ(embed_cli_tokenize(line, argv, 8), 7);
    CHECK_STR_EQ(argv[0], "set");
    CHECK_STR_EQ(argv[1], "name");
    CHECK_STR_EQ(argv[2], "Line A");
    CHECK_STR_EQ(argv[3], "a \"b\"");
    CHECK_STR_EQ(argv[4], "x y");
    CHECK_STR_EQ(argv[5], "q\"t");
    CHECK_STR_EQ(argv[6], ""); // 空の引用符は空の引数
    char two[] = "a b c";
    CHECK_EQ(embed_cli_tokenize(two, argv, 2), EMBED_CLI_ERR_TOO_MANY);
    char open[] = "a 'b";
    CHECK_EQ(embed_cli_tokenize(open, argv, 8), EMBED_CLI_ERR_SYNTAX);
    char empty[] = "";
    CHECK_EQ(embed_cli_tokenize(empty, argv, 8), 0);
    char single_backslash[] = "'a\\b'"; // シングルクォート内の \ はそのまま
    REQUIRE_EQ(embed_cli_tokenize(single_backslash, argv, 8), 1);
    CHECK_STR_EQ(argv[0], "a\\b");
}

TEST_CASE(line_editing_and_echo) {
    Term t;
    embed_cli_t cli;
    setup(cli, t);
    embed_cli_start(&cli);
    CHECK_STR_EQ(t.out.c_str(), "> ");
    t.out.clear();
    embed_cli_feed_str(&cli, "stx\batus\r\n"); // Backspace で x を消す、CR LF は 1 回
    REQUIRE_EQ(t.calls.size(), 1u);
    CHECK(t.calls[0][0] == "status");
    CHECK_STR_EQ(t.out.c_str(), "stx\b \batus\r\n> ");
    t.out.clear();
    embed_cli_feed_str(&cli, "led\x7f\x7f\x7f\x7f" "status\n"); // DEL、空行で余分な Backspace は無視
    CHECK_EQ(t.calls.size(), 2u);
    t.out.clear();
    embed_cli_feed_str(&cli, "garbage\x03"); // Ctrl-C で行を破棄
    CHECK(t.out.find("^C\r\n> ") != std::string::npos);
    embed_cli_feed_str(&cli, "\r");
    CHECK_EQ(t.calls.size(), 2u);
    embed_cli_feed_str(&cli, "abc\x15status\r"); // Ctrl-U で行を消去
    CHECK_EQ(t.calls.size(), 3u);
    // 制御文字は無視、エコーなし
    embed_cli_set_echo(&cli, false);
    t.out.clear();
    embed_cli_feed_str(&cli, "sta\x01tus\r");
    CHECK_EQ(t.calls.size(), 4u);
    CHECK_STR_EQ(t.out.c_str(), "> "); // エコーなしでもプロンプトは出す
}

TEST_CASE(overlong_line_is_rejected_not_truncated) {
    Term t;
    embed_cli_t cli;
    setup(cli, t);
    std::string s = "set ";
    s += std::string(EMBED_CLI_LINE_MAX, 'x');
    embed_cli_feed_str(&cli, s.c_str());
    CHECK(t.out.find('\a') != std::string::npos); // 入らない文字はベル
    t.out.clear();
    embed_cli_feed(&cli, '\r');
    CHECK(t.calls.empty()); // 切り詰めて実行しない
    CHECK(t.out.find("error: line too long") != std::string::npos);
    embed_cli_feed_str(&cli, "status\r"); // 次の行は正常
    CHECK_EQ(t.calls.size(), 1u);
}

TEST_CASE(history_with_arrow_keys) {
    Term t;
    embed_cli_t cli;
    setup(cli, t);
    embed_cli_feed_str(&cli, "set a 1\r");
    embed_cli_feed_str(&cli, "set b 2\r");
    embed_cli_feed_str(&cli, "set b 2\r"); // 直前と同じ行は履歴に積まない
    embed_cli_feed_str(&cli, "\x1b[A\r"); // ↑: 最新
    REQUIRE_EQ(t.calls.size(), 4u);
    CHECK(t.calls[3][1] == "b");
    embed_cli_feed_str(&cli, "\x1b[A\x1b[A\x1b[A\r"); // 古い方へ。最古で止まる
    CHECK(t.calls[4][1] == "a");
    // 呼び出した古い行を実行すると、それが最新として積まれる: 履歴は [a, b, a]
    embed_cli_feed_str(&cli, "\x1b[A\x1b[A\x1b[B\r"); // ↑ (a) ↑ (b) ↓ (a)
    CHECK(t.calls[5][1] == "a");
    embed_cli_feed_str(&cli, "\x1b[A\x1b[A\r"); // ↑ (a) ↑ (b)
    CHECK(t.calls[6][1] == "b");
    t.out.clear();
    embed_cli_feed_str(&cli, "\x1b[A\x1b[B"); // ↓ で編集中の空行に戻る
    CHECK(t.out.find("\r> set b 2\x1b[K") != std::string::npos);
    CHECK(t.out.find("\r> \x1b[K") != std::string::npos);
    embed_cli_feed(&cli, '\r');
    CHECK_EQ(t.calls.size(), 7u);
    embed_cli_feed_str(&cli, "\x1b[C\x1bOx"); // 未対応のシーケンスは捨てる
    embed_cli_feed(&cli, '\r');
    CHECK_EQ(t.calls.size(), 7u);
    // 履歴は最大 EMBED_CLI_HISTORY 行
    for (int i = 0; i < 6; ++i)
        embed_cli_feed_str(&cli, ("set k " + std::to_string(i) + "\r").c_str());
    for (int i = 0; i < 10; ++i)
        embed_cli_feed_str(&cli, "\x1b[A");
    embed_cli_feed(&cli, '\r');
    CHECK(t.calls.back()[2] == std::to_string(6 - EMBED_CLI_HISTORY));
}

TEST_CASE(tab_completion) {
    Term t;
    embed_cli_t cli;
    setup(cli, t);
    t.out.clear();
    embed_cli_feed_str(&cli, "l\t");
    CHECK(t.out.find("\r> led \x1b[K") != std::string::npos);
    embed_cli_feed_str(&cli, "off\r");
    CHECK(t.out.find("led OFF") != std::string::npos);
    t.out.clear();
    embed_cli_feed_str(&cli, "st\t"); // status と stats
    CHECK(t.out.find("status  stats  \r\n> st") != std::string::npos);
    embed_cli_feed_str(&cli, "ats\r");
    CHECK(t.calls.back()[0] == "stats");
    t.out.clear();
    embed_cli_feed_str(&cli, "h\t"); // 組み込みの help も補完する
    CHECK(t.out.find("> help ") != std::string::npos);
    embed_cli_feed(&cli, 0x03);
    t.out.clear();
    embed_cli_feed_str(&cli, "set x\t"); // 引数は補完しない
    CHECK_STR_EQ(t.out.c_str(), "set x");
}

TEST_CASE(parse_long_bases_and_ranges) {
    long v = 0;
    CHECK(embed_cli_parse_long("1500", 0, 3000, &v));
    CHECK_EQ(v, 1500);
    CHECK(embed_cli_parse_long("-42", -100, 100, &v));
    CHECK_EQ(v, -42);
    CHECK(embed_cli_parse_long("0x1F", 0, 255, &v));
    CHECK_EQ(v, 31);
    CHECK(embed_cli_parse_long("0b101", 0, 255, &v));
    CHECK_EQ(v, 5);
    CHECK(embed_cli_parse_long("010", 0, 255, &v)); // 8 進ではない
    CHECK_EQ(v, 10);
    CHECK(!embed_cli_parse_long("3001", 0, 3000, &v));
    CHECK(!embed_cli_parse_long("12a", 0, 3000, &v));
    CHECK(!embed_cli_parse_long("", 0, 3000, &v));
    CHECK(!embed_cli_parse_long("-", -10, 10, &v));
    CHECK(!embed_cli_parse_long("0x", 0, 10, &v));
    CHECK(!embed_cli_parse_long("0b2", 0, 10, &v));
    CHECK(!embed_cli_parse_long(" 5", 0, 10, &v));
    CHECK(embed_cli_parse_long("9223372036854775807", LONG_MIN, LONG_MAX, &v) || sizeof(long) == 4);
    CHECK(!embed_cli_parse_long("99999999999999999999", LONG_MIN, LONG_MAX, &v)); // 桁あふれ
    if (sizeof(long) == 8) {
        CHECK(embed_cli_parse_long("-9223372036854775808", LONG_MIN, LONG_MAX, &v));
        CHECK_EQ(v, LONG_MIN);
        CHECK(!embed_cli_parse_long("9223372036854775808", LONG_MIN, LONG_MAX, &v));
    }
    CHECK(!embed_cli_parse_long("1", 5, 0, &v)); // min > max
}

TEST_CASE(parse_double_and_bool) {
    double d = 0;
    CHECK(embed_cli_parse_double("21.5", 0, 100, &d));
    CHECK_EQ(d, 21.5);
    CHECK(embed_cli_parse_double("-1e-3", -1, 1, &d));
    CHECK_NEAR(d, -0.001, 1e-12);
    CHECK(!embed_cli_parse_double("100.1", 0, 100, &d));
    CHECK(!embed_cli_parse_double("1.5x", 0, 100, &d));
    CHECK(!embed_cli_parse_double("nan", -1e9, 1e9, &d));
    CHECK(!embed_cli_parse_double("inf", -1e308, 1e308, &d));
    CHECK(!embed_cli_parse_double("", 0, 1, &d));
    CHECK(!embed_cli_parse_double(" 1", 0, 2, &d));
    bool b = false;
    CHECK(embed_cli_parse_bool("ON", &b));
    CHECK(b);
    CHECK(embed_cli_parse_bool("no", &b));
    CHECK(!b);
    CHECK(embed_cli_parse_bool("1", &b));
    CHECK(b);
    CHECK(!embed_cli_parse_bool("onn", &b));
    CHECK(!embed_cli_parse_bool("", &b));
}

TEST_CASE(null_safety) {
    embed_cli_init(nullptr, nullptr, 0, nullptr, nullptr, nullptr);
    embed_cli_feed(nullptr, 'a');
    embed_cli_feed_str(nullptr, "a");
    embed_cli_print(nullptr, "a");
    CHECK_EQ(embed_cli_execute(nullptr, "a"), EMBED_CLI_ERR_SYNTAX);
    embed_cli_t cli;
    embed_cli_init(&cli, nullptr, 5, nullptr, nullptr, nullptr); // 出力先なしでも落ちない
    CHECK_EQ(embed_cli_execute(&cli, "x"), EMBED_CLI_ERR_UNKNOWN);
    embed_cli_feed_str(&cli, "help\r\t");
    char *argv[2];
    CHECK_EQ(embed_cli_tokenize(nullptr, argv, 2), EMBED_CLI_ERR_TOO_MANY);
}

EMBED_TEST_MAIN()
