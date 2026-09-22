#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "embed_modbus/modbus_slave.h"
#define EMBED_MODBUS_TCP_MAX_ADU 260
#define EMBED_MODBUS_TCP_PENDING 16
typedef struct {
    uint16_t transaction, length;
    uint8_t unit;
} embed_mbap;
size_t embed_mbap_encode(const embed_mbap *h, uint8_t *dst, size_t cap);
size_t embed_mbap_decode(const uint8_t *src, size_t len, embed_mbap *h);
size_t embed_modbus_tcp_handle(const modbus_slave_t *slave, const uint8_t *src, size_t len,
                               uint8_t *dst, size_t cap);
/* Connection state is caller-owned: one instance per socket. Feed may contain fragments or multiple
 * ADUs. */
typedef struct {
    uint8_t data[EMBED_MODBUS_TCP_MAX_ADU];
    size_t used;
    bool failed;
} embed_modbus_tcp_connection;
typedef void (*embed_modbus_tcp_send)(const uint8_t *, size_t, void *);
bool embed_modbus_tcp_feed(embed_modbus_tcp_connection *c, const modbus_slave_t *slave,
                           const uint8_t *data, size_t len, embed_modbus_tcp_send send, void *user);
/* RTU-over-TCP requires the caller to supply a complete RTU ADU (no MBAP). */
size_t embed_modbus_rtu_tcp_handle(const modbus_slave_t *, const uint8_t *, size_t, uint8_t *,
                                   size_t);
typedef struct {
    uint16_t next, ids[EMBED_MODBUS_TCP_PENDING];
    bool used[EMBED_MODBUS_TCP_PENDING];
} embed_modbus_transactions;
bool embed_modbus_transaction_begin(embed_modbus_transactions *, uint16_t *id);
bool embed_modbus_transaction_end(embed_modbus_transactions *, uint16_t id);

#ifdef __cplusplus
}
#endif
