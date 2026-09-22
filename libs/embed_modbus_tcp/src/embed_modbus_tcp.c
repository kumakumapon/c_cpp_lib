#include "embed_modbus_tcp/embed_modbus_tcp.h"
#include <string.h>
#include "embed_modbus/modbus_rtu.h"
static uint16_t get16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0]<<8)|p[1]); }
static void put16(uint8_t *p,uint16_t n) { p[0]=(uint8_t)(n>>8);p[1]=(uint8_t)n; }
size_t embed_mbap_encode(const embed_mbap *h, uint8_t *dst,size_t cap) {
    if (!h || !dst || cap<7 || h->length<2 || h->length>254) return 0;
    put16(dst,h->transaction);put16(dst+2,0);put16(dst+4,h->length);dst[6]=h->unit;return 7;
}
size_t embed_mbap_decode(const uint8_t *src,size_t len,embed_mbap *h) {
    if (!src || !h || len<7 || get16(src+2)!=0 || get16(src+4)<2 || get16(src+4)>254) return 0;
    h->transaction=get16(src);h->length=get16(src+4);h->unit=src[6];return 7;
}
size_t embed_modbus_tcp_handle(const modbus_slave_t *slave,const uint8_t *src,size_t len,uint8_t *dst,size_t cap) {
    embed_mbap h; uint8_t pdu[253];size_t n=0;
    if (!slave || !dst || cap<260 || !embed_mbap_decode(src,len,&h) || len!=(size_t)h.length+6 || h.unit!=slave->unit_id) return 0;
    if (!modbus_slave_process_pdu(slave,src+7,len-7,pdu,&n) || n==0 || n>253) return 0;
    h.length=(uint16_t)(n+1);embed_mbap_encode(&h,dst,cap);memcpy(dst+7,pdu,n);return n+7;
}
bool embed_modbus_tcp_feed(embed_modbus_tcp_connection *c,const modbus_slave_t *slave,const uint8_t *data,size_t len,embed_modbus_tcp_send send,void *user) {
    if (!c || !slave || (!data && len) || !send || c->failed || c->used>=sizeof(c->data)) return false;
    while (len--) {
        c->data[c->used++]=*data++;
        if (c->used>=7) {
            embed_mbap h;
            if (!embed_mbap_decode(c->data,c->used,&h)) {c->failed=true;return false;}
            if (c->used==(size_t)h.length+6) {
                uint8_t out[260];size_t n=embed_modbus_tcp_handle(slave,c->data,c->used,out,sizeof(out));
                c->used=0; if(n) send(out,n,user);
            }
        }
    }
    return true;
}
size_t embed_modbus_rtu_tcp_handle(const modbus_slave_t *s,const uint8_t *src,size_t len,uint8_t *dst,size_t cap) {
    size_t n=0;
    if (!s || !src || len<4 || len>256 || !dst || cap<256) return 0;
    return modbus_rtu_slave_handle(s,src,len,dst,&n) ? n : 0;
}
bool embed_modbus_transaction_begin(embed_modbus_transactions *t,uint16_t *id) {
    if(!t || !id) return false;
    unsigned slot;for(slot=0;slot<EMBED_MODBUS_TCP_PENDING && t->used[slot];++slot) {}
    if(slot==EMBED_MODBUS_TCP_PENDING) return false;
    for(unsigned attempt=0;attempt<=EMBED_MODBUS_TCP_PENDING;++attempt) {
        uint16_t candidate=t->next++;bool found=false;
        for(unsigned i=0;i<EMBED_MODBUS_TCP_PENDING;++i) if(t->used[i] && t->ids[i]==candidate) found=true;
        if(!found) {t->ids[slot]=candidate;t->used[slot]=true;*id=candidate;return true;}
    }
    return false;
}
bool embed_modbus_transaction_end(embed_modbus_transactions *t,uint16_t id) {
    if(!t) return false;
    for(unsigned i=0;i<EMBED_MODBUS_TCP_PENDING;++i) if(t->used[i] && t->ids[i]==id) {t->used[i]=false;return true;}
    return false;
}
