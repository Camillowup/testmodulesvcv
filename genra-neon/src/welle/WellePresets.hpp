// SPDX-License-Identifier: GPL-3.0-only
// WELLE – the factory presets, by style (the styles match GENRA's and GENRA TECHNO's genres: Follow GENRA picks
// presets for the genre). Plain C++ without Rack: test/neon_test.cpp plays every one and sets its level (gain).
// The tables are the built-in ones (neon::Builtin); the module turns them into FOLDER and TABLE.
#pragma once
#include "WelleDsp.hpp"

namespace welle {

/** The knobs in WELLE's ParamId order (the first NUM_PRESET_PARAMS params). */
static const int NUM_PRESET_PARAMS = 37;

struct Preset {
	const char* style;
	const char* name;
	int tA;
	float posA;
	int wmA;
	float warpA, octA;
	int tB;
	float posB;
	int wmB;
	float warpB, tuneB;
	int ftype;
	float cutoff, res, fenv, drive, key, fatk, fdec;
	float scan;
	int rate;
	float penv;
	float chorus, delay, verb;
	float atk, dec, sus, rel;
	float mix, sub;
	int unison;
	float spread;
	int mode;
	float glide, accent;
	float gain;  // the output level that makes this preset even with the others (test/neon_test.cpp)

	/** FOLDER and TABLE of a built-in table. */
	static void where(int b, float& folder, float& table) {
		std::vector<int> v = builtinsOf(BUILTINS[b].folder);
		folder = (float) BUILTINS[b].folder;
		int i = 0;
		for (size_t k = 0; k < v.size(); k++)
			if (v[k] == b)
				i = (int) k;
		table = v.size() > 1 ? (float) i / (float) (v.size() - 1) : 0.f;
	}
	void values(float* v) const {
		float fA, tbA, fB, tbB;
		where(tA, fA, tbA);
		where(tB, fB, tbB);
		const float a[NUM_PRESET_PARAMS] = {fA, tbA, posA, (float) wmA, warpA, octA,
		                                    fB, tbB, posB, (float) wmB, warpB, tuneB,
		                                    (float) ftype, cutoff, res, fenv, drive, key, fatk, fdec,
		                                    scan, (float) rate, penv,
		                                    chorus, delay, verb,
		                                    atk, dec, sus, rel,
		                                    mix, sub, (float) unison, spread,
		                                    (float) mode, glide, accent};
		for (int i = 0; i < NUM_PRESET_PARAMS; i++)
			v[i] = a[i];
	}
	Params params() const {
		Params p;
		p.posA = posA, p.warpModeA = wmA, p.warpA = warpA, p.octA = octA;
		p.posB = posB, p.warpModeB = wmB, p.warpB = warpB, p.tuneB = tuneB;
		p.ftype = ftype, p.cutoff = cutoff, p.res = res, p.fenv = fenv, p.drive = drive, p.key = key;
		p.fatk = fatk, p.fdec = fdec, p.scan = scan, p.rate = rate, p.penv = penv;
		p.chorus = chorus, p.delay = delay, p.verb = verb;
		p.atk = atk, p.dec = dec, p.sus = sus, p.rel = rel;
		p.mix = mix, p.sub = sub, p.unison = unison, p.spread = spread;
		p.mode = mode, p.glide = glide, p.accent = accent;
		p.outGain = gain;
		return p;
	}
};

#define L24 gmono::LADDER24
#define L12 gmono::LADDER12
#define DIO gmono::DIODE
#define M20 gmono::MS20
#define SLP gmono::SVF_LP
#define SBP gmono::SVF_BP
#define SHP gmono::SVF_HP
#define OTA gmono::OTA
// clang-format off
//  style, name,                 tableA  posA wA    warpA octA  tableB    posB wB  warpB tuneB  filter cut  res  fenv drive key  fatk fdec  scan rate penv  chor dly  verb  atk  dec  sus  rel   mix  sub  uni spread mode   glide acc   gain
static const Preset PRESETS[] = {
	{"TECHNO · ELECTRO · DETROIT · BERLIN", "TECHNO STAB", B_STAB, .30f, W_BEND, .00f, 0.f, B_DUBCHORD, .20f, W_BEND, .00f, 0.f, L24, .55f, .35f, .45f, .20f, .40f, .00f, .32f, .10f, 6, .20f, .10f, .35f, .30f, .02f, .38f, .00f, .35f, .30f, .00f, 1, .20f, MODE_POLY, .10f, .60f, /*gain*/ 0.712f},
	{"TECHNO · ELECTRO · DETROIT · BERLIN", "DUB TECHNO CHORD", B_DUBCHORD, .50f, W_BEND, .00f, 0.f, B_STAB, .40f, W_BEND, .00f, 0.f, L12, .45f, .25f, .30f, .10f, .30f, .05f, .50f, .25f, 2, .10f, .30f, .55f, .55f, .05f, .55f, .20f, .55f, .40f, .00f, 3, .35f, MODE_POLY, .10f, .50f, /*gain*/ 0.368f},
	{"TECHNO · ELECTRO · DETROIT · BERLIN", "DETROIT LEAD", B_SAWSTACK, .60f, W_SYNC, .25f, 0.f, B_PWM, .50f, W_BEND, .00f, 7.f, M20, .60f, .40f, .35f, .30f, .50f, .00f, .40f, .20f, 8, .00f, .00f, .40f, .25f, .02f, .50f, .60f, .30f, .35f, .00f, 3, .30f, MODE_MONO, .25f, .60f, /*gain*/ 0.801f},
	{"TECHNO · ELECTRO · DETROIT · BERLIN", "WAREHOUSE REESE", B_REESE, .20f, W_BEND, .10f, -1.f, B_REESE, .70f, W_BEND, .00f, .10f, L24, .40f, .20f, .25f, .40f, .20f, .00f, .45f, .60f, 4, .00f, .20f, .00f, .05f, .00f, .50f, .80f, .15f, .50f, .50f, 3, .30f, MODE_MONO, .20f, .50f, /*gain*/ 0.875f},
	{"TECHNO · ELECTRO · DETROIT · BERLIN", "METAL PERC", B_METALLIC, .50f, W_FOLD, .20f, 0.f, B_GLASS, .30f, W_BEND, .00f, 0.f, SBP, .70f, .50f, .50f, .10f, .50f, .00f, .20f, .00f, 6, .30f, .00f, .30f, .30f, .04f, .30f, .00f, .20f, .20f, .00f, 1, .20f, MODE_POLY, .00f, .60f, /*gain*/ 1.544f},
	{"HARD TECHNO · HARDCORE · EBM · INDUSTRIAL", "INDUSTRIAL GRIND", B_INDUSTRIAL, .50f, W_CRUSH, .30f, -1.f, B_FOLD, .60f, W_BEND, .00f, 0.f, DIO, .50f, .50f, .40f, .70f, .30f, .00f, .40f, .40f, 10, .00f, .00f, .00f, .15f, .00f, .40f, .50f, .20f, .40f, .30f, 2, .20f, MODE_MONO, .10f, .60f, /*gain*/ 1.879f},
	{"HARD TECHNO · HARDCORE · EBM · INDUSTRIAL", "HOOVER", B_HOOVER, .40f, W_BEND, .20f, 0.f, B_HOOVER, .70f, W_BEND, .00f, -12.f, L24, .80f, .10f, .10f, .20f, .40f, .00f, .40f, .30f, 4, .00f, .50f, .00f, .25f, .02f, .50f, .80f, .35f, .40f, .00f, 5, .60f, MODE_MONO, .50f, .50f, /*gain*/ 1.079f},
	{"MINIMAL · ACID", "ACID WAVE", B_ACID, .30f, W_BEND, .00f, -1.f, B_ACID, .60f, W_BEND, .00f, 0.f, DIO, .30f, .75f, .60f, .30f, .30f, .00f, .30f, .00f, 6, .40f, .00f, .30f, .10f, .00f, .40f, .50f, .10f, .00f, .00f, 1, .20f, MODE_MONO, .30f, .80f, /*gain*/ 1.473f},
	{"MINIMAL · ACID", "MINIMAL BLIP", B_FM, .20f, W_BEND, .00f, 0.f, B_GLASS, .30f, W_BEND, .00f, 0.f, SLP, .60f, .30f, .40f, .00f, .50f, .00f, .25f, .00f, 6, .00f, .00f, .45f, .20f, .03f, .30f, .00f, .20f, .30f, .00f, 1, .20f, MODE_POLY, .00f, .50f, /*gain*/ 1.461f},
	{"HOUSE · GARAGE", "RAVE ORGAN", B_RAVEORGAN, .50f, W_BEND, .00f, 0.f, B_ORGAN, .40f, W_BEND, .00f, 0.f, L12, .75f, .10f, .20f, .10f, .50f, .00f, .35f, .00f, 6, .00f, .20f, .00f, .20f, .00f, .40f, .50f, .30f, .00f, .00f, 1, .20f, MODE_POLY, .00f, .50f, /*gain*/ 0.637f},
	{"HOUSE · GARAGE", "GARAGE KEYS", B_PLUCK, .30f, W_BEND, .00f, 0.f, B_FMBELL, .30f, W_BEND, .00f, 0.f, SLP, .70f, .15f, .25f, .00f, .50f, .00f, .40f, .00f, 6, .00f, .30f, .25f, .25f, .00f, .50f, .20f, .35f, .35f, .00f, 1, .20f, MODE_POLY, .00f, .50f, /*gain*/ 0.820f},
	{"POP · SYNTHWAVE · TRANCE · MELODIC TECHNO", "SUPERSAW", B_SAWSTACK, .85f, W_BEND, .00f, 0.f, B_SAWSTACK, .70f, W_BEND, .00f, 0.f, L24, .70f, .10f, .20f, .00f, .50f, .00f, .40f, .00f, 6, .00f, .20f, .30f, .35f, .01f, .50f, .80f, .45f, .50f, .00f, 7, .55f, MODE_POLY, .00f, .50f, /*gain*/ 0.525f},
	{"POP · SYNTHWAVE · TRANCE · MELODIC TECHNO", "TRANCE PLUCK", B_SAWSTACK, .60f, W_SYNC, .15f, 0.f, B_PWM, .40f, W_BEND, .00f, 12.f, L24, .50f, .30f, .55f, .00f, .50f, .00f, .25f, .00f, 6, .10f, .00f, .45f, .35f, .02f, .36f, .00f, .30f, .25f, .00f, 5, .40f, MODE_POLY, .00f, .60f, /*gain*/ 0.529f},
	{"POP · SYNTHWAVE · TRANCE · MELODIC TECHNO", "SYNTHWAVE BRASS", B_PWM, .40f, W_BEND, .00f, 0.f, B_SAWSTACK, .50f, W_BEND, .00f, .08f, L24, .45f, .15f, .35f, .00f, .50f, .30f, .50f, .00f, 6, .00f, .40f, .00f, .30f, .10f, .50f, .70f, .40f, .50f, .00f, 2, .30f, MODE_POLY, .00f, .50f, /*gain*/ 0.528f},
	{"PSYTRANCE", "PSY BASS", B_BASIC, .55f, W_BEND, .00f, -1.f, B_BASIC, .70f, W_BEND, .00f, 0.f, L24, .35f, .25f, .50f, .10f, .40f, .00f, .18f, .00f, 6, .00f, .00f, .00f, .00f, .00f, .15f, .00f, .08f, .00f, .00f, 1, .20f, MODE_MONO, .00f, .60f, /*gain*/ 2.588f},
	{"PSYTRANCE", "PSY ACID LEAD", B_ACID, .70f, W_MIRROR, .30f, 0.f, B_SYNC, .40f, W_BEND, .00f, 0.f, M20, .55f, .55f, .40f, .30f, .50f, .00f, .30f, .50f, 9, .30f, .00f, .40f, .20f, .00f, .40f, .40f, .20f, .30f, .00f, 1, .20f, MODE_MONO, .20f, .70f, /*gain*/ 1.162f},
	{"AMBIENT", "GLASS PAD", B_GLASS, .40f, W_BEND, .00f, 0.f, B_AIR, .30f, W_BEND, .00f, 0.f, SLP, .65f, .10f, .10f, .00f, .40f, .00f, .50f, .50f, 1, .00f, .50f, .30f, .70f, .55f, .60f, .80f, .65f, .50f, .00f, 3, .50f, MODE_POLY, .00f, .40f, /*gain*/ 0.780f},
	{"AMBIENT", "SPECTRAL DRIFT", B_SPECTRAL, .30f, W_BEND, .00f, 0.f, B_ODDEVEN, .50f, W_BEND, .00f, 0.f, L12, .60f, .10f, .10f, .00f, .40f, .00f, .50f, .90f, 0, .00f, .30f, .20f, .60f, .60f, .60f, .90f, .70f, .50f, .00f, 2, .40f, MODE_POLY, .00f, .40f, /*gain*/ 0.818f},
	{"DRONE · BERLIN SCHOOL", "BERLIN SEQ", B_PWM, .30f, W_BEND, .00f, 0.f, B_SAWSTACK, .40f, W_BEND, .00f, 0.f, L24, .40f, .45f, .45f, .10f, .50f, .00f, .30f, .30f, 3, .00f, .00f, .50f, .40f, .00f, .35f, .10f, .30f, .30f, .00f, 1, .20f, MODE_POLY, .00f, .60f, /*gain*/ 0.675f},
	{"DRONE · BERLIN SCHOOL", "DARK DRONE", B_SUB, .60f, W_BEND, .00f, 0.f, B_NOISESCAN, .40f, W_BEND, .00f, 0.f, L12, .35f, .20f, .10f, .20f, .30f, .00f, .50f, .70f, 0, .00f, .20f, .00f, .60f, .70f, .60f, 1.0f, .80f, .30f, .40f, 3, .40f, MODE_POLY, .00f, .40f, /*gain*/ 0.839f},
	{"SOUL · JAZZ · LO-FI", "LO-FI VOCAL KEYS", B_CHOIR, .30f, W_BEND, .00f, 0.f, B_PLUCK, .20f, W_BEND, .00f, 0.f, L12, .55f, .10f, .20f, .10f, .40f, .00f, .45f, .00f, 6, .00f, .40f, .10f, .30f, .02f, .60f, .40f, .40f, .40f, .00f, 1, .20f, MODE_POLY, .00f, .50f, /*gain*/ 0.506f},
	{"GRIME · DUBSTEP · DRILL", "GROWL BASS", B_GROWL, .20f, W_FOLD, .30f, -1.f, B_REESE, .50f, W_BEND, .00f, 0.f, L24, .55f, .35f, .20f, .50f, .30f, .00f, .40f, .80f, 8, .00f, .00f, .00f, .00f, .00f, .50f, 1.0f, .10f, .30f, .50f, 2, .20f, MODE_MONO, .10f, .60f, /*gain*/ 0.698f},
	{"GRIME · DUBSTEP · DRILL", "WOBBLE TALK", B_TALK, .50f, W_BEND, .00f, -1.f, B_VOCAL, .50f, W_BEND, .00f, 0.f, M20, .50f, .40f, .20f, .30f, .30f, .00f, .40f, 1.0f, 8, .00f, .00f, .00f, .05f, .00f, .50f, 1.0f, .10f, .30f, .50f, 1, .20f, MODE_MONO, .10f, .60f, /*gain*/ 0.530f},
	{"HIP-HOP · TRAP · PHONK", "TRAP BELL", B_FMBELL, .40f, W_BEND, .00f, 0.f, B_GLASS, .50f, W_BEND, .00f, 0.f, SLP, .80f, .10f, .20f, .00f, .50f, .00f, .40f, .00f, 6, .00f, .00f, .30f, .30f, .00f, .55f, .00f, .50f, .40f, .00f, 1, .20f, MODE_POLY, .00f, .50f, /*gain*/ 0.969f},
	{"HIP-HOP · TRAP · PHONK", "PHONK COWBELL", B_METALLIC, .20f, W_BEND, .00f, 0.f, B_PWM, .20f, W_BEND, .00f, 7.f, SBP, .70f, .30f, .30f, .20f, .60f, .00f, .25f, .00f, 6, .00f, .00f, .20f, .10f, .03f, .36f, .00f, .25f, .40f, .00f, 1, .20f, MODE_POLY, .00f, .60f, /*gain*/ 0.844f},
	{"HIP-HOP · TRAP · PHONK", "SUB BASS", B_SUB, .30f, W_BEND, .00f, -1.f, B_SUB, .10f, W_BEND, .00f, 0.f, L12, .30f, .00f, .10f, .10f, .20f, .00f, .40f, .00f, 6, .00f, .00f, .00f, .00f, .00f, .50f, 1.0f, .15f, .00f, .30f, 1, .20f, MODE_MONO, .20f, .40f, /*gain*/ 1.073f},
	{"DUB · REGGAE", "DUB SKANK", B_ORGAN, .30f, W_BEND, .00f, 0.f, B_PLUCK, .30f, W_BEND, .00f, 0.f, L12, .60f, .20f, .30f, .10f, .50f, .00f, .20f, .00f, 6, .00f, .00f, .50f, .40f, .03f, .30f, .00f, .15f, .30f, .00f, 1, .20f, MODE_POLY, .00f, .50f, /*gain*/ 0.963f},
	{"DOWNTEMPO · TRIP-HOP", "TRIP-HOP STRINGS", B_STRINGS, .50f, W_BEND, .00f, 0.f, B_CHOIR, .40f, W_BEND, .00f, 0.f, L12, .55f, .10f, .15f, .00f, .40f, .00f, .50f, .20f, 2, .00f, .50f, .20f, .50f, .40f, .60f, .80f, .60f, .40f, .00f, 3, .40f, MODE_POLY, .00f, .40f, /*gain*/ 0.558f},
	{"WITCH HOUSE · HORROR · DUNGEON SYNTH", "HAUNTED CHOIR", B_CHOIR, .70f, W_BEND, .00f, 0.f, B_AIR, .60f, W_BEND, .00f, 0.f, SLP, .50f, .15f, .10f, .00f, .40f, .00f, .50f, .40f, 2, .00f, .40f, .20f, .80f, .50f, .60f, .90f, .80f, .40f, .00f, 3, .60f, MODE_POLY, .00f, .40f, /*gain*/ 0.914f},
	{"BREAKBEAT · BREAKS · BIG BEAT · BREAKCORE", "CHIP ARP", B_CHIP, .30f, W_BEND, .00f, 0.f, B_BITWAVE, .50f, W_BEND, .00f, 0.f, L12, .80f, .10f, .20f, .00f, .50f, .00f, .30f, .40f, 8, .00f, .00f, .35f, .15f, .00f, .30f, .30f, .15f, .30f, .00f, 1, .20f, MODE_POLY, .00f, .50f, /*gain*/ 0.864f},
	{"AFROBEAT · AFROBEATS · AMAPIANO · GQOM", "LOG WAVE", B_PLUCK, .70f, W_BEND, .00f, -1.f, B_SUB, .30f, W_BEND, .00f, 0.f, L24, .35f, .30f, .45f, .20f, .40f, .00f, .25f, .00f, 6, .20f, .00f, .20f, .10f, .00f, .35f, .30f, .20f, .30f, .40f, 1, .20f, MODE_MONO, .30f, .60f, /*gain*/ 1.281f},
	{"DARKWAVE · POP · SYNTHWAVE", "PHASE KEYS", B_PHASEDIST, .40f, W_BEND, .00f, 0.f, B_DIGITAL, .30f, W_BEND, .00f, 0.f, L24, .50f, .20f, .30f, .00f, .50f, .00f, .40f, .00f, 6, .00f, .50f, .25f, .35f, .02f, .50f, .50f, .40f, .30f, .00f, 2, .30f, MODE_POLY, .00f, .50f, /*gain*/ 0.545f},
};
// clang-format on
#undef L24
#undef L12
#undef DIO
#undef M20
#undef SLP
#undef SBP
#undef SHP
#undef OTA
static const int NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

}  // namespace welle
