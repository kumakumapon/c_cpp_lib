#include "canopen_internal.h"

/* ================= 送信キュー ================= */

void canopen_queue_tx(canopen_node_t *n, uint32_t id, const uint8_t *data, uint8_t len) {
    if (n->txq_count >= CANOPEN_TX_QUEUE_SIZE) {
        n->tx_overflows++;
        return;
    }
    can_frame_t *f = &n->txq[(n->txq_head + n->txq_count) % CANOPEN_TX_QUEUE_SIZE];
    memset(f, 0, sizeof(*f));
    f->id = id;
    f->len = len;
    if (len)
        memcpy(f->data, data, len);
    n->txq_count++;
}

bool canopen_node_pop_tx(canopen_node_t *n, can_frame_t *tx) {
    if (!n || !tx || n->txq_count == 0)
        return false;
    *tx = n->txq[n->txq_head];
    n->txq_head = (uint8_t)((n->txq_head + 1) % CANOPEN_TX_QUEUE_SIZE);
    n->txq_count--;
    return true;
}

size_t canopen_node_tx_pending(const canopen_node_t *n) { return n ? n->txq_count : 0; }

/* ================= 初期化・NMT ================= */

static void queue_bootup(canopen_node_t *n) {
    const uint8_t zero = CANOPEN_NMT_STATE_INITIALISING;
    canopen_queue_tx(n, CANOPEN_FC_HEARTBEAT + n->node_id, &zero, 1);
}

static void set_state(canopen_node_t *n, canopen_nmt_state_t st) {
    if (n->nmt_state == st)
        return;
    n->nmt_state = st;
    if (st == CANOPEN_NMT_STATE_OPERATIONAL)
        canopen_pdo_on_operational(n);
    if (n->callbacks.on_nmt_change)
        n->callbacks.on_nmt_change(n, st, n->callback_user);
}

void canopen_node_init(canopen_node_t *node, uint8_t node_id, const char *device_name) {
    if (!node)
        return;
    memset(node, 0, sizeof(canopen_node_t));

    node->node_id = (node_id >= 1 && node_id <= 127) ? node_id : 1;
    node->nmt_state = CANOPEN_NMT_STATE_INITIALISING;

    /* デフォルト通信パラメータ設定 */
    node->device_type = 0x00020192; /* CiA 402 Servo Drive / Generic Device Profile */
    node->cob_id_sync = 0x00000080;
    node->heartbeat_producer_ms = 1000;
    node->vendor_id = 0x000005AD;
    node->product_code = 0x00010001;
    node->revision_number = 0x00010000;
    node->serial_number = 0x00000001;
    node->sdo_timeout_ms = 1000;
    strncpy(node->device_name, device_name ? device_name : "CANopen-Node",
            sizeof(node->device_name) - 1);

    canopen_od_init(&node->od);
    canopen_od_add_entry(&node->od, 0x1000, 0x00, OD_TYPE_UINT32, OD_ACCESS_RO,
                         &node->device_type, sizeof(node->device_type));
    canopen_od_add_entry(&node->od, 0x1001, 0x00, OD_TYPE_UINT8, OD_ACCESS_RO,
                         &node->error_register, sizeof(node->error_register));
    canopen_od_add_entry(&node->od, 0x1005, 0x00, OD_TYPE_UINT32, OD_ACCESS_RW,
                         &node->cob_id_sync, sizeof(node->cob_id_sync));
    canopen_od_add_entry(&node->od, 0x1008, 0x00, OD_TYPE_VISIBLE_STRING, OD_ACCESS_RO,
                         node->device_name, sizeof(node->device_name) - 1);
    canopen_od_add_entry(&node->od, 0x1017, 0x00, OD_TYPE_UINT16, OD_ACCESS_RW,
                         &node->heartbeat_producer_ms, sizeof(node->heartbeat_producer_ms));
    canopen_od_add_entry(&node->od, 0x1018, 0x01, OD_TYPE_UINT32, OD_ACCESS_RO,
                         &node->vendor_id, sizeof(node->vendor_id));
    canopen_od_add_entry(&node->od, 0x1018, 0x02, OD_TYPE_UINT32, OD_ACCESS_RO,
                         &node->product_code, sizeof(node->product_code));
    canopen_od_add_entry(&node->od, 0x1018, 0x03, OD_TYPE_UINT32, OD_ACCESS_RO,
                         &node->revision_number, sizeof(node->revision_number));
    canopen_od_add_entry(&node->od, 0x1018, 0x04, OD_TYPE_UINT32, OD_ACCESS_RO,
                         &node->serial_number, sizeof(node->serial_number));

    canopen_pdo_defaults(node);
    /* 初期化完了: ブートアップを送って Pre-Operational へ */
    queue_bootup(node);
    node->nmt_state = CANOPEN_NMT_STATE_PRE_OPERATIONAL;
}

