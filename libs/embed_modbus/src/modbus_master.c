#include "embed_modbus/modbus_master.h"
#include <string.h>

/* ---------------------------------------------------------
 * Master 要求PDUエンコード
 * --------------------------------------------------------- */
static bool encode_read_req(uint8_t fc, uint16_t start_addr, uint16_t count, uint8_t *pdu, size_t *pdu_len) {
    if (!pdu || !pdu_len) return false;
    pdu[0] = fc;
    pdu[1] = (uint8_t)((start_addr >> 8) & 0xFF);
    pdu[2] = (uint8_t)(start_addr & 0xFF);
    pdu[3] = (uint8_t)((count >> 8) & 0xFF);
    pdu[4] = (uint8_t)(count & 0xFF);
    *pdu_len = 5;
    return true;
}

bool modbus_master_req_read_coils(uint16_t start_addr, uint16_t count, uint8_t *pdu, size_t *pdu_len) {
    if (count == 0 || count > MODBUS_MAX_READ_COILS) return false;
    return encode_read_req(MODBUS_FC_READ_COILS, start_addr, count, pdu, pdu_len);
}

bool modbus_master_req_read_discrete_inputs(uint16_t start_addr, uint16_t count, uint8_t *pdu, size_t *pdu_len) {
    if (count == 0 || count > MODBUS_MAX_READ_COILS) return false;
    return encode_read_req(MODBUS_FC_READ_DISCRETE_INPUTS, start_addr, count, pdu, pdu_len);
}

bool modbus_master_req_read_holding_registers(uint16_t start_addr, uint16_t count, uint8_t *pdu, size_t *pdu_len) {
    if (count == 0 || count > MODBUS_MAX_READ_REGISTERS) return false;
    return encode_read_req(MODBUS_FC_READ_HOLDING_REGISTERS, start_addr, count, pdu, pdu_len);
}

bool modbus_master_req_read_input_registers(uint16_t start_addr, uint16_t count, uint8_t *pdu, size_t *pdu_len) {
    if (count == 0 || count > MODBUS_MAX_READ_REGISTERS) return false;
    return encode_read_req(MODBUS_FC_READ_INPUT_REGISTERS, start_addr, count, pdu, pdu_len);
}

bool modbus_master_req_write_single_coil(uint16_t addr, bool value, uint8_t *pdu, size_t *pdu_len) {
    if (!pdu || !pdu_len) return false;
    pdu[0] = MODBUS_FC_WRITE_SINGLE_COIL;
    pdu[1] = (uint8_t)((addr >> 8) & 0xFF);
    pdu[2] = (uint8_t)(addr & 0xFF);
    pdu[3] = value ? 0xFF : 0x00;
    pdu[4] = 0x00;
    *pdu_len = 5;
    return true;
}

bool modbus_master_req_write_single_register(uint16_t addr, uint16_t value, uint8_t *pdu, size_t *pdu_len) {
    if (!pdu || !pdu_len) return false;
    pdu[0] = MODBUS_FC_WRITE_SINGLE_REGISTER;
    pdu[1] = (uint8_t)((addr >> 8) & 0xFF);
    pdu[2] = (uint8_t)(addr & 0xFF);
    pdu[3] = (uint8_t)((value >> 8) & 0xFF);
    pdu[4] = (uint8_t)(value & 0xFF);
    *pdu_len = 5;
    return true;
}

bool modbus_master_req_write_multiple_coils(uint16_t start_addr, uint16_t count, const uint8_t *src_bytes, uint8_t *pdu, size_t *pdu_len) {
    if (!pdu || !pdu_len || !src_bytes || count == 0 || count > MODBUS_MAX_WRITE_COILS) return false;

    uint8_t byte_count = (uint8_t)((count + 7) / 8);
    pdu[0] = MODBUS_FC_WRITE_MULTIPLE_COILS;
    pdu[1] = (uint8_t)((start_addr >> 8) & 0xFF);
    pdu[2] = (uint8_t)(start_addr & 0xFF);
    pdu[3] = (uint8_t)((count >> 8) & 0xFF);
    pdu[4] = (uint8_t)(count & 0xFF);
    pdu[5] = byte_count;

    memcpy(&pdu[6], src_bytes, byte_count);
    *pdu_len = 6 + byte_count;
    return true;
}

