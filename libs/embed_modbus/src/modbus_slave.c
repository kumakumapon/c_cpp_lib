#include "embed_modbus/modbus_slave.h"
#include <string.h>

void modbus_slave_init(modbus_slave_t *slave, uint8_t unit_id,
                       const modbus_slave_callbacks_t *callbacks, void *user_data) {
    if (!slave) return;
    slave->unit_id = unit_id;
    if (callbacks) {
        slave->callbacks = *callbacks;
    } else {
        memset(&slave->callbacks, 0, sizeof(modbus_slave_callbacks_t));
    }
    slave->user_data = user_data;
}

static size_t build_exception_pdu(uint8_t fc, modbus_exception_t ex, uint8_t *resp_pdu) {
    resp_pdu[0] = (uint8_t)(fc | 0x80);
    resp_pdu[1] = (uint8_t)ex;
    return 2;
}

bool modbus_slave_process_pdu(const modbus_slave_t *slave,
                              const uint8_t *req_pdu, size_t req_len,
                              uint8_t *resp_pdu, size_t *resp_len) {
    if (!slave || !req_pdu || req_len < 1 || !resp_pdu || !resp_len) {
        return false;
    }

    uint8_t fc = req_pdu[0];

    switch (fc) {
    /* ---------------------------------------------------------
     * FC 0x01 / 0x02: Read Coils / Read Discrete Inputs
     * --------------------------------------------------------- */
    case MODBUS_FC_READ_COILS:
    case MODBUS_FC_READ_DISCRETE_INPUTS: {
        if (req_len < 5) {
            *resp_len = build_exception_pdu(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, resp_pdu);
            return true;
        }

        uint16_t start_addr = (uint16_t)((req_pdu[1] << 8) | req_pdu[2]);
        uint16_t count      = (uint16_t)((req_pdu[3] << 8) | req_pdu[4]);

        if (count == 0 || count > MODBUS_MAX_READ_COILS) {
            *resp_len = build_exception_pdu(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, resp_pdu);
            return true;
        }

        uint8_t byte_count = (uint8_t)((count + 7) / 8);
        uint8_t coil_bytes[256];
        memset(coil_bytes, 0, sizeof(coil_bytes));

        modbus_exception_t ex = MODBUS_EX_ILLEGAL_FUNCTION;
        if (fc == MODBUS_FC_READ_COILS && slave->callbacks.read_coils) {
            ex = slave->callbacks.read_coils(start_addr, count, coil_bytes, slave->user_data);
        } else if (fc == MODBUS_FC_READ_DISCRETE_INPUTS && slave->callbacks.read_discrete_inputs) {
            ex = slave->callbacks.read_discrete_inputs(start_addr, count, coil_bytes, slave->user_data);
        }

        if (ex != MODBUS_EX_NONE) {
            *resp_len = build_exception_pdu(fc, ex, resp_pdu);
            return true;
        }

        resp_pdu[0] = fc;
        resp_pdu[1] = byte_count;
        memcpy(&resp_pdu[2], coil_bytes, byte_count);
        *resp_len = 2 + byte_count;
        return true;
    }

    /* ---------------------------------------------------------
     * FC 0x03 / 0x04: Read Holding Registers / Read Input Registers
     * --------------------------------------------------------- */
    case MODBUS_FC_READ_HOLDING_REGISTERS:
    case MODBUS_FC_READ_INPUT_REGISTERS: {
        if (req_len < 5) {
            *resp_len = build_exception_pdu(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, resp_pdu);
            return true;
        }

        uint16_t start_addr = (uint16_t)((req_pdu[1] << 8) | req_pdu[2]);
        uint16_t count      = (uint16_t)((req_pdu[3] << 8) | req_pdu[4]);

        if (count == 0 || count > MODBUS_MAX_READ_REGISTERS) {
            *resp_len = build_exception_pdu(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, resp_pdu);
            return true;
        }

        uint16_t reg_buffer[MODBUS_MAX_READ_REGISTERS];
        memset(reg_buffer, 0, sizeof(reg_buffer));

        modbus_exception_t ex = MODBUS_EX_ILLEGAL_FUNCTION;
        if (fc == MODBUS_FC_READ_HOLDING_REGISTERS && slave->callbacks.read_holding_registers) {
            ex = slave->callbacks.read_holding_registers(start_addr, count, reg_buffer, slave->user_data);
        } else if (fc == MODBUS_FC_READ_INPUT_REGISTERS && slave->callbacks.read_input_registers) {
            ex = slave->callbacks.read_input_registers(start_addr, count, reg_buffer, slave->user_data);
        }

        if (ex != MODBUS_EX_NONE) {
            *resp_len = build_exception_pdu(fc, ex, resp_pdu);
            return true;
        }

        resp_pdu[0] = fc;
        resp_pdu[1] = (uint8_t)(count * 2);
        for (uint16_t i = 0; i < count; ++i) {
            resp_pdu[2 + (i * 2)]     = (uint8_t)((reg_buffer[i] >> 8) & 0xFF);
            resp_pdu[2 + (i * 2) + 1] = (uint8_t)(reg_buffer[i] & 0xFF);
        }
        *resp_len = 2 + (count * 2);
        return true;
    }

    /* ---------------------------------------------------------
     * FC 0x05: Write Single Coil
     * --------------------------------------------------------- */
    case MODBUS_FC_WRITE_SINGLE_COIL: {
        if (req_len < 5) {
            *resp_len = build_exception_pdu(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, resp_pdu);
            return true;
        }

        uint16_t addr = (uint16_t)((req_pdu[1] << 8) | req_pdu[2]);
        uint16_t val  = (uint16_t)((req_pdu[3] << 8) | req_pdu[4]);

        if (val != 0x0000 && val != 0xFF00) {
            *resp_len = build_exception_pdu(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, resp_pdu);
            return true;
        }

        uint8_t coil_val = (val == 0xFF00) ? 1 : 0;
        modbus_exception_t ex = MODBUS_EX_ILLEGAL_FUNCTION;
        if (slave->callbacks.write_coils) {
            ex = slave->callbacks.write_coils(addr, 1, &coil_val, slave->user_data);
        }

        if (ex != MODBUS_EX_NONE) {
            *resp_len = build_exception_pdu(fc, ex, resp_pdu);
            return true;
        }

        /* 成功応答: リクエストPDUをそのままエコー */
        memcpy(resp_pdu, req_pdu, 5);
        *resp_len = 5;
        return true;
    }

    /* ---------------------------------------------------------
     * FC 0x06: Write Single Register
     * --------------------------------------------------------- */
    case MODBUS_FC_WRITE_SINGLE_REGISTER: {
        if (req_len < 5) {
            *resp_len = build_exception_pdu(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, resp_pdu);
            return true;
        }

        uint16_t addr = (uint16_t)((req_pdu[1] << 8) | req_pdu[2]);
        uint16_t val  = (uint16_t)((req_pdu[3] << 8) | req_pdu[4]);

        modbus_exception_t ex = MODBUS_EX_ILLEGAL_FUNCTION;
        if (slave->callbacks.write_holding_registers) {
            ex = slave->callbacks.write_holding_registers(addr, 1, &val, slave->user_data);
        }

        if (ex != MODBUS_EX_NONE) {
            *resp_len = build_exception_pdu(fc, ex, resp_pdu);
            return true;
        }

        /* 成功応答: リクエストPDUをエコー */
        memcpy(resp_pdu, req_pdu, 5);
        *resp_len = 5;
        return true;
    }

    /* ---------------------------------------------------------
     * FC 0x0F: Write Multiple Coils
     * --------------------------------------------------------- */
    case MODBUS_FC_WRITE_MULTIPLE_COILS: {
        if (req_len < 6) {
            *resp_len = build_exception_pdu(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, resp_pdu);
            return true;
        }

        uint16_t start_addr = (uint16_t)((req_pdu[1] << 8) | req_pdu[2]);
        uint16_t count      = (uint16_t)((req_pdu[3] << 8) | req_pdu[4]);
        uint8_t byte_count  = req_pdu[5];

        if (count == 0 || count > MODBUS_MAX_WRITE_COILS || byte_count != (uint8_t)((count + 7) / 8) || req_len < (size_t)(6 + byte_count)) {
            *resp_len = build_exception_pdu(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, resp_pdu);
            return true;
        }

        modbus_exception_t ex = MODBUS_EX_ILLEGAL_FUNCTION;
        if (slave->callbacks.write_coils) {
            ex = slave->callbacks.write_coils(start_addr, count, &req_pdu[6], slave->user_data);
        }

        if (ex != MODBUS_EX_NONE) {
            *resp_len = build_exception_pdu(fc, ex, resp_pdu);
            return true;
        }

        resp_pdu[0] = fc;
        resp_pdu[1] = req_pdu[1];
        resp_pdu[2] = req_pdu[2];
        resp_pdu[3] = req_pdu[3];
        resp_pdu[4] = req_pdu[4];
        *resp_len = 5;
        return true;
    }

    /* ---------------------------------------------------------
     * FC 0x10: Write Multiple Registers
     * --------------------------------------------------------- */
    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS: {
        if (req_len < 6) {
            *resp_len = build_exception_pdu(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, resp_pdu);
            return true;
        }

        uint16_t start_addr = (uint16_t)((req_pdu[1] << 8) | req_pdu[2]);
        uint16_t count      = (uint16_t)((req_pdu[3] << 8) | req_pdu[4]);
        uint8_t byte_count  = req_pdu[5];

        if (count == 0 || count > MODBUS_MAX_WRITE_REGISTERS || byte_count != (count * 2) || req_len < (size_t)(6 + byte_count)) {
            *resp_len = build_exception_pdu(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, resp_pdu);
            return true;
        }

        uint16_t reg_buffer[MODBUS_MAX_WRITE_REGISTERS];
        for (uint16_t i = 0; i < count; ++i) {
            reg_buffer[i] = (uint16_t)((req_pdu[6 + (i * 2)] << 8) | req_pdu[6 + (i * 2) + 1]);
        }

        modbus_exception_t ex = MODBUS_EX_ILLEGAL_FUNCTION;
        if (slave->callbacks.write_holding_registers) {
            ex = slave->callbacks.write_holding_registers(start_addr, count, reg_buffer, slave->user_data);
        }

        if (ex != MODBUS_EX_NONE) {
            *resp_len = build_exception_pdu(fc, ex, resp_pdu);
            return true;
        }

        resp_pdu[0] = fc;
        resp_pdu[1] = req_pdu[1];
        resp_pdu[2] = req_pdu[2];
        resp_pdu[3] = req_pdu[3];
        resp_pdu[4] = req_pdu[4];
        *resp_len = 5;
        return true;
    }

    default:
        *resp_len = build_exception_pdu(fc, MODBUS_EX_ILLEGAL_FUNCTION, resp_pdu);
        return true;
    }
}
