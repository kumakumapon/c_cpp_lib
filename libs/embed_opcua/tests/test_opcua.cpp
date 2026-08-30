#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cstring>

#include "embed_opcua/opcua_types.h"
#include "embed_opcua/opcua_binary.h"
#include "embed_opcua/opcua_server.h"
#include "embed_opcua/embed_opcua.hpp"

void test_binary_encoding() {
    std::cout << "[Test] Running OPC-UA binary encoding tests..." << std::endl;

    uint8_t buffer[256];

    // 1. Double エンコード/デコード
    double original_d = 123.456789;
    size_t len = opcua_encode_double(original_d, buffer);
    assert(len == 8);

    double decoded_d = 0.0;
    size_t dec_len = opcua_decode_double(buffer, &decoded_d);
    assert(dec_len == 8);
    assert(decoded_d == original_d);

    // 2. String エンコード/デコード
    const char *str = "OPC-UA Embedded";
    len = opcua_encode_string(str, buffer);
    assert(len == 4 + strlen(str));

    char dec_str[64];
    dec_len = opcua_decode_string(buffer, dec_str, sizeof(dec_str));
    assert(strcmp(dec_str, str) == 0);

    // 3. Variant (Int32)
    opcua_variant_t var_in{};
    var_in.type = OPCUA_TYPE_INT32;
    var_in.value.int32_val = -98765;
    len = opcua_encode_variant(&var_in, buffer);

    opcua_variant_t var_out{};
    dec_len = opcua_decode_variant(buffer, &var_out);
    assert(var_out.type == OPCUA_TYPE_INT32);
    assert(var_out.value.int32_val == -98765);

    std::cout << "[Test] OPC-UA binary encoding passed!" << std::endl;
}

void test_tcp_hello_ack() {
    std::cout << "[Test] Running TCP Hello/Ack handshake tests..." << std::endl;

    uint8_t buffer[256];

    // 1. Hello メッセージ作成
    opcua_hello_message_t hello{};
    hello.protocol_version = 0;
    hello.receive_buffer_size = 65535;
    hello.send_buffer_size = 65535;
    hello.max_message_size = 1024;
    hello.max_chunk_count = 1;
    strcpy(hello.endpoint_url, "opc.tcp://192.168.1.50:4840");

    size_t hello_len = opcua_encode_hello(&hello, buffer);
    assert(hello_len > 0);

    // 2. サーバー側で Hello を処理して ACK を生成
    opcua_server_t server;
    opcua_server_init(&server, "opc.tcp://192.168.1.50:4840");

    uint8_t resp_buf[256];
    size_t resp_len = opcua_server_process_tcp_message(&server, buffer, hello_len, resp_buf, sizeof(resp_buf));
    assert(resp_len > 0);

    // 3. ACK をデコードして検証
    opcua_ack_message_t ack{};
    size_t ack_dec_len = opcua_decode_ack(resp_buf, resp_len, &ack);
    assert(ack_dec_len > 0);
    assert(ack.protocol_version == 0);
    assert(ack.max_message_size == 1024);

    std::cout << "[Test] TCP Hello/Ack handshake passed!" << std::endl;
}

void test_server_address_space_and_services() {
    std::cout << "[Test] Running Server Address Space & Services tests..." << std::endl;

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
    bool ok = opcua_server_add_variable(&server, &nid_temp, "Temperature", &init_val, 0x03 /* Read/Write */);
    assert(ok);

    // 1. Read サービス (Value)
    opcua_datavalue_t read_dv{};
    uint32_t status = opcua_server_process_read(&server, &nid_temp, OPCUA_ATTR_VALUE, &read_dv);
    assert(status == OPCUA_STATUS_GOOD);
    assert(read_dv.value.type == OPCUA_TYPE_FLOAT);
    assert(read_dv.value.value.float_val == 26.5f);

    // 2. Read サービス (DisplayName)
    status = opcua_server_process_read(&server, &nid_temp, OPCUA_ATTR_DISPLAY_NAME, &read_dv);
    assert(status == OPCUA_STATUS_GOOD);
    assert(read_dv.value.type == OPCUA_TYPE_STRING);
    assert(strcmp(read_dv.value.value.string_val, "Temperature") == 0);

    // 3. Write サービス (Value更新)
    opcua_datavalue_t write_dv{};
    write_dv.value.type = OPCUA_TYPE_FLOAT;
    write_dv.value.value.float_val = 31.2f;
    write_dv.status = OPCUA_STATUS_GOOD;

    status = opcua_server_process_write(&server, &nid_temp, OPCUA_ATTR_VALUE, &write_dv);
    assert(status == OPCUA_STATUS_GOOD);

    // 更新後の確認
    status = opcua_server_process_read(&server, &nid_temp, OPCUA_ATTR_VALUE, &read_dv);
    assert(read_dv.value.value.float_val == 31.2f);

    std::cout << "[Test] Server Address Space & Services passed!" << std::endl;
}

void test_cpp_server_wrapper() {
    std::cout << "[Test] Running C++ Server Wrapper tests..." << std::endl;

    embed_opcua::Server server("opc.tcp://0.0.0.0:4840");
    server.add_variable(1, 2001, "Pressure", 101.325);
    server.add_variable(1, 2002, "PumpState", true);

    opcua_datavalue_t dv{};
    bool ok = server.get_value(1, 2001, dv);
    assert(ok);
    assert(dv.value.type == OPCUA_TYPE_DOUBLE);
    assert(dv.value.value.double_val == 101.325);

    server.set_value(1, 2001, 102.5);
    server.get_value(1, 2001, dv);
    assert(dv.value.value.double_val == 102.5);

    std::cout << "[Test] C++ Server Wrapper passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Running embed_opcua test suite..." << std::endl;
    std::cout << "========================================" << std::endl;

    test_binary_encoding();
    test_tcp_hello_ack();
    test_server_address_space_and_services();
    test_cpp_server_wrapper();

    std::cout << "========================================" << std::endl;
    std::cout << " All embed_opcua tests PASSED successfully!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