void canopen_node_set_callbacks(canopen_node_t *n, const canopen_node_callbacks_t *cb,
                                void *user) {
    if (!n)
        return;
    if (cb)
        n->callbacks = *cb;
    else
        memset(&n->callbacks, 0, sizeof(n->callbacks));
    n->callback_user = user;
}

static void reset_communication(canopen_node_t *n, canopen_nmt_cmd_t cmd) {
    canopen_sdo_reset(n);
    canopen_pdo_reset_runtime(n);
    n->heartbeat_timer_ms = 0;
    memset(n->hb_remote, 0, sizeof(n->hb_remote));
    if (cmd == CANOPEN_NMT_CMD_RESET_NODE) {
        n->error_register = 0;
        n->error_count = 0;
    }
    if (n->callbacks.on_nmt_reset)
        n->callbacks.on_nmt_reset(n, cmd, n->callback_user);
    n->nmt_state = CANOPEN_NMT_STATE_INITIALISING;
    queue_bootup(n);
    set_state(n, CANOPEN_NMT_STATE_PRE_OPERATIONAL);
}

static void handle_nmt(canopen_node_t *n, const can_frame_t *rx) {
    if (rx->len != 2) /* NMT は常に 2 バイト (CiA 301 7.2.8.3.1) */
        return;
    const uint8_t cmd = rx->data[0], target = rx->data[1];
    if (target != 0 && target != n->node_id)
        return;
    switch (cmd) {
    case CANOPEN_NMT_CMD_START:
        set_state(n, CANOPEN_NMT_STATE_OPERATIONAL);
        break;
    case CANOPEN_NMT_CMD_STOP:
        canopen_sdo_reset(n); /* Stopped では SDO も止まるので転送中のものは破棄 */
        set_state(n, CANOPEN_NMT_STATE_STOPPED);
        break;
    case CANOPEN_NMT_CMD_ENTER_PRE_OP:
        set_state(n, CANOPEN_NMT_STATE_PRE_OPERATIONAL);
        break;
    case CANOPEN_NMT_CMD_RESET_NODE:
    case CANOPEN_NMT_CMD_RESET_COMM:
        reset_communication(n, (canopen_nmt_cmd_t)cmd);
        break;
    default:
        break;
    }
}

/* ================= Heartbeat 監視 ================= */

static void consume_heartbeat(canopen_node_t *n, uint8_t remote, uint8_t state) {
    for (unsigned i = 0; i < CANOPEN_MAX_HB_CONSUMERS; ++i) {
        const uint32_t cfg = n->hb_consumer[i];
        if (((cfg >> 16) & 0xFF) != remote || (cfg & 0xFFFF) == 0)
            continue;
        n->hb_remote[i].state = (canopen_nmt_state_t)(state & 0x7F);
        n->hb_remote[i].seen = true;
        n->hb_remote[i].timed_out = false;
        n->hb_remote[i].elapsed_ms = 0;
    }
}

static void tick_consumers(canopen_node_t *n, uint32_t ms) {
    for (unsigned i = 0; i < CANOPEN_MAX_HB_CONSUMERS; ++i) {
        const uint32_t cfg = n->hb_consumer[i];
        const uint32_t timeout = cfg & 0xFFFF;
        if (!timeout || !n->hb_remote[i].seen || n->hb_remote[i].timed_out)
            continue;
        n->hb_remote[i].elapsed_ms += ms;
        if (n->hb_remote[i].elapsed_ms < timeout)
            continue;
        n->hb_remote[i].timed_out = true;
        const uint8_t remote = (uint8_t)((cfg >> 16) & 0xFF);
        if (n->callbacks.on_heartbeat_timeout)
            n->callbacks.on_heartbeat_timeout(n, remote, n->callback_user);
        (void)canopen_node_emcy(n, CANOPEN_EMCY_HEARTBEAT, CANOPEN_ERR_REG_COMMUNICATION, NULL);
    }
}

