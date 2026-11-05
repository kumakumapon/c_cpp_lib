#include "embed_cli/embed_cli.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================= 出力 ================= */

static void raw_write(embed_cli_t *cli, const char *s, size_t n) {
    if (cli->write && n)
        cli->write(s, n, cli->user);
}

static void echo_write(embed_cli_t *cli, const char *s, size_t n) {
    if (cli->echo)
        raw_write(cli, s, n);
}

void embed_cli_print(embed_cli_t *cli, const char *s) {
    if (!cli || !s)
        return;
    /* 単独の "\n" を "\r\n" にして送る */
    const char *start = s;
    for (const char *p = s; *p; ++p) {
        if (*p == '\n' && (p == s || p[-1] != '\r')) {
            raw_write(cli, start, (size_t)(p - start));
            raw_write(cli, "\r\n", 2);
            start = p + 1;
        }
    }
    raw_write(cli, start, strlen(start));
}

void embed_cli_vprintf(embed_cli_t *cli, const char *fmt, va_list ap) {
    if (!cli || !fmt)
        return;
    char buf[EMBED_CLI_PRINTF_MAX];
    vsnprintf(buf, sizeof(buf), fmt, ap); /* 長すぎる分は切り詰め */
    embed_cli_print(cli, buf);
}

void embed_cli_printf(embed_cli_t *cli, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    embed_cli_vprintf(cli, fmt, ap);
    va_end(ap);
}

static void prompt(embed_cli_t *cli) {
    if (cli->prompt)
        raw_write(cli, cli->prompt, strlen(cli->prompt));
}

/* ================= 初期化 ================= */

void embed_cli_init(embed_cli_t *cli, const embed_cli_command_t *commands, size_t command_count,
                    embed_cli_write_fn write, void *user, const char *prompt_text) {
    if (!cli)
        return;
    memset(cli, 0, sizeof(*cli));
    cli->commands = commands;
    cli->command_count = commands ? command_count : 0;
    cli->write = write;
    cli->user = user;
    cli->prompt = prompt_text;
    cli->echo = true;
#if EMBED_CLI_HISTORY > 0
    cli->hist_pos = -1;
#endif
}

void embed_cli_set_echo(embed_cli_t *cli, bool echo) {
    if (cli)
        cli->echo = echo;
}

void embed_cli_start(embed_cli_t *cli) {
    if (cli)
        prompt(cli);
}

/* ================= 分割・解析 ================= */

int embed_cli_tokenize(char *line, char **argv, int max_args) {
    if (!line || !argv || max_args <= 0)
        return EMBED_CLI_ERR_TOO_MANY;
    int argc = 0;
    char *in = line, *out = line; /* out は常に in 以下なのでその場で書ける */
    for (;;) {
        while (*in == ' ' || *in == '\t')
            ++in;
        if (*in == '\0')
            break;
        if (argc >= max_args)
            return EMBED_CLI_ERR_TOO_MANY;
        argv[argc++] = out;
        char quote = 0;
        while (*in && (quote || (*in != ' ' && *in != '\t'))) {
            if (quote && *in == quote) {
                quote = 0;
                ++in;
            } else if (!quote && (*in == '"' || *in == '\'')) {
                quote = *in++;
            } else if (*in == '\\' && quote != '\'' && in[1] != '\0') {
                *out++ = in[1]; /* \ の次の 1 文字をそのまま (シングルクォート内は除く) */
                in += 2;
            } else {
                *out++ = *in++;
            }
        }
        if (quote)
            return EMBED_CLI_ERR_SYNTAX;
        if (*in)
            ++in; /* 区切りの空白 */
        *out++ = '\0';
    }
    return argc;
}

bool embed_cli_parse_long(const char *s, long min, long max, long *out) {
    if (!s || !out || min > max)
        return false;
    const char *p = s;
    bool neg = false;
    if (*p == '+' || *p == '-')
        neg = *p++ == '-';
    unsigned base = 10;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    } else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) {
        base = 2;
        p += 2;
    }
    if (*p == '\0')
        return false;
    /* 先頭 0 を 8 進として扱わない (strtol の base 0 と違い "010" は 10) */
    unsigned long limit = neg ? (unsigned long)LONG_MAX + 1ul : (unsigned long)LONG_MAX;
    unsigned long v = 0;
    for (; *p; ++p) {
        unsigned d;
        if (*p >= '0' && *p <= '9')
            d = (unsigned)(*p - '0');
        else if (*p >= 'a' && *p <= 'f')
            d = (unsigned)(*p - 'a' + 10);
        else if (*p >= 'A' && *p <= 'F')
            d = (unsigned)(*p - 'A' + 10);
        else
            return false;
        if (d >= base || v > (limit - d) / base)
            return false;
        v = v * base + d;
    }
    long result;
    if (neg)
        result = v == (unsigned long)LONG_MAX + 1ul ? LONG_MIN : -(long)v;
    else
        result = (long)v;
    if (result < min || result > max)
        return false;
    *out = result;
    return true;
}

