// uart_arq (Stop-and-Wait ARQ) を、フレーム単位で損失を注入できる仮想回線で検証する。
#include "uart_protocol/uart_arq.h"
#include "embed_test/embed_test.hpp"
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace {
using Frame = std::vector<uint8_t>;

struct Node {
    uart_protocol_t proto{};
    uart_arq_t arq{};
    std::deque<Frame> outbox;               // 回線上のフレーム (送信順)
    std::function<bool(const Frame &)> drop; // true を返したフレームは失われる
    unsigned sent_frames = 0;
    std::vector<std::string> received;       // アプリへ渡ったペイロード
    std::vector<std::pair<uint8_t, bool>> results;
};

void node_tx(const uint8_t *d, size_t n, void *u) {
    auto *node = static_cast<Node *>(u);
    Frame f(d, d + n);
    node->sent_frames++;
    if (node->drop && node->drop(f))
        return;
    node->outbox.push_back(f);
}
void node_rx(const uart_packet_t *p, void *u) {
    auto *node = static_cast<Node *>(u);
    node->received.emplace_back((const char *)p->payload, p->length);
}
void node_result(uint8_t seq, bool ok, void *u) {
    static_cast<Node *>(u)->results.emplace_back(seq, ok);
}

void setup(Node &n, uint32_t timeout, uint8_t retries) {
    uart_proto_init(&n.proto, node_tx, uart_arq_proto_rx, &n.arq);
    n.proto.user_data = &n; // tx は Node が受け、rx は ARQ が受ける
    n.proto.rx_cb = [](const uart_packet_t *p, void *u) {
        uart_arq_on_packet(&static_cast<Node *>(u)->arq, p);
    };
    REQUIRE(uart_arq_init(&n.arq, &n.proto, timeout, retries));
    uart_arq_set_callbacks(&n.arq, node_rx, node_result, &n);
}

/* 回線上のフレームを相手へ届ける (届けた結果の ACK もさらに届ける) */
void pump(Node &a, Node &b) {
    while (!a.outbox.empty() || !b.outbox.empty()) {
        while (!a.outbox.empty()) {
            Frame f = a.outbox.front();
            a.outbox.pop_front();
            uart_proto_process_bytes(&b.proto, f.data(), f.size());
        }
        while (!b.outbox.empty()) {
            Frame f = b.outbox.front();
            b.outbox.pop_front();
            uart_proto_process_bytes(&a.proto, f.data(), f.size());
        }
    }
}

bool send_text(Node &n, const char *text, uint32_t now) {
    return uart_arq_send(&n.arq, UART_PKT_TYPE_DATA, (const uint8_t *)text,
                         (uint16_t)std::strlen(text), now, nullptr);
}
} // namespace

TEST_CASE(delivery_without_loss) {
    Node a, b;
    setup(a, 50, 3);
    setup(b, 50, 3);
    uint8_t seq = 99;
    REQUIRE(uart_arq_send(&a.arq, UART_PKT_TYPE_DATA, (const uint8_t *)"hello", 5, 0, &seq));
    CHECK_EQ(seq, 0u);
    CHECK(uart_arq_busy(&a.arq));
    pump(a, b);
    REQUIRE_EQ(b.received.size(), 1u);
    CHECK_STR_EQ(b.received[0].c_str(), "hello");
    REQUIRE_EQ(a.results.size(), 1u);
    CHECK_EQ(a.results[0].first, 0u);
    CHECK(a.results[0].second);
    CHECK(!uart_arq_busy(&a.arq));
    CHECK_EQ(a.arq.stats.delivered, 1u);
    CHECK_EQ(a.arq.stats.retransmits, 0u);
    CHECK_EQ(b.arq.stats.rx_delivered, 1u);
    // 次の送信は SEQ 1
    REQUIRE(uart_arq_send(&a.arq, UART_PKT_TYPE_EVENT, (const uint8_t *)"x", 1, 0, &seq));
    CHECK_EQ(seq, 1u);
}

