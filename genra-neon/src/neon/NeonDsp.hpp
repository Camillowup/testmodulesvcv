// SPDX-License-Identifier: GPL-3.0-only
// GENRA NEON – the shared sound parts of the neon modules (WELLE, NEBEL): delay lines, a stereo chorus, a tempo
// ping-pong delay, an octave-up shifter and a modulated 8-line feedback-delay reverb with three characters.
// Plain C++ without Rack, so test/neon_test.cpp can run it.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace neon {

static const float PI = 3.14159265358979f;
static const float C4 = 261.6256f;  // 0 V on V/OCT

inline float clampf(float x, float a, float b) { return std::min(std::max(x, a), b); }
inline float wrap(float p) { return p - std::floor(p); }
/** sin(2πp), p any phase (a 5th-order polynomial on a quarter wave; error < 0.0005). */
inline float sin2pi(float p) {
	p = wrap(p);
	float x = p < 0.5f ? p : p - 0.5f;  // 0..0.5
	float s = p < 0.5f ? 1.f : -1.f;
	x = x < 0.25f ? x : 0.5f - x;       // 0..0.25
	float t = x * 4.f;                  // 0..1 (quarter)
	float t2 = t * t;
	float y = t * (1.5707963f + t2 * (-0.6459640f + t2 * 0.0796926f));
	return s * y;
}
/** Soft saturation, tanh-like, exact ±1 at |x| = 3. */
inline float sat(float x) {
	x = clampf(x, -3.f, 3.f);
	return x * (27.f + x * x) / (27.f + 9.f * x * x);
}
/** A wave folder: folds back what goes past ±1. */
inline float fold(float x) {
	x = 0.25f * x + 0.25f;
	x = x - std::floor(x);
	return 4.f * std::fabs(x - 0.5f) - 1.f;
}
inline float knobTime(float k) { return 0.001f * std::pow(10000.f, clampf(k, 0.f, 1.f)); }  // 1 ms .. 10 s
/** The envelope times of the neon modules: 1 ms .. 6 s, most of the knob for the musical range (0.5: 1.2 s). */
inline float envTime(float k) {
	k = clampf(k, 0.f, 1.f);
	return 0.001f + 6.f * std::pow(k, 2.3f);
}
inline float dbToGain(float db) { return std::pow(10.f, db / 20.f); }

/** A small fast random number generator (xorshift32). */
struct Rng {
	uint32_t s = 0x9e3779b9u;
	explicit Rng(uint32_t seed = 0x9e3779b9u) : s(seed ? seed : 1u) {}
	uint32_t next() {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return s;
	}
	float uni() { return (next() >> 8) * (1.f / 16777216.f); }  // 0..1
	float bi() { return 2.f * uni() - 1.f; }                       // -1..1
};

/** Tempo divisions for MOTION / ORBIT, slowest first (in beats). */
static const int NUM_RATES = 12;
static const float RATE_BEATS[NUM_RATES] = {32.f, 16.f, 8.f, 4.f, 2.f, 1.5f, 1.f, 0.75f, 0.5f, 1.f / 3.f, 0.25f, 0.125f};
static const char* const RATE_NAMES[NUM_RATES] = {"8 BARS", "4 BARS", "2 BARS", "1 BAR", "1/2", "1/4 DOT", "1/4",
                                                   "1/8 DOT", "1/8", "1/8 TRIP", "1/16", "1/32"};
inline int rateIndex(float k) { return clampf(std::round(clampf(k, 0.f, 1.f) * (NUM_RATES - 1)), 0, NUM_RATES - 1); }

/** A quick mute around a jump (a preset, a filter type, a table): down in 4 ms, the change happens in silence, back up
 * in 30 ms. trigger(true): the change needs a clean start (filters, tails) – step() says when to do it. */
struct SwitchFade {
	float g = 1.f;
	int state = 0;  // 0 open / opening, 1 closing, 2 closed (hold)
	int hold = 0;
	bool wantReset = false;
	void trigger(bool reset) {
		if (state == 0)
			state = 1;
		wantReset = wantReset || reset;
		if (state == 2)
			hold = std::max(hold, 64);
	}
	bool busy() const { return state != 0; }
	/** One sample: the gain; `atBottom` true once, when the output is silent (do the reset and take the new level). */
	float step(float fs, bool& atBottom) {
		atBottom = false;
		if (state == 1) {
			g -= 1.f / (0.004f * fs);
			if (g <= 0.f) {
				g = 0.f;
				state = 2;
				hold = (int) (0.025f * fs);
				atBottom = true;
			}
		} else if (state == 2) {
			if (--hold <= 0)
				state = 0;
		} else if (g < 1.f)
			g = std::min(1.f, g + 1.f / (0.03f * fs));
		return g * g;
	}
};

