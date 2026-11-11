/* SPDX-License-Identifier: GPL-3.0-only
 * FUMI for VCV Rack: the C interface of the FUMI core (fumicore/fumicore.c).
 * FuMi-1's instrument (fumi.tar.gz: firmware/src/dsp, unchanged but for its patch table): the pitch core with the
 * ST-50's tunings, 本数 and scales, the trill in pairs and the ornaments, 6-operator FM, the plate reverb. No looper,
 * no device UI. Each module instance loads a private copy of the library (the instrument's state is static); it exports
 * one symbol, jfu_get. */
#ifndef FUMICORE_H
#define FUMICORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define JFU_API_VERSION 1u

/* FuMi-1's parameters (fumi.h's enum, the ones FUMI uses; the core checks they match) */
enum {
    JFU_HON = 0, JFU_SCALE = 1, JFU_TUNING = 2, JFU_DEPTH = 3, JFU_FINE = 4, JFU_A4 = 5,
    JFU_VOICE = 7, JFU_YOIN = 8, JFU_LEVEL = 9,
    JFU_VIB_ON = 10, JFU_VIB_RATE = 11, JFU_VIB_DEPTH = 12,
    JFU_TRILL_ON = 13, JFU_TRILL_RATE = 14, JFU_TRILL_FEEL = 15,
    JFU_MONO = 16, JFU_SLIDE = 21,
    JFU_CHARACTER = 25, JFU_REVERB = 26,
    JFU_OROSHI = 48,
    JFU_NP = 49
};
/* the ornaments (fumi.h ORN_*) */
enum { JFU_ORN_VIB, JFU_ORN_TRILL, JFU_ORN_DAMP, JFU_ORN_STRONG, JFU_ORN_OSHIDE, JFU_ORN_HIKIIRO, JFU_ORN_ATOOSHI, JFU_ORN_N };
#define JFU_NCH 16                                   /* note channels (a poly cable's) */

typedef struct jfu_api {
    uint32_t api;                                    /* JFU_API_VERSION */
    uint32_t (*sample_rate)(void);
    void (*init)(uint32_t seed);
    void (*render)(uint32_t n, float *l, float *r);  /* n frames (a multiple of 32), -1..1 */
    void (*set)(uint32_t p, int32_t v);
    int32_t (*get)(uint32_t p);
    int32_t (*min)(uint32_t p);
    int32_t (*max)(uint32_t p);
    int32_t (*def)(uint32_t p);
    void (*format)(uint32_t p, int32_t v, char *out, uint32_t n);
    /* a channel's note: on (vel 1..127) or off; cents from A4 */
    void (*note)(uint32_t ch, int on, float cents, int vel);
    /* the pitch of a note as FUMI plays it, cents from A4: midi (fractional, V/OCT's) chromatic with the tuning's
     * offsets, or snapped to the nearest degree of the scale (the ST-50's keys) */
    float (*pitch)(float midi, int snap);
    float (*white_cents)(uint32_t key);              /* white key 0..15 (mi fa la ti do x3, top mi): cents from A4 */
    int (*san_midi)(void);                           /* 三's MIDI note (本数) */
    void (*ornament)(uint32_t orn, int on);
    void (*bend)(int up, int down, int held);        /* the sprung bend: up / down pressed, a note held */
    void (*panic)(void);
    uint32_t (*npatches)(void);
    const char *(*patch_name)(uint32_t i);
    const char *(*patch_ja)(uint32_t i);             /* UTF-8 */
    uint32_t (*patch_family)(uint32_t i);            /* 0 kotos, 1 bells, 2 winds, 3 sustained, 4 percussion */
    uint32_t (*active_voices)(void);
    float (*peak)(void);
} jfu_api;

typedef const jfu_api *(*jfu_get_fn)(void);

#ifdef __cplusplus
}
#endif
#endif
