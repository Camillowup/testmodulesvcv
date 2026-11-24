// SPDX-License-Identifier: GPL-3.0-only
// KRATZ – the scratch module: the parts without Rack (tested in test/kratz_test.cpp).
//   Deck     the record: a position that a motor (SPEED, DRAG) or a hand moves; reads the sample with cubic
//            interpolation, SLIP's ghost, LOOP, the pitch shifter (TUNE, PITCH, V/OCT, STRETCH), the fader gain, TONE
//            (a DJ filter) and a DC blocker (a record that stands still is silent).
//   Patterns the 23 scratch patterns: where the hand is (in strokes, 0 = the anchor, 1 = one stroke forward) and
//            whether the fader is open, over one cycle (RATE).
//   detectKey / detectBpm  what the sample is in: the root (Krumhansl profiles on a chroma) and its tempo (onsets,
//            autocorrelation).
// The WAV reader and the sample in memory are SCHLAG's (genra/schlag/SchlagDsp.hpp).
#pragma once
#include "../../genra/schlag/SchlagDsp.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace kratz {

using schlag::Sample;
static const float PI = 3.14159265358979f;

inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline float fracf(float x) { return x - std::floor(x); }

// ---------------------------------------------------------------- the patterns
enum Pat {
	P_OFF,
	P_BABY, P_FORWARD, P_BACKWARD, P_DRAG, P_SCRIBBLE, P_UZI, P_TEAR, P_CLOVER, P_SWIPE, P_SPINBACK,  // the record only
	P_CHIRP, P_TRANSFORMER, P_FLARE1, P_FLARE2, P_FLARE3, P_ORBIT, P_CRAB, P_TWIDDLE,                 // with the fader
	P_MILITARY, P_AQUAMAN, P_BOOMERANG,                                                               // combinations
	P_RANDOM, P_COMBO,                                                                                // KRATZ's own
	NUM_SINGLES,
	P_PHRASE0 = NUM_SINGLES // the phrases follow (PHRASES[])
};
static const char* const PAT_NAMES[NUM_SINGLES] = {
    "OFF", "BABY", "FORWARD", "BACKWARD", "DRAG", "SCRIBBLE", "UZI", "TEAR", "CLOVER TEAR", "SWIPE", "SPINBACK",
    "CHIRP", "TRANSFORMER", "1-CLICK FLARE", "2-CLICK FLARE", "3-CLICK FLARE", "ORBIT", "CRAB", "TWIDDLE",
    "MILITARY", "AQUAMAN", "BOOMERANG", "RANDOM", "COMBO"};
inline float tri(float t) { return t < 0.5f ? 2.f * t : 2.f - 2.f * t; }
/** n clicks (short closings of the fader) in a stroke s (0..1): the fader is closed near (k + 1) / (n + 1). */
inline float clicks(float s, int n, float w = 0.045f) {
	for (int k = 0; k < n; k++)
		if (std::fabs(s - (k + 1.f) / (n + 1.f)) < w)
			return 0.f;
	return 1.f;
}
inline float seg(float t, float a, float b, float xa, float xb) { return xa + (xb - xa) * (t - a) / (b - a); }

