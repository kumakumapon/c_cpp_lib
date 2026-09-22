#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Signed Q1.15/Q1.31. Saturating arithmetic; division by zero saturates by sign (0/0=0).
 * Trig angles are normalized by pi: [-1,1) represents [-pi,pi).
 * sqrt of a negative input returns zero. No floating-point runtime operations. */
int16_t embed_q15_add(int16_t a,int16_t b);
int16_t embed_q15_sub(int16_t a,int16_t b);
int16_t embed_q15_mul(int16_t a,int16_t b);
int16_t embed_q15_div(int16_t a,int16_t b);
int16_t embed_q15_atan2(int16_t a,int16_t b);
int16_t embed_q15_sin(int16_t x);
int16_t embed_q15_cos(int16_t x);
int16_t embed_q15_sqrt(int16_t x);
int32_t embed_q31_add(int32_t a,int32_t b);
int32_t embed_q31_sub(int32_t a,int32_t b);
int32_t embed_q31_mul(int32_t a,int32_t b);
int32_t embed_q31_div(int32_t a,int32_t b);
int32_t embed_q31_atan2(int32_t a,int32_t b);
int32_t embed_q31_sin(int32_t x);
int32_t embed_q31_cos(int32_t x);
int32_t embed_q31_sqrt(int32_t x);

#ifdef __cplusplus
}
#endif
