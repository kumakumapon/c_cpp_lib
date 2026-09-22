#include "embed_selftest/embed_selftest.h"
#include <string.h>
bool embed_selftest_march_c(volatile uint32_t *ram,size_t n) {
    if(!ram || !n) return false;
    for(size_t i=0;i<n;++i) ram[i]=0;
    for(size_t i=0;i<n;++i) {if(ram[i]!=0) return false;ram[i]=UINT32_MAX;}
    for(size_t i=0;i<n;++i) {if(ram[i]!=UINT32_MAX) return false;ram[i]=0;}
    for(size_t i=n;i-->0;) {if(ram[i]!=0) return false;ram[i]=UINT32_MAX;}
    for(size_t i=n;i-->0;) {if(ram[i]!=UINT32_MAX) return false;ram[i]=0;}
    for(size_t i=0;i<n;++i) if(ram[i]!=0) return false;
    return true;
}
bool embed_selftest_rom(const void *rom,size_t n,uint32_t expected) {return rom && n && embed_crc(EMBED_CRC32_IEEE,rom,n)==expected;}
void embed_selftest_stack_fill(void *p,size_t n,uint8_t marker) {if(p) memset(p,marker,n);}
size_t embed_selftest_stack_unused(const void *p,size_t n,uint8_t marker,bool down) {
    if(!p) return 0;
    const uint8_t *b=(const uint8_t*)p;size_t i=0;
    while(i<n && b[down?i:n-1-i]==marker) ++i;
    return i;
}
bool embed_selftest_cpu(embed_selftest_cpu_fn fn,void *user) {return fn && fn(user);}
bool embed_selftest_clock(uint64_t measured,uint64_t expected,uint32_t ppm) {
    if(!expected || ppm>1000000) return false;
    uint64_t delta=measured>expected?measured-expected:expected-measured;
    uint64_t allowance=(expected/1000000)*ppm+((expected%1000000)*ppm)/1000000;
    return delta<=allowance;
}
bool embed_watchdog_init(embed_watchdog *w,uint64_t now,uint64_t min,uint64_t max,void (*feed)(void*),void *user) {
    if(!w || !feed || !max || min>max) return false;
    *w=(embed_watchdog){now,min,max,feed,user,false};return true;
}
bool embed_watchdog_poll(embed_watchdog *w,uint64_t now,bool healthy) {
    if(!w || !w->feed || w->fault) return false;
    if(!healthy || now<w->last || now-w->last>w->max_interval) {w->fault=true;return false;}
    if(now-w->last<w->min_interval) return false;
    w->feed(w->user);w->last=now;return true;
}
