/* SPDX-License-Identifier: GPL-3.0-only
 * KESSEL for VCV Rack: the KESSEL core. The LXR firmware's sound engine (kessel/Core, Erica Synths, GPL-3.0-only)
 * compiled for the host as it is (but for two reads past a table's end, kessel/SNAPSHOT.txt), with this file on top: what the firmware's mixer does every block (the filters'
 * frequencies, each voice's slow part and its block, the decimation), but each voice into a buffer of its own, so
 * the module gets every voice alone and mixes them itself; the effect bus on a stereo block of its own. The
 * parameters reach the voices through the firmware's own handler (kc_cc.c). Stand-ins for the few things of the rest
 * of the firmware the engine asks for (the faders, the random number unit, the sequencer's tempo). */
#include "kc_lxr.h"
#include "kesselcore.h"
#include <math.h>
#include "Preset/InitKit.h"
#include "Soundengine/MIDI/ParameterArray.h"

#if defined(_WIN32)
#define KC_EXPORT __declspec(dllexport)
#else
#define KC_EXPORT __attribute__((visibility("default")))
#endif

/* ------------------------------------------------------------------ stand-ins for the rest of the firmware */
RNG_HandleTypeDef hrng;
static uint32_t rngState = 0x12345678u;
uint32_t HAL_RNG_GetRandomNumber(RNG_HandleTypeDef *h) {
	(void) h;
	/* xorshift32, as random as the noise needs */
	uint32_t x = rngState;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	rngState = x;
	return x;
}

/* the LXR's volume faders: all up (KESSEL's levels are its own) */
float adc_getFaderVolume(uint8_t idx) {
	(void) idx;
	return 1.f;
}

/* the modulation targets (velocity, LFOs): KESSEL routes no modulation into the engine */
ModulationNode velocityModulators[6];
void modNode_init(ModulationNode *vm) { (void) vm; }
void modNode_updateValue(ModulationNode *vm, float val) {
	(void) vm;
	(void) val;
}

uint16_t seq_getBpm(void) { return 120; }

/* the fine tune, as the firmware's MIDI parser has it (MidiParser.c) */
#define SEMITONE_UP 1.0594630943592952645618252949463f
float midiParser_calcDetune(uint8_t value) {
	float frac = (value / 127.f - 0.5f);
	float cent = 1;
	cent += frac * (SEMITONE_UP - 1);
	return cent;
}

/* ------------------------------------------------------------------ the voices */
extern float mixer_decimation_rate[7];  /* kc_cc.c: the firmware's decimation, per voice and for all */
static float decCnt[6];
static int16_t decSample[6];
static uint8_t hatOpen = 0;

/* the firmware's mixer_decimateBlock */
static void decimate(int v, int16_t *buf) {
	float inc = mixer_decimation_rate[v] * mixer_decimation_rate[6];
	for (int i = 0; i < OUTPUT_DMA_SIZE; i++) {
		decCnt[v] += inc;
		if (decCnt[v] >= 1.f) {
			decCnt[v] -= 1.f;
			decSample[v] = buf[i];
		}
		buf[i] = decSample[v];
	}
}

/* A voice that has given nothing but zeros for KC_SLEEP_BLOCKS blocks (2 s: longer than any attack or clap repeat)
 * sleeps: its block is not worked out (it would be zeros) until the voice is struck or one of its parameters is set.
 * The slow part (calc...Async) always runs. */
#ifndef KC_SLEEP_BLOCKS
#define KC_SLEEP_BLOCKS 1378u
#endif
static uint32_t quietBlocks[JKS_NVOICE];
static uint8_t asleep[JKS_NVOICE];
static void wake(uint32_t v) {
	if (v < JKS_NVOICE) {
		asleep[v] = 0;
		quietBlocks[v] = 0;
	}
}
static void wakeAll(void) {
	for (uint32_t v = 0; v < JKS_NVOICE; v++)
		wake(v);
}

static Reverb reverb;
static uint8_t fxType = JKS_FX_OFF;