// ---------------------------------------------------------------- delay line
struct Ring {
	std::vector<float> b;
	int w = 0, mask = 0;
	void resize(int n) {
		int s = 4;
		while (s < n)
			s <<= 1;
		b.assign(s, 0.f);
		mask = s - 1;
		w = 0;
	}
	void clear() { std::fill(b.begin(), b.end(), 0.f); }
	void push(float x) {
		b[w] = x;
		w = (w + 1) & mask;
	}
	/** d samples ago (fractional, ≥ 1). */
	float read(float d) const {
		d = clampf(d, 1.f, (float) mask - 2.f);
		float rp = (float) w - d;
		if (rp < 0.f)
			rp += (float) (mask + 1);
		int i = (int) rp;
		float f = rp - (float) i;
		float a = b[i & mask], c = b[(i + 1) & mask];
		return a + (c - a) * f;
	}
	float tap(int d) const { return b[(w - d) & mask]; }
};

// ---------------------------------------------------------------- stereo chorus (two modulated taps, opposite phase)
struct Chorus {
	Ring l, r;
	float ph = 0.f, fs = 48000.f;
	void setSampleRate(float sr) {
		fs = sr;
		l.resize((int) (0.05f * sr));
		r.resize((int) (0.05f * sr));
	}
	void process(float amt, float& L, float& R) {
		l.push(L);
		r.push(R);
		ph = wrap(ph + 0.37f / fs);
		if (amt < 0.001f)
			return;
		float dl = (0.008f + 0.003f * sin2pi(ph)) * fs, dr = (0.008f + 0.003f * sin2pi(ph + 0.5f)) * fs;
		float wl = l.read(dl), wr = r.read(dr);
		L = L * (1.f - 0.35f * amt) + wl * 0.8f * amt;
		R = R * (1.f - 0.35f * amt) + wr * 0.8f * amt;
	}
};

// ---------------------------------------------------------------- ping-pong delay in time with the tempo
struct PingPong {
	Ring l, r;
	float fs = 48000.f, time = -1.f, lp[2] = {}, hp[2] = {};
	void setSampleRate(float sr) {
		fs = sr;
		l.resize((int) (3.2f * sr));
		r.resize((int) (3.2f * sr));
		time = -1.f;
	}
	void clear() {
		l.clear();
		r.clear();
		lp[0] = lp[1] = hp[0] = hp[1] = 0.f;
	}
	void process(float mix, float fb, float seconds, float& L, float& R) {
		float target = clampf(seconds, 0.01f, 3.f) * fs;
		if (time < 0.f)
			time = target;
		time += (target - time) * (1.f - std::exp(-1.f / (0.08f * fs)));
		float yl = l.read(time), yr = r.read(time);
		float in[2] = {yr * fb + 0.5f * (L + R) * 0.7f, yl * fb};
		float a = 1.f - std::exp(-2.f * PI * 4800.f / fs), h = 2.f * PI * 140.f / fs;
		for (int i = 0; i < 2; i++) {
			lp[i] += (in[i] - lp[i]) * a;
			hp[i] += (lp[i] - hp[i]) * h;
			in[i] = sat(lp[i] - hp[i]);
		}
		l.push(in[0]);
		r.push(in[1]);
		L += yl * mix;
		R += yr * mix;
	}
};

// ---------------------------------------------------------------- octave-up shifter (two crossfaded moving taps)
struct Shifter {
	Ring buf;
	float ph = 0.f, fs = 48000.f, win = 0.06f;
	void setSampleRate(float sr) {
		fs = sr;
		buf.resize((int) (0.2f * sr));
	}
	/** ratio 2: an octave up. */
	float process(float x, float ratio = 2.f) {
		buf.push(x);
		float W = win * fs;
		ph = wrap(ph + (ratio - 1.f) / W);  // the delay shrinks as the phase grows
		float d1 = (1.f - ph) * W + 2.f, d2 = (1.f - wrap(ph + 0.5f)) * W + 2.f;
		float g1 = sin2pi(0.5f * ph), g2 = sin2pi(0.5f * wrap(ph + 0.5f));
		return buf.read(d1) * g1 * g1 + buf.read(d2) * g2 * g2;
	}
};

// ---------------------------------------------------------------- reverb: 8 delay lines, a Hadamard mix, damping
enum VerbType { V_NEBULA, V_HALL, V_VOID, NUM_VERBS };
static const char* const VERB_NAMES[NUM_VERBS] = {"NEBULA", "HALL", "VOID"};
static const char* const VERB_INFO[NUM_VERBS] = {"SHIMMER CLOUD", "BRIGHT ROOM", "ENDLESS DARK"};

