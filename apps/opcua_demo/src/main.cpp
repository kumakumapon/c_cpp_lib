#include <iostream>
#include <vector>
#include <string>
#include <cstdio>
#include <iomanip>

#include "embed_opcua/opcua_types.h"
#include "embed_opcua/opcua_binary.h"
#include "embed_opcua/opcua_server.h"
#include "embed_opcua/embed_opcua.hpp"

static void print_binary_frame(const char *label, const uint8_t *data, size_t len) {
    std::printf("  %s (%zu bytes): ", label, len);
    for (size_t i = 0; i < len; ++i) {
        std::printf("%02X ", data[i]);
    }
    std::printf("\n");
}

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << " OPC-UA Binary Protocol Stack (embed_opcua) Demo" << std::endl;
    std::cout << "=====================================================" << std::endl;

    // 1. OPC-UA Server インスタンス生成 (アドレス空間のセットアップ)
    embed_opcua::Server server("opc.tcp://192.168.1.100:4840");

    // アドレス空間に変数を登録
    server.add_variable(1, 1001, "MotorSpeed_RPM", 1750.0);
    server.add_variable(1, 1002, "MotorCurrent_A", 4.85);
    server.add_variable(1, 1003, "InverterStatus", std::string("RUNNING"));
    server.add_variable(1, 1004, "EmergencyStop", false);

    std::cout << "\n[1] OPC-UA Server Address Space configured:\n";
    std::cout << "  - ns=1;i=1001: MotorSpeed_RPM (Double)\n";
    std::cout << "  - ns=1;i=1002: MotorCurrent_A (Double)\n";
    std::cout << "  - ns=1;i=1003: InverterStatus (String)\n";
    std::cout << "  - ns=1;i=1004: EmergencyStop  (Boolean)\n";

    // ---------------------------------------------------------
    // ステップ 1: OPC-UA Client との Connection Handshake (HEL / ACK)
    // ---------------------------------------------------------
    std::cout << "\n>>> Step 1: TCP Handshake (Hello / Acknowledge) <<<\n";
    {
        opcua_hello_message_t client_hello{};
        client_hello.protocol_version = 0;
        client_hello.receive_buffer_size = 65535;
        client_hello.send_buffer_size = 65535;
        client_hello.max_message_size = 1024;
        client_hello.max_chunk_count = 1;
        strcpy(client_hello.endpoint_url, "opc.tcp://192.168.1.100:4840");

        uint8_t hello_buf[256];
        size_t hello_len = opcua_encode_hello(&client_hello, hello_buf);
        print_binary_frame("[Client -> HEL]", hello_buf, hello_len);

        // サーバーが処理して ACK を返信
        std::vector<uint8_t> req(hello_buf, hello_buf + hello_len);
        std::vector<uint8_t> resp = server.process_message(req);
        print_binary_frame("[Server -> ACK]", resp.data(), resp.size());

        opcua_ack_message_t ack{};
        opcua_decode_ack(resp.data(), resp.size(), &ack);
        std::printf("  [Client Parsed ACK] Buffer Size: %u, Max Message: %u\n",
                    ack.receive_buffer_size, ack.max_message_size);
    }

    // ---------------------------------------------------------
    // ステップ 2: Read サービス (PLC/SCADAからのモニタリング)
    // ---------------------------------------------------------
    std::cout << "\n>>> Step 2: OPC-UA Read Service <<<\n";
    {
        opcua_datavalue_t dv{};
        server.get_value(1, 1001, dv);
        std::printf("  [Read] ns=1;i=1001 (MotorSpeed_RPM): %.1f RPM (Status: 0x%08X)\n",
                    dv.value.value.double_val, dv.status);

        server.get_value(1, 1002, dv);
        std::printf("  [Read] ns=1;i=1002 (MotorCurrent_A): %.2f A   (Status: 0x%08X)\n",
                    dv.value.value.double_val, dv.status);
    }

    // ---------------------------------------------------------
    // ステップ 3: Write サービス (制御指示による値変更)
    // ---------------------------------------------------------
    std::cout << "\n>>> Step 3: OPC-UA Write Service <<<\n";
    {
        std::cout << "  [Client] Setting MotorSpeed_RPM to 2400.0 RPM...\n";
        server.set_value(1, 1001, 2400.0);

        opcua_datavalue_t updated_dv{};
        server.get_value(1, 1001, updated_dv);
        std::printf("  [Verified] New MotorSpeed_RPM: %.1f RPM\n", updated_dv.value.value.double_val);
    }

    std::cout << "\n=====================================================" << std::endl;
    std::cout << " embed_opcua demonstration completed successfully!" << std::endl;
    std::cout << "=====================================================" << std::endl;
    return 0;
}
