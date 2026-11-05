// スーパーループ型ファームウェアの骨格。ホスト上で仮想時刻を進めて動かす。
//
//   起動時   RAM (March C-)・ROM (CRC-32) の自己診断 → 設定をフラッシュから読む (embed_nvparam)
//   10 ms    温度センサ読み取り → メディアン + 1 次 LPF (embed_filter) → 警報判定
//   5 ms     ボタン (embed_input): クリックで表示モード切替、長押しで設定保存
//   1 s      テレメトリ: JSON (embed_json) + CRC-16 を COBS (embed_frame) で UART へ
//   100 ms   ウォッチドッグ給餌 (embed_selftest)。異常時は給餌を止める
//   常時     UART 受信 → コマンドコンソール (embed_cli)、ROM の周期検査 (CPU 時間を分割)
//
// 周期処理は embed_sched のタイマ、背景処理はラウンドロビンのタスク。実機では
// g_now_ms を SysTick で進め、uart_* と adc_read をドライバに置き換える。
#include "embed_cli/embed_cli.h"
#include "embed_crc/embed_crc.h"
#include "embed_filter/embed_filter.h"
#include "embed_frame/embed_frame.h"
#include "embed_input/embed_input.h"
#include "embed_json/embed_json.h"
#include "embed_nvparam/embed_nvparam.h"
#include "embed_ringbuf/embed_ringbuf.h"
#include "embed_sched/embed_sched.h"
#include "embed_selftest/embed_selftest.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
/* ======================= 仮想ハードウェア ======================= */
uint64_t g_now_ms = 0;
uint64_t clock_ms(void *) { return g_now_ms; }

const char kRomImage[] =
    "c_cpp_lib firmware image v1.0.0 -- this table stands in for the flash contents checked by the ROM test.";
const uint32_t kRomCrc = 0x76BDAE39u; /* ビルド時に計算して埋め込む値 */

uint32_t g_scratch_ram[256]; /* 自己診断専用の RAM 領域 */

/* 温度センサ: 22 ℃ から 1 秒に 2 ℃ 上がる + ノイズ + 時々のスパイク */
float adc_read_temperature() {
    const double t = (double)g_now_ms / 1000.0;
    double v = 22.0 + 2.0 * t + 0.2 * std::sin(t * 37.0);
    if (g_now_ms % 970 == 0)
        v += 40.0; /* 配線ノイズによるスパイク */
    return (float)v;
}

/* ボタン (押下で true): 1.0 s にクリック、3.0〜4.0 s に長押し */
bool gpio_button() {
    return (g_now_ms >= 1000 && g_now_ms < 1080) || (g_now_ms >= 3000 && g_now_ms < 4000);
}

/* UART: TX はリングバッファに積み、ホスト側がそれを読む。RX はホストから届いた文字 */
uint8_t g_tx_storage[2048], g_rx_storage[256];
embed_ringbuf_t g_uart_tx, g_uart_rx;

/* フラッシュ: 2 KB × 2 バンク (消去は 0xFF、書き込みは 1 -> 0) */
uint8_t g_flash[4096];
bool flash_read(void *, uint32_t a, void *d, size_t n) { return std::memcpy(d, g_flash + a, n), true; }
bool flash_write(void *, uint32_t a, const void *s, size_t n) {
    for (size_t i = 0; i < n; ++i)
        g_flash[a + i] &= ((const uint8_t *)s)[i];
    return true;
}
bool flash_erase(void *, uint32_t a, size_t n) { return std::memset(g_flash + a, 0xFF, n), true; }
const embed_nvparam_flash_t kFlash = {flash_read, flash_write, flash_erase, nullptr, {0, 2048}, 2048, 8};

/* ======================= アプリケーション ======================= */
struct Config {
    float alarm_threshold_c;
    uint16_t telemetry_period_ms;
    uint8_t display_mode;
};
const Config kDefaults = {60.0f, 1000, 0};