static void kc_init(uint32_t seed) {
	rngState = seed ? seed : 0x12345678u;
	initDrumVoice();
	Snare_init();
	Cymbal_init();
	HiHat_init();
	FxBus_init();
	Reverb_init(&reverb);
	memset(FX_BUFFER, 0, sizeof(FX_BUFFER));
	for (int v = 0; v < 7; v++)
		mixer_decimation_rate[v] = 1.f;
	for (int v = 0; v < 6; v++) {
		decCnt[v] = 0.f;
		decSample[v] = 0;
	}
	hatOpen = 0;
	fxType = JKS_FX_OFF;
	wakeAll();
}

static void kc_set(uint32_t page, uint32_t num, uint32_t value) {
	MidiMsg m;
	m.status = page == JKS_CC2 ? MIDI_CC2 : MIDI_CC;
	m.data1 = (uint8_t) num;
	m.data2 = (uint8_t) (value > 127 ? 127 : value);
	kc_ccHandler(m, 0, 0, 0.f);  /* (no "original value" bookkeeping: KESSEL keeps its own) */
	wakeAll();                   /* (a parameter by its LXR number: any voice's) */
}

static void kc_trigger(uint32_t voice, uint32_t vel, uint32_t open) {
	uint8_t v = (uint8_t) (vel < 1 ? 1 : (vel > 127 ? 127 : vel));
	wake(voice);
	switch (voice) {
	case JKS_DRUM1:
	case JKS_DRUM2:
	case JKS_DRUM3:
		Drum_trigger((uint8_t) voice, v, SEQ_DEFAULT_NOTE);
		break;
	case JKS_SNARE:
		Snare_trigger(v, SEQ_DEFAULT_NOTE);
		break;
	case JKS_CYM:
		Cymbal_trigger(v, SEQ_DEFAULT_NOTE);
		break;
	case JKS_HAT:
		hatOpen = open ? 1 : 0;
		HiHat_trigger(v, hatOpen, SEQ_DEFAULT_NOTE);
		break;
	default:
		break;
	}
}

static void kc_render(float *out) {
	SVF_recalcFreq(&voiceArray[0].filter);
	SVF_recalcFreq(&voiceArray[1].filter);
	SVF_recalcFreq(&voiceArray[2].filter);
	SVF_recalcFreq(&snareVoice.filter);
	SVF_recalcFreq(&cymbalVoice.filter);
	SVF_recalcFreq(&hatVoice.filter);

	calcDrumVoiceAsync(0);
	calcDrumVoiceAsync(1);
	calcDrumVoiceAsync(2);
	Snare_calcAsync();
	Cymbal_calcAsync();
	HiHat_calcAsync();

	int16_t buf[OUTPUT_DMA_SIZE];
	for (int v = 0; v < JKS_NVOICE; v++) {
		if (asleep[v]) {
			for (int i = 0; i < OUTPUT_DMA_SIZE; i++)
				out[v * JKS_BLOCK + i] = 0.f;
			continue;
		}
		switch (v) {
		case JKS_DRUM1:
		case JKS_DRUM2:
		case JKS_DRUM3:
			calcDrumVoiceSyncBlock((uint8_t) v, buf, OUTPUT_DMA_SIZE);
			break;
		case JKS_SNARE:
			Snare_calcSyncBlock(buf, OUTPUT_DMA_SIZE);
			break;
		case JKS_CYM:
			Cymbal_calcSyncBlock(buf, OUTPUT_DMA_SIZE);
			break;
		default:
			HiHat_calcSyncBlock(buf, OUTPUT_DMA_SIZE);
			break;
		}
		/* the firmware's order of the decimation rates: drum 1-3, snare, cymbal, hi-hat (as mixer.c) */
		decimate(v, buf);
		int any = 0;
		for (int i = 0; i < OUTPUT_DMA_SIZE; i++) {
			any |= buf[i];
			out[v * JKS_BLOCK + i] = buf[i] * (1.f / 32768.f);
		}
		if (any)
			quietBlocks[v] = 0;
		else if (++quietBlocks[v] >= KC_SLEEP_BLOCKS)
			asleep[v] = 1;
	}
}

static uint32_t kc_pan(uint32_t voice) {
	switch (voice) {
	case JKS_DRUM1:
	case JKS_DRUM2:
	case JKS_DRUM3:
		return voiceArray[voice].pan;
	case JKS_SNARE:
		return snareVoice.pan;
	case JKS_CYM:
		return cymbalVoice.pan;
	default:
		return hatVoice.pan;
	}
}

