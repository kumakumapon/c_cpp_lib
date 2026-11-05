#pragma once
/*
 * embed_json: MQTT / HTTP のペイロード向けの小さな JSON (RFC 8259) ライブラリ。動的確保なし。
 *
 * - writer: 呼び出し側のバッファへ書き出す。カンマ・コロンは自動、文字列はエスケープ、
 *   NaN / Inf は null。容量不足や構造の誤用 (キーなしの値、閉じ忘れ) は ok = false になり、
 *   embed_json_finish が NULL を返す (途中までの JSON を送ってしまわない)。
 * - parser: 文字列をコピーせず、トークン (型・範囲・親) の配列にする。文法は厳密
 *   (末尾カンマ・不正なエスケープ・先頭 0 の数値・余分な文字は不正)。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#ifndef EMBED_JSON_MAX_DEPTH
#define EMBED_JSON_MAX_DEPTH 16 /* 入れ子の最大の深さ (書き出し・解析共通) */
#endif

/* ================= 書き出し ================= */
typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool ok;
    uint8_t depth;
    bool root_done;
    uint8_t level[EMBED_JSON_MAX_DEPTH]; /* 内部用: 各階層の種類と状態 */
} embed_json_writer_t;

void embed_json_writer_init(embed_json_writer_t *w, char *buf, size_t cap);
bool embed_json_object_begin(embed_json_writer_t *w);
bool embed_json_object_end(embed_json_writer_t *w);
bool embed_json_array_begin(embed_json_writer_t *w);
bool embed_json_array_end(embed_json_writer_t *w);
bool embed_json_key(embed_json_writer_t *w, const char *key);
bool embed_json_string(embed_json_writer_t *w, const char *s);
bool embed_json_string_n(embed_json_writer_t *w, const char *s, size_t len);
bool embed_json_int(embed_json_writer_t *w, int64_t v);
bool embed_json_uint(embed_json_writer_t *w, uint64_t v);
/* 有効数字 15 桁。NaN / Inf は null */
bool embed_json_double(embed_json_writer_t *w, double v);
/* 小数点以下 decimals 桁 (0〜9) に丸める。センサ値向け。NaN / Inf は null */
bool embed_json_fixed(embed_json_writer_t *w, double v, int decimals);
bool embed_json_bool(embed_json_writer_t *w, bool v);
bool embed_json_null(embed_json_writer_t *w);
/* キーと値をまとめて書く */
bool embed_json_kv_string(embed_json_writer_t *w, const char *key, const char *v);
bool embed_json_kv_int(embed_json_writer_t *w, const char *key, int64_t v);
bool embed_json_kv_double(embed_json_writer_t *w, const char *key, double v);
bool embed_json_kv_fixed(embed_json_writer_t *w, const char *key, double v, int decimals);
bool embed_json_kv_bool(embed_json_writer_t *w, const char *key, bool v);
/* 完成した JSON (NUL 終端) を返す。誤用・容量不足・閉じ忘れ・空なら NULL */
const char *embed_json_finish(embed_json_writer_t *w, size_t *len);

/* ================= 解析 ================= */
typedef enum {
    EMBED_JSON_OBJECT = 1,
    EMBED_JSON_ARRAY,
    EMBED_JSON_STRING, /* start / end は引用符の内側 (エスケープはそのまま) */
    EMBED_JSON_NUMBER,
    EMBED_JSON_BOOL,
    EMBED_JSON_NULL
} embed_json_type;

typedef struct {
    embed_json_type type;
    uint32_t start, end; /* js 内の [start, end) */
    uint16_t size;       /* オブジェクト: キー数、配列: 要素数、キー文字列: 1 */
    int32_t parent;      /* 親トークンの位置 (ルートは -1)。オブジェクトの値の親はキー */
} embed_json_token_t;

#define EMBED_JSON_ERR_NOMEM (-1)   /* トークン配列が足りない */
#define EMBED_JSON_ERR_INVALID (-2) /* 文法違反 */
#define EMBED_JSON_ERR_PARTIAL (-3) /* 途中で終わっている (続きを受信してから再解析) */
#define EMBED_JSON_ERR_DEPTH (-4)   /* 入れ子が深すぎる */

/* トークン数 (1 以上) またはエラーを返す。tokens[0] がルート。 */
int embed_json_parse(const char *js, size_t len, embed_json_token_t *tokens, size_t max_tokens);
/* オブジェクト object の key に対応する値のトークン位置。なければ -1 (キーの比較はエスケープしない生の文字列) */
int embed_json_object_get(const char *js, const embed_json_token_t *tokens, int count, int object,
                          const char *key);
/* 配列 array の n 番目の要素のトークン位置。なければ -1 */
int embed_json_array_get(const embed_json_token_t *tokens, int count, int array, size_t n);
bool embed_json_token_eq(const char *js, const embed_json_token_t *token, const char *s);
/* 文字列をエスケープ解除して out へ (UTF-8、サロゲートペア対応)。容量不足・型違いは false */
bool embed_json_get_string(const char *js, const embed_json_token_t *token, char *out, size_t cap);
/* 整数 (小数部・指数部なし) を範囲検査付きで */
bool embed_json_get_int(const char *js, const embed_json_token_t *token, int64_t *out);
bool embed_json_get_double(const char *js, const embed_json_token_t *token, double *out);
bool embed_json_get_bool(const char *js, const embed_json_token_t *token, bool *out);

#ifdef __cplusplus
}
#endif
