// SPDX-License-Identifier: GPL-3.0-only
// FLUT – the sound engine: a drone that breathes like the sea. Up to six voices (the clouds) sit on the notes that come
// in (GENRA's chord: every held note; one note: its chord tones are made from it), spread over the octaves (SPRD, NEW
// throws a new voicing), wander against each other (DRIFT) and glide into a new chord (MORPH) instead of jumping.
// The TIDE is a slow cycle (SYNC: 1 .. 128 bars in time; FREE: 2 s .. 10 min): at high tide it is louder, brighter,
// denser and BLOOM opens partials above; DEPTH says how much. TEXTURE: GLASS, REED, CHOIR, BOWED, DUST, with BRIGHT,
// GRIT (crackle) and BREATH (air). SUB: a sine an octave or two below. HAZE: a long reverb; IN L/R go in too.
// DRONE plays all the time, GATE only while a note is held (SWELL / FADE). FREEZE holds tide, drift and chord.
// Plain C++ without Rack (test/neon_test.cpp).
#pragma once
#include "../neon/NeonDsp.hpp"
#include "../neon/Notes.hpp"
#include "../../genra/mono/MonoDsp.hpp"
#include "../../genra/OutLimit.hpp"

namespace flut {

using namespace neon;

enum Texture { X_GLASS, X_REED, X_CHOIR, X_BOWED, X_DUST, NUM_TEXTURES };
static const char* const TEXTURE_NAMES[NUM_TEXTURES] = {"GLASS", "REED", "CHOIR", "BOWED", "DUST"};
static const char* const TEXTURE_INFO[NUM_TEXTURES] = {"CLEAR, BELL-LIKE", "HOLLOW, ODD", "AH, VOWELS", "STRINGS",
                                                       "NOISE BANDS"};
enum TideMode { TM_SYNC, TM_FREE, NUM_TIDEMODES };
static const char* const TIDEMODE_NAMES[NUM_TIDEMODES] = {"SYNC", "FREE"};
static const char* const TIDEMODE_INFO[NUM_TIDEMODES] = {"BARS, IN TIME", "SECONDS / MINUTES"};
enum PlayMode { PM_DRONE, PM_GATE, NUM_PLAYMODES };
static const char* const PLAYMODE_NAMES[NUM_PLAYMODES] = {"DRONE", "GATE"};
static const int NUM_BARS = 8;
static const float TIDE_BARS[NUM_BARS] = {1.f, 2.f, 4.f, 8.f, 16.f, 32.f, 64.f, 128.f};

static const int MAXV = 6;
static const int NP = 10;  // partials

// the partials of each texture (1 = the fundamental)
static const float PARTIALS[NUM_TEXTURES][NP] = {
    {1.f, 0.f, 0.42f, 0.f, 0.24f, 0.08f, 0.14f, 0.f, 0.08f, 0.04f},          // GLASS
    {1.f, 0.05f, 0.55f, 0.04f, 0.36f, 0.03f, 0.25f, 0.02f, 0.16f, 0.02f},    // REED (odd)
    {1.f, 0.55f, 0.75f, 0.85f, 0.45f, 0.25f, 0.16f, 0.12f, 0.08f, 0.04f},    // CHOIR ("ah")
    {1.f, 0.5f, 0.33f, 0.25f, 0.2f, 0.16f, 0.14f, 0.12f, 0.1f, 0.09f},       // BOWED (saw-like)
    {0.5f, 0.15f, 0.1f, 0.05f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},                // DUST (most of it is noise)
};

inline int barsIndex(float k) { return (int) std::round(clampf(k, 0.f, 1.f) * (NUM_BARS - 1)); }
inline float freeSeconds(float k) { return 2.f * std::pow(300.f, clampf(k, 0.f, 1.f)); }  // 2 s .. 10 min
inline float slowTime(float k) { return 0.02f + 30.f * std::pow(clampf(k, 0.f, 1.f), 2.4f); }   // 20 ms .. 30 s
inline float morphTime(float k) { return 0.02f + 20.f * std::pow(clampf(k, 0.f, 1.f), 2.2f); }  // 20 ms .. 20 s

struct Params {
	int voices = 6;
	float oct = 0.f, spread = 0.4f, drift = 0.3f, morph = 0.4f, sub = 0.3f;
	int subOct = 1;  // 1: an octave below, 2: two
	int tideMode = TM_SYNC;
	float time = 0.7f, depth = 0.6f, phase = 0.f, skew = 0.f;
	int texture = X_GLASS;
	float bright = 0.5f, grit = 0.f, breath = 0.1f;
	float bloom = 0.3f, width = 0.6f, haze = 0.5f;
	float swell = 0.4f, fade = 0.5f, level = 0.8f;
	int playMode = PM_DRONE;
	float outGain = 1.f;
	bool freeze = false;
};

/** Slow smooth noise (a drifting value −1..1). */
struct Wander {
	float a = 0.f, b = 0.f, t = 0.f, rate = 0.1f;
	float step(float dt, Rng& r) {
		t += rate * dt;
		if (t >= 1.f) {
			t -= 1.f;
			a = b;
			b = r.bi();
		}
		float s = t * t * (3.f - 2.f * t);
		return a + (b - a) * s;
	}
};

struct Voice {
	float pitch = -1.f, target = -1.f;  // V/OCT
	float ph[2][NP] = {}, bph[2] = {};
	Wander wd, wa;
	float lev = 0.f, levT = 0.f;  // how much the voice sounds (for the display too)
	gmono::Filter bp;             // DUST, BREATH: the noise band
	float pan = 0.5f, out = 0.f, f0 = 130.f, bpG = 0.1f;
	int octOff = 0;
};

struct Engine {
	float fs = 48000.f, dt = 1.f / 48000.f;
	Voice v[MAXV];
	Rng rng{0xf107u};
	Rng crackRng{0x5eed1234u};
	float dcx[3] = {}, dcy[3] = {};  // DC blockers on L, R, SUB (2.1.2)
	float wts[NP] = {};
	uint32_t seed = 1;
	double tidePh = 0.0;
	float tide = 0.5f;
	float env = 0.f;
	float chord[MAXV] = {-1.f, -1.f + 7.f / 12.f, -1.f + 15.f / 12.f, -1.f + 19.f / 12.f, -1.f + 26.f / 12.f, 0.f};
	int nChord = 1;
	float notesIn[16] = {};
	float subPh = 0.f, subOut = 0.f, subLow = -1.f;
	float crackle = 0.f;
	Verb verb;
	gmono::Filter tone[2];
	float toneG = 0.f;
	int ctl = 0;
	float lastNoteShow = -1.f;

