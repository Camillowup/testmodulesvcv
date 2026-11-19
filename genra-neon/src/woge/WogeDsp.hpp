// SPDX-License-Identifier: GPL-3.0-only
// WOGE – the sound engine: scanned synthesis. Every voice has its own string of 24 masses on springs that moves slowly
// (about 1500 updates a second: MASS, TENSION, DAMP, CENTER); a scan head runs along it at the pitch of the note and
// reads its shape as the wave. A note (or STRIKE) excites the string: HAMMER, PLUCK, NOISE, BOW (as long as the gate
// is held) or AUDIO (the EXC input pushes it). PATH sets how the head runs, WIDTH how much of the string it reads,
// DRIFT moves the part it reads in time. Then GENRA's ladder filter with drive, an envelope, poly 4 / mono / legato.
// FREEZE holds the strings. Plain C++ without Rack (test/neon_test.cpp).
#pragma once
#include "../neon/NeonDsp.hpp"
#include "../neon/Notes.hpp"
#include "../../genra/mono/MonoDsp.hpp"
#include "../../genra/OutLimit.hpp"

namespace woge {

using namespace neon;

enum Excite { X_HAMMER, X_PLUCK, X_NOISE, X_BOW, X_AUDIO, NUM_EXCITES };
static const char* const EXCITE_NAMES[NUM_EXCITES] = {"HAMMER", "PLUCK", "NOISE", "BOW", "AUDIO"};
static const char* const EXCITE_INFO[NUM_EXCITES] = {"A HIT", "PULLED AND LET GO", "A RANDOM SHOVE", "WHILE HELD",
                                                     "EXC IN PUSHES IT"};
enum Path { P_LINEAR, P_LOOP, P_BOUNCE, P_SPIRAL, NUM_PATHS };
static const char* const PATH_NAMES[NUM_PATHS] = {"LINEAR", "LOOP", "BOUNCE", "SPIRAL"};
static const char* const PATH_INFO[NUM_PATHS] = {"END TO END", "A CLOSED RING", "THERE AND BACK", "EVEN, THEN ODD"};

static const int N = 24;     // masses
static const int MAXV = 4;   // voices
static const float UPD = 1500.f;  // string updates a second

struct Params {
	float mass = 0.4f, tension = 0.5f, damp = 0.3f, center = 0.2f;
	int excite = X_HAMMER;
	float pos = 0.3f, force = 0.6f;
	int path = P_LINEAR;
	float width = 1.f, drift = 0.f;
	float cutoff = 0.75f, cutCv = 0.f, res = 0.1f, drive = 0.f;
	float atk = 0.f, dec = 0.5f, sus = 0.8f, rel = 0.4f;
	int mode = MODE_POLY;
	float glide = 0.2f, accent = 0.5f;
	float outGain = 1.f;
	bool freeze = false;
};

struct String {
	float x[N] = {}, v[N] = {}, prev[N] = {};
	float peak = 0.f;
	void clear() {
		for (int i = 0; i < N; i++)
			x[i] = v[i] = prev[i] = 0.f;
		peak = 0.f;
	}
	/** A bump around mass position c (0..1), width w (in masses). */
	static float bump(int i, float c, float w) {
		float d = (float) i - c * (N - 1);
		return std::exp(-d * d / (2.f * w * w));
	}
	void hit(int how, float c, float amt, Rng& r) {
		switch (how) {
			case X_PLUCK:
				for (int i = 0; i < N; i++) {
					float u = (float) i / (N - 1);
					float tri = u < c ? u / std::max(c, 0.02f) : (1.f - u) / std::max(1.f - c, 0.02f);
					x[i] += amt * 0.8f * tri;
				}
				break;
			case X_NOISE: {
				float s = 0.f;
				for (int i = 0; i < N; i++) {
					s = 0.55f * s + 0.45f * r.bi();
					x[i] += amt * 1.4f * s * (0.3f + 0.7f * bump(i, c, 6.f));
				}
				break;
			}
			default:  // HAMMER (BOW and AUDIO start with a small hammer too)
				for (int i = 0; i < N; i++)
					v[i] += amt * (how == X_HAMMER ? 60.f : 18.f) * bump(i, c, 1.6f);
		}
	}
};

struct Voice {
	bool active = false, gate = false, acc = false, slideOn = false;
	float pitch = 0.f, target = 0.f, vel = 0.8f;
	gmono::Env amp;
	gmono::Filter flt[2];
	float g = 0.f;
	String s;
	float ph = 0.f, dc[2] = {}, dcy[2] = {}, ampNow = 0.f, lev = 0.f;
};

struct Engine {
	float fs = 48000.f, dt = 1.f / 48000.f;
	Voice v[MAXV];
	Notes notes;
	Rng rng{0x51ab};
	int upd = 0, updEvery = 32;
	float updFrac = 0.f;
	float driftPh = 0.f;
	int ctl = 0, newest = 0;
	bool strike = false;  // STRIKE: hit every sounding voice once
	// for the display
	float showShape[N] = {}, showScan = 0.f, showCenter = 0.5f, lastNote = 0.f, showPeak = 0.f;

