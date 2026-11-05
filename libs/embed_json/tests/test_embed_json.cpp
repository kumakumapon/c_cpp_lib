// 書き出しは完成した文字列そのもので、解析は RFC 8259 の文法 (受理・拒否の両方) で確かめる。
#include "embed_json/embed_json.h"
#include "embed_test/embed_test.hpp"
#include <cmath>
#include <cstring>
#include <string>

namespace {
int parse(const char *js, embed_json_token_t *t, size_t n) { return embed_json_parse(js, std::strlen(js), t, n); }
} // namespace

TEST_CASE(writer_builds_nested_document) {
    char buf[256];
    embed_json_writer_t w;
    embed_json_writer_init(&w, buf, sizeof(buf));
    embed_json_object_begin(&w);
    embed_json_kv_string(&w, "device", "pump-01");
    embed_json_kv_int(&w, "seq", -42);
    embed_json_kv_fixed(&w, "temp", 23.456, 1);
    embed_json_kv_bool(&w, "running", true);
    embed_json_key(&w, "alarms");
    embed_json_array_begin(&w);
    embed_json_uint(&w, 18446744073709551615ull);
    embed_json_null(&w);
    embed_json_object_begin(&w);
    embed_json_object_end(&w);
    embed_json_array_begin(&w);
    embed_json_array_end(&w);
    embed_json_array_end(&w);
    embed_json_kv_double(&w, "ratio", 0.1);
    embed_json_object_end(&w);
    size_t len = 0;
    const char *out = embed_json_finish(&w, &len);
    REQUIRE(out != nullptr);
    CHECK_STR_EQ(out, "{\"device\":\"pump-01\",\"seq\":-42,\"temp\":23.5,\"running\":true,"
                      "\"alarms\":[18446744073709551615,null,{},[]],\"ratio\":0.1}");
    CHECK_EQ(len, std::strlen(out));
}

TEST_CASE(writer_escapes_strings) {
    char buf[128];
    embed_json_writer_t w;
    embed_json_writer_init(&w, buf, sizeof(buf));
    embed_json_array_begin(&w);
    embed_json_string(&w, "a\"b\\c/\b\f\n\r\t\x01\x1f");
    embed_json_string(&w, "日本");    // UTF-8 はそのまま
    embed_json_string_n(&w, "x\0y", 3); // NUL を含む長さ指定
    embed_json_string(&w, nullptr);   // NULL は null
    embed_json_array_end(&w);
    const char *out = embed_json_finish(&w, nullptr);
    REQUIRE(out != nullptr);
    CHECK_STR_EQ(out, "[\"a\\\"b\\\\c/\\b\\f\\n\\r\\t\\u0001\\u001f\",\"日本\",\"x\\u0000y\",null]");
}

TEST_CASE(writer_numbers) {
    char buf[160];
    embed_json_writer_t w;
    embed_json_writer_init(&w, buf, sizeof(buf));
    embed_json_array_begin(&w);
    embed_json_double(&w, NAN);
    embed_json_double(&w, INFINITY);
    embed_json_fixed(&w, -0.004, 2); // -0.00 は 0.00
    embed_json_fixed(&w, 2.5, 0);
    embed_json_fixed(&w, 1.0, 12);   // 桁数は 9 まで
    embed_json_fixed(&w, 1e20, 2);   // 大きすぎる値は有効数字表記
    embed_json_double(&w, 1e-7);
    embed_json_int(&w, INT64_MIN);
    embed_json_array_end(&w);
    const char *out = embed_json_finish(&w, nullptr);
    REQUIRE(out != nullptr);
    CHECK_STR_EQ(out, "[null,null,0.00,2,1.000000000,1e+20,1e-07,-9223372036854775808]");
    // 出力した数値はすべて JSON として解析できる
    embed_json_token_t t[16];
    CHECK_EQ(parse(out, t, 16), 9);
}