struct App {
    embed_sched sched;
    embed_nvparam_t nv;
    Config cfg = kDefaults;
    embed_cli_t cli;
    embed_button_t button;
    median_filter_t median;
    lpf_first_order_t lpf;
    embed_watchdog wdg;
    embed_selftest_rom_ctx rom;
    float temperature = 0;
    bool alarm = false;
    bool healthy = true;
    unsigned telemetry_seq = 0, wdg_feeds = 0, rom_passes = 0, saves = 0;
    std::vector<std::string> log;
} app;

void log_event(const std::string &s) {
    char t[24];
    std::snprintf(t, sizeof(t), "[%5.2fs] ", (double)g_now_ms / 1000.0);
    app.log.push_back(t + s);
    std::printf("%s%s\n", t, s.c_str());
}

/* ---- 周期処理 ---- */
void task_sensor(void *) {
    const float raw = adc_read_temperature();
    app.temperature = lpf_update(&app.lpf, median_filter_update(&app.median, raw));
    const bool alarm = app.temperature > app.cfg.alarm_threshold_c;
    if (alarm != app.alarm) {
        app.alarm = alarm;
        char m[64];
        std::snprintf(m, sizeof(m), "alarm %s (%.1f C, threshold %.1f C)", alarm ? "ON" : "off",
                      (double)app.temperature, (double)app.cfg.alarm_threshold_c);
        log_event(m);
    }
}

bool save_config() {
    const bool ok = embed_nvparam_save(&app.nv, &app.cfg, sizeof(app.cfg)) == EMBED_NVPARAM_OK;
    app.saves += ok ? 1u : 0u;
    log_event(ok ? "config saved to flash" : "config save FAILED");
    return ok;
}

void task_button(void *) {
    const uint8_t ev = embed_button_update(&app.button, gpio_button(), (uint32_t)g_now_ms);
    if (ev & EMBED_BUTTON_CLICK) {
        app.cfg.display_mode = (uint8_t)((app.cfg.display_mode + 1) % 3);
        log_event("button click -> display mode " + std::to_string(app.cfg.display_mode));
    }
    if (ev & EMBED_BUTTON_LONG_PRESS) {
        log_event("button long press -> save config");
        save_config();
    }
}

void task_telemetry(void *) {
    char json[160];
    embed_json_writer_t w;
    embed_json_writer_init(&w, json, sizeof(json));
    embed_json_object_begin(&w);
    embed_json_kv_int(&w, "seq", app.telemetry_seq++);
    embed_json_kv_int(&w, "uptime_ms", (int64_t)g_now_ms);
    embed_json_kv_fixed(&w, "temp_c", app.temperature, 1);
    embed_json_kv_bool(&w, "alarm", app.alarm);
    embed_json_kv_int(&w, "mode", app.cfg.display_mode);
    embed_json_object_end(&w);
    size_t len = 0;
    const char *doc = embed_json_finish(&w, &len);
    if (!doc)
        return;
    /* ペイロード + CRC-16 を COBS で符号化し、区切りの 0x00 を付けて送る */
    uint8_t payload[176], frame[EMBED_COBS_MAX_ENCODED(176) + 1];
    std::memcpy(payload, doc, len);
    len += embed_crc_append(EMBED_CRC16_CCITT, embed_crc(EMBED_CRC16_CCITT, payload, len), payload + len,
                            sizeof(payload) - len);
    size_t n = 0;
    if (embed_cobs_encode(payload, len, frame, sizeof(frame) - 1, &n)) {
        frame[n++] = EMBED_COBS_DELIMITER;
        embed_ringbuf_write(&g_uart_tx, frame, n);
    }
}

void task_watchdog(void *) {
    if (embed_watchdog_poll(&app.wdg, g_now_ms, app.healthy))
        app.wdg_feeds++;
}

