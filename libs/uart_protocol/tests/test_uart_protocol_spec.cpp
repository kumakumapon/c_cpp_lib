// uart_protocol のフレーム仕様 (バイト列)・全エラー経路・再同期・受信タイムアウト・統計を固定する。
// 期待フレームは CRC-16/CCITT-FALSE を Python で別途計算した値。
#include "uart_protocol/uart_protocol.h"
#include "uart_protocol/uart_protocol.hpp"
#include "embed_test/embed_test.hpp"
#include <cstring>
#include <type_traits>
#include <vector>

namespace {
// AA | TYPE SEQ LEN_H LEN_L | PAYLOAD | CRC_H CRC_L | 55
const uint8_t kDataAB[] = {0xAA, 0x01, 0x00, 0x00, 0x02, 0x41, 0x42, 0x73, 0xAB, 0x55};
const uint8_t kPing[] = {0xAA, 0x04, 0x00, 0x00, 0x00, 0x4E, 0x31, 0x55};
const uint8_t kAck5[] = {0xAA, 0x02, 0x05, 0x00, 0x01, 0x2A, 0x5F, 0xD3, 0x55};
const uint8_t kCmdAbc[] = {0xAA, 0x10, 0x01, 0x00, 0x03, 0x61, 0x62, 0x63, 0x85, 0x1F, 0x55};
const uint8_t kPongHi[] = {0xAA, 0x05, 0x07, 0x00, 0x02, 0x68, 0x69, 0x3B, 0xA9, 0x55};

struct Capture {
    std::vector<uart_packet_t> rx;
    std::vector<uint8_t> tx;
};
void on_tx(const uint8_t *d, size_t n, void *u) {
    auto *c = static_cast<Capture *>(u);
    c->tx.insert(c->tx.end(), d, d + n);
}
void on_rx(const uart_packet_t *p, void *u) { static_cast<Capture *>(u)->rx.push_back(*p); }

void feed(uart_protocol_t &p, const uint8_t *d, size_t n) { uart_proto_process_bytes(&p, d, n); }
template <size_t N> void feed(uart_protocol_t &p, const uint8_t (&d)[N]) { feed(p, d, N); }
} // namespace

TEST_CASE(encode_matches_reference_bytes) {
    uint8_t out[UART_PROTO_MAX_FRAME_SIZE];
    size_t n = 0;
    REQUIRE(uart_proto_encode(0x01, 0x00, (const uint8_t *)"AB", 2, out, &n));
    REQUIRE_EQ(n, sizeof(kDataAB));
    CHECK_MEM_EQ(out, kDataAB, n);
    REQUIRE(uart_proto_encode(0x04, 0x00, nullptr, 0, out, &n));
    REQUIRE_EQ(n, sizeof(kPing));
    CHECK_MEM_EQ(out, kPing, n);
    const uint8_t star = 0x2A;
    REQUIRE(uart_proto_encode(0x02, 0x05, &star, 1, out, &n));
    CHECK_MEM_EQ(out, kAck5, sizeof(kAck5));
}

TEST_CASE(encode_limits) {
    static uint8_t payload[UART_PROTO_MAX_PAYLOAD + 1];
    static uint8_t out[UART_PROTO_MAX_FRAME_SIZE + 8];
    size_t n = 0;
    CHECK(uart_proto_encode(1, 0, payload, UART_PROTO_MAX_PAYLOAD, out, &n));
    CHECK_EQ(n, (size_t)UART_PROTO_MAX_FRAME_SIZE);
    CHECK_EQ(out[3], (uint8_t)(UART_PROTO_MAX_PAYLOAD >> 8));
    CHECK_EQ(out[4], (uint8_t)(UART_PROTO_MAX_PAYLOAD & 0xFF));
    CHECK(!uart_proto_encode(1, 0, payload, UART_PROTO_MAX_PAYLOAD + 1, out, &n));
    CHECK(!uart_proto_encode(1, 0, nullptr, 1, out, &n));
    CHECK(!uart_proto_encode(1, 0, payload, 1, nullptr, &n));
    CHECK(!uart_proto_encode(1, 0, payload, 1, out, nullptr));
}

