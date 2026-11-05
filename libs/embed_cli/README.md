# embed_cli

シリアルコンソール (UART / USB CDC / Telnet) 用のコマンドシェル。C11、動的確保なし。
デバッグ用の保守コンソールや、量産時の設定・検査コマンドに使えます。

| 機能 | 内容 |
| --- | --- |
| 行編集 | Backspace (0x08 / 0x7F)、Ctrl-C で行を破棄、Ctrl-U で行を消去。CR / LF / CR LF のどれでも改行 |
| 履歴 | ↑↓ (VT100)。直前と同じ行は積まない。`EMBED_CLI_HISTORY` 行 (既定 4、0 で無効) |
| 補完 | Tab でコマンド名を補完。候補が複数なら一覧を表示 |
| 引数 | 空白区切り。`"..."` / `'...'` でまとめ、`\` で次の 1 文字をそのまま (シングルクォート内は除く) |
| help | 組み込み。`help <command>` で 1 件 |
| 出力 | `embed_cli_print` / `embed_cli_printf` の `\n` を `\r\n` に変換 |
| 解析 | `embed_cli_parse_long` (10 進 / 0x / 0b、範囲検査、"010" は 10)、`parse_double`、`parse_bool` |

長すぎる行は切り詰めて実行せず、エラーにします。

```c
static int cmd_led(embed_cli_t *cli, int argc, char **argv) {
    bool on;
    if (argc != 2 || !embed_cli_parse_bool(argv[1], &on)) {
        embed_cli_print(cli, "usage: led <on|off>\n");
        return 1;
    }
    led_set(on);
    return 0;
}
static int cmd_speed(embed_cli_t *cli, int argc, char **argv) {
    long rpm;
    if (argc != 2 || !embed_cli_parse_long(argv[1], 0, 3000, &rpm)) {
        embed_cli_print(cli, "usage: speed <0..3000>\n");
        return 1;
    }
    embed_cli_printf(cli, "speed = %ld rpm\n", rpm);
    return 0;
}
static const embed_cli_command_t commands[] = {
    {"led", "led <on|off>     switch the LED", cmd_led},
    {"speed", "speed <0..3000>  set motor speed", cmd_speed},
};

static void uart_write(const char *data, size_t len, void *user) { uart_send(data, len); }

embed_cli_t cli;
embed_cli_init(&cli, commands, 2, uart_write, NULL, "> ");
embed_cli_start(&cli);
/* メインループ: 受信割込みが積んだ文字を渡す */
char c;
while (rx_get(&c))
    embed_cli_feed(&cli, c);
```

- ハンドラの戻り値は `cli.last_status` に入ります。`cli.app` はアプリの文脈を渡すのに使えます。
- `embed_cli_execute(&cli, "speed 100")` で行編集を通さずに実行できます (起動スクリプトなど)。
- ハンドラ内で `embed_cli_feed` を呼んでも、実行中の行は壊れません。
- 割込みから直接 `embed_cli_feed` を呼ぶと、出力関数が割込み内で動きます。通常はメインループから呼んでください。

## テスト

`test_embed_cli` は端末へ送られるバイト列ごと、Backspace / Ctrl-C / Ctrl-U、CR LF、履歴の移動と上限、
Tab 補完、行あふれ、引用符とエスケープの分割、数値・真偽値の解析の境界を確認します。
