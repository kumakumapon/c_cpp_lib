#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* 組み込みでよく使う CRC。名称は CRC RevEng カタログに合わせる。
 * 列挙値の順序は ABI の一部なので、追加は末尾に限る。 */
typedef enum {
    EMBED_CRC8_J1850,   /* CRC-8/SAE-J1850       車載 (J1850, AUTOSAR)       */
    EMBED_CRC16_CCITT,  /* CRC-16/IBM-3740       CCITT-FALSE                 */
    EMBED_CRC16_MODBUS, /* CRC-16/MODBUS         Modbus RTU                  */
    EMBED_CRC16_XMODEM, /* CRC-16/XMODEM         XMODEM, Bluetooth           */
    EMBED_CRC32_IEEE,   /* CRC-32/ISO-HDLC       Ethernet, zip, PNG          */
    EMBED_CRC32C,       /* CRC-32/ISCSI          Castagnoli, SCTP, ext4      */
    EMBED_CRC8_SMBUS,   /* CRC-8/SMBUS           SMBus PEC, ATM HEC 系        */
    EMBED_CRC8_MAXIM,   /* CRC-8/MAXIM-DOW       1-Wire (DS18B20 等)         */
    EMBED_CRC8_NRSC5,   /* CRC-8/NRSC-5          Sensirion SHT3x/SCD4x 等    */
    EMBED_CRC16_KERMIT, /* CRC-16/KERMIT         CCITT (反転)                */
    EMBED_CRC16_X25,    /* CRC-16/IBM-SDLC       X.25, HDLC, PPP FCS         */
    EMBED_CRC32_MPEG2,  /* CRC-32/MPEG-2         STM32 等のハードウェア CRC  */
    EMBED_CRC_KIND_COUNT
} embed_crc_kind;

/* Rocksoft/Williams モデルのパラメータ (refin == refout のアルゴリズムのみ)。
 * poly / init は正規形 (MSB ファースト) で与える。width は 1〜32。 */
typedef struct {
    uint8_t width;
    bool reflect;
    uint32_t poly;
    uint32_t init;
    uint32_t xorout;
} embed_crc_params_t;

/* update には finish 前の内部状態を渡す。finish が反転と xor-out を適用する。
 * 不正な種別: init / finish / embed_crc は 0、update は state をそのまま返す。
 * len == 0 なら data は NULL でよい。NULL かつ非 0 長は state を変更しない。 */
uint32_t embed_crc_init(embed_crc_kind kind);
uint32_t embed_crc_update(embed_crc_kind kind, uint32_t state, const void *data, size_t len);
uint32_t embed_crc_finish(embed_crc_kind kind, uint32_t state);
uint32_t embed_crc(embed_crc_kind kind, const void *data, size_t len);
/* 種別のパラメータを取得する (embed_crc_params_* と組み合わせて検証にも使える)。 */
bool embed_crc_get_params(embed_crc_kind kind, embed_crc_params_t *out);

/* 任意パラメータの CRC (ビット演算。テーブル設定に関係なく動く)。
 * params が NULL または width が範囲外なら init / finish / compute は 0、update は state。 */
uint32_t embed_crc_params_init(const embed_crc_params_t *params);
uint32_t embed_crc_params_update(const embed_crc_params_t *params, uint32_t state, const void *data,
                                 size_t len);
uint32_t embed_crc_params_finish(const embed_crc_params_t *params, uint32_t state);
uint32_t embed_crc_params_compute(const embed_crc_params_t *params, const void *data, size_t len);

/* フレーム末尾に付けた CRC を含めて検査する。CRC は反転方式ならリトルエンディアン、
 * 非反転方式ならビッグエンディアンで、幅のバイト数 (8/16/32bit のみ) だけ置く。 */
bool embed_crc_verify(embed_crc_kind kind, const void *frame_with_crc, size_t len);
/* CRC を上記の規則で dst へ書き、書いたバイト数 (1/2/4) を返す。cap 不足なら 0。 */
size_t embed_crc_append(embed_crc_kind kind, uint32_t crc, uint8_t *dst, size_t cap);

uint16_t embed_fletcher16(uint16_t state, const void *data, size_t len);
uint32_t embed_adler32(uint32_t state, const void *data, size_t len);

#ifdef __cplusplus
}
#endif