TEST_CASE(parse_reports_progress_until_eof) {
    Capture cap;
    uart_protocol_t p;
    uart_proto_init(&p, nullptr, on_rx, &cap);
    for (size_t i = 0; i + 1 < sizeof(kCmdAbc); ++i)
        CHECK_EQ(uart_proto_parse_byte(&p, kCmdAbc[i]), UART_PARSE_IN_PROGRESS);
    CHECK_EQ(uart_proto_parse_byte(&p, kCmdAbc[sizeof(kCmdAbc) - 1]), UART_PARSE_OK);
    REQUIRE_EQ(cap.rx.size(), 1u);
    CHECK_EQ(cap.rx[0].type, 0x10u);
    CHECK_EQ(cap.rx[0].seq, 0x01u);
    CHECK_EQ(cap.rx[0].length, 3u);
    CHECK_MEM_EQ(cap.rx[0].payload, "abc", 3);
    CHECK_EQ(cap.rx[0].crc, 0x851Fu);
    uart_proto_stats_t s{};
    uart_proto_get_stats(&p, &s);
    CHECK_EQ(s.rx_bytes, (uint32_t)sizeof(kCmdAbc));
    CHECK_EQ(s.rx_packets, 1u);
}

TEST_CASE(zero_and_max_length_payloads) {
    Capture cap;
    uart_protocol_t p;
    uart_proto_init(&p, on_tx, on_rx, &cap);
    feed(p, kPing);
    REQUIRE_EQ(cap.rx.size(), 1u);
    CHECK_EQ(cap.rx[0].length, 0u);
    static uint8_t big[UART_PROTO_MAX_PAYLOAD];
    for (size_t i = 0; i < sizeof(big); ++i)
        big[i] = (uint8_t)(i * 13 + 1);
    REQUIRE(uart_proto_send(&p, UART_PKT_TYPE_DATA, big, UART_PROTO_MAX_PAYLOAD));
    CHECK_EQ(uart_proto_process_bytes(&p, cap.tx.data(), cap.tx.size()), 1u);
    REQUIRE_EQ(cap.rx.size(), 2u);
    CHECK_EQ(cap.rx[1].length, (uint16_t)UART_PROTO_MAX_PAYLOAD);
    CHECK_MEM_EQ(cap.rx[1].payload, big, sizeof(big));
}

TEST_CASE(length_error_is_reported_at_len_low_byte) {
    uart_protocol_t p;
    uart_proto_init(&p, nullptr, nullptr, nullptr);
    const uint16_t too_long = UART_PROTO_MAX_PAYLOAD + 1;
    const uint8_t hdr[] = {0xAA, 0x01, 0x00, (uint8_t)(too_long >> 8), (uint8_t)too_long};
    for (size_t i = 0; i + 1 < sizeof(hdr); ++i)
        CHECK_EQ(uart_proto_parse_byte(&p, hdr[i]), UART_PARSE_IN_PROGRESS);
    CHECK_EQ(uart_proto_parse_byte(&p, hdr[sizeof(hdr) - 1]), UART_PARSE_ERR_LEN);
    uart_proto_stats_t s{};
    uart_proto_get_stats(&p, &s);
    CHECK_EQ(s.rx_length_errors, 1u);
    CHECK_EQ(s.rx_format_errors, 0u);
    CHECK_EQ(s.rx_crc_errors, 0u);
}

TEST_CASE(format_and_crc_errors) {
    Capture cap;
    uart_protocol_t p;
    uart_proto_init(&p, nullptr, on_rx, &cap);
    uint8_t bad_eof[sizeof(kDataAB)];
    std::memcpy(bad_eof, kDataAB, sizeof(bad_eof));
    bad_eof[sizeof(bad_eof) - 1] = 0x56;
    for (size_t i = 0; i + 1 < sizeof(bad_eof); ++i)
        uart_proto_parse_byte(&p, bad_eof[i]);
    CHECK_EQ(uart_proto_parse_byte(&p, bad_eof[sizeof(bad_eof) - 1]), UART_PARSE_ERR_FORMAT);

    uint8_t bad_crc[sizeof(kDataAB)];
    std::memcpy(bad_crc, kDataAB, sizeof(bad_crc));
    bad_crc[8] ^= 0x02; // CRC 下位バイト (0xAB -> 0xA9。0xAA = SOF にならない値)
    for (size_t i = 0; i + 1 < sizeof(bad_crc); ++i)
        uart_proto_parse_byte(&p, bad_crc[i]);
    CHECK_EQ(uart_proto_parse_byte(&p, bad_crc[sizeof(bad_crc) - 1]), UART_PARSE_ERR_CRC);

    uint8_t bad_payload[sizeof(kDataAB)];
    std::memcpy(bad_payload, kDataAB, sizeof(bad_payload));
    bad_payload[5] = 0x40; // ペイロードの化け
    feed(p, bad_payload);
    CHECK(cap.rx.empty());
    uart_proto_stats_t s{};
    uart_proto_get_stats(&p, &s);
    CHECK_EQ(s.rx_format_errors, 1u);
    CHECK_EQ(s.rx_crc_errors, 2u);
    CHECK_EQ(s.rx_packets, 0u);
    // エラーの後も正常フレームは受かる
    feed(p, kDataAB);
    CHECK_EQ(cap.rx.size(), 1u);
}

