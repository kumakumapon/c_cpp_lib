#include <iostream>
#include <vector>
#include <string>
#include <cstdio>
#include <iomanip>

#include "embed_modbus/modbus_types.h"
#include "embed_modbus/modbus_crc.h"
#include "embed_modbus/modbus_slave.h"
#include "embed_modbus/modbus_master.h"
#include "embed_modbus/modbus_rtu.h"
#include "embed_modbus/embed_modbus.hpp"

// レジスタアドレスマップ定義
#define REG_TEMPERATURE     0x0000  // 保持レジスタ: 温度 (0.1度単位, 255 = 25.5℃)
#define REG_HUMIDITY        0x0001  // 保持レジスタ: 湿度 (0.1%単位, 600 = 60.0%)
#define REG_DEVICE_STATUS   0x0002  // 保持レジスタ: ステータスフラグ
#define COIL_HEATER_RELAY   0x0010  // コイル: ヒーターリレー制御 (ON/OFF)
#define COIL_COOLER_RELAY   0x0011  // コイル: クーラーリレー制御 (ON/OFF)

static void print_hex_stream(const char *label, const uint8_t *data, size_t len) {
    std::printf("  %s (%zu bytes): ", label, len);
    for (size_t i = 0; i < len; ++i) {
        std::printf("%02X ", data[i]);
    }
    std::printf("\n");
}

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << " Modbus RTU Protocol Stack (embed_modbus) Demo" << std::endl;
    std::cout << "=====================================================" << std::endl;

    // 1. Modbus RTU Slave (Unit ID = 1) の準備
    embed_modbus::Slave sensor_slave(1);

    // 初期センサ値・リレー状態のセット
    sensor_slave.set_holding_register(REG_TEMPERATURE, 268);  // 26.8 ℃
    sensor_slave.set_holding_register(REG_HUMIDITY, 554);     // 55.4 %
    sensor_slave.set_holding_register(REG_DEVICE_STATUS, 0x0001); // RUNNING
    sensor_slave.set_coil(COIL_HEATER_RELAY, false);
    sensor_slave.set_coil(COIL_COOLER_RELAY, true);

    std::cout << "\n[1] Slave Node initialized (Unit ID: 1):\n";
    std::printf("  Initial Temperature: %.1f C\n", sensor_slave.get_holding_register(REG_TEMPERATURE) / 10.0f);
    std::printf("  Initial Humidity   : %.1f %%\n", sensor_slave.get_holding_register(REG_HUMIDITY) / 10.0f);
    std::printf("  Cooler Relay State : %s\n", sensor_slave.get_coil(COIL_COOLER_RELAY) ? "ON" : "OFF");

    // ---------------------------------------------------------
    // シナリオ 1: Master が保持レジスタ (温度 & 湿度) を一括読み出し (FC 0x03)
    // ---------------------------------------------------------
    std::cout << "\n>>> Scenario 1: Master Read Holding Registers (FC 0x03) <<<\n";
    {
        uint8_t req_pdu[5];
        size_t req_pdu_len = 0;
        modbus_master_req_read_holding_registers(REG_TEMPERATURE, 2, req_pdu, &req_pdu_len);

        uint8_t req_adu[MODBUS_MAX_ADU_LENGTH];
        size_t req_adu_len = 0;
        modbus_rtu_encode_frame(1, req_pdu, req_pdu_len, req_adu, &req_adu_len);
        print_hex_stream("[Master TX -> RS485 Bus]", req_adu, req_adu_len);

        // Slave 受信・処理
        std::vector<uint8_t> bus_request(req_adu, req_adu + req_adu_len);
        std::vector<uint8_t> bus_response;
        sensor_slave.handle_rtu_request(bus_request, bus_response);
        print_hex_stream("[Slave  TX -> RS485 Bus]", bus_response.data(), bus_response.size());

        // Master 応答パース
        uint8_t unit_id = 0;
        const uint8_t *resp_pdu = nullptr;
        size_t resp_pdu_len = 0;
        modbus_rtu_decode_frame(bus_response.data(), bus_response.size(), &unit_id, &resp_pdu, &resp_pdu_len);

        uint16_t sensor_values[2];
        modbus_master_parse_read_registers_resp(resp_pdu, resp_pdu_len, sensor_values, 2);
        std::printf("  [Master Parsed] Unit ID: %d | Temp: %.1f C | Humid: %.1f %%\n",
                    unit_id, sensor_values[0] / 10.0f, sensor_values[1] / 10.0f);
    }

    // ---------------------------------------------------------
    // シナリオ 2: Master がヒーターリレーをONに制御 (FC 0x05 Write Single Coil)
    // ---------------------------------------------------------
    std::cout << "\n>>> Scenario 2: Master Write Single Coil (FC 0x05) <<<\n";
    {
        uint8_t req_pdu[5];
        size_t req_pdu_len = 0;
        modbus_master_req_write_single_coil(COIL_HEATER_RELAY, true, req_pdu, &req_pdu_len);

        uint8_t req_adu[MODBUS_MAX_ADU_LENGTH];
        size_t req_adu_len = 0;
        modbus_rtu_encode_frame(1, req_pdu, req_pdu_len, req_adu, &req_adu_len);
        print_hex_stream("[Master TX -> RS485 Bus]", req_adu, req_adu_len);

        std::vector<uint8_t> bus_req(req_adu, req_adu + req_adu_len);
        std::vector<uint8_t> bus_resp;
        sensor_slave.handle_rtu_request(bus_req, bus_resp);
        print_hex_stream("[Slave  TX -> RS485 Bus]", bus_resp.data(), bus_resp.size());

        std::printf("  [Slave State Update] Heater Relay is now: %s\n",
                    sensor_slave.get_coil(COIL_HEATER_RELAY) ? "ON (Active)" : "OFF");
    }

    // ---------------------------------------------------------
    // シナリオ 3: 複数レジスタ一括書き込み (FC 0x10)
    // ---------------------------------------------------------
    std::cout << "\n>>> Scenario 3: Master Write Multiple Registers (FC 0x10) <<<\n";
    {
        uint16_t new_calibration[2] = {295, 480}; // 29.5℃, 48.0%
        uint8_t req_pdu[MODBUS_MAX_PDU_LENGTH];
        size_t req_pdu_len = 0;
        modbus_master_req_write_multiple_registers(REG_TEMPERATURE, 2, new_calibration, req_pdu, &req_pdu_len);

        uint8_t req_adu[MODBUS_MAX_ADU_LENGTH];
        size_t req_adu_len = 0;
        modbus_rtu_encode_frame(1, req_pdu, req_pdu_len, req_adu, &req_adu_len);

        std::vector<uint8_t> bus_req(req_adu, req_adu + req_adu_len);
        std::vector<uint8_t> bus_resp;
        sensor_slave.handle_rtu_request(bus_req, bus_resp);

        std::printf("  [Slave State Update] Calibration updated -> Temp: %.1f C, Humid: %.1f %%\n",
                    sensor_slave.get_holding_register(REG_TEMPERATURE) / 10.0f,
                    sensor_slave.get_holding_register(REG_HUMIDITY) / 10.0f);
    }

    std::cout << "\n=====================================================" << std::endl;
    std::cout << " Modbus RTU Demonstration completed successfully!" << std::endl;
    std::cout << "=====================================================" << std::endl;
    return 0;
}
