#pragma once
#ifndef CANOPEN_OD_H
#define CANOPEN_OD_H

#include "canopen_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    canopen_od_entry_t entries[CANOPEN_MAX_OD_ENTRIES];
    size_t count;
} canopen_od_t;

/**
 * @brief オブジェクトディクショナリを初期化します。
 */
void canopen_od_init(canopen_od_t *od);

/**
 * @brief ODエントリを追加登録します (同じ index/subindex は置き換え。フックは解除)。
 */
bool canopen_od_add_entry(canopen_od_t *od, uint16_t index, uint8_t subindex,
                          canopen_od_type_t type, canopen_od_access_t access,
                          void *data_ptr, size_t data_len);

/**
 * @brief 書き込み検証フックを設定します (NULL で解除)。SDO・RPDO・canopen_od_write に適用。
 */
bool canopen_od_set_write_hook(canopen_od_t *od, uint16_t index, uint8_t subindex,
                               canopen_od_write_hook hook, void *user);

/**
 * @brief インデックスとサブインデックスからODエントリを検索します。
 */
canopen_od_entry_t* canopen_od_find_entry(canopen_od_t *od, uint16_t index, uint8_t subindex);
const canopen_od_entry_t *canopen_od_find_entry_const(const canopen_od_t *od, uint16_t index,
                                                      uint8_t subindex);

/**
 * @brief ODエントリから値を読み出します (ホストのバイト順)。
 *
 * 格納先が小さい場合は先頭 max_dest_len バイトだけをコピーします (互換動作)。
 */
bool canopen_od_read(const canopen_od_t *od, uint16_t index, uint8_t subindex,
                     void *dest, size_t max_dest_len, size_t *out_len);

/**
 * @brief ODエントリへ値を書き込みます (ホストのバイト順)。canopen_od_write_checked が 0 なら true。
 *
 * 数値型は長さが一致しないと失敗します (旧版は黙って切り詰め・部分書き込みしていた)。
 */
bool canopen_od_write(canopen_od_t *od, uint16_t index, uint8_t subindex,
                      const void *src, size_t src_len);

/**
 * @brief 検査付き読み出し。0 または SDO アボートコードを返します。
 *
 * 文字列型 (VISIBLE_STRING) の長さは最初の NUL まで。格納先が足りなければ OUT_OF_MEMORY。
 */
uint32_t canopen_od_read_checked(const canopen_od_t *od, uint16_t index, uint8_t subindex,
                                 void *dest, size_t max_dest_len, size_t *out_len);

/**
 * @brief 検査付き書き込み。0 または SDO アボートコードを返します。
 *
 * 数値型は長さ一致が必要 (長い: LENGTH_HIGH、短い: LENGTH_LOW)。文字列・OCTET・DOMAIN は
 * 格納領域以下の長さを受け付け、残りを 0 で埋めます。フックが拒否すると値は変わりません。
 */
uint32_t canopen_od_write_checked(canopen_od_t *od, uint16_t index, uint8_t subindex,
                                  const void *src, size_t src_len);

/** 数値型 (PDO にマップでき、送受信時にリトルエンディアンへ変換する型) か */
bool canopen_od_type_is_numeric(canopen_od_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* CANOPEN_OD_H */
