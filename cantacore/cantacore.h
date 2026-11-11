/* SPDX-License-Identifier: GPL-3.0-only
 * CANTA for VCV Rack: the C interface of the CANTA core (cantacore/cantacore.c).
 * MIKU-FM's singing voice (firmware/audio/fvoice: an integer formant voice that sings Japanese morae; vfx: its effects)
 * without the band and the sequencer: the voice, its harmony voice, the four languages (JAPANESE lyrics, ANIMAL babble,
 * SCAT, VOWELS) as MIKU-FM's engine.c plays them, the lyrics grammar (firmware/song/lyrics.c) and own presets.
 * Each module instance loads a private copy of the library (the effects keep their buffers in static variables); it
 * exports one symbol, jca_get. */
#ifndef CANTACORE_H
#define CANTACORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define JCA_API_VERSION 1u

/* the voice's parameters, by the core's own index (0..100 unless noted) */
enum {
    JCA_BREATH, JCA_BRIGHT, JCA_GLIDE, JCA_TENSE, JCA_FRY, JCA_WHISPER, JCA_FORMANT, JCA_HUMAN,   /* VOICE (TENSE, FORMANT: 50 neutral) */
    JCA_ATK, JCA_REL, JCA_SCOOP, JCA_VIB,                                                       /* SHAPE */
    JCA_CHORUS, JCA_DELAY, JCA_REVERB, JCA_DRIVE,                                               /* FX */
    JCA_LEVEL,                                                                                  /* 0..127 */
    JCA_LANG, JCA_CHR,                                                                          /* JCA_JP.., JCA_DEEP.. */
    JCA_HINT, JCA_HMIX,                                                                         /* harmony: 0..6 (OFF -12 +3 +4 +5 +7 +12), mix */
    JCA_LYRICS,                                                                                 /* 0 FOLLOW (the lyrics, syllable by syllable), 1 FIXED */
    JCA_FIXED,                                                                                  /* the mora FIXED sings, 1..mora_count()-1 */
    JCA_VRATE, JCA_VDEL, JCA_TREM, JCA_RSIZE, JCA_DTIME, JCA_DFB, JCA_CRATE,                   /* deeper: vibrato rate, delay; tremolo; reverb size, delay time (0..4), feedback, chorus rate */
    JCA_CLEN, JCA_CLEV, JCA_VSPD, JCA_HDET, JCA_HCHR, JCA_ARATE,                               /* consonant length, level, vowel speed (50 = 100 %), harmony detune, character (0..4), babble rate */
    JCA_NP
};
enum { JCA_JP, JCA_ANIMAL, JCA_SCAT, JCA_VOWEL };                   /* languages */
enum { JCA_DEEP, JCA_LOW, JCA_MID, JCA_YOUNG, JCA_TINY };           /* characters (formant size) */
enum { JCA_REST = 1, JCA_SEPARATE = 2, JCA_SUSTAIN = 4, JCA_WORD_END = 8, JCA_PHRASE_END = 16 };  /* lyric flags */

typedef struct jca_api {
    uint32_t api;                                    /* JCA_API_VERSION */
    uint32_t (*sample_rate)(void);                   /* 44100 */
    void (*init)(uint32_t seed);
    void (*render)(uint32_t n, float *l, float *r);  /* n frames, -1..1 */
    void (*set)(uint32_t j, int32_t value);
    int32_t (*get)(uint32_t j);
    const char *(*label)(uint32_t j);
    int32_t (*min)(uint32_t j);
    int32_t (*max)(uint32_t j);
    void (*format)(uint32_t j, int32_t v, char *out, uint32_t n);
    /* notes (one voice): a new gate sings the next syllable (FOLLOW) or the FIXED mora; ANIMAL and SCAT babble on while
     * it is held. glide: a new pitch under the held gate (legato, the same vowel). next: the next syllable now. */
    void (*note_on)(uint32_t note, uint32_t vel);
    void (*glide)(uint32_t note);
    void (*next)(void);
    void (*note_off)(void);
    void (*all_off)(void);
    void (*set_bpm)(uint32_t bpm);                   /* SCAT's eighths */
    /* lyrics: MIKU-FM's grammar (ka-ra-o-ke, words by spaces, '_' holds the vowel, '/' a rest, '|' a phrase end) */
    int32_t (*set_lyrics)(const char *text);         /* -1: taken; else the byte where it fails (the lyrics stay) */
    uint32_t (*lyric_count)(void);
    const char *(*lyric_text)(uint32_t i);           /* the unit's mora ("" for a rest, "_" for a held vowel) */
    uint32_t (*lyric_flags)(uint32_t i);             /* JCA_REST .. */
    uint32_t (*lyric_pos)(void);                     /* the unit the next gate sings */
    void (*lyric_seek)(uint32_t i);
    int32_t (*lyric_sung)(void);                     /* the unit sounding now (-1: none, or not from the lyrics) */
    uint32_t (*mora_count)(void);                    /* the mora table (0 = none) */
    const char *(*mora_name)(uint32_t i);
    uint32_t (*sung_mora)(void);                     /* the mora sung last */
    uint32_t (*mouth)(void);                         /* 0..4 the vowel A I U E O, 5 closed */
    uint32_t (*level)(void);                         /* the envelope 0..255 */
    uint32_t (*gated)(void);
    uint32_t (*npresets)(void);
    const char *(*preset_name)(uint32_t i);
    const char *(*preset_lyrics)(uint32_t i);
    void (*load_preset)(uint32_t i);                 /* the sound (all parameters) and the preset's lyrics */
} jca_api;

typedef const jca_api *(*jca_get_fn)(void);

#ifdef __cplusplus
}
#endif
#endif