/** Pattern p at t (0..1 of a cycle): x where the hand is (in strokes), g the fader (0..1). */
inline void shape(int p, float t, float& x, float& g) {
	t = fracf(t);
	float s = fracf(2.f * t); // the stroke: forward (t < 0.5) or back
	x = tri(t);
	g = 1.f;
	switch (p) {
		case P_BABY: break;
		case P_FORWARD: g = t < 0.5f ? 1.f : 0.f; break;
		case P_BACKWARD: g = t < 0.5f ? 0.f : 1.f; break;
		case P_DRAG:
			x = t < 0.85f ? 0.35f * t / 0.85f : 0.35f * (1.f - (t - 0.85f) / 0.15f);
			g = t < 0.85f ? 1.f : 0.f;
			break;
		case P_SCRIBBLE: x = 0.25f * tri(fracf(4.f * t)); break;
		case P_UZI: x = 0.12f * tri(fracf(8.f * t)); break;
		case P_TEAR:
			if (t < 0.4f)
				x = t / 0.4f;
			else if (t < 0.6f)
				x = seg(t, 0.4f, 0.6f, 1.f, 0.5f);
			else if (t < 0.7f)
				x = 0.5f;
			else
				x = seg(t, 0.7f, 1.f, 0.5f, 0.f);
			break;
		case P_CLOVER: {
			static const float T[9] = {0.f, 0.2f, 0.25f, 0.45f, 0.5f, 0.7f, 0.75f, 0.95f, 1.f};
			static const float X[9] = {0.f, 0.5f, 0.5f, 1.f, 1.f, 0.5f, 0.5f, 0.f, 0.f};
			for (int i = 0; i < 8; i++)
				if (t < T[i + 1]) {
					x = seg(t, T[i], T[i + 1], X[i], X[i + 1]);
					break;
				}
			break;
		}
		case P_SWIPE: x = t < 0.2f ? t / 0.2f : 1.f - (t - 0.2f) / 0.8f; break;
		case P_SPINBACK:
			if (t < 0.35f) {
				float u = t / 0.35f;
				x = -2.f * u * u;
			} else {
				float u = (t - 0.35f) / 0.65f;
				x = -2.f + 2.f * u * u;
			}
			break;
		case P_CHIRP: g = (t < 0.3f || t >= 0.7f) ? 1.f : 0.f; break;
		case P_TRANSFORMER: g = fracf(8.f * t) < 0.5f ? 1.f : 0.f; break;
		case P_FLARE1: g = clicks(s, 1); break;
		case P_FLARE2: g = clicks(s, 2); break;
		case P_FLARE3: g = clicks(s, 3, 0.035f); break;
		case P_ORBIT: g = (s < 0.06f || s > 0.94f) ? 0.f : clicks(s, 2); break;
		case P_CRAB: g = (s < 0.5f && fracf(s * 8.f) < 0.5f) ? 1.f : 0.f; break;
		case P_TWIDDLE: g = (s < 0.5f && fracf(s * 4.f) < 0.5f) ? 1.f : 0.f; break;
		case P_MILITARY:
			x = tri(fracf(2.f * t));
			if (t >= 0.5f) {
				float u = 2.f * t - 1.f;
				g = (u < 0.3f || u >= 0.7f) ? 1.f : 0.f;
			}
			break;
		case P_AQUAMAN: g = t < 0.5f ? clicks(s, 2) : 1.f; break;
		case P_BOOMERANG: {
			x = tri(fracf(2.f * t));
			float u = fracf(4.f * t);
			g = (u < 0.15f || (u > 0.45f && u < 0.55f)) ? 0.f : 1.f;
			break;
		}
		default: break;
	}
}

/** RANDOM picks a pattern per cycle, COMBO plays a phrase (BABY, CHIRP, 1-CLICK FLARE, BABY). */
inline int resolvePattern(int p, int cycle, uint32_t seed) {
	if (p == P_COMBO) {
		static const int C[4] = {P_BABY, P_CHIRP, P_FLARE1, P_BABY};
		return C[((cycle % 4) + 4) % 4];
	}
	if (p == P_RANDOM) {
		uint32_t h = (uint32_t) cycle * 2654435761u ^ seed;
		h ^= h >> 13;
		h *= 0x5bd1e995u;
		h ^= h >> 15;
		return P_BABY + (int) (h % (uint32_t) (P_BOOMERANG - P_BABY + 1));
	}
	return p;
}

