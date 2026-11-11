// SPDX-License-Identifier: GPL-3.0-only
// LIED: the singing. Plain C++ (no Rack), so the host test can run it.
//
// A Unit (a syllable, a word or the whole line, from LiedText.hpp) is spoken at a constant pitch; it is sung at the
// note's pitch with PSOLA: at every period of the note a grain of two of the speech's periods (around its nearest pitch
// mark) is laid down, so the pitch follows the note while the vowel (its formants) stays. Played faster or slower
// through the consonants (SPEED); in the vowel it stays while the gate is held (VOWEL), and the rest of the syllable
// follows when the gate falls. FORMANT reads the grains faster or slower (a smaller or bigger singer), ROUGH shakes
// the periods, CHOIR adds three more singers, a little out of tune and time, left and right.
#pragma once
#include "LiedText.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lied {

static const float PI = 3.14159265358979f;

struct Rng {
	uint32_t s = 0x12345678u;
	float uni() {  // 0..1
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return (s >> 8) * (1.f / 16777216.f);
	}
	float bi() { return uni() * 2.f - 1.f; }
};

struct Params {
	float speed = 1.f;      // the consonants: 0.5 .. 2 times
	float vowel = 1.f;      // 0: spoken (the vowel as long as eSpeak says), 1: held as long as the gate
	float formant = 1.f;    // grain read rate (1: as spoken)
	float breath = 0.f;     // 0..1
	float bright = 0.5f;    // 0..1 (0.5: as spoken)
	float rough = 0.f;      // 0..1
	float choir = 0.f;      // 0..1
	float glide = 0.f;      // seconds
	float vibDepth = 0.f;   // semitones
	float vibRate = 5.5f;   // Hz
	float vibDelay = 0.3f;  // seconds
};

struct Grain {
	float c = 0, age = 0, half = 1, ratio = 1, gain = 0;
	bool on = false;
};

/** One singer reading the unit: its grains and their timing. */
struct Reader {
	static const int NG = 24;
	Grain g[NG];
	float countdown = 0.f;
	float detune = 0.f;   // semitones
	float offset = 0.f;   // source samples behind / ahead of the voice
	float panL = 1.f, panR = 1.f;
	bool odd = false;
	void reset() {
		for (Grain& x : g)
			x.on = false;
		countdown = 0.f;
	}
};

inline float sampleAt(const std::vector<float>& s, float x) {
	if (x < 0.f)
		return 0.f;
	int i = (int) x;
	if (i + 1 >= (int) s.size())
		return i < (int) s.size() ? s[i] : 0.f;
	float f = x - i;
	return s[i] + (s[i + 1] - s[i]) * f;
}

inline float hann(float t) {  // t in -1..1
	return 0.5f + 0.5f * std::cos(PI * t);
}

/** A syllable being sung. */
struct Voice {
	const Unit* u = nullptr;
	float sp = 0.f;        // where in the unit (source samples)
	float dir = 1.f;       // in the held vowel it goes back and forth
	bool held = false;
	bool active = false;
	float env = 0.f, envStep = 0.f, envTo = 0.f;
	float gain = 1.f;
	Reader r[4];

	int nearestMark(float x) const {
		const std::vector<int>& m = u->marks;
		if (m.empty())
			return (int) x;
		auto it = std::lower_bound(m.begin(), m.end(), (int) x);
		if (it == m.end())
			return m.back();
		if (it == m.begin())
			return *it;
		int b = *it, a = *(it - 1);
		return (x - a < b - x) ? a : b;
	}
};

struct Singer {
	float sr = 44100.f, srcRate = 22050.f;
	Voice v[2];           // the one singing, the one fading out
	Rng rng;
	float semis = 0.f, semisTo = 0.f;  // the note (0 = C4)
	float slideTime = 0.f;
	float vibPhase = 0.f, sinceOn = 0.f;
	float breathEnv = 0.f, nlp1 = 0.f, nlp2 = 0.f, tilt = 0.f;
	float lastSpec = 0.f;
	// coefficients kept until what they are made of changes (the same values as worked out every sample)
	float cGlideT = -1.f, cGlideSr = 0.f, cGlideK = 0.f;   // the glide's
	float cTiltSr = 0.f, cTiltK = 0.f;                     // the tilt filter's
	float cBright = -1.f, cBrightG = 1.f;                  // BRIGHT's gain

