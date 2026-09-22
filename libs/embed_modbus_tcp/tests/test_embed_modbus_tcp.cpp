#include "embed_modbus/modbus_rtu.h"
#include "embed_modbus_tcp/embed_modbus_tcp.h"
#include "embed_test/embed_test.hpp"
#include <cstring>
static unsigned sent = 0;
static void capture(const uint8_t *p, size_t n, void *) {
    CHECK_EQ(n, 9u);
    CHECK_EQ(p[7], 0xc1u);
    ++sent;
}
TEST_CASE(frames_and_connections) {
    modbus_slave_t slave{};
    modbus_slave_init(&slave, 1, nullptr, nullptr);
    uint8_t frame[8] = {}, out[260];
    embed_mbap h{42, 2, 1}, decoded{};
    CHECK_EQ(embed_mbap_encode(&h, frame, sizeof(frame)), 7u);
    frame[7] = 0x41;
    CHECK_EQ(embed_modbus_tcp_handle(&slave, frame, 8, out, sizeof(out)), 9u);
    CHECK_EQ(embed_mbap_decode(out, 9, &decoded), 7u);
    CHECK_EQ(decoded.transaction, 42u);
    for (size_t n = 0; n < 8; ++n)
        CHECK_EQ(embed_modbus_tcp_handle(&slave, frame, n, out, sizeof(out)), 0u);
    embed_modbus_tcp_connection a{}, b{};
    CHECK(embed_modbus_tcp_feed(&a, &slave, frame, 3, capture, nullptr));
    CHECK(embed_modbus_tcp_feed(&b, &slave, frame, 8, capture, nullptr));
    CHECK_EQ(sent, 1u);
    CHECK(embed_modbus_tcp_feed(&a, &slave, frame + 3, 5, capture, nullptr));
    CHECK_EQ(sent, 2u);
    uint8_t pair[16];
    memcpy(pair, frame, 8);
    memcpy(pair + 8, frame, 8);
    CHECK(embed_modbus_tcp_feed(&a, &slave, pair, 16, capture, nullptr));
    CHECK_EQ(sent, 4u);
    frame[2] = 1;
    CHECK(!embed_modbus_tcp_feed(&a, &slave, frame, 8, capture, nullptr));
}
TEST_CASE(transaction_wrap_and_exhaustion) {
    embed_modbus_transactions t{};
    t.next = 65535;
    uint16_t id = 0;
    CHECK(embed_modbus_transaction_begin(&t, &id));
    CHECK_EQ(id, 65535u);
    CHECK(embed_modbus_transaction_begin(&t, &id));
    CHECK_EQ(id, 0u);
    for (unsigned i = 2; i < 16; ++i)
        CHECK(embed_modbus_transaction_begin(&t, &id));
    CHECK(!embed_modbus_transaction_begin(&t, &id));
    CHECK(embed_modbus_transaction_end(&t, 0));
    CHECK(!embed_modbus_transaction_end(&t, 0));
    CHECK(embed_modbus_transaction_begin(&t, &id));
}

TEST_CASE(rtu_over_tcp_crc_and_bounds) {
    modbus_slave_t slave{};
    modbus_slave_init(&slave, 1, nullptr, nullptr);
    uint8_t pdu[] = {0x41}, req[256], out[257];
    size_t n = 0;
    REQUIRE(modbus_rtu_encode_frame(1, pdu, 1, req, &n));
    CHECK_EQ(embed_modbus_rtu_tcp_handle(&slave, req, n, out, 256), 5u);
    CHECK_EQ(embed_modbus_rtu_tcp_handle(&slave, req, n, out, 255), 0u);
    req[n - 1] ^= 1;
    CHECK_EQ(embed_modbus_rtu_tcp_handle(&slave, req, n, out, 256), 0u);
    embed_mbap h{0, 255, 1};
    CHECK_EQ(embed_mbap_encode(&h, out, sizeof(out)), 0u);
}
EMBED_TEST_MAIN()