// ---------------------------------------------------------------- the phrases (after Akai's Flex Beat)
// A phrase plays over the running record: the record keeps its place in time underneath (as with SLIP), the phrase
// says where the hand moves it and when the fader is open, on a grid of 16ths (16 per bar, 1 or 2 bars). At its end the
// record is where it would have been. One character per 16th; '-' lengthens the action before it:
//   P play  . rest (fader closed)  R repeat the action's first 16th  H half speed  V backwards  J / j one beat later /
//   earlier (a juggle: kick and snare change places)  S tape stop  K backspin
//   scratches (a stroke over RANGE): b baby  k forward  h backward  d drag  s scribble  u uzi  r tear  y clover tear
//   w swipe  c chirp  t transformer  f 1-click flare  F 2-click flare  o orbit  x crab  g twiddle  m military
//   a aquaman  q boomerang
struct Phrase {
	const char* name;
	const char* group;
	const char* steps;
};
static const Phrase PHRASES[] = {
    {"PREMIER HOOK", "DJ Scratch: Boom Bap", "PPPPPPPPb-b-b-b-PPPPPPPPc-c-k-k-"},
    {"TWO-FOUR CHIRPS", "DJ Scratch: Boom Bap", "PPPPc-c-PPPPc-c-"},
    {"BABY TALK", "DJ Scratch: Boom Bap", "b-b-b-b-PPPPb-b-"},
    {"FORWARD STABS", "DJ Scratch: Boom Bap", "PPPPk-PPPPk-k-PP"},
    {"TEAR DOWN", "DJ Scratch: Boom Bap", "PPPPPPPPr---r---"},
    {"HOOK FILL", "DJ Scratch: Boom Bap", "PPPPPPPPPPPPPPPPb-c-f-F-b-c-t---"},
    {"OFFBEAT STAB", "DJ Scratch: Techno / House", "PPk-PPk-PPk-PPk-"},
    {"TRANSFORMER ROLL", "DJ Scratch: Techno / House", "PPPPPPPPt---t---"},
    {"GATE CHOPS", "DJ Scratch: Techno / House", "P.P.P.P.PP..PP.."},
    {"DROP BRAKE", "DJ Scratch: Techno / House", "PPPPPPPPPPPPPPPPPPPPPPPPS-------"},
    {"ACID WOBBLE", "DJ Scratch: Techno / House", "PPPPd---PPPPd---"},
    {"FLARE RUN", "Turntablist", "f-f-F---F---f-f-"},
    {"CRAB ATTACK", "Turntablist", "PPPPx-x-x-x-PPPP"},
    {"TRANSFORMER BATTLE", "Turntablist", "t---t---t---t---"},
    {"BOOMERANG COMBO", "Turntablist", "b-b-q---PPPPPPPPPPPPPPPPx-x-q---"},
    {"ORBIT FLOW", "Turntablist", "o-o-o-o-a-a-a-a-"},
    {"BATTLE BURST", "Turntablist", "u---s---x-g-m---"},
    {"BACKSPIN", "Backspins & Stops", "PPPPPPPPPPPPK---"},
    {"TAPE STOP", "Backspins & Stops", "PPPPPPPPS-------"},
    {"LONG BRAKE", "Backspins & Stops", "PPPPPPPPPPPPPPPPS---------------"},
    {"SPIN & DROP", "Backspins & Stops", "PPPPPPPPK---...."},
    {"REWIND", "Backspins & Stops", "PPPPPPPPPPPPPPPPPPPPPPPPV-------"},
    {"SCRIBBLE FILL", "Breaks / DnB / Big Beat", "PPPPPPPPPPPPs---"},
    {"UZI BREAK", "Breaks / DnB / Big Beat", "PPPPPPPPu---u---"},
    {"AMEN CUTS", "Breaks / DnB / Big Beat", "PPk-PPk-PPPPb-b-"},
    {"BIG BEAT RISE", "Breaks / DnB / Big Beat", "PPPPPPPPPPPPPPPPR-R-RRRRRRRRK---"},
    {"SPINBACK SWITCH", "Breaks / DnB / Big Beat", "PPPPPPPPPPPPPPPPPPPPPPPPs---K---"},
    {"KICK SWAP", "DJ Patterns", "PPPPJJJJPPPPjjjj"},
    {"HALF TIME", "DJ Patterns", "H-------H-------"},
    {"STUTTER REPEAT", "DJ Patterns", "PPPPPPPPR-------"},
    {"REVERSE SNARE", "DJ Patterns", "PPPPV-PPPPPPV-PP"},
    {"JUGGLE SHUFFLE", "DJ Patterns", "PPPPjjjjPPPPJJJJPPPPPPPPjjjjPPPP"},
    {"DOUBLE BACK", "DJ Patterns", "PPPPPPPPV---PPPP"},
};
static const int NUM_PHRASES = (int) (sizeof(PHRASES) / sizeof(PHRASES[0]));
static const int NUM_PATS = NUM_SINGLES + NUM_PHRASES; // everything in the PAT field

inline bool isPhrase(int p) { return p >= P_PHRASE0 && p < NUM_PATS; }
inline const char* patName(int p) {
	if (p >= 0 && p < NUM_SINGLES)
		return PAT_NAMES[p];
	if (isPhrase(p))
		return PHRASES[p - P_PHRASE0].name;
	return "?";
}
inline const char* patGroup(int p) {
	if (p == P_OFF)
		return "Off";
	if (p <= P_SPINBACK)
		return "Single: the record (fader open)";
	if (p <= P_TWIDDLE)
		return "Single: with the fader (clicks)";
	if (p <= P_BOOMERANG)
		return "Single: combinations";
	if (p < NUM_SINGLES)
		return "Single: auto";
	if (isPhrase(p))
		return PHRASES[p - P_PHRASE0].group;
	return "";
}
/** A phrase's length in 16ths. */
inline int phraseSteps(int p) { return isPhrase(p) ? (int) std::strlen(PHRASES[p - P_PHRASE0].steps) : 0; }
inline int phraseBars(int p) { return phraseSteps(p) / 16; }