TEST_CASE(lost_data_is_retransmitted_at_timeout) {
    Node a, b;
    setup(a, 50, 3);
    setup(b, 50, 3);
    int data_frames = 0;
    a.drop = [&data_frames](const Frame &f) { return f[1] == UART_PKT_TYPE_DATA && ++data_frames == 1; };
    REQUIRE(send_text(a, "m1", 1000));
    pump(a, b);
    CHECK(b.received.empty());
    uart_arq_poll(&a.arq, 1049); // 49 ms: まだ再送しない
    CHECK_EQ(a.sent_frames, 1u);
    uart_arq_poll(&a.arq, 1050); // 50 ms ちょうどで再送
    CHECK_EQ(a.sent_frames, 2u);
    pump(a, b);
    CHECK_EQ(b.received.size(), 1u);
    REQUIRE_EQ(a.results.size(), 1u);
    CHECK(a.results[0].second);
    CHECK_EQ(a.arq.stats.retransmits, 1u);
}

TEST_CASE(lost_ack_causes_duplicate_that_is_filtered) {
    Node a, b;
    setup(a, 20, 3);
    setup(b, 20, 3);
    int acks = 0;
    b.drop = [&acks](const Frame &f) { return f[1] == UART_PKT_TYPE_ACK && ++acks == 1; };
    REQUIRE(send_text(a, "once", 0));
    pump(a, b);
    CHECK_EQ(b.received.size(), 1u);
    CHECK(a.results.empty());
    uart_arq_poll(&a.arq, 20);
    pump(a, b);
    CHECK_EQ(b.received.size(), 1u); // アプリへは 1 回だけ
    CHECK_EQ(b.arq.stats.rx_duplicates, 1u);
    REQUIRE_EQ(a.results.size(), 1u);
    CHECK(a.results[0].second);
}

TEST_CASE(gives_up_after_max_retries) {
    Node a, b;
    setup(a, 10, 2);
    setup(b, 10, 2);
    a.drop = [](const Frame &) { return true; };
    REQUIRE(send_text(a, "lost", 0));
    for (uint32_t t = 1; t <= 100; ++t)
        uart_arq_poll(&a.arq, t);
    CHECK_EQ(a.sent_frames, 3u); // 初回 + 再送 2 回
    REQUIRE_EQ(a.results.size(), 1u);
    CHECK(!a.results[0].second);
    CHECK_EQ(a.arq.stats.failed, 1u);
    CHECK(!uart_arq_busy(&a.arq));
    CHECK(send_text(a, "next", 100)); // 失敗後は次を送れる
}

TEST_CASE(zero_retries_fails_on_first_timeout) {
    Node a, b;
    setup(a, 10, 0);
    a.drop = [](const Frame &) { return true; };
    REQUIRE(send_text(a, "x", 0));
    uart_arq_poll(&a.arq, 9);
    CHECK(a.results.empty());
    uart_arq_poll(&a.arq, 10);
    CHECK_EQ(a.sent_frames, 1u);
    REQUIRE_EQ(a.results.size(), 1u);
    CHECK(!a.results[0].second);
}

TEST_CASE(nack_triggers_immediate_retransmit_and_stale_acks_are_ignored) {
    Node a, b;
    setup(a, 1000, 3);
    REQUIRE(send_text(a, "n", 5));
    a.outbox.clear();
    uart_packet_t pkt{};
    pkt.type = UART_PKT_TYPE_NACK;
    pkt.length = 1;
    pkt.payload[0] = 0; // 送信中の SEQ
    uart_arq_on_packet(&a.arq, &pkt);
    CHECK_EQ(a.sent_frames, 2u);
    CHECK_EQ(a.arq.stats.retransmits, 1u);
    pkt.type = UART_PKT_TYPE_ACK;
    pkt.payload[0] = 7; // 別の SEQ への ACK は無視
    uart_arq_on_packet(&a.arq, &pkt);
    CHECK(uart_arq_busy(&a.arq));
    pkt.length = 2; // 長さが 1 でない ACK も無視
    pkt.payload[0] = 0;
    uart_arq_on_packet(&a.arq, &pkt);
    CHECK(uart_arq_busy(&a.arq));
    pkt.length = 1;
    uart_arq_on_packet(&a.arq, &pkt);
    CHECK(!uart_arq_busy(&a.arq));
    uart_arq_on_packet(&a.arq, &pkt); // 送信待ちがないときの ACK は無視
    CHECK_EQ(a.results.size(), 1u);
}

