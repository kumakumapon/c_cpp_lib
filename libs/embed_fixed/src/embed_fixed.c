#include "embed_fixed/embed_fixed.h"
#include <limits.h>
#include "fixed_tables.h"
static int32_t sat(int64_t x) {return x>INT32_MAX?INT32_MAX:x<INT32_MIN?INT32_MIN:(int32_t)x;}
int32_t embed_q31_add(int32_t a,int32_t b) {return sat((int64_t)a+b);}
int32_t embed_q31_sub(int32_t a,int32_t b) {return sat((int64_t)a-b);}
int32_t embed_q31_mul(int32_t a,int32_t b) {return sat(((int64_t)a*b)/INT64_C(2147483648));}
int32_t embed_q31_div(int32_t a,int32_t b) {return b?sat(((int64_t)a*INT64_C(2147483648))/b):a>0?INT32_MAX:a<0?INT32_MIN:0;}
static int32_t sine_phase(uint32_t phase) {
    bool neg=(phase & 0x80000000u)!=0;uint32_t q=phase & 0x7fffffffu;
    if(q>0x40000000u) q=0x80000000u-q;
    unsigned i=q>>22;uint32_t f=q & 0x3fffffu;
    int64_t v=sin_table[i];
    if(i<256) v+=((int64_t)(sin_table[i+1]-sin_table[i])*f)/4194304;
    return neg?(int32_t)-v:(int32_t)v;
}
int32_t embed_q31_sin(int32_t x) {return sine_phase((uint32_t)x);}
int32_t embed_q31_cos(int32_t x) {return sine_phase((uint32_t)x+0x40000000u);}
int32_t embed_q31_atan2(int32_t y,int32_t x) {
    if(!x && !y) return 0;
    uint64_t ax=x<0?-(int64_t)x:x,ay=y<0?-(int64_t)y:y;
    uint64_t lo=ax<ay?ax:ay,hi=ax>ay?ax:ay;
    uint64_t r=(lo*65536)/hi;unsigned i=(unsigned)(r>>8),f=(unsigned)(r & 255);
    int64_t angle=atan_table[i];
    if(i<256) angle+=((int64_t)(atan_table[i+1]-atan_table[i])*f)/256;
    if(ay>ax) angle=INT64_C(1073741824)-angle;
    if(x<0) angle=INT64_C(2147483648)-angle;
    if(y<0) angle=-angle;
    /* +pi and -pi share the representable phase -1. */
    return angle==INT64_C(2147483648)?INT32_MIN:(int32_t)angle;
}
int32_t embed_q31_sqrt(int32_t x) {
    if(x<=0) return 0;
    uint64_t n=(uint64_t)x*UINT64_C(2147483648),result=0,bit=UINT64_C(1)<<62;
    while(bit>n) bit>>=2;
    while(bit) {if(n>=result+bit) {n-=result+bit;result=(result>>1)+bit;}else result>>=1;bit>>=2;}
    return (int32_t)result;
}
static int16_t sat15(int32_t x) {return x>INT16_MAX?INT16_MAX:x<INT16_MIN?INT16_MIN:(int16_t)x;}
int16_t embed_q15_add(int16_t a,int16_t b) {return sat15((int32_t)a+b);}
int16_t embed_q15_sub(int16_t a,int16_t b) {return sat15((int32_t)a-b);}
int16_t embed_q15_mul(int16_t a,int16_t b) {return sat15(((int32_t)a*b)/32768);}
int16_t embed_q15_div(int16_t a,int16_t b) {return b?sat15(((int32_t)a*32768)/b):a>0?INT16_MAX:a<0?INT16_MIN:0;}
int16_t embed_q15_sin(int16_t x) {return (int16_t)(embed_q31_sin((int32_t)x*65536)/65536);}
int16_t embed_q15_cos(int16_t x) {return (int16_t)(embed_q31_cos((int32_t)x*65536)/65536);}
int16_t embed_q15_atan2(int16_t y,int16_t x) {return (int16_t)(embed_q31_atan2((int32_t)y*65536,(int32_t)x*65536)/65536);}
int16_t embed_q15_sqrt(int16_t x) {return (int16_t)(embed_q31_sqrt((int32_t)x*65536)/65536);}
