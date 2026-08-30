#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cstring>

#include "embed_eip/eip_types.h"
#include "embed_eip/eip_encap.h"
#include "embed_eip/eip_cip.h"
#include "embed_eip/eip_adapter.h"
#include "embed_eip/embed_eip.hpp"

void test_encap_header() {
    std::cout << "[Test] Running Encapsulation Header tests..." << std::endl;

    eip_encap_header_t hdr_in;
    memset(&hdr_in, 0, sizeof(hdr_in));
    hdr_in.command = EIP_CMD_REGISTER_SESSION;
    hdr_in.length = 4;
    hdr_in.session_handle = 0x12345678;
    hdr_in.status = EIP_STATUS_SUCCESS;
    const uint8_t ctx[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    memcpy(hdr_in.sender_context, ctx, 8);
    hdr_in.options = 0;

    uint8_t buffer[64];
    size_t enc_sz = eip_encode_encap_header(&hdr_in, buffer);
    assert(enc_sz == EIP_ENCAP_HEADER_SIZE);

    eip_encap_header_t hdr_out;
    size_t dec_sz = eip_decode_encap_header(buffer, sizeof(buffer), &hdr_out);
    assert(dec_sz == EIP_ENCAP_HEADER_SIZE);
    assert(hdr_out.command == EIP_CMD_REGISTER_SESSION);
    assert(hdr_out.length == 4);
    assert(hdr_out.session_handle == 0x12345678);
    assert(hdr_out.status == EIP_STATUS_SUCCESS);
    assert(memcmp(hdr_out.sender_context, ctx, 8) == 0);

    std::cout << "[Test] Encapsulation Header passed!" << std::endl;
}

void test_epath_and_cip_messages() {
    std::cout << "[Test] Running EPATH & CIP message tests..." << std::endl;

    // 1. EPATH: Class 0x01 (Identity), Instance 0x01, Attribute 0x07 (Product Name)
    cip_epath_t path_in;
    path_in.class_id = CIP_CLASS_IDENTITY;
    path_in.instance_id = 1;
    path_in.attribute_id = 7;
    path_in.has_attribute = true;

    uint8_t epath_buf[32];
    size_t epath_len = cip_encode_epath(&path_in, epath_buf);
    assert(epath_len == 6); // 2B (Class) + 2B (Inst) + 2B (Attr)

    cip_epath_t path_out;
    size_t dec_len = cip_decode_epath(epath_buf, epath_len, &path_out);
    assert(dec_len == 6);
    assert(path_out.class_id == CIP_CLASS_IDENTITY);
    assert(path_out.instance_id == 1);
    assert(path_out.attribute_id == 7);
    assert(path_out.has_attribute == true);

    // 2. CIP Request Encode / Decode
    uint8_t req_buf[64];
    size_t req_len = cip_build_get_attribute_single(CIP_CLASS_IDENTITY, 1, 7, req_buf, sizeof(req_buf));
    assert(req_len > 0);

    cip_request_t req_dec;
    bool ok = cip_decode_request(req_buf, req_len, &req_dec);
    assert(ok);
    assert(req_dec.service == CIP_SERVICE_GET_ATTRIBUTE_SINGLE);
    assert(req_dec.path.class_id == CIP_CLASS_IDENTITY);
    assert(req_dec.path.instance_id == 1);
    assert(req_dec.path.attribute_id == 7);

    std::cout << "[Test] EPATH & CIP messages passed!" << std::endl;
}

void test_adapter_session_and_identity() {
    std::cout << "[Test] Running Adapter Session & Identity tests..." << std::endl;

    cip_identity_attr_t id{};
    id.vendor_id = 0x002A; // Rockwell
    id.device_type = 0x000C;
    id.product_code = 0x0064;
    id.major_revision = 2;
    id.minor_revision = 1;
    id.status = 0x0060;
    id.serial_number = 0xAABBCCDD;
    strcpy(id.product_name, "Test Adapter Node");

    eip_adapter_t adapter;
    eip_adapter_init(&adapter, &id);

    // 1. RegisterSession 要求
    eip_encap_header_t reg_req;
    memset(&reg_req, 0, sizeof(reg_req));
    reg_req.command = EIP_CMD_REGISTER_SESSION;
    reg_req.length = 4;

    uint8_t req_packet[64];
    size_t req_len = eip_encode_encap_header(&reg_req, req_packet);
    req_packet[req_len++] = 0x01; // Protocol Version 1
    req_packet[req_len++] = 0x00;
    req_packet[req_len++] = 0x00; // Options 0
    req_packet[req_len++] = 0x00;

    uint8_t resp_packet[256];
    size_t resp_len = eip_adapter_process_packet(&adapter, req_packet, req_len, resp_packet, sizeof(resp_packet));
    assert(resp_len == EIP_ENCAP_HEADER_SIZE + 4);

    eip_encap_header_t reg_resp_hdr;
    eip_decode_encap_header(resp_packet, resp_len, &reg_resp_hdr);
    assert(reg_resp_hdr.status == EIP_STATUS_SUCCESS);
    assert(reg_resp_hdr.session_handle != 0);
    uint32_t session_id = reg_resp_hdr.session_handle;

    // 2. SendRRData: Get_Attribute_Single (Identity Product Name)
    uint8_t cip_req[64];
    size_t cip_req_len = cip_build_get_attribute_single(CIP_CLASS_IDENTITY, 1, 7, cip_req, sizeof(cip_req));

    uint8_t send_rr_buf[256];
    size_t send_rr_len = eip_build_send_rr_data(session_id, reg_resp_hdr.sender_context, cip_req, cip_req_len, send_rr_buf, sizeof(send_rr_buf));
    assert(send_rr_len > 0);

    resp_len = eip_adapter_process_packet(&adapter, send_rr_buf, send_rr_len, resp_packet, sizeof(resp_packet));
    assert(resp_len > 0);

    const uint8_t *cip_resp_data = nullptr;
    size_t cip_resp_len = 0;
    eip_encap_header_t resp_hdr;
    eip_decode_encap_header(resp_packet, resp_len, &resp_hdr);
    assert(resp_hdr.status == EIP_STATUS_SUCCESS);

    bool parsed = eip_parse_send_rr_data(&resp_packet[EIP_ENCAP_HEADER_SIZE], resp_hdr.length, &cip_resp_data, &cip_resp_len);
    assert(parsed);
    assert(cip_resp_len > 4);
    assert(cip_resp_data[0] == (CIP_SERVICE_GET_ATTRIBUTE_SINGLE | 0x80));
    assert(cip_resp_data[2] == CIP_STATUS_SUCCESS);

    // 文字列データ検証 (Short String: 1B len + chars)
    uint8_t str_len = cip_resp_data[4];
    assert(str_len == strlen("Test Adapter Node"));
    assert(memcmp(&cip_resp_data[5], "Test Adapter Node", str_len) == 0);

    std::cout << "[Test] Adapter Session & Identity passed!" << std::endl;
}

void test_cpp_adapter_assembly() {
    std::cout << "[Test] Running C++ Adapter Assembly I/O tests..." << std::endl;

    embed_eip::Adapter adapter(0x0001, 0x000C, 0x0001, "EIP IO Node");

    // アセンブリ 100 (Input / センサデータ 4バイト) & アセンブリ 150 (Output / 制御指示 4バイト)
    adapter.add_assembly(100, 4);
    adapter.add_assembly(150, 4);

    uint32_t sensor_val = 0x12345678;
    adapter.set_assembly_pod(100, sensor_val);

    uint32_t read_back = 0;
    bool ok = adapter.get_assembly_pod(100, read_back);
    assert(ok);
    assert(read_back == 0x12345678);

    std::cout << "[Test] C++ Adapter Assembly I/O passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Running embed_eip test suite..." << std::endl;
    std::cout << "========================================" << std::endl;

    test_encap_header();
    test_epath_and_cip_messages();
    test_adapter_session_and_identity();
    test_cpp_adapter_assembly();

    std::cout << "========================================" << std::endl;
    std::cout << " All embed_eip tests PASSED successfully!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