TEST_CASE(send_validation) {
    Node a;
    setup(a, 10, 1);
    CHECK(!uart_arq_send(&a.arq, UART_PKT_TYPE_ACK, nullptr, 0, 0, nullptr));
    CHECK(!uart_arq_send(&a.arq, UART_PKT_TYPE_PING, nullptr, 0, 0, nullptr));
    CHECK(!uart_arq_send(&a.arq, UART_PKT_TYPE_DATA, nullptr, 3, 0, nullptr));
    static uint8_t big[UART_PROTO_MAX_PAYLOAD + 1];
    CHECK(!uart_arq_send(&a.arq, UART_PKT_TYPE_DATA, big, UART_PROTO_MAX_PAYLOAD + 1, 0, nullptr));
    CHECK(uart_arq_send(&a.arq, UART_PKT_TYPE_DATA, nullptr, 0, 0, nullptr));
    CHECK(!uart_arq_send(&a.arq, UART_PKT_TYPE_DATA, nullptr, 0, 0, nullptr)); // busy
    CHECK_EQ(a.arq.next_seq, 1u); // 失敗した送信は SEQ を消費しない
    uart_arq_reset(&a.arq);
    CHECK(!uart_arq_busy(&a.arq));
    uart_arq_t arq;
    uart_protocol_t proto;
    uart_proto_init(&proto, nullptr, nullptr, nullptr);
    CHECK(!uart_arq_init(&arq, &proto, 0, 1)); // タイムアウト 0 は不可
    CHECK(!uart_arq_init(&arq, nullptr, 10, 1));
    CHECK(!uart_arq_init(nullptr, &proto, 10, 1));
    REQUIRE(uart_arq_init(&arq, &proto, 10, 1));
    CHECK(!uart_arq_send(&arq, UART_PKT_TYPE_DATA, nullptr, 0, 0, nullptr)); // tx_fn なし
    CHECK(!uart_arq_busy(&arq));
    CHECK(!uart_arq_busy(nullptr));
    uart_arq_poll(nullptr, 0);
    uart_arq_on_packet(nullptr, nullptr);
}

TEST_CASE(ping_passes_through_without_ack) {
    Node a, b;
    setup(a, 10, 1);
    setup(b, 10, 1);
    CHECK(uart_proto_send_ping(&a.proto));
    pump(a, b);
    CHECK_EQ(b.received.size(), 1u);
    CHECK_EQ(b.sent_frames, 0u); // PING には ACK を返さない
}

TEST_CASE(bidirectional_transfer_over_lossy_link) {
    Node a, b;
    setup(a, 5, 30);
    setup(b, 5, 30);
    uint32_t rng = 4242;
    auto lossy = [&rng](const Frame &) {
        rng = rng * 1664525u + 1013904223u;
        return (rng >> 24) < 77; // 約 30% のフレームを失う
    };
    a.drop = lossy;
    b.drop = lossy;
    const int kMessages = 40;
    int next_a = 0, next_b = 0;
    for (uint32_t now = 0; now < 20000 && (a.results.size() < kMessages || b.results.size() < kMessages); ++now) {
        if (!uart_arq_busy(&a.arq) && next_a < kMessages)
            send_text(a, ("A" + std::to_string(next_a++)).c_str(), now);
        if (!uart_arq_busy(&b.arq) && next_b < kMessages)
            send_text(b, ("B" + std::to_string(next_b++)).c_str(), now);
        pump(a, b);
        uart_arq_poll(&a.arq, now);
        uart_arq_poll(&b.arq, now);
    }
    REQUIRE_EQ(a.results.size(), (size_t)kMessages);
    REQUIRE_EQ(b.results.size(), (size_t)kMessages);
    for (const auto &r : a.results)
        CHECK(r.second);
    // 損失があっても、順序どおり・重複なしで 1 回ずつ届く
    REQUIRE_EQ(b.received.size(), (size_t)kMessages);
    REQUIRE_EQ(a.received.size(), (size_t)kMessages);
    for (int i = 0; i < kMessages; ++i) {
        CHECK(b.received[i] == "A" + std::to_string(i));
        CHECK(a.received[i] == "B" + std::to_string(i));
    }
    CHECK(a.arq.stats.retransmits > 0);
    CHECK(b.arq.stats.rx_duplicates > 0);
}

EMBED_TEST_MAIN()
