// SPDX-License-Identifier: GPL-3.0-only
// WELLE – the sound engine: two wavetable oscillators per voice (TABLE, POSITION, a WARP of the phase or the wave),
// a sub, up to 7 unison copies spread in pitch and stereo, a drive into GENRA's filters (GENRA MONO's eight models),
// an amp and a filter envelope, MOTION (the position wanders in time) and the bus: chorus, ping-pong delay, reverb.
// Up to 8 voices (poly: one per channel), mono and legato with glide. Plain C++ without Rack (test/neon_test.cpp).
#pragma once
#include "../neon/Wavetable.hpp"
#include "../../genra/mono/MonoDsp.hpp"
#include "../../genra/OutLimit.hpp"

namespace welle {

using namespace neon;

enum Warp { W_BEND, W_SYNC, W_PWM, W_MIRROR, W_FOLD, W_CRUSH, NUM_WARPS };
static const char* const WARP_NAMES[NUM_WARPS] = {"BEND", "SYNC", "PWM", "MIRROR", "FOLD", "CRUSH"};
static const char* const WARP_INFO[NUM_WARPS] = {"PHASE BEND", "HARD SYNC", "SQUEEZE", "MIRROR", "WAVE FOLD", "BITS"};
enum VoiceMode { MODE_POLY, MODE_MONO, MODE_LEGATO, NUM_MODES };
static const char* const MODE_NAMES[NUM_MODES] = {"POLY", "MONO", "LEGATO"};
enum DelayDiv { D_16, D_8, D_8D, D_4, D_4D, NUM_DDIVS };
static const float DDIV_BEATS[NUM_DDIVS] = {0.25f, 0.5f, 0.75f, 1.f, 1.5f};
static const char* const DDIV_NAMES[NUM_DDIVS] = {"1/16", "1/8", "1/8 dotted", "1/4", "1/4 dotted"};

static const int MAXV = 8;   // voices
static const int MAXU = 7;   // unison copies
static const int MAXCH = 16;

inline float glideSeconds(float k) { return 0.6f * k * k; }

struct Params {
	float posA = 0.f, posB = 0.f;  // 0..1, CV included
	int warpModeA = W_BEND, warpModeB = W_BEND;
	float warpA = 0.f, warpB = 0.f;
	float octA = 0.f, tuneB = 0.f;  // octaves, semitones
	int ftype = gmono::LADDER24;
	float cutoff = 0.7f, cutCv = 0.f, res = 0.1f, fenv = 0.3f, drive = 0.f, key = 0.5f, fatk = 0.f, fdec = 0.4f;
	float scan = 0.f, penv = 0.f;
	int rate = 6;
	float chorus = 0.f, delay = 0.f, verb = 0.f;
	float atk = 0.f, dec = 0.5f, sus = 1.f, rel = 0.4f;
	float mix = 0.f, sub = 0.f, spread = 0.3f;
	int unison = 1;
	int mode = MODE_POLY;
	float glide = 0.2f, accent = 0.5f;
	int ddiv = D_8D;
	float outGain = 1.f;
	bool freeze = false;
};

struct Channel {
	float voct = 0.f, vel = 0.8f;
	bool gate = false, acc = false, slide = false;
};

struct Voice {
	bool active = false, gate = false, acc = false, slideOn = false;
	float pitch = 0.f, target = 0.f, vel = 0.8f;
	gmono::Env amp, fe;
	gmono::Filter flt[2];
	float gL = 0.f, gR = 0.f;  // filter coefficients, kept a few samples
	float ph[2][MAXU] = {}, subPh = 0.f;
	float pos[2] = {}, posHeld[2] = {};
	float ampNow = 0.f;
	int ctl = 0;
	void start(float note, float v, bool a, bool glideIn, bool retrig, uint32_t seed) {
		target = note;
		if (!glideIn || !active)
			pitch = note;
		vel = v, acc = a;
		if (!active || retrig) {
			// fresh phases for the unison copies (spread out, so the stack is not phase-locked)
			Rng r(seed);
			for (int o = 0; o < 2; o++)
				for (int u = 0; u < MAXU; u++)
					ph[o][u] = u == 0 ? 0.f : r.uni();
		}
		active = true;
	}
};

struct Engine {
	float fs = 48000.f, dt = 1.f / 48000.f;
	Voice v[MAXV];
	const Table* tab[2] = {nullptr, nullptr};
	// mono / legato
	int order[MAXCH] = {};
	int held = 0;
	bool lastGate[MAXCH] = {};
	int monoCh = -1;
	// motion
	float motionPh = 0.f, motion = 0.f;
	// bus
	Chorus chorus;
	PingPong delay;
	Verb verb;
	uint32_t seed = 1;
	int ctl = 0;
	// for the display
	float lastNote = 0.f, showPos[2] = {}, showEnv = 0.f;
	int newest = 0;