/** The scratch behind a character of a phrase, and the length of its cycle in quarters. */
inline bool phraseScratch(char ch, int& pat, float& cycle) {
	switch (ch) {
		case 'b': pat = P_BABY; cycle = 0.5f; return true;
		case 'k': pat = P_FORWARD; cycle = 0.5f; return true;
		case 'h': pat = P_BACKWARD; cycle = 0.5f; return true;
		case 'd': pat = P_DRAG; cycle = 1.f; return true;
		case 's': pat = P_SCRIBBLE; cycle = 1.f; return true;
		case 'u': pat = P_UZI; cycle = 1.f; return true;
		case 'r': pat = P_TEAR; cycle = 1.f; return true;
		case 'y': pat = P_CLOVER; cycle = 1.f; return true;
		case 'w': pat = P_SWIPE; cycle = 0.5f; return true;
		case 'c': pat = P_CHIRP; cycle = 0.5f; return true;
		case 't': pat = P_TRANSFORMER; cycle = 1.f; return true;
		case 'f': pat = P_FLARE1; cycle = 0.5f; return true;
		case 'F': pat = P_FLARE2; cycle = 1.f; return true;
		case 'o': pat = P_ORBIT; cycle = 0.5f; return true;
		case 'x': pat = P_CRAB; cycle = 0.5f; return true;
		case 'g': pat = P_TWIDDLE; cycle = 0.5f; return true;
		case 'm': pat = P_MILITARY; cycle = 1.f; return true;
		case 'a': pat = P_AQUAMAN; cycle = 0.5f; return true;
		case 'q': pat = P_BOOMERANG; cycle = 1.f; return true;
		default: return false;
	}
}

/** Phrase p at e quarters from its start (0 .. its length): off = how far (in quarters) the record is from where it
 * would be, g the fader, playLike: the record moves as it plays (the pitch shifter may stay in). strokeQ: a stroke in
 * quarters (RANGE). */
inline void phraseAt(int p, float e, float strokeQ, float& off, float& g, bool& playLike) {
	off = 0.f;
	g = 1.f;
	playLike = true;
	if (!isPhrase(p))
		return;
	const char* st = PHRASES[p - P_PHRASE0].steps;
	int n = (int) std::strlen(st);
	int i = clampf(std::floor(e * 4.f), 0.f, (float) (n - 1));
	int a = i;
	while (a > 0 && st[a] == '-')
		a--;
	int b = i + 1;
	while (b < n && st[b] == '-')
		b++;
	float s = a * 0.25f, L = (b - a) * 0.25f, el = e - s, u = clampf(el / L, 0.f, 1.f);
	float rel = el;
	char ch = st[a];
	int sp;
	float cyc;
	if (phraseScratch(ch, sp, cyc)) {
		float x, gg;
		shape(sp, el / cyc, x, gg);
		rel = x * strokeQ;
		g = gg;
		playLike = false;
	} else
		switch (ch) {
			case '.': g = 0.f; break;
			case 'R': rel = std::fmod(el, 0.25f); break;
			case 'H': rel = el * 0.5f; break;
			case 'V': rel = -el; break;
			case 'J': rel = el + 1.f; break;
			case 'j': rel = el - 1.f; break;
			case 'S':
				rel = L * (u - 0.5f * u * u);
				g = u > 0.9f ? (1.f - u) * 10.f : 1.f;
				playLike = false;
				break;
			case 'K':
				rel = -2.f * L * u * u;
				g = 1.f - u * u * u;
				playLike = false;
				break;
			default: break;
		}
	off = rel - el;
}

// the modes of a phrase (as Flex Beat)
enum PhraseMode { M_LOOP, M_HOLD, M_ONESHOT, NUM_MODES };
static const char* const MODE_NAMES[NUM_MODES] = {"LOOP", "HOLD", "ONE SHOT"};

// RATE: one cycle (a stroke forward and back) in quarters
static const int NUM_RATES = 8;
static const char* const RATE_NAMES[NUM_RATES] = {"1 BAR", "1/2", "1/4", "1/8", "1/16", "1/4 T", "1/8 T", "1/16 T"};
static const float RATE_BEATS[NUM_RATES] = {4.f, 2.f, 1.f, 0.5f, 0.25f, 2.f / 3.f, 1.f / 3.f, 1.f / 6.f};
// LOOP lengths in quarters
static const int NUM_LOOPS = 6;
static const char* const LOOP_NAMES[NUM_LOOPS] = {"1/8", "1/4", "1/2", "1", "2", "4"};
static const float LOOP_BEATS[NUM_LOOPS] = {0.125f, 0.25f, 0.5f, 1.f, 2.f, 4.f};
/** RANGE: the stroke (and the SCR fader's travel) in quarters, 1/16 .. 2 bars. */
inline float rangeBeats(float v) { return 0.25f * std::pow(32.f, clampf(v, 0.f, 1.f)); }
/** DRAG: how long the motor takes to start or stop the record (seconds). */
inline float dragTime(float v) { return 0.004f * std::pow(400.f, clampf(v, 0.f, 1.f)); }
/** SPEED: the motor's speed, 0 .. 2 (centre 1, a little detent around it). */
inline float speedOf(float v) {
	v = clampf(v, 0.f, 1.f);
	float d = v - 0.5f;
	if (std::fabs(d) < 0.02f)
		return 1.f;
	return std::pow(2.f, (d - (d > 0 ? 0.02f : -0.02f)) / 0.48f);
}
/** The LVL fader: sharp (a scratch fader: open after a few percent) or smooth (a volume). */
inline float faderGain(float v, bool sharp) {
	v = clampf(v, 0.f, 1.f);
	if (sharp)
		return clampf(v / 0.06f, 0.f, 1.f);
	return v * v;
}

