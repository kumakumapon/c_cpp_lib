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
 * @brief ODエントリを追加登録します。
 */
bool canopen_od_add_entry(canopen_od_t *od, uint16_t index, uint8_t subindex,
                          canopen_od_type_t type, canopen_od_access_t access,
                          void *data_ptr, size_t data_len);

/**
 * @brief インデックスとサブインデックスからODエントリを検索します。
 */
canopen_od_entry_t* canopen_od_find_entry(canopen_od_t *od, uint16_t index, uint8_t subindex);

/**
 * @brief ODエントリから値を読み出します。
 */
bool canopen_od_read(const canopen_od_t *od, uint16_t index, uint8_t subindex,
                     void *dest, size_t max_dest_len, size_t *out_len);

/**
 * @brief ODエントリへ値を書き込みます。
 */
bool canopen_od_write(canopen_od_t *od, uint16_t index, uint8_t subindex,
                      const void *src, size_t src_len);

#ifdef __cplusplus
}
#endif

#endif /* CANOPEN_OD_H */
