// SPDX-License-Identifier: GPL-3.0-only
// PULS – the sound engine: pulsar synthesis (after Curtis Roads). A train of pulses; every pulse is a short burst
// (the pulsaret: a wave at the formant F1, plus F2, under a window) followed by silence. RATE sets how many pulses a
// second: slow it is a rhythm, fast (FREE, NOTE) it becomes a tone whose colour the formants give. Three modes:
// CLOCK (note values, in time), FREE (0.25 Hz .. 2 kHz, the moment rhythm turns into tone), NOTE (the pulse rate follows
// V/OCT: an instrument; the MASK then steps with the clock and cuts the tone). The MASK (16 steps) lets pulses through
// or not; DUTY is how much of the period sounds, SKEW tilts the window. SPREAD throws pulses left and right, JITTER
// shakes their time and formant, TAIL lets them ring out. Ladder filter, envelope. Plain C++ without Rack.
#pragma once
#include "../neon/NeonDsp.hpp"
#include "../neon/Notes.hpp"
#include "../../genra/mono/MonoDsp.hpp"
#include "../../genra/OutLimit.hpp"

namespace puls {

using namespace neon;

enum Mode { M_CLOCK, M_FREE, M_NOTE, NUM_PMODES };
static const char* const PMODE_NAMES[NUM_PMODES] = {"CLOCK", "FREE", "NOTE"};
static const char* const PMODE_INFO[NUM_PMODES] = {"NOTE VALUES, IN TIME", "0.25 HZ .. 2 KHZ", "RATE FOLLOWS V/OCT"};
enum Shape { S_SINE, S_TRI, S_SAW, S_SQUARE, S_BELL, S_NOISE, NUM_SHAPES };
static const char* const SHAPE_NAMES[NUM_SHAPES] = {"SINE", "TRIANGLE", "SAW", "SQUARE", "BELL", "NOISE"};
static const char* const SHAPE_INFO[NUM_SHAPES] = {"PURE", "SOFT", "BRIGHT", "HOLLOW", "METAL", "GRAINS"};
enum Window { W_GAUSS, W_EXPO, W_REXPO, W_HANN, W_RECT, NUM_WINDOWS };
static const char* const WINDOW_NAMES[NUM_WINDOWS] = {"GAUSS", "EXPO", "REV EXPO", "HANN", "RECT"};
static const char* const WINDOW_INFO[NUM_WINDOWS] = {"ROUND", "HIT, DECAY", "SWELL, CUT", "SMOOTH", "HARD"};
enum Pattern { PT_EUCLID, PT_RANDOM, PT_BURST, PT_ALL, PT_EDIT, NUM_PATTERNS };
static const char* const PATTERN_NAMES[NUM_PATTERNS] = {"EUCLID", "RANDOM", "BURST", "ALL", "EDIT"};
static const char* const PATTERN_INFO[NUM_PATTERNS] = {"EVEN HITS", "DICE", "GROUPS", "EVERY PULSE", "YOUR STEPS"};

// CLOCK: the note values, slowest first (in beats)
static const int NUM_DIVS = 10;
static const float DIV_BEATS[NUM_DIVS] = {4.f, 2.f, 1.f, 0.5f, 1.f / 3.f, 0.25f, 1.f / 6.f, 0.125f, 1.f / 12.f, 0.0625f};
static const char* const DIV_NAMES[NUM_DIVS] = {"1/1", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64"};

static const int STEPS = 16;
static const int MAXV = 4;

inline float freeHz(float k) { return 0.25f * std::pow(2.f, 13.f * clampf(k, 0.f, 1.f)); }  // 0.25 Hz .. 2 kHz
inline int divIndex(float k) { return (int) std::round(clampf(k, 0.f, 1.f) * (NUM_DIVS - 1)); }
inline int noteOct(float k) { return (int) std::round((clampf(k, 0.f, 1.f) - 0.5f) * 6.f); }  // −3 .. +3
inline float formantHz(float k) { return 60.f * std::pow(2.f, 7.f * clampf(k, 0.f, 1.f)); }    // 60 Hz .. 7.7 kHz

/** The mask the pattern makes (EDIT: the steps stay as they are). */
inline void makeMask(int pat, float dens, float rot, uint32_t seed, bool* m) {
	int hits = (int) std::round(clampf(dens, 0.f, 1.f) * STEPS);
	int r = (int) std::round(clampf(rot, 0.f, 1.f) * (STEPS - 1));
	Rng g(seed * 2654435761u + 7u);
	for (int i = 0; i < STEPS; i++) {
		bool on = false;
		switch (pat) {
			case PT_EUCLID: on = hits > 0 && ((i * hits) % STEPS) < hits; break;
			case PT_RANDOM: on = g.uni() < dens; break;
			case PT_BURST: {
				int len = 2 + (int) (dens * 5.f);
				int grp = (i / 4 + (int) (seed % 3)) % 2 == 0;
				on = grp && (i % 4) < std::min(len, 4);
				break;
			}
			case PT_ALL: on = true; break;
			default: on = m[i]; break;
		}
		if (pat != PT_EDIT)
			m[(i + r) % STEPS] = on;
	}
}

struct Params {
	int pmode = M_CLOCK;
	float rate = 0.55f, rateCv = 0.f;  // CLOCK: note value; FREE: Hz; NOTE: octave (CV in volts: 1 V/oct in FREE)
	float f1 = 0.45f, f2 = 0.65f;
	int shape = S_SINE, window = W_GAUSS;
	float duty = 0.5f, skew = 0.5f;
	int pattern = PT_EUCLID;
	float dens = 0.6f, rot = 0.f, prob = 1.f;
	float cutoff = 0.8f, cutCv = 0.f, res = 0.1f, drive = 0.f;
	float spread = 0.3f, jitter = 0.f, tail = 0.2f;
	float atk = 0.f, dec = 0.5f, sus = 0.8f, rel = 0.3f;
	int vmode = MODE_MONO;
	float glide = 0.1f, accent = 0.5f;
	float outGain = 1.f;
	bool gated = false;  // the envelope follows the gates (GATE patched or followed); else it is open
};

/** One pulse train. */
struct Train {
	float ph = 0.f;          // 0..1 over the period (FREE / NOTE)
	float t = 0.f;           // seconds since the pulse began
	float len = 0.01f;       // the pulsaret's length (s)
	float f1 = 400.f, f2 = 800.f;
	float pan = 0.5f, amp = 0.f;
	bool on = false;         // this pulse plays
	float nsh = 0.f, nph = 0.f;  // NOISE: held value and its clock
	float periodMul = 1.f;
};

struct Voice {
	bool active = false, gate = false, acc = false, slideOn = false;
	float pitch = 0.f, target = 0.f, vel = 0.8f;
	gmono::Env amp;
	Train tr;
	float ampNow = 0.f;
};

struct Engine {
	float fs = 48000.f, dt = 1.f / 48000.f;
	Voice v[MAXV];
	Notes notes;
	Rng rng{0x9a1b};
	bool mask[STEPS] = {};
	int step = 0;          // the mask step now
	long long lastSlot = -1;
	gmono::Filter flt[2];
	float g = 0.f;
	Verb verb;
	int ctl = 0, newest = 0;
	// for the display / outputs
	uint32_t pulseCount = 0;   // every pulse (played or masked)
	bool lastOn = false;
	float trig = 0.f;          // PULSE out timer
	float showRate = 1.f, lastNote = 0.f;
	double beats = 0.0;
	bool side = false;

	Engine() {
		setSampleRate(48000.f);
		makeMask(PT_EUCLID, 0.6f, 0.f, 1, mask);
	}
	void setSampleRate(float sr) {
		fs = sr, dt = 1.f / sr;
		verb.setSampleRate(sr);
		verb.set(V_HALL, 0.35f, 0.3f, 0.6f);
		flt[0].reset(), flt[1].reset();
	}
	void allOff() {
		for (auto& x : v) {
			x.active = x.gate = false;
			x.amp.v = 0.f;
			x.amp.stage = 0;
		}
		notes.reset();
		verb.clear();
		flt[0].reset(), flt[1].reset();
	}

	static float wave(int shape, float p, Train& tr, Rng& r, float f, float dt) {
		switch (shape) {
			case S_TRI: return 1.f - 4.f * std::fabs(wrap(p + 0.25f) - 0.5f);
			case S_SAW: return 1.f - 2.f * wrap(p);
			case S_SQUARE: return wrap(p) < 0.5f ? 0.8f : -0.8f;
			case S_BELL: return 0.7f * sin2pi(p) + 0.4f * sin2pi(p * 2.76f) + 0.2f * sin2pi(p * 5.4f);
			case S_NOISE:
				tr.nph += f * 2.f * dt;
				if (tr.nph >= 1.f) {
					tr.nph -= std::floor(tr.nph);
					tr.nsh = r.bi();
				}
				return tr.nsh;
			default: return sin2pi(p);
		}
	}
	static float win(int w, float u, float skew) {
		u = clampf(u, 0.f, 1.f);
		// SKEW: the window's middle earlier (0) or later (1)
		float e = std::pow(4.f, (clampf(skew, 0.f, 1.f) - 0.5f) * 2.f);
		u = std::pow(u, e);
		switch (w) {
			case W_EXPO: return std::exp(-5.f * u) * std::min(1.f, u * 200.f);
			case W_REXPO: return std::exp(-5.f * (1.f - u)) * std::min(1.f, (1.f - u) * 200.f);
			case W_HANN: {
				float s = sin2pi(0.5f * u);
				return s * s;
			}
			case W_RECT: return std::min(1.f, std::min(u, 1.f - u) * 120.f);
			default: {
				float d = (u - 0.5f) * 4.f;
				return std::exp(-d * d * 0.5f) * std::min(1.f, std::min(u, 1.f - u) * 60.f);
			}
		}
	}

	/** A new pulse on train tr: decides whether it plays (the mask, PROB), its formants (JITTER), its side (SPREAD). */
	void fire(Train& tr, const Params& p, float period, float tune, bool advanceMask, bool maskOpen) {
		if (advanceMask)
			step = (step + 1) % STEPS;
		bool on = maskOpen && (p.prob >= 0.999f || rng.uni() < p.prob);
		tr.on = on;
		tr.t = 0.f;
		tr.len = clampf(p.duty * period, 0.0004f, 2.f);
		float j = p.jitter;
		float jf = 1.f + j * 0.35f * rng.bi();
		tr.f1 = formantHz(p.f1) * tune * jf;
		tr.f2 = formantHz(p.f2) * tune * (1.f + j * 0.35f * rng.bi());
		tr.periodMul = 1.f + j * 0.45f * rng.bi();
		side = !side;
		tr.pan = 0.5f + (side ? 0.5f : -0.5f) * p.spread * (0.6f + 0.4f * rng.uni());
		tr.amp = 1.f;
		pulseCount++;
		lastOn = on;
		if (on)
			trig = 0.002f;
	}

	/** One sample of a train: the pulsaret while it lasts. */
	float sound(Train& tr, const Params& p, float& pan) {
		pan = tr.pan;
		if (!tr.on || tr.t >= tr.len)
			return 0.f;
		float u = tr.t / tr.len;
		float w = win(p.window, u, p.skew);
		float y = wave(p.shape, tr.f1 * tr.t, tr, rng, tr.f1, dt) + 0.6f * wave(p.shape == S_NOISE ? S_SINE : p.shape, tr.f2 * tr.t, tr, rng, tr.f2, dt);
		return y * w * 0.7f;
	}

	void process(const Params& p, const Channel* ch, int n, bool slideIn, float bpm, double beatCount, bool resetNow,
	             float fm, float& L, float& R, float& env, float& pulseOut) {
		beats = beatCount;
		if (resetNow) {
			step = STEPS - 1;
			lastSlot = -1;
			for (auto& x : v)
				x.tr.ph = 0.f;
		}
		bool noteMode = p.pmode == M_NOTE;
		int vm = noteMode ? p.vmode : (p.vmode == MODE_POLY ? MODE_MONO : p.vmode);  // one train unless NOTE
		notes.step(
		    vm, ch, n, noteMode ? MAXV : 1,
		    [&](int i, int c, bool overlap) {
			    Voice& vo = v[i];
			    bool gl = slideIn ? vo.slideOn : (vm != MODE_POLY && overlap);
			    vo.target = ch[c].voct;
			    if (!gl || !vo.active)
				    vo.pitch = ch[c].voct;
			    vo.vel = ch[c].vel, vo.acc = ch[c].acc;
			    if (!(vm == MODE_LEGATO && overlap))
				    vo.amp.gate = false;
			    if (!vo.active || !(vm == MODE_LEGATO && overlap))
				    vo.tr.ph = 0.999f;  // the first pulse comes at once
			    vo.active = true;
			    newest = i;
		    },
		    [&](int i, int c) {
			    v[i].target = ch[c].voct;
			    v[i].slideOn = ch[c].slide;
			    v[i].acc = ch[c].acc;
			    v[i].gate = true;
		    },
		    [&](int i) { v[i].gate = false; });
		if (!p.gated && !noteMode) {  // nothing plays the envelope: the train runs on its own
			v[0].active = true;
			v[0].gate = true;
		}
		float glideK = 1.f - std::exp(-dt / std::max(0.002f, 0.6f * p.glide * p.glide * 0.35f));
		float aT = envTime(p.atk), dT = envTime(p.dec), rT = envTime(p.rel);
		// the mask step in NOTE mode: 16ths of the clock
		bool maskOpenNote = true;
		if (noteMode) {
			long long slot = (long long) std::floor(beats * 4.0);
			if (slot != lastSlot) {
				lastSlot = slot;
				step = (int) (((slot % STEPS) + STEPS) % STEPS);
				pulseCount++;
				lastOn = mask[step];
				if (mask[step])
					trig = 0.002f;
			}
			maskOpenNote = mask[step];
		}
		float sumL = 0.f, sumR = 0.f;
		for (int i = 0; i < MAXV; i++) {
			Voice& vo = v[i];
			if (!vo.active)
				continue;
			vo.pitch += (vo.target - vo.pitch) * glideK;
			float e = vo.amp.process(vo.gate, aT, dT, p.sus, rT, dt);
			vo.ampNow = e;
			if (!vo.gate && vo.amp.stage == 0) {
				vo.active = false;
				continue;
			}
			Train& tr = vo.tr;
			// the formants follow V/OCT (in NOTE mode half of it, so the vowel stays)
			float tune = std::pow(2.f, (noteMode ? 0.5f : 1.f) * vo.pitch);
			float period;
			if (p.pmode == M_CLOCK) {
				float db = DIV_BEATS[clamp(divIndex(p.rate + p.rateCv * 0.1f), 0, NUM_DIVS - 1)];
				period = db * 60.f / std::max(bpm, 20.f);
				long long slot = (long long) std::floor(beats / db);
				if (slot != lastSlot) {
					lastSlot = slot;
					fire(tr, p, period, tune, true, true);
					tr.on = tr.on && mask[step];
					lastOn = tr.on;
				}
			} else {
				float hz = p.pmode == M_FREE ? freeHz(p.rate) * std::pow(2.f, p.rateCv)
				                             : C4 * std::pow(2.f, vo.pitch + noteOct(p.rate) + p.rateCv);
				hz = clampf(hz, 0.05f, 0.45f * fs);
				period = 1.f / hz;
				tr.ph += hz * dt / tr.periodMul + fm * 0.02f * hz * dt;
				if (tr.ph >= 1.f) {
					tr.ph -= std::floor(tr.ph);
					if (noteMode)
						fire(tr, p, period, tune, false, maskOpenNote);
					else {
						fire(tr, p, period, tune, true, true);
						tr.on = tr.on && mask[step];
						lastOn = tr.on;
					}
				}
				if (noteMode && !maskOpenNote)
					tr.on = false;
			}
			if (i == newest)
				showRate = 1.f / std::max(period, 1e-4f);
			float pan;
			float y = sound(tr, p, pan);
			tr.t += dt;
			float accK = vo.acc ? p.accent : 0.f;
			float lev = e * (0.45f + 0.55f * vo.vel) * (1.f + 0.5f * accK);
			sumL += y * lev * (1.f - pan) * 1.41f;
			sumR += y * lev * pan * 1.41f;
		}
		lastNote = v[newest].pitch;
		env = v[newest].ampNow;
		// ---- filter, drive, tail
		float drive = 1.f + 7.f * p.drive * p.drive;
		if (--ctl <= 0) {
			ctl = 8;
			float oct = 10.f * clampf(p.cutoff, 0.f, 1.f) + p.cutCv + 0.3f * lastNote;
			g = gmono::Filter::coef(20.f * std::pow(2.f, oct), fs);
			verb.set(V_HALL, 0.25f + 0.5f * p.tail, 0.15f + 0.6f * p.tail, 0.6f);
		}
		float l = gmono::sat(sumL * drive) / std::sqrt(drive) * 1.4f;
		float r = gmono::sat(sumR * drive) / std::sqrt(drive) * 1.4f;
		l = flt[0].processG(l, g, p.res, gmono::LADDER24);
		r = flt[1].processG(r, g, p.res, gmono::LADDER24);
		float wl, wr;
		verb.process(l, r, wl, wr);
		l += wl * p.tail * 1.3f;
		r += wr * p.tail * 1.3f;
		if (trig > 0.f)
			trig -= dt;
		pulseOut = trig > 0.f ? 10.f : 0.f;
		L = genra_out::limit(l * 3.f * p.outGain);
		R = genra_out::limit(r * 3.f * p.outGain);
	}
	template <typename T>
	static T clamp(T x, T a, T b) {
		return std::min(std::max(x, a), b);
	}
};

}  // namespace puls
