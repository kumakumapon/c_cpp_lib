#include "embed_nvparam/embed_nvparam.h"
#include "embed_crc/embed_crc.h"
#include <string.h>

/*
 * 記録の配置 (リトルエンディアン):
 *   +0  magic   "NVP1" (0x3150564E)
 *   +4  version uint16
 *   +6  length  uint16
 *   +8  seq     uint32
 *   +12 crc     uint32  CRC-32 (IEEE) of bytes [0, 12) and the data
 *   +align_up(16, write_align)  data (write_align まで 0xFF で埋める)
 */
#define NV_MAGIC 0x3150564Eu
#define NV_HEADER 16u
#define NV_CHUNK 64u /* 1, 2, 4, 8, 16, 32 のどれでも割り切れる */

typedef struct {
    uint16_t version, length;
    uint32_t seq, crc;
} record_t;

static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static size_t align_up(size_t n, size_t a) { return (n + a - 1) / a * a; }
static size_t data_offset(const embed_nvparam_flash_t *f) { return align_up(NV_HEADER, f->write_align); }

static bool flash_valid(const embed_nvparam_flash_t *f) {
    if (!f || !f->read || !f->write || !f->erase || f->bank_size == 0)
        return false;
    const size_t a = f->write_align;
    if (a != 1 && a != 2 && a != 4 && a != 8 && a != 16 && a != 32)
        return false;
    if (f->bank_size % a != 0 || f->bank_size < data_offset(f) + a)
        return false;
    /* バンクが重ならないこと */
    const uint32_t lo = f->bank_addr[0] < f->bank_addr[1] ? f->bank_addr[0] : f->bank_addr[1];
    const uint32_t hi = f->bank_addr[0] < f->bank_addr[1] ? f->bank_addr[1] : f->bank_addr[0];
    return (size_t)(hi - lo) >= f->bank_size;
}

bool embed_nvparam_init(embed_nvparam_t *nv, const embed_nvparam_flash_t *flash, uint16_t version) {
    if (!nv || !flash_valid(flash))
        return false;
    memset(nv, 0, sizeof(*nv));
    nv->flash = flash;
    nv->version = version;
    nv->active = -1;
    return true;
}

size_t embed_nvparam_capacity(const embed_nvparam_t *nv) {
    if (!nv || !nv->flash)
        return 0;
    const size_t room = nv->flash->bank_size - data_offset(nv->flash);
    return room < 0xFFFFu ? room : 0xFFFFu;
}

static uint32_t header_crc_state(const uint8_t *hdr) {
    return embed_crc_update(EMBED_CRC32_IEEE, embed_crc_init(EMBED_CRC32_IEEE), hdr, 12);
}

/* バンクの記録を検査する。有効なら rec に入れて true */
static bool check_bank(const embed_nvparam_t *nv, int bank, record_t *rec) {
    const embed_nvparam_flash_t *f = nv->flash;
    uint8_t hdr[NV_HEADER];
    if (!f->read(f->ctx, f->bank_addr[bank], hdr, sizeof(hdr)) || get32(hdr) != NV_MAGIC)
        return false;
    rec->version = (uint16_t)(hdr[4] | (hdr[5] << 8));
    rec->length = (uint16_t)(hdr[6] | (hdr[7] << 8));
    rec->seq = get32(hdr + 8);
    rec->crc = get32(hdr + 12);
    if (rec->length > embed_nvparam_capacity(nv))
        return false;
    uint32_t state = header_crc_state(hdr);
    uint8_t chunk[NV_CHUNK];
    const uint32_t base = f->bank_addr[bank] + (uint32_t)data_offset(f);
    for (size_t off = 0; off < rec->length; off += NV_CHUNK) {
        const size_t n = rec->length - off < NV_CHUNK ? rec->length - off : NV_CHUNK;
        if (!f->read(f->ctx, base + (uint32_t)off, chunk, n))
            return false;
        state = embed_crc_update(EMBED_CRC32_IEEE, state, chunk, n);
    }
    return embed_crc_finish(EMBED_CRC32_IEEE, state) == rec->crc;
}

/* 新しい方の有効な記録を探す。見つかれば nv->active / seq を更新する */
static bool find_latest(embed_nvparam_t *nv, record_t *latest) {
    record_t r[2];
    const bool ok0 = check_bank(nv, 0, &r[0]);
    const bool ok1 = check_bank(nv, 1, &r[1]);
    int pick = -1;
    if (ok0 && ok1)
        pick = (int32_t)(r[1].seq - r[0].seq) > 0 ? 1 : 0; /* 通し番号の周回を考慮 */
    else if (ok0)
        pick = 0;
    else if (ok1)
        pick = 1;
    nv->active = pick;
    if (pick < 0)
        return false;
    nv->seq = r[pick].seq;
    *latest = r[pick];
    return true;
}

static bool read_data(const embed_nvparam_t *nv, void *data, size_t len) {
    const embed_nvparam_flash_t *f = nv->flash;
    return len == 0 ||
           f->read(f->ctx, f->bank_addr[nv->active] + (uint32_t)data_offset(f), data, len);
}

