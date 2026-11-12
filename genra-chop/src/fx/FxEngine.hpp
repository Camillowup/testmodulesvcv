// SPDX-License-Identifier: GPL-3.0-only
// GENRA CHOP FX: the effects (no Rack in here: test/fx_test.cpp).
//
// Two buses, each with one effect at a time out of 37 (six groups), three controls (CTRL 1-3, what they do depends on
// the effect) and ON. Written to cost little: a bus that is off and quiet sleeps, only the chosen effect runs,
// coefficients follow the controls every 16 samples (not per sample), the drives use antiderivative anti-aliasing
// instead of oversampling. Echoes and reverbs ring out after OFF, then the bus sleeps.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace fx {

enum Group { G_TEXTURE, G_FILTER, G_DRIVE, G_SPACE, G_MOD, G_PERFORM, NUM_GROUPS };
extern const char* const GROUP_NAMES[NUM_GROUPS];

// the order is the list on the panel; new effects only at the end (patches keep the number)
enum Id {
	VINYL, CASSETTE, LOFI, WOWFLUT, RADIO, DUST, WARP,              // TEXTURE
	FILTDRIVE, SUPERFILT, ISOLATOR, RESONATOR, TALK, AUTOWAH,      // FILTER
	SATURATOR, FUZZ, FOLDER, COMP, GATER,                          // DRIVE
	DELAY, TAPEECHO, PINGPONG, REVDELAY, REVERB,                   // SPACE
	CHORUS, FLANGER, PHASER, RINGMOD, TREMOLO, VIBRATO,            // MOD
	LOOPER, SCATTER, STOPPER, TAPESTOP, BACKSPIN, TAPESTART, DOWNER, SUBOCT, // PERFORM
	LOWCUT, HIGHCUT,                                               // FILTER (2.4.0: the DJ sweeps)
	NUM_FX
};

struct Info {
	const char* name;
	int group;
	const char* ctrl[3];
	float def[3];
	bool tail; // rings out after OFF (echoes, reverb): the dry stays, the wet is added
};
extern const Info FX[NUM_FX];

// note values in beats (delays, gater, looper): synced to the clock (16ths at CLK) or to the tempo set in the menu
static const int NUM_DIVS = 12;
extern const float DIV_BEATS[NUM_DIVS];
extern const char* const DIV_NAMES[NUM_DIVS];

struct Tempo {
	float spb = 22050.f;  // samples per beat
	double barPos = 0.0;  // 0..1 over a bar of 4 beats
};

/** What CTRL k of an effect shows for the value v (0..1). */
std::string valueText(int fx, int k, float v);

struct Ring {
	std::vector<float> b;
	uint32_t mask = 0;
	int64_t w = 0;
	void init(int pow2) {
		b.assign((size_t) 1 << pow2, 0.f);
		mask = (uint32_t) b.size() - 1;
		w = 0;
	}
	void clear() { std::fill(b.begin(), b.end(), 0.f); }
	void push(float x) {
		b[(uint32_t) w & mask] = x;
		w++;
	}
	/** The sample `d` samples ago (d >= 1, fractional). */
	float tap(float d) const {
		if (d < 1.f)
			d = 1.f;
		double pos = (double) w - d;
		int64_t i0 = (int64_t) (pos < 0 ? pos - 1.0 : pos);
		if ((double) i0 > pos)
			i0--;
		float fr = (float) (pos - (double) i0);
		float a = b[(uint32_t) i0 & mask], c = b[(uint32_t) (i0 + 1) & mask];
		return a + (c - a) * fr;
	}
	/** The sample at absolute position p (fractional). */
	float at(double p) const {
		int64_t i0 = (int64_t) p;
		if ((double) i0 > p)
			i0--;
		float fr = (float) (p - (double) i0);
		float a = b[(uint32_t) i0 & mask], c = b[(uint32_t) (i0 + 1) & mask];
		return a + (c - a) * fr;
	}
	size_t size() const { return b.size(); }
};

