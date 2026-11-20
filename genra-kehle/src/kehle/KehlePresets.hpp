// SPDX-License-Identifier: GPL-3.0-only
// KEHLE – the factory presets, by sound (ROBOT, CHOIR, WHISPER, FX). The values are the knobs in KEHLE's ParamId
// order (LEVEL is not in them). Plain C++ without Rack: test/kehle_test.cpp plays every one with a test voice and sets its level (gain).
#pragma once
#include "KehleDsp.hpp"

namespace kehle {

enum PresetParam {
	P_CHAR, P_BANDS, P_FORMANT, P_RESO, P_SIBIL, P_CAR, P_OCT, P_ENS, P_ATK, P_REL, P_MIX,
	NUM_PRESET_PARAMS
};

struct Preset {
	const char* style;
	const char* name;
	float v[NUM_PRESET_PARAMS];
	float gain;  // the output level that makes this preset even with the others (test/kehle_test.cpp)
	void values(float* out) const {
		for (int i = 0; i < NUM_PRESET_PARAMS; i++)
			out[i] = v[i];
	}
};

/** The engine's settings from the knob values (no CV). */
inline Params paramsFrom(const float* v, float gain) {
	Params p;
	p.character = (int) v[P_CHAR], p.bands = v[P_BANDS], p.formant = v[P_FORMANT], p.reso = v[P_RESO];
	p.sibil = v[P_SIBIL], p.carrier = (int) v[P_CAR], p.oct = v[P_OCT], p.ens = v[P_ENS];
	p.atk = v[P_ATK], p.rel = v[P_REL], p.dry = v[P_MIX];  // LEVEL is not part of a preset (as in GENRA Neon)
	p.outGain = gain;
	return p;
}

//                         CHAR         BANDS FORM  RESO  SIBIL CAR           OCT   ENS   ATK   REL   DRY
static const Preset PRESETS[] = {
    {"ROBOT", "Classic Robot",  {CH_CLASSIC, 0.6f, 0.5f, 0.4f, 0.5f, CAR_SAW, 0.5f, 0.2f, 0.15f, 0.3f, 0.f}, /*gain*/ 1.457f},
    {"ROBOT", "Daft Talk",      {CH_CLASSIC, 1.0f, 0.52f, 0.55f, 0.6f, CAR_PULSE, 0.5f, 0.3f, 0.1f, 0.25f, 0.f}, /*gain*/ 1.498f},
    {"ROBOT", "Strings Speak",  {CH_CLASSIC, 0.8f, 0.5f, 0.35f, 0.4f, CAR_STRINGS, 0.5f, 0.5f, 0.2f, 0.4f, 0.f}, /*gain*/ 1.208f},
    {"ROBOT", "Vintage Strings", {CH_CLASSIC, 0.4f, 0.5f, 0.4f, 0.4f, CAR_STRINGS, 0.25f, 0.75f, 0.2f, 0.45f, 0.f}, /*gain*/ 2.592f},
    {"ROBOT", "Velvet Pad",     {CH_CLASSIC, 0.8f, 0.5f, 0.55f, 0.3f, CAR_STRINGS, 0.5f, 0.55f, 0.3f, 0.65f, 0.f}, /*gain*/ 0.898f},
    {"ROBOT", "Tin Man",        {CH_METAL, 0.6f, 0.5f, 0.5f, 0.4f, CAR_PULSE, 0.5f, 0.15f, 0.15f, 0.3f, 0.f}, /*gain*/ 0.590f},
    {"CHOIR", "Neon Choir",     {CH_CHOIR, 0.8f, 0.5f, 0.45f, 0.4f, CAR_STRINGS, 0.5f, 0.7f, 0.25f, 0.45f, 0.f}, /*gain*/ 1.183f},
    {"CHOIR", "Cathedral Bots", {CH_CHOIR, 0.8f, 0.45f, 0.5f, 0.3f, CAR_SAW, 0.25f, 0.8f, 0.3f, 0.55f, 0.f}, /*gain*/ 2.683f},
    {"CHOIR", "Glass Angels",   {CH_GLASS, 0.8f, 0.55f, 0.7f, 0.3f, CAR_SAW, 0.75f, 0.4f, 0.2f, 0.5f, 0.f}, /*gain*/ 0.798f},
    {"CHOIR", "Crystal Bell",   {CH_GLASS, 0.6f, 0.6f, 0.9f, 0.2f, CAR_PULSE, 1.0f, 0.3f, 0.1f, 0.6f, 0.f}, /*gain*/ 0.713f},
    {"WHISPER", "Ghost Whisper", {CH_WHISPER, 1.0f, 0.5f, 0.4f, 0.7f, CAR_NOISE, 0.5f, 0.2f, 0.15f, 0.35f, 0.f}, /*gain*/ 7.058f},
    {"WHISPER", "Night Breath", {CH_WHISPER, 0.8f, 0.45f, 0.5f, 0.5f, CAR_SAW, 0.5f, 0.4f, 0.2f, 0.45f, 0.f}, /*gain*/ 4.723f},
    {"FX", "Monster",           {CH_SHIFT, 0.6f, 0.15f, 0.45f, 0.3f, CAR_SAW, 0.25f, 0.2f, 0.15f, 0.3f, 0.f}, /*gain*/ 1.168f},
    {"FX", "Mouse",             {CH_SHIFT, 0.6f, 0.88f, 0.4f, 0.5f, CAR_SAW, 0.75f, 0.2f, 0.1f, 0.25f, 0.f}, /*gain*/ 1.669f},
    {"FX", "Talkbox Funk",      {CH_TALKBOX, 0.4f, 0.5f, 0.3f, 0.3f, CAR_SAW, 0.5f, 0.1f, 0.15f, 0.3f, 0.f}, /*gain*/ 1.414f},
    {"FX", "Talkbox Square",    {CH_TALKBOX, 0.4f, 0.55f, 0.35f, 0.3f, CAR_PULSE, 0.5f, 0.1f, 0.15f, 0.3f, 0.f}, /*gain*/ 1.388f},
};
static const int NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

}  // namespace kehle