static uint32_t kc_hatOpen(void) { return hatOpen; }

/* ------------------------------------------------------------------ the effect bus */
static float fxP[4] = {-1.f, -1.f, -1.f, -1.f};
static uint32_t fxSub = 0xffffffffu;

static void kc_fxSet(uint32_t type, uint32_t sub, const float *p, float dryWet) {
	if (type != fxType) {
		/* the delay and the reverb share the effect memory: start clean */
		memset(FX_BUFFER, 0, sizeof(FX_BUFFER));
		if (type == JKS_FX_REVERB)
			Reverb_init(&reverb);
		else if (type == JKS_FX_DELAY)
			Delay_init(&fxBus.delay);
		fxType = (uint8_t) type;
		fxBus.fxType = (uint8_t) (type == JKS_FX_REVERB ? FX_OFF : type);
		fxSub = 0xffffffffu;
		for (int i = 0; i < 4; i++)
			fxP[i] = -1.f;
	}
	if (sub != fxSub) {
		fxSub = sub;
		if (type == JKS_FX_DIST)
			fxBus.distortion.distortionType = (uint8_t) sub;
		else if (type == JKS_FX_DELAY) {
			fxBus.delay.delayType = (uint8_t) sub;
			if (sub) {
				/* the ping-pong keeps both sides in one half of the memory each: the pointers the plain delay left
				 * anywhere in the whole of it come into that half (else its first sample reads and writes past
				 * the memory's end) */
				fxBus.delay.oldRead &= FX_BUFFER_SIZE >> 1;
				fxBus.delay.newRead &= FX_BUFFER_SIZE >> 1;
				fxBus.delay.write &= FX_BUFFER_SIZE >> 1;
			}
		} else if (type == JKS_FX_RING)
			Ringmod_setWave(&fxBus.ringmod, (uint8_t) sub);
	}
	for (int i = 0; i < 4; i++) {
		if (p[i] != fxP[i]) {
			fxP[i] = p[i];
			if (type == JKS_FX_REVERB) {
				switch (i) {
				case 0: Reverb_setDelayTime(&reverb, p[i]); break;
				case 1: Reverb_setFeedback(&reverb, p[i]); break;
				case 2: Reverb_setComposition(&reverb, p[i]); break;
				default: Reverb_setVariation(&reverb, p[i]); break;
				}
			} else {
				FxBus_setParameter((uint8_t) i, p[i]);
			}
		}
	}
	fxBus.dryWet = dryWet;
}

static void kc_fxDelaySamples(float n) {
	/* calcDelayTime (Delay.c) with the range at 1: from HI_MIN to HI_MAX (half of it for the ping-pong) */
	float max = fxBus.delay.delayType == DELAY_NORMAL ? (float) HI_MAX : (float) HI_MAX * 0.48f;
	float t = (n - (float) HI_MIN) / (max - (float) HI_MIN);
	t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
	if (fxBus.delay.range != 1.f || fxBus.delay.delayTime != t) {
		fxBus.delay.range = 1.f;
		Delay_setDelayTime(&fxBus.delay, t);
	}
}

static void kc_fxProcess(const float *in, float *out) {
	int16_t bus[OUTPUT_DMA_SIZE * 2];
	int16_t o1[OUTPUT_DMA_SIZE * 2];
	int16_t o2[OUTPUT_DMA_SIZE * 2];
	for (int i = 0; i < OUTPUT_DMA_SIZE * 2; i++) {
		float x = in[i] * 32767.f;
		bus[i] = (int16_t) (x > 32767.f ? 32767.f : (x < -32768.f ? -32768.f : x));
		o1[i] = 0;
		o2[i] = 0;
	}
	if (fxType == JKS_FX_REVERB) {
		Reverb_process(&reverb, bus, o2, o2 + 1, OUTPUT_DMA_SIZE, 2, fxBus.dryWet);
	} else {
		fxBus.audioRouting = MIXER_ROUTING_DAC1_STEREO;
		FxBus_process(bus, o1, o1 + 1, o2, o2 + 1, OUTPUT_DMA_SIZE);
	}
	for (int i = 0; i < OUTPUT_DMA_SIZE * 2; i++)
		out[i] = o2[i] * (1.f / 32768.f);
}