struct SVF {
	float a1 = 0.f, a2 = 0.f, a3 = 0.f, k = 1.f, ic1 = 0.f, ic2 = 0.f;
	void set(float fc, float q, float sr);
	void run(float v0, float& lp, float& bp, float& hp) {
		float v3 = v0 - ic2;
		float v1 = a1 * ic1 + a2 * v3;
		float v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.f * v1 - ic1;
		ic2 = 2.f * v2 - ic2;
		lp = v2, bp = v1, hp = v0 - k * v1 - v2;
	}
	void reset() { ic1 = ic2 = 0.f; }
};

/** One bus: one effect, three controls, ON. The module sets want / on / c and calls process() per sample. */
struct Bus {
	// ---- set by the module
	int want = VINYL;
	bool on = false;
	float c[3] = {0.5f, 0.5f, 0.5f};
	bool retrigger = false; // the one-shots (stops, spins) start again (a quick key on the same effect)

	/** ringPow: the ring for loops and echoes, 2^ringPow samples per side (19: 11 s at 48 kHz). */
	explicit Bus(int ringPow = 19);
	void setSampleRate(float sr);
	void process(float& L, float& R, const Tempo& t);
	/** After process(): how much of the input is still in the output as it was (1: all, the effect off or only
	 *  added on top; 0: all replaced). The rest of the output is the effect's: output - input x keep(). */
	float keep() const { return keepNow; }
	/** Running (or ringing out): for the panel's light. */
	bool busy() const { return !sleeping; }
	int current() const { return cur; }

  private:
	float sr = 44100.f;
	int cur = -1;
	float keepNow = 1.f;
	float g = 0.f, sw = 1.f; // on/off fade, effect-change fade
	bool sleeping = true, wasOn = false, trig = false;
	int quiet = 0, blockLeft = 0;
	float cs[3] = {-1.f, -1.f, -1.f}; // the controls the coefficients were made for
	float spbSeen = 0.f;
	uint32_t rng = 0x9e3779b9u;

	// shared state (one effect at a time)
	Ring ring[2];
	SVF f[2][3];
	float lp[2][4] = {}, hp[2][2] = {}, dc[2][2] = {}, held[2] = {}, adaa[2] = {}, env[2] = {}, ap[2][6] = {};
	double ph = 0.0, ph2 = 0.0;          // LFOs (0..1)
	double rp[2] = {0.0, 0.0};           // read positions (loops, spins)
	float rate = 1.f, t0 = 0.f;          // varispeed
	int64_t loopStart = 0;
	int loopLen = 0, holdCount = 0, sliceAt = 0, slicePlay = 0, sliceLeft = 0;
	bool frozen = false, sliceRev = false;
	float oscC = 1.f, oscS = 0.f;        // ring mod oscillator
	float comb[2][3][4096] = {};         // resonator
	int combW = 0;
	std::vector<float> rv[8];            // reverb lines
	int rvW[8] = {}, rvLen[8] = {};
	std::vector<float> rvAp[4];
	int rvApW[4] = {}, rvApLen[4] = {};
	float rvLp[8] = {};
	float subState[2] = {}, subSign[2] = {1.f, 1.f}, subLast[2] = {};

	// coefficients (prep)
	float k1 = 0.f, k2 = 0.f, k3 = 0.f, k4 = 0.f, k5 = 0.f, k6 = 0.f;
	float fdel = 1.f, ffb = 0.f, fmix = 0.f;

	void reset();
	void prep(const Tempo& t);
	float noise() {
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return (int32_t) rng * (1.f / 2147483648.f);
	}
	void runInsert(float& L, float& R, const Tempo& t);
	void runSpace(float inL, float inR, float& wL, float& wR, const Tempo& t);
	float reverbTick(float inL, float inR, float& outR);
	bool writesLive() const;
};

// ---------------------------------------------------------------- the presets (both buses; ON stays off)
static const int ROUTE_12 = 0, ROUTE_21 = 1, ROUTE_PAR = 2;
extern const char* const ROUTE_NAMES[3];
struct Preset {
	const char* name;
	int fx[2];
	float c[2][3];
	int route;
};
extern const Preset PRESETS[];
extern const int NUM_PRESETS;

} // namespace fx
