/* SPDX-License-Identifier: GPL-3.0-only
 * SWARM for VCV Rack: the C interface of the SWARM core (swarmcore/swarmcore.c).
 * The JIANT FM firmware's SWARM (eng_input.c, SOURCE SWARM: a drone made of the devices heard around) on one part,
 * with no radio: virtual devices send it made-up advertisements (sw_heard, as the BLE interrupt would): an address
 * (its note), a manufacturer (its timbre's family), data bytes (its FM modulator), a period (its rhythm, the jitter a
 * vibrato), a level (its closeness), a channel (its side). NEAR is the closeness (PROX). Its drawing is the firmware's
 * own (ui_engvis.c vx_swarm). Each module instance loads a private copy of the library; it exports one symbol, jsw_get. */
#ifndef SWARMCORE_H
#define SWARMCORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define JSW_API_VERSION 1u
enum { JSW_VIS_W = 232, JSW_VIS_H = 150 };

/* the part's parameters the module uses, by its own index */
enum {
    JSW_PLUCK, JSW_FM, JSW_IMAGE, JSW_DRIFT, JSW_OCTAVE, JSW_CHAOS,   /* E1 E2 E4 E5 E6 E7 */
    JSW_DIST, JSW_CHOR, JSW_DLY, JSW_REV,
    JSW_LEVEL, JSW_ROOT, JSW_SCALE,
    JSW_NP
};

typedef struct jsw_api {
    uint32_t api;                                    /* JSW_API_VERSION */
    uint32_t (*sample_rate)(void);                   /* the engine's rate (44100) */
    void (*init)(uint32_t seed);                     /* seed: the swarm's devices */
    void (*render)(uint32_t n, float *l, float *r);  /* n frames, -1..1 */
    void (*set)(uint32_t j, int32_t value);
    int32_t (*get)(uint32_t j);
    const char *(*label)(uint32_t j);
    int32_t (*min)(uint32_t j);
    int32_t (*max)(uint32_t j);
    void (*format)(uint32_t j, int32_t v, char *out, uint32_t n);
    uint32_t (*npresets)(void);
    const char *(*preset_name)(uint32_t i);
    void (*load_preset)(uint32_t i);
    const uint16_t *(*vis)(uint32_t w, uint32_t h);  /* RGB565, byte-swapped (the FM-1 panel's order) */
    /* the air: VOICES 1..8 devices, BUSY (how often they send), MOVE (how they walk, come and go), NEAR (the
     * closeness), each 0..1; called every block */
    void (*air)(uint32_t voices, float busy, float move, float nearv);
    void (*arrive)(void);                            /* a new device comes (the one heard longest ago goes) */
    void (*renew)(uint32_t seed);                    /* a new swarm: every device another */
    uint32_t (*seed)(void);                          /* the swarm's seed now (saved with the patch) */
    void (*set_key)(int32_t semitones);              /* the transpose (C4 = 0) */
    void (*set_clock)(uint32_t bpm, uint32_t pos);   /* the tempo; the samples (engine rate) since the song's start */
    uint32_t (*devices)(void);                       /* the devices heard now */
    uint32_t (*closeness)(void);                     /* the morph FAR 0 .. NEAR 128 */
} jsw_api;

typedef const jsw_api *(*jsw_get_fn)(void);

#ifdef __cplusplus
}
#endif
#endif
