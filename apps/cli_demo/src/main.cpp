// embed_cli による保守コンソールのデモ。
//
//   cli_demo        組み込みのスクリプトを流して結果を検査する (CI のスモークテスト)
//   cli_demo -i     標準入力から対話 (Ctrl-D で終了)。マイコンでは UART の受信文字を渡すだけ
//
// コマンド: help, led, speed, status, calc, history の確認は ↑↓ (端末が VT100 の場合)
#include "embed_cli/embed_cli.h"
#include "sample_math/math_utils.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

namespace {
/* 装置の状態 (実機ならハードウェアのレジスタやドライバ) */
struct Device {
    bool led = false;
    long speed_rpm = 0;
    unsigned commands = 0;
};

Device &dev(embed_cli_t *cli) { return *static_cast<Device *>(cli->app); }

int cmd_led(embed_cli_t *cli, int argc, char **argv) {
    bool on = false;
    if (argc != 2 || !embed_cli_parse_bool(argv[1], &on)) {
        embed_cli_print(cli, "usage: led <on|off>\n");
        return 1;
    }
    dev(cli).led = on;
    embed_cli_printf(cli, "LED %s\n", on ? "ON" : "OFF");
    return 0;
}

int cmd_speed(embed_cli_t *cli, int argc, char **argv) {
    long rpm = 0;
    if (argc != 2 || !embed_cli_parse_long(argv[1], -3000, 3000, &rpm)) {
        embed_cli_print(cli, "usage: speed <-3000..3000>  (0x / 0b prefixes accepted)\n");
        return 1;
    }
    dev(cli).speed_rpm = rpm;
    embed_cli_printf(cli, "speed = %ld rpm\n", rpm);
    return 0;
}

int cmd_status(embed_cli_t *cli, int, char **) {
    const Device &d = dev(cli);
    embed_cli_printf(cli, "led=%s speed=%ld rpm commands=%u\n", d.led ? "on" : "off", d.speed_rpm,
                     d.commands);
    return 0;
}

int cmd_calc(embed_cli_t *cli, int argc, char **argv) {
    long a = 0, b = 0;
    if (argc != 4 || !embed_cli_parse_long(argv[2], -100000, 100000, &a) ||
        !embed_cli_parse_long(argv[3], -100000, 100000, &b)) {
        embed_cli_print(cli, "usage: calc <add|mul> <a> <b>\n");
        return 1;
    }
    if (std::strcmp(argv[1], "add") == 0)
        embed_cli_printf(cli, "%ld\n", (long)sample_math::add((int)a, (int)b));
    else if (std::strcmp(argv[1], "mul") == 0)
        embed_cli_printf(cli, "%ld\n", (long)sample_math::multiply((int)a, (int)b));
    else
        return 1;
    return 0;
}

int cmd_echo(embed_cli_t *cli, int argc, char **argv) {
    for (int i = 1; i < argc; ++i)
        embed_cli_printf(cli, "[%s]%s", argv[i], i + 1 < argc ? " " : "\n");
    return 0;
}

const embed_cli_command_t kCommands[] = {
    {"led", "led <on|off>          switch the status LED", cmd_led},
    {"speed", "speed <rpm>           set motor speed (-3000..3000)", cmd_speed},
    {"status", "status                show device state", cmd_status},
    {"calc", "calc <add|mul> <a> <b>  integer calculator (sample_math)", cmd_calc},
    {"echo", "echo <args...>        show how arguments are split", cmd_echo},
};

std::string transcript;
void out(const char *data, size_t len, void *) {
    std::fwrite(data, 1, len, stdout);
    transcript.append(data, len);
}
} // namespace

int main(int argc, char **argv) {
    Device device;
    embed_cli_t cli;
    embed_cli_init(&cli, kCommands, sizeof(kCommands) / sizeof(kCommands[0]), out, nullptr,
                   "device> ");
    cli.app = &device;

    const bool interactive = argc > 1 && std::strcmp(argv[1], "-i") == 0;
    std::printf("embed_cli maintenance console demo%s\r\n",
                interactive ? " (type help, Ctrl-D to quit)" : " (scripted)");
    embed_cli_start(&cli);

    if (interactive) {
        embed_cli_set_echo(&cli, false); // 端末側がエコーする
        int c;
        while ((c = std::getchar()) != EOF) {
            embed_cli_feed(&cli, (char)c);
            if (c == '\n' || c == '\r')
                device.commands++;
        }
        std::printf("\r\n");
        return 0;
    }

    // UART から 1 文字ずつ届いたのと同じように流す (Backspace・Tab 補完・履歴も含む)
    const char *script = "help\r"
                         "led on\r"
                         "spe\t0x5DC\r"          // Tab で "speed " に補完、16 進で 1500
                         "speed 9999\r"          // 範囲外
                         "staX\btus\r"           // Backspace
                         "echo one \"two words\" 'it''s' a\\ b\r"
                         "calc add 40 2\r"
                         "\x1b[A\r"              // ↑ で直前のコマンドをもう一度
                         "reboot\r"              // 未登録
                         "led off\r";
    embed_cli_feed_str(&cli, script);
    std::printf("\r\n");

    int failures = 0;
    auto expect = [&failures](const char *text) {
        if (transcript.find(text) == std::string::npos) {
            std::printf("missing in transcript: %s\n", text);
            ++failures;
        }
    };
    expect("led     led <on|off>          switch the status LED");
    expect("speed = 1500 rpm");
    expect("usage: speed");
    expect("led=on speed=1500 rpm");
    expect("[one] [two words] [its] [a b]");
    expect("42\r\n");
    expect("error: unknown command 'reboot'");
    expect("LED OFF");
    if (device.led || device.speed_rpm != 1500)
        ++failures;
    std::printf("%s\n", failures ? "demo FAILED" : "demo completed successfully");
    return failures ? 1 : 0;
}
