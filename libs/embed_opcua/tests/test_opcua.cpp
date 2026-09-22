#include "embed_test/embed_test.hpp"
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "embed_opcua/embed_opcua.hpp"
#include "embed_opcua/opcua_binary.h"
#include "embed_opcua/opcua_server.h"
#include "embed_opcua/opcua_types.h"

TEST_CASE(test_binary_encoding) {
    std::cout << "# [Test] Running OPC-UA binary encoding tests..." << std::endl;

    uint8_t buffer[256];

    // 1. Double エンコード/デコード
    double original_d = 123.456789;
    size_t len = opcua_encode_double(original_d, buffer, sizeof(buffer));
    CHECK(len == 8);

    double decoded_d = 0.0;
    size_t dec_len = opcua_decode_double(buffer, len, &decoded_d);
    CHECK(dec_len == 8);
    CHECK(decoded_d == original_d);

    // 2. String エンコード/デコード
    const char *str = "OPC-UA Embedded";
    len = opcua_encode_string(str, buffer, sizeof(buffer));
    CHECK(len == 4 + strlen(str));

    char dec_str[64];
    dec_len = opcua_decode_string(buffer, len, dec_str, sizeof(dec_str));
    CHECK(strcmp(dec_str, str) == 0);

    // 3. Variant (Int32)
    opcua_variant_t var_in{};
    var_in.type = OPCUA_TYPE_INT32;
    var_in.value.int32_val = -98765;
    len = opcua_encode_variant(&var_in, buffer, sizeof(buffer));

    opcua_variant_t var_out{};
    dec_len = opcua_decode_variant(buffer, len, &var_out);
    CHECK(var_out.type == OPCUA_TYPE_INT32);
    CHECK(var_out.value.int32_val == -98765);

    std::cout << "# [Test] OPC-UA binary encoding passed!" << std::endl;
}

TEST_CASE(test_tcp_hello_ack) {
    std::cout << "# [Test] Running TCP Hello/Ack handshake tests..." << std::endl;

    uint8_t buffer[256];

    // 1. Hello メッセージ作成
    opcua_hello_message_t hello{};
    hello.protocol_version = 0;
    hello.receive_buffer_size = 65535;
    hello.send_buffer_size = 65535;
    hello.max_message_size = 1024;
    hello.max_chunk_count = 1;
    strcpy(hello.endpoint_url, "opc.tcp://192.168.1.50:4840");

    size_t hello_len = opcua_encode_hello(&hello, buffer, sizeof(buffer));
    CHECK(hello_len > 0);

    // 2. サーバー側で Hello を処理して ACK を生成
    opcua_server_t server;
    opcua_server_init(&server, "opc.tcp://192.168.1.50:4840");

    uint8_t resp_buf[256];
    size_t resp_len =
        opcua_server_process_tcp_message(&server, buffer, hello_len, resp_buf, sizeof(resp_buf));
    CHECK(resp_len > 0);

    // 3. ACK をデコードして検証
    opcua_ack_message_t ack{};
    size_t ack_dec_len = opcua_decode_ack(resp_buf, resp_len, &ack);
    CHECK(ack_dec_len > 0);
    CHECK(ack.protocol_version == 0);
    CHECK(ack.max_message_size == 1024);

    std::cout << "# [Test] TCP Hello/Ack handshake passed!" << std::endl;
}

