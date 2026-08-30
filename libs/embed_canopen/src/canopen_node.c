#include "embed_canopen/canopen_node.h"
#include <string.h>

#define SDO_ABORT_OBJECT_NOT_FOUND   0x06020000
#define SDO_ABORT_READ_ONLY          0x06010002
#define SDO_ABORT_WRITE_ONLY         0x06010001
#define SDO_ABORT_PARAM_ERROR        0x06090030

void canopen_node_init(canopen_node_t *node, uint8_t node_id, const char *device_name) {
    if (!node) return;
    memset(node, 0, sizeof(canopen_node_t));

    node->node_id = (node_id >= 1 && node_id <= 127) ? node_id : 1;
    node->nmt_state = CANOPEN_NMT_STATE_PRE_OPERATIONAL;

    /* デフォルト通信パラメータ設定 */
    node->device_type = 0x00020192; // CiA 402 Servo Drive / Generic Device Profile
    node->error_register = 0x00;
    node->cob_id_sync = 0x00000080;
    node->heartbeat_producer_ms = 1000;
    node->vendor_id = 0x000005AD;
    node->product_code = 0x00010001;
    node->revision_number = 0x00010000;
    node->serial_number = 0x00000001;

    if (device_name) {
        strncpy(node->device_name, device_name, sizeof(node->device_name) - 1);
    } else {
        strncpy(node->device_name, "CANopen-Node", sizeof(node->device_name) - 1);
    }

    /* オブジェクトディクショナリ (OD) 初期化 */
    canopen_od_init(&node->od);

    /* 標準通信ODエントリ登録 (0x1000〜0x1018) */
    canopen_od_add_entry(&node->od, 0x1000, 0x00, OD_TYPE_UINT32, OD_ACCESS_RO,
                         &node->device_type, sizeof(node->device_type));
    canopen_od_add_entry(&node->od, 0x1001, 0x00, OD_TYPE_UINT8, OD_ACCESS_RO,
                         &node->error_register, sizeof(node->error_register));
    canopen_od_add_entry(&node->od, 0x1005, 0x00, OD_TYPE_UINT32, OD_ACCESS_RW,
                         &node->cob_id_sync, sizeof(node->cob_id_sync));
    canopen_od_add_entry(&node->od, 0x1008, 0x00, OD_TYPE_VISIBLE_STRING, OD_ACCESS_RO,
                         node->device_name, strlen(node->device_name));
    canopen_od_add_entry(&node->od, 0x1017, 0x00, OD_TYPE_UINT16, OD_ACCESS_RW,
                         &node->heartbeat_producer_ms, sizeof(node->heartbeat_producer_ms));

    // 0x1018 Identity Object
    canopen_od_add_entry(&node->od, 0x1018, 0x01, OD_TYPE_UINT32, OD_ACCESS_RO,
                         &node->vendor_id, sizeof(node->vendor_id));
    canopen_od_add_entry(&node->od, 0x1018, 0x02, OD_TYPE_UINT32, OD_ACCESS_RO,
                         &node->product_code, sizeof(node->product_code));
    canopen_od_add_entry(&node->od, 0x1018, 0x03, OD_TYPE_UINT32, OD_ACCESS_RO,
                         &node->revision_number, sizeof(node->revision_number));
    canopen_od_add_entry(&node->od, 0x1018, 0x04, OD_TYPE_UINT32, OD_ACCESS_RO,
                         &node->serial_number, sizeof(node->serial_number));
}

static void send_sdo_abort(uint8_t node_id, uint16_t index, uint8_t subindex, uint32_t abort_code, can_frame_t *tx_frame) {
    tx_frame->id = CANOPEN_FC_TSDO + node_id;
    tx_frame->len = 8;
    tx_frame->is_extended = false;
    tx_frame->is_rtr = false;
    tx_frame->data[0] = 0x80; // SDO Abort CS
    tx_frame->data[1] = (uint8_t)(index & 0xFF);
    tx_frame->data[2] = (uint8_t)((index >> 8) & 0xFF);
    tx_frame->data[3] = subindex;
    tx_frame->data[4] = (uint8_t)(abort_code & 0xFF);
    tx_frame->data[5] = (uint8_t)((abort_code >> 8) & 0xFF);
    tx_frame->data[6] = (uint8_t)((abort_code >> 16) & 0xFF);
    tx_frame->data[7] = (uint8_t)((abort_code >> 24) & 0xFF);
}

