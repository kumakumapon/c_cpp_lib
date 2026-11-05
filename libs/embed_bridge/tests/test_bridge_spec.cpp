// ルーターの各エンドポイント (Modbus / OPC UA / EtherNet/IP / CANopen / MQTT / 生変数) の
// 読み書き・型変換・失敗時の扱いと、周期・変化検出・不感帯・MQTT 受信の振る舞いを固定する。
#include "embed_bridge/bridge_router.h"
#include "embed_bridge/embed_bridge.hpp"
#include "embed_test/embed_test.hpp"
#include <cstring>
#include <string>
#include <vector>

namespace {
bridge_rule_t rule(bridge_endpoint_t src, bridge_data_type_t st, bridge_endpoint_t dst,
                   bridge_data_type_t dt, bridge_sync_mode_t mode = BRIDGE_SYNC_ALWAYS) {
    bridge_rule_t r{};
    r.src = src;
    r.dst = dst;
    r.src_type = st;
    r.dst_type = dt;
    r.sync_mode = mode;
    return r;
}
bridge_endpoint_t raw(void *p) {
    bridge_endpoint_t e{};
    e.proto = BRIDGE_PROTO_RAW_VAR;
    e.target.raw_ptr = p;
    return e;
}
bridge_endpoint_t modbus(uint16_t addr) {
    bridge_endpoint_t e{};
    e.proto = BRIDGE_PROTO_MODBUS_REG;
    e.target.modbus_addr = addr;
    return e;
}
bridge_endpoint_t mqtt(const char *topic) {
    bridge_endpoint_t e{};
    e.proto = BRIDGE_PROTO_MQTT_TOPIC;
    std::strncpy(e.target.mqtt_topic, topic, sizeof(e.target.mqtt_topic) - 1);
    return e;
}
bridge_endpoint_t eip(uint16_t instance, uint16_t offset) {
    bridge_endpoint_t e{};
    e.proto = BRIDGE_PROTO_EIP_ASSEMBLY;
    e.target.eip.instance_id = instance;
    e.target.eip.offset = offset;
    return e;
}
bridge_endpoint_t canopen(uint16_t index, uint8_t sub) {
    bridge_endpoint_t e{};
    e.proto = BRIDGE_PROTO_CANOPEN_OD;
    e.target.canopen.index = index;
    e.target.canopen.subindex = sub;
    return e;
}

/* MQTT PUBLISH (QoS 0) のペイロードを取り出す */
std::vector<std::string> published;
void capture_publish(const uint8_t *d, size_t n, void *) {
    if (n < 2 || (d[0] & 0xF0) != 0x30)
        return;
    size_t i = 1, remaining = 0, mult = 1;
    while (i < n) {
        remaining += (d[i] & 0x7F) * mult;
        mult *= 128;
        if (!(d[i++] & 0x80))
            break;
    }
    const size_t topic_len = (size_t)(d[i] << 8 | d[i + 1]);
    const size_t payload_at = i + 2 + topic_len;
    published.emplace_back((const char *)d + payload_at, i + remaining - payload_at);
}
} // namespace

TEST_CASE(modbus_bool_destination_is_written) {
    // 旧版は sz == 1 のとき何も書かずに成功を返していた
    uint16_t regs[4] = {7, 7, 7, 7};
    bool flag = true;
    bridge_router_t r;
    bridge_router_init(&r);
    bridge_router_bind_modbus(&r, regs, 4);
    auto rr = rule(raw(&flag), BRIDGE_TYPE_BOOL, modbus(2), BRIDGE_TYPE_BOOL);
    REQUIRE(bridge_router_add_rule(&r, &rr) >= 0);
    CHECK_EQ(bridge_router_sync_all(&r), 1u);
    CHECK_EQ(regs[2], 1u);
    flag = false;
    bridge_router_sync_all(&r);
    CHECK_EQ(regs[2], 0u);
    // Modbus から BOOL: 0 以外は 1
    regs[0] = 0x8000;
    uint8_t out = 0;
    auto back = rule(modbus(0), BRIDGE_TYPE_BOOL, raw(&out), BRIDGE_TYPE_BOOL);
    bridge_router_t r2;
    bridge_router_init(&r2);
    bridge_router_bind_modbus(&r2, regs, 4);
    REQUIRE(bridge_router_add_rule(&r2, &back) >= 0);
    bridge_router_sync_all(&r2);
    CHECK_EQ(out, 1u);
}