bool modbus_master_req_write_multiple_registers(uint16_t start_addr, uint16_t count, const uint16_t *src_regs, uint8_t *pdu, size_t *pdu_len) {
    if (!pdu || !pdu_len || !src_regs || count == 0 || count > MODBUS_MAX_WRITE_REGISTERS) return false;

    uint8_t byte_count = (uint8_t)(count * 2);
    pdu[0] = MODBUS_FC_WRITE_MULTIPLE_REGISTERS;
    pdu[1] = (uint8_t)((start_addr >> 8) & 0xFF);
    pdu[2] = (uint8_t)(start_addr & 0xFF);
    pdu[3] = (uint8_t)((count >> 8) & 0xFF);
    pdu[4] = (uint8_t)(count & 0xFF);
    pdu[5] = byte_count;

    for (uint16_t i = 0; i < count; ++i) {
        pdu[6 + (i * 2)]     = (uint8_t)((src_regs[i] >> 8) & 0xFF);
        pdu[6 + (i * 2) + 1] = (uint8_t)(src_regs[i] & 0xFF);
    }
    *pdu_len = 6 + byte_count;
    return true;
}

/* ---------------------------------------------------------
 * Master 応答PDUデコード
 * --------------------------------------------------------- */
modbus_status_t modbus_master_parse_read_coils_resp(const uint8_t *resp_pdu, size_t resp_len, uint8_t *dest_bytes, uint16_t expected_count) {
    if (!resp_pdu || resp_len < 2 || !dest_bytes) return MODBUS_ERR_INVALID_PARAM;

    if (resp_pdu[0] & 0x80) {
        return MODBUS_ERR_EXCEPTION_RECEIVED;
    }

    uint8_t expected_bytes = (uint8_t)((expected_count + 7) / 8);
    uint8_t byte_count = resp_pdu[1];

    if (byte_count != expected_bytes || resp_len < (size_t)(2 + byte_count)) {
        return MODBUS_ERR_INVALID_RESPONSE;
    }

    memcpy(dest_bytes, &resp_pdu[2], byte_count);
    return MODBUS_OK;
}

modbus_status_t modbus_master_parse_read_registers_resp(const uint8_t *resp_pdu, size_t resp_len, uint16_t *dest_regs, uint16_t expected_count) {
    if (!resp_pdu || resp_len < 2 || !dest_regs) return MODBUS_ERR_INVALID_PARAM;

    if (resp_pdu[0] & 0x80) {
        return MODBUS_ERR_EXCEPTION_RECEIVED;
    }

    uint8_t expected_bytes = (uint8_t)(expected_count * 2);
    uint8_t byte_count = resp_pdu[1];

    if (byte_count != expected_bytes || resp_len < (size_t)(2 + byte_count)) {
        return MODBUS_ERR_INVALID_RESPONSE;
    }

    for (uint16_t i = 0; i < expected_count; ++i) {
        dest_regs[i] = (uint16_t)((resp_pdu[2 + (i * 2)] << 8) | resp_pdu[2 + (i * 2) + 1]);
    }
    return MODBUS_OK;
}

modbus_status_t modbus_master_parse_write_resp(const uint8_t *resp_pdu, size_t resp_len, uint8_t expected_fc, uint16_t expected_addr) {
    if (!resp_pdu || resp_len < 5) return MODBUS_ERR_INVALID_PARAM;

    if (resp_pdu[0] & 0x80) {
        return MODBUS_ERR_EXCEPTION_RECEIVED;
    }

    if (resp_pdu[0] != expected_fc) {
        return MODBUS_ERR_FUNCTION_MISMATCH;
    }

    uint16_t addr = (uint16_t)((resp_pdu[1] << 8) | resp_pdu[2]);
    if (addr != expected_addr) {
        return MODBUS_ERR_ADDRESS_MISMATCH;
    }

    return MODBUS_OK;
}