TEST_CASE(writer_rejects_misuse_and_overflow) {
    char buf[64];
    embed_json_writer_t w;
    // オブジェクトの中でキーのない値
    embed_json_writer_init(&w, buf, sizeof(buf));
    embed_json_object_begin(&w);
    CHECK(!embed_json_int(&w, 1));
    CHECK(embed_json_finish(&w, nullptr) == nullptr);
    // 値のないキーで閉じる
    embed_json_writer_init(&w, buf, sizeof(buf));
    embed_json_object_begin(&w);
    embed_json_key(&w, "a");
    CHECK(!embed_json_object_end(&w));
    // 種類の違う閉じ方
    embed_json_writer_init(&w, buf, sizeof(buf));
    embed_json_array_begin(&w);
    CHECK(!embed_json_object_end(&w));
    // 配列の中でキー
    embed_json_writer_init(&w, buf, sizeof(buf));
    embed_json_array_begin(&w);
    CHECK(!embed_json_key(&w, "a"));
    // 閉じ忘れ・ルートの値が 2 つ・空
    embed_json_writer_init(&w, buf, sizeof(buf));
    embed_json_array_begin(&w);
    CHECK(embed_json_finish(&w, nullptr) == nullptr);
    embed_json_writer_init(&w, buf, sizeof(buf));
    embed_json_int(&w, 1);
    CHECK(!embed_json_int(&w, 2));
    embed_json_writer_init(&w, buf, sizeof(buf));
    CHECK(embed_json_finish(&w, nullptr) == nullptr);
    // 容量不足: 途中までの JSON を返さない
    char small[8];
    embed_json_writer_init(&w, small, sizeof(small));
    embed_json_array_begin(&w);
    embed_json_string(&w, "toolong");
    embed_json_array_end(&w);
    CHECK(embed_json_finish(&w, nullptr) == nullptr);
    // ちょうど入る ("[1]" + NUL = 4)
    char exact[4];
    embed_json_writer_init(&w, exact, sizeof(exact));
    embed_json_array_begin(&w);
    embed_json_int(&w, 1);
    embed_json_array_end(&w);
    REQUIRE(embed_json_finish(&w, nullptr) != nullptr);
    CHECK_STR_EQ(exact, "[1]");
    // 深さの上限
    char deep[128];
    embed_json_writer_init(&w, deep, sizeof(deep));
    for (int i = 0; i < EMBED_JSON_MAX_DEPTH; ++i)
        CHECK(embed_json_array_begin(&w));
    CHECK(!embed_json_array_begin(&w));
    embed_json_writer_init(&w, nullptr, 10);
    CHECK(!embed_json_null(&w));
}

TEST_CASE(parser_tokens_and_lookup) {
    const char *js = R"( {"id": "pump-01", "setpoint": 21.5, "enabled": false,
                          "limits": {"min": -10, "max": 1e2}, "tags": ["a", "b\"c", "\u00e9\ud83d\ude00"],
                          "none": null} )";
    embed_json_token_t t[32];
    const int n = parse(js, t, 32);
    REQUIRE(n > 0);
    CHECK_EQ(t[0].type, EMBED_JSON_OBJECT);
    CHECK_EQ(t[0].size, 6u);
    CHECK_EQ(t[0].parent, -1);
    const int id = embed_json_object_get(js, t, n, 0, "id");
    REQUIRE(id > 0);
    char s[32];
    REQUIRE(embed_json_get_string(js, &t[id], s, sizeof(s)));
    CHECK_STR_EQ(s, "pump-01");
    double sp = 0;
    REQUIRE(embed_json_get_double(js, &t[embed_json_object_get(js, t, n, 0, "setpoint")], &sp));
    CHECK_EQ(sp, 21.5);
    bool en = true;
    REQUIRE(embed_json_get_bool(js, &t[embed_json_object_get(js, t, n, 0, "enabled")], &en));
    CHECK(!en);
    const int limits = embed_json_object_get(js, t, n, 0, "limits");
    REQUIRE(limits > 0);
    int64_t mn = 0;
    REQUIRE(embed_json_get_int(js, &t[embed_json_object_get(js, t, n, limits, "min")], &mn));
    CHECK_EQ(mn, -10);
    const int mx = embed_json_object_get(js, t, n, limits, "max");
    int64_t imx = 0;
    CHECK(!embed_json_get_int(js, &t[mx], &imx)); // 1e2 は整数として読まない
    double dmx = 0;
    CHECK(embed_json_get_double(js, &t[mx], &dmx));
    CHECK_EQ(dmx, 100.0);
    // キー "min" はトップレベルにはない (入れ子の中だけ)
    CHECK_EQ(embed_json_object_get(js, t, n, 0, "min"), -1);
    const int tags = embed_json_object_get(js, t, n, 0, "tags");
    REQUIRE(tags > 0);
    CHECK_EQ(t[tags].size, 3u);
    REQUIRE(embed_json_get_string(js, &t[embed_json_array_get(t, n, tags, 1)], s, sizeof(s)));
    CHECK_STR_EQ(s, "b\"c");
    REQUIRE(embed_json_get_string(js, &t[embed_json_array_get(t, n, tags, 2)], s, sizeof(s)));
    CHECK_STR_EQ(s, "\xC3\xA9\xF0\x9F\x98\x80"); // é と 😀 (サロゲートペア)
    CHECK_EQ(embed_json_array_get(t, n, tags, 3), -1);
    CHECK_EQ(t[embed_json_object_get(js, t, n, 0, "none")].type, EMBED_JSON_NULL);
    CHECK_EQ(embed_json_object_get(js, t, n, 0, "missing"), -1);
    CHECK_EQ(embed_json_object_get(js, t, n, tags, "a"), -1); // 配列にキー検索はできない
    CHECK(!embed_json_get_string(js, &t[id], s, 7));          // 容量不足 ("pump-01" + NUL = 8)
}

TEST_CASE(parser_accepts_valid_grammar) {
    const char *valid[] = {"0", "-0", "1.5e-3", "-12.25E+2", "\"\"", "true", "null", "[]", "{}",
                           " [ 1 , [ 2 , [ ] ] ] ", "{\"a\":{\"b\":[{}]}}", "\"\\/\\b\\u0041\""};
    embed_json_token_t t[16];
    for (const char *js : valid)
        if (!CHECK(parse(js, t, 16) > 0))
            std::printf("# rejected valid: %s\n", js);
}

