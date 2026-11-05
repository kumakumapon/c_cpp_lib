#include "embed_canopen/canopen_od.h"
#include <string.h>

void canopen_od_init(canopen_od_t *od) {
    if (!od)
        return;
    memset(od, 0, sizeof(canopen_od_t));
}

bool canopen_od_type_is_numeric(canopen_od_type_t type) {
    return type != OD_TYPE_VISIBLE_STRING && type != OD_TYPE_OCTET_STRING &&
           type != OD_TYPE_DOMAIN;
}

bool canopen_od_add_entry(canopen_od_t *od, uint16_t index, uint8_t subindex,
                          canopen_od_type_t type, canopen_od_access_t access, void *data_ptr,
                          size_t data_len) {
    if (!od || !data_ptr || data_len == 0)
        return false;
    canopen_od_entry_t *entry = canopen_od_find_entry(od, index, subindex);
    if (!entry) {
        if (od->count >= CANOPEN_MAX_OD_ENTRIES)
            return false;
        entry = &od->entries[od->count++];
        entry->index = index;
        entry->subindex = subindex;
    }
    entry->type = type;
    entry->access = access;
    entry->data_ptr = data_ptr;
    entry->data_len = data_len;
    entry->on_write = NULL;
    entry->hook_user = NULL;
    return true;
}

bool canopen_od_set_write_hook(canopen_od_t *od, uint16_t index, uint8_t subindex,
                               canopen_od_write_hook hook, void *user) {
    canopen_od_entry_t *entry = canopen_od_find_entry(od, index, subindex);
    if (!entry)
        return false;
    entry->on_write = hook;
    entry->hook_user = user;
    return true;
}

const canopen_od_entry_t *canopen_od_find_entry_const(const canopen_od_t *od, uint16_t index,
                                                      uint8_t subindex) {
    if (!od)
        return NULL;
    for (size_t i = 0; i < od->count; ++i)
        if (od->entries[i].index == index && od->entries[i].subindex == subindex)
            return &od->entries[i];
    return NULL;
}

canopen_od_entry_t *canopen_od_find_entry(canopen_od_t *od, uint16_t index, uint8_t subindex) {
    return (canopen_od_entry_t *)canopen_od_find_entry_const(od, index, subindex);
}

/* index だけ存在するならサブインデックス不在、index もなければオブジェクト不在 */
static uint32_t missing_code(const canopen_od_t *od, uint16_t index) {
    for (size_t i = 0; i < od->count; ++i)
        if (od->entries[i].index == index)
            return CANOPEN_SDO_ABORT_NO_SUBINDEX;
    return CANOPEN_SDO_ABORT_NO_OBJECT;
}

static size_t value_length(const canopen_od_entry_t *e) {
    if (e->type != OD_TYPE_VISIBLE_STRING)
        return e->data_len;
    const char *s = (const char *)e->data_ptr;
    size_t n = 0;
    while (n < e->data_len && s[n] != '\0')
        ++n;
    return n;
}

uint32_t canopen_od_read_checked(const canopen_od_t *od, uint16_t index, uint8_t subindex,
                                 void *dest, size_t max_dest_len, size_t *out_len) {
    if (!od || (!dest && max_dest_len))
        return CANOPEN_SDO_ABORT_GENERAL;
    const canopen_od_entry_t *e = canopen_od_find_entry_const(od, index, subindex);
    if (!e)
        return missing_code(od, index);
    if (e->access == OD_ACCESS_WO)
        return CANOPEN_SDO_ABORT_WRITE_ONLY;
    const size_t n = value_length(e);
    if (n > max_dest_len)
        return CANOPEN_SDO_ABORT_OUT_OF_MEMORY;
    if (n)
        memcpy(dest, e->data_ptr, n);
    if (out_len)
        *out_len = n;
    return 0;
}

uint32_t canopen_od_write_checked(canopen_od_t *od, uint16_t index, uint8_t subindex,
                                  const void *src, size_t src_len) {
    if (!od || (!src && src_len))
        return CANOPEN_SDO_ABORT_GENERAL;
    canopen_od_entry_t *e = canopen_od_find_entry(od, index, subindex);
    if (!e)
        return missing_code(od, index);
    if (e->access == OD_ACCESS_RO)
        return CANOPEN_SDO_ABORT_READ_ONLY;
    if (src_len > e->data_len)
        return CANOPEN_SDO_ABORT_LENGTH_HIGH;
    if (canopen_od_type_is_numeric(e->type) && src_len < e->data_len)
        return CANOPEN_SDO_ABORT_LENGTH_LOW;
    if (e->on_write) {
        const uint32_t code = e->on_write(e, src, src_len, e->hook_user);
        if (code)
            return code;
    }
    if (src_len)
        memcpy(e->data_ptr, src, src_len);
    if (src_len < e->data_len)
        memset((uint8_t *)e->data_ptr + src_len, 0, e->data_len - src_len);
    return 0;
}

bool canopen_od_read(const canopen_od_t *od, uint16_t index, uint8_t subindex, void *dest,
                     size_t max_dest_len, size_t *out_len) {
    if (!od || !dest)
        return false;
    const canopen_od_entry_t *e = canopen_od_find_entry_const(od, index, subindex);
    if (!e || e->access == OD_ACCESS_WO)
        return false;
    const size_t copy_len = e->data_len <= max_dest_len ? e->data_len : max_dest_len;
    memcpy(dest, e->data_ptr, copy_len);
    if (out_len)
        *out_len = copy_len;
    return true;
}

bool canopen_od_write(canopen_od_t *od, uint16_t index, uint8_t subindex, const void *src,
                      size_t src_len) {
    if (!src)
        return false;
    return canopen_od_write_checked(od, index, subindex, src, src_len) == 0;
}
