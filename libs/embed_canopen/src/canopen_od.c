#include "embed_canopen/canopen_od.h"
#include <string.h>

void canopen_od_init(canopen_od_t *od) {
    if (!od) return;
    memset(od, 0, sizeof(canopen_od_t));
}

bool canopen_od_add_entry(canopen_od_t *od, uint16_t index, uint8_t subindex,
                          canopen_od_type_t type, canopen_od_access_t access,
                          void *data_ptr, size_t data_len) {
    if (!od || !data_ptr || data_len == 0) return false;

    // 既存エントリの検索・上書きチェック
    canopen_od_entry_t *existing = canopen_od_find_entry(od, index, subindex);
    if (existing) {
        existing->type = type;
        existing->access = access;
        existing->data_ptr = data_ptr;
        existing->data_len = data_len;
        return true;
    }

    if (od->count >= CANOPEN_MAX_OD_ENTRIES) {
        return false;
    }

    canopen_od_entry_t *entry = &od->entries[od->count++];
    entry->index = index;
    entry->subindex = subindex;
    entry->type = type;
    entry->access = access;
    entry->data_ptr = data_ptr;
    entry->data_len = data_len;
    return true;
}

canopen_od_entry_t* canopen_od_find_entry(canopen_od_t *od, uint16_t index, uint8_t subindex) {
    if (!od) return NULL;
    for (size_t i = 0; i < od->count; ++i) {
        if (od->entries[i].index == index && od->entries[i].subindex == subindex) {
            return &od->entries[i];
        }
    }
    return NULL;
}

bool canopen_od_read(const canopen_od_t *od, uint16_t index, uint8_t subindex,
                     void *dest, size_t max_dest_len, size_t *out_len) {
    if (!od || !dest) return false;
    for (size_t i = 0; i < od->count; ++i) {
        const canopen_od_entry_t *entry = &od->entries[i];
        if (entry->index == index && entry->subindex == subindex) {
            if (entry->access == OD_ACCESS_WO) {
                return false; // 書き込み専用
            }
            size_t copy_len = (entry->data_len <= max_dest_len) ? entry->data_len : max_dest_len;
            memcpy(dest, entry->data_ptr, copy_len);
            if (out_len) {
                *out_len = copy_len;
            }
            return true;
        }
    }
    return false;
}

bool canopen_od_write(canopen_od_t *od, uint16_t index, uint8_t subindex,
                      const void *src, size_t src_len) {
    if (!od || !src) return false;
    canopen_od_entry_t *entry = canopen_od_find_entry(od, index, subindex);
    if (!entry) return false;
    if (entry->access == OD_ACCESS_RO) return false; // 読み取り専用

    size_t copy_len = (src_len <= entry->data_len) ? src_len : entry->data_len;
    memcpy(entry->data_ptr, src, copy_len);
    return true;
}