TEST_CASE(test_server_address_space_and_services) {
    std::cout << "# [Test] Running Server Address Space & Services tests..." << std::endl;

    opcua_server_t server;
    opcua_server_init(&server, nullptr);

    opcua_nodeid_t nid_temp{};
    nid_temp.namespace_index = 1;
    nid_temp.id_type = OPCUA_NODEID_NUMERIC;
    nid_temp.id.numeric = 1001;

    opcua_variant_t init_val{};
    init_val.type = OPCUA_TYPE_FLOAT;
    init_val.value.float_val = 26.5f;

    // 変数ノード追加
    bool ok = opcua_server_add_variable(&server, &nid_temp, "Temperature", &init_val,
                                        0x03 /* Read/Write */);
    CHECK(ok);

    // 1. Read サービス (Value)
    opcua_datavalue_t read_dv{};
    uint32_t status = opcua_server_process_read(&server, &nid_temp, OPCUA_ATTR_VALUE, &read_dv);
    CHECK(status == OPCUA_STATUS_GOOD);
    CHECK(read_dv.value.type == OPCUA_TYPE_FLOAT);
    CHECK(read_dv.value.value.float_val == 26.5f);

    // 2. Read サービス (DisplayName)
    status = opcua_server_process_read(&server, &nid_temp, OPCUA_ATTR_DISPLAY_NAME, &read_dv);
    CHECK(status == OPCUA_STATUS_GOOD);
    CHECK(read_dv.value.type == OPCUA_TYPE_STRING);
    CHECK(strcmp(read_dv.value.value.string_val, "Temperature") == 0);

    // 3. Write サービス (Value更新)
    opcua_datavalue_t write_dv{};
    write_dv.value.type = OPCUA_TYPE_FLOAT;
    write_dv.value.value.float_val = 31.2f;
    write_dv.status = OPCUA_STATUS_GOOD;

    status = opcua_server_process_write(&server, &nid_temp, OPCUA_ATTR_VALUE, &write_dv);
    CHECK(status == OPCUA_STATUS_GOOD);

    // 更新後の確認
    status = opcua_server_process_read(&server, &nid_temp, OPCUA_ATTR_VALUE, &read_dv);
    CHECK(read_dv.value.value.float_val == 31.2f);

    std::cout << "# [Test] Server Address Space & Services passed!" << std::endl;
}

TEST_CASE(test_cpp_server_wrapper) {
    std::cout << "# [Test] Running C++ Server Wrapper tests..." << std::endl;

    embed_opcua::Server server("opc.tcp://0.0.0.0:4840");
    server.add_variable(1, 2001, "Pressure", 101.325);
    server.add_variable(1, 2002, "PumpState", true);

    opcua_datavalue_t dv{};
    bool ok = server.get_value(1, 2001, dv);
    CHECK(ok);
    CHECK(dv.value.type == OPCUA_TYPE_DOUBLE);
    CHECK(dv.value.value.double_val == 101.325);

    server.set_value(1, 2001, 102.5);
    server.get_value(1, 2001, dv);
    CHECK(dv.value.value.double_val == 102.5);

    std::cout << "# [Test] C++ Server Wrapper passed!" << std::endl;
}

TEST_CASE(bounded_codecs_and_malformed_frames) {
    opcua_datavalue_t dv{}, decoded{};
    dv.value.type = OPCUA_TYPE_STRING;
    std::strcpy(dv.value.value.string_val, "boundary");
    dv.status = 1;
    dv.server_timestamp = 123;
    uint8_t wire[256] = {};
    size_t n = opcua_encode_datavalue(&dv, wire, sizeof(wire));
    REQUIRE(n > 0);
    for (size_t i = 0; i < n; ++i) {
        std::vector<uint8_t> truncated(wire, wire + i);
        CHECK_EQ(opcua_decode_datavalue(truncated.data(), i, &decoded), 0u);
        std::vector<uint8_t> out(i + 1, 0xa5);
        CHECK_EQ(opcua_encode_datavalue(&dv, out.data(), i), 0u);
        CHECK_EQ(out[i], 0xa5u);
    }
    CHECK_EQ(opcua_decode_datavalue(wire, n, &decoded), n);
    CHECK_EQ(decoded.server_timestamp, 123u);
    uint8_t short_id[] = {3, 0, 0, 255};
    opcua_nodeid_t id{};
    CHECK_EQ(opcua_decode_nodeid(short_id, sizeof(short_id), &id), 0u);
    uint8_t bad_string[] = {0xfe, 0xff, 0xff, 0xff};
    char text[8];
    CHECK_EQ(opcua_decode_string(bad_string, 4, text, sizeof(text)), 0u);
    uint8_t unknown[] = {0xff};
    CHECK_EQ(opcua_decode_nodeid(unknown, 1, &id), 0u);
    CHECK_EQ(opcua_decode_variant(unknown, 1, &decoded.value), 0u);
    opcua_hello_message_t hello{};
    n = opcua_encode_hello(&hello, wire, sizeof(wire));
    REQUIRE(n == 32);
    for (size_t i = 0; i < n; ++i)
        CHECK_EQ(opcua_decode_hello(wire, i, &hello), 0u);
    wire[4] = 31;
    CHECK_EQ(opcua_decode_hello(wire, n, &hello), 0u);
    opcua_server_t server{};
    opcua_server_init(&server, nullptr);
    id.id_type = OPCUA_NODEID_NUMERIC;
    CHECK(opcua_server_add_variable(&server, &id, "String", &dv.value, 3));
    uint8_t request[] = {'M', 'S', 'G', 'F', 8, 0, 0, 0};
    uint8_t response[17];
    memset(response, 0xa5, sizeof(response));
    CHECK_EQ(opcua_server_process_tcp_message(&server, request, 8, response, 16), 0u);
    CHECK_EQ(response[16], 0xa5u);
}