bool embed_cli_parse_double(const char *s, double min, double max, double *out) {
    if (!s || !out || *s == '\0' || isspace((unsigned char)*s))
        return false;
    char *end = NULL;
    const double v = strtod(s, &end);
    if (!end || *end != '\0' || v != v || isinf(v) || v < min || v > max)
        return false;
    *out = v;
    return true;
}

static bool ieq(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
    return *a == *b;
}

bool embed_cli_parse_bool(const char *s, bool *out) {
    if (!s || !out)
        return false;
    static const char *const yes[] = {"1", "on", "true", "yes"};
    static const char *const no[] = {"0", "off", "false", "no"};
    for (size_t i = 0; i < 4; ++i) {
        if (ieq(s, yes[i])) {
            *out = true;
            return true;
        }
        if (ieq(s, no[i])) {
            *out = false;
            return true;
        }
    }
    return false;
}

/* ================= 実行 ================= */

static const embed_cli_command_t *find(const embed_cli_t *cli, const char *name) {
    for (size_t i = 0; i < cli->command_count; ++i)
        if (cli->commands[i].name && strcmp(cli->commands[i].name, name) == 0)
            return &cli->commands[i];
    return NULL;
}

static int builtin_help(embed_cli_t *cli, int argc, char **argv) {
    if (argc >= 2) {
        const embed_cli_command_t *c = find(cli, argv[1]);
        if (!c) {
            embed_cli_printf(cli, "error: unknown command '%s'\n", argv[1]);
            return EMBED_CLI_ERR_UNKNOWN;
        }
        embed_cli_printf(cli, "%s\n", c->help ? c->help : c->name);
        return 0;
    }
    size_t width = 4;
    for (size_t i = 0; i < cli->command_count; ++i)
        if (cli->commands[i].name && strlen(cli->commands[i].name) > width)
            width = strlen(cli->commands[i].name);
    embed_cli_printf(cli, "%-*s  %s\n", (int)width, "help", "show commands (help <command>)");
    for (size_t i = 0; i < cli->command_count; ++i) {
        const embed_cli_command_t *c = &cli->commands[i];
        if (c->name)
            embed_cli_printf(cli, "%-*s  %s\n", (int)width, c->name, c->help ? c->help : "");
    }
    return 0;
}

int embed_cli_execute(embed_cli_t *cli, const char *line) {
    if (!cli || !line)
        return EMBED_CLI_ERR_SYNTAX;
    if (strlen(line) >= sizeof(cli->scratch)) {
        embed_cli_print(cli, "error: line too long\n");
        return cli->last_status = EMBED_CLI_ERR_TOO_LONG;
    }
    strcpy(cli->scratch, line);
    char *argv[EMBED_CLI_ARGV_MAX + 1];
    const int argc = embed_cli_tokenize(cli->scratch, argv, EMBED_CLI_ARGV_MAX);
    if (argc == EMBED_CLI_ERR_SYNTAX) {
        embed_cli_print(cli, "error: unterminated quote\n");
        return cli->last_status = argc;
    }
    if (argc == EMBED_CLI_ERR_TOO_MANY) {
        embed_cli_printf(cli, "error: too many arguments (max %d)\n", EMBED_CLI_ARGV_MAX);
        return cli->last_status = argc;
    }
    if (argc == 0)
        return cli->last_status = EMBED_CLI_EMPTY;
    argv[argc] = NULL;
    const embed_cli_command_t *c = find(cli, argv[0]);
    if (c && c->handler)
        return cli->last_status = c->handler(cli, argc, argv);
    if (strcmp(argv[0], "help") == 0)
        return cli->last_status = builtin_help(cli, argc, argv);
    embed_cli_printf(cli, "error: unknown command '%s' (type help)\n", argv[0]);
    return cli->last_status = EMBED_CLI_ERR_UNKNOWN;
}

/* ================= 行編集 ================= */

static void redraw(embed_cli_t *cli) {
    if (!cli->echo)
        return;
    raw_write(cli, "\r", 1);
    prompt(cli);
    raw_write(cli, cli->line, cli->len);
    raw_write(cli, "\x1b[K", 3); /* 行末まで消去 */
}

#if EMBED_CLI_HISTORY > 0
static void history_add(embed_cli_t *cli) {
    if (cli->hist_count) {
        const unsigned last = (cli->hist_head + EMBED_CLI_HISTORY - 1u) % EMBED_CLI_HISTORY;
        if (strcmp(cli->history[last], cli->line) == 0)
            return; /* 直前と同じ行は積まない */
    }
    strcpy(cli->history[cli->hist_head], cli->line);
    cli->hist_head = (uint8_t)((cli->hist_head + 1u) % EMBED_CLI_HISTORY);
    if (cli->hist_count < EMBED_CLI_HISTORY)
        cli->hist_count++;
}

static void history_show(embed_cli_t *cli, int pos) {
    cli->hist_pos = pos;
    if (pos < 0) {
        cli->len = 0;
    } else {
        const unsigned at =
            (cli->hist_head + 2u * EMBED_CLI_HISTORY - 1u - (unsigned)pos) % EMBED_CLI_HISTORY;
        strcpy(cli->line, cli->history[at]);
        cli->len = strlen(cli->line);
    }
    cli->line[cli->len] = '\0';
    redraw(cli);
}
#endif