TEST_CASE(modbus_32bit_values_are_high_word_first) {
    uint16_t regs[4] = {};
    float f = 1.0f; // 0x3F800000
    int32_t i = -2; // 0xFFFFFFFE
    bridge_router_t r;
    bridge_router_init(&r);
    bridge_router_bind_modbus(&r, regs, 4);
    auto a = rule(raw(&f), BRIDGE_TYPE_FLOAT, modbus(0), BRIDGE_TYPE_FLOAT);
    auto b = rule(raw(&i), BRIDGE_TYPE_INT32, modbus(2), BRIDGE_TYPE_INT32);
    REQUIRE(bridge_router_add_rule(&r, &a) >= 0);
    REQUIRE(bridge_router_add_rule(&r, &b) >= 0);
    CHECK_EQ(bridge_router_sync_all(&r), 2u);
    CHECK_EQ(regs[0], 0x3F80u);
    CHECK_EQ(regs[1], 0x0000u);
    CHECK_EQ(regs[2], 0xFFFFu);
    CHECK_EQ(regs[3], 0xFFFEu);
    // 読み戻し
    float f2 = 0;
    auto c = rule(modbus(0), BRIDGE_TYPE_FLOAT, raw(&f2), BRIDGE_TYPE_FLOAT);
    bridge_router_t r2;
    bridge_router_init(&r2);
    bridge_router_bind_modbus(&r2, regs, 4);
    REQUIRE(bridge_router_add_rule(&r2, &c) >= 0);
    bridge_router_sync_all(&r2);
    CHECK_EQ(f2, 1.0f);
    // 最後のレジスタから 32bit は範囲外
    auto d = rule(raw(&i), BRIDGE_TYPE_INT32, modbus(3), BRIDGE_TYPE_INT32);
    int idx = bridge_router_add_rule(&r2, &d);
    REQUIRE(idx >= 0);
    CHECK_EQ(bridge_router_sync_all(&r2), 1u); // c だけ
    CHECK_EQ(bridge_router_get_rule(&r2, idx)->error_count, 1u);
}

TEST_CASE(raw_bytes_use_data_size) {
    uint8_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8}, dst[8] = {};
    uint16_t regs[4] = {};
    bridge_router_t r;
    bridge_router_init(&r);
    bridge_router_bind_modbus(&r, regs, 4);
    auto a = rule(raw(src), BRIDGE_TYPE_RAW_BYTES, raw(dst), BRIDGE_TYPE_RAW_BYTES);
    a.data_size = 8;
    auto b = rule(raw(src), BRIDGE_TYPE_RAW_BYTES, modbus(0), BRIDGE_TYPE_RAW_BYTES);
    b.data_size = 8;
    REQUIRE(bridge_router_add_rule(&r, &a) >= 0);
    REQUIRE(bridge_router_add_rule(&r, &b) >= 0);
    CHECK_EQ(bridge_router_sync_all(&r), 2u);
    CHECK_MEM_EQ(dst, src, 8); // 旧版は 2 バイトしかコピーしなかった
    CHECK_EQ(regs[0], 0x0102u);
    CHECK_EQ(regs[3], 0x0708u);
    // 生バイトと数値の混在・長さ 0 は登録できない
    auto mixed = rule(raw(src), BRIDGE_TYPE_RAW_BYTES, raw(dst), BRIDGE_TYPE_UINT16);
    mixed.data_size = 2;
    CHECK(bridge_router_add_rule(&r, &mixed) < 0);
    auto zero = rule(raw(src), BRIDGE_TYPE_RAW_BYTES, raw(dst), BRIDGE_TYPE_RAW_BYTES);
    CHECK(bridge_router_add_rule(&r, &zero) < 0);
    // 奇数長は Modbus に入らない
    auto odd = rule(raw(src), BRIDGE_TYPE_RAW_BYTES, modbus(0), BRIDGE_TYPE_RAW_BYTES);
    odd.data_size = 3;
    int idx = bridge_router_add_rule(&r, &odd);
    REQUIRE(idx >= 0);
    bridge_router_sync_all(&r);
    CHECK_EQ(bridge_router_get_rule(&r, idx)->error_count, 1u);
}

