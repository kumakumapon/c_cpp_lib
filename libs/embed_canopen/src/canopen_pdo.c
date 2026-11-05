/* PDO (CiA 301 7.2.2): マッピング、SYNC / イベント / 変化検出による送信、RPDO の反映、
 * 通信・マッピングパラメータ (0x1400/0x1600/0x1800/0x1A00) の仮想オブジェクト */
#include "canopen_internal.h"

static bool pdo_valid(const canopen_pdo_t *p) { return !(p->cob_id & CANOPEN_PDO_INVALID); }

static bool type_ok(uint8_t type) { return type <= 240 || type == 254 || type == 255; }

static void reset_pdo_runtime(canopen_pdo_t *p) {
    p->timer_ms = 0;
    p->since_tx_ms = UINT32_MAX; /* 送信禁止時間は「十分前に送った」扱いで始める */
    p->sync_count = 0;
    p->pending = false;
    p->has_last = false;
    p->rx_pending = false;
}

void canopen_pdo_defaults(canopen_node_t *n) {
    for (unsigned i = 0; i < CANOPEN_MAX_TPDO; ++i) {
        canopen_pdo_t *p = &n->tpdo[i];
        memset(p, 0, sizeof(*p));
        p->cob_id = CANOPEN_PDO_INVALID | (0x180u + 0x100u * i + n->node_id);
        p->transmission_type = 254;
        reset_pdo_runtime(p);
    }
    for (unsigned i = 0; i < CANOPEN_MAX_RPDO; ++i) {
        canopen_pdo_t *p = &n->rpdo[i];
        memset(p, 0, sizeof(*p));
        p->cob_id = CANOPEN_PDO_INVALID | (0x200u + 0x100u * i + n->node_id);
        p->transmission_type = 254;
        reset_pdo_runtime(p);
    }
}

void canopen_pdo_reset_runtime(canopen_node_t *n) {
    for (unsigned i = 0; i < CANOPEN_MAX_TPDO; ++i)
        reset_pdo_runtime(&n->tpdo[i]);
    for (unsigned i = 0; i < CANOPEN_MAX_RPDO; ++i)
        reset_pdo_runtime(&n->rpdo[i]);
}

void canopen_pdo_on_operational(canopen_node_t *n) {
    /* Operational に入ったら、イベント駆動の TPDO は現在値を一度送る */
    for (unsigned i = 0; i < CANOPEN_MAX_TPDO; ++i) {
        canopen_pdo_t *p = &n->tpdo[i];
        reset_pdo_runtime(p);
        if (pdo_valid(p) && p->transmission_type >= 254)
            p->pending = true;
    }
    for (unsigned i = 0; i < CANOPEN_MAX_RPDO; ++i)
        reset_pdo_runtime(&n->rpdo[i]);
}

/* ---- マッピング ---- */

static uint32_t check_map_entry(canopen_node_t *n, uint32_t m, bool tx) {
    const uint16_t idx = (uint16_t)(m >> 16);
    const uint8_t sub = (uint8_t)(m >> 8);
    const uint8_t bits = (uint8_t)m;
    const canopen_od_entry_t *e = canopen_od_find_entry_const(&n->od, idx, sub);
    if (!e)
        return CANOPEN_SDO_ABORT_NO_OBJECT;
    if (!canopen_od_type_is_numeric(e->type) || bits == 0 || (size_t)bits != e->data_len * 8u)
        return CANOPEN_SDO_ABORT_NOT_MAPPABLE;
    if (tx ? e->access == OD_ACCESS_WO : e->access == OD_ACCESS_RO)
        return CANOPEN_SDO_ABORT_NOT_MAPPABLE;
    return 0;
}

static uint32_t validate_count(canopen_node_t *n, const canopen_pdo_t *p, uint8_t count, bool tx) {
    if (count > CANOPEN_PDO_MAX_MAP)
        return CANOPEN_SDO_ABORT_VALUE_HIGH;
    unsigned total = 0;
    for (uint8_t i = 0; i < count; ++i) {
        if (check_map_entry(n, p->map[i], tx))
            return CANOPEN_SDO_ABORT_NOT_MAPPABLE;
        total += p->map[i] & 0xFFu;
        if (total > 64)
            return CANOPEN_SDO_ABORT_PDO_LENGTH;
    }
    return 0;
}

static uint8_t mapped_bytes(const canopen_pdo_t *p) {
    unsigned total = 0;
    for (uint8_t i = 0; i < p->map_count; ++i)
        total += (p->map[i] & 0xFFu) / 8u;
    return (uint8_t)total;
}