bool canopen_node_add_heartbeat_consumer(canopen_node_t *n, uint8_t remote, uint16_t timeout_ms) {
    if (!n || remote == 0 || remote > 127 || timeout_ms == 0)
        return false;
    unsigned slot = CANOPEN_MAX_HB_CONSUMERS;
    for (unsigned i = 0; i < CANOPEN_MAX_HB_CONSUMERS; ++i) {
        const uint32_t cfg = n->hb_consumer[i];
        if (((cfg >> 16) & 0xFF) == remote) {
            slot = i;
            break;
        }
        if ((cfg & 0xFFFF) == 0 && slot == CANOPEN_MAX_HB_CONSUMERS)
            slot = i;
    }
    if (slot == CANOPEN_MAX_HB_CONSUMERS)
        return false;
    uint8_t v[4];
    co_put_le(v, ((uint32_t)remote << 16) | timeout_ms, 4);
    return canopen_obj_write(n, 0x1016, (uint8_t)(slot + 1), v, 4) == 0;
}

bool canopen_node_get_remote_state(const canopen_node_t *n, uint8_t remote,
                                   canopen_nmt_state_t *state) {
    if (!n || !state)
        return false;
    for (unsigned i = 0; i < CANOPEN_MAX_HB_CONSUMERS; ++i)
        if (((n->hb_consumer[i] >> 16) & 0xFF) == remote && n->hb_remote[i].seen) {
            *state = n->hb_remote[i].state;
            return true;
        }
    return false;
}

/* ================= EMCY ================= */

bool canopen_node_emcy(canopen_node_t *n, uint16_t code, uint8_t bits, const uint8_t *manuf) {
    if (!n)
        return false;
    if (code == CANOPEN_EMCY_NO_ERROR) {
        n->error_register = bits;
    } else {
        n->error_register |= (uint8_t)(bits | CANOPEN_ERR_REG_GENERIC);
        memmove(&n->error_history[1], &n->error_history[0],
                (CANOPEN_ERROR_HISTORY - 1) * sizeof(n->error_history[0]));
        n->error_history[0] = code;
        if (n->error_count < CANOPEN_ERROR_HISTORY)
            n->error_count++;
    }
    if (n->nmt_state != CANOPEN_NMT_STATE_PRE_OPERATIONAL &&
        n->nmt_state != CANOPEN_NMT_STATE_OPERATIONAL)
        return false;
    uint8_t d[8] = {0};
    co_put_le(d, code, 2);
    d[2] = n->error_register;
    if (manuf)
        memcpy(d + 3, manuf, 5);
    canopen_queue_tx(n, CANOPEN_FC_EMERGENCY + n->node_id, d, 8);
    return true;
}

/* ================= 仮想オブジェクト (0x1003, 0x1016) ================= */

static bool comm_read(canopen_node_t *n, uint16_t idx, uint8_t sub, uint8_t *buf, size_t cap,
                      size_t *len, uint32_t *code) {
    if (idx == 0x1003) {
        if (sub == 0)
            *code = co_put_value(buf, cap, len, n->error_count, 1);
        else if (sub > CANOPEN_ERROR_HISTORY)
            *code = CANOPEN_SDO_ABORT_NO_SUBINDEX;
        else if (sub > n->error_count)
            *code = CANOPEN_SDO_ABORT_NO_DATA;
        else
            *code = co_put_value(buf, cap, len, n->error_history[sub - 1], 4);
        return true;
    }
    if (idx == 0x1016) {
        if (sub == 0)
            *code = co_put_value(buf, cap, len, CANOPEN_MAX_HB_CONSUMERS, 1);
        else if (sub > CANOPEN_MAX_HB_CONSUMERS)
            *code = CANOPEN_SDO_ABORT_NO_SUBINDEX;
        else
            *code = co_put_value(buf, cap, len, n->hb_consumer[sub - 1], 4);
        return true;
    }
    return false;
}