bool canopen_node_process_frame(canopen_node_t *node, const can_frame_t *rx_frame, can_frame_t *tx_frame) {
    if (!node || !rx_frame || !tx_frame) return false;

    // 1. NMT コマンドフレームの処理 (COB-ID == 0x000)
    if (rx_frame->id == CANOPEN_FC_NMT_SERVICE) {
        if (rx_frame->len >= 2) {
            uint8_t cmd = rx_frame->data[0];
            uint8_t target_id = rx_frame->data[1];

            if (target_id == 0 || target_id == node->node_id) {
                switch (cmd) {
                    case CANOPEN_NMT_CMD_START:
                        node->nmt_state = CANOPEN_NMT_STATE_OPERATIONAL;
                        break;
                    case CANOPEN_NMT_CMD_STOP:
                        node->nmt_state = CANOPEN_NMT_STATE_STOPPED;
                        break;
                    case CANOPEN_NMT_CMD_ENTER_PRE_OP:
                        node->nmt_state = CANOPEN_NMT_STATE_PRE_OPERATIONAL;
                        break;
                    case CANOPEN_NMT_CMD_RESET_NODE:
                    case CANOPEN_NMT_CMD_RESET_COMM:
                        node->nmt_state = CANOPEN_NMT_STATE_PRE_OPERATIONAL;
                        node->heartbeat_timer_ms = 0;
                        break;
                    default:
                        break;
                }
            }
        }
        return false;
    }

    // 停止状態 (STOPPED) の場合は NMT 以外のフレームを受け付けない
    if (node->nmt_state == CANOPEN_NMT_STATE_STOPPED) {
        return false;
    }

    // 2. SDO 要求フレームの処理 (COB-ID == 0x600 + NodeID)
    if (rx_frame->id == (uint32_t)(CANOPEN_FC_RSDO + node->node_id)) {
        if (rx_frame->len < 4) return false;

        uint8_t cs_byte = rx_frame->data[0];
        uint16_t index = (uint16_t)(rx_frame->data[1] | (rx_frame->data[2] << 8));
        uint8_t subindex = rx_frame->data[3];

        uint8_t cs = (cs_byte >> 5) & 0x07;

        if (cs == 2) {
            /* SDO Upload (Client Reads from Server/OD) */
            canopen_od_entry_t *entry = canopen_od_find_entry(&node->od, index, subindex);
            if (!entry) {
                send_sdo_abort(node->node_id, index, subindex, SDO_ABORT_OBJECT_NOT_FOUND, tx_frame);
                return true;
            }
            if (entry->access == OD_ACCESS_WO) {
                send_sdo_abort(node->node_id, index, subindex, SDO_ABORT_WRITE_ONLY, tx_frame);
                return true;
            }

            uint8_t val[4] = {0};
            size_t read_len = 0;
            if (!canopen_od_read(&node->od, index, subindex, val, sizeof(val), &read_len)) {
                send_sdo_abort(node->node_id, index, subindex, SDO_ABORT_PARAM_ERROR, tx_frame);
                return true;
            }

            uint8_t n = (read_len <= 4) ? (uint8_t)(4 - read_len) : 0;
            tx_frame->id = CANOPEN_FC_TSDO + node->node_id;
            tx_frame->len = 8;
            tx_frame->is_extended = false;
            tx_frame->is_rtr = false;
            tx_frame->data[0] = (uint8_t)(0x40 | (n << 2) | 0x02 | 0x01); // sc=2(0x40), e=1, s=1
            tx_frame->data[1] = rx_frame->data[1];
            tx_frame->data[2] = rx_frame->data[2];
            tx_frame->data[3] = rx_frame->data[3];
            memcpy(&tx_frame->data[4], val, 4);
            return true;
        } else if (cs == 1) {
            /* SDO Download (Client Writes to Server/OD) */
            canopen_od_entry_t *entry = canopen_od_find_entry(&node->od, index, subindex);
            if (!entry) {
                send_sdo_abort(node->node_id, index, subindex, SDO_ABORT_OBJECT_NOT_FOUND, tx_frame);
                return true;
            }
            if (entry->access == OD_ACCESS_RO) {
                send_sdo_abort(node->node_id, index, subindex, SDO_ABORT_READ_ONLY, tx_frame);
                return true;
            }

            // e (bit 1) and s (bit 0)
            bool is_expedited = (cs_byte & 0x02) != 0;
            uint8_t n = 0;
            if (cs_byte & 0x01) {
                n = (cs_byte >> 2) & 0x03;
            }
            size_t write_len = is_expedited ? (4 - n) : 4;
            if (!canopen_od_write(&node->od, index, subindex, &rx_frame->data[4], write_len)) {
                send_sdo_abort(node->node_id, index, subindex, SDO_ABORT_PARAM_ERROR, tx_frame);
                return true;
            }

            // SDO Download 成功応答
            tx_frame->id = CANOPEN_FC_TSDO + node->node_id;
            tx_frame->len = 8;
            tx_frame->is_extended = false;
            tx_frame->is_rtr = false;
            tx_frame->data[0] = 0x60; // Download Response CS=3
            tx_frame->data[1] = rx_frame->data[1];
            tx_frame->data[2] = rx_frame->data[2];
            tx_frame->data[3] = rx_frame->data[3];
            memset(&tx_frame->data[4], 0, 4);
            return true;
        }
    }

    // 3. RPDO1 (0x200 + NodeID)
    if (rx_frame->id == (uint32_t)(CANOPEN_FC_RPDO1 + node->node_id)) {
        if (node->nmt_state == CANOPEN_NMT_STATE_OPERATIONAL) {
            // 受信データを OD 0x2000.0 (Custom RPDO Receive Data) へ自動反映
            canopen_od_write(&node->od, 0x2000, 0x00, rx_frame->data, rx_frame->len);
        }
        return false;
    }

    return false;
}

