// SPDX-License-Identifier: GPL-3.0-only
// GENRA CHOP: the sample and what is found in it (no Rack in here: test/hh_test.cpp).
//
// Load once, analyse once (in a worker thread): the chops (by transients or in equal parts), per chop the tones in it
// (a 12-tone fingerprint from the spectrum), the chord that fits it best, how long it rings, how bright it is; over all
// chops the key. The audio thread only plays what is here.
#pragma once
#include "Core.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace hh {

struct SampleData {
	std::vector<float> L, R; // R empty: mono
	float sampleRate = 44100.f;
	std::string path, name;
	size_t frames() const { return L.size(); }
};

/** Reads a WAV file's bytes (PCM 8/16/24/32 bit, float 32/64, mono or more channels; the first two are used).
 *  maxSeconds: longer files are cut. False with err set if it is not a WAV this can read. */
bool readWav(const std::vector<uint8_t>& buf, SampleData& out, std::string& err, float maxSeconds = 60.f);
/** A stereo 24-bit WAV of the frames [a, b), reversed and/or pitched (resampled, plain linear) if asked. */
std::vector<uint8_t> writeWav(const SampleData& s, size_t a, size_t b, bool reverse, int semitones);

enum ChopMode { CM_TRANSIENT, CM_EQUAL16, CM_EQUAL8, NUM_CHOP_MODES };
extern const char* const CHOP_MODES[NUM_CHOP_MODES];

struct ChopSet {
	SampleData data;
	int mode = CM_TRANSIENT;
	size_t start[PADS] = {}, end[PADS] = {};
	SampleInfo info;
	float chroma[PADS][12] = {};
	std::vector<float> peaks; // for the display: max |x| in 512 parts
	// for a break: which chop plays which drum voice (-1: none)
	int voicePad[DRUMS] = {-1, -1, -1, -1, -1, -1, -1, -1};
};

/** Cut and analyse. */
void analyse(ChopSet& cs, int mode);
/** The chords and the key again (after the cut is the same but e.g. nothing else changed): part of analyse(). */
void findHarmony(ChopSet& cs);

} // namespace hh