static uint8_t pack(canopen_node_t *n, const canopen_pdo_t *p, uint8_t out[8]) {
    uint8_t off = 0;
    memset(out, 0, 8);
    for (uint8_t i = 0; i < p->map_count; ++i) {
        const uint32_t m = p->map[i];
        const uint8_t bytes = (uint8_t)((m & 0xFFu) / 8u);
        const canopen_od_entry_t *e =
            canopen_od_find_entry_const(&n->od, (uint16_t)(m >> 16), (uint8_t)(m >> 8));
        if (e && off + bytes <= 8)
            co_copy(out + off, (const uint8_t *)e->data_ptr, bytes, true);
        off = (uint8_t)(off + bytes);
    }
    return off;
}

static void transmit(canopen_node_t *n, canopen_pdo_t *p) {
    uint8_t data[8];
    const uint8_t len = pack(n, p, data);
    canopen_queue_tx(n, p->cob_id & 0x7FFu, data, len);
    memcpy(p->last, data, 8);
    p->has_last = true;
    p->pending = false;
    p->timer_ms = 0;
    p->since_tx_ms = 0;
}

static void apply_rpdo(canopen_node_t *n, canopen_pdo_t *p, uint8_t num, const uint8_t *data) {
    uint8_t off = 0;
    for (uint8_t i = 0; i < p->map_count; ++i) {
        const uint32_t m = p->map[i];
        const uint8_t bytes = (uint8_t)((m & 0xFFu) / 8u);
        uint8_t tmp[8];
        co_copy(tmp, data + off, bytes, true);
        /* フックが拒否した値は反映しない (他の項目は反映する) */
        (void)canopen_od_write_checked(&n->od, (uint16_t)(m >> 16), (uint8_t)(m >> 8), tmp, bytes);
        off = (uint8_t)(off + bytes);
    }
    if (n->callbacks.on_rpdo)
        n->callbacks.on_rpdo(n, num, n->callback_user);
}

/* ---- 実行時 ---- */

void canopen_pdo_sync(canopen_node_t *n) {
    for (unsigned i = 0; i < CANOPEN_MAX_RPDO; ++i) {
        canopen_pdo_t *p = &n->rpdo[i];
        if (pdo_valid(p) && p->transmission_type <= 240 && p->rx_pending) {
            p->rx_pending = false;
            apply_rpdo(n, p, (uint8_t)(i + 1), p->rx_data);
        }
    }
    for (unsigned i = 0; i < CANOPEN_MAX_TPDO; ++i) {
        canopen_pdo_t *p = &n->tpdo[i];
        if (!pdo_valid(p))
            continue;
        if (p->transmission_type == 0) {
            if (p->pending)
                transmit(n, p);
        } else if (p->transmission_type <= 240) {
            if (++p->sync_count >= p->transmission_type) {
                p->sync_count = 0;
                transmit(n, p);
            }
        }
    }
}

static uint32_t inhibit_ms(const canopen_pdo_t *p) { return (p->inhibit_100us + 9u) / 10u; }

void canopen_pdo_tick(canopen_node_t *n, uint32_t ms) {
    for (unsigned i = 0; i < CANOPEN_MAX_TPDO; ++i) {
        canopen_pdo_t *p = &n->tpdo[i];
        if (!pdo_valid(p) || p->transmission_type < 254)
            continue;
        p->since_tx_ms = ms > UINT32_MAX - p->since_tx_ms ? UINT32_MAX : p->since_tx_ms + ms;
        p->timer_ms += ms;
        if (p->event_timer_ms && p->timer_ms >= p->event_timer_ms)
            p->pending = true;
        uint8_t now[8];
        pack(n, p, now);
        if (!p->has_last || memcmp(now, p->last, 8) != 0)
            p->pending = true; /* 値が変わった (Change of State) */
        if (p->pending && p->since_tx_ms >= inhibit_ms(p))
            transmit(n, p);
    }
}

bool canopen_pdo_rx(canopen_node_t *n, const can_frame_t *rx) {
    for (unsigned i = 0; i < CANOPEN_MAX_RPDO; ++i) {
        canopen_pdo_t *p = &n->rpdo[i];
        if (!pdo_valid(p) || (p->cob_id & 0x7FFu) != rx->id)
            continue;
        if (rx->len < mapped_bytes(p)) {
            (void)canopen_node_emcy(n, CANOPEN_EMCY_PDO_LENGTH, CANOPEN_ERR_REG_COMMUNICATION, NULL);
            return true;
        }
        if (p->transmission_type <= 240) {
            memcpy(p->rx_data, rx->data, 8);
            p->rx_pending = true; /* 同期型: 次の SYNC で反映 */
        } else {
            apply_rpdo(n, p, (uint8_t)(i + 1), rx->data);
        }
        return true;
    }
    return false;
}

/* ---- パラメータの設定 (SDO と API で共通の検査) ---- */

