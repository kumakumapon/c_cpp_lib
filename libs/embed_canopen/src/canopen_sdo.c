/* SDO サーバ (CiA 301 7.2.4): 加速型・分割型のアップロード / ダウンロード */
#include "canopen_internal.h"

static void respond(canopen_node_t *n, const uint8_t d[8]) {
    canopen_queue_tx(n, CANOPEN_FC_TSDO + n->node_id, d, 8);
}

static void header(uint8_t d[8], uint8_t cs, uint16_t idx, uint8_t sub) {
    memset(d, 0, 8);
    d[0] = cs;
    d[1] = (uint8_t)idx;
    d[2] = (uint8_t)(idx >> 8);
    d[3] = sub;
}

static void sdo_abort(canopen_node_t *n, uint16_t idx, uint8_t sub, uint32_t code) {
    uint8_t d[8];
    header(d, 0x80, idx, sub);
    co_put_le(d + 4, code, 4);
    respond(n, d);
    n->sdo.state = CANOPEN_SDO_IDLE;
}

void canopen_sdo_reset(canopen_node_t *n) {
    n->sdo.state = CANOPEN_SDO_IDLE;
    n->sdo.idle_ms = 0;
}

static void initiate_download(canopen_node_t *n, const uint8_t *d, uint16_t idx, uint8_t sub) {
    const bool expedited = (d[0] & 0x02) != 0;
    const bool size_indicated = (d[0] & 0x01) != 0;
    n->sdo.state = CANOPEN_SDO_IDLE; /* 転送中に新しい要求が来たら、古い転送は捨てる */
    if (expedited) {
        size_t len;
        if (size_indicated) {
            len = 4u - ((d[0] >> 2) & 0x03u);
        } else {
            /* サイズ指定なし: オブジェクト本来の長さ (4 バイト以下なら) を使う */
            const size_t natural = canopen_obj_size(n, idx, sub);
            len = natural >= 1 && natural <= 4 ? natural : 4;
        }
        const uint32_t code = canopen_obj_write(n, idx, sub, d + 4, len);
        if (code) {
            sdo_abort(n, idx, sub, code);
            return;
        }
        uint8_t r[8];
        header(r, 0x60, idx, sub);
        respond(n, r);
        return;
    }
    const size_t size = size_indicated ? co_get_le(d + 4, 4) : 0;
    uint32_t code = canopen_obj_probe_write(n, idx, sub, size_indicated, size);
    if (!code && size_indicated && size > CANOPEN_SDO_BUFFER_SIZE)
        code = CANOPEN_SDO_ABORT_OUT_OF_MEMORY;
    if (code) {
        sdo_abort(n, idx, sub, code);
        return;
    }
    n->sdo.state = CANOPEN_SDO_DOWNLOAD;
    n->sdo.index = idx;
    n->sdo.subindex = sub;
    n->sdo.toggle = 0;
    n->sdo.size_indicated = size_indicated;
    n->sdo.size = size;
    n->sdo.offset = 0;
    uint8_t r[8];
    header(r, 0x60, idx, sub);
    respond(n, r);
}

static void download_segment(canopen_node_t *n, const uint8_t *d) {
    const uint16_t idx = n->sdo.index;
    const uint8_t sub = n->sdo.subindex;
    const uint8_t t = (d[0] >> 4) & 1u;
    if (t != n->sdo.toggle) {
        sdo_abort(n, idx, sub, CANOPEN_SDO_ABORT_TOGGLE);
        return;
    }
    const size_t k = 7u - ((d[0] >> 1) & 0x07u);
    const bool last = (d[0] & 0x01) != 0;
    if (n->sdo.offset + k > CANOPEN_SDO_BUFFER_SIZE ||
        (n->sdo.size_indicated && n->sdo.offset + k > n->sdo.size)) {
        sdo_abort(n, idx, sub, CANOPEN_SDO_ABORT_LENGTH_HIGH);
        return;
    }
    memcpy(n->sdo.buf + n->sdo.offset, d + 1, k);
    n->sdo.offset += k;
    if (last) {
        if (n->sdo.size_indicated && n->sdo.offset != n->sdo.size) {
            sdo_abort(n, idx, sub, CANOPEN_SDO_ABORT_TYPE_MISMATCH);
            return;
        }
        /* 全部そろってから一度に書く (途中の値を OD に見せない) */
        const uint32_t code = canopen_obj_write(n, idx, sub, n->sdo.buf, n->sdo.offset);
        if (code) {
            sdo_abort(n, idx, sub, code);
            return;
        }
        n->sdo.state = CANOPEN_SDO_IDLE;
    }
    uint8_t r[8] = {0};
    r[0] = (uint8_t)(0x20u | (t << 4));
    respond(n, r);
    n->sdo.toggle ^= 1u;
}

