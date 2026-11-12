// SPDX-License-Identifier: GPL-3.0-only
// GENRA CHOP: the sample and what is found in it (no Rack in here: test/hh_test.cpp).
//
// Load once, analyse once (in a worker thread): the chops (by transients or in equal parts), per chop the tones in it
// (a 12-tone fingerprint from the spectrum), the chord that fits it best, how long it rings, how bright it is; over all
// chops the key. The audio thread only plays what is here.
#pragma once
#include "Core.hpp"
#include <memory>
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

// Transient: the strongest attacks; Equal: parts of the same length. How many: CHOPS (4..64).
enum ChopMode { CM_TRANSIENT, CM_EQUAL, NUM_CHOP_MODES };
static const int MIN_CHOPS = 4, DEFAULT_CHOPS = 16;
extern const char* const CHOP_MODES[NUM_CHOP_MODES];

struct ChopSet {
	SampleData data;
	int mode = CM_TRANSIENT;
	int want = DEFAULT_CHOPS; // how many chops were asked for (info.count: how many there are)
	size_t start[PADS] = {}, end[PADS] = {};       // as played (MOVE shifts these, in the audio thread)
	size_t cutStart[PADS] = {}, cutEnd[PADS] = {}; // as cut (never moved)
	float bpm = 0.f;                               // the sample's own tempo, as found (of the original, unwarped)
	// WARP: a warped set keeps the original it was made from (to warp again or go back); the ratio is the new length
	std::shared_ptr<const ChopSet> source;
	double warpRatio = 1.0;
	int warpHow = -1; // WARP_BEATS / WARP_TONE (-1: not warped)
	SampleInfo info;
	float chroma[PADS][12] = {};
	std::vector<float> peaks; // for the display: max |x| in 512 parts
	// for a break: which chop plays which drum voice (-1: none)
	int voicePad[DRUMS] = {-1, -1, -1, -1, -1, -1, -1, -1};
};

/** Cut (into `count` chops, 4..64) and analyse. */
void analyse(ChopSet& cs, int mode, int count = DEFAULT_CHOPS);

// ---------------------------------------------------------------- WARP
enum WarpMode { WARP_AUTO, WARP_BEATS, WARP_TONE, NUM_WARP_MODES };
extern const char* const WARP_MODES[NUM_WARP_MODES];
/** The tempo of a sample: from the onsets (autocorrelation), folded into 70..140; a loop of whole bars wins. */
float findTempo(const SampleData& s);
/** AUTO: BEATS for hits and breaks, TONE for loops with chords. */
int autoWarp(const ChopSet& cs);
/** BEATS: every chop at its new place (start x ratio), played as it is (shortened or followed by a short fade). */
void warpBeats(const ChopSet& src, double ratio, SampleData& out);
/** Copy what was found (cut, chords, key, tempo) without the audio; start/end from the cut. */
void copyAnalysis(const ChopSet& from, ChopSet& to);
/** A warped set from the original and the new audio (ratio = new length / old length). */
void makeWarped(const ChopSet& orig, SampleData&& audio, double ratio, int how, ChopSet& out);
/** The waveform for the display. */
void computePeaks(ChopSet& cs);
/** The chords and the key again (after the cut is the same but e.g. nothing else changed): part of analyse(). */
void findHarmony(ChopSet& cs);

} // namespace hh
