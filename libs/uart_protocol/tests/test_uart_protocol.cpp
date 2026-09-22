#include <iostream>
#include <vector>
#include <string>
#include "embed_test/embed_test.hpp"
#include <cstring>

#include "uart_protocol/uart_crc.h"
#include "uart_protocol/uart_ringbuf.h"
#include "uart_protocol/uart_protocol.h"
#include "uart_protocol/uart_protocol.hpp"

TEST_CASE(test_crc16) {
    std::cout << "# [Test] Running CRC-16 tests..." << std::endl;
    const uint8_t test_data[] = "123456789";
    uint16_t crc = uart_crc16_ccitt(test_data, 9, 0xFFFF);
    CHECK(crc == 0x29B1);

    // バイトごとの更新が一致するか検証
    uint16_t step_crc = 0xFFFF;
    for (size_t i = 0; i < 9; ++i) {
        step_crc = uart_crc16_update(step_crc, test_data[i]);
    }
    CHECK(step_crc == 0x29B1);

    std::cout << "# [Test] CRC-16 passed!" << std::endl;
}

TEST_CASE(test_ringbuf) {
    std::cout << "# [Test] Running RingBuffer tests..." << std::endl;
    uint8_t storage[16];
    uart_ringbuf_t rb;
    uart_ringbuf_init(&rb, storage, sizeof(storage));

    CHECK(uart_ringbuf_available(&rb) == 0);
    CHECK(uart_ringbuf_free_space(&rb) == 15);

    // 書き込み
    const uint8_t data[] = {1, 2, 3, 4, 5};
    size_t written = uart_ringbuf_write(&rb, data, 5);
    CHECK(written == 5);
    CHECK(uart_ringbuf_available(&rb) == 5);

    // 読み出し
    uint8_t read_buf[5];
    size_t read_len = uart_ringbuf_read(&rb, read_buf, 5);
    CHECK(read_len == 5);
    for (int i = 0; i < 5; ++i) {
        CHECK(read_buf[i] == (i + 1));
    }
    CHECK(uart_ringbuf_available(&rb) == 0);

    std::cout << "# [Test] RingBuffer passed!" << std::endl;
}

static std::vector<uint8_t> s_tx_stream;
static std::vector<uart_packet_t> s_rx_packets;

static void mock_tx(const uint8_t *data, size_t length, void *) {
    s_tx_stream.insert(s_tx_stream.end(), data, data + length);
}

static void mock_rx(const uart_packet_t *packet, void *) {
    s_rx_packets.push_back(*packet);
}

TEST_CASE(test_protocol_encode_decode) {
    std::cout << "# [Test] Running protocol encode/decode tests..." << std::endl;
    s_tx_stream.clear();
    s_rx_packets.clear();

    uart_protocol_t proto;
    uart_proto_init(&proto, mock_tx, mock_rx, nullptr);

    // 1. DATAパケット送信
    const uint8_t payload[] = "Hello UART Protocol!";
    uint16_t payload_len = (uint16_t)strlen((const char *)payload);
    bool ok = uart_proto_send(&proto, UART_PKT_TYPE_DATA, payload, payload_len);
    CHECK(ok);
    CHECK(!s_tx_stream.empty());

    // 2. 送信されたストリームを受信側に流し込む
    uart_proto_process_bytes(&proto, s_tx_stream.data(), s_tx_stream.size());

    CHECK(s_rx_packets.size() == 1);
    CHECK(s_rx_packets[0].type == UART_PKT_TYPE_DATA);
    CHECK(s_rx_packets[0].length == payload_len);
    CHECK(memcmp(s_rx_packets[0].payload, payload, payload_len) == 0);

    // 3. ACK送信
    s_tx_stream.clear();
    s_rx_packets.clear();
    uart_proto_send_ack(&proto, 42);
    uart_proto_process_bytes(&proto, s_tx_stream.data(), s_tx_stream.size());

    CHECK(s_rx_packets.size() == 1);
    CHECK(s_rx_packets[0].type == UART_PKT_TYPE_ACK);
    CHECK(s_rx_packets[0].length == 1);
    CHECK(s_rx_packets[0].payload[0] == 42);

    std::cout << "# [Test] Protocol encode/decode passed!" << std::endl;
}

TEST_CASE(test_protocol_noise_and_crc_error) {
    std::cout << "# [Test] Running noise and CRC error tests..." << std::endl;
    s_tx_stream.clear();
    s_rx_packets.clear();

    uart_protocol_t proto;
    uart_proto_init(&proto, mock_tx, mock_rx, nullptr);

    // 正常パケット生成
    const uint8_t payload[] = "TestData";
    uart_proto_send(&proto, UART_PKT_TYPE_DATA, payload, 8);

    // 途中のCRCバイトを破壊
    s_tx_stream[s_tx_stream.size() - 2] ^= 0xFF;

    // 前後にゴミデータ(ノイズ)を挿入
    std::vector<uint8_t> noisy_stream;
    noisy_stream.push_back(0xFF);
    noisy_stream.push_back(0x00);
    noisy_stream.insert(noisy_stream.end(), s_tx_stream.begin(), s_tx_stream.end());
    noisy_stream.push_back(0x77);

    uart_proto_process_bytes(&proto, noisy_stream.data(), noisy_stream.size());

    // パケットは破棄され、コールバックは呼ばれない
    CHECK(s_rx_packets.empty());

    uart_proto_stats_t stats{};
    uart_proto_get_stats(&proto, &stats);
    CHECK(stats.rx_crc_errors == 1);

    std::cout << "# [Test] Noise and CRC error passed!" << std::endl;
}

TEST_CASE(test_cpp_wrapper) {
    std::cout << "# [Test] Running C++ Protocol wrapper tests..." << std::endl;
    std::vector<uint8_t> pipe;
    std::vector<std::string> received_messages;

    uart_protocol::Protocol nodeA;
    uart_protocol::Protocol nodeB;

    nodeA.set_tx_handler([&pipe](const uint8_t *data, size_t len) {
        pipe.insert(pipe.end(), data, data + len);
    });

    nodeB.set_rx_handler([&received_messages](const uart_packet_t &pkt) {
        if (pkt.type == UART_PKT_TYPE_DATA) {
            received_messages.emplace_back((const char *)pkt.payload, pkt.length);
        }
    });

    nodeA.send_string(UART_PKT_TYPE_DATA, "Message from Node A to Node B");

    // pipeからNodeBへ転送
    nodeB.process_bytes(pipe);

    CHECK(received_messages.size() == 1);
    CHECK(received_messages[0] == "Message from Node A to Node B");

    std::cout << "# [Test] C++ Protocol wrapper passed!" << std::endl;
}

EMBED_TEST_MAIN()
