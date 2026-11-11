/* SPDX-License-Identifier: GPL-3.0-only
 * KESSEL for VCV Rack: the C interface of the KESSEL core (kesselcore/kesselcore.c).
 * The LXR firmware's sound engine (Erica Synths, kessel/Core): its six drum voices (three drum voices, snare, cymbal,
 * hi-hat) and its effect bus, as the LXR renders them, a block of 64 samples at 44.1 kHz at a time. No sequencer, no
 * menu. The parameters go in as the LXR's own (CC and CC2 numbers, 0..127), through the firmware's own handler.
 * Each module instance loads a private copy of the library (the firmware's state is static); it exports one symbol,
 * jks_get. */
#ifndef KESSELCORE_H
#define KESSELCORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define JKS_API_VERSION 1u
enum { JKS_BLOCK = 64 };

/* the voices as the core counts them */
enum { JKS_DRUM1, JKS_DRUM2, JKS_DRUM3, JKS_SNARE, JKS_CYM, JKS_HAT, JKS_NVOICE };
/* a voice's parameters as KESSEL has them (kesselcore.c: SLOTS), each 0..127 as the LXR takes it */
enum {
	JKS_S_WAVE, JKS_S_TUNE, JKS_S_FM, JKS_S_FMFREQ, JKS_S_CLICK, JKS_S_PAMT, JKS_S_PDEC, JKS_S_PAN,
	JKS_S_FTYPE, JKS_S_CUT, JKS_S_RES, JKS_S_DRIVE, JKS_S_LEVEL, JKS_S_ATK, JKS_S_DEC, JKS_S_DECOPEN,
	/* not on the panel: the kits set them */
	JKS_S_MODWAVE, JKS_S_TRANSWAVE, JKS_S_TRANSFREQ, JKS_S_MIXOSC, JKS_S_SLOPE, JKS_S_PSLOPE, JKS_S_DECIM,
	JKS_S_FILTDRIVE, JKS_S_FINE,
	JKS_NSLOT
};
/* the parameter pages: the LXR's CC numbers (MIDI_CC) and its second page (MIDI_CC2) */
enum { JKS_CC = 0, JKS_CC2 = 1 };
/* the effects of the bus (the LXR's FX types) */
enum { JKS_FX_OFF, JKS_FX_DIST, JKS_FX_RING, JKS_FX_COMP, JKS_FX_DELAY, JKS_FX_REVERB };

typedef struct jks_api {
	uint32_t api;                                          /* JKS_API_VERSION */
	uint32_t (*sample_rate)(void);                         /* the engine's rate (44100) */
	void (*init)(uint32_t seed);
	/** a parameter, as the LXR takes it: page JKS_CC or JKS_CC2, its number, 0..127 */
	void (*set)(uint32_t page, uint32_t num, uint32_t value);
	/** strike a voice (velocity 1..127); the hi-hat: open 1 for the open one */
	void (*trigger)(uint32_t voice, uint32_t vel, uint32_t open);
	/** the next block: JKS_BLOCK samples of every voice, -1..1, voice after voice (out[v * JKS_BLOCK + i]) */
	void (*render)(float *out);
	/** a voice's pan (0 left .. 127 right) */
	uint32_t (*pan)(uint32_t voice);
	/** the hi-hat struck last was the open one */
	uint32_t (*hat_open)(void);
	/** the effect bus: a block of stereo samples in (interleaved l r), the bus's output (dry/wet as set) out */
	void (*fx_process)(const float *in, float *out);
	/** the effect: type (JKS_FX_*), its sub type (distortion: 0 tube, 1 fold, 2 rat; delay: 0 normal, 1 ping-pong;
	 * ring: the wave), the four parameters 0..1, dry/wet 0..1 */
	void (*fx_set)(uint32_t type, uint32_t sub, const float *p4, float dryWet);
	/** the delay's time in samples (for the clock's divisions): its range and time knob set so that it lasts n */
	void (*fx_delay_samples)(float n);
	/** the LXR's init kit into every voice (the ground every KESSEL kit starts from) */
	void (*load_init)(void);
	/** a voice's parameter by KESSEL's slot (JKS_S_*), 0..127 */
	void (*set_slot)(uint32_t voice, uint32_t slot, uint32_t value);
	/** the voice has that slot */
	int32_t (*has_slot)(uint32_t voice, uint32_t slot);
} jks_api;

typedef const jks_api *(*jks_get_fn)(void);

#ifdef __cplusplus
}
#endif
#endif