static uint32_t set_cob_id(canopen_pdo_t *p, uint32_t v) {
    if (v & CANOPEN_PDO_EXTENDED)
        return CANOPEN_SDO_ABORT_VALUE_RANGE; /* 29bit ID は未対応 */
    const bool make_valid = !(v & CANOPEN_PDO_INVALID);
    if (make_valid && (v & 0x7FFu) == 0)
        return CANOPEN_SDO_ABORT_VALUE_RANGE;
    /* 有効なまま CAN ID を変えることはできない (一度無効にする: CiA 301 7.5.2.35) */
    if (pdo_valid(p) && make_valid && (v & 0x7FFu) != (p->cob_id & 0x7FFu))
        return CANOPEN_SDO_ABORT_VALUE_RANGE;
    const bool was_valid = pdo_valid(p);
    p->cob_id = v;
    if (!was_valid && make_valid)
        reset_pdo_runtime(p);
    return 0;
}

static uint32_t set_map_count(canopen_node_t *n, canopen_pdo_t *p, uint8_t count, bool tx) {
    if (pdo_valid(p))
        return CANOPEN_SDO_ABORT_DEVICE_STATE;
    const uint32_t code = validate_count(n, p, count, tx);
    if (code)
        return code;
    p->map_count = count;
    return 0;
}

static uint32_t set_map_entry(canopen_node_t *n, canopen_pdo_t *p, uint8_t sub, uint32_t v,
                              bool tx) {
    if (pdo_valid(p))
        return CANOPEN_SDO_ABORT_DEVICE_STATE;
    if (p->map_count != 0) /* sub0 を 0 にしてから書く */
        return CANOPEN_SDO_ABORT_UNSUPPORTED;
    if (v != 0) {
        const uint32_t code = check_map_entry(n, v, tx);
        if (code)
            return code;
    }
    p->map[sub - 1] = v;
    return 0;
}

static bool decode_index(uint16_t idx, bool *tx, bool *map, unsigned *num) {
    const uint16_t bases[4] = {0x1400, 0x1600, 0x1800, 0x1A00};
    for (unsigned k = 0; k < 4; ++k) {
        if (idx >= bases[k] && idx < bases[k] + 4u) {
            *tx = k >= 2;
            *map = (k & 1u) != 0;
            *num = idx - bases[k];
            return true;
        }
    }
    return false;
}

bool canopen_pdo_is_obj(uint16_t idx) {
    bool tx, map;
    unsigned num;
    return decode_index(idx, &tx, &map, &num);
}

size_t canopen_pdo_obj_size(uint16_t idx, uint8_t sub) {
    bool tx, map;
    unsigned num;
    if (!decode_index(idx, &tx, &map, &num))
        return 0;
    if (sub == 0 || (!map && sub == 2))
        return 1;
    if (!map && (sub == 3 || sub == 5))
        return 2;
    return 4;
}

bool canopen_pdo_obj_read(canopen_node_t *n, uint16_t idx, uint8_t sub, uint8_t *buf, size_t cap,
                          size_t *len, uint32_t *code) {
    bool tx, map;
    unsigned num;
    if (!decode_index(idx, &tx, &map, &num))
        return false;
    const canopen_pdo_t *p = tx ? &n->tpdo[num] : &n->rpdo[num];
    if (map) {
        if (sub == 0)
            *code = co_put_value(buf, cap, len, p->map_count, 1);
        else if (sub <= CANOPEN_PDO_MAX_MAP)
            *code = co_put_value(buf, cap, len, p->map[sub - 1], 4);
        else
            *code = CANOPEN_SDO_ABORT_NO_SUBINDEX;
        return true;
    }
    switch (sub) {
    case 0:
        *code = co_put_value(buf, cap, len, tx ? 5u : 2u, 1); /* 最大サブインデックス */
        break;
    case 1:
        *code = co_put_value(buf, cap, len, p->cob_id, 4);
        break;
    case 2:
        *code = co_put_value(buf, cap, len, p->transmission_type, 1);
        break;
    case 3:
        *code = tx ? co_put_value(buf, cap, len, p->inhibit_100us, 2) : CANOPEN_SDO_ABORT_NO_SUBINDEX;
        break;
    case 5:
        *code = tx ? co_put_value(buf, cap, len, p->event_timer_ms, 2) : CANOPEN_SDO_ABORT_NO_SUBINDEX;
        break;
    default:
        *code = CANOPEN_SDO_ABORT_NO_SUBINDEX;
        break;
    }
    return true;
}