static void initiate_upload(canopen_node_t *n, uint16_t idx, uint8_t sub) {
    n->sdo.state = CANOPEN_SDO_IDLE;
    size_t len = 0;
    const uint32_t code = canopen_obj_read(n, idx, sub, n->sdo.buf, sizeof(n->sdo.buf), &len);
    if (code) {
        sdo_abort(n, idx, sub, code);
        return;
    }
    uint8_t r[8];
    if (len >= 1 && len <= 4) {
        /* 加速型: e=1, s=1, n = 4 - len */
        header(r, (uint8_t)(0x43u | ((4u - len) << 2)), idx, sub);
        memcpy(r + 4, n->sdo.buf, len);
        respond(n, r);
        return;
    }
    /* 分割型 (長さ 0 の文字列もこちら): e=0, s=1, データ部にサイズ */
    header(r, 0x41, idx, sub);
    co_put_le(r + 4, (uint32_t)len, 4);
    respond(n, r);
    n->sdo.state = CANOPEN_SDO_UPLOAD;
    n->sdo.index = idx;
    n->sdo.subindex = sub;
    n->sdo.toggle = 0;
    n->sdo.size = len;
    n->sdo.offset = 0;
}

static void upload_segment(canopen_node_t *n, const uint8_t *d) {
    const uint8_t t = (d[0] >> 4) & 1u;
    if (t != n->sdo.toggle) {
        sdo_abort(n, n->sdo.index, n->sdo.subindex, CANOPEN_SDO_ABORT_TOGGLE);
        return;
    }
    const size_t remaining = n->sdo.size - n->sdo.offset;
    const size_t k = remaining < 7 ? remaining : 7;
    const bool last = n->sdo.offset + k == n->sdo.size;
    uint8_t r[8] = {0};
    r[0] = (uint8_t)((t << 4) | ((7u - k) << 1) | (last ? 1u : 0u));
    memcpy(r + 1, n->sdo.buf + n->sdo.offset, k);
    respond(n, r);
    n->sdo.offset += k;
    n->sdo.toggle ^= 1u;
    if (last)
        n->sdo.state = CANOPEN_SDO_IDLE;
}

void canopen_sdo_process(canopen_node_t *n, const can_frame_t *rx) {
    const uint8_t *d = rx->data;
    const uint8_t ccs = d[0] >> 5;
    const uint16_t idx = (uint16_t)(d[1] | (d[2] << 8));
    const uint8_t sub = d[3];
    n->sdo.idle_ms = 0;
    switch (ccs) {
    case 1:
        initiate_download(n, d, idx, sub);
        break;
    case 2:
        initiate_upload(n, idx, sub);
        break;
    case 0:
        if (n->sdo.state == CANOPEN_SDO_DOWNLOAD)
            download_segment(n, d);
        else
            sdo_abort(n, n->sdo.index, n->sdo.subindex, CANOPEN_SDO_ABORT_CS);
        break;
    case 3:
        if (n->sdo.state == CANOPEN_SDO_UPLOAD)
            upload_segment(n, d);
        else
            sdo_abort(n, n->sdo.index, n->sdo.subindex, CANOPEN_SDO_ABORT_CS);
        break;
    case 4: /* クライアントからのアボート: 応答しない */
        n->sdo.state = CANOPEN_SDO_IDLE;
        break;
    default: /* ブロック転送 (5, 6) などは未対応 */
        sdo_abort(n, idx, sub, CANOPEN_SDO_ABORT_CS);
        break;
    }
}

void canopen_sdo_tick(canopen_node_t *n, uint32_t elapsed_ms) {
    if (n->sdo.state == CANOPEN_SDO_IDLE || n->sdo_timeout_ms == 0)
        return;
    n->sdo.idle_ms += elapsed_ms;
    if (n->sdo.idle_ms >= n->sdo_timeout_ms)
        sdo_abort(n, n->sdo.index, n->sdo.subindex, CANOPEN_SDO_ABORT_TIMEOUT);
}
