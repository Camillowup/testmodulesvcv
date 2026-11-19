// SPDX-License-Identifier: GPL-3.0-only
// FLUT – the factory presets, by style (matching GENRA's genres for Follow GENRA). The values are the knobs in FLUT's
// ParamId order. Plain C++ without Rack: test/neon_test.cpp plays every one and sets its level (gain).
#pragma once
#include "FlutDsp.hpp"

namespace flut {

enum PresetParam {
	P_VOICES, P_OCT, P_SPRD, P_DRIFT, P_MORPH, P_SUB, P_SUBOCT, P_TIDEMODE, P_TIME, P_DEPTH, P_PHS, P_SKEW,
	P_TEXTURE, P_BRIGHT, P_GRIT, P_BREATH, P_BLOOM, P_WIDTH, P_HAZE, P_SWELL, P_FADE, P_LEVEL, P_PLAYMODE,
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
	p.voices = (int) v[P_VOICES], p.oct = v[P_OCT], p.spread = v[P_SPRD], p.drift = v[P_DRIFT], p.morph = v[P_MORPH];
	p.sub = v[P_SUB], p.subOct = (int) v[P_SUBOCT] + 1, p.tideMode = (int) v[P_TIDEMODE], p.time = v[P_TIME];
	p.depth = v[P_DEPTH], p.phase = v[P_PHS], p.skew = v[P_SKEW];
	p.texture = (int) v[P_TEXTURE], p.bright = v[P_BRIGHT], p.grit = v[P_GRIT], p.breath = v[P_BREATH];
	p.bloom = v[P_BLOOM], p.width = v[P_WIDTH], p.haze = v[P_HAZE];
	p.swell = v[P_SWELL], p.fade = v[P_FADE], p.level = v[P_LEVEL], p.playMode = (int) v[P_PLAYMODE];
	p.outGain = gain;
	return p;
}

/** The TIME knob value of a SYNC length (index into TIDE_BARS: 0 = 1 bar … 7 = 128 bars). */
#define BR(i) ((float) (i) / (NUM_BARS - 1))
#define SY TM_SYNC
#define FR TM_FREE
#define DRN PM_DRONE
#define GAT PM_GATE
// clang-format off
//                                            vox oct  sprd drift morph sub  soct(0: -1, 1: -2) tide time   depth phs  skew   texture  brgt grit brth  bloom wid  haze  swell fade lev  mode
static const Preset PRESETS[] = {
	{"AMBIENT", "DEEP HARBOUR",               {6, 0.f, .40f, .30f, .40f, .30f, 0, SY, BR(5), .60f, .00f, .00f, X_GLASS, .50f, .00f, .10f, .30f, .70f, .55f, .40f, .50f, .80f, DRN}, /*gain*/ 0.775f},
	{"AMBIENT", "MOON CHOIR",                 {6, 0.f, .50f, .35f, .50f, .15f, 0, SY, BR(6), .70f, .00f, .20f, X_CHOIR, .45f, .00f, .20f, .40f, .80f, .65f, .50f, .60f, .80f, DRN}, /*gain*/ 0.628f},
	{"AMBIENT", "GLASS LAGOON",               {5, 0.f, .60f, .20f, .60f, .10f, 0, FR, .45f, .50f, .00f, .00f, X_GLASS, .65f, .00f, .00f, .60f, .80f, .70f, .50f, .60f, .80f, DRN}, /*gain*/ 0.867f},
	{"DRONE · BERLIN SCHOOL", "LOW TIDE ORGAN", {6, -1.f, .30f, .25f, .40f, .50f, 0, SY, BR(5), .50f, .00f, .00f, X_REED, .40f, .00f, .05f, .20f, .60f, .45f, .40f, .50f, .80f, DRN}, /*gain*/ 0.751f},
	{"DRONE · BERLIN SCHOOL", "ENDLESS BOW",  {6, -1.f, .40f, .40f, .60f, .40f, 1, FR, .60f, .70f, .00f, .30f, X_BOWED, .45f, .00f, .10f, .30f, .70f, .60f, .60f, .70f, .80f, DRN}, /*gain*/ 0.690f},
	{"DRONE · BERLIN SCHOOL", "SALT DUST",    {4, 0.f, .50f, .50f, .50f, .20f, 0, FR, .50f, .60f, .00f, .00f, X_DUST, .50f, .40f, .40f, .20f, .90f, .70f, .50f, .60f, .80f, DRN}, /*gain*/ 1.188f},
	{"TECHNO · ELECTRO · DETROIT · BERLIN", "DUB TIDE", {5, 0.f, .30f, .20f, .30f, .40f, 0, SY, BR(3), .70f, .00f, .40f, X_REED, .40f, .10f, .10f, .20f, .60f, .60f, .20f, .40f, .80f, DRN}, /*gain*/ 0.768f},
	{"TECHNO · ELECTRO · DETROIT · BERLIN", "WAREHOUSE MIST", {6, 0.f, .40f, .30f, .20f, .30f, 0, SY, BR(4), .80f, .00f, .00f, X_CHOIR, .50f, .20f, .20f, .30f, .70f, .70f, .30f, .40f, .80f, DRN}, /*gain*/ 0.464f},
	{"POP · SYNTHWAVE · TRANCE · MELODIC TECHNO", "SUNRISE SWELL", {6, 0.f, .50f, .25f, .40f, .20f, 0, SY, BR(4), .80f, .00f, .50f, X_BOWED, .60f, .00f, .05f, .50f, .80f, .50f, .30f, .40f, .80f, DRN}, /*gain*/ 0.854f},
	{"HOUSE · GARAGE", "SEA ORGAN",           {4, 0.f, .20f, .15f, .25f, .30f, 0, SY, BR(3), .40f, .00f, .00f, X_REED, .55f, .00f, .05f, .10f, .50f, .35f, .10f, .30f, .80f, GAT}, /*gain*/ 1.032f},
	{"SOUL · JAZZ · LO-FI", "VINYL FOG",      {5, 0.f, .40f, .40f, .40f, .20f, 0, FR, .40f, .50f, .00f, .00f, X_GLASS, .40f, .50f, .20f, .20f, .60f, .55f, .30f, .40f, .80f, GAT}, /*gain*/ 0.921f},
	{"DOWNTEMPO · TRIP-HOP", "HARBOUR LIGHTS", {5, 0.f, .40f, .30f, .40f, .30f, 0, SY, BR(4), .60f, .00f, .00f, X_CHOIR, .40f, .20f, .30f, .30f, .70f, .60f, .40f, .50f, .80f, DRN}, /*gain*/ 0.703f},
	{"WITCH HOUSE · HORROR · DUNGEON SYNTH", "DROWNED BELLS", {6, -1.f, .60f, .60f, .70f, .40f, 1, FR, .70f, .80f, .00f, -.40f, X_GLASS, .30f, .30f, .30f, .50f, .90f, .80f, .60f, .70f, .80f, DRN}, /*gain*/ 0.748f},
	{"DUB · REGGAE", "ECHO REEF",             {4, 0.f, .30f, .20f, .30f, .50f, 0, SY, BR(3), .50f, .00f, .00f, X_DUST, .40f, .20f, .30f, .20f, .60f, .80f, .30f, .50f, .80f, DRN}, /*gain*/ 0.918f},
};
// clang-format on
#undef BR
#undef SY
#undef FR
#undef DRN
#undef GAT
static const int NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

}  // namespace flut