TEST_CASE(parser_rejects_invalid_grammar) {
    struct Bad {
        const char *js;
        int err;
    };
    const Bad bad[] = {
        {"01", EMBED_JSON_ERR_INVALID},       {"1.", EMBED_JSON_ERR_PARTIAL},
        {"1.e3", EMBED_JSON_ERR_INVALID},     {"+1", EMBED_JSON_ERR_INVALID},
        {".5", EMBED_JSON_ERR_INVALID},       {"[1,]", EMBED_JSON_ERR_INVALID},
        {"{\"a\":1,}", EMBED_JSON_ERR_INVALID}, {"{a:1}", EMBED_JSON_ERR_INVALID},
        {"{\"a\" 1}", EMBED_JSON_ERR_INVALID}, {"[1 2]", EMBED_JSON_ERR_INVALID},
        {"tru", EMBED_JSON_ERR_PARTIAL},      {"trUe", EMBED_JSON_ERR_INVALID},
        {"\"a\\x\"", EMBED_JSON_ERR_INVALID}, {"\"\\u12G4\"", EMBED_JSON_ERR_INVALID},
        {"\"a\tb\"", EMBED_JSON_ERR_INVALID}, {"[1] x", EMBED_JSON_ERR_INVALID},
        {"{\"a\":", EMBED_JSON_ERR_PARTIAL},  {"[\"abc", EMBED_JSON_ERR_PARTIAL},
        {"", EMBED_JSON_ERR_PARTIAL},         {"'a'", EMBED_JSON_ERR_INVALID},
        {"NaN", EMBED_JSON_ERR_INVALID},      {"[true false]", EMBED_JSON_ERR_INVALID},
    };
    embed_json_token_t t[16];
    for (const auto &b : bad)
        if (!CHECK_EQ(parse(b.js, t, 16), b.err))
            std::printf("# input: %s\n", b.js);
}

TEST_CASE(parser_limits) {
    embed_json_token_t t[4];
    CHECK_EQ(parse("[1,2,3]", t, 4), 4);
    CHECK_EQ(parse("[1,2,3,4]", t, 4), EMBED_JSON_ERR_NOMEM);
    std::string deep(EMBED_JSON_MAX_DEPTH, '[');
    deep += std::string(EMBED_JSON_MAX_DEPTH, ']');
    embed_json_token_t many[64];
    CHECK_EQ(embed_json_parse(deep.c_str(), deep.size(), many, 64), EMBED_JSON_MAX_DEPTH);
    std::string deeper = "[" + deep + "]";
    CHECK_EQ(embed_json_parse(deeper.c_str(), deeper.size(), many, 64), EMBED_JSON_ERR_DEPTH);
    CHECK_EQ(embed_json_parse(nullptr, 0, t, 4), EMBED_JSON_ERR_INVALID);
    // 長さで区切られた入力 (NUL 終端でない受信バッファ)
    const char frame[] = "{\"v\":7}garbage";
    CHECK_EQ(embed_json_parse(frame, 7, t, 4), 3);
}

TEST_CASE(int_range) {
    embed_json_token_t t[2];
    int64_t v = 0;
    const char *max = "9223372036854775807";
    REQUIRE_EQ(parse(max, t, 2), 1);
    CHECK(embed_json_get_int(max, &t[0], &v));
    CHECK_EQ(v, INT64_MAX);
    const char *min = "-9223372036854775808";
    REQUIRE_EQ(parse(min, t, 2), 1);
    CHECK(embed_json_get_int(min, &t[0], &v));
    CHECK_EQ(v, INT64_MIN);
    const char *over = "9223372036854775808";
    REQUIRE_EQ(parse(over, t, 2), 1);
    CHECK(!embed_json_get_int(over, &t[0], &v));
    const char *str = "\"5\"";
    REQUIRE_EQ(parse(str, t, 2), 1);
    CHECK(!embed_json_get_int(str, &t[0], &v)); // 型が違う
    bool b;
    CHECK(!embed_json_get_bool(str, &t[0], &b));
}

TEST_CASE(round_trip_writer_to_parser) {
    char buf[256];
    embed_json_writer_t w;
    embed_json_writer_init(&w, buf, sizeof(buf));
    embed_json_object_begin(&w);
    embed_json_kv_string(&w, "msg", "line1\nline2 \"quoted\" \x02");
    embed_json_kv_int(&w, "n", 123456789012345);
    embed_json_object_end(&w);
    const char *out = embed_json_finish(&w, nullptr);
    REQUIRE(out != nullptr);
    embed_json_token_t t[8];
    const int n = parse(out, t, 8);
    REQUIRE(n == 5);
    char s[64];
    REQUIRE(embed_json_get_string(out, &t[embed_json_object_get(out, t, n, 0, "msg")], s, sizeof(s)));
    CHECK_STR_EQ(s, "line1\nline2 \"quoted\" \x02");
    int64_t v = 0;
    REQUIRE(embed_json_get_int(out, &t[embed_json_object_get(out, t, n, 0, "n")], &v));
    CHECK_EQ(v, 123456789012345);
}

EMBED_TEST_MAIN()