	void setRate(float sampleRate, float coreRate) {
		sr = sampleRate;
		srcRate = coreRate;
	}

	bool sounding() const { return v[0].active || v[1].active; }
	const Voice& cur() const { return v[0]; }

	/** A new syllable (gate up). */
	void start(const Unit* u, float gain, const Params& p) {
		if (v[0].active) {
			v[1] = v[0];
			v[1].envTo = 0.f;
			v[1].envStep = -1.f / (0.012f * sr);
		}
		Voice& x = v[0];
		x.u = u;
		x.sp = 0.f;
		x.dir = 1.f;
		x.held = true;
		x.active = u && !u->s.empty();
		x.env = 0.f;
		x.envTo = 1.f;
		x.envStep = 1.f / (0.004f * sr);
		x.gain = gain;
		static const float DET[4] = {0.f, -1.f, 1.f, 0.45f};
		static const float OFF[4] = {0.f, 0.011f, 0.019f, 0.007f};
		static const float PAN[4] = {0.f, -0.8f, 0.8f, -0.3f};
		for (int i = 0; i < 4; i++) {
			x.r[i].reset();
			x.r[i].detune = DET[i];
			x.r[i].offset = -OFF[i] * srcRate;
			float pan = PAN[i];
			x.r[i].panL = std::sqrt(0.5f * (1.f - pan)) * 1.41421f;
			x.r[i].panR = std::sqrt(0.5f * (1.f + pan)) * 1.41421f;
		}
		sinceOn = 0.f;
		(void) p;
	}

	/** The gate fell: the rest of the syllable follows. */
	void release() { v[0].held = false; }
	/** A slide onto the next note: the same vowel goes on (a melisma). */
	void hold() {
		if (v[0].active)
			v[0].held = true;
	}
	void stopAll() {
		v[0].active = v[1].active = false;
	}

	void note(float s, bool slide) {
		semisTo = s;
		slideTime = slide ? 0.06f : 0.f;
	}

	/** Advance one voice: where it reads, its envelope. */
	void move(Voice& x, const Params& p) {
		const Unit* u = x.u;
		float r0 = srcRate / sr;
		float n = (float) u->s.size();
		bool hasVowel = u->v1 > u->v0 + 4;
		bool inVowel = hasVowel && x.sp >= u->v0 && x.sp < u->v1;
		float rate = r0;
		if (inVowel && x.held && p.vowel > 0.97f) {
			// held: slowly back and forth in the middle of the vowel; a short vowel (a schwa) stays on its middle
			float L = (float) (u->v1 - u->v0);
			float a = u->v0 + L * 0.25f, b = u->v1 - L * 0.25f;
			if (b - a < u->t0 * 3.f) {
				float m = 0.5f * (u->v0 + u->v1);
				x.sp += (m - x.sp) * 0.02f;
			} else {
				rate = r0 * 0.25f;
				x.sp += rate * x.dir;
				if (x.sp >= b) {
					x.sp = b;
					x.dir = -1.f;
				} else if (x.sp <= a && x.dir < 0.f) {
					x.sp = a;
					x.dir = 1.f;
				}
			}
		} else {
			x.dir = 1.f;
			if (inVowel)
				rate = x.held ? r0 * std::max(0.04f, 1.f - p.vowel) : r0;
			else
				rate = r0 * p.speed;
			x.sp += rate;
		}
		if (x.sp >= n - 1.f && x.envTo > 0.f) {
			x.envTo = 0.f;
			x.envStep = -1.f / (0.006f * sr);
		}
		x.env += x.envStep;
		if (x.envStep > 0.f && x.env >= x.envTo) {
			x.env = x.envTo;
			x.envStep = 0.f;
		}
		if (x.envStep < 0.f && x.env <= 0.f) {
			x.env = 0.f;
			x.active = false;
		}
	}