embed_nvparam_status embed_nvparam_peek(embed_nvparam_t *nv, uint16_t *version, size_t *len) {
    if (!nv || !nv->flash)
        return EMBED_NVPARAM_INVALID;
    record_t rec;
    if (!find_latest(nv, &rec))
        return EMBED_NVPARAM_EMPTY;
    if (version)
        *version = rec.version;
    if (len)
        *len = rec.length;
    return EMBED_NVPARAM_OK;
}

embed_nvparam_status embed_nvparam_load_raw(embed_nvparam_t *nv, void *data, size_t cap,
                                            size_t *len) {
    if (!nv || !nv->flash || (!data && cap))
        return EMBED_NVPARAM_INVALID;
    record_t rec;
    if (!find_latest(nv, &rec))
        return EMBED_NVPARAM_EMPTY;
    if (rec.length > cap)
        return EMBED_NVPARAM_SIZE;
    if (!read_data(nv, data, rec.length))
        return EMBED_NVPARAM_IO;
    if (len)
        *len = rec.length;
    return EMBED_NVPARAM_OK;
}

embed_nvparam_status embed_nvparam_load(embed_nvparam_t *nv, void *data, size_t len) {
    if (!nv || !nv->flash || (!data && len))
        return EMBED_NVPARAM_INVALID;
    record_t rec;
    if (!find_latest(nv, &rec))
        return EMBED_NVPARAM_EMPTY;
    if (rec.version != nv->version)
        return EMBED_NVPARAM_VERSION;
    if (rec.length != len)
        return EMBED_NVPARAM_SIZE;
    return read_data(nv, data, len) ? EMBED_NVPARAM_OK : EMBED_NVPARAM_IO;
}

embed_nvparam_status embed_nvparam_save(embed_nvparam_t *nv, const void *data, size_t len) {
    if (!nv || !nv->flash || (!data && len))
        return EMBED_NVPARAM_INVALID;
    const embed_nvparam_flash_t *f = nv->flash;
    if (len > embed_nvparam_capacity(nv))
        return EMBED_NVPARAM_INVALID;
    if (nv->active < 0) {
        record_t rec; /* 初回の保存: 既存の記録 (前回の起動時のもの) を確認しておく */
        (void)find_latest(nv, &rec);
    }
    const int target = nv->active < 0 ? 0 : 1 - nv->active;
    const uint32_t seq = nv->active < 0 ? 1u : nv->seq + 1u;
    const uint32_t bank = f->bank_addr[target];

    if (!f->erase(f->ctx, bank, f->bank_size))
        return EMBED_NVPARAM_IO;

    /* 1. データ (末尾は書き込み単位まで 0xFF で埋める) */
    const uint8_t *src = (const uint8_t *)data;
    const size_t padded = align_up(len, f->write_align);
    uint8_t chunk[NV_CHUNK];
    for (size_t off = 0; off < padded; off += NV_CHUNK) {
        const size_t n = padded - off < NV_CHUNK ? padded - off : NV_CHUNK;
        memset(chunk, 0xFF, n);
        if (off < len)
            memcpy(chunk, src + off, len - off < n ? len - off : n);
        if (!f->write(f->ctx, bank + (uint32_t)(data_offset(f) + off), chunk, n))
            return EMBED_NVPARAM_IO;
    }

    /* 2. ヘッダ (これを書き終えた時点で確定) */
    uint8_t hdr[32];
    memset(hdr, 0xFF, sizeof(hdr));
    put32(hdr, NV_MAGIC);
    hdr[4] = (uint8_t)nv->version;
    hdr[5] = (uint8_t)(nv->version >> 8);
    hdr[6] = (uint8_t)len;
    hdr[7] = (uint8_t)(len >> 8);
    put32(hdr + 8, seq);
    uint32_t state = header_crc_state(hdr);
    state = embed_crc_update(EMBED_CRC32_IEEE, state, data, len);
    put32(hdr + 12, embed_crc_finish(EMBED_CRC32_IEEE, state));
    if (!f->write(f->ctx, bank, hdr, data_offset(f)))
        return EMBED_NVPARAM_IO;

    /* 3. 読み戻して照合 */
    record_t rec;
    if (!check_bank(nv, target, &rec) || rec.seq != seq || rec.length != len)
        return EMBED_NVPARAM_IO;
    nv->active = target;
    nv->seq = seq;
    nv->saves++;
    return EMBED_NVPARAM_OK;
}

embed_nvparam_status embed_nvparam_erase(embed_nvparam_t *nv) {
    if (!nv || !nv->flash)
        return EMBED_NVPARAM_INVALID;
    const embed_nvparam_flash_t *f = nv->flash;
    for (int b = 0; b < 2; ++b)
        if (!f->erase(f->ctx, f->bank_addr[b], f->bank_size))
            return EMBED_NVPARAM_IO;
    nv->active = -1;
    nv->seq = 0;
    return EMBED_NVPARAM_OK;
}
