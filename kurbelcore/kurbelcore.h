/* SPDX-License-Identifier: GPL-3.0-only
 * KURBEL for VCV Rack: the C interface of the KURBEL core (kurbelcore/kurbelcore.c).
 * ESTAMPIE's hurdy-gurdy (GHOULBOX's GURDY engine, eng_gurdy.c) and its shawm (a preset of the TRIO engine) on one
 * part of the firmware: voices, envelope, temperament, effects as the FM-1 renders them. No device UI, no sequencer.
 * Each module instance loads a private copy of the library (the firmware's state is static); it exports one symbol,
 * jku_get. */
#ifndef KURBELCORE_H
#define KURBELCORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define JKU_API_VERSION 1u

/* the part's parameters the module uses, by its own index */
enum {
    JKU_E0, JKU_E1, JKU_E2, JKU_E3, JKU_E4, JKU_E5, JKU_E6, JKU_E7,   /* the engine's eight (WHEEL: DRN DLVL 5TH WHL BUZZ
                                                                     * COUP BODY WOBL; REED: WAVE INT2 INT3 DTN MODE CUT
                                                                     * RES PW) */
    JKU_ATK, JKU_DEC, JKU_SUS, JKU_REL,
    JKU_DIST, JKU_CHOR, JKU_DLY, JKU_REV,
    JKU_LEVEL, JKU_VOICE, JKU_RING, JKU_TEMP, JKU_ROOT,
    JKU_NP
};
enum { JKU_WHEEL, JKU_REED };                        /* the engines */
enum { JKU_COUP_CRANK = 5 };                         /* WHEEL's COUP (E5): HOLD 1/4 1/8 1/8T 1/16 CRANK */

typedef struct jku_api {
    uint32_t api;                                    /* JKU_API_VERSION */
    uint32_t (*sample_rate)(void);
    void (*init)(uint32_t seed);
    void (*render)(uint32_t n, float *l, float *r);  /* n frames, -1..1 */
    void (*note_on)(uint32_t note, uint32_t vel);
    void (*note_off)(uint32_t note);
    void (*all_off)(void);
    void (*set)(uint32_t j, int32_t value);
    int32_t (*get)(uint32_t j);
    const char *(*label)(uint32_t j);
    int32_t (*min)(uint32_t j);
    int32_t (*max)(uint32_t j);
    void (*format)(uint32_t j, int32_t v, char *out, uint32_t n);
    uint32_t (*npresets)(void);
    const char *(*preset_name)(uint32_t i);
    uint32_t (*preset_engine)(uint32_t i);           /* JKU_WHEEL / JKU_REED */
    void (*load_preset)(uint32_t i);                 /* the sound, its engine, RING as the firmware sets it */
    uint32_t (*engine)(void);                        /* the engine the part plays now */
    uint32_t (*active_voices)(void);
    void (*set_bpm)(uint32_t bpm);                   /* the tempo COUP's strokes follow */
    /* the crank (COUP CRANK): its speed in clicks per second, as GHOULBOX's encoder gives it (full pressure at
     * 60 .. 12 clicks/s, by GEAR); called every block. A sharp speed-up is a wrist stroke, as on the FM-1 */
    void (*crank)(float clicks_per_s, float dt);
    void (*stroke)(void);                            /* a wrist stroke now (the trompette's burst) */
    uint32_t (*strokes)(void);                       /* the strokes so far (wraps): for the display */
} jku_api;

typedef const jku_api *(*jku_get_fn)(void);

#ifdef __cplusplus
}
#endif
#endif
