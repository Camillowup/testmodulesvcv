// SPDX-License-Identifier: GPL-3.0-only
// NEBEL – the factory presets, by style (matching GENRA's and GENRA TECHNO's genres for Follow GENRA). Plain C++ without
// Rack: test/neon_test.cpp plays every one and sets its level (gain).
#pragma once
#include "NebelDsp.hpp"

namespace nebel {

/** The knobs in NEBEL's ParamId order (the first NUM_PRESET_PARAMS params). */
static const int NUM_PRESET_PARAMS = 31;

struct Preset {
	const char* style;
	const char* name;
	int gen;
	float timbre, morph, oct;
	int chord;
	float spread;
	int voices;
	float detune, chaos, drift, strum, accent;
	int texture;
	float amount, color, density, width;
	int space;
	float size, decay, smix, tone;
	float atk, dec, sus, rel;
	int orate;
	float odepth, macro, glide;
	int src;
	float gain;  // the output level that makes this preset even with the others (test/neon_test.cpp)

	void values(float* v) const {
		const float a[NUM_PRESET_PARAMS] = {(float) gen, timbre, morph, oct, (float) chord, spread, (float) voices,
		                                    detune, chaos, drift, strum, accent,
		                                    (float) texture, amount, color, density, width,
		                                    (float) space, size, decay, smix, tone,
		                                    atk, dec, sus, rel,
		                                    (float) orate, odepth, macro, glide, (float) src};
		for (int i = 0; i < NUM_PRESET_PARAMS; i++)
			v[i] = a[i];
	}
	Params params() const {
		Params p;
		p.gen = gen, p.timbre = timbre, p.morph = morph, p.oct = oct, p.chord = chord, p.spread = spread;
		p.voices = voices, p.detune = detune, p.chaos = chaos, p.drift = drift, p.strum = strum, p.accent = accent;
		p.texture = texture, p.amount = amount, p.color = color, p.density = density, p.width = width;
		p.space = space, p.size = size, p.decay = decay, p.smix = smix, p.tone = tone;
		p.atk = atk, p.dec = dec, p.sus = sus, p.rel = rel;
		p.orate = orate, p.odepth = odepth, p.macro = macro, p.glide = glide, p.chordSrc = src;
		p.outGain = gain;
		return p;
	}
};

#define WT G_WAVETABLE
#define AN G_ANALOG
#define AD G_ADDITIVE
#define MO G_MODAL
#define ST G_STRING
#define PNL SRC_PANEL
#define GNR SRC_GENRA
// clang-format off
//  style, name,                 gen  timb morph oct  chord      spread vox det  chaos drift strum acc   texture    amt  col  dens width  space     size decay smix tone   atk  dec  sus  rel   orate odep macro glide src  gain
static const Preset PRESETS[] = {
	{"AMBIENT", "DEEP SPACE PAD", WT, .45f, .30f, 0.f, C_MIN9, .40f, 6, .30f, .10f, .30f, .00f, .50f, T_SHIMMER, .35f, .50f, .40f, .80f, V_NEBULA, .75f, .65f, .45f, .55f, .55f, .60f, .85f, .65f, 2, .40f, .00f, .10f, PNL, /*gain*/ 1.521f},
	{"AMBIENT", "STARDUST CHOIR", WT, .70f, .05f, 0.f, C_MAJ9, .50f, 8, .35f, .05f, .40f, .10f, .50f, T_GRAIN, .40f, .40f, .50f, .80f, V_NEBULA, .80f, .70f, .50f, .50f, .60f, .60f, .90f, .70f, 1, .50f, .00f, .10f, PNL, /*gain*/ 1.085f},
	{"AMBIENT", "GLASS GALAXY", MO, .50f, .70f, 0.f, C_ADD9, .60f, 6, .20f, .10f, .20f, .25f, .50f, T_SHIMMER, .50f, .70f, .30f, .90f, V_NEBULA, .80f, .75f, .55f, .70f, .01f, .55f, .60f, .70f, 3, .50f, .00f, .00f, PNL, /*gain*/ 1.917f},
	{"AMBIENT", "SOLAR WIND", AD, .40f, .20f, 0.f, C_SUS4, .70f, 8, .40f, .15f, .60f, .00f, .50f, T_SMEAR, .60f, .50f, .50f, .80f, V_HALL, .70f, .60f, .40f, .50f, .70f, .60f, .90f, .75f, 0, .60f, .00f, .20f, PNL, /*gain*/ 0.745f},
	{"DRONE · BERLIN SCHOOL", "VOID DRONE", AN, .30f, .25f, -1.f, C_FIFTHS, .60f, 8, .50f, .10f, .70f, .00f, .40f, T_SMEAR, .50f, .30f, .70f, .70f, V_VOID, .90f, .80f, .55f, .30f, .75f, .60f, 1.0f, .85f, 0, .30f, .00f, .20f, PNL, /*gain*/ 2.088f},
	{"DRONE · BERLIN SCHOOL", "BERLIN CLUSTER", AN, .55f, .55f, 0.f, C_MIN7, .30f, 4, .30f, .00f, .20f, .00f, .60f, T_DUST, .15f, .50f, .30f, .70f, V_HALL, .60f, .50f, .35f, .50f, .10f, .50f, .70f, .45f, 4, .30f, .00f, .10f, PNL, /*gain*/ 0.693f},
	{"DRONE · BERLIN SCHOOL", "DARK MATTER", ST, .30f, .70f, -1.f, C_CLUSTER, .40f, 6, .40f, .30f, .50f, .00f, .40f, T_FOLD, .30f, .40f, .50f, .70f, V_VOID, .85f, .80f, .50f, .25f, .60f, .60f, .90f, .80f, 1, .40f, .00f, .20f, PNL, /*gain*/ 3.867f},
	{"TECHNO · ELECTRO · DETROIT · BERLIN", "DUB TECHNO CLOUD", AN, .45f, .40f, 0.f, C_MIN7, .20f, 4, .25f, .00f, .20f, .00f, .60f, T_SMEAR, .40f, .40f, .60f, .70f, V_HALL, .70f, .60f, .45f, .40f, .02f, .45f, .30f, .55f, 6, .30f, .00f, .00f, GNR, /*gain*/ 1.219f},
	{"TECHNO · ELECTRO · DETROIT · BERLIN", "DETROIT STRINGS", WT, .55f, .70f, 0.f, C_MIN9, .30f, 6, .40f, .00f, .30f, .00f, .50f, T_SHIMMER, .20f, .60f, .20f, .80f, V_HALL, .60f, .50f, .30f, .60f, .30f, .50f, .80f, .50f, 3, .30f, .00f, .10f, GNR, /*gain*/ 1.204f},
	{"POP · SYNTHWAVE · TRANCE · MELODIC TECHNO", "MELODIC NEBULA", WT, .60f, .40f, 0.f, C_MAJ7, .50f, 8, .45f, .05f, .30f, .00f, .50f, T_SHIMMER, .40f, .70f, .40f, .90f, V_NEBULA, .70f, .60f, .40f, .65f, .35f, .55f, .85f, .55f, 4, .40f, .00f, .10f, GNR, /*gain*/ 1.072f},
	{"POP · SYNTHWAVE · TRANCE · MELODIC TECHNO", "TRANCE GATE PAD", AN, .70f, .70f, 0.f, C_MIN7, .40f, 7, .50f, .00f, .20f, .00f, .50f, T_GRAIN, .20f, .50f, .70f, .80f, V_HALL, .60f, .50f, .30f, .65f, .02f, .30f, .70f, .30f, 10, .70f, .00f, .00f, GNR, /*gain*/ 0.598f},
	{"POP · SYNTHWAVE · TRANCE · MELODIC TECHNO", "SYNTHWAVE SKY", AN, .45f, .55f, 0.f, C_MAJ9, .40f, 6, .40f, .00f, .30f, .05f, .50f, T_SHIMMER, .25f, .50f, .30f, .80f, V_HALL, .70f, .60f, .40f, .55f, .30f, .55f, .80f, .55f, 3, .30f, .00f, .10f, GNR, /*gain*/ 0.675f},
	{"HOUSE · GARAGE", "DEEP HOUSE CHORD", AN, .40f, .45f, 0.f, C_MIN9, .20f, 5, .20f, .00f, .10f, .03f, .60f, T_DUST, .10f, .60f, .20f, .60f, V_HALL, .50f, .40f, .25f, .50f, .01f, .40f, .30f, .40f, 6, .20f, .00f, .00f, GNR, /*gain*/ 1.220f},
	{"HOUSE · GARAGE", "ORGAN CLUSTER", AD, .60f, .10f, 0.f, C_MIN7, .20f, 4, .20f, .00f, .10f, .00f, .60f, T_SMEAR, .15f, .60f, .20f, .60f, V_HALL, .50f, .40f, .25f, .60f, .02f, .40f, .80f, .30f, 6, .30f, .00f, .00f, GNR, /*gain*/ 1.051f},
	{"SOUL · JAZZ · LO-FI", "LO-FI NEBULA KEYS", MO, .20f, .40f, 0.f, C_MAJ9, .20f, 4, .30f, .00f, .30f, .08f, .50f, T_DUST, .40f, .40f, .40f, .60f, V_HALL, .50f, .45f, .30f, .40f, .00f, .50f, .40f, .50f, 3, .20f, .00f, .00f, GNR, /*gain*/ 0.941f},
	{"SOUL · JAZZ · LO-FI", "JAZZ AURORA", AD, .45f, .30f, 0.f, C_DOM9, .30f, 5, .25f, .00f, .25f, .05f, .50f, T_SHIMMER, .20f, .50f, .20f, .70f, V_HALL, .60f, .50f, .35f, .55f, .20f, .55f, .70f, .55f, 3, .30f, .00f, .10f, GNR, /*gain*/ 1.061f},
	{"DOWNTEMPO · TRIP-HOP", "TRIP-HOP HAZE", ST, .40f, .60f, 0.f, C_MIN11, .30f, 6, .30f, .05f, .40f, .10f, .50f, T_DUST, .30f, .40f, .40f, .70f, V_HALL, .60f, .55f, .40f, .40f, .30f, .55f, .80f, .60f, 2, .30f, .00f, .10f, GNR, /*gain*/ 1.407f},
	{"WITCH HOUSE · HORROR · DUNGEON SYNTH", "HAUNTED NEBULA", WT, .80f, .00f, 0.f, C_CLUSTER, .50f, 7, .50f, .40f, .50f, .00f, .50f, T_GRAIN, .50f, .30f, .60f, .80f, V_VOID, .80f, .75f, .55f, .30f, .60f, .60f, .90f, .80f, 1, .50f, .00f, .20f, PNL, /*gain*/ 1.040f},
	{"WITCH HOUSE · HORROR · DUNGEON SYNTH", "BLACK HOLE", AN, .20f, .20f, -1.f, C_CHAOS, .60f, 8, .60f, .60f, .70f, .00f, .40f, T_FOLD, .40f, .30f, .70f, .80f, V_VOID, 1.0f, .90f, .60f, .20f, .80f, .60f, 1.0f, .90f, 0, .60f, .00f, .30f, PNL, /*gain*/ 8.361f},
	{"GRIME · DUBSTEP · DRILL", "DUBSTEP ATMOS", WT, .30f, .80f, 0.f, C_MIN7, .40f, 6, .30f, .10f, .30f, .00f, .50f, T_SMEAR, .40f, .40f, .50f, .80f, V_NEBULA, .70f, .60f, .40f, .40f, .40f, .55f, .85f, .60f, 2, .30f, .00f, .10f, GNR, /*gain*/ 1.433f},
	{"HIP-HOP · TRAP · PHONK", "TRAP CHOIR", WT, .65f, .05f, 0.f, C_MIN7, .30f, 6, .30f, .00f, .20f, .00f, .50f, T_SHIMMER, .25f, .50f, .30f, .70f, V_HALL, .60f, .50f, .35f, .50f, .25f, .55f, .80f, .50f, 3, .30f, .00f, .10f, GNR, /*gain*/ 1.379f},
	{"DUB · REGGAE", "DUB STARS", MO, .60f, .50f, 0.f, C_MIN7, .30f, 4, .20f, .00f, .20f, .05f, .60f, T_SHIMMER, .30f, .60f, .30f, .70f, V_NEBULA, .60f, .55f, .45f, .50f, .00f, .40f, .30f, .50f, 6, .30f, .00f, .00f, GNR, /*gain*/ 1.424f},
	{"PSYTRANCE", "PSY SWARM", AD, .70f, .60f, 0.f, C_WHOLE, .50f, 8, .60f, .30f, .40f, .00f, .50f, T_GRAIN, .40f, .60f, .70f, .90f, V_NEBULA, .70f, .60f, .40f, .60f, .20f, .50f, .80f, .50f, 10, .70f, .00f, .10f, PNL, /*gain*/ 0.770f},
	{"HARD TECHNO · HARDCORE · EBM · INDUSTRIAL", "INDUSTRIAL STORM", ST, .60f, .80f, 0.f, C_QUARTAL, .40f, 6, .50f, .40f, .40f, .00f, .60f, T_FOLD, .50f, .60f, .40f, .80f, V_VOID, .70f, .60f, .40f, .40f, .10f, .50f, .80f, .50f, 6, .40f, .00f, .00f, GNR, /*gain*/ 0.929f},
};
// clang-format on
#undef WT
#undef AN
#undef AD
#undef MO
#undef ST
#undef PNL
#undef GNR
static const int NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

}  // namespace nebel