	Engine() { setSampleRate(48000.f); }
	void setSampleRate(float sr) {
		fs = sr, dt = 1.f / sr;
		chorus.setSampleRate(sr);
		delay.setSampleRate(sr);
		verb.setSampleRate(sr);
		verb.set(V_HALL, 0.7f, 0.5f, 0.6f);
		for (auto& x : v)
			x.flt[0].reset(), x.flt[1].reset();
	}
	void allOff() {
		for (auto& x : v) {
			x.active = x.gate = false;
			x.amp.v = x.fe.v = 0.f;
			x.amp.stage = x.fe.stage = 0;
			x.flt[0].reset(), x.flt[1].reset();
		}
		held = 0;
		monoCh = -1;
		for (bool& g : lastGate)
			g = false;
		delay.clear();
		verb.clear();
	}
	void resetMotion() { motionPh = 0.f; }
	/** While the output is muted for a preset change: filters and effect tails start clean (the notes go on). */
	void softReset() {
		for (auto& x : v) {
			x.flt[0].reset(), x.flt[1].reset();
			// held notes start again with the new sound (its attack, its level), as if struck anew
			x.amp.v = x.fe.v = 0.f;
			x.amp.stage = x.fe.stage = x.gate ? 1 : 0;
			if (!x.gate)
				x.active = false;
		}
		delay.clear();
		verb.clear();
	}

	/** The oscillator: one sample of table t at frame f, phase p, increment inc, warp mode/amount. */
	static float osc(const Table* t, float f, float p, float inc, int mode, float w) {
		int lvl;
		switch (mode) {
			case W_BEND: {
				float k = 0.5f - 0.46f * w;
				p = p < k ? 0.5f * p / k : 0.5f + 0.5f * (p - k) / (1.f - k);
				lvl = Table::level(inc * (1.f + 2.f * w));
				return t->read(lvl, f, p);
			}
			case W_SYNC: {
				float r = 1.f + 7.f * w;
				lvl = Table::level(inc * r);
				return t->read(lvl, f, wrap(p * r));
			}
			case W_PWM: {
				float r = 1.f + 5.f * w;
				lvl = Table::level(inc * r);
				float q = p * r;
				return q < 1.f ? t->read(lvl, f, q) : 0.f;
			}
			case W_MIRROR: {
				float m = 1.f - std::fabs(2.f * p - 1.f);
				lvl = Table::level(inc * (1.f + w));
				return t->read(lvl, f, p + (m - p) * w);
			}
			case W_FOLD: {
				lvl = Table::level(inc * (1.f + 3.f * w));
				return fold(t->read(lvl, f, p) * (1.f + 4.f * w));
			}
			case W_CRUSH: default: {
				lvl = Table::level(inc);
				float y = t->read(lvl, f, w > 0.f ? std::floor(p * (2048.f - 2040.f * w)) / (2048.f - 2040.f * w) : p);
				float q = std::pow(2.f, 9.f - 7.5f * w);
				return w > 0.f ? std::round(y * q) / q : y;
			}
		}
	}

