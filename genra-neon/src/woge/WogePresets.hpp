// SPDX-License-Identifier: GPL-3.0-only
// WOGE – the factory presets, by style (the styles match GENRA's and GENRA TECHNO's genres: Follow GENRA picks presets
// for the genre). The values are the knobs in WOGE's ParamId order. Plain C++ without Rack: test/neon_test.cpp plays
// every one and sets its level (gain).
#pragma once
#include "WogeDsp.hpp"

namespace woge {

enum PresetParam {
	P_MASS, P_TENS, P_DAMP, P_CNTR, P_EXCITE, P_POS, P_FORCE, P_PATH, P_WIDTH, P_DRIFT,
	P_CUTOFF, P_RES, P_DRIVE, P_ATK, P_DEC, P_SUS, P_REL, P_VMODE, P_GLIDE, P_ACCENT,
	NUM_PRESET_PARAMS
};

struct Preset {
	const char* style;
	const char* name;
	float v[NUM_PRESET_PARAMS];
	float gain;  // the output level that makes this preset even with the others (test/neon_test.cpp)
	void values(float* out) const {
		for (int i = 0; i < NUM_PRESET_PARAMS; i++)
			out[i] = v[i];
	}
};

/** The engine's settings from the knob values (no CV). */
inline Params paramsFrom(const float* v, float gain) {
	Params p;
	p.mass = v[P_MASS], p.tension = v[P_TENS], p.damp = v[P_DAMP], p.center = v[P_CNTR];
	p.excite = (int) v[P_EXCITE], p.pos = v[P_POS], p.force = v[P_FORCE];
	p.path = (int) v[P_PATH], p.width = v[P_WIDTH], p.drift = v[P_DRIFT];
	p.cutoff = v[P_CUTOFF], p.res = v[P_RES], p.drive = v[P_DRIVE];
	p.atk = v[P_ATK], p.dec = v[P_DEC], p.sus = v[P_SUS], p.rel = v[P_REL];
	p.mode = (int) v[P_VMODE], p.glide = v[P_GLIDE], p.accent = v[P_ACCENT];
	p.outGain = gain;
	return p;
}

#define HAM X_HAMMER
#define PLU X_PLUCK
#define NOI X_NOISE
#define BOW X_BOW
#define LIN P_LINEAR
#define LOO P_LOOP
#define BOU P_BOUNCE
#define SPI P_SPIRAL
#define POLY MODE_POLY
#define MONO MODE_MONO
#define LEG MODE_LEGATO
// clang-format off
//                                                  mass tens damp cntr excite pos  force path width drift  cut  res  drv   atk  dec  sus  rel   mode  glide acc
static const Preset PRESETS[] = {
	{"AMBIENT", "GLASS ORGAN",                       {.30f, .55f, .15f, .30f, BOW, .30f, .45f, LIN, 1.0f, .20f, .70f, .10f, .00f, .25f, .60f, .85f, .55f, POLY, .10f, .40f}, /*gain*/ 1.082f},
	{"AMBIENT", "LIVING STRING PAD",                 {.45f, .40f, .10f, .20f, BOW, .25f, .40f, BOU, .70f, .50f, .62f, .12f, .00f, .45f, .60f, .90f, .70f, POLY, .10f, .30f}, /*gain*/ 1.122f},
	{"DRONE · BERLIN SCHOOL", "TIDAL STRING",        {.70f, .30f, .10f, .15f, BOW, .40f, .55f, LOO, 1.0f, .70f, .55f, .20f, .10f, .35f, .60f, .90f, .75f, POLY, .20f, .30f}, /*gain*/ 1.502f},
	{"DRONE · BERLIN SCHOOL", "SLOW SURGE",          {.85f, .20f, .05f, .10f, BOW, .55f, .50f, BOU, .55f, .85f, .48f, .25f, .15f, .60f, .70f, 1.0f, .85f, POLY, .30f, .20f}, /*gain*/ 1.689f},
	{"TECHNO · ELECTRO · DETROIT · BERLIN", "SCAN STAB", {.30f, .70f, .55f, .30f, HAM, .30f, .70f, SPI, 1.0f, .10f, .60f, .30f, .30f, .00f, .35f, .20f, .25f, POLY, .10f, .60f}, /*gain*/ 0.890f},
	{"TECHNO · ELECTRO · DETROIT · BERLIN", "DUB SCAN CHORD", {.40f, .55f, .45f, .25f, PLU, .35f, .60f, BOU, .50f, .30f, .45f, .25f, .10f, .00f, .45f, .30f, .40f, POLY, .10f, .50f}, /*gain*/ 0.688f},
	{"TECHNO · ELECTRO · DETROIT · BERLIN", "SCAN BASS", {.50f, .45f, .40f, .35f, HAM, .25f, .70f, LIN, 1.0f, .00f, .35f, .30f, .40f, .00f, .40f, .50f, .20f, MONO, .15f, .60f}, /*gain*/ 2.648f},
	{"MINIMAL · ACID", "RUBBER ACID",                {.20f, .80f, .35f, .40f, PLU, .20f, .70f, LIN, 1.0f, .10f, .40f, .55f, .40f, .00f, .30f, .40f, .20f, MONO, .25f, .70f}, /*gain*/ 2.110f},
	{"POP · SYNTHWAVE · TRANCE · MELODIC TECHNO", "NEON KEYS", {.35f, .60f, .30f, .25f, HAM, .30f, .55f, LIN, 1.0f, .15f, .70f, .15f, .05f, .00f, .50f, .50f, .45f, POLY, .10f, .40f}, /*gain*/ 0.962f},
	{"POP · SYNTHWAVE · TRANCE · MELODIC TECHNO", "WAVE RIDER LEAD", {.40f, .65f, .20f, .30f, BOW, .35f, .55f, SPI, 1.0f, .30f, .75f, .20f, .20f, .05f, .50f, .80f, .35f, MONO, .20f, .50f}, /*gain*/ 2.558f},
	{"HOUSE · GARAGE", "WARM SCAN PIANO",            {.40f, .60f, .50f, .30f, HAM, .15f, .60f, LIN, 1.0f, .05f, .60f, .10f, .05f, .00f, .55f, .20f, .40f, POLY, .10f, .40f}, /*gain*/ 0.900f},
	{"SOUL · JAZZ · LO-FI", "DUSTY MALLET",          {.35f, .55f, .65f, .35f, HAM, .45f, .60f, BOU, .80f, .10f, .50f, .10f, .10f, .00f, .35f, .00f, .35f, POLY, .10f, .40f}, /*gain*/ 1.386f},
	{"DOWNTEMPO · TRIP-HOP", "TAPE STRING",          {.45f, .45f, .40f, .25f, PLU, .30f, .55f, LIN, 1.0f, .40f, .50f, .15f, .15f, .02f, .60f, .40f, .60f, POLY, .10f, .40f}, /*gain*/ 0.677f},
	{"HIP-HOP · TRAP · PHONK", "BOOM BELL",          {.25f, .85f, .50f, .40f, HAM, .40f, .65f, SPI, 1.0f, .00f, .65f, .10f, .10f, .00f, .45f, .00f, .40f, POLY, .10f, .50f}, /*gain*/ 0.834f},
	{"GRIME · DUBSTEP · DRILL", "WOBBLE STRING",     {.90f, .25f, .30f, .20f, BOW, .50f, .60f, LOO, 1.0f, .60f, .45f, .35f, .50f, .00f, .50f, .80f, .20f, MONO, .15f, .60f}, /*gain*/ 1.912f},
	{"HARD TECHNO · HARDCORE · EBM · INDUSTRIAL", "IRON RING", {.30f, .90f, .20f, .60f, NOI, .50f, .70f, LOO, 1.0f, .20f, .70f, .20f, .60f, .00f, .30f, .30f, .25f, POLY, .10f, .60f}, /*gain*/ 0.896f},
	{"PSYTRANCE", "SPIRAL ZAP",                      {.25f, .95f, .60f, .45f, PLU, .30f, .75f, SPI, 1.0f, .20f, .80f, .40f, .20f, .00f, .20f, .10f, .15f, MONO, .05f, .60f}, /*gain*/ 1.918f},
	{"WITCH HOUSE · HORROR · DUNGEON SYNTH", "GHOST WIRE", {.80f, .20f, .05f, .10f, BOW, .45f, .45f, LOO, .60f, .80f, .40f, .40f, .10f, .50f, .70f, .90f, .80f, POLY, .30f, .30f}, /*gain*/ 2.162f},
};
// clang-format on
#undef HAM
#undef PLU
#undef NOI
#undef BOW
#undef LIN
#undef LOO
#undef BOU
#undef SPI
#undef POLY
#undef MONO
#undef LEG
static const int NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

}  // namespace woge