/* ------------------------------------------------------------------ KESSEL's parameters of a voice (its slots) */
/* For each voice and slot: the LXR parameter (page, number); -1: the voice has none. */
typedef struct { int8_t page; int16_t num; } kc_par;
#define C1(n) {JKS_CC, n}
#define C2(n) {JKS_CC2, n}
#define NONE {-1, -1}
static const kc_par SLOTS[JKS_NVOICE][JKS_NSLOT] = {
	/* WAVE TUNE FM FMFREQ CLICK PAMT PDEC PAN FTYPE CUT RES DRIVE LEVEL ATK DEC DECOPEN
	   MODWAVE TRANSWAVE TRANSFREQ MIXOSC SLOPE PSLOPE DECIM FILTDRIVE FINE */
	{C1(OSC_WAVE_DRUM1), C1(F_OSC1_COARSE), C1(FMAMNT1), C1(FMDTN1), C2(CC2_TRANS1_VOL), C1(MODAMNT1), C1(PITCHD1),
	 C1(PAN1), C2(CC2_FILTER_TYPE_1), C1(FILTER_FREQ_DRUM1), C1(RESO_DRUM1), C1(OSC1_DIST), C1(VOL1), C1(VELOA1),
	 C1(VELOD1), NONE, C1(MOD_WAVE_DRUM1), C2(CC2_TRANS1_WAVE), C2(CC2_TRANS1_FREQ), C2(CC2_MIX_MOD_1),
	 C1(VOL_SLOPE1), C1(PITCH_SLOPE1), C1(VOICE_DECIMATION1), C2(CC2_FILTER_DRIVE_1), C1(F_OSC1_FINE)},
	{C1(OSC_WAVE_DRUM2), C1(F_OSC2_COARSE), C1(FMAMNT2), C1(FMDTN2), C2(CC2_TRANS2_VOL), C1(MODAMNT2), C1(PITCHD2),
	 C1(PAN2), C2(CC2_FILTER_TYPE_2), C1(FILTER_FREQ_DRUM2), C1(RESO_DRUM2), C1(OSC2_DIST), C1(VOL2), C1(VELOA2),
	 C1(VELOD2), NONE, C1(MOD_WAVE_DRUM2), C2(CC2_TRANS2_WAVE), C2(CC2_TRANS2_FREQ), C2(CC2_MIX_MOD_2),
	 C1(VOL_SLOPE2), C1(PITCH_SLOPE2), C1(VOICE_DECIMATION2), C2(CC2_FILTER_DRIVE_2), C1(F_OSC2_FINE)},
	{C1(OSC_WAVE_DRUM3), C1(F_OSC3_COARSE), C1(FMAMNT3), C1(FMDTN3), C2(CC2_TRANS3_VOL), C1(MODAMNT3), C1(PITCHD3),
	 C1(PAN3), C2(CC2_FILTER_TYPE_3), C1(FILTER_FREQ_DRUM3), C1(RESO_DRUM3), C1(OSC3_DIST), C1(VOL3), C1(VELOA3),
	 C1(VELOD3), NONE, C1(MOD_WAVE_DRUM3), C2(CC2_TRANS3_WAVE), C2(CC2_TRANS3_FREQ), C2(CC2_MIX_MOD_3),
	 C1(VOL_SLOPE3), C1(PITCH_SLOPE3), C1(VOICE_DECIMATION3), C2(CC2_FILTER_DRIVE_3), C1(F_OSC3_FINE)},
	/* the snare: FM = the noise's share, FMFREQ = the noise's frequency, CLICK = REPEAT (the clap) */
	{C1(OSC_WAVE_SNARE), C1(F_OSC4_COARSE), C1(SNARE_MIX), C1(SNARE_NOISE_F), C1(REPEAT1), C1(MODAMNT4), C1(PITCHD4),
	 C1(PAN4), C2(CC2_FILTER_TYPE_4), C1(SNARE_FILTER_F), C1(SNARE_RESO), C1(SNARE_DISTORTION), C1(VOL4), C1(VELOA4),
	 C1(VELOD4), NONE, NONE, C2(CC2_TRANS4_WAVE), C2(CC2_TRANS4_FREQ), NONE,
	 C1(EG_SNARE1_SLOPE), C1(PITCH_SLOPE4), C1(VOICE_DECIMATION4), C2(CC2_FILTER_DRIVE_4), C1(F_OSC4_FINE)},
	/* the cymbal: FM / FMFREQ the first modulator, PAMT / PDEC the second, CLICK = REPEAT */
	{C1(CYM_WAVE1), C1(F_OSC5_COARSE), C1(CYM_MOD_OSC_GAIN1), C1(CYM_MOD_OSC_F1), C1(CYM_REPEAT), C1(CYM_MOD_OSC_GAIN2),
	 C1(CYM_MOD_OSC_F2), C1(PAN5), C2(CC2_FILTER_TYPE_5), C1(CYM_FIL_FREQ), C1(CYM_RESO), C1(CYMBAL_DISTORTION),
	 C1(VOL5), C1(VELOA5), C1(VELOD5), NONE, C1(CYM_WAVE2), C2(CC2_TRANS5_WAVE), C2(CC2_TRANS5_FREQ), NONE,
	 C1(CYM_SLOPE), NONE, C1(VOICE_DECIMATION5), C2(CC2_FILTER_DRIVE_5), C1(F_OSC5_FINE)},
	/* the hi-hat: FM / FMFREQ the first modulator, PAMT / PDEC the second; DEC closed, DECOPEN open */
	{C1(WAVE1_HH), C1(F_OSC6_COARSE), C1(MOD_OSC_GAIN1), C1(MOD_OSC_F1), C2(CC2_TRANS6_VOL), C1(MOD_OSC_GAIN2),
	 C1(MOD_OSC_F2), C1(PAN6), C2(CC2_FILTER_TYPE_6), C1(HAT_FILTER_F), C1(HAT_RESO), C1(HAT_DISTORTION), C1(VOL6),
	 C1(VELOA6), C1(VELOD6), C1(VELOD6_OPEN), C1(WAVE2_HH), C2(CC2_TRANS6_WAVE), C2(CC2_TRANS6_FREQ), NONE,
	 C1(VOL_SLOPE6), NONE, C1(VOICE_DECIMATION6), C2(CC2_FILTER_DRIVE_6), C1(F_OSC6_FINE)},
};