// ---------------------------------------------------------------- the pitch shifter
/** Two grains crossfading over a delay line: shifts the pitch by `ratio` without changing the time. */
struct Shifter {
	static const int N = 1 << 14;
	std::vector<float> buf = std::vector<float>(N * 2, 0.f);
	int w = 0;
	float d = 0.f; // the first grain's delay (samples)
	float grain = 2048.f;
	void setSampleRate(float sr) { grain = std::max(512.f, std::min(4096.f, sr * 0.045f)); }
	void reset() {
		std::fill(buf.begin(), buf.end(), 0.f);
		d = 0.f;
	}
	inline float rd(float pos, int c) const {
		float p = (float) w - pos;
		while (p < 0.f)
			p += N;
		int i = (int) p;
		float f = p - i;
		float a = buf[((i) & (N - 1)) * 2 + c], b = buf[((i + 1) & (N - 1)) * 2 + c];
		return a + (b - a) * f;
	}
	void process(float inL, float inR, float ratio, float& outL, float& outR) {
		buf[w * 2] = inL;
		buf[w * 2 + 1] = inR;
		d += 1.f - ratio;
		while (d < 0.f)
			d += grain;
		while (d >= grain)
			d -= grain;
		float d2 = d + grain * 0.5f;
		if (d2 >= grain)
			d2 -= grain;
		float w1 = std::sin(PI * d / grain), w2 = std::sin(PI * d2 / grain);
		w1 *= w1;
		w2 *= w2;
		float n = 1.f / std::max(0.5f, w1 + w2);
		outL = (rd(d + 1.f, 0) * w1 + rd(d2 + 1.f, 0) * w2) * n;
		outR = (rd(d + 1.f, 1) * w1 + rd(d2 + 1.f, 1) * w2) * n;
		w = (w + 1) & (N - 1);
	}
};

// ---------------------------------------------------------------- the hand
/** A movement that arrives in steps, spread over the time until the next step (mouse, jog wheel). */
struct Spread {
	double rem = 0.0, rate = 0.0;
	float since = 1e9f;
	void add(double d, float sr) {
		if (d != 0.0) {
			float iv = clampf(since, 0.004f * sr, 0.05f * sr);
			rem += d;
			rate = rem / iv;
			since = 0.f;
		} else
			since += 1.f;
	}
	double step() {
		double s = rate;
		if (std::fabs(s) >= std::fabs(rem)) {
			s = rem;
			rate = 0.0;
		}
		rem -= s;
		return s;
	}
};
/** A value that arrives in steps (a fader), glided in a straight line over the time until the next step. */
struct Glide {
	float cur = 0.5f, target = 0.5f, inc = 0.f, since = 1e9f;
	void set(float v, float sr) {
		if (v != target) {
			float iv = clampf(since, 0.003f * sr, 0.04f * sr);
			inc = (v - cur) / iv;
			target = v;
			since = 0.f;
		} else
			since += 1.f;
	}
	float next() {
		if (std::fabs(target - cur) <= std::fabs(inc))
			cur = target;
		else
			cur += inc;
		return cur;
	}
};

// ---------------------------------------------------------------- the record
struct DeckIn {
	float rate = 1.f;       // the motor's speed in sample frames per output sample (signed: REV is negative)
	float drag = 0.05f;     // seconds the motor takes from standstill to full speed
	bool hand = false;      // a hand (mouse, fader, CV, pattern, MOTION) holds the record
	double handPos = 0.0;   // … at this frame
	bool slip = false;      // SLIP: when the hand lets go, the record is where it would have been
	bool loop = false;
	double loopStart = 0.0, loopLen = 0.0; // frames
	float gain = 1.f;       // the fader (and CUT)
	float shift = 1.f;      // pitch ratio for the shifter (1: off)
	bool shiftOn = false;   // the shifter is in the path (the motor plays; a scratch goes past it)
	bool shiftHand = false; // … also while a hand moves the record as it plays (a phrase's PLAY, REPEAT, JUGGLE)
	float tone = 0.5f;      // 0 low pass .. 0.5 open .. 1 high pass
};