TEST_CASE(noise_sof_before_frame_is_recovered) {
    // 旧実装では、直前のノイズ 0xAA を SOF と誤認すると後続の正しいフレームを失っていた
    Capture cap;
    uart_protocol_t p;
    uart_proto_init(&p, nullptr, on_rx, &cap);
    const uint8_t noise[] = {0x13, 0xAA};
    feed(p, noise);
    feed(p, kDataAB);
    REQUIRE_EQ(cap.rx.size(), 1u);
    CHECK_MEM_EQ(cap.rx[0].payload, "AB", 2);
    // 偽フレームの長さ (5) が正しいフレームの途中で尽きるケース
    const uint8_t false_hdr[] = {0xAA, 0x01, 0x00, 0x00, 0x05};
    feed(p, false_hdr);
    feed(p, kCmdAbc);
    REQUIRE_EQ(cap.rx.size(), 2u);
    CHECK_EQ(cap.rx[1].type, 0x10u);
    // 再走査中の失敗は統計に数えない: 偽フレームごとに 1 件だけ
    uart_proto_stats_t s{};
    uart_proto_get_stats(&p, &s);
    CHECK_EQ(s.rx_packets, 2u);
    CHECK_EQ(s.rx_bytes, (uint32_t)(2 + sizeof(kDataAB) + 5 + sizeof(kCmdAbc)));
    CHECK_EQ(s.rx_format_errors + s.rx_crc_errors + s.rx_length_errors, 2u);
}

TEST_CASE(back_to_back_frames_with_garbage) {
    Capture cap;
    uart_protocol_t p;
    uart_proto_init(&p, nullptr, on_rx, &cap);
    std::vector<uint8_t> stream;
    for (int i = 0; i < 3; ++i) {
        stream.insert(stream.end(), std::begin(kPongHi), std::end(kPongHi));
        stream.push_back(0x55); // EOF と同じ値のゴミ
        stream.push_back(0x00);
    }
    stream.insert(stream.end(), std::begin(kAck5), std::end(kAck5));
    CHECK_EQ(uart_proto_process_bytes(&p, stream.data(), stream.size()), 4u);
    REQUIRE_EQ(cap.rx.size(), 4u);
    CHECK_EQ(cap.rx[3].type, (uint8_t)UART_PKT_TYPE_ACK);
    CHECK_EQ(cap.rx[3].payload[0], 0x2Au);
}

