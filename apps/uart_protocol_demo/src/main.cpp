#include <iostream>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <iomanip>

#include "uart_protocol/uart_arq.h"
#include "uart_protocol/uart_protocol.h"
#include "uart_protocol/uart_protocol.hpp"

#include <deque>

// ---------------------------------------------------------
// シナリオ 5 用: フレームを落とす仮想回線の上で ARQ (ACK 待ち・再送) を動かす
// ---------------------------------------------------------
namespace arq_demo {
struct Node {
    const char *name;
    uart_protocol_t proto;
    uart_arq_t arq;
    std::deque<std::vector<uint8_t>> wire; // この端点が送ったフレーム
    unsigned sent = 0, dropped = 0;
    std::vector<std::string> received;
};
unsigned g_drop_counter = 0;
void tx(const uint8_t *d, size_t n, void *u) {
    auto *node = static_cast<Node *>(u);
    node->sent++;
    if (++g_drop_counter % 3 == 0) { // 3 フレームに 1 つ失われる回線
        node->dropped++;
        return;
    }
    node->wire.emplace_back(d, d + n);
}
void on_receive(const uart_packet_t *p, void *u) {
    static_cast<Node *>(u)->received.emplace_back((const char *)p->payload, p->length);
}
void on_result(uint8_t seq, bool ok, void *u) {
    std::printf("  [%s] seq %u %s\n", static_cast<Node *>(u)->name, seq, ok ? "delivered (ACK)" : "FAILED");
}
void setup(Node &n) {
    uart_proto_init(&n.proto, tx, nullptr, &n);
    n.proto.rx_cb = [](const uart_packet_t *p, void *u) { uart_arq_on_packet(&static_cast<Node *>(u)->arq, p); };
    uart_arq_init(&n.arq, &n.proto, 20 /* ms */, 5 /* retries */);
    uart_arq_set_callbacks(&n.arq, on_receive, on_result, &n);
}
void deliver(Node &from, Node &to) {
    while (!from.wire.empty()) {
        auto f = from.wire.front();
        from.wire.pop_front();
        uart_proto_process_bytes(&to.proto, f.data(), f.size());
    }
}
bool run() {
    Node a{"sensor", {}, {}, {}, 0, 0, {}}, b{"gateway", {}, {}, {}, 0, 0, {}};
    setup(a);
    setup(b);
    const char *messages[] = {"temp=23.5", "temp=23.7", "temp=24.0", "alarm=0", "temp=24.4"};
    size_t next = 0;
    for (uint32_t now = 0; now < 2000 && b.received.size() < 5; ++now) {
        if (!uart_arq_busy(&a.arq) && next < 5)
            uart_arq_send(&a.arq, UART_PKT_TYPE_DATA, (const uint8_t *)messages[next], (uint16_t)std::strlen(messages[next]), now, nullptr), ++next;
        deliver(a, b);
        deliver(b, a);
        uart_arq_poll(&a.arq, now);
    }
    std::printf("  link: %u frames sent, %u dropped; retransmits=%u duplicates filtered=%u\n",
                a.sent + b.sent, a.dropped + b.dropped, a.arq.stats.retransmits, b.arq.stats.rx_duplicates);
    bool ok = b.received.size() == 5;
    for (size_t i = 0; ok && i < 5; ++i)
        ok = b.received[i] == messages[i];
    std::printf("  gateway received all 5 messages in order exactly once: %s\n", ok ? "yes" : "NO");
    return ok;
}
} // namespace arq_demo