	Engine() { setSampleRate(48000.f); }
	void setSampleRate(float sr) {
		fs = sr, dt = 1.f / sr;
		updEvery = std::max(1, (int) std::round(sr / UPD));
		for (auto& x : v)
			x.flt[0].reset(), x.flt[1].reset();
	}
	void allOff() {
		for (auto& x : v) {
			x.active = x.gate = false;
			x.amp.v = 0.f;
			x.amp.stage = 0;
			x.s.clear();
			x.flt[0].reset(), x.flt[1].reset();
		}
		notes.reset();
	}

	/** The string's spring constants from MASS, TENSION, CENTER (per second²), kept stable. */
	static void springs(const Params& p, float& k, float& c) {
		float m = 0.25f * std::pow(16.f, clampf(p.mass, 0.f, 1.f));
		k = 30.f * std::pow(8000.f, clampf(p.tension, 0.f, 1.f)) / m;
		k = std::min(k, 0.55f * UPD * UPD);  // explicit integration stays stable
		c = clampf(p.center, 0.f, 1.f);
		c = c * c * k * 0.25f;
	}

	/** Moves one voice's string one update further. */
	void move(Voice& vo, const Params& p, float k, float c, float damp, float exc) {
		String& s = vo.s;
		float h = 1.f / UPD;
		for (int i = 0; i < N; i++)
			s.prev[i] = s.x[i];
		bool loop = p.path == P_LOOP;
		float bow = vo.gate && p.excite == X_BOW ? p.force * (0.6f + 0.6f * vo.vel) * (vo.acc ? 1.f + p.accent : 1.f) : 0.f;
		float peak = 0.f;
		for (int i = 0; i < N; i++) {
			float l = i > 0 ? s.x[i - 1] : (loop ? s.x[N - 1] : 0.f);
			float r = i < N - 1 ? s.x[i + 1] : (loop ? s.x[0] : 0.f);
			float a = k * (l - 2.f * s.x[i] + r) - c * s.x[i];
			float b = String::bump(i, p.pos, 1.6f);
			if (bow > 0.f) {  // the bow: friction that grips near its own speed and slips when far from it
				float d = (2.5f - s.v[i]) * 0.6f;
				a += bow * 2400.f * b * d * std::exp(0.5f - 0.5f * d * d);
			}
			if (exc != 0.f)
				a += exc * b;
			s.v[i] = (s.v[i] + a * h) * damp;
			peak = std::max(peak, std::fabs(s.x[i]));
		}
		for (int i = 0; i < N; i++) {
			s.x[i] += s.v[i] * h;
			s.x[i] = clampf(s.x[i], -4.f, 4.f);
		}
		s.peak += (peak - s.peak) * 0.08f;
	}