TEST_CASE(randomized_noise_never_loses_frames) {
    Capture cap;
    uart_protocol_t p;
    uart_proto_init(&p, nullptr, on_rx, &cap);
    uint32_t rng = 12345;
    auto rnd = [&rng]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };
    std::vector<std::vector<uint8_t>> sent;
    std::vector<uint8_t> stream;
    for (int f = 0; f < 300; ++f) {
        size_t noise = rnd() % 12;
        for (size_t i = 0; i < noise; ++i)
            stream.push_back((rnd() & 3) == 0 ? 0xAA : (uint8_t)rnd()); // SOF を多めに混ぜる
        std::vector<uint8_t> payload(rnd() % 40);
        for (auto &b : payload)
            b = (uint8_t)rnd();
        uint8_t frame[UART_PROTO_MAX_FRAME_SIZE];
        size_t n = 0;
        REQUIRE(uart_proto_encode(UART_PKT_TYPE_DATA, (uint8_t)f, payload.data(),
                                  (uint16_t)payload.size(), frame, &n));
        stream.insert(stream.end(), frame, frame + n);
        sent.push_back(payload);
    }
    for (size_t i = 0; i < stream.size(); ++i)
        uart_proto_parse_byte(&p, stream[i]);
    // 末尾のノイズが始めた偽フレームが最後のフレームを抱えたまま止まることがある。
    // 受信が途絶えたらタイムアウトで再走査して取り出す。
    uart_proto_set_rx_timeout(&p, 1);
    uart_proto_poll(&p, 0);
    uart_proto_poll(&p, 1);
    for (size_t i = 0; i < cap.rx.size() && i < sent.size(); ++i)
        if (cap.rx[i].seq != (uint8_t)i) {
            std::printf("# first mismatch at %zu (seq %u)\n", i, cap.rx[i].seq);
            break;
        }
    REQUIRE_EQ(cap.rx.size(), sent.size());
    for (size_t i = 0; i < sent.size(); ++i) {
        CHECK_EQ(cap.rx[i].seq, (uint8_t)i);
        CHECK_EQ(cap.rx[i].length, (uint16_t)sent[i].size());
        CHECK(sent[i].empty() || std::memcmp(cap.rx[i].payload, sent[i].data(), sent[i].size()) == 0);
    }
}

TEST_CASE(rx_timeout_aborts_stalled_frame_at_boundary) {
    Capture cap;
    uart_protocol_t p;
    uart_proto_init(&p, nullptr, on_rx, &cap);
    uart_proto_set_rx_timeout(&p, 10);
    const uint8_t stalled[] = {0xAA, 0x01, 0x00, 0x01, 0xF4}; // LEN = 500 の途中で止まる
    feed(p, stalled);
    CHECK(!uart_proto_poll(&p, 100)); // バイト到着を記録
    CHECK(!uart_proto_poll(&p, 109)); // 9 ms: まだ
    CHECK(uart_proto_poll(&p, 110));  // 10 ms ちょうどで破棄
    uart_proto_stats_t s{};
    uart_proto_get_stats(&p, &s);
    CHECK_EQ(s.rx_timeouts, 1u);
    CHECK(!uart_proto_poll(&p, 200)); // SOF 待ちなら何もしない
    feed(p, kDataAB);
    CHECK_EQ(cap.rx.size(), 1u);
}

TEST_CASE(rx_timeout_handles_clock_wrap_and_disable) {
    uart_protocol_t p;
    uart_proto_init(&p, nullptr, nullptr, nullptr);
    uart_proto_set_rx_timeout(&p, 10);
    uart_proto_parse_byte(&p, 0xAA);
    CHECK(!uart_proto_poll(&p, 0xFFFFFFFAu));
    CHECK(!uart_proto_poll(&p, 0x00000003u)); // 周回後 9 ms
    CHECK(uart_proto_poll(&p, 0x00000004u));  // 10 ms
    uart_proto_set_rx_timeout(&p, 0);
    uart_proto_parse_byte(&p, 0xAA);
    CHECK(!uart_proto_poll(&p, 0));
    CHECK(!uart_proto_poll(&p, 1000000)); // 無効なら破棄しない
    CHECK(!uart_proto_poll(nullptr, 0));
}

TEST_CASE(rx_timeout_rescues_frame_swallowed_by_false_length) {
    // ノイズ AA の後に正しいフレームが来ると、偽の LEN = (SEQ<<8)|LEN_H = 0x0100 = 256 になり、
    // 正しいフレーム全体がペイロードとして飲み込まれる。タイムアウト後の再走査で救出する。
    Capture cap;
    uart_protocol_t p;
    uart_proto_init(&p, nullptr, on_rx, &cap);
    uart_proto_set_rx_timeout(&p, 5);
    uart_proto_parse_byte(&p, 0xAA);
    feed(p, kCmdAbc);
    CHECK(cap.rx.empty());
    uart_proto_poll(&p, 0);
    CHECK(uart_proto_poll(&p, 5));
    REQUIRE_EQ(cap.rx.size(), 1u);
    CHECK_MEM_EQ(cap.rx[0].payload, "abc", 3);
}