static void kc_setSlot(uint32_t voice, uint32_t slot, uint32_t value) {
	if (voice >= JKS_NVOICE || slot >= JKS_NSLOT)
		return;
	kc_par p = SLOTS[voice][slot];
	if (p.page < 0)
		return;
	MidiMsg m;
	m.status = p.page == JKS_CC2 ? MIDI_CC2 : MIDI_CC;
	m.data1 = (uint8_t) p.num;
	m.data2 = (uint8_t) (value > 127 ? 127 : value);
	kc_ccHandler(m, 0, 0, 0.f);
	wake(voice);                 /* (the voice may sound differently now: it is worked out again) */
}

static int32_t kc_hasSlot(uint32_t voice, uint32_t slot) {
	return voice < JKS_NVOICE && slot < JKS_NSLOT && SLOTS[voice][slot].page >= 0;
}

/* the LXR's init kit (Preset/initKit.c) into the voices, as its preset manager sends a kit to the sound engine:
 * parameter p below 128 as CC p + 1, the others as CC2 p - 128 */
static void kc_loadInit(void) {
	for (int i = 8; i < END_OF_SOUND_PARAMETERS && i < INIT_KIT_SIZE; i++) {
		int p = i - 8;
		if (p < 128)
			kc_set(JKS_CC, (uint32_t) ((p + 1) & 0x7f), initKit[i]);
		else
			kc_set(JKS_CC2, (uint32_t) ((p - 128) & 0x7f), initKit[i]);
	}
}

static uint32_t kc_rate(void) { return 44100u; }

static const jks_api API = {
	JKS_API_VERSION,
	kc_rate,
	kc_init,
	kc_set,
	kc_trigger,
	kc_render,
	kc_pan,
	kc_hatOpen,
	kc_fxProcess,
	kc_fxSet,
	kc_fxDelaySamples,
	kc_loadInit,
	kc_setSlot,
	kc_hasSlot,
};

KC_EXPORT const jks_api *jks_get(void) { return &API; }
