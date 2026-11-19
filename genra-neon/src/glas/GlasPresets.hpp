// SPDX-License-Identifier: GPL-3.0-only
// GLAS – the factory presets, by style (matching GENRA's genres for Follow GENRA). The values are the knobs in GLAS's
// ParamId order; a preset also paints its BANK picture (seeded by its number). Plain C++ without Rack:
// test/neon_test.cpp plays every one and sets its level (gain).
#pragma once
#include "GlasDsp.hpp"

namespace glas {

enum PresetParam {
	P_TOOL, P_SIZE, P_BANK, P_PLAYMODE, P_LEN, P_DIR, P_SPEED, P_POS, P_SCALE, P_LOW, P_RNG, P_TILT,
	P_ATK, P_DEC, P_SUS, P_REL, P_SPREAD, P_BLUR, P_SHIMMER,
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
	p.playMode = (int) v[P_PLAYMODE], p.len = (int) v[P_LEN], p.dir = (int) v[P_DIR];
	p.speed = v[P_SPEED], p.pos = v[P_POS], p.scale = (int) v[P_SCALE];
	p.low = v[P_LOW], p.range = v[P_RNG], p.tilt = v[P_TILT];
	p.atk = v[P_ATK], p.dec = v[P_DEC], p.sus = v[P_SUS], p.rel = v[P_REL];
	p.spread = v[P_SPREAD], p.blur = v[P_BLUR], p.shimmer = v[P_SHIMMER];
	p.outGain = gain;
	return p;
}
/** The seed a preset paints its picture with. */
inline uint32_t presetSeed(int i) { return 1000u + (uint32_t) i * 17u; }

#define DRN PM_DRONE
#define GAT PM_GATE
// clang-format off
//                                              tool size bank      mode len dir     speed pos  scale          low  rng  tilt   atk  dec  sus  rel   sprd blur shim
static const Preset PRESETS[] = {
	{"AMBIENT", "FROZEN CHOIR",                 {0, .30f, B_CHOIR,  DRN, 3, D_FWD,  .50f, .00f, SC_GENRA_CHORD, .40f, .72f, .00f, .45f, .50f, 1.0f, .60f, .70f, .40f, .20f}, /*gain*/ 1.325f},
	{"AMBIENT", "AURORA GLASS",                 {0, .30f, B_AURORA, DRN, 4, D_PING, .50f, .00f, SC_GENRA_KEY,   .45f, .72f, .20f, .50f, .50f, 1.0f, .60f, .80f, .60f, .40f}, /*gain*/ 2.166f},
	{"DRONE · BERLIN SCHOOL", "MURZIN DRONE",   {0, .30f, B_DRONE,  DRN, 4, D_FWD,  .50f, .00f, SC_GENRA_KEY,   .35f, .72f, -.20f, .50f, .50f, 1.0f, .60f, .50f, .30f, .00f}, /*gain*/ 1.232f},
	{"DRONE · BERLIN SCHOOL", "GLASS GLISSANDI", {0, .30f, B_GLISS, DRN, 3, D_PING, .50f, .00f, SC_FREE,        .40f, .72f, .00f, .40f, .50f, 1.0f, .60f, .60f, .20f, .10f}, /*gain*/ 1.773f},
	{"WITCH HOUSE · HORROR · DUNGEON SYNTH", "SPECTRAL FOG", {0, .30f, B_NOISE, DRN, 4, D_FWD, .50f, .00f, SC_FREE, .30f, .80f, -.30f, .60f, .50f, 1.0f, .70f, .80f, .70f, .30f}, /*gain*/ 2.442f},
	{"TECHNO · ELECTRO · DETROIT · BERLIN", "GLASS PULSE", {0, .30f, B_PULSE, DRN, 1, D_FWD, .50f, .00f, SC_GENRA_CHORD, .40f, .72f, .10f, .05f, .50f, 1.0f, .30f, .50f, .05f, .00f}, /*gain*/ 2.483f},
	{"TECHNO · ELECTRO · DETROIT · BERLIN", "DUB GLASS CHORDS", {0, .30f, B_CHOIR, DRN, 3, D_FWD, .50f, .00f, SC_GENRA_CHORD, .35f, .72f, -.20f, .30f, .50f, 1.0f, .50f, .40f, .50f, .00f}, /*gain*/ 1.517f},
	{"POP · SYNTHWAVE · TRANCE · MELODIC TECHNO", "BELL PLATE", {0, .30f, B_BELLS, DRN, 2, D_FWD, .50f, .00f, SC_GENRA_KEY, .45f, .72f, .30f, .05f, .50f, 1.0f, .50f, .70f, .15f, .30f}, /*gain*/ 1.779f},
	{"POP · SYNTHWAVE · TRANCE · MELODIC TECHNO", "PAINTED PAD", {0, .30f, B_AURORA, GAT, 3, D_FWD, .50f, .00f, SC_GENRA_CHORD, .40f, .60f, .10f, .40f, .60f, .90f, .60f, .60f, .40f, .10f}, /*gain*/ 2.008f},
	{"SOUL · JAZZ · LO-FI", "OLD GLASS KEYS",   {0, .30f, B_CHOIR,  GAT, 2, D_FWD,  .50f, .00f, SC_GENRA_CHORD, .40f, .65f, -.30f, .05f, .45f, .40f, .45f, .40f, .25f, .10f}, /*gain*/ 1.791f},
	{"DOWNTEMPO · TRIP-HOP", "SMEARED SPECTRUM", {0, .30f, B_GLISS, DRN, 4, D_BWD,  .40f, .00f, SC_GENRA_KEY,   .35f, .72f, -.20f, .50f, .50f, 1.0f, .60f, .60f, .80f, .20f}, /*gain*/ 2.587f},
	{"AMBIENT", "STAR BELLS",                   {0, .30f, B_BELLS,  DRN, 4, D_PING, .35f, .00f, SC_GENRA_KEY,   .55f, .72f, .40f, .40f, .50f, 1.0f, .60f, .90f, .50f, .60f}, /*gain*/ 2.705f},
};
// clang-format on
#undef DRN
#undef GAT
static const int NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

}  // namespace glas