TEST_CASE(periodic_rule_keeps_phase) {
    uint16_t v = 1, out = 0;
    bridge_router_t r;
    bridge_router_init(&r);
    auto a = rule(raw(&v), BRIDGE_TYPE_UINT16, raw(&out), BRIDGE_TYPE_UINT16, BRIDGE_SYNC_PERIODIC);
    a.interval_ms = 100;
    REQUIRE(bridge_router_add_rule(&r, &a) >= 0);
    CHECK_EQ(bridge_router_tick(&r, 99), 0u);
    CHECK_EQ(bridge_router_tick(&r, 51), 1u); // 150: 発火、残り 50
    CHECK_EQ(bridge_router_tick(&r, 49), 0u); // 199
    CHECK_EQ(bridge_router_tick(&r, 1), 1u);  // 200 (旧版は 0 に戻すので 250 までずれた)
    CHECK_EQ(bridge_router_tick(&r, 350), 1u); // 遅れても 1 回だけ、残り 50
    CHECK_EQ(bridge_router_tick(&r, 50), 1u);
}

TEST_CASE(on_change_with_deadband_in_destination_units) {
    uint16_t regs[2] = {50, 0}; // 0.5 ℃ 単位 (0.5 は 2 進で正確に表せるので境界を厳密に試せる)
    float temp = 0;
    bridge_router_t r;
    bridge_router_init(&r);
    bridge_router_bind_modbus(&r, regs, 2);
    auto a = rule(modbus(0), BRIDGE_TYPE_UINT16, raw(&temp), BRIDGE_TYPE_FLOAT, BRIDGE_SYNC_ON_CHANGE);
    a.scale = 0.5f;
    a.deadband = 1.0f; // 宛先の単位 (℃) で 1.0
    int idx = bridge_router_add_rule(&r, &a);
    REQUIRE(idx >= 0);
    CHECK_EQ(bridge_router_tick(&r, 1), 1u);
    CHECK_EQ(temp, 25.0f);
    regs[0] = 51; // 25.5: 差 0.5 は不感帯内
    CHECK_EQ(bridge_router_tick(&r, 1), 0u);
    regs[0] = 53; // 26.5: 差 1.5
    CHECK_EQ(bridge_router_tick(&r, 1), 1u);
    CHECK_EQ(temp, 26.5f);
    regs[0] = 51; // 25.5: 差 1.0 ちょうどは転送しない
    CHECK_EQ(bridge_router_tick(&r, 1), 0u);
    regs[0] = 50; // 25.0: 差 1.5
    CHECK_EQ(bridge_router_tick(&r, 1), 1u);
    CHECK_EQ(bridge_router_get_rule(&r, idx)->sync_count, 3u);
    auto bad = a;
    bad.deadband = -1.0f;
    CHECK(bridge_router_add_rule(&r, &bad) < 0);
}