static bool comm_write(canopen_node_t *n, uint16_t idx, uint8_t sub, const uint8_t *d, size_t len,
                       uint32_t *code) {
    if (idx == 0x1003) {
        if (sub > CANOPEN_ERROR_HISTORY)
            *code = CANOPEN_SDO_ABORT_NO_SUBINDEX;
        else if (sub != 0)
            *code = CANOPEN_SDO_ABORT_READ_ONLY;
        else if ((*code = co_len_code(len, 1)) == 0) {
            if (d[0] != 0) /* 0 を書くと履歴を消去。それ以外は不可 (CiA 301 7.5.2.4) */
                *code = CANOPEN_SDO_ABORT_VALUE_RANGE;
            else
                n->error_count = 0;
        }
        return true;
    }
    if (idx == 0x1016) {
        if (sub > CANOPEN_MAX_HB_CONSUMERS)
            *code = CANOPEN_SDO_ABORT_NO_SUBINDEX;
        else if (sub == 0)
            *code = CANOPEN_SDO_ABORT_READ_ONLY;
        else if ((*code = co_len_code(len, 4)) == 0) {
            const uint32_t v = co_get_le(d, 4);
            const uint32_t id = (v >> 16) & 0xFF;
            if (v >> 24) {
                *code = CANOPEN_SDO_ABORT_VALUE_RANGE;
                return true;
            }
            /* 同じノードを有効な監視時間で 2 回登録することはできない */
            for (unsigned i = 0; i < CANOPEN_MAX_HB_CONSUMERS; ++i)
                if (i + 1 != sub && id != 0 && (v & 0xFFFF) != 0 &&
                    ((n->hb_consumer[i] >> 16) & 0xFF) == id && (n->hb_consumer[i] & 0xFFFF)) {
                    *code = CANOPEN_SDO_ABORT_PARAM_INCOMPAT;
                    return true;
                }
            n->hb_consumer[sub - 1] = v;
            memset(&n->hb_remote[sub - 1], 0, sizeof(n->hb_remote[0]));
        }
        return true;
    }
    return false;
}

/* ================= オブジェクトアクセスの振り分け ================= */

uint32_t canopen_obj_read(canopen_node_t *n, uint16_t idx, uint8_t sub, uint8_t *buf, size_t cap,
                          size_t *len) {
    uint32_t code = 0;
    if (comm_read(n, idx, sub, buf, cap, len, &code) ||
        canopen_pdo_obj_read(n, idx, sub, buf, cap, len, &code))
        return code;
    size_t got = 0;
    code = canopen_od_read_checked(&n->od, idx, sub, buf, cap, &got);
    if (code)
        return code;
    const canopen_od_entry_t *e = canopen_od_find_entry_const(&n->od, idx, sub);
    if (e && canopen_od_type_is_numeric(e->type) && got <= 8) {
        uint8_t tmp[8];
        co_copy(tmp, buf, got, true);
        memcpy(buf, tmp, got);
    }
    if (len)
        *len = got;
    return 0;
}

uint32_t canopen_obj_write(canopen_node_t *n, uint16_t idx, uint8_t sub, const uint8_t *data,
                           size_t len) {
    uint32_t code = 0;
    if (comm_write(n, idx, sub, data, len, &code) ||
        canopen_pdo_obj_write(n, idx, sub, data, len, &code))
        return code;
    const canopen_od_entry_t *e = canopen_od_find_entry_const(&n->od, idx, sub);
    if (e && canopen_od_type_is_numeric(e->type) && len == e->data_len && len <= 8) {
        uint8_t tmp[8];
        co_copy(tmp, data, len, true);
        return canopen_od_write_checked(&n->od, idx, sub, tmp, len);
    }
    return canopen_od_write_checked(&n->od, idx, sub, data, len);
}

size_t canopen_obj_size(canopen_node_t *n, uint16_t idx, uint8_t sub) {
    if (idx == 0x1003)
        return sub == 0 ? 1 : 4;
    if (idx == 0x1016)
        return sub == 0 ? 1 : 4;
    if (canopen_pdo_is_obj(idx))
        return canopen_pdo_obj_size(idx, sub);
    const canopen_od_entry_t *e = canopen_od_find_entry_const(&n->od, idx, sub);
    return e ? e->data_len : 0;
}

uint32_t canopen_obj_probe_write(canopen_node_t *n, uint16_t idx, uint8_t sub, bool size_known,
                                 size_t size) {
    if (idx == 0x1003 || idx == 0x1016 || canopen_pdo_is_obj(idx)) {
        /* 仮想オブジェクトはすべて 4 バイト以下。最終的な検査は書き込み時に行う */
        return size_known && size > 4 ? CANOPEN_SDO_ABORT_LENGTH_HIGH : 0;
    }
    const canopen_od_entry_t *e = canopen_od_find_entry_const(&n->od, idx, sub);
    if (!e) {
        for (size_t i = 0; i < n->od.count; ++i)
            if (n->od.entries[i].index == idx)
                return CANOPEN_SDO_ABORT_NO_SUBINDEX;
        return CANOPEN_SDO_ABORT_NO_OBJECT;
    }
    if (e->access == OD_ACCESS_RO)
        return CANOPEN_SDO_ABORT_READ_ONLY;
    if (size_known && size > e->data_len)
        return CANOPEN_SDO_ABORT_LENGTH_HIGH;
    if (size_known && canopen_od_type_is_numeric(e->type) && size < e->data_len)
        return CANOPEN_SDO_ABORT_LENGTH_LOW;
    return 0;
}

