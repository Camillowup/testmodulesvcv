/* SPDX-License-Identifier: GPL-3.0-only */
/* The MIAOU core: the five sounds of MIAOU (github.com/pjac1810/miaou, src/toy.c, commit 417e693, GPL-3.0-only) and
 * its toy speaker, without the beats, songs, recorder, arpeggiator and sequencer. The engine is MIAOU's (integer,
 * 44.1 kHz, Q15); what was fixed in the firmware is scaled by seven parameters (MC_P_DECAY .. MC_P_SPEAKER). All state
 * lives in one struct, so every module instance has an engine of its own; plain C, no Rack dependency. */
#ifndef MIAOUCORE_H
#define MIAOUCORE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MC_RATE 44100
#define MC_VOICES 16
#define MC_MAXFRAMES 64

enum { MC_PIANO, MC_BELLS, MC_ORGAN, MC_BANJO, MC_MEOW, MC_SOUNDS };

/* the parameters, 0..1 each (0.5 = the firmware's own value, MC_P_BREATH 0.3, MC_P_KITTEN 0.5 = a third up) */
enum {
  MC_P_DECAY,   /* the sounds' decay and release times x0.25 .. x4 (and the banjo string's feedback) */
  MC_P_TONE,    /* brightness: piano and organ filter, the bells' FM index, the banjo's pluck */
  MC_P_MEOW,  /* the meow's length 0.2 .. 1.55 s (0.55 s at 0.5) */
  MC_P_SWOOP,   /* the meow's pitch bow x0 .. x2 */
  MC_P_BREATH,  /* the meow's breath 0 .. 3.3x (MIAOU's at 0.3) */
  MC_P_KITTEN,  /* the meow's formants -9 .. +9 semitones around MIAOU's kitten (0.5) */
  MC_P_SPEAKER, /* the toy speaker: 0 clean .. 1 no bass, a soft top, pushed, eight bits */
  MC_NPARAMS
};

typedef struct mc_engine mc_engine;

mc_engine *mc_new(uint32_t seed);
void mc_free(mc_engine *m);
void mc_reset(mc_engine *m);
void mc_set(mc_engine *m, int param, float v);
/* a note on one source (a channel): sound MC_PIANO..MC_MEOW, pitch in semitones (MIDI, fractional), velocity 1..127;
 * the same source again first lets its previous note go */
void mc_note_on(mc_engine *m, int src, int sound, float pitch, int vel);
void mc_note_off(mc_engine *m, int src);
/* a held note's pitch moves (V/OCT under a held gate; the banjo's string gets the new length and rings on) */
void mc_pitch(mc_engine *m, int src, float pitch);
void mc_all_off(mc_engine *m);
/* frames <= MC_MAXFRAMES, mono, -1..1 */
void mc_render(mc_engine *m, float *out, int frames);

/* for the display */
int mc_mouth(const mc_engine *m);       /* 0..255: how far the cat's mouth is open */
int mc_look(const mc_engine *m);        /* -8..8: where the cat looks (the last note, low to high) */
int mc_lit(const mc_engine *m, int note); /* 1: the note (MIDI) sounds */
int mc_active(const mc_engine *m);      /* voices sounding */

#ifdef __cplusplus
}
#endif
#endif