TEST_CASE(rule_activation_and_lookup) {
    uint16_t v = 5, out = 0;
    bridge_router_t r;
    bridge_router_init(&r);
    auto a = rule(raw(&v), BRIDGE_TYPE_UINT16, raw(&out), BRIDGE_TYPE_UINT16, BRIDGE_SYNC_ON_CHANGE);
    int idx = bridge_router_add_rule(&r, &a);
    REQUIRE(idx >= 0);
    CHECK_EQ(bridge_router_tick(&r, 1), 1u);
    REQUIRE(bridge_router_set_rule_active(&r, idx, false));
    v = 6;
    CHECK_EQ(bridge_router_tick(&r, 1), 0u);
    CHECK_EQ(out, 5u);
    REQUIRE(bridge_router_set_rule_active(&r, idx, true));
    CHECK_EQ(bridge_router_tick(&r, 1), 1u);
    CHECK_EQ(out, 6u);
    CHECK_EQ(bridge_router_tick(&r, 1), 0u);
    REQUIRE(bridge_router_set_rule_active(&r, idx, false));
    REQUIRE(bridge_router_set_rule_active(&r, idx, true)); // 再開時は値が同じでも 1 回送る
    CHECK_EQ(bridge_router_tick(&r, 1), 1u);
    CHECK(!bridge_router_set_rule_active(&r, 5, true));
    CHECK(!bridge_router_set_rule_active(&r, -1, true));
    CHECK(bridge_router_get_rule(&r, 1) == nullptr);
    CHECK(bridge_router_get_rule(nullptr, 0) == nullptr);
}

TEST_CASE(mqtt_messages_write_to_devices) {
    uint16_t regs[4] = {};
    bridge_router_t r;
    bridge_router_init(&r);
    bridge_router_bind_modbus(&r, regs, 4);
    auto sp = rule(mqtt("plant/setpoint"), BRIDGE_TYPE_FLOAT, modbus(1), BRIDGE_TYPE_UINT16);
    sp.scale = 10.0f; // 21.5 ℃ -> 215
    auto run = rule(mqtt("plant/run"), BRIDGE_TYPE_BOOL, modbus(2), BRIDGE_TYPE_BOOL);
    int sp_idx = bridge_router_add_rule(&r, &sp);
    REQUIRE(sp_idx >= 0);
    REQUIRE(bridge_router_add_rule(&r, &run) >= 0);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "plant/setpoint", " 21.5\n", 6), 1u);
    CHECK_EQ(regs[1], 215u);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "plant/run", "ON", 2), 1u);
    CHECK_EQ(regs[2], 1u);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "plant/run", "false", 5), 1u);
    CHECK_EQ(regs[2], 0u);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "plant/setpoint", "0x10", 4), 1u); // 16 -> 160
    CHECK_EQ(regs[1], 160u);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "plant/setpoint", "abc", 3), 0u);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "plant/setpoint", "12x", 3), 0u);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "plant/setpoint", "nan", 3), 0u);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "plant/setpoint", "", 0), 0u);
    CHECK_EQ(bridge_router_get_rule(&r, sp_idx)->error_count, 4u);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "plant/other", "1", 1), 0u);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "plant/setpoin", "1", 1), 0u); // 前方一致はしない
    // 周期処理は MQTT 送信元のルールを読まない (エラーにもしない)
    CHECK_EQ(bridge_router_sync_all(&r), 0u);
    CHECK_EQ(bridge_router_get_rule(&r, sp_idx)->error_count, 4u);
    bridge_router_set_rule_active(&r, sp_idx, false);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "plant/setpoint", "1", 1), 0u);
    CHECK_EQ(bridge_router_on_mqtt_message(nullptr, "plant/run", "1", 1), 0u);
    // NUL 終端でないトピック (mqtt_message_t そのまま)
    const char frame_topic[] = "plant/runXYZ";
    CHECK_EQ(bridge_router_on_mqtt_message_n(&r, frame_topic, 9, "1", 1), 1u);
    CHECK_EQ(regs[2], 1u);
    CHECK_EQ(bridge_router_on_mqtt_message_n(&r, frame_topic, 10, "1", 1), 0u);
}