static void complete(embed_cli_t *cli) {
    for (size_t i = 0; i < cli->len; ++i)
        if (cli->line[i] == ' ')
            return; /* コマンド名だけを補完する */
    const char *match = NULL;
    size_t matches = 0;
    const size_t n = cli->len;
    for (size_t i = 0; i <= cli->command_count; ++i) {
        const char *name = i < cli->command_count ? cli->commands[i].name : "help";
        if (name && strncmp(name, cli->line, n) == 0 && (i < cli->command_count || !find(cli, "help"))) {
            match = name;
            ++matches;
        }
    }
    if (matches == 1) {
        const size_t full = strlen(match);
        if (full + 1 < sizeof(cli->line)) {
            memcpy(cli->line, match, full);
            cli->line[full] = ' ';
            cli->len = full + 1;
            cli->line[cli->len] = '\0';
            redraw(cli);
        }
    } else if (matches > 1) {
        raw_write(cli, "\r\n", 2);
        for (size_t i = 0; i <= cli->command_count; ++i) {
            const char *name = i < cli->command_count ? cli->commands[i].name : "help";
            if (name && strncmp(name, cli->line, n) == 0 &&
                (i < cli->command_count || !find(cli, "help"))) {
                raw_write(cli, name, strlen(name));
                raw_write(cli, "  ", 2);
            }
        }
        raw_write(cli, "\r\n", 2);
        prompt(cli);
        raw_write(cli, cli->line, cli->len);
    }
}

static void submit(embed_cli_t *cli) {
    echo_write(cli, "\r\n", 2);
    cli->line[cli->len] = '\0';
    if (cli->overflow) {
        embed_cli_print(cli, "error: line too long\n");
        cli->last_status = EMBED_CLI_ERR_TOO_LONG;
    } else if (cli->len > 0) {
#if EMBED_CLI_HISTORY > 0
        history_add(cli);
#endif
        /* ハンドラ中に feed されても壊れないよう、実行前に行を空にする */
        char copy[EMBED_CLI_LINE_MAX];
        memcpy(copy, cli->line, cli->len + 1);
        cli->len = 0;
        cli->line[0] = '\0';
        embed_cli_execute(cli, copy);
    }
    cli->len = 0;
    cli->line[0] = '\0';
    cli->overflow = false;
#if EMBED_CLI_HISTORY > 0
    cli->hist_pos = -1;
#endif
    prompt(cli);
}

void embed_cli_feed(embed_cli_t *cli, char ch) {
    if (!cli)
        return;
    const unsigned char c = (unsigned char)ch;
    /* VT100 のエスケープシーケンス: ESC [ A (上) / ESC [ B (下)。それ以外は捨てる */
    if (cli->esc == 1) {
        cli->esc = c == '[' ? 2 : 0;
        return;
    }
    if (cli->esc == 2) {
        if (c >= 0x40 && c <= 0x7E) { /* 終端文字 */
            cli->esc = 0;
#if EMBED_CLI_HISTORY > 0
            if (c == 'A' && cli->hist_count) {
                const int next = cli->hist_pos + 1;
                history_show(cli, next < cli->hist_count ? next : cli->hist_count - 1);
            } else if (c == 'B' && cli->hist_pos >= 0) {
                history_show(cli, cli->hist_pos - 1);
            }
#endif
        }
        return;
    }
    const bool was_cr = cli->last_cr;
    cli->last_cr = false;
    switch (c) {
    case 0x1B:
        cli->esc = 1;
        return;
    case '\r':
        cli->last_cr = true;
        submit(cli);
        return;
    case '\n':
        if (!was_cr) /* CR LF は 1 回の改行 */
            submit(cli);
        return;
    case 0x08:
    case 0x7F:
        if (cli->len > 0) {
            cli->len--;
            cli->line[cli->len] = '\0';
            echo_write(cli, "\b \b", 3);
        }
        return;
    case 0x03: /* Ctrl-C */
        cli->len = 0;
        cli->line[0] = '\0';
        cli->overflow = false;
        echo_write(cli, "^C\r\n", 4);
        prompt(cli);
        return;
    case 0x15: /* Ctrl-U */
        cli->len = 0;
        cli->line[0] = '\0';
        cli->overflow = false;
        redraw(cli);
        return;
    case '\t':
        complete(cli);
        return;
    default:
        break;
    }
    if (c < 0x20)
        return; /* その他の制御文字は無視 */
    if (cli->len + 1 >= sizeof(cli->line)) {
        cli->overflow = true;
        echo_write(cli, "\a", 1);
        return;
    }
    cli->line[cli->len++] = (char)c;
    cli->line[cli->len] = '\0';
    echo_write(cli, &ch, 1);
}

void embed_cli_feed_str(embed_cli_t *cli, const char *s) {
    if (!cli || !s)
        return;
    while (*s)
        embed_cli_feed(cli, *s++);
}