TEST_CASE(all_scalar_and_nodeid_boundaries) {
    const opcua_type_id_t types[] = {OPCUA_TYPE_NULL,   OPCUA_TYPE_BOOLEAN,  OPCUA_TYPE_SBYTE,
                                     OPCUA_TYPE_BYTE,   OPCUA_TYPE_INT16,    OPCUA_TYPE_UINT16,
                                     OPCUA_TYPE_INT32,  OPCUA_TYPE_UINT32,   OPCUA_TYPE_INT64,
                                     OPCUA_TYPE_UINT64, OPCUA_TYPE_FLOAT,    OPCUA_TYPE_DOUBLE,
                                     OPCUA_TYPE_STRING, OPCUA_TYPE_DATETIME, OPCUA_TYPE_STATUSCODE};
    for (auto type : types) {
        opcua_variant_t v{}, out{};
        v.type = type;
        uint8_t encoded[128];
        size_t n = opcua_encode_variant(&v, encoded, sizeof(encoded));
        REQUIRE(n);
        for (size_t i = 0; i < n; ++i) {
            std::vector<uint8_t> small(encoded, encoded + i);
            CHECK_EQ(opcua_decode_variant(small.data(), i, &out), 0u);
            std::vector<uint8_t> dst(i + 1, 0xa5);
            CHECK_EQ(opcua_encode_variant(&v, dst.data(), i), 0u);
            CHECK_EQ(dst[i], 0xa5u);
        }
        CHECK_EQ(opcua_decode_variant(encoded, n, &out), n);
        CHECK_EQ(out.type, type);
    }
    for (unsigned form = 0; form < 4; ++form) {
        opcua_nodeid_t id{}, out{};
        id.id.numeric = form == 0 ? 1 : form == 1 ? 256 : 65536;
        id.namespace_index = form == 0 ? 0 : 256;
        if (form == 1)
            id.namespace_index = 1;
        if (form == 3) {
            id.id_type = OPCUA_NODEID_STRING;
            std::strcpy(id.id.string, "node");
        }
        uint8_t encoded[128];
        size_t n = opcua_encode_nodeid(&id, encoded, sizeof(encoded));
        REQUIRE(n);
        for (size_t i = 0; i < n; ++i) {
            std::vector<uint8_t> small(encoded, encoded + i);
            CHECK_EQ(opcua_decode_nodeid(small.data(), i, &out), 0u);
            std::vector<uint8_t> dst(i + 1, 0xa5);
            CHECK_EQ(opcua_encode_nodeid(&id, dst.data(), i), 0u);
            CHECK_EQ(dst[i], 0xa5u);
        }
        CHECK_EQ(opcua_decode_nodeid(encoded, n, &out), n);
    }
}
TEST_CASE(random_bounded_decoders) {
    uint32_t state = 12345;
    for (unsigned trial = 0; trial < 1024; ++trial) {
        size_t len = trial % 80;
        std::vector<uint8_t> wire(len);
        for (auto &b : wire) {
            state = state * 1664525u + 1013904223u;
            b = (uint8_t)(state >> 24);
        }
        opcua_nodeid_t id{};
        opcua_variant_t v{};
        opcua_datavalue_t dv{};
        opcua_hello_message_t hello{};
        opcua_ack_message_t ack{};
        CHECK(opcua_decode_nodeid(wire.data(), len, &id) <= len);
        CHECK(opcua_decode_variant(wire.data(), len, &v) <= len);
        CHECK(opcua_decode_datavalue(wire.data(), len, &dv) <= len);
        CHECK(opcua_decode_hello(wire.data(), len, &hello) <= len);
        CHECK(opcua_decode_ack(wire.data(), len, &ack) <= len);
    }
}
EMBED_TEST_MAIN()
