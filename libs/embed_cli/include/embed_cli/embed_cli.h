#pragma once
/*
 * embed_cli: シリアルコンソール (UART / USB CDC / Telnet) 用のコマンドシェル。
 *
 * - 1 文字ずつ embed_cli_feed に渡す (受信割込みはリングバッファに積み、メインループで渡す)
 * - 行編集: Backspace / Ctrl-C (行の破棄) / Ctrl-U (行の消去)
 * - 履歴: ↑↓ (VT100 のエスケープシーケンス)。EMBED_CLI_HISTORY = 0 で無効
 * - Tab: コマンド名の補完 (候補が複数なら一覧を表示)
 * - 引数: 空白区切り、"..." / '...' でまとめる、\ で次の 1 文字をそのまま
 * - help は組み込み (同名のコマンドを登録すればそちらを使う)
 * - 出力の "\n" は "\r\n" に変換して送る
 * 動的確保なし。CLI 1 つあたりの RAM は (行長 × (履歴 + 2)) + 数十バイト。
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#ifndef EMBED_CLI_LINE_MAX
#define EMBED_CLI_LINE_MAX 128 /* 1 行の最大長 (終端を含む) */
#endif
#ifndef EMBED_CLI_ARGV_MAX
#define EMBED_CLI_ARGV_MAX 16 /* コマンド名を含む引数の最大数 */
#endif
#ifndef EMBED_CLI_HISTORY
#define EMBED_CLI_HISTORY 4 /* 履歴の行数 (0 で無効) */
#endif
#ifndef EMBED_CLI_PRINTF_MAX
#define EMBED_CLI_PRINTF_MAX 160 /* embed_cli_printf 1 回の最大長 */
#endif

typedef struct embed_cli embed_cli_t;
/* 戻り値は自由 (embed_cli_t の last_status に入る)。慣例として 0 が成功。 */
typedef int (*embed_cli_handler)(embed_cli_t *cli, int argc, char **argv);
typedef struct {
    const char *name;
    const char *help; /* "help" の一覧に出す 1 行の説明 (例: "led <on|off>  LED を切り替える") */
    embed_cli_handler handler;
} embed_cli_command_t;
typedef void (*embed_cli_write_fn)(const char *data, size_t len, void *user);

/* embed_cli_execute の戻り値のうちライブラリが返すもの */
#define EMBED_CLI_EMPTY 0                /* 空行 */
#define EMBED_CLI_ERR_UNKNOWN (-1000)    /* 未登録のコマンド */
#define EMBED_CLI_ERR_SYNTAX (-1001)     /* 引用符が閉じていない */
#define EMBED_CLI_ERR_TOO_MANY (-1002)   /* 引数が多すぎる */
#define EMBED_CLI_ERR_TOO_LONG (-1003)   /* 行が長すぎる */

struct embed_cli {
    const embed_cli_command_t *commands;
    size_t command_count;
    embed_cli_write_fn write;
    void *user;       /* write に渡す値 */
    void *app;        /* ハンドラから使うアプリの文脈 (自由に使ってよい) */
    const char *prompt;
    bool echo;
    int last_status;
    /* 以下は内部状態 */
    char line[EMBED_CLI_LINE_MAX];
    size_t len;
    bool overflow;
    bool last_cr;
    uint8_t esc;
    char scratch[EMBED_CLI_LINE_MAX];
#if EMBED_CLI_HISTORY > 0
    char history[EMBED_CLI_HISTORY][EMBED_CLI_LINE_MAX];
    uint8_t hist_count;
    uint8_t hist_head; /* 次に書く位置 */
    int hist_pos;      /* -1: 編集中の行、0: 最新 */
#endif
};

void embed_cli_init(embed_cli_t *cli, const embed_cli_command_t *commands, size_t command_count,
                    embed_cli_write_fn write, void *user, const char *prompt);
/* エコーバックの有無 (既定: あり)。ローカルエコーする端末やスクリプト入力では切る。 */
void embed_cli_set_echo(embed_cli_t *cli, bool echo);
/* プロンプトを表示する (起動時のバナーの後などに呼ぶ) */
void embed_cli_start(embed_cli_t *cli);
void embed_cli_feed(embed_cli_t *cli, char c);
void embed_cli_feed_str(embed_cli_t *cli, const char *s);
/* 1 行をそのまま実行する (行編集を通さない。スクリプトや起動時の設定用)。 */
int embed_cli_execute(embed_cli_t *cli, const char *line);

void embed_cli_print(embed_cli_t *cli, const char *s);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
void embed_cli_printf(embed_cli_t *cli, const char *fmt, ...);
void embed_cli_vprintf(embed_cli_t *cli, const char *fmt, va_list ap);

/* ---- 単体でも使えるユーティリティ ---- */
/* line をその場で分割する。引数の数、または EMBED_CLI_ERR_SYNTAX / EMBED_CLI_ERR_TOO_MANY。 */
int embed_cli_tokenize(char *line, char **argv, int max_args);
/* 10 進 / 0x 16 進 / 0b 2 進、符号付き。範囲 [min, max] 外・余分な文字は false。 */
bool embed_cli_parse_long(const char *s, long min, long max, long *out);
/* 小数 (指数表記可)。範囲外・NaN・余分な文字は false。 */
bool embed_cli_parse_double(const char *s, double min, double max, double *out);
/* 1/0, on/off, true/false, yes/no (大文字小文字は区別しない) */
bool embed_cli_parse_bool(const char *s, bool *out);

#ifdef __cplusplus
}
#endif
