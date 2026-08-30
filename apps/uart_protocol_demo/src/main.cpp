#include <iostream>
#include <vector>
#include <string>
#include <cstdio>
#include <iomanip>

#include "uart_protocol/uart_protocol.h"
#include "uart_protocol/uart_protocol.hpp"

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
    std::printf("  [Slave Stats] CRC Errors: %u | Format Errors: %u\n",
                slave_stats.rx_crc_errors, slave_stats.rx_format_errors);

    std::cout << "\n=====================================================" << std::endl;
    std::cout << " Demo completed successfully!" << std::endl;
    std::cout << "=====================================================" << std::endl;
    return 0;
}
