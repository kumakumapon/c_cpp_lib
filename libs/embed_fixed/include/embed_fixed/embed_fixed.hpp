#pragma once
#include "embed_fixed.h"
namespace embed_fixed {
template<int Bits> class Q;
#define EMBED_FIXED_CLASS(bits,type) template<> class Q<bits> { public: type raw=0; constexpr Q()=default; static constexpr Q from_raw(type x) {Q q;q.raw=x;return q;} friend Q operator+(Q a,Q b) {return from_raw(embed_q##bits##_add(a.raw,b.raw));} friend Q operator-(Q a,Q b) {return from_raw(embed_q##bits##_sub(a.raw,b.raw));} friend Q operator*(Q a,Q b) {return from_raw(embed_q##bits##_mul(a.raw,b.raw));} friend Q operator/(Q a,Q b) {return from_raw(embed_q##bits##_div(a.raw,b.raw));} friend bool operator<(Q a,Q b) {return a.raw<b.raw;} };
EMBED_FIXED_CLASS(15,int16_t)
EMBED_FIXED_CLASS(31,int32_t)
#undef EMBED_FIXED_CLASS
using Q15=Q<15>;using Q31=Q<31>;
/* Numeric-generic control adapters: choose float, Q15 or Q31; inputs/gains must be scaled into range. */
template<class Number> struct LowPass {Number value{};Number alpha{};Number update(Number input) {value=value+alpha*(input-value);return value;}};
template<class Number> struct PID {
    Number kp{},ki{},kd{},integral{},previous{},low{},high{};
    Number update(Number target,Number measured,Number dt) {
        Number error=target-measured;integral=integral+ki*error*dt;
        if(integral<low) integral=low;if(high<integral) integral=high;
        Number output=kp*error+integral+kd*((error-previous)/dt);previous=error;
        return output<low?low:high<output?high:output;
    }
};
}