	/** One reader's sound at this sample. */
	/** (noteSemis: the reader's pitch; its frequency is worked out only when a grain is due.) */
	float read(Voice& x, Reader& rd, float noteSemis, const Params& p, float level) {
		const Unit* u = x.u;
		float ratio = srcRate / sr * p.formant;
		float half = std::max(4.f, u->t0 / ratio);  // a grain is two of the speech's periods long
		rd.countdown -= 1.f;
		if (rd.countdown <= 0.f && level > 0.f) {
			float hz = 261.6256f * std::exp2(noteSemis / 12.f);
			float tout = sr / std::max(20.f, hz);
			float jit = 1.f + rng.bi() * p.rough * 0.12f;
			rd.countdown += tout * jit;
			if (rd.countdown < 1.f)
				rd.countdown = 1.f;
			for (Grain& g : rd.g)
				if (!g.on) {
					g.on = true;
					g.c = (float) x.nearestMark(x.sp + rd.offset);
					g.age = 0.f;
					g.half = half;
					g.ratio = ratio;
					// keep the loudness at any pitch: more grains overlap at higher notes
					float dens = half / tout;
					float comp = 1.f / std::sqrt(std::max(0.25f, dens));
					float shimmer = 1.f + rng.bi() * p.rough * 0.3f;
					rd.odd = !rd.odd;
					float fry = rd.odd ? 1.f - p.rough * 0.45f : 1.f;
					g.gain = std::min(1.6f, comp) * shimmer * fry * level;
					break;
				}
		}
		float out = 0.f;
		for (Grain& g : rd.g) {
			if (!g.on)
				continue;
			float t = g.age - g.half;
			out += sampleAt(u->s, g.c + t * g.ratio) * hann(t / g.half) * g.gain;
			g.age += 1.f;
			if (g.age >= 2.f * g.half)
				g.on = false;
		}
		return out;
	}

	/** One stereo sample. */
	void process(const Params& p, float& L, float& R) {
		// the note: glide, slide, vibrato
		float gt = std::max(p.glide, slideTime);
		if (gt > 0.0005f) {
			if (gt != cGlideT || sr != cGlideSr) {
				cGlideT = gt;
				cGlideSr = sr;
				cGlideK = 1.f - std::exp(-1.f / (gt * sr));
			}
			semis += (semisTo - semis) * cGlideK;
		} else
			semis = semisTo;
		sinceOn += 1.f / sr;
		vibPhase += p.vibRate / sr;
		if (vibPhase >= 1.f)
			vibPhase -= 1.f;
		float vibAmt = p.vibDepth * clampF((sinceOn - p.vibDelay) / 0.4f, 0.f, 1.f);
		float vib = vibAmt * std::sin(2.f * PI * vibPhase);
		float baseSemis = semis + vib;
		int nr = p.choir > 0.02f ? 4 : 1;
		L = R = 0.f;
		for (int k = 0; k < 2; k++) {
			Voice& x = v[k];
			if (!x.active)
				continue;
			move(x, p);
			if (!x.active)
				continue;
			float lvl = x.env * x.gain;
			for (int i = 0; i < nr; i++) {
				Reader& rd = x.r[i];
				float det = rd.detune * p.choir * 0.22f;  // up to a fifth of a semitone apart
				float g = i == 0 ? 1.f : p.choir * 0.75f;
				float s = read(x, rd, baseSemis + det, p, lvl * g);
				if (nr == 1) {
					L += s;
					R += s;
				} else {
					L += s * rd.panL;
					R += s * rd.panR;
				}
			}
		}
		if (nr > 1) {
			float c = 1.f / (1.f + p.choir * 1.2f);
			L *= c;
			R *= c;
		}
		// breath: noise that follows the voice
		float mono = 0.5f * (L + R);
		float a = std::fabs(mono);
		breathEnv += (a - breathEnv) * (a > breathEnv ? 0.01f : 0.0015f);
		if (p.breath > 0.001f) {
			float nz = rng.bi();
			nlp1 += (nz - nlp1) * 0.6f;            // ~ 7 kHz low-pass
			float hp = nlp1 - nlp2;
			nlp2 += (nlp1 - nlp2) * 0.18f;         // minus ~ 1.3 kHz: a band of air
			float b = hp * breathEnv * p.breath * 2.2f;
			L += b;
			R += b;
		}
		// brightness: tilt around 800 Hz
		if (p.bright != cBright) {
			cBright = p.bright;
			cBrightG = std::exp2((p.bright - 0.5f) * 3.f);
		}
		if (sr != cTiltSr) {
			cTiltSr = sr;
			cTiltK = 1.f - std::exp(-2.f * PI * 800.f / sr);
		}
		float bg = cBrightG, k = cTiltK;
		tilt += (mono - tilt) * k;
		float hiMono = mono - tilt;
		float add = hiMono * (bg - 1.f);
		L += add;
		R += add;
	}