TEST_CASE(mqtt_destination_payload_formats) {
    mqtt_client_t client;
    mqtt_client_init(&client, capture_publish, nullptr);
    client.state = MQTT_STATE_CONNECTED;
    published.clear();
    float f = 25.0f;
    int32_t i = -5;
    bool b = true;
    uint8_t blob[3] = {0x01, 0x00, 0xFF};
    bridge_router_t r;
    bridge_router_init(&r);
    bridge_router_bind_mqtt(&r, &client);
    auto a = rule(raw(&f), BRIDGE_TYPE_FLOAT, mqtt("s/f"), BRIDGE_TYPE_FLOAT);
    auto c = rule(raw(&i), BRIDGE_TYPE_INT32, mqtt("s/i"), BRIDGE_TYPE_INT32);
    auto d = rule(raw(&b), BRIDGE_TYPE_BOOL, mqtt("s/b"), BRIDGE_TYPE_BOOL);
    auto e = rule(raw(blob), BRIDGE_TYPE_RAW_BYTES, mqtt("s/raw"), BRIDGE_TYPE_RAW_BYTES);
    e.data_size = 3;
    for (auto *x : {&a, &c, &d, &e})
        REQUIRE(bridge_router_add_rule(&r, x) >= 0);
    CHECK_EQ(bridge_router_sync_all(&r), 4u);
    REQUIRE_EQ(published.size(), 4u);
    CHECK(published[0] == "25.00");
    CHECK(published[1] == "-5");
    CHECK(published[2] == "1");
    CHECK(published[3] == std::string("\x01\x00\xFF", 3));
    // 未接続なら失敗
    client.state = MQTT_STATE_DISCONNECTED;
    CHECK_EQ(bridge_router_sync_all(&r), 0u);
}

TEST_CASE(eip_writes_stay_inside_the_assembly) {
    cip_identity_attr_t id{};
    eip_adapter_t adapter;
    eip_adapter_init(&adapter, &id);
    REQUIRE(eip_adapter_add_assembly(&adapter, 100, 4));
    uint16_t v = 0xABCD;
    bridge_router_t r;
    bridge_router_init(&r);
    bridge_router_bind_eip(&r, &adapter);
    auto ok = rule(raw(&v), BRIDGE_TYPE_UINT16, eip(100, 2), BRIDGE_TYPE_UINT16);
    auto beyond = rule(raw(&v), BRIDGE_TYPE_UINT16, eip(100, 3), BRIDGE_TYPE_UINT16);
    auto missing = rule(raw(&v), BRIDGE_TYPE_UINT16, eip(101, 0), BRIDGE_TYPE_UINT16);
    REQUIRE(bridge_router_add_rule(&r, &ok) >= 0);
    int b = bridge_router_add_rule(&r, &beyond);
    int m = bridge_router_add_rule(&r, &missing);
    CHECK_EQ(bridge_router_sync_all(&r), 1u); // 旧版は範囲外も成功扱いで黙って切り捨てていた
    CHECK_EQ(bridge_router_get_rule(&r, b)->error_count, 1u);
    CHECK_EQ(bridge_router_get_rule(&r, m)->error_count, 1u);
    uint8_t data[EIP_MAX_ASSEMBLY_SIZE];
    size_t n = 0;
    REQUIRE(eip_adapter_get_assembly_data(&adapter, 100, data, &n));
    uint16_t back;
    std::memcpy(&back, data + 2, 2);
    CHECK_EQ(back, 0xABCDu);
}

TEST_CASE(canopen_lengths_must_match) {
    canopen_node_t node;
    canopen_node_init(&node, 3, "gw");
    int32_t speed = 70000;
    uint16_t small = 0;
    canopen_od_add_entry(&node.od, 0x2000, 0, OD_TYPE_INT32, OD_ACCESS_RW, &speed, 4);
    canopen_od_add_entry(&node.od, 0x2001, 0, OD_TYPE_UINT16, OD_ACCESS_RW, &small, 2);
    int16_t out16 = 0;
    int32_t out32 = 0;
    bridge_router_t r;
    bridge_router_init(&r);
    bridge_router_bind_canopen(&r, &node);
    auto narrow = rule(canopen(0x2000, 0), BRIDGE_TYPE_INT16, raw(&out16), BRIDGE_TYPE_INT16);
    auto exact = rule(canopen(0x2000, 0), BRIDGE_TYPE_INT32, raw(&out32), BRIDGE_TYPE_INT32);
    auto wide_write = rule(raw(&out32), BRIDGE_TYPE_INT32, canopen(0x2001, 0), BRIDGE_TYPE_INT32);
    int n = bridge_router_add_rule(&r, &narrow);
    REQUIRE(bridge_router_add_rule(&r, &exact) >= 0);
    int w = bridge_router_add_rule(&r, &wide_write);
    CHECK_EQ(bridge_router_sync_all(&r), 1u);
    CHECK_EQ(out32, 70000);
    CHECK_EQ(out16, 0); // INT32 の下位だけを INT16 として読まない
    CHECK_EQ(bridge_router_get_rule(&r, n)->error_count, 1u);
    CHECK_EQ(bridge_router_get_rule(&r, w)->error_count, 1u);
    CHECK_EQ(small, 0u);
}