	/** One stereo sample. ch: n channels; slideIn: SLIDE is patched; bpm: the tempo. env: newest voice's envelope,
	 * motionOut: MOTION −1..1. */
	void process(const Params& p, const Channel* ch, int n, bool slideIn, float bpm, float fm, float& L, float& R,
	             float& env, float& motionOut) {
		n = std::min(n, MAXCH);
		// ---- MOTION (frozen with FREEZE)
		if (!p.freeze) {
			float beats = RATE_BEATS[clamp(p.rate, 0, NUM_RATES - 1)];
			motionPh = wrap(motionPh + bpm / 60.f / beats * dt);
		}
		motion = 1.f - 4.f * std::fabs(motionPh - 0.5f);  // triangle −1..1
		motionOut = motion;
		// ---- notes
		if (p.mode == MODE_POLY) {
			for (int c = 0; c < std::min(n, MAXV); c++) {
				const Channel& x = ch[c];
				Voice& vo = v[c];
				if (x.gate && !lastGate[c]) {
					bool gl = slideIn ? vo.slideOn : false;
					vo.start(x.voct, x.vel, x.acc, gl, true, ++seed);
					newest = c;
				}
				if (x.gate) {
					vo.target = x.voct;
					vo.slideOn = x.slide;
					vo.acc = x.acc;
				}
				vo.gate = x.gate;
				lastGate[c] = x.gate;
			}
			for (int c = n; c < MAXV; c++)
				v[c].gate = false, lastGate[c] = false;
		} else {
			// mono / legato: the last note pressed wins, the one before comes back when it is let go
			for (int c = 0; c < n; c++) {
				bool g = ch[c].gate;
				if (g && !lastGate[c]) {
					for (int i = 0; i < held; i++)
						if (order[i] == c) {
							for (int j = i; j < held - 1; j++)
								order[j] = order[j + 1];
							held--;
							break;
						}
					if (held < MAXCH)
						order[held++] = c;
				} else if (!g && lastGate[c]) {
					for (int i = 0; i < held; i++)
						if (order[i] == c) {
							for (int j = i; j < held - 1; j++)
								order[j] = order[j + 1];
							held--;
							break;
						}
				}
				lastGate[c] = g;
			}
			for (int c = n; c < MAXCH; c++)
				lastGate[c] = false;
			Voice& vo = v[0];
			int want = held > 0 ? order[held - 1] : -1;
			if (want >= 0) {
				const Channel& x = ch[want];
				bool changed = want != monoCh || !vo.gate;
				if (changed) {
					bool overlap = vo.gate;  // a note was still held
					bool gl = slideIn ? vo.slideOn : overlap;
					bool retrig = !(p.mode == MODE_LEGATO && overlap);
					if (retrig)
						vo.amp.gate = false, vo.fe.gate = false;  // the envelopes start again
					vo.start(x.voct, x.vel, x.acc, gl, !overlap, ++seed);
					newest = 0;
				}
				vo.target = x.voct;
				vo.slideOn = x.slide;
				vo.acc = x.acc;
				vo.gate = true;
				monoCh = want;
			} else {
				vo.gate = false;
				monoCh = -1;
			}
			for (int i = 1; i < MAXV; i++)
				v[i].gate = false;
		}

		// ---- the voices
		const Table* tA = tab[0];
		const Table* tB = tab[1];
		float sumL = 0.f, sumR = 0.f;
		int nu = clamp(p.unison, 1, MAXU);
		float uNorm = 1.f / std::sqrt((float) nu);
		float glideK = 1.f - std::exp(-dt / std::max(0.002f, glideSeconds(p.glide) * 0.35f));
		float aT = envTime(p.atk), dT = envTime(p.dec), rT = envTime(p.rel);
		float faT = envTime(p.fatk), fdT = envTime(p.fdec);
		float drive = 1.f + 7.f * p.drive * p.drive;
		bool doCtl = --ctl <= 0;
		if (doCtl)
			ctl = 4;
		for (int i = 0; i < MAXV; i++) {
			Voice& vo = v[i];
			if (!vo.active)
				continue;
			vo.pitch += (vo.target - vo.pitch) * glideK;
			float e = vo.amp.process(vo.gate, aT, dT, p.sus, rT, dt);
			float fe = vo.fe.process(vo.gate, faT, fdT, 0.f, fdT, dt);
			vo.ampNow = e;
			if (!vo.gate && vo.amp.stage == 0) {
				vo.active = false;
				continue;
			}
			float accK = vo.acc ? p.accent : 0.f;
			// positions (FREEZE holds them)
			for (int o = 0; o < 2; o++) {
				float base = o == 0 ? p.posA : p.posB;
				float pos = clampf(base + p.penv * fe + p.scan * 0.5f * motion, 0.f, 1.f);
				if (!p.freeze)
					vo.posHeld[o] = pos;
				vo.pos[o] = vo.posHeld[o];
			}
			float sL = 0.f, sR = 0.f;
			float nA = vo.pitch + p.octA, nB = vo.pitch + p.tuneB / 12.f;
			float fA = C4 * std::pow(2.f, nA) / (tA ? tA->ratio : 1.f);
			float fB = C4 * std::pow(2.f, nB) / (tB ? tB->ratio : 1.f);
			float frA = tA ? vo.pos[0] * (tA->frames - 1) : 0.f, frB = tB ? vo.pos[1] * (tB->frames - 1) : 0.f;
			float pmFm = fm * 0.1f;  // FM: ±5 V = ±half a cycle
			for (int u = 0; u < nu; u++) {
				float off = nu > 1 ? ((float) u / (nu - 1) - 0.5f) * 2.f : 0.f;  // −1..1
				float det = std::pow(2.f, off * p.spread * 0.35f / 12.f);
				float pan = 0.5f + 0.5f * off * clampf(p.spread * 1.4f, 0.f, 1.f);
				float y = 0.f;
				if (tA && p.mix < 0.999f) {
					float inc = std::min(fA * det * dt, 0.49f);
					vo.ph[0][u] = wrap(vo.ph[0][u] + inc);
					y += (1.f - p.mix) * osc(tA, frA, wrap(vo.ph[0][u] + pmFm), inc, p.warpModeA, p.warpA);
				}
				if (tB && p.mix > 0.001f) {
					float inc = std::min(fB * det * dt, 0.49f);
					vo.ph[1][u] = wrap(vo.ph[1][u] + inc);
					y += p.mix * osc(tB, frB, wrap(vo.ph[1][u] + pmFm), inc, p.warpModeB, p.warpB);
				}
				sL += y * (1.f - pan);
				sR += y * pan;
			}
			sL *= uNorm * 1.41f, sR *= uNorm * 1.41f;
			if (nu == 1)
				sL *= 0.7071f, sR *= 0.7071f;
			if (p.sub > 0.001f) {
				vo.subPh = wrap(vo.subPh + std::min(0.5f * C4 * std::pow(2.f, nA) * dt, 0.49f));
				float s = sin2pi(vo.subPh) * p.sub * 0.8f;
				sL += s, sR += s;
			}
			// drive into the filter
			sL = gmono::sat(sL * drive) / std::sqrt(drive) * 1.6f;
			sR = gmono::sat(sR * drive) / std::sqrt(drive) * 1.6f;
			if (doCtl || vo.ctl == 0) {
				vo.ctl = 1;
				float oct = 10.f * clampf(p.cutoff, 0.f, 1.f) + p.cutCv + p.key * vo.pitch + p.fenv * 6.f * fe
				            + accK * 1.5f * fe + (vo.vel - 0.8f) * 1.2f;
				float fc = 20.f * std::pow(2.f, oct);
				float g = gmono::Filter::coef(fc, fs);
				vo.gL = vo.gR = g;
			}
			float res = clampf(p.res + 0.15f * accK, 0.f, 1.f);
			float yL = vo.flt[0].processG(sL, vo.gL, res, p.ftype);
			float yR = vo.flt[1].processG(sR, vo.gR, res, p.ftype);
			float lev = e * (0.45f + 0.55f * vo.vel) * (1.f + 0.4f * accK);
			sumL += yL * lev;
			sumR += yR * lev;
			if (i == newest) {
				lastNote = vo.pitch;
				showPos[0] = vo.pos[0], showPos[1] = vo.pos[1];
			}
		}
		showEnv = v[newest].ampNow;
		env = showEnv;
		// ---- the bus
		float l = sumL, r = sumR;
		chorus.process(p.chorus, l, r);
		{
			float secs = 60.f / std::max(bpm, 20.f) * DDIV_BEATS[clamp(p.ddiv, 0, NUM_DDIVS - 1)];
			delay.process(p.delay * 0.8f, 0.45f, secs, l, r);
		}
		float wl, wr;
		verb.process(l, r, wl, wr);
		l = l * (1.f - 0.25f * p.verb) + wl * p.verb * 1.2f;
		r = r * (1.f - 0.25f * p.verb) + wr * p.verb * 1.2f;
		L = genra_out::limit(l * 5.f * p.outGain);
		R = genra_out::limit(r * 5.f * p.outGain);
	}
	template <typename T>
	static T clamp(T x, T a, T b) {
		return std::min(std::max(x, a), b);
	}
};

}  // namespace welle