/* ---- 背景処理 (ラウンドロビン) ---- */
void bg_console(void *) {
    uint8_t c;
    while (embed_ringbuf_get(&g_uart_rx, &c))
        embed_cli_feed(&app.cli, (char)c);
}
void bg_rom_check(void *) {
    switch (embed_selftest_rom_step(&app.rom, 16)) {
    case EMBED_SELFTEST_PASS:
        app.rom_passes++;
        break;
    case EMBED_SELFTEST_FAIL:
        app.healthy = false; /* ウォッチドッグの給餌を止めて安全側へ */
        log_event("ROM check FAILED");
        break;
    default:
        break;
    }
}

/* ---- コンソールのコマンド ---- */
int cmd_status(embed_cli_t *cli, int, char **) {
    embed_cli_printf(cli, "temp=%.1fC alarm=%s threshold=%.1fC mode=%u feeds=%u rom_passes=%u\n",
                     (double)app.temperature, app.alarm ? "on" : "off", (double)app.cfg.alarm_threshold_c,
                     app.cfg.display_mode, app.wdg_feeds, app.rom_passes);
    return 0;
}
int cmd_set(embed_cli_t *cli, int argc, char **argv) {
    double v = 0;
    if (argc == 3 && std::strcmp(argv[1], "threshold") == 0 &&
        embed_cli_parse_double(argv[2], -40.0, 150.0, &v)) {
        app.cfg.alarm_threshold_c = (float)v;
        embed_cli_printf(cli, "threshold = %.1f C\n", v);
        return 0;
    }
    embed_cli_print(cli, "usage: set threshold <-40..150>\n");
    return 1;
}
int cmd_save(embed_cli_t *cli, int, char **) {
    embed_cli_print(cli, save_config() ? "saved\n" : "save failed\n");
    return 0;
}
const embed_cli_command_t kCommands[] = {
    {"status", "status                  show sensor and health", cmd_status},
    {"set", "set threshold <C>       change the alarm threshold", cmd_set},
    {"save", "save                    store the configuration", cmd_save},
};
std::string g_console_out;
void console_write(const char *d, size_t n, void *) { g_console_out.append(d, n); }

/* ======================= 起動 ======================= */
bool boot() {
    std::printf("--- boot ---\n");
    if (!embed_selftest_march_c(g_scratch_ram, 256) || !embed_selftest_rom(kRomImage, sizeof(kRomImage) - 1, kRomCrc)) {
        std::printf("self-test FAILED: staying in safe state\n");
        return false;
    }
    std::printf("self-test: RAM March C- OK, ROM CRC-32 OK (0x%08X)\n", (unsigned)kRomCrc);

    embed_nvparam_init(&app.nv, &kFlash, 1);
    app.cfg = kDefaults;
    const embed_nvparam_status st = embed_nvparam_load(&app.nv, &app.cfg, sizeof(app.cfg));
    std::printf("config: %s (threshold %.1f C)\n", st == EMBED_NVPARAM_OK ? "loaded from flash" : "defaults",
                (double)app.cfg.alarm_threshold_c);

    embed_ringbuf_init(&g_uart_tx, g_tx_storage, sizeof(g_tx_storage));
    embed_ringbuf_init(&g_uart_rx, g_rx_storage, sizeof(g_rx_storage));
    median_filter_init(&app.median, 5);
    lpf_init_from_cutoff(&app.lpf, 2.0f, 100.0f);
    embed_button_init(&app.button, nullptr, (uint32_t)g_now_ms);
    embed_watchdog_init(&app.wdg, g_now_ms, 50, 200, [](void *) {}, nullptr);
    embed_selftest_rom_begin(&app.rom, kRomImage, sizeof(kRomImage) - 1, kRomCrc);
    embed_cli_init(&app.cli, kCommands, 3, console_write, nullptr, "> ");
    embed_cli_set_echo(&app.cli, false);

    embed_sched_init(&app.sched, g_now_ms, clock_ms, nullptr);
    embed_sched_start(&app.sched, 0, 10, 10, task_sensor, nullptr);
    embed_sched_start(&app.sched, 1, 5, 5, task_button, nullptr);
    embed_sched_start(&app.sched, 2, app.cfg.telemetry_period_ms, app.cfg.telemetry_period_ms, task_telemetry, nullptr);
    embed_sched_start(&app.sched, 3, 100, 100, task_watchdog, nullptr);
    embed_sched_add_task(&app.sched, bg_console, nullptr, 2);
    embed_sched_add_task(&app.sched, bg_rom_check, nullptr, 2);
    return true;
}