TEST_CASE(opcua_types_and_failures) {
    opcua_server_t server;
    opcua_server_init(&server, "opc.tcp://localhost:4840");
    opcua_nodeid_t node{};
    node.id_type = OPCUA_NODEID_NUMERIC;
    node.namespace_index = 1;
    node.id.numeric = 10;
    opcua_variant_t init{};
    init.type = OPCUA_TYPE_DOUBLE;
    init.value.double_val = 12.75;
    REQUIRE(opcua_server_add_variable(&server, &node, "Value", &init, 0x03));
    float out = 0;
    bridge_endpoint_t ua{};
    ua.proto = BRIDGE_PROTO_OPCUA_NODE;
    ua.target.opcua_node = node;
    bridge_router_t r;
    bridge_router_init(&r);
    bridge_router_bind_opcua(&r, &server);
    auto a = rule(ua, BRIDGE_TYPE_FLOAT, raw(&out), BRIDGE_TYPE_FLOAT);
    REQUIRE(bridge_router_add_rule(&r, &a) >= 0);
    CHECK_EQ(bridge_router_sync_all(&r), 1u);
    CHECK_NEAR(out, 12.75, 1e-6);
    // 存在しないノード・生バイトは失敗
    bridge_endpoint_t missing = ua;
    missing.target.opcua_node.id.numeric = 99;
    uint8_t blob[2] = {};
    auto b = rule(missing, BRIDGE_TYPE_FLOAT, raw(&out), BRIDGE_TYPE_FLOAT);
    auto c = rule(raw(blob), BRIDGE_TYPE_RAW_BYTES, ua, BRIDGE_TYPE_RAW_BYTES);
    c.data_size = 2;
    int bi = bridge_router_add_rule(&r, &b);
    int ci = bridge_router_add_rule(&r, &c);
    CHECK_EQ(bridge_router_sync_all(&r), 1u);
    CHECK_EQ(bridge_router_get_rule(&r, bi)->error_count, 1u);
    CHECK_EQ(bridge_router_get_rule(&r, ci)->error_count, 1u);
}

TEST_CASE(unbound_endpoints_fail) {
    uint16_t v = 1;
    bridge_router_t r;
    bridge_router_init(&r);
    const bridge_endpoint_t targets[] = {modbus(0), eip(1, 0), canopen(0x2000, 0), mqtt("t"), raw(nullptr)};
    for (const auto &t : targets) {
        auto x = rule(raw(&v), BRIDGE_TYPE_UINT16, t, BRIDGE_TYPE_UINT16);
        REQUIRE(bridge_router_add_rule(&r, &x) >= 0);
    }
    CHECK_EQ(bridge_router_sync_all(&r), 0u);
    for (int i = 0; i < 5; ++i)
        CHECK_EQ(bridge_router_get_rule(&r, i)->error_count, 1u);
    CHECK_EQ(bridge_router_sync_all(nullptr), 0u);
    CHECK_EQ(bridge_router_tick(nullptr, 1), 0u);
    CHECK(bridge_router_add_rule(nullptr, nullptr) < 0);
}