TEST_CASE(sof_inside_corrupted_frame_delays_but_does_not_lose) {
    // CRC が化けて 0xAA (SOF) になると、そこから偽フレームが始まる。続く DATA フレーム
    // (TYPE=01, SEQ=00) の先頭が偽の LEN = 0x0100 と読まれ、後続を抱え込む。
    // 偽フレームが破綻するか (後続のトラフィック) タイムアウトすると、抱えたフレームを取り出す。
    Capture cap;
    uart_protocol_t p;
    uart_proto_init(&p, nullptr, on_rx, &cap);
    uint8_t bad[sizeof(kDataAB)];
    std::memcpy(bad, kDataAB, sizeof(bad));
    bad[8] = 0xAA;
    feed(p, bad);
    feed(p, kDataAB);
    feed(p, kAck5);
    CHECK(cap.rx.empty()); // 偽フレームの中で保留中
    uart_proto_set_rx_timeout(&p, 2);
    uart_proto_poll(&p, 50);
    CHECK(uart_proto_poll(&p, 52));
    REQUIRE_EQ(cap.rx.size(), 2u);
    CHECK_EQ(cap.rx[0].type, (uint8_t)UART_PKT_TYPE_DATA);
    CHECK_MEM_EQ(cap.rx[0].payload, "AB", 2);
    CHECK_EQ(cap.rx[1].type, (uint8_t)UART_PKT_TYPE_ACK);
    // タイムアウトがなくても、後続のトラフィックで偽フレームが破綻すれば取り出せる
    Capture cap2;
    uart_protocol_t q;
    uart_proto_init(&q, nullptr, on_rx, &cap2);
    feed(q, bad);
    feed(q, kDataAB);
    for (int i = 0; i < 60 && cap2.rx.empty(); ++i)
        feed(q, kPing);
    REQUIRE(!cap2.rx.empty());
    CHECK_EQ(cap2.rx[0].type, (uint8_t)UART_PKT_TYPE_DATA);
}

TEST_CASE(sequence_numbers) {
    Capture cap;
    uart_protocol_t p;
    uart_proto_init(&p, on_tx, nullptr, &cap);
    p.next_tx_seq = 254;
    CHECK(uart_proto_send_ping(&p));
    CHECK(uart_proto_send_ping(&p));
    CHECK(uart_proto_send_ping(&p));
    REQUIRE_EQ(cap.tx.size(), 3 * sizeof(kPing));
    CHECK_EQ(cap.tx[2], 254u);
    CHECK_EQ(cap.tx[2 + 8], 255u);
    CHECK_EQ(cap.tx[2 + 16], 0u); // 255 の次は 0
    // 明示 SEQ は自動採番を進めない
    CHECK(uart_proto_send_seq(&p, UART_PKT_TYPE_DATA, 0x77, nullptr, 0));
    CHECK_EQ(p.next_tx_seq, 1u);
    // 失敗した送信は SEQ を消費しない
    static uint8_t big[UART_PROTO_MAX_PAYLOAD + 1];
    CHECK(!uart_proto_send(&p, UART_PKT_TYPE_DATA, big, UART_PROTO_MAX_PAYLOAD + 1));
    CHECK_EQ(p.next_tx_seq, 1u);
    uart_protocol_t no_tx;
    uart_proto_init(&no_tx, nullptr, nullptr, nullptr);
    CHECK(!uart_proto_send_ping(&no_tx));
    CHECK_EQ(no_tx.next_tx_seq, 0u);
    uart_proto_stats_t s{};
    uart_proto_get_stats(&p, &s);
    CHECK_EQ(s.tx_packets, 4u);
    CHECK_EQ(s.tx_bytes, (uint32_t)(4 * sizeof(kPing)));
}

TEST_CASE(ack_nack_payload_layout) {
    Capture cap;
    uart_protocol_t p;
    uart_proto_init(&p, on_tx, nullptr, &cap);
    p.next_tx_seq = 5;
    CHECK(uart_proto_send_ack(&p, 0x2A));
    CHECK_MEM_EQ(cap.tx.data(), kAck5, sizeof(kAck5));
    cap.tx.clear();
    CHECK(uart_proto_send_nack(&p, 0x2A));
    CHECK_EQ(cap.tx[1], (uint8_t)UART_PKT_TYPE_NACK);
    CHECK_EQ(cap.tx[5], 0x2Au);
}

