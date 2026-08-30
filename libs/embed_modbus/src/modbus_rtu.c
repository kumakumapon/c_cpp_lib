#include "embed_modbus/modbus_rtu.h"
#include <string.h>

bool modbus_rtu_encode_frame(uint8_t unit_id, const uint8_t *pdu, size_t pdu_len,
                             uint8_t *adu, size_t *adu_len) {
    if (!pdu || pdu_len == 0 || !adu || !adu_len) return false;
    if (pdu_len + 3 > MODBUS_MAX_ADU_LENGTH) return false;

    adu[0] = unit_id;
    memcpy(&adu[1], pdu, pdu_len);

    uint16_t crc = modbus_crc16(adu, 1 + pdu_len);
    /* Modbus RTU CRCはリトルエンディアン (下位バイト先) */
    adu[1 + pdu_len]     = (uint8_t)(crc & 0xFF);
    adu[1 + pdu_len + 1] = (uint8_t)((crc >> 8) & 0xFF);

    *adu_len = 1 + pdu_len + 2;
    return true;
}

modbus_status_t modbus_rtu_decode_frame(const uint8_t *adu, size_t adu_len,
                                        uint8_t *unit_id, const uint8_t **pdu, size_t *pdu_len) {
    if (!adu || adu_len < 4 || !unit_id || !pdu || !pdu_len) {
        return MODBUS_ERR_INVALID_PARAM;
    }

    /* CRC16検証 */
    uint16_t calc_crc = modbus_crc16(adu, adu_len - 2);
    uint16_t frame_crc = (uint16_t)(adu[adu_len - 2] | (adu[adu_len - 1] << 8));

    if (calc_crc != frame_crc) {
        return MODBUS_ERR_CRC;
    }

    *unit_id = adu[0];
    *pdu = &adu[1];
    *pdu_len = adu_len - 3;
    return MODBUS_OK;
}

bool modbus_rtu_slave_handle(const modbus_slave_t *slave,
                             const uint8_t *req_adu, size_t req_len,
                             uint8_t *resp_adu, size_t *resp_len) {
    if (!slave || !req_adu || req_len < 4 || !resp_adu || !resp_len) {
        return false;
    }

    uint8_t req_unit_id = 0;
    const uint8_t *req_pdu = NULL;
    size_t req_pdu_len = 0;

    modbus_status_t status = modbus_rtu_decode_frame(req_adu, req_len, &req_unit_id, &req_pdu, &req_pdu_len);
    if (status != MODBUS_OK) {
        return false; /* CRCエラー等のため破棄 */
    }

    /* 自身宛てまたはブロードキャストか判定 */
    if (req_unit_id != slave->unit_id && req_unit_id != MODBUS_BROADCAST_ADDRESS) {
        return false; /* 他スレーブ宛て */
    }

    uint8_t resp_pdu[MODBUS_MAX_PDU_LENGTH];
    size_t resp_pdu_len = 0;

    bool should_respond = modbus_slave_process_pdu(slave, req_pdu, req_pdu_len, resp_pdu, &resp_pdu_len);

    /* ブロードキャスト (Unit ID 0) の場合は応答しない */
    if (req_unit_id == MODBUS_BROADCAST_ADDRESS) {
        return false;
    }

    if (should_respond && resp_pdu_len > 0) {
        return modbus_rtu_encode_frame(slave->unit_id, resp_pdu, resp_pdu_len, resp_adu, resp_len);
    }

    return false;
}