// コマンドID定義
enum CustomCmd : uint8_t {
    CMD_GET_TEMPERATURE = 0x10,
    CMD_SET_LED_STATE   = 0x11,
    CMD_SYSTEM_RESET    = 0x12
};

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << " UART Protocol Stack (uart_protocol) Demonstration" << std::endl;
    std::cout << "=====================================================" << std::endl;

    // 通信仮想バス（Master -> Slave, Slave -> Master）
    std::vector<uint8_t> bus_m2s;
    std::vector<uint8_t> bus_s2m;

    uart_protocol::Protocol master;
    uart_protocol::Protocol slave;

    // 1. Masterの送信ハンドラ
    master.set_tx_handler([&bus_m2s](const uint8_t *data, size_t len) {
        bus_m2s.insert(bus_m2s.end(), data, data + len);
    });

    // 2. Slaveの送信ハンドラ
    slave.set_tx_handler([&bus_s2m](const uint8_t *data, size_t len) {
        bus_s2m.insert(bus_s2m.end(), data, data + len);
    });

    // 3. Slaveの受信ハンドラ (コマンドを解釈して応答)
    slave.set_rx_handler([&slave](const uart_packet_t &pkt) {
        std::printf("  [Slave RX] Type=0x%02X, Seq=%d, Len=%d\n", pkt.type, pkt.seq, pkt.length);

        if (pkt.type == UART_PKT_TYPE_PING) {
            std::cout << "  [Slave] Responding PONG...\n";
            slave.send(UART_PKT_TYPE_PONG, nullptr, 0);
        } else if (pkt.type == UART_PKT_TYPE_COMMAND) {
            uint8_t cmd_id = pkt.payload[0];
            if (cmd_id == CMD_GET_TEMPERATURE) {
                float temp = 24.8f;
                uint8_t resp_data[5];
                resp_data[0] = cmd_id;
                memcpy(&resp_data[1], &temp, sizeof(float));
                std::printf("  [Slave] Responding Temperature: %.1f C\n", temp);
                slave.send(UART_PKT_TYPE_RESPONSE, resp_data, sizeof(resp_data));
            } else if (cmd_id == CMD_SET_LED_STATE) {
                uint8_t led_on = pkt.payload[1];
                std::printf("  [Slave] LED is now %s\n", led_on ? "ON" : "OFF");
                slave.send_ack(pkt.seq);
            }
        }
    });

    // 4. Masterの受信ハンドラ
    master.set_rx_handler([](const uart_packet_t &pkt) {
        std::printf("  [Master RX] Type=0x%02X, Seq=%d, Len=%d\n", pkt.type, pkt.seq, pkt.length);
        if (pkt.type == UART_PKT_TYPE_PONG) {
            std::cout << "  [Master] Received PONG from Slave! Device is ONLINE.\n";
        } else if (pkt.type == UART_PKT_TYPE_RESPONSE) {
            uint8_t cmd_id = pkt.payload[0];
            if (cmd_id == CMD_GET_TEMPERATURE) {
                float temp = 0.0f;
                memcpy(&temp, &pkt.payload[1], sizeof(float));
                std::printf("  [Master] Temperature reading: %.1f C\n", temp);
            }
        } else if (pkt.type == UART_PKT_TYPE_ACK) {
            std::printf("  [Master] Received ACK for Sequence #%d\n", pkt.payload[0]);
        }
    });

    // ---------------------------------------------------------
    // シナリオ 1: PING / PONG 疎通確認
    // ---------------------------------------------------------
    std::cout << "\n>>> Scenario 1: Ping / Pong Test <<<\n";
    master.send_ping();
    slave.process_bytes(bus_m2s);
    bus_m2s.clear();
    master.process_bytes(bus_s2m);
    bus_s2m.clear();

    // ---------------------------------------------------------
    // シナリオ 2: コマンド送信 (温度取得)
    // ---------------------------------------------------------
    std::cout << "\n>>> Scenario 2: Command & Response (Get Temperature) <<<\n";
    uint8_t cmd_temp = CMD_GET_TEMPERATURE;
    master.send(UART_PKT_TYPE_COMMAND, &cmd_temp, 1);
    slave.process_bytes(bus_m2s);
    bus_m2s.clear();
    master.process_bytes(bus_s2m);
    bus_s2m.clear();

    // ---------------------------------------------------------
    // シナリオ 3: コマンド送信 (LED制御 + ACK応答)
    // ---------------------------------------------------------
    std::cout << "\n>>> Scenario 3: Command & ACK (Turn LED ON) <<<\n";
    uint8_t cmd_led[2] = {CMD_SET_LED_STATE, 1};
    master.send(UART_PKT_TYPE_COMMAND, cmd_led, 2);
    slave.process_bytes(bus_m2s);
    bus_m2s.clear();
    master.process_bytes(bus_s2m);
    bus_s2m.clear();

    // ---------------------------------------------------------
    // シナリオ 4: ノイズ混入と自動エラー破棄
    // ---------------------------------------------------------
    std::cout << "\n>>> Scenario 4: Noise & Corrupted Frame Handling <<<\n";
    master.send(UART_PKT_TYPE_COMMAND, &cmd_temp, 1);

    // バイト破損をシミュレート
    bus_m2s[3] ^= 0xFF; // ペイロード長またはヘッダを破壊
    std::cout << "  [Bus] Corrupted 1 byte in stream.\n";
    slave.process_bytes(bus_m2s);
    bus_m2s.clear();

    auto slave_stats = slave.get_stats();
    std::printf("  [Slave Stats] CRC Errors: %u | Format Errors: %u | Length Errors: %u\n",
                slave_stats.rx_crc_errors, slave_stats.rx_format_errors, slave_stats.rx_length_errors);
    if (slave_stats.rx_crc_errors + slave_stats.rx_format_errors + slave_stats.rx_length_errors != 1)
        return 1; // 破損したフレームはちょうど 1 件のエラーとして数える

    // ---------------------------------------------------------
    // シナリオ 5: 確実な配送 (uart_arq) - 3 フレームに 1 つ落ちる回線
    // ---------------------------------------------------------
    std::cout << "\n>>> Scenario 5: Reliable delivery over a lossy link (uart_arq) <<<\n";
    if (!arq_demo::run())
        return 1;

    std::cout << "\n=====================================================" << std::endl;
    std::cout << " Demo completed successfully!" << std::endl;
    std::cout << "=====================================================" << std::endl;
    return 0;
}