struct Deck {
	const Sample* smp = nullptr;
	float sr = 44100.f;
	double pos = 0.0, ghost = 0.0;
	double handSm = 0.0;
	float vel = 0.f;        // frames per output sample
	bool wasHand = false;
	float gainSm = 0.f, shiftMix = 0.f, shiftSm = 1.f, needle = 0.f;
	float lp1 = 0.f, lp2 = 0.f, bpL = 0.f, bpR = 0.f, lpL = 0.f, lpR = 0.f; // the DJ filter (state variable)
	float dcxL = 0.f, dcyL = 0.f, dcxR = 0.f, dcyR = 0.f;
	Shifter sh;
	// for the panel
	float showVel = 0.f;

	void setSampleRate(float s) {
		sr = s;
		sh.setSampleRate(s);
	}
	void setSample(const Sample* s) {
		smp = s;
		pos = ghost = handSm = 0.0;
		vel = 0.f;
		wasHand = false;
	}
	int frames() const { return smp ? smp->frames : 0; }

	inline void read(double p, float& l, float& r) const {
		int n = smp->frames;
		if (n < 4) {
			l = r = 0.f;
			return;
		}
		if (p < 0.0 || p > n - 1) {
			l = r = 0.f;
			return;
		}
		int i = (int) p;
		float f = (float) (p - i);
		int ch = smp->channels;
		const float* d = smp->data.data();
		auto at = [&](int k, int c) {
			k = k < 0 ? 0 : (k >= n ? n - 1 : k);
			return d[(size_t) k * ch + (ch > 1 ? c : 0)];
		};
		for (int c = 0; c < 2; c++) {
			float y0 = at(i - 1, c), y1 = at(i, c), y2 = at(i + 1, c), y3 = at(i + 2, c);
			float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
			float v = ((c3 * f + c2) * f + c1) * f + y1;
			(c == 0 ? l : r) = v;
		}
	}
	/** Wrap a position into the loop (or into the sample: the record goes round). */
	inline double wrap(double p, const DeckIn& in) const {
		int n = frames();
		if (n < 2)
			return 0.0;
		if (in.loop && in.loopLen > 8.0) {
			double a = in.loopStart, b = in.loopStart + in.loopLen;
			if (p >= b || p < a) {
				double q = std::fmod(p - a, in.loopLen);
				if (q < 0)
					q += in.loopLen;
				p = a + q;
			}
			return p;
		}
		if (p >= n || p < 0.0) {
			p = std::fmod(p, (double) n);
			if (p < 0)
				p += n;
		}
		return p;
	}

