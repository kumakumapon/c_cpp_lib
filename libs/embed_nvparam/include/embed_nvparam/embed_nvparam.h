#pragma once
/*
 * embed_nvparam: 設定値・校正値をフラッシュに保存する。書き込み中の電源断に耐える。
 *
 * 2 つのバンク (消去単位) を交互に使う。保存は「古い方のバンクを消去 → データ → ヘッダ」の順で
 * 書き、ヘッダ (マジック・版・長さ・通し番号・CRC-32) を書き終えた時点で確定する。
 * どの時点で電源が切れても、読み出し時には CRC が正しい記録のうち通し番号が新しい方を使うので、
 * 「直前に保存した値」か「その前の値」のどちらかに必ず戻る (壊れた値は使わない)。
 *
 * フラッシュの操作は利用側の関数で行う (内蔵フラッシュ・SPI フラッシュ・EEPROM エミュレーション)。
 * 動的確保なし。スタックは 64 バイト程度。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* addr から len バイト読む */
    bool (*read)(void *ctx, uint32_t addr, void *dst, size_t len);
    /* 消去済みの領域へ書く。addr と len は write_align の倍数で渡す */
    bool (*write)(void *ctx, uint32_t addr, const void *src, size_t len);
    /* addr から len バイト (バンク全体) を消去する */
    bool (*erase)(void *ctx, uint32_t addr, size_t len);
    void *ctx;
    uint32_t bank_addr[2]; /* 2 つのバンクの先頭アドレス (重ならないこと) */
    size_t bank_size;      /* 1 バンクの大きさ (消去単位の倍数) */
    size_t write_align;    /* 書き込みの単位: 1, 2, 4, 8, 16, 32 (例: STM32L4 は 8) */
} embed_nvparam_flash_t;

typedef enum {
    EMBED_NVPARAM_OK = 0,
    EMBED_NVPARAM_EMPTY,   /* 有効な記録がない (初回起動など)。data は変更しない */
    EMBED_NVPARAM_VERSION, /* 有効な記録はあるが版が違う。data は変更しない */
    EMBED_NVPARAM_SIZE,    /* 有効な記録はあるが長さが違う。data は変更しない */
    EMBED_NVPARAM_IO,      /* フラッシュの操作か書き込み後の照合に失敗 */
    EMBED_NVPARAM_INVALID  /* 引数不正・記録がバンクに入らない */
} embed_nvparam_status;

typedef struct {
    const embed_nvparam_flash_t *flash;
    uint16_t version;
    int active;    /* 最新の有効な記録があるバンク (なければ -1) */
    uint32_t seq;  /* その通し番号 */
    uint32_t saves;
} embed_nvparam_t;

/* 構成を検査して初期化する (フラッシュは読まない)。version はアプリのデータ構造の版。 */
bool embed_nvparam_init(embed_nvparam_t *nv, const embed_nvparam_flash_t *flash, uint16_t version);
/* 保存できる最大のデータ長 */
size_t embed_nvparam_capacity(const embed_nvparam_t *nv);
/* 最新の有効な記録を読む。版と長さが一致したときだけ data へ書いて OK を返す。 */
embed_nvparam_status embed_nvparam_load(embed_nvparam_t *nv, void *data, size_t len);
/* 最新の有効な記録の版と長さを調べる (旧版データの移行用)。 */
embed_nvparam_status embed_nvparam_peek(embed_nvparam_t *nv, uint16_t *version, size_t *len);
/* 版に関係なく最新の記録を読む (移行用)。cap が足りなければ SIZE。 */
embed_nvparam_status embed_nvparam_load_raw(embed_nvparam_t *nv, void *data, size_t cap,
                                            size_t *len);
/* 保存する。書き込み後に読み戻して CRC を照合する。失敗しても前の記録は残る。 */
embed_nvparam_status embed_nvparam_save(embed_nvparam_t *nv, const void *data, size_t len);
/* 両バンクを消去する (工場出荷状態へ) */
embed_nvparam_status embed_nvparam_erase(embed_nvparam_t *nv);

#ifdef __cplusplus
}
#endif