uint32_t canopen_node_sdo_read(canopen_node_t *n, uint16_t idx, uint8_t sub, uint8_t *buf,
                               size_t cap, size_t *len) {
    if (!n || (!buf && cap))
        return CANOPEN_SDO_ABORT_GENERAL;
    return canopen_obj_read(n, idx, sub, buf, cap, len);
}

uint32_t canopen_node_sdo_write(canopen_node_t *n, uint16_t idx, uint8_t sub, const uint8_t *data,
                                size_t len) {
    if (!n || (!data && len))
        return CANOPEN_SDO_ABORT_GENERAL;
    return canopen_obj_write(n, idx, sub, data, len);
}

/* ================= フレームの振り分け・時間 ================= */

static void handle_frame(canopen_node_t *n, const can_frame_t *rx) {
    if (rx->is_extended || rx->is_rtr)
        return;
    const uint32_t id = rx->id;
    if (id == CANOPEN_FC_NMT_SERVICE) {
        handle_nmt(n, rx);
        return;
    }
    if ((id & ~0x7Fu) == CANOPEN_FC_HEARTBEAT && rx->len >= 1) {
        const uint8_t remote = (uint8_t)(id & 0x7F);
        if (remote != n->node_id)
            consume_heartbeat(n, remote, rx->data[0]);
        return;
    }
    if (n->nmt_state == CANOPEN_NMT_STATE_STOPPED)
        return; /* Stopped では NMT と Heartbeat 以外を受け付けない */
    if (!(n->cob_id_sync & CANOPEN_PDO_EXTENDED) && id == (n->cob_id_sync & 0x7FFu) &&
        rx->len <= 1) {
        if (n->nmt_state == CANOPEN_NMT_STATE_OPERATIONAL)
            canopen_pdo_sync(n);
        if (n->callbacks.on_sync)
            n->callbacks.on_sync(n, n->callback_user);
        return;
    }
    if (id == (uint32_t)(CANOPEN_FC_RSDO + n->node_id)) {
        if (rx->len == 8) /* SDO は常に 8 バイト */
            canopen_sdo_process(n, rx);
        return;
    }
    if (n->nmt_state == CANOPEN_NMT_STATE_OPERATIONAL)
        (void)canopen_pdo_rx(n, rx);
}

bool canopen_node_process_frame(canopen_node_t *node, const can_frame_t *rx_frame,
                                can_frame_t *tx_frame) {
    if (!node || !rx_frame || !tx_frame)
        return false;
    handle_frame(node, rx_frame);
    return canopen_node_pop_tx(node, tx_frame);
}

bool canopen_node_timer_tick(canopen_node_t *node, uint32_t elapsed_ms, can_frame_t *tx_frame) {
    if (!node || !tx_frame)
        return false;
    if (node->heartbeat_producer_ms > 0) {
        node->heartbeat_timer_ms += elapsed_ms;
        if (node->heartbeat_timer_ms >= node->heartbeat_producer_ms) {
            /* 遅れて呼ばれても 1 回だけ送り、周期の位相は保つ */
            node->heartbeat_timer_ms %= node->heartbeat_producer_ms;
            const uint8_t st = (uint8_t)node->nmt_state;
            canopen_queue_tx(node, CANOPEN_FC_HEARTBEAT + node->node_id, &st, 1);
        }
    } else {
        node->heartbeat_timer_ms = 0;
    }
    tick_consumers(node, elapsed_ms);
    canopen_sdo_tick(node, elapsed_ms);
    if (node->nmt_state == CANOPEN_NMT_STATE_OPERATIONAL)
        canopen_pdo_tick(node, elapsed_ms);
    return canopen_node_pop_tx(node, tx_frame);
}

bool canopen_node_send_tpdo1(const canopen_node_t *node, const uint8_t *data, uint8_t len,
                             can_frame_t *tx_frame) {
    if (!node || !tx_frame)
        return false;
    if (node->nmt_state != CANOPEN_NMT_STATE_OPERATIONAL)
        return false; /* Operational 状態以外は PDO 送信不可 */
    memset(tx_frame, 0, sizeof(*tx_frame));
    tx_frame->id = node->tpdo[0].cob_id & 0x7FFu;
    tx_frame->len = (len > CANOPEN_MAX_CAN_DATA_LEN) ? CANOPEN_MAX_CAN_DATA_LEN : len;
    if (data && tx_frame->len > 0)
        memcpy(tx_frame->data, data, tx_frame->len);
    return true;
}