	Engine() {
		setSampleRate(48000.f);
		voicing(1);
	}
	void setSampleRate(float sr) {
		fs = sr, dt = 1.f / sr;
		verb.setSampleRate(sr);
		verb.set(V_NEBULA, 0.9f, 0.8f, 0.5f);
		for (auto& x : v)
			x.bp.reset();
		tone[0].reset(), tone[1].reset();
	}
	void allOff() {
		verb.clear();
		env = 0.f;
	}
	/** NEW: a new voicing (which octave each voice sits in) and new wander seeds. */
	void voicing(uint32_t s) {
		seed = s;
		Rng r(s * 2654435761u + 1u);
		for (int i = 0; i < MAXV; i++) {
			float u = r.uni();
			v[i].octOff = u < 0.2f ? -1 : (u < 0.65f ? 0 : (u < 0.9f ? 1 : 2));
			v[i].wd.rate = 0.03f + 0.17f * r.uni();
			v[i].wa.rate = 0.02f + 0.1f * r.uni();
			v[i].wd.b = r.bi();
			v[i].wa.b = r.bi();
			for (int k = 0; k < NP; k++)
				v[i].ph[0][k] = r.uni(), v[i].ph[1][k] = r.uni();
		}
	}

	/** The chord from the notes that come in: every held note; a single note makes its own (root, fifth, tenth …). */
	void takeChord(const Channel* ch, int n) {
		int k = 0;
		float in[16];
		for (int c = 0; c < std::min(n, 16); c++)
			if (ch[c].gate)
				in[k++] = ch[c].voct;
		if (k == 0)
			return;  // nothing held: the last chord stays (a drone keeps going)
		std::sort(in, in + k);
		if (k == 1) {
			static const float ADD[MAXV] = {0.f, 7.f, 15.f, 19.f, 26.f, 12.f};  // root, fifth, minor tenth, …
			for (int i = 0; i < MAXV; i++)
				chord[i] = in[0] + ADD[i] / 12.f;
			nChord = MAXV;
		} else {
			for (int i = 0; i < k && i < MAXV; i++)
				chord[i] = in[i];
			nChord = std::min(k, MAXV);
		}
	}