TEST_CASE(boundaries_of_registers_rules_and_values) {
    uint16_t regs[4] = {};
    bridge_router_t r;
    bridge_router_init(&r);
    bridge_router_bind_modbus(&r, regs, 4);
    uint32_t big = 0x11223344u, back = 0;
    // 32bit は最後から 2 番目のレジスタまで置ける
    auto ok = rule(raw(&big), BRIDGE_TYPE_UINT32, modbus(2), BRIDGE_TYPE_UINT32);
    auto edge = rule(raw(&big), BRIDGE_TYPE_UINT32, modbus(3), BRIDGE_TYPE_UINT32);
    auto beyond = rule(raw(&big), BRIDGE_TYPE_UINT16, modbus(4), BRIDGE_TYPE_UINT16);
    REQUIRE(bridge_router_add_rule(&r, &ok) >= 0);
    int e = bridge_router_add_rule(&r, &edge);
    int b = bridge_router_add_rule(&r, &beyond);
    CHECK_EQ(bridge_router_sync_all(&r), 1u);
    CHECK_EQ(regs[2], 0x1122u);
    CHECK_EQ(regs[3], 0x3344u);
    CHECK_EQ(bridge_router_get_rule(&r, e)->error_count, 1u);
    CHECK_EQ(bridge_router_get_rule(&r, b)->error_count, 1u);
    auto rd = rule(modbus(2), BRIDGE_TYPE_UINT32, raw(&back), BRIDGE_TYPE_UINT32);
    auto rd_edge = rule(modbus(3), BRIDGE_TYPE_UINT32, raw(&back), BRIDGE_TYPE_UINT32);
    bridge_router_t r2;
    bridge_router_init(&r2);
    bridge_router_bind_modbus(&r2, regs, 4);
    REQUIRE(bridge_router_add_rule(&r2, &rd) >= 0);
    int re = bridge_router_add_rule(&r2, &rd_edge);
    CHECK_EQ(bridge_router_sync_all(&r2), 1u);
    CHECK_EQ(back, 0x11223344u);
    CHECK_EQ(bridge_router_get_rule(&r2, re)->error_count, 1u);
    // ルール数の上限
    bridge_router_t full;
    bridge_router_init(&full);
    uint16_t v = 0, o = 0;
    auto x = rule(raw(&v), BRIDGE_TYPE_UINT16, raw(&o), BRIDGE_TYPE_UINT16);
    for (int i = 0; i < BRIDGE_MAX_RULES; ++i)
        CHECK_EQ(bridge_router_add_rule(&full, &x), i);
    CHECK_EQ(bridge_router_add_rule(&full, &x), -1);
    CHECK(bridge_router_get_rule(&full, BRIDGE_MAX_RULES - 1) != nullptr);
    CHECK(bridge_router_get_rule(&full, BRIDGE_MAX_RULES) == nullptr);
    CHECK(bridge_router_set_rule_active(&full, BRIDGE_MAX_RULES - 1, false));
    CHECK(!bridge_router_set_rule_active(&full, BRIDGE_MAX_RULES, false));
    // スケールとオフセットの加算 (offset は加える)
    float f = 10.0f, out = 0;
    auto sc = rule(raw(&f), BRIDGE_TYPE_FLOAT, raw(&out), BRIDGE_TYPE_FLOAT);
    sc.scale = 2.0f;
    sc.offset = 5.0f;
    bridge_router_t r3;
    bridge_router_init(&r3);
    REQUIRE(bridge_router_add_rule(&r3, &sc) >= 0);
    bridge_router_sync_all(&r3);
    CHECK_EQ(out, 25.0f);
    // 飽和の境界: int16 の上限ちょうどと +1
    int32_t src = 32767;
    int16_t d16 = 0;
    auto sat = rule(raw(&src), BRIDGE_TYPE_INT32, raw(&d16), BRIDGE_TYPE_INT16);
    bridge_router_t r4;
    bridge_router_init(&r4);
    REQUIRE(bridge_router_add_rule(&r4, &sat) >= 0);
    bridge_router_sync_all(&r4);
    CHECK_EQ(d16, 32767);
    src = 32768;
    bridge_router_sync_all(&r4);
    CHECK_EQ(d16, 32767);
    src = -32768;
    bridge_router_sync_all(&r4);
    CHECK_EQ(d16, -32768);
    // 0 以外はすべて BOOL の true (0.5 も)
    float tiny = 0.5f;
    uint8_t flag = 0;
    auto bo = rule(raw(&tiny), BRIDGE_TYPE_FLOAT, raw(&flag), BRIDGE_TYPE_BOOL);
    bridge_router_t r5;
    bridge_router_init(&r5);
    REQUIRE(bridge_router_add_rule(&r5, &bo) >= 0);
    bridge_router_sync_all(&r5);
    CHECK_EQ(flag, 1u);
    tiny = 0.0f;
    bridge_router_sync_all(&r5);
    CHECK_EQ(flag, 0u);
}

