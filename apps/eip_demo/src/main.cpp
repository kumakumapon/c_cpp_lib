#include <iostream>
#include <vector>
#include <iomanip>
#include <cstring>

#include "embed_eip/eip_types.h"
#include "embed_eip/eip_encap.h"
#include "embed_eip/eip_cip.h"
#include "embed_eip/eip_adapter.h"
#include "embed_eip/embed_eip.hpp"

// センサ入力アセンブリ構造体 (Instance 100)
#pragma pack(push, 1)
struct SensorInputAssembly {
    float temperature;   // 温度 (℃)
    float pressure;      // 圧力 (kPa)
    uint16_t status_flags;// ステータス
};

// 制御出力アセンブリ構造体 (Instance 150)
struct ControlOutputAssembly {
    uint16_t motor_speed_rpm; // 目標回転数
    uint8_t valve_state;      // 0: 閉, 1: 開
    uint8_t alert_buzzer;     // 0: OFF, 1: ON
};
#pragma pack(pop)

static void print_hex(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
    }
    std::cout << std::dec << std::endl;
}

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << " EtherNet/IP Protocol Stack (embed_eip) Demo" << std::endl;
    std::cout << "=====================================================" << std::endl;

    // 1. EtherNet/IP アダプタノードの構築
    embed_eip::Adapter adapter(0x002A, 0x000C, 0x0100, "Industrial IO-Link Node");
    adapter.add_assembly(100, sizeof(SensorInputAssembly));
    adapter.add_assembly(150, sizeof(ControlOutputAssembly));

    SensorInputAssembly initial_sensor{28.4f, 101.3f, 0x0001};
    adapter.set_assembly_pod(100, initial_sensor);

    std::cout << "\n[1] EtherNet/IP Adapter Node configured:" << std::endl;
    std::cout << "  Vendor ID   : 0x002A (Rockwell Automation / ODVA Vendor)" << std::endl;
    std::cout << "  Product Name: Industrial IO-Link Node" << std::endl;
    std::cout << "  Assembly 100: Sensor Input (" << sizeof(SensorInputAssembly) << " bytes)" << std::endl;
    std::cout << "  Assembly 150: Control Output (" << sizeof(ControlOutputAssembly) << " bytes)" << std::endl;

    // -----------------------------------------------------------------
    // Scenario 1: PLC/Scanner - RegisterSession
    // -----------------------------------------------------------------
    std::cout << "\n>>> Step 1: PLC Scanner requests RegisterSession <<<" << std::endl;
    eip_encap_header_t reg_req;
    memset(&reg_req, 0, sizeof(reg_req));
    reg_req.command = EIP_CMD_REGISTER_SESSION;
    reg_req.length = 4;
    const uint8_t sender_ctx[8] = {'P', 'L', 'C', '_', 'C', 'P', 'U', '1'};
    memcpy(reg_req.sender_context, sender_ctx, 8);

    uint8_t packet_buf[512];
    size_t req_len = eip_encode_encap_header(&reg_req, packet_buf);
    packet_buf[req_len++] = 0x01; // Protocol version
    packet_buf[req_len++] = 0x00;
    packet_buf[req_len++] = 0x00; // Options
    packet_buf[req_len++] = 0x00;

    std::cout << "  [PLC -> TCP 44818] (RegisterSession, " << req_len << " bytes): ";
    print_hex(packet_buf, req_len);

    uint8_t resp_buf[512];
    size_t resp_len = eip_adapter_process_packet(adapter.raw_handle(), packet_buf, req_len, resp_buf, sizeof(resp_buf));
    std::cout << "  [Adapter -> PLC] (" << resp_len << " bytes): ";
    print_hex(resp_buf, resp_len);

    eip_encap_header_t reg_resp_hdr;
    eip_decode_encap_header(resp_buf, resp_len, &reg_resp_hdr);
    uint32_t session_id = reg_resp_hdr.session_handle;
    std::cout << "  [PLC Parsed] Session Registered! Assigned Handle: 0x" << std::hex << session_id << std::dec << std::endl;

    // -----------------------------------------------------------------
    // Scenario 2: CIP Identity Object Query (Product Name)
    // -----------------------------------------------------------------
    std::cout << "\n>>> Step 2: PLC Query Identity Object (Class 0x01, Inst 1, Attr 7) <<<" << std::endl;
    uint8_t cip_req[64];
    size_t cip_req_len = cip_build_get_attribute_single(CIP_CLASS_IDENTITY, 1, 7, cip_req, sizeof(cip_req));

    req_len = eip_build_send_rr_data(session_id, sender_ctx, cip_req, cip_req_len, packet_buf, sizeof(packet_buf));
    std::cout << "  [PLC -> SendRRData] (" << req_len << " bytes)" << std::endl;

    resp_len = eip_adapter_process_packet(adapter.raw_handle(), packet_buf, req_len, resp_buf, sizeof(resp_buf));
    
    const uint8_t *cip_resp = nullptr;
    size_t cip_resp_len = 0;
    eip_encap_header_t send_rr_hdr;
    eip_decode_encap_header(resp_buf, resp_len, &send_rr_hdr);
    eip_parse_send_rr_data(&resp_buf[EIP_ENCAP_HEADER_SIZE], send_rr_hdr.length, &cip_resp, &cip_resp_len);

    uint8_t name_len = cip_resp[4];
    std::string product_name((const char *)&cip_resp[5], name_len);
    std::cout << "  [PLC Parsed] Identity Product Name: \"" << product_name << "\"" << std::endl;

    // -----------------------------------------------------------------
    // Scenario 3: PLC Read Sensor Input Assembly (Instance 100)
    // -----------------------------------------------------------------
    std::cout << "\n>>> Step 3: PLC Read Input Assembly (Instance 100) <<<" << std::endl;
    cip_req_len = cip_build_get_attribute_single(CIP_CLASS_ASSEMBLY, 100, 3 /* Data */, cip_req, sizeof(cip_req));
    req_len = eip_build_send_rr_data(session_id, sender_ctx, cip_req, cip_req_len, packet_buf, sizeof(packet_buf));

    resp_len = eip_adapter_process_packet(adapter.raw_handle(), packet_buf, req_len, resp_buf, sizeof(resp_buf));
    eip_decode_encap_header(resp_buf, resp_len, &send_rr_hdr);
    eip_parse_send_rr_data(&resp_buf[EIP_ENCAP_HEADER_SIZE], send_rr_hdr.length, &cip_resp, &cip_resp_len);

    SensorInputAssembly received_sensor{};
    memcpy(&received_sensor, &cip_resp[4], sizeof(received_sensor));
    std::cout << "  [PLC Parsed] Sensor Data -> Temp: " << received_sensor.temperature << " C, "
              << "Pressure: " << received_sensor.pressure << " kPa, Status: 0x"
              << std::hex << received_sensor.status_flags << std::dec << std::endl;

    // -----------------------------------------------------------------
    // Scenario 4: PLC Write Output Assembly (Instance 150)
    // -----------------------------------------------------------------
    std::cout << "\n>>> Step 4: PLC Write Output Assembly (Instance 150) <<<" << std::endl;
    ControlOutputAssembly ctrl{1800, 1, 0};
    cip_req_len = cip_build_set_attribute_single(CIP_CLASS_ASSEMBLY, 150, 3 /* Data */,
                                                 (const uint8_t *)&ctrl, sizeof(ctrl),
                                                 cip_req, sizeof(cip_req));
    req_len = eip_build_send_rr_data(session_id, sender_ctx, cip_req, cip_req_len, packet_buf, sizeof(packet_buf));

    resp_len = eip_adapter_process_packet(adapter.raw_handle(), packet_buf, req_len, resp_buf, sizeof(resp_buf));
    
    ControlOutputAssembly applied_ctrl{};
    adapter.get_assembly_pod(150, applied_ctrl);
    std::cout << "  [Adapter Verified] Output Updated -> Target RPM: " << applied_ctrl.motor_speed_rpm
              << ", Valve: " << (applied_ctrl.valve_state ? "OPEN" : "CLOSED")
              << ", Buzzer: " << (applied_ctrl.alert_buzzer ? "ON" : "OFF") << std::endl;

    // -----------------------------------------------------------------
    // Scenario 5: UnRegisterSession
    // -----------------------------------------------------------------
    std::cout << "\n>>> Step 5: PLC Scanner UnRegisterSession <<<" << std::endl;
    eip_encap_header_t unreg_req;
    memset(&unreg_req, 0, sizeof(unreg_req));
    unreg_req.command = EIP_CMD_UNREGISTER_SESSION;
    unreg_req.session_handle = session_id;
    req_len = eip_encode_encap_header(&unreg_req, packet_buf);

    eip_adapter_process_packet(adapter.raw_handle(), packet_buf, req_len, resp_buf, sizeof(resp_buf));
    std::cout << "  [Adapter] Session 0x" << std::hex << session_id << std::dec << " cleanly closed." << std::endl;

    std::cout << "\n=====================================================" << std::endl;
    std::cout << " embed_eip demonstration completed successfully!" << std::endl;
    std::cout << "=====================================================" << std::endl;
    return 0;
}