bool canopen_node_timer_tick(canopen_node_t *node, uint32_t elapsed_ms, can_frame_t *tx_frame) {
    if (!node || !tx_frame) return false;

    if (node->heartbeat_producer_ms > 0) {
        node->heartbeat_timer_ms += elapsed_ms;
        if (node->heartbeat_timer_ms >= node->heartbeat_producer_ms) {
            node->heartbeat_timer_ms = 0;

            tx_frame->id = CANOPEN_FC_HEARTBEAT + node->node_id;
            tx_frame->len = 1;
            tx_frame->is_extended = false;
            tx_frame->is_rtr = false;
            tx_frame->data[0] = (uint8_t)node->nmt_state;
            return true;
        }
    }

    return false;
}

bool canopen_node_send_tpdo1(const canopen_node_t *node, const uint8_t *data, uint8_t len, can_frame_t *tx_frame) {
    if (!node || !tx_frame) return false;
    if (node->nmt_state != CANOPEN_NMT_STATE_OPERATIONAL) {
        return false; // Operational 状態以外は PDO 送信不可
    }

    tx_frame->id = CANOPEN_FC_TPDO1 + node->node_id;
    tx_frame->len = (len > CANOPEN_MAX_CAN_DATA_LEN) ? CANOPEN_MAX_CAN_DATA_LEN : len;
    tx_frame->is_extended = false;
    tx_frame->is_rtr = false;
    if (data && tx_frame->len > 0) {
        memcpy(tx_frame->data, data, tx_frame->len);
    }
    return true;
}