	static float clampF(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
};

// ------------------------------------------------------------------------------------------------ the effects
/** ECHO (a ping-pong delay, a dotted eighth of the tempo), VERB (a small Freeverb), LO-FI (fewer samples and bits). */
struct Fx {
	float sr = 44100.f;
	std::vector<float> dl, dr;
	int dw = 0;
	float dtime = 0.375f;
	float fbL = 0.f, fbR = 0.f, dlp = 0.f, drp = 0.f;
	// reverb
	static const int NC = 4, NA = 2;
	std::vector<float> cb[2][NC], ab[2][NA];
	int ci[2][NC] = {}, ai[2][NA] = {};
	float cf[2][NC] = {};
	// lo-fi
	float holdL = 0.f, holdR = 0.f, holdN = 0.f;

	void setRate(float s) {
		sr = s;
		dl.assign((size_t) (sr * 2.1f), 0.f);
		dr.assign((size_t) (sr * 2.1f), 0.f);
		dw = 0;
		static const int CL[NC] = {1116, 1188, 1277, 1356}, AL[NA] = {556, 441};
		for (int c = 0; c < 2; c++) {
			for (int i = 0; i < NC; i++) {
				cb[c][i].assign((size_t) ((CL[i] + c * 23) * sr / 44100.f), 0.f);
				ci[c][i] = 0;
				cf[c][i] = 0.f;
			}
			for (int i = 0; i < NA; i++) {
				ab[c][i].assign((size_t) ((AL[i] + c * 23) * sr / 44100.f), 0.f);
				ai[c][i] = 0;
			}
		}
	}

	float verb1(int c, float x) {
		float out = 0.f;
		for (int i = 0; i < NC; i++) {
			std::vector<float>& b = cb[c][i];
			float y = b[ci[c][i]];
			cf[c][i] = y * 0.75f + cf[c][i] * 0.25f;
			b[ci[c][i]] = x + cf[c][i] * 0.84f;
			if (++ci[c][i] >= (int) b.size())
				ci[c][i] = 0;
			out += y;
		}
		for (int i = 0; i < NA; i++) {
			std::vector<float>& b = ab[c][i];
			float y = b[ai[c][i]];
			b[ai[c][i]] = out + y * 0.5f;
			out = y - out;
			if (++ai[c][i] >= (int) b.size())
				ai[c][i] = 0;
		}
		return out;
	}

	void process(float& L, float& R, float echo, float fdbk, float verb, float lofi, float bpm) {
		if (dl.empty())
			setRate(sr);
		// lo-fi first (on the voice)
		if (lofi > 0.01f) {
			float step = 1.f + lofi * 14.f;
			holdN += 1.f;
			if (holdN >= step) {
				holdN -= step;
				float bits = 16.f - lofi * 12.f;
				float q = std::exp2(bits - 1.f);
				holdL = std::round(L * q) / q;
				holdR = std::round(R * q) / q;
			}
			L = L * (1.f - lofi) + holdL * lofi;
			R = R * (1.f - lofi) + holdR * lofi;
		}
		// echo: ping-pong
		if (echo > 0.001f || fbL != 0.f) {
			float t = clampF(60.f / std::max(30.f, bpm) * 0.75f, 0.05f, 2.f);
			dtime += (t - dtime) * 0.0005f;
			float d = dtime * sr;
			int n = (int) dl.size();
			float rp = dw - d;
			while (rp < 0)
				rp += n;
			int i0 = (int) rp, i1 = (i0 + 1) % n;
			float f = rp - i0;
			float yl = dl[i0] + (dl[i1] - dl[i0]) * f;
			float yr = dr[i0] + (dr[i1] - dr[i0]) * f;
			dlp += (yl - dlp) * 0.35f;
			drp += (yr - drp) * 0.35f;
			float in = 0.5f * (L + R) * echo;
			dl[dw] = in + drp * fdbk;
			dr[dw] = dlp * fdbk;
			if (++dw >= n)
				dw = 0;
			fbL = dlp;
			fbR = drp;
			L += dlp;
			R += drp;
			if (echo <= 0.001f && std::fabs(dlp) + std::fabs(drp) < 1e-5f)
				fbL = fbR = 0.f;
		}
		if (verb > 0.001f) {
			float in = 0.5f * (L + R) * 0.12f;
			float vl = verb1(0, in), vr = verb1(1, in);
			L += vl * verb * 1.6f;
			R += vr * verb * 1.6f;
		}
	}

	static float clampF(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
};

}  // namespace lied