bool canopen_pdo_obj_write(canopen_node_t *n, uint16_t idx, uint8_t sub, const uint8_t *d,
                           size_t len, uint32_t *code) {
    bool tx, map;
    unsigned num;
    if (!decode_index(idx, &tx, &map, &num))
        return false;
    canopen_pdo_t *p = tx ? &n->tpdo[num] : &n->rpdo[num];
    if (map) {
        if (sub > CANOPEN_PDO_MAX_MAP)
            *code = CANOPEN_SDO_ABORT_NO_SUBINDEX;
        else if (sub == 0)
            *code = co_len_code(len, 1) ? co_len_code(len, 1) : set_map_count(n, p, d[0], tx);
        else
            *code = co_len_code(len, 4) ? co_len_code(len, 4)
                                        : set_map_entry(n, p, sub, co_get_le(d, 4), tx);
        return true;
    }
    switch (sub) {
    case 0:
        *code = CANOPEN_SDO_ABORT_READ_ONLY;
        break;
    case 1:
        *code = co_len_code(len, 4) ? co_len_code(len, 4) : set_cob_id(p, co_get_le(d, 4));
        break;
    case 2:
        if ((*code = co_len_code(len, 1)) == 0) {
            if (!type_ok(d[0])) {
                *code = CANOPEN_SDO_ABORT_VALUE_RANGE;
            } else {
                p->transmission_type = d[0];
                p->sync_count = 0;
            }
        }
        break;
    case 3:
        if (!tx)
            *code = CANOPEN_SDO_ABORT_NO_SUBINDEX;
        else if ((*code = co_len_code(len, 2)) == 0) {
            if (pdo_valid(p))
                *code = CANOPEN_SDO_ABORT_DEVICE_STATE; /* 禁止時間は無効な間だけ変更できる */
            else
                p->inhibit_100us = (uint16_t)co_get_le(d, 2);
        }
        break;
    case 5:
        if (!tx)
            *code = CANOPEN_SDO_ABORT_NO_SUBINDEX;
        else if ((*code = co_len_code(len, 2)) == 0) {
            p->event_timer_ms = (uint16_t)co_get_le(d, 2);
            p->timer_ms = 0;
        }
        break;
    default:
        *code = CANOPEN_SDO_ABORT_NO_SUBINDEX;
        break;
    }
    return true;
}

/* ---- 公開 API ---- */

static bool configure(canopen_node_t *n, canopen_pdo_t *p, const canopen_pdo_map_entry_t *map,
                      uint8_t count, uint8_t type, bool tx) {
    if (count > CANOPEN_PDO_MAX_MAP || (count && !map) || !type_ok(type))
        return false;
    p->cob_id |= CANOPEN_PDO_INVALID;
    p->map_count = 0;
    for (uint8_t i = 0; i < count; ++i) {
        const uint32_t raw = ((uint32_t)map[i].index << 16) | ((uint32_t)map[i].subindex << 8) |
                             map[i].bit_length;
        if (set_map_entry(n, p, (uint8_t)(i + 1), raw, tx) || raw == 0)
            return false;
    }
    if (set_map_count(n, p, count, tx))
        return false;
    p->transmission_type = type;
    return set_cob_id(p, p->cob_id & ~CANOPEN_PDO_INVALID) == 0;
}

bool canopen_node_config_tpdo(canopen_node_t *n, uint8_t num, const canopen_pdo_map_entry_t *map,
                              uint8_t count, uint8_t type, uint16_t event_timer_ms) {
    if (!n || num < 1 || num > CANOPEN_MAX_TPDO)
        return false;
    canopen_pdo_t *p = &n->tpdo[num - 1];
    p->event_timer_ms = event_timer_ms;
    if (!configure(n, p, map, count, type, true))
        return false;
    if (n->nmt_state == CANOPEN_NMT_STATE_OPERATIONAL && type >= 254)
        p->pending = true;
    return true;
}

bool canopen_node_config_rpdo(canopen_node_t *n, uint8_t num, const canopen_pdo_map_entry_t *map,
                              uint8_t count, uint8_t type) {
    if (!n || num < 1 || num > CANOPEN_MAX_RPDO)
        return false;
    return configure(n, &n->rpdo[num - 1], map, count, type, false);
}

bool canopen_node_trigger_tpdo(canopen_node_t *n, uint8_t num) {
    if (!n || num < 1 || num > CANOPEN_MAX_TPDO)
        return false;
    canopen_pdo_t *p = &n->tpdo[num - 1];
    if (!pdo_valid(p) || (p->transmission_type >= 1 && p->transmission_type <= 240))
        return false;
    p->pending = true;
    if (p->transmission_type >= 254 && n->nmt_state == CANOPEN_NMT_STATE_OPERATIONAL &&
        p->since_tx_ms >= inhibit_ms(p))
        transmit(n, p);
    return true;
}

bool canopen_node_set_tpdo_inhibit(canopen_node_t *n, uint8_t num, uint16_t inhibit_100us) {
    if (!n || num < 1 || num > CANOPEN_MAX_TPDO || pdo_valid(&n->tpdo[num - 1]))
        return false;
    n->tpdo[num - 1].inhibit_100us = inhibit_100us;
    return true;
}