TEST_CASE(mqtt_number_parsing_edges) {
    uint16_t regs[2] = {};
    bridge_router_t r;
    bridge_router_init(&r);
    bridge_router_bind_modbus(&r, regs, 2);
    auto a = rule(mqtt("t"), BRIDGE_TYPE_FLOAT, modbus(0), BRIDGE_TYPE_UINT16);
    REQUIRE(bridge_router_add_rule(&r, &a) >= 0);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "t", "  7  ", 5), 1u); // 前後の空白
    CHECK_EQ(regs[0], 7u);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "t", "1e300", 5), 1u); // 範囲内の最大近辺は飽和
    CHECK_EQ(regs[0], 65535u);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "t", "1e301", 5), 0u);  // 範囲外は拒否
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "t", "-1e301", 6), 0u);
    std::string s31(31, '1'), s32(32, '1'); // 31 文字までを受理、32 文字以上は拒否
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "t", s31.c_str(), 31), 1u);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "t", s32.c_str(), 32), 0u);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "t", "  ", 2), 0u);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "t", "TRUE", 4), 1u); // 大文字小文字を区別しない
    CHECK_EQ(regs[0], 1u);
    CHECK_EQ(bridge_router_on_mqtt_message(&r, "t", nullptr, 3), 0u);
}

TEST_CASE(modbus_slave_binding_and_register_bounds) {
    uint16_t regs[4] = {1, 2, 3, 4};
    bridge_router_t r;
    bridge_router_init(&r);
    modbus_slave_t slave{};
    CHECK(!bridge_router_init_modbus_slave(&r, &slave, 1)); // レジスタ未バインド
    bridge_router_bind_modbus(&r, regs, 4);
    CHECK(!bridge_router_init_modbus_slave(&r, &slave, 0));
    CHECK(!bridge_router_init_modbus_slave(&r, &slave, 248));
    CHECK(bridge_router_init_modbus_slave(&r, &slave, 247));
    CHECK(bridge_router_init_modbus_slave(&r, &slave, 1));
    uint16_t out[4] = {};
    CHECK_EQ(slave.callbacks.read_holding_registers(0, 4, out, slave.user_data), MODBUS_EX_NONE);
    CHECK_EQ(out[3], 4u);
    CHECK_EQ(slave.callbacks.read_holding_registers(3, 1, out, slave.user_data), MODBUS_EX_NONE);
    CHECK_EQ(slave.callbacks.read_holding_registers(3, 2, out, slave.user_data), MODBUS_EX_ILLEGAL_DATA_ADDRESS);
    CHECK_EQ(slave.callbacks.read_holding_registers(4, 0, out, slave.user_data), MODBUS_EX_NONE);
    CHECK_EQ(slave.callbacks.read_holding_registers(5, 0, out, slave.user_data), MODBUS_EX_ILLEGAL_DATA_ADDRESS);
    const uint16_t w[2] = {9, 9};
    CHECK_EQ(slave.callbacks.write_holding_registers(2, 2, w, slave.user_data), MODBUS_EX_NONE);
    CHECK_EQ(regs[3], 9u);
    CHECK_EQ(slave.callbacks.write_holding_registers(3, 2, w, slave.user_data), MODBUS_EX_ILLEGAL_DATA_ADDRESS);
    CHECK_EQ(slave.callbacks.write_holding_registers(5, 0, w, slave.user_data), MODBUS_EX_ILLEGAL_DATA_ADDRESS);
}

EMBED_TEST_MAIN()