	void process(const Params& p, const Channel* ch, int n, bool anyGate, float bpm, bool resetNow, float inL, float inR,
	             float& L, float& R, float& tideOut, float& subV, float* polyOut) {
		// ---- chord, tide (FREEZE holds both)
		if (!p.freeze)
			takeChord(ch, n);
		if (resetNow)
			tidePh = 0.0;
		float period = p.tideMode == TM_SYNC ? TIDE_BARS[clamp(barsIndex(p.time), 0, NUM_BARS - 1)] * 4.f * 60.f / std::max(bpm, 20.f)
		                                    : freeSeconds(p.time);
		if (!p.freeze) {
			tidePh += (double) (dt / period);
			tidePh -= std::floor(tidePh);
		}
		float ph = wrap((float) tidePh + p.phase);
		float s = 0.5f + 0.45f * clampf(p.skew, -1.f, 1.f);  // where high tide is
		float q = ph < s ? 0.5f * ph / s : 0.5f + 0.5f * (ph - s) / (1.f - s);
		tide = 0.5f - 0.5f * std::cos(2.f * PI * q);
		tideOut = 10.f * tide;
		float depth = clampf(p.depth, 0.f, 1.f);
		float tLevel = 1.f - depth * 0.75f * (1.f - tide);
		// ---- the envelope (DRONE: open; GATE: while a note is held)
		bool open = p.playMode == PM_DRONE || anyGate;
		float et = open ? slowTime(p.swell) : slowTime(p.fade);
		env += ((open ? 1.f : 0.f) - env) * (1.f - std::exp(-dt / et));
		// ---- the voices
		int nv = clamp(p.voices, 1, MAXV);
		float mk = 1.f - std::exp(-dt / morphTime(p.morph));
		float bright = clampf(p.bright + depth * 0.35f * (tide - 0.5f), 0.f, 1.f);
		float tilt = (bright - 0.5f) * 2.4f;
		int tx = clamp(p.texture, 0, NUM_TEXTURES - 1);
		bool doCtl = --ctl <= 0;
		if (doCtl) {
			ctl = 16;
			for (int k = 0; k < NP; k++)
				wts[k] = PARTIALS[tx][k] * std::pow((float) (k + 1), tilt * 0.6f - 0.3f);
		}
		float sumL = 0.f, sumR = 0.f;
		float lowest = 99.f;
		for (int i = 0; i < MAXV; i++) {
			Voice& vo = v[i];
			int ci = i % std::max(1, nChord);
			int stack = nChord > 0 ? i / std::max(1, nChord) : 0;
			int oo = (int) std::round(p.spread * vo.octOff * 1.5f) + (p.spread > 0.25f ? 0 : 0) + stack;
			vo.target = chord[ci] + p.oct + (float) oo;
			vo.pitch += (vo.target - vo.pitch) * mk;
			float wd = p.freeze ? vo.wd.a : vo.wd.step(dt, rng);
			float wa = p.freeze ? vo.wa.a : vo.wa.step(dt, rng);
			float cents = p.drift * 30.f * wd;
			// the upper voices fade at low tide (denser at high tide)
			float dens = 1.f - depth * ((float) i / MAXV) * (1.f - tide);
			vo.levT = i < nv ? dens * (1.f + 0.45f * p.drift * wa) : 0.f;
			vo.lev += (vo.levT - vo.lev) * (1.f - std::exp(-dt / 0.6f));
			if (vo.lev < 1e-4f && vo.levT <= 0.f) {
				vo.out = 0.f;
				continue;
			}
			lowest = std::min(lowest, vo.pitch);
			if (doCtl) {
				vo.f0 = C4 * std::pow(2.f, vo.pitch + cents / 1200.f);
				vo.bpG = gmono::Filter::coef(std::min(vo.f0 * (tx == X_DUST ? 1.f : 2.f), 0.4f * fs), fs);
			}
			float f0 = vo.f0;
			float y = 0.f;
			for (int c = 0; c < 2; c++) {  // two copies, a few cents apart
				float fc = f0 * (c ? 1.0035f + p.drift * 0.004f : 1.f);
				vo.ph[c][0] = wrap(vo.ph[c][0] + fc * dt);
				// the partials from one phase: sin(kθ) by the angle sum, each partial a turn further
				float s1 = sin2pi(vo.ph[c][0]), c1 = sin2pi(vo.ph[c][0] + 0.25f);
				float sk = s1, ck = c1;
				int kmax = std::min(NP, (int) (0.45f * fs / std::max(fc, 1.f)));
				for (int k = 0; k < kmax; k++) {
					y += wts[k] * sk;
					float ns = sk * c1 + ck * s1;
					ck = ck * c1 - sk * s1;
					sk = ns;
				}
				// BLOOM: an octave and a twelfth above, opening at high tide
				float bl = p.bloom * (0.25f + 0.75f * tide);
				if (bl > 0.001f) {
					vo.bph[c] = wrap(vo.bph[c] + fc * (c ? 3.f : 2.f) * dt);
					y += bl * 0.35f * sin2pi(vo.bph[c]);
				}
			}
			y *= 0.32f;
			// DUST and BREATH: a band of noise at the voice's pitch
			float nz = (tx == X_DUST ? 1.f : 0.f) + p.breath * 0.5f;
			if (nz > 0.001f) {
				y += nz * 2.2f * vo.bp.processG(rng.bi(), vo.bpG, 0.75f, gmono::SVF_BP);
			}
			y *= vo.lev;
			vo.out = y;
			float off = nv > 1 ? ((float) i / (nv - 1) - 0.5f) * 2.f : 0.f;
			vo.pan = 0.5f + 0.5f * p.width * off * (i % 2 ? -1.f : 1.f);
			sumL += y * std::sqrt(1.f - vo.pan);
			sumR += y * std::sqrt(vo.pan);
			if (polyOut)
				polyOut[i] = clampf(y * 3.f, -10.f, 10.f);
		}
		for (int i = 0; i < MAXV && polyOut; i++)
			if (v[i].lev < 1e-4f)
				polyOut[i] = 0.f;
		lastNoteShow = lowest < 98.f ? lowest : lastNoteShow;
		// ---- SUB: under the lowest voice
		float sub = 0.f;
		if (lowest < 98.f) {
			subLow += (lowest - (float) clamp(p.subOct, 1, 2) - subLow) * mk;
			subPh = wrap(subPh + C4 * std::pow(2.f, subLow) * dt);
			sub = sin2pi(subPh) * p.sub * 0.9f;
		}
		subOut = sub;
		// ---- GRIT: crackle
		if (p.grit > 0.001f) {
			// its own random numbers: right after a small uni() the next value of the same generator leans to one
			// side, so the crackle had a sign and the haze summed it up into a large DC offset (2.1.2)
			if (rng.uni() < p.grit * 40.f * dt) {
				uint32_t r = crackRng.next();
				crackle = ((r & 1u) ? 1.f : -1.f) * (0.4f + 0.6f * ((r >> 8) * (1.f / 16777216.f)));
			}
			crackle *= 0.985f;
			float c = crackle * p.grit * 0.9f;
			sumL += c, sumR += c * 0.8f;
		}
		// ---- tone (brightness at the bus), tide level, envelope
		if (doCtl)
			toneG = gmono::Filter::coef(400.f * std::pow(2.f, 6.5f * bright), fs);
		float l = tone[0].processG(sumL, toneG, 0.05f, gmono::SVF_LP);
		float r = tone[1].processG(sumR, toneG, 0.05f, gmono::SVF_LP);
		float g = env * tLevel;
		l = (l + sub) * g, r = (r + sub) * g;
		// ---- HAZE (IN L/R go in as well, carried by the tide)
		float il = inL * 0.2f * tLevel, ir = inR * 0.2f * tLevel;
		float wl, wr;
		verb.process(l + il, r + ir, wl, wr);
		float hz = clampf(p.haze, 0.f, 1.f);
		l = (l + il) * (1.f - 0.45f * hz) + wl * hz * 1.4f;
		r = (r + ir) * (1.f - 0.45f * hz) + wr * hz * 1.4f;
		// no DC can reach the outputs (a stuck offset drives the limiter to 8 V and nothing is heard)
		float io[3] = {l, r, sub * g};
		for (int c = 0; c < 3; c++) {
			float y = io[c] - dcx[c] + 0.9993f * dcy[c];
			dcx[c] = io[c], dcy[c] = y;
			io[c] = y;
		}
		l = io[0], r = io[1];
		float subG = io[2];
		float lev = p.level * 1.25f;
		subV = genra_out::limit(subG * 5.f * lev * p.outGain);
		L = genra_out::limit(l * 3.4f * lev * p.outGain);
		R = genra_out::limit(r * 3.4f * lev * p.outGain);
	}
	template <typename T>
	static T clamp(T x, T a, T b) {
		return std::min(std::max(x, a), b);
	}
};

}  // namespace flut