struct Verb {
	static const int N = 8;
	Ring line[N];
	Ring ap[4];
	float base[N] = {0.0437f, 0.0511f, 0.0593f, 0.0671f, 0.0743f, 0.0839f, 0.0917f, 0.1013f};
	float apLen[4] = {0.0047f, 0.0063f, 0.0083f, 0.0109f};
	float lp[N] = {}, modPh = 0.f, fs = 48000.f;
	Shifter shimmer[2];
	float shimFb[2] = {};
	float dcx[2] = {}, dcy[2] = {};
	void setSampleRate(float sr) {
		fs = sr;
		for (int i = 0; i < N; i++)
			line[i].resize((int) (base[i] * 2.4f * sr) + 64);
		for (int i = 0; i < 4; i++)
			ap[i].resize((int) (apLen[i] * sr) + 8);
		for (int c = 0; c < 2; c++)
			shimmer[c].setSampleRate(sr);
	}
	void clear() {
		for (int i = 0; i < N; i++)
			line[i].clear(), lp[i] = 0.f;
		for (int i = 0; i < 4; i++)
			ap[i].clear();
		shimFb[0] = shimFb[1] = 0.f;
	}
	float scale = 1.f, g[N] = {}, damp = 0.5f;
	int type = V_HALL;
	/** size 0..1 (room), decay 0..1 (tail), tone 0..1 (dark .. bright), t: VerbType. Once per block. */
	void set(int t, float size, float decay, float tone) {
		type = t;
		size = clampf(size, 0.f, 1.f), decay = clampf(decay, 0.f, 1.f), tone = clampf(tone, 0.f, 1.f);
		scale = type == V_VOID ? 1.2f + 1.2f * size : (type == V_HALL ? 0.6f + 1.2f * size : 0.8f + 1.4f * size);
		float rt = type == V_VOID ? 3.f + 57.f * decay * decay : 0.4f + 14.f * decay * decay;  // seconds (RT60)
		float cut = type == V_VOID ? 600.f + 5000.f * tone : 1500.f + 14000.f * tone;
		damp = 1.f - std::exp(-2.f * PI * cut / fs);
		for (int i = 0; i < N; i++)
			g[i] = std::pow(10.f, -3.f * base[i] * scale / rt);
	}
	/** Returns the wet signal (set() before). */
	void process(float inL, float inR, float& outL, float& outR) {
		// input diffusion (series allpasses)
		float x = 0.5f * (inL + inR) + (type == V_NEBULA ? 0.35f * (shimFb[0] + shimFb[1]) : 0.f);
		for (int i = 0; i < 4; i++) {
			float d = apLen[i] * fs;
			float y = ap[i].read(d);
			float v = x + 0.62f * y;
			ap[i].push(v);
			x = y - 0.62f * v;
		}
		modPh = wrap(modPh + (type == V_NEBULA ? 0.6f : 0.25f) / fs);
		float o[N];
		for (int i = 0; i < N; i++) {
			float d = base[i] * scale * fs;
			if (i < 4)
				d += (type == V_HALL ? 6.f : 18.f) * sin2pi(modPh + 0.25f * i) * fs / 48000.f;
			float y = line[i].read(d);
			lp[i] += (y - lp[i]) * damp;
			o[i] = lp[i] * g[i];
		}
		// Hadamard 8x8 (fast, normalised)
		float h[N];
		for (int i = 0; i < N; i++)
			h[i] = o[i];
		for (int len = 1; len < N; len <<= 1)
			for (int i = 0; i < N; i += 2 * len)
				for (int j = i; j < i + len; j++) {
					float a = h[j], b = h[j + len];
					h[j] = a + b, h[j + len] = a - b;
				}
		const float norm = 0.35355339f;  // 1/sqrt(8)
		for (int i = 0; i < N; i++)
			line[i].push(h[i] * norm + x * 0.5f);
		float l = o[0] + o[2] + o[4] + o[6], r = o[1] + o[3] + o[5] + o[7];
		l *= 0.6f, r *= 0.6f;
		if (type == V_NEBULA) {  // the shimmer: an octave up back into the tank
			shimFb[0] = 0.4f * shimmer[0].process(l);
			shimFb[1] = 0.4f * shimmer[1].process(r);
		}
		float io[2] = {l, r};
		for (int c = 0; c < 2; c++) {
			float y = io[c] - dcx[c] + 0.9995f * dcy[c];
			dcx[c] = io[c], dcy[c] = y;
			io[c] = y;
		}
		outL = io[0], outR = io[1];
	}
};

/** C4 (0 V) based note name of a V/OCT voltage. */
inline void noteName(float voct, char* out) {
	static const char* const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	int semis = (int) std::lround(voct * 12.f) + 48;
	if (semis < 0)
		semis = 0;
	int o = semis / 12, n = semis % 12;
	std::snprintf(out, 8, "%s%d", N[n], o);
}

}  // namespace neon
