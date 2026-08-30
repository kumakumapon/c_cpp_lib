#pragma once
#ifndef MODBUS_MASTER_H
#define MODBUS_MASTER_H

#include "modbus_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * Master 要求PDUエンコード関数群
 * --------------------------------------------------------- */
bool modbus_master_req_read_coils(uint16_t start_addr, uint16_t count, uint8_t *pdu, size_t *pdu_len);
bool modbus_master_req_read_discrete_inputs(uint16_t start_addr, uint16_t count, uint8_t *pdu, size_t *pdu_len);
bool modbus_master_req_read_holding_registers(uint16_t start_addr, uint16_t count, uint8_t *pdu, size_t *pdu_len);
bool modbus_master_req_read_input_registers(uint16_t start_addr, uint16_t count, uint8_t *pdu, size_t *pdu_len);

bool modbus_master_req_write_single_coil(uint16_t addr, bool value, uint8_t *pdu, size_t *pdu_len);
bool modbus_master_req_write_single_register(uint16_t addr, uint16_t value, uint8_t *pdu, size_t *pdu_len);

bool modbus_master_req_write_multiple_coils(uint16_t start_addr, uint16_t count, const uint8_t *src_bytes, uint8_t *pdu, size_t *pdu_len);
bool modbus_master_req_write_multiple_registers(uint16_t start_addr, uint16_t count, const uint16_t *src_regs, uint8_t *pdu, size_t *pdu_len);

/* ---------------------------------------------------------
 * Master 応答PDUデコード関数群
 * --------------------------------------------------------- */
modbus_status_t modbus_master_parse_read_coils_resp(const uint8_t *resp_pdu, size_t resp_len, uint8_t *dest_bytes, uint16_t expected_count);
modbus_status_t modbus_master_parse_read_registers_resp(const uint8_t *resp_pdu, size_t resp_len, uint16_t *dest_regs, uint16_t expected_count);
modbus_status_t modbus_master_parse_write_resp(const uint8_t *resp_pdu, size_t resp_len, uint8_t expected_fc, uint16_t expected_addr);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_MASTER_H */