	/** One output sample (±1 per channel). */
	void process(const DeckIn& in, float& outL, float& outR) {
		if (!smp || smp->frames < 4) {
			outL = outR = 0.f;
			showVel = 0.f;
			return;
		}
		int n = smp->frames;
		ghost = wrap(ghost + in.rate, in);
		if (in.hand) {
			if (!wasHand)
				handSm = pos;
			// the hand: a short glide (0.6 ms) so a stepping fader does not click
			float k = 1.f - std::exp(-1.f / (0.0006f * sr));
			if (std::fabs(in.handPos - handSm) > smp->rate * 0.05f)
				handSm = in.handPos; // a jump (a cue, the loop going round): no sweep through the record
			else
				handSm += (in.handPos - handSm) * k;
			double np = std::max(0.0, std::min((double) n - 1.0, handSm));
			vel = (float) (np - pos);
			pos = np;
		} else {
			if (wasHand && in.slip)
				pos = ghost;
			float k = 1.f - std::exp(-1.f / (std::max(0.002f, in.drag) * sr));
			vel += (in.rate - vel) * k;
			pos = wrap(pos + vel, in);
		}
		if (!in.slip)
			ghost = pos;
		wasHand = in.hand;
		float l, r;
		read(pos, l, r);
		// a record that barely moves is silent (the stylus sees no groove): a soft curve, smoothed (2 ms)
		float sp = std::fabs(vel);
		float mv = clampf(sp * 60.f, 0.f, 1.f);
		mv = mv * mv * (3.f - 2.f * mv);
		needle += (mv - needle) * (1.f - std::exp(-1.f / (0.002f * sr)));
		l *= needle;
		r *= needle;
		// the shifter (only while the motor plays: a scratch goes past it)
		float mk = 1.f - std::exp(-1.f / (0.008f * sr));
		shiftMix += ((in.shiftOn && (!in.hand || in.shiftHand) ? 1.f : 0.f) - shiftMix) * mk;
		shiftSm += (in.shift - shiftSm) * (1.f - std::exp(-1.f / (0.02f * sr)));
		float sl, sr2;
		sh.process(l, r, shiftSm, sl, sr2);
		if (shiftMix > 1e-4f) {
			l += (sl - l) * shiftMix;
			r += (sr2 - r) * shiftMix;
		}
		// the fader: fast (0.7 ms) so the clicks stay crisp but do not crack
		float gk = 1.f - std::exp(-1.f / (0.0007f * sr));
		gainSm += (in.gain - gainSm) * gk;
		l *= gainSm;
		r *= gainSm;
		// TONE: a DJ filter (low pass below the middle, high pass above)
		float t = clampf(in.tone, 0.f, 1.f);
		if (std::fabs(t - 0.5f) > 0.01f) {
			bool hp = t > 0.5f;
			float a = hp ? (t - 0.5f) * 2.f : (0.5f - t) * 2.f;
			float fc = hp ? 20.f * std::pow(400.f, a) : 20000.f * std::pow(0.004f, a);
			float g = std::tan(PI * std::min(fc, sr * 0.45f) / sr);
			float kq = 1.2f;
			auto svf = [&](float x, float& s1, float& s2) {
				float hpv = (x - (kq + g) * s1 - s2) / (1.f + g * (kq + g));
				float v1 = g * hpv, bpv = v1 + s1;
				s1 = bpv + v1;
				float v2 = g * bpv, lpv = v2 + s2;
				s2 = lpv + v2;
				return hp ? hpv : lpv;
			};
			float fl = svf(l, bpL, lpL), fr = svf(r, bpR, lpR);
			float m = clampf(a * 8.f, 0.f, 1.f);
			l += (fl - l) * m;
			r += (fr - r) * m;
		} else {
			bpL = bpR = lpL = lpR = 0.f;
		}
		// DC blocker (10 Hz)
		float R = 1.f - 2.f * PI * 10.f / sr;
		float yl = l - dcxL + R * dcyL, yr = r - dcxR + R * dcyR;
		dcxL = l;
		dcxR = r;
		dcyL = yl;
		dcyR = yr;
		outL = std::isfinite(yl) ? clampf(yl, -2.f, 2.f) : 0.f;
		outR = std::isfinite(yr) ? clampf(yr, -2.f, 2.f) : 0.f;
		if (!std::isfinite(yl) || !std::isfinite(yr)) {
			dcxL = dcyL = dcxR = dcyR = 0.f;
			bpL = bpR = lpL = lpR = 0.f;
		}
		showVel = vel;
	}
};

// ---------------------------------------------------------------- what the sample is in
static const char* const NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/** Mono at about 11 kHz (averages), for the analysis. */
inline std::vector<float> monoLow(const Sample& s, float& rate) {
	int dec = std::max(1, (int) std::round(s.rate / 11025.f));
	rate = s.rate / dec;
	std::vector<float> m((size_t) (s.frames / dec));
	for (size_t i = 0; i < m.size(); i++) {
		float a = 0.f;
		for (int k = 0; k < dec; k++) {
			size_t f = i * dec + k;
			for (int c = 0; c < s.channels; c++)
				a += s.data[f * s.channels + c];
		}
		m[i] = a / (dec * s.channels);
	}
	return m;
}