TEST_CASE(auto_pong_echoes_seq_and_payload) {
    Capture cap;
    uart_protocol_t p;
    uart_proto_init(&p, on_tx, on_rx, &cap);
    uint8_t ping[16];
    size_t n = 0;
    REQUIRE(uart_proto_encode(UART_PKT_TYPE_PING, 7, (const uint8_t *)"hi", 2, ping, &n));
    feed(p, ping, n);
    CHECK(cap.tx.empty()); // 既定は自動応答しない
    CHECK_EQ(cap.rx.size(), 1u);
    uart_proto_set_auto_pong(&p, true);
    feed(p, ping, n);
    CHECK_EQ(cap.rx.size(), 2u); // PING はアプリにも通知
    REQUIRE_EQ(cap.tx.size(), sizeof(kPongHi));
    CHECK_MEM_EQ(cap.tx.data(), kPongHi, sizeof(kPongHi));
    CHECK_EQ(p.next_tx_seq, 0u); // PONG は PING の SEQ を使う
}

TEST_CASE(reset_rx_and_null_safety) {
    Capture cap;
    uart_protocol_t p;
    uart_proto_init(&p, nullptr, on_rx, &cap);
    feed(p, kDataAB, 6);
    uart_proto_reset_rx(&p);
    feed(p, kDataAB + 6, sizeof(kDataAB) - 6); // 後半だけでは受からない
    CHECK(cap.rx.empty());
    feed(p, kDataAB);
    CHECK_EQ(cap.rx.size(), 1u);
    CHECK_EQ(uart_proto_parse_byte(nullptr, 0xAA), UART_PARSE_ERR_FORMAT);
    CHECK_EQ(uart_proto_process_bytes(nullptr, kDataAB, 3), 0u);
    CHECK_EQ(uart_proto_process_bytes(&p, nullptr, 3), 0u);
    uart_proto_init(nullptr, nullptr, nullptr, nullptr);
    uart_proto_reset_rx(nullptr);
    uart_proto_reset_stats(nullptr);
    uart_proto_get_stats(nullptr, nullptr);
    uart_proto_set_rx_timeout(nullptr, 1);
    uart_proto_set_auto_pong(nullptr, true);
    CHECK(!uart_proto_send(nullptr, 1, nullptr, 0));
    uart_proto_reset_stats(&p);
    uart_proto_stats_t s{};
    uart_proto_get_stats(&p, &s);
    CHECK_EQ(s.rx_bytes, 0u);
    CHECK_EQ(s.rx_packets, 0u);
}

TEST_CASE(cpp_wrapper_rejects_oversize_and_is_not_copyable) {
    static_assert(!std::is_copy_constructible_v<uart_protocol::Protocol>);
    static_assert(!std::is_move_constructible_v<uart_protocol::Protocol>);
    std::vector<uint8_t> wire;
    uart_protocol::Protocol node([&wire](const uint8_t *d, size_t n) { wire.insert(wire.end(), d, d + n); });
    // 旧実装は uint16_t へ切り詰めて 70000 -> 4464 バイトを黙って送っていた
    std::vector<uint8_t> huge(70000, 0x11);
    CHECK(!node.send(UART_PKT_TYPE_DATA, huge));
    CHECK(wire.empty());
    std::vector<uint8_t> max(UART_PROTO_MAX_PAYLOAD, 0x22);
    CHECK(node.send(UART_PKT_TYPE_DATA, max));
    CHECK_EQ(wire.size(), (size_t)UART_PROTO_MAX_FRAME_SIZE);
    int received = 0;
    uart_protocol::Protocol peer(nullptr, [&received](const uart_packet_t &pkt) {
        received += pkt.length == UART_PROTO_MAX_PAYLOAD ? 1 : 100;
    });
    CHECK_EQ(peer.process_bytes(wire), 1u);
    CHECK_EQ(received, 1);
    peer.set_rx_timeout(3);
    peer.parse_byte(0xAA);
    peer.poll(0);
    CHECK(peer.poll(3));
    CHECK_EQ(peer.get_stats().rx_timeouts, 1u);
}

EMBED_TEST_MAIN()
