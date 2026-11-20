// SPDX-License-Identifier: GPL-3.0-only
// PULS – the factory presets, by style (matching GENRA's genres for Follow GENRA). The values are the knobs in PULS's
// ParamId order. Plain C++ without Rack: test/neon_test.cpp plays every one and sets its level (gain).
#pragma once
#include "PulsDsp.hpp"

namespace puls {

enum PresetParam {
	P_PMODE, P_RATE, P_F1, P_F2, P_SHAPE, P_WINDOW, P_DUTY, P_SKEW, P_PATTERN, P_DENS, P_ROT, P_PROB,
	P_CUTOFF, P_RES, P_DRIVE, P_SPREAD, P_JITTER, P_TAIL, P_ATK, P_DEC, P_SUS, P_REL, P_VMODE, P_GLIDE, P_ACCENT,
	NUM_PRESET_PARAMS
};

struct Preset {
	const char* style;
	const char* name;
	float v[NUM_PRESET_PARAMS];
	float gain;
	void values(float* out) const {
		for (int i = 0; i < NUM_PRESET_PARAMS; i++)
			out[i] = v[i];
	}
};

inline Params paramsFrom(const float* v, float gain) {
	Params p;
	p.pmode = (int) v[P_PMODE], p.rate = v[P_RATE], p.f1 = v[P_F1], p.f2 = v[P_F2];
	p.shape = (int) v[P_SHAPE], p.window = (int) v[P_WINDOW], p.duty = v[P_DUTY], p.skew = v[P_SKEW];
	p.pattern = (int) v[P_PATTERN], p.dens = v[P_DENS], p.rot = v[P_ROT], p.prob = v[P_PROB];
	p.cutoff = v[P_CUTOFF], p.res = v[P_RES], p.drive = v[P_DRIVE];
	p.spread = v[P_SPREAD], p.jitter = v[P_JITTER], p.tail = v[P_TAIL];
	p.atk = v[P_ATK], p.dec = v[P_DEC], p.sus = v[P_SUS], p.rel = v[P_REL];
	p.vmode = (int) v[P_VMODE], p.glide = v[P_GLIDE], p.accent = v[P_ACCENT];
	p.outGain = gain;
	return p;
}

/** The RATE knob value of a CLOCK note value. */
#define DV(i) ((float) (i) / (NUM_DIVS - 1))
#define CLK M_CLOCK
#define FRE M_FREE
#define NOT M_NOTE
#define POLY MODE_POLY
#define MONO MODE_MONO
#define LEG MODE_LEGATO
// clang-format off
//                                                    mode rate    f1   f2   shape    window  duty skew  pattern    dens rot  prob   cut  res  drv   sprd jit  tail  atk  dec  sus  rel   vmode glide acc
static const Preset PRESETS[] = {
	{"TECHNO · ELECTRO · DETROIT · BERLIN", "PULSAR HATS", {CLK, DV(5), .85f, .95f, S_NOISE, W_EXPO, .15f, .30f, PT_EUCLID, .75f, .00f, .90f, .95f, .10f, .10f, .60f, .10f, .15f, .00f, .50f, 1.0f, .30f, MONO, .00f, .50f}, /*gain*/ 2.112f},
	{"TECHNO · ELECTRO · DETROIT · BERLIN", "NEUTRON THUMP", {CLK, DV(2), .10f, .18f, S_SINE, W_EXPO, .50f, .30f, PT_ALL, 1.0f, .00f, 1.0f, .60f, .10f, .40f, .00f, .00f, .10f, .00f, .50f, 1.0f, .30f, MONO, .00f, .50f}, /*gain*/ 2.589f},
	{"MINIMAL · ACID", "CLICK GARDEN",                {CLK, DV(5), .60f, .80f, S_BELL, W_GAUSS, .10f, .50f, PT_RANDOM, .45f, .00f, .90f, .90f, .10f, .00f, .80f, .30f, .40f, .00f, .50f, 1.0f, .30f, MONO, .00f, .50f}, /*gain*/ 1.805f},
	{"AMBIENT", "STAR GRAINS",                        {FRE, .35f, .60f, .75f, S_SINE, W_HANN, .70f, .50f, PT_RANDOM, .60f, .00f, 1.0f, .85f, .05f, .00f, .90f, .50f, .80f, .20f, .50f, 1.0f, .60f, MONO, .00f, .30f}, /*gain*/ 1.271f},
	{"AMBIENT", "PULSAR VOICE",                       {FRE, .62f, .50f, .62f, S_SINE, W_GAUSS, .40f, .50f, PT_ALL, 1.0f, .00f, 1.0f, .80f, .05f, .00f, .40f, .10f, .50f, .30f, .50f, 1.0f, .60f, MONO, .00f, .30f}, /*gain*/ 1.567f},
	{"DRONE · BERLIN SCHOOL", "SUBHARMONIC CHOIR",    {FRE, .70f, .45f, .60f, S_TRI, W_GAUSS, .50f, .50f, PT_EUCLID, .50f, .00f, 1.0f, .75f, .10f, .00f, .50f, .05f, .60f, .40f, .50f, 1.0f, .70f, MONO, .00f, .30f}, /*gain*/ 1.723f},
	{"GRIME · DUBSTEP · DRILL", "FORMANT BASS",       {NOT, .50f, .30f, .45f, S_SAW, W_GAUSS, .60f, .50f, PT_ALL, 1.0f, .00f, 1.0f, .50f, .30f, .30f, .10f, .00f, .05f, .00f, .40f, .60f, .20f, MONO, .15f, .60f}, /*gain*/ 3.485f},
	{"POP · SYNTHWAVE · TRANCE · MELODIC TECHNO", "VOWEL LEAD", {NOT, .50f, .55f, .70f, S_SINE, W_GAUSS, .45f, .40f, PT_ALL, 1.0f, .00f, 1.0f, .80f, .10f, .10f, .20f, .05f, .30f, .02f, .50f, .80f, .35f, MONO, .20f, .50f}, /*gain*/ 1.913f},
	{"POP · SYNTHWAVE · TRANCE · MELODIC TECHNO", "GATED PULSAR PAD", {NOT, .50f, .45f, .65f, S_TRI, W_HANN, .70f, .50f, PT_EUCLID, .75f, .00f, 1.0f, .75f, .10f, .00f, .60f, .05f, .50f, .20f, .50f, .90f, .50f, POLY, .00f, .40f}, /*gain*/ 1.487f},
	{"HOUSE · GARAGE", "CHORD CLICKS",                {NOT, .50f, .60f, .70f, S_BELL, W_GAUSS, .30f, .50f, PT_BURST, .60f, .00f, 1.0f, .80f, .10f, .00f, .40f, .05f, .35f, .00f, .30f, .20f, .30f, POLY, .00f, .50f}, /*gain*/ 2.384f},
	{"SOUL · JAZZ · LO-FI", "STUTTER KEYS",           {NOT, .50f, .50f, .60f, S_SQUARE, W_GAUSS, .50f, .50f, PT_EUCLID, .60f, .00f, .90f, .60f, .10f, .10f, .40f, .10f, .35f, .00f, .50f, .40f, .40f, POLY, .00f, .40f}, /*gain*/ 2.223f},
	{"HIP-HOP · TRAP · PHONK", "TAPE PULSE",          {CLK, DV(3), .35f, .50f, S_SINE, W_EXPO, .40f, .40f, PT_EUCLID, .50f, .00f, 1.0f, .55f, .10f, .30f, .30f, .05f, .30f, .00f, .50f, 1.0f, .30f, MONO, .00f, .50f}, /*gain*/ 2.641f},
	{"PSYTRANCE", "ZAP RAIN",                         {CLK, DV(6), .75f, .90f, S_SAW, W_EXPO, .20f, .30f, PT_RANDOM, .70f, .00f, 1.0f, .85f, .40f, .20f, .70f, .40f, .30f, .00f, .50f, 1.0f, .30f, MONO, .00f, .50f}, /*gain*/ 2.486f},
	{"HARD TECHNO · HARDCORE · EBM · INDUSTRIAL", "INDUSTRIAL TICK", {CLK, DV(5), .70f, .85f, S_SQUARE, W_RECT, .08f, .50f, PT_EUCLID, .55f, .00f, 1.0f, .90f, .20f, .70f, .30f, .10f, .20f, .00f, .50f, 1.0f, .30f, MONO, .00f, .50f}, /*gain*/ 2.293f},
	{"WITCH HOUSE · HORROR · DUNGEON SYNTH", "HAUNTED PULSE", {FRE, .40f, .40f, .55f, S_BELL, W_REXPO, .80f, .50f, PT_RANDOM, .50f, .00f, 1.0f, .70f, .20f, .00f, .70f, .60f, .90f, .30f, .50f, 1.0f, .70f, MONO, .00f, .30f}, /*gain*/ 1.607f},
	{"DUB · REGGAE", "DUB SONAR",                     {CLK, DV(2), .40f, .50f, S_SINE, W_GAUSS, .25f, .50f, PT_EUCLID, .40f, .00f, 1.0f, .70f, .10f, .10f, .50f, .05f, .80f, .00f, .50f, 1.0f, .30f, MONO, .00f, .40f}, /*gain*/ 2.043f},
};
// clang-format on
#undef DV
#undef CLK
#undef FRE
#undef NOT
#undef POLY
#undef MONO
#undef LEG
static const int NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

}  // namespace puls