/** The root (0 = C) and whether it is minor; conf: how sure (0..1). -1 when there is nothing to hear. */
inline int detectKey(const Sample& s, bool& minor, float& conf) {
	minor = false;
	conf = 0.f;
	float rate;
	std::vector<float> m = monoLow(s, rate);
	const int F = 4096, HOP = 2048;
	if ((int) m.size() < F / 2)
		return -1;
	double chroma[12] = {};
	std::vector<float> win(F);
	for (int i = 0; i < F; i++)
		win[i] = 0.5f - 0.5f * std::cos(2.f * PI * i / (F - 1));
	std::vector<float> fr(F);
	int frames = 0;
	for (size_t start = 0; start + F / 2 <= m.size() && frames < 400; start += HOP, frames++) {
		float e = 0.f;
		for (int i = 0; i < F; i++) {
			float v = start + i < m.size() ? m[start + i] : 0.f;
			fr[i] = v * win[i];
			e += fr[i] * fr[i];
		}
		if (e < 1e-6f)
			continue;
		for (int note = 36; note < 96; note++) {
			float f = 440.f * std::pow(2.f, (note - 69) / 12.f);
			if (f > rate * 0.45f)
				break;
			float w = 2.f * std::cos(2.f * PI * f / rate), s1 = 0.f, s2 = 0.f;
			for (int i = 0; i < F; i++) {
				float s0 = fr[i] + w * s1 - s2;
				s2 = s1;
				s1 = s0;
			}
			float mag = s1 * s1 + s2 * s2 - w * s1 * s2;
			chroma[note % 12] += std::sqrt(std::max(0.f, mag)) / std::sqrt(e);
		}
	}
	double tot = 0.0;
	for (double c : chroma)
		tot += c;
	if (tot < 1e-9)
		return -1;
	static const double MAJ[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
	static const double MIN[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
	auto corr = [&](const double* prof, int root) {
		double mx = 0, my = 0;
		for (int i = 0; i < 12; i++) {
			mx += chroma[(i + root) % 12];
			my += prof[i];
		}
		mx /= 12;
		my /= 12;
		double sxy = 0, sxx = 0, syy = 0;
		for (int i = 0; i < 12; i++) {
			double a = chroma[(i + root) % 12] - mx, b = prof[i] - my;
			sxy += a * b;
			sxx += a * a;
			syy += b * b;
		}
		return sxy / std::sqrt(std::max(1e-12, sxx * syy));
	};
	double best = -2, second = -2;
	int root = 0;
	for (int r = 0; r < 12; r++)
		for (int mi = 0; mi < 2; mi++) {
			double c = corr(mi ? MIN : MAJ, r);
			if (c > best) {
				second = best;
				best = c;
				root = r;
				minor = mi;
			} else if (c > second)
				second = c;
		}
	conf = (float) clampf((float) (best - second) * 4.f + (float) best * 0.5f, 0.f, 1.f);
	return root;
}

/** The tempo (BPM, 70..160) of a loop or a song; 0 for a short sound (a one-shot: no tempo of its own). */
inline float detectBpm(const Sample& s) {
	if (s.frames < s.rate * 2.5f)
		return 0.f;
	float rate;
	std::vector<float> m = monoLow(s, rate);
	const int HOP = 128;
	int nf = (int) m.size() / HOP;
	if (nf < 64)
		return 0.f;
	std::vector<float> env(nf), on(nf, 0.f);
	// the energy in a window of 4 hops (46 ms: also a bass note is a steady energy, not a ripple)
	for (int i = 0; i < nf; i++) {
		float e = 0.f;
		for (int k = 0; k < HOP * 4; k++) {
			size_t q = (size_t) i * HOP + k;
			float v = q < m.size() ? m[q] : 0.f;
			float w = 0.5f - 0.5f * std::cos(2.f * PI * k / (HOP * 4));
			e += v * v * w;
		}
		env[i] = std::log(1e-6f + e);
	}
	for (int i = 1; i < nf; i++)
		on[i] = std::max(0.f, env[i] - env[i - 1]);
	float mean = 0.f;
	for (float v : on)
		mean += v;
	mean /= nf;
	for (float& v : on)
		v = std::max(0.f, v - mean);
	float fps = rate / HOP;
	// the autocorrelation of the onsets (normalised by the overlap)
	auto ac = [&](float lag) {
		int l0 = (int) lag;
		float f = lag - l0;
		double a = 0.0;
		int n = 0;
		for (int i = 0; i + l0 + 1 < nf; i++, n++)
			a += on[i] * (on[i + l0] * (1.f - f) + on[i + l0 + 1] * f);
		return n > 0 ? a / n : 0.0;
	};
	// a comb over the beat and its multiples (one beat, two, three, four): the beat that the whole loop agrees with
	float bestBpm = 0.f;
	double best = 0.0;
	for (float bpm = 70.f; bpm < 160.f; bpm += 0.25f) {
		float lag = fps * 60.f / bpm;
		if (lag * 4.f + 2.f >= nf)
			continue;
		double a = ac(lag) + ac(lag * 2.f) + ac(lag * 3.f) + ac(lag * 4.f);
		// half and double tempo fit almost as well: prefer tempos around 120 (a soft prior over an octave)
		float o = std::log2(bpm / 120.f) / 0.6f;
		a *= std::exp(-0.5f * o * o);
		if (a > best * 1.0001) {
			best = a;
			bestBpm = bpm;
		}
	}
	if (best <= 0.0)
		return 0.f;
	return std::round(bestBpm * 2.f) / 2.f;
}

// ---------------------------------------------------------------- writing a recording
/** A float WAV (32 bit, stereo) from the recorder (interleaved). */
inline bool writeWav(const std::string& path, const float* data, int frames, int channels, int rate) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	uint32_t bytes = (uint32_t) frames * channels * 4;
	std::fwrite("RIFF", 1, 4, f);
	u32(36 + bytes);
	std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16);
	u16(3);
	u16((uint16_t) channels);
	u32((uint32_t) rate);
	u32((uint32_t) rate * channels * 4);
	u16((uint16_t) (channels * 4));
	u16(32);
	std::fwrite("data", 1, 4, f);
	u32(bytes);
	std::fwrite(data, 4, (size_t) frames * channels, f);
	std::fclose(f);
	return true;
}

} // namespace kratz
