/* SPDX-License-Identifier: GPL-3.0-only
 * SAURUS for VCV Rack: the C interface of the SAURUS core (sauruscore/sauruscore.c).
 * PALEOTONE's creature voice (eng_saurus.c, on SLOOP and Felucca) on one part of the firmware: voices, envelope,
 * effects as the FM-1 renders them. No device UI, no sequencer. Each module instance loads a private copy of the
 * library (the firmware's state is static); it exports one symbol, jsa_get. */
#ifndef SAURUSCORE_H
#define SAURUSCORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define JSA_API_VERSION 1u

/* the part's parameters the module uses, by its own index */
enum {
    JSA_CALL, JSA_SIZE, JSA_SWOOP, JSA_LEN, JSA_GROWL, JSA_BRTH, JSA_MOUTH, JSA_WILD,   /* the engine's eight */
    JSA_ATK, JSA_DEC, JSA_SUS, JSA_REL,
    JSA_DIST, JSA_CHOR, JSA_DLY, JSA_REV,
    JSA_LEVEL, JSA_VOICE,
    JSA_NP
};
#define JSA_NCALL 11u                                /* CALL: HOLD BELLOW ROAR BARK HOOT TRILL CHIRP MOAN HISS SNORT KEYS */
#define JSA_CALL_KEYS 10u
#define JSA_NKIT 20u                                 /* KEYS: presets 0..19 on the keys from C2, one a semitone */
#define JSA_KIT_BASE 36u

typedef struct jsa_api {
    uint32_t api;                                    /* JSA_API_VERSION */
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
    void (*load_preset)(uint32_t i);                 /* the sound as the firmware loads it (VOICE, LEVEL stay) */
    uint32_t (*active_voices)(void);
    void (*set_bpm)(uint32_t bpm);                   /* the tempo the delay follows */
    const char *(*call_name)(uint32_t c);
    /* a call's shape: what 0 pitch (in 16ths of SWOOP's depth), 1 mouth (16 = MOUTH), 2 loudness (16 = full) at
     * 0, 1/4 .. 1 of LEN; out[5]. KEYS has none (returns 0) */
    int (*call_curve)(uint32_t c, uint32_t what, int32_t *out);
} jsa_api;

typedef const jsa_api *(*jsa_get_fn)(void);

#ifdef __cplusplus
}
#endif
#endif
