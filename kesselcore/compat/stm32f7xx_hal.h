/* SPDX-License-Identifier: GPL-3.0-only
 * KESSEL for VCV Rack: a stand-in for the STM32 HAL and the Cortex-M intrinsics the LXR sound engine uses, so its
 * code runs on the host unchanged. (Only what the sound engine needs: no peripherals.) */
#ifndef KC_STM32F7XX_HAL_H
#define KC_STM32F7XX_HAL_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* the half-precision buffers of the effects: plain floats here */
#define __fp16 float
#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif
#define I2S_AUDIOFREQ_44K 44100u

typedef struct { uint32_t state; } RNG_HandleTypeDef;
uint32_t HAL_RNG_GetRandomNumber(RNG_HandleTypeDef *h);

static inline int16_t kc_sat16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t) v); }
/* QADD16 / QSUB16: two halfwords added (subtracted) with saturation */
static inline uint32_t __QADD16(uint32_t a, uint32_t b) {
	int16_t lo = kc_sat16((int32_t) (int16_t) (a & 0xffff) + (int16_t) (b & 0xffff));
	int16_t hi = kc_sat16((int32_t) (int16_t) (a >> 16) + (int16_t) (b >> 16));
	return ((uint32_t) (uint16_t) hi << 16) | (uint16_t) lo;
}
static inline uint32_t __QSUB16(uint32_t a, uint32_t b) {
	int16_t lo = kc_sat16((int32_t) (int16_t) (a & 0xffff) - (int16_t) (b & 0xffff));
	int16_t hi = kc_sat16((int32_t) (int16_t) (a >> 16) - (int16_t) (b >> 16));
	return ((uint32_t) (uint16_t) hi << 16) | (uint16_t) lo;
}
/* PKHBT: the bottom halfword of a, the bottom halfword of b (shifted by s) on top */
static inline uint32_t __PKHBT(uint32_t a, uint32_t b, uint32_t s) {
	return (a & 0xffffu) | ((b << s) & 0xffff0000u);
}
static inline int32_t __SSAT(int32_t v, uint32_t bits) {
	int32_t mx = (int32_t) ((1u << (bits - 1)) - 1), mn = -(int32_t) (1u << (bits - 1));
	return v > mx ? mx : (v < mn ? mn : v);
}
static inline uint32_t __USAT(int32_t v, uint32_t bits) {
	int32_t mx = (int32_t) ((1u << bits) - 1);
	return (uint32_t) (v > mx ? mx : (v < 0 ? 0 : v));
}
static inline int32_t __QADD(int32_t a, int32_t b) {
	int64_t s = (int64_t) a + b;
	return s > 2147483647LL ? 2147483647 : (s < -2147483648LL ? (int32_t) -2147483647 - 1 : (int32_t) s);
}
static inline uint8_t __CLZ(uint32_t v) {
	uint8_t n = 0;
	if (!v)
		return 32;
	while (!(v & 0x80000000u)) {
		v <<= 1;
		n++;
	}
	return n;
}
/* the overflow flag the noise oscillator reads after its phase step: not available here (the phase wrap is caught by
 * its other test) */
typedef union { uint32_t w; struct { uint32_t V; } b; } APSR_Type;
static inline uint32_t __get_APSR(void) { return 0; }
#endif