	/** The string's shape at u (0..1 of a cycle), read along the PATH over the part WIDTH / DRIFT choose; fr: 0..1
	 * between the last two updates. */
	static float shape(const Voice& vo, const Params& p, float u, float fr, float centre) {
		float q;
		if (p.path == P_LINEAR && p.width > 0.995f)
			q = u;  // end to end: the fixed ends meet, so the wave is closed
		else if (p.path == P_LOOP && p.width > 0.995f)
			q = u;
		else {
			float tri = 1.f - std::fabs(2.f * u - 1.f);  // there and back: no jump
			q = centre + (tri - 0.5f) * clampf(p.width, 0.05f, 1.f);
			q = clampf(q, 0.f, 1.f);
		}
		float idx;
		const String& s = vo.s;
		if (p.path == P_SPIRAL) {  // the even masses, then the odd ones back
			float t = q * N;
			int k = (int) t;
			float f = t - k;
			auto at = [&](int j) {
				j = ((j % N) + N) % N;
				int m = j < N / 2 ? 2 * j : 2 * (N - 1 - j) + 1;
				return s.prev[m] + (s.x[m] - s.prev[m]) * fr;
			};
			return at(k) + (at(k + 1) - at(k)) * f;
		}
		if (p.path == P_LOOP) {
			idx = q * N;
			int k = (int) idx;
			float f = idx - k;
			int a = k % N, b = (k + 1) % N;
			float ya = s.prev[a] + (s.x[a] - s.prev[a]) * fr, yb = s.prev[b] + (s.x[b] - s.prev[b]) * fr;
			return ya + (yb - ya) * f;
		}
		// LINEAR / BOUNCE: the fixed ends (0) are part of the line
		idx = q * (N + 1);
		int k = (int) idx;
		float f = idx - k;
		auto at = [&](int j) {
			if (j <= 0 || j >= N + 1)
				return 0.f;
			return s.prev[j - 1] + (s.x[j - 1] - s.prev[j - 1]) * fr;
		};
		return at(k) + (at(k + 1) - at(k)) * f;
	}