/* ======================= ホスト側 (PC のモニタツール) ======================= */
struct Host {
    uint8_t buf[256];
    embed_cobs_decoder_t dec;
    unsigned frames = 0, crc_errors = 0;
    double last_temp = 0;
    Host() { embed_cobs_decoder_init(&dec, buf, sizeof(buf)); }
    void poll() {
        uint8_t b;
        while (embed_ringbuf_get(&g_uart_tx, &b)) {
            const uint8_t *f = nullptr;
            size_t n = 0;
            if (embed_cobs_decoder_feed(&dec, b, &f, &n) != EMBED_FRAME_COMPLETE)
                continue;
            if (!embed_crc_verify(EMBED_CRC16_CCITT, f, n)) {
                ++crc_errors;
                continue;
            }
            const char *js = (const char *)f;
            embed_json_token_t t[16];
            const int count = embed_json_parse(js, n - 2, t, 16);
            const int temp = count > 0 ? embed_json_object_get(js, t, count, 0, "temp_c") : -1;
            if (temp >= 0 && embed_json_get_double(js, &t[temp], &last_temp)) {
                ++frames;
                std::printf("  host <- %.*s\n", (int)(n - 2), js);
            }
        }
    }
    void send(const char *line) { embed_ringbuf_write(&g_uart_rx, (const uint8_t *)line, std::strlen(line)); }
};
} // namespace

int main() {
    if (!boot())
        return 1;
    Host host;
    /* 5 秒間のスーパーループ (1 ループ = 1 ms) */
    for (; g_now_ms <= 5000; ++g_now_ms) {
        if (g_now_ms == 1500)
            host.send("status\r");
        if (g_now_ms == 2000)
            host.send("set threshold 25\r"); /* 閾値を下げて警報を出させる */
        if (g_now_ms == 4500)
            host.send("status\rset threshold 999\r"); /* 範囲外は拒否 */
        embed_sched_run_once(&app.sched);
        host.poll();
    }
    std::printf("--- console transcript ---\n%s\n", g_console_out.c_str());

    /* 再起動して、長押しで保存した設定が残っていることを確かめる */
    std::printf("--- reboot ---\n");
    const float saved_threshold = app.cfg.alarm_threshold_c;
    Config reloaded{};
    embed_nvparam_t nv;
    embed_nvparam_init(&nv, &kFlash, 1);
    const bool reload_ok = embed_nvparam_load(&nv, &reloaded, sizeof(reloaded)) == EMBED_NVPARAM_OK;
    std::printf("config after reboot: threshold %.1f C, mode %u\n", (double)reloaded.alarm_threshold_c,
                reloaded.display_mode);

    int failures = 0;
    auto check = [&failures](bool ok, const char *what) {
        if (!ok) {
            std::printf("CHECK FAILED: %s\n", what);
            ++failures;
        }
    };
    check(host.frames >= 5, "telemetry frames received");
    check(host.crc_errors == 0, "no telemetry CRC errors");
    check(app.alarm, "alarm raised after lowering the threshold");
    check(app.wdg_feeds >= 45, "watchdog fed every 100 ms");
    check(app.rom_passes >= 1, "background ROM check completed");
    check(app.saves == 1, "config saved once by long press");
    check(reload_ok && reloaded.alarm_threshold_c == saved_threshold && reloaded.alarm_threshold_c == 25.0f,
          "saved threshold survives reboot");
    check(reloaded.display_mode == 1, "display mode changed by click and saved");
    check(g_console_out.find("usage: set threshold") != std::string::npos, "out-of-range value rejected");
    check(app.sched.late_timers == 0, "no late timers");
    std::printf("%s\n", failures ? "demo FAILED" : "demo completed successfully");
    return failures ? 1 : 0;
}
