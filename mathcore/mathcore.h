/* SPDX-License-Identifier: GPL-3.0-only
 * MATH for VCV Rack: the C interface of the MATH core (mathcore/mathcore.c).
 * The JIANT FM firmware's MATH engine (bytebeat / floatbeat, eng_lofi.c) on one part: its voices, envelope, filter and
 * effects as the FM-1 renders them, and its being (ui_engvis.c) drawn on a canvas. No device UI, no sequencer. Each
 * module instance loads a private copy of the library (the firmware's state is static); it exports one symbol,
 * jma_get. */
#ifndef MATHCORE_H
#define MATHCORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define JMA_API_VERSION 1u
enum { JMA_VIS_W = 232, JMA_VIS_H = 150 };

/* the part's parameters the module uses, by its own index */
enum {
    JMA_CHIP, JMA_WAVE,                              /* E0, E1: CHIP (4BIT 4B/2 8BIT 1BIT), WAVE (BYTE 5, FLOAT 6) */
    JMA_E2, JMA_E3, JMA_E4, JMA_E5, JMA_E6, JMA_E7,  /* ALGO VAR MOTION GRIT(FEEDBK) BEND LOOP */
    JMA_ATK, JMA_DEC, JMA_SUS, JMA_REL,
    JMA_DIST, JMA_CHOR, JMA_DLY, JMA_REV,
    JMA_LEVEL, JMA_VOICE, JMA_RTRG,
    JMA_NP
};

typedef struct jma_api {
    uint32_t api;                                    /* JMA_API_VERSION */
    uint32_t (*sample_rate)(void);                   /* the engine's rate (44100) */
    void (*init)(uint32_t seed);
    void (*render)(uint32_t n, float *l, float *r);  /* n frames, -1..1 */
    void (*note_on)(uint32_t note, uint32_t vel);    /* MIDI note 0..127, velocity 1..127 */
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
    void (*load_preset)(uint32_t i);
    const uint16_t *(*vis)(uint32_t w, uint32_t h);  /* RGB565, byte-swapped (the FM-1 panel's order) */
    uint32_t (*active_voices)(void);
    /* the pitch of every sounding voice, in 1/16 semitones (MIDI note x 16), without a new note: the formula's time
     * runs on (THRU) */
    void (*set_pitch)(int32_t pitch16);
    /* the song's clock for MOTION SYNC: the tempo, the quarter note since RESET, the samples (at the engine's rate)
     * into it */
    void (*set_clock)(uint32_t bpm, uint32_t quarter, uint32_t pos);
} jma_api;

typedef const jma_api *(*jma_get_fn)(void);

#ifdef __cplusplus
}
#endif
#endif