	void process(const Params& p, const Channel* ch, int n, bool slideIn, float bpm, float fm, float excIn, float& L,
	             float& R, float& env, float& shapeOut) {
		// ---- notes
		notes.step(
		    p.mode, ch, n, MAXV,
		    [&](int i, int c, bool overlap) {
			    Voice& vo = v[i];
			    const Channel& x = ch[c];
			    bool gl = slideIn ? vo.slideOn : (p.mode != MODE_POLY && overlap);
			    vo.target = x.voct;
			    if (!gl || !vo.active)
				    vo.pitch = x.voct;
			    vo.vel = x.vel, vo.acc = x.acc;
			    bool retrig = !(p.mode == MODE_LEGATO && overlap);
			    if (retrig) {
				    vo.amp.gate = false;
				    float amt = p.force * (0.5f + 0.7f * x.vel) * (x.acc ? 1.f + p.accent : 1.f);
				    vo.s.hit(p.excite, p.pos, amt, rng);
			    }
			    vo.active = true;
			    newest = i;
		    },
		    [&](int i, int c) {
			    Voice& vo = v[i];
			    vo.target = ch[c].voct;
			    vo.slideOn = ch[c].slide;
			    vo.acc = ch[c].acc;
			    vo.gate = true;
		    },
		    [&](int i) { v[i].gate = false; });
		if (strike) {
			strike = false;
			for (auto& vo : v)
				if (vo.active)
					vo.s.hit(p.excite == X_PLUCK || p.excite == X_NOISE ? p.excite : X_HAMMER, p.pos, p.force * 1.1f, rng);
		}
		// ---- the strings, at their own slow rate (FREEZE holds them)
		float k, c;
		springs(p, k, c);
		float gamma = 0.08f * std::pow(400.f, clampf(p.damp, 0.f, 1.f));  // 0.08 .. 32 per second
		float damp = std::exp(-gamma / UPD);
		if (++upd >= updEvery) {
			upd = 0;
			if (!p.freeze)
				for (auto& vo : v)
					if (vo.active) {
						float exc = p.excite == X_AUDIO ? excIn * p.force * 900.f : 0.f;
						move(vo, p, k, c, damp, exc);
					}
		}
		float fr = p.freeze ? 1.f : (float) upd / updEvery;
		// DRIFT: the part of the string that is read wanders (in time: a cycle every 4 bars at full drift … 16 bars)
		if (!p.freeze && p.drift > 0.001f)
			driftPh = wrap(driftPh + bpm / 60.f / (16.f * (1.f - 0.75f * p.drift)) * dt);
		float centre = 0.5f + 0.42f * p.drift * sin2pi(driftPh);
		// ---- the voices
		float glideK = 1.f - std::exp(-dt / std::max(0.002f, 0.6f * p.glide * p.glide * 0.35f));
		float aT = envTime(p.atk), dT = envTime(p.dec), rT = envTime(p.rel);
		float drive = 1.f + 7.f * p.drive * p.drive;
		bool doCtl = --ctl <= 0;
		if (doCtl)
			ctl = 8;
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
			float f = C4 * std::pow(2.f, vo.pitch);
			float inc = std::min(f * dt, 0.45f);
			vo.ph = wrap(vo.ph + inc);
			float pm = fm * 0.1f;
			float yl = shape(vo, p, wrap(vo.ph + pm), fr, centre);
			float yr = shape(vo, p, wrap(vo.ph + pm + 0.035f), fr, centre);
			// the level follows the string's energy softly (a dying string fades), the shape is normalised
			float pk = std::max(vo.s.peak, 1e-4f);
			float norm = 1.f / pk * (pk / (pk + 0.06f));
			yl *= norm, yr *= norm;
			for (int s = 0; s < 2; s++) {  // DC blocker (the string may lean to one side)
				float& in = s ? yr : yl;
				float y = in - vo.dc[s] + 0.996f * vo.dcy[s];
				vo.dc[s] = in, vo.dcy[s] = y;
				in = y;
			}
			yl = gmono::sat(yl * drive * 0.8f) / std::sqrt(drive) * 1.4f;
			yr = gmono::sat(yr * drive * 0.8f) / std::sqrt(drive) * 1.4f;
			float accK = vo.acc ? p.accent : 0.f;
			if (doCtl || vo.g == 0.f) {
				float oct = 10.f * clampf(p.cutoff, 0.f, 1.f) + p.cutCv + 0.5f * vo.pitch + accK * 1.2f * e + (vo.vel - 0.8f) * 1.2f;
				vo.g = gmono::Filter::coef(20.f * std::pow(2.f, oct), fs);
			}
			yl = vo.flt[0].processG(yl, vo.g, clampf(p.res + 0.15f * accK, 0.f, 1.f), gmono::LADDER24);
			yr = vo.flt[1].processG(yr, vo.g, clampf(p.res + 0.15f * accK, 0.f, 1.f), gmono::LADDER24);
			float lev = e * (0.45f + 0.55f * vo.vel) * (1.f + 0.4f * accK);
			vo.lev = lev;
			sumL += yl * lev;
			sumR += yr * lev;
		}
		// ---- for the display and SHAPE
		const Voice& nv = v[newest];
		for (int i = 0; i < N; i++)
			showShape[i] = nv.s.x[i];
		showScan = nv.ph;
		showCenter = centre;
		showPeak = nv.s.peak;
		lastNote = nv.pitch;
		env = nv.ampNow;
		shapeOut = clampf(nv.s.x[N / 2] / std::max(nv.s.peak, 0.05f), -1.f, 1.f);
		L = genra_out::limit(sumL * 2.6f * p.outGain);
		R = genra_out::limit(sumR * 2.6f * p.outGain);
	}
};

}  // namespace woge
