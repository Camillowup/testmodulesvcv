// SPDX-License-Identifier: GPL-3.0-only
// GENRA CHOP: the pattern engine (no Rack in here, so it can be tested on its own: test/hh_test.cpp).
//
// Four bars of 16 steps for five tracks: drums (8 voices, GENRA's order), bass, melody, pads (4 voices) and CHOP (the
// pads of a sample). Every track keeps a BASE pattern (what NEW made, plus what was recorded) and is RENDERED from it:
// DENS filters by each note's priority, CHOP (MIDI chop on drums, melody and pads) cuts the bars into beats and
// repeats, stutters, drops or swaps them. Both work live on the knobs, NEW only rolls the base again.
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>

namespace hh {

static const int STEPS = 16, BARS = 4, LEN = STEPS * BARS, DRUMS = 8, PAD_VOICES = 4, PADS = 16;
enum DrumVoice { KICK, SNARE, CLAP, RIM, CH, OH, TOM, PERC };
enum Track { T_DRUMS, T_BASS, T_MEL, T_PAD, T_CHOP, NUM_TRACKS, T_HARMONY = 10 };

// ---------------------------------------------------------------- random (xorshift, the same on every system)
struct Rng {
	uint32_t s = 0x9e3779b9u;
	void seed(uint32_t v) { s = v ? v : 0x9e3779b9u; }
	uint32_t next() {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return s;
	}
	float uni() { return (next() >> 8) * (1.f / 16777216.f); }
	int below(int n) { return n <= 1 ? 0 : (int) (next() % (uint32_t) n); }
	bool chance(float p) { return uni() < p; }
	float range(float a, float b) { return a + (b - a) * uni(); }
};
/** A value 0..1 that only depends on its arguments (for the live knobs: the same cut for the same place). */
inline float hash01(uint32_t a, uint32_t b, uint32_t c) {
	uint32_t h = a * 0x8da6b343u ^ b * 0xd8163841u ^ c * 0xcb1ab31fu;
	h ^= h >> 15;
	h *= 0x2c1b3c6du;
	h ^= h >> 12;
	h *= 0x297a2d39u;
	h ^= h >> 15;
	return (h >> 8) * (1.f / 16777216.f);
}

// ---------------------------------------------------------------- harmony
enum Quality { Q_MAJ, Q_MIN, Q_DOM7, Q_MAJ7, Q_MIN7, Q_MIN9, Q_DIM, Q_SUS4, NUM_QUALITIES, Q_NONE = -1 };
extern const char* const QUALITY_NAMES[NUM_QUALITIES];
extern const int QUALITY_TONES[NUM_QUALITIES][5]; // semitones over the root, -1 = none
extern const char* const NOTE_NAMES[12];
struct Chord {
	int root = 0;           // pitch class, 0 = C
	int quality = Q_MIN;    // Q_NONE: no chord (a hit without pitch)
	bool valid() const { return quality >= 0; }
};
/** "F#m7", "Dmaj", "–" */
void chordName(const Chord& c, char* buf, int len);
enum Scale { SC_MINOR, SC_DORIAN, SC_MAJOR, SC_PHRYGIAN, SC_HARMMINOR, NUM_SCALES };
struct Key {
	int root = 5;   // F
	bool minor = true;
};
void keyName(const Key& k, char* buf, int len); // "F#m", "Eb"
/** Scale degree d (any integer, wraps into octaves) of a scale over root pc: semitones from the key root. */
int scaleStep(int scale, int d);

// ---------------------------------------------------------------- the styles (28 + 1, by decade)
enum Era { E_80, E_90, E_00, E_10, E_20, NUM_ERAS };
extern const char* const ERA_NAMES[NUM_ERAS];
enum BassMode { BM_ROOTS, BM_808, BM_WALK, BM_FUNK };
enum MelMode { MM_STAB, MM_LEAD, MM_BELL, MM_JAZZ, MM_RIFF };
enum PadMode { PM_SUSTAIN, PM_STAB, PM_SWELL };
struct Style {
	const char* name;  // "East Coast Boom Bap"
	const char* shortName; // for the field: "East Coast"
	int era;
	float bpm, swing;  // swing 0.5 straight .. 0.72
	// one bar per drum voice, 16 characters: X strong, x normal, o ghost (comes with DENS), . nothing
	const char* drum[DRUMS];
	float hatRoll;     // trap rolls on the hats
	float humanize;    // timing spread (fraction of a step)
	float dilla;       // drunk timing: late snares, loose kicks
	int scale, color;  // color 0 triads, 1 sevenths, 2 ninths
	const char* prog;  // scale degree (1-based) per bar, "1 6 4 5"
	int bass, mel, pad;
	int melOct;        // melody's octave (MIDI note of its centre)
	// the sample chopper
	float chopLoop;    // 0: stabs, 1: the loop as it is
	float chopPitch;   // how often a chop is pitched
	int chopUp;        // semitones all chops go up (Soul Chop: the chipmunk)
	float chopRev, chopStutter, chopOff;
};
static const int NUM_STYLES = 29;
extern const Style STYLES[NUM_STYLES];
int styleIndexInEra(int style);
int stylesInEra(int era, int* out, int max);

// ---------------------------------------------------------------- the patterns
struct Hit {
	float vel = 0.f;   // 0: no hit
	float prio = 0.f;  // DENS shows it when prio <= DENS
	float off = 0.f;   // timing, in steps (-0.45 .. 0.45)
	uint8_t ratchet = 1; // hits in the step (trap rolls)
	float chance = 1.f;  // 0..1: how likely it plays (CHOP GRID)
	uint8_t cond = 0;    // 1: only every 2nd time round
};
struct Note {
	int8_t pitch = -1; // MIDI note, -1: none
	float len = 1.f;   // in steps
	float vel = 0.8f;
	float prio = 0.f;
	float off = 0.f;
	bool acc = false, slide = false;
	float chance = 1.f;
	uint8_t cond = 0;
};
struct PadEv {
	int8_t pitch[PAD_VOICES] = {-1, -1, -1, -1};
	float len = 4.f, vel = 0.7f, prio = 0.f, off = 0.f;
	float mod = 0.f;
	float chance = 1.f;
	uint8_t cond = 0;
};
struct ChopEv {
	int8_t pad = -1;   // -1: none
	float len = 1.f;   // in steps
	float vel = 0.9f;
	float off = 0.f;
	int8_t pitch = 0;  // semitones
	bool rev = false;
	uint8_t ratchet = 1;
	float prio = 0.f;
	float chance = 1.f;
	uint8_t cond = 0;
};
struct Pattern {
	Hit drum[DRUMS][LEN];
	Note bass[LEN];
	Note mel[LEN];
	PadEv pad[LEN];
	ChopEv chop[LEN];
	Chord bar[BARS]; // the chords per bar
	// from GENRA CHOP GRID
	uint8_t lenTrack[NUM_TRACKS] = {0, 0, 0, 0, 0}; // own length in steps (0: the loop, 64)
	uint8_t lenDrum[DRUMS] = {0, 0, 0, 0, 0, 0, 0, 0};
	float swTrack[NUM_TRACKS] = {0, 0, 0, 0, 0};    // own swing 0.5..0.75 (0: the module's SWING)
	float swDrum[DRUMS] = {0, 0, 0, 0, 0, 0, 0, 0};
	bool edited[NUM_TRACKS] = {false, false, false, false, false}; // fixed notes: DENS leaves them, NEW ALL keeps them
	bool chordFollow = true; // edited bass / melody / pads move with new chords
	void clear();
};

// ---------------------------------------------------------------- what the generator knows of the sample
struct ChopInfo {
	float seconds = 0.f; // length of the chop
	Chord chord;         // what is in it (Q_NONE: a hit / unclear)
	float conf = 0.f;    // 0..1
	int kind = 0;        // 0 sustain, 1 stab, 2 hit (no pitch)
	float centroid = 0.f; // spectral centre (Hz): for the break (kick low, hats high)
};
struct SampleInfo {
	int count = 0; // chops (0: no sample)
	ChopInfo chop[PADS];
	Key key;
	float keyConf = 0.f;
};

// per pad, set by hand (right-click)
struct PadSet {
	bool lock = false, rev = false;
	int8_t pitch = 0;
	int8_t chordRoot = -1, chordQ = -1; // -1: the detected chord
};

struct Settings {
	int style = 5;
	Key key;
	bool keyAuto = true;
	int bassMode = 0;  // 0 FOLLOW (the chop's chord), 1 ROOT+5, 2 WALK, 3 808
	int melMode = 0;   // 0 STAB, 1 LEAD, 2 ARP, 3 RIFF
	int padMode = 0;   // 0 SUSTAIN, 1 STAB, 2 SWELL
	int drumMode = 0;  // 0 KIT, 1 BREAK, 2 BOTH
};
extern const char* const BASS_MODES[4];
extern const char* const MEL_MODES[4];
extern const char* const PAD_MODES[3];
extern const char* const DRUM_MODES[3];

/** The chord a pad stands for (the hand-set one, else the detected one; Q_NONE if neither). */
Chord padChord(const SampleInfo& si, const PadSet* ps, int pad);

/** The scale the style plays in the key (a major key with a minor style plays major and the other way round). */
int scaleOf(const Settings& set);
bool inScale(int note, int scale, int keyRoot);
int snapToScale(int note, int scale, int keyRoot);
/** Chord tones as pitch classes (up to 5); returns the count. */
int tonesOf(const Chord& c, int* out);

/** NEW: roll the base of one track (or all: track -1). `lockChords`: keep the bars' chords. */
void generate(Pattern& p, int track, const Settings& set, const SampleInfo& si, const PadSet* pads, uint32_t seed,
              bool lockChords, float bpm);
/** MUTATE: change about `amount` of the chop events (locked pads stay). */
void mutateChops(Pattern& p, const Settings& set, const SampleInfo& si, const PadSet* pads, uint32_t seed, float amount,
                 float bpm);
/** The chord sounding at step s (the chop's chord if the sample has one there, else the bar's). */
Chord chordAt(const Pattern& p, const SampleInfo& si, const PadSet* pads, int s);

// ---------------------------------------------------------------- rendering (live)
struct Render {
	Hit drum[DRUMS][LEN];
	Note bass[LEN];
	Note mel[LEN];
	PadEv pad[LEN];
	ChopEv chop[LEN];
};
struct Knobs {
	float dens[NUM_TRACKS] = {0.5f, 0.5f, 0.5f, 0.5f, 1.f};
	float cut[NUM_TRACKS] = {0.f, 0.f, 0.f, 0.f, 0.f}; // MIDI chop (drums, melody, pads)
	int bassOct = 0;
	uint32_t cutSeed = 1;
};
void render(const Pattern& p, const Knobs& k, Render& r);

} // namespace hh
