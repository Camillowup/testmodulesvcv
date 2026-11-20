// SPDX-License-Identifier: GPL-3.0-only
// KEHLE – the engine of the neon vocoder (plain C++ without Rack, so test/kehle_test.cpp can play it).
// A classic channel vocoder: the modulator (MOD: a voice) is split into 8–32 bands, an envelope follower per band
// measures how loud each band is, and the same bands of the carrier (CAR L/R: a synth, or the built-in carrier on
// V/OCT + GATE) are opened by those envelopes. On top: seven CHARACTERs (CLASSIC, WHISPER, CHOIR, METAL, GLASS, SHIFT,
// TALKBOX), FORMANT (moves the carrier's bands: monster .. mouse), RESO, SIBIL (the hiss of S, T, F comes
// through as noise, only on real S sounds), ENSEMBLE, DRY, FREEZE (the envelopes stand still).
// Against grit: the band envelopes are smoothed (two more poles, 30 Hz), so the voice's own vibration does not ring
// on the carrier; a soft gate on MOD; a gentle low-pass at the output.
#pragma once
#include "../neon/NeonDsp.hpp"
#include "../../genra/OutLimit.hpp"
#include <cmath>

namespace kehle {

using neon::clampf;

enum Character { CH_CLASSIC, CH_WHISPER, CH_CHOIR, CH_METAL, CH_GLASS, CH_SHIFT, CH_TALKBOX, NUM_CHARS };
static const char* const CHAR_NAMES[NUM_CHARS] = {"CLASSIC", "WHISPER", "CHOIR", "METAL", "GLASS", "SHIFT", "TALKBOX"};
static const char* const CHAR_INFO[NUM_CHARS] = {"ROBOT", "GHOST", "CHOIR", "TIN CAN", "CRYSTAL", "MONSTER <> MOUSE",
                                                 "FUNK TUBE"};
enum Carrier { CAR_SAW, CAR_PULSE, CAR_STRINGS, CAR_NOISE, NUM_CARS };
static const char* const CAR_NAMES[NUM_CARS] = {"SAW CHORD", "PULSE", "STRINGS", "NOISE"};
static const char* const CAR_INFO[NUM_CARS] = {"BRIGHT", "HOLLOW", "DETUNED", "AIR"};

static const int MAXB = 32;   // bands
static const int MAXV = 6;    // voices of the built-in carrier

/** The knob values (0..1) and switches; CV is already added. */
struct Params {
	int character = CH_CLASSIC, carrier = CAR_SAW;
	float bands = 0.55f, formant = 0.5f, reso = 0.4f, sibil = 0.5f, oct = 0.5f, ens = 0.2f;
	float atk = 0.15f, rel = 0.3f, dry = 0.f, level = 0.8f;
	bool freeze = false;
	float outGain = 1.f;
};

/** How many bands the BANDS knob gives (8, 12, 16, 20, 24, 32). */
inline int bandCount(float k) {
	static const int N[6] = {8, 12, 16, 20, 24, 32};
	return N[(int) clampf(std::round(clampf(k, 0.f, 1.f) * 5.f), 0.f, 5.f)];
}
/** The octave shift of the built-in carrier (-2 .. +2). */
inline int octShift(float k) { return (int) clampf(std::round((clampf(k, 0.f, 1.f) - 0.5f) * 4.f), -2.f, 2.f); }
/** The formant shift in octaves (±1, SHIFT: ±2). */
inline float formantOct(float k, int character) {
	return (clampf(k, 0.f, 1.f) - 0.5f) * (character == CH_SHIFT ? 4.f : 2.f);
}

/** A band-pass (RBJ, 0 dB at the centre), transposed direct form II. */
struct BP {
	float b0 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;
	void set(float f, float q, float fs) {
		f = clampf(f, 20.f, 0.45f * fs);
		float w = 2.f * neon::PI * f / fs, cs = std::cos(w), al = std::sin(w) / (2.f * q), a0 = 1.f + al;
		b0 = al / a0;
		a1 = -2.f * cs / a0;
		a2 = (1.f - al) / a0;
	}
	float process(float x) {
		float y = b0 * x + z1;
		z1 = -a1 * y + z2;
		z2 = -b0 * x - a2 * y;
		return y;
	}
	void reset() { z1 = z2 = 0.f; }
};
/** A one-pole high-pass (for the hiss detector and the noise). */
struct HP1 {
	float k = 0.5f, x1 = 0.f, y = 0.f;
	void set(float f, float fs) { k = std::exp(-2.f * neon::PI * f / fs); }
	float process(float x) {
		y = k * (y + x - x1);
		x1 = x;
		return y;
	}
};

inline float polyBlep(float t, float dt) {
	if (t < dt) {
		t /= dt;
		return t + t - t * t - 1.f;
	}
	if (t > 1.f - dt) {
		t = (t - 1.f) / dt;
		return t * t + t + t + 1.f;
	}
	return 0.f;
}

/** One voice of the built-in carrier: saw, pulse or three detuned saws; fades in and out (no clicks). */
struct Voice {
	float ph[3] = {0.f, 0.33f, 0.71f}, freq = 0.f, amp = 0.f, vib = 0.f;
	bool on = false;
	float process(int type, float target, float fs, bool choir, float vibPh) {
		float dt = 1.f / fs;
		freq = freq <= 0.f ? target : freq + (target - freq) * 0.002f;  // a hint of glide between chords
		amp += ((on ? 1.f : 0.f) - amp) * (on ? 0.004f : 0.0015f);
		if (amp < 1e-5f && !on)
			return 0.f;
		float f = freq * (choir ? 1.f + 0.006f * neon::sin2pi(vibPh) : 1.f);
		float s = 0.f;
		if (type == CAR_STRINGS || choir) {
			static const float DET[3] = {1.f, 1.0071f, 0.9934f};
			for (int k = 0; k < 3; k++) {
				float inc = f * DET[k] * dt;
				ph[k] += inc;
				if (ph[k] >= 1.f)
					ph[k] -= 1.f;
				s += (2.f * ph[k] - 1.f) - polyBlep(ph[k], inc);
			}
			s *= 0.5f;
		} else {
			float inc = f * dt;
			ph[0] += inc;
			if (ph[0] >= 1.f)
				ph[0] -= 1.f;
			if (type == CAR_PULSE) {
				float p2 = ph[0] + 0.5f;
				if (p2 >= 1.f)
					p2 -= 1.f;
				s = (ph[0] < 0.5f ? 1.f : -1.f) + polyBlep(ph[0], inc) - polyBlep(p2, inc);
			} else
				s = (2.f * ph[0] - 1.f) - polyBlep(ph[0], inc);
		}
		return s * amp;
	}
};

struct Engine {
	float fs = 48000.f;
	BP ana[MAXB][2], car[MAXB][2][2];  // the modulator's bands (two in a row: steeper) and the carrier's, per side
	float env[MAXB] = {}, sm1[MAXB] = {}, sm2[MAXB] = {}, smooth[MAXB] = {}, ring[MAXB] = {}, ringInc[MAXB] = {}, weight[MAXB] = {};
	int nb = 0, lastChar = -1;
	float lastShift = -99.f, lastQ = -1.f, carQ = 4.f, makeup = 1.f;
	HP1 sibHp[2], noiseHp;
	float sibEnv = 0.f, voiceEnv = 0.f, sibOpen = 0.f, modEnv = 0.f, gateEnv = 0.f, gate = 0.f, voiceLp = 0.f;
	float lp[2][2] = {};  // the output's gentle low-pass (two poles per side)
	Voice voice[MAXV];
	neon::Rng rng;
	neon::Chorus chorus;
	neon::DcBlock dc[2];
	float vibPh = 0.f;
	// for the display (UI thread reads, no lock: only numbers)
	float showBand[MAXB] = {};
	int showN = 16;
	float showLevel = 0.f, showNote = 0.f;
	bool showExt = false;

	void setSampleRate(float sr) {
		fs = sr;
		chorus.setSampleRate(sr);
		for (auto& d : dc)
			d.setSampleRate(sr);
		sibHp[0].set(4200.f, sr);
		sibHp[1].set(4200.f, sr);
		noiseHp.set(3000.f, sr);
		lastShift = -99.f;
		nb = 0;
	}

	/** The bands' frequencies, from 110 Hz to 7.5 kHz; the carrier's are moved by FORMANT. */
	void setBands(int n, float shiftOct, float reso, int character) {
		float span = std::log2(7500.f / 110.f), step = span / (n - 1);
		float qBase = 1.f / (std::pow(2.f, step / 2.f) - std::pow(2.f, -step / 2.f));
		float q = qBase * (0.8f + 3.5f * reso * reso);
		if (character == CH_GLASS)
			q *= 4.f;
		if (character == CH_TALKBOX)
			q *= 0.6f;
		bool full = n != nb || character != lastChar;
		for (int i = 0; i < n; i++) {
			float f = 110.f * std::pow(2.f, step * i);
			if (full) {
				ana[i][0].set(f, qBase * 1.1f, fs);
				ana[i][1].set(f, qBase * 1.1f, fs);
				weight[i] = 1.f + 0.25f * i / (n - 1);  // a touch more of the highs: clearer words (not the breath)
			}
			float fc = f * std::pow(2.f, shiftOct);
			for (int s = 0; s < 2; s++) {
				car[i][s][0].set(fc, q, fs);
				car[i][s][1].set(fc, q, fs);
			}
			ringInc[i] = clampf(fc * (character == CH_GLASS ? 1.f : 1.41f), 20.f, 0.45f * fs) / fs;
		}
		if (n != nb)
			for (int i = n; i < MAXB; i++)
				env[i] = sm1[i] = sm2[i] = smooth[i] = 0.f;
		nb = n;
		lastChar = character;
		lastShift = shiftOct;
		lastQ = reso;
		carQ = q;
		// narrow carrier bands pass less: make up for it; more bands: each is quieter
		makeup = std::min(std::sqrt(q / qBase), 2.5f) * std::sqrt(16.f / n) * 2.2f;
	}

	/** notes: V/OCT of the built-in carrier's voices (count n, 0 = silent); ext: a carrier is patched (carL, carR in volts);
	 * mod: the voice (volts). Out: L, R (volts), env 0..1 (how loud the voice is). */
	void process(const Params& p, const float* notes, int n, bool ext, float carL, float carR, float mod, float& L, float& R,
	             float& envOut) {
		int nbWant = bandCount(p.bands);
		if (p.character == CH_TALKBOX)
			nbWant = std::min(nbWant, 12);
		float sh = formantOct(p.formant, p.character);
		if (nbWant != nb || p.character != lastChar || std::fabs(sh - lastShift) > 0.004f || std::fabs(p.reso - lastQ) > 0.004f)
			setBands(nbWant, sh, p.reso, p.character);

		// the carrier
		float cl, cr;
		bool choir = p.character == CH_CHOIR;
		vibPh = neon::wrap(vibPh + 5.2f / fs);
		if (ext) {
			cl = carL * 0.2f;
			cr = carR * 0.2f;
		} else {
			cl = cr = 0.f;
			int os = octShift(p.oct);
			for (int v = 0; v < MAXV; v++) {
				Voice& vc = voice[v];
				vc.on = v < n;
				float target = v < n ? neon::C4 * std::pow(2.f, notes[v] + os) : vc.freq;
				float s = 0.f;
				if (p.carrier != CAR_NOISE)
					s = vc.process(p.carrier, target, fs, choir, vibPh + 0.17f * v);
				else {
					vc.process(CAR_SAW, target, fs, false, 0.f);  // keeps its fade
					s = rng.bi() * vc.amp * 0.9f;
				}
				float pan = (v & 1) ? 0.35f : -0.35f;
				cl += s * (1.f - pan) * 0.6f;
				cr += s * (1.f + pan) * 0.6f;
			}
			if (n > 0)
				showNote = notes[0] + os;
		}
		showExt = ext;
		if (p.character == CH_WHISPER) {
			float nl = rng.bi(), nr = rng.bi();
			cl = 0.15f * cl + 0.9f * nl;
			cr = 0.15f * cr + 0.9f * nr;
		}
		if (p.character == CH_TALKBOX) {
			cl = neon::sat(cl * 2.f) * 0.7f;
			cr = neon::sat(cr * 2.f) * 0.7f;
		}

		// the modulator: bands and their envelopes
		float m = mod * 0.2f;
		// a soft gate: below the noise of a quiet mic the bands stay shut
		float am = std::fabs(m);
		gateEnv += (am - gateEnv) * (am > gateEnv ? 0.01f : 0.0005f);
		float gTarget = gateEnv > 0.004f ? 1.f : (gateEnv < 0.002f ? 0.f : (gateEnv - 0.002f) / 0.002f);
		gate += (gTarget - gate) * (gTarget > gate ? 0.004f : 0.0003f);
		m *= gate;
		float ta = 0.002f * std::pow(30.f, clampf(p.atk, 0.f, 1.f)), tr = 0.005f * std::pow(300.f, clampf(p.rel, 0.f, 1.f));
		float ka = 1.f - std::exp(-1.f / (ta * fs)), kr = 1.f - std::exp(-1.f / (tr * fs));
		float ks = 1.f - std::exp(-2.f * neon::PI * 30.f / fs);  // the smoothing after the follower (2 poles, 30 Hz)
		bool talk = p.character == CH_TALKBOX;
		float outL = 0.f, outR = 0.f, total = 0.f;
		for (int i = 0; i < nb; i++) {
			float a = ana[i][1].process(ana[i][0].process(m)) * weight[i];
			float r = std::fabs(a);
			if (!p.freeze) {
				env[i] += (r - env[i]) * (r > env[i] ? ka : kr);
				sm1[i] += (env[i] - sm1[i]) * ks;
				sm2[i] += (sm1[i] - sm2[i]) * ks;
			}
			float e = sm2[i];
			if (talk) {
				smooth[i] += (e - smooth[i]) * 0.004f;
				e = smooth[i];
			}
			float bl = car[i][0][1].process(car[i][0][0].process(cl));
			float br = car[i][1][1].process(car[i][1][0].process(cr));
			if (p.character == CH_METAL) {
				ring[i] = neon::wrap(ring[i] + ringInc[i]);
				float rm = neon::sin2pi(ring[i]);
				bl = 0.3f * bl + 0.9f * bl * rm;
				br = 0.3f * br + 0.9f * br * rm;
			} else if (p.character == CH_GLASS) {
				ring[i] = neon::wrap(ring[i] + ringInc[i]);
				float sing = neon::sin2pi(ring[i]) * 0.12f;
				bl += sing;
				br += sing;
			}
			outL += bl * e;
			outR += br * e;
			total += e;
			showBand[i] = e;
		}
		showN = nb;

		// the hiss of S, T, F: only when the voice's highs are louder than its body (a sung vowel is not)
		float hs = sibHp[1].process(sibHp[0].process(m));
		float hr = std::fabs(hs);
		voiceLp += (m - voiceLp) * 0.25f;  // ~2.5 kHz
		float vr = std::fabs(voiceLp);
		if (!p.freeze) {
			sibEnv += (hr - sibEnv) * (hr > sibEnv ? 0.02f : 0.0015f);
			voiceEnv += (vr - voiceEnv) * (vr > voiceEnv ? 0.02f : 0.0015f);
			float ratio = sibEnv / (voiceEnv + 1e-4f);
			float want = clampf((ratio - 1.0f) * 2.f, 0.f, 1.f);
			sibOpen += (want - sibOpen) * (want > sibOpen ? 0.01f : 0.002f);
		}
		float noise = noiseHp.process(rng.bi()) * sibEnv * sibOpen * p.sibil * 3.f;
		float g = makeup * 1.2f;
		L = outL * g + noise;
		R = outR * g + noise;
		{  // a gentle low-pass (~11 kHz): no fizz on top
			float kl = 1.f - std::exp(-2.f * neon::PI * std::min(11000.f, 0.4f * fs) / fs);
			lp[0][0] += (L - lp[0][0]) * kl;
			lp[0][1] += (lp[0][0] - lp[0][1]) * kl;
			lp[1][0] += (R - lp[1][0]) * kl;
			lp[1][1] += (lp[1][0] - lp[1][1]) * kl;
			L = lp[0][1];
			R = lp[1][1];
		}
		float ens = choir ? std::max(p.ens, 0.6f) : p.ens;
		chorus.process(ens, L, R);
		float dry = clampf(p.dry, 0.f, 1.f);
		L = (L * (1.f - 0.5f * dry) + m * dry) * 5.f;
		R = (R * (1.f - 0.5f * dry) + m * dry) * 5.f;
		float lv = 2.f * clampf(p.level, 0.f, 1.f) * p.outGain;
		L = genra_out::limit(dc[0].process(L) * lv);
		R = genra_out::limit(dc[1].process(R) * lv);
		float lev = std::fabs(m);
		modEnv += (lev - modEnv) * (lev > modEnv ? 0.01f : 0.0006f);
		envOut = clampf(modEnv * 3.f, 0.f, 1.f);
		showLevel = clampf(total * 4.f / std::max(1, nb) * 4.f, 0.f, 1.f);
		if (p.freeze)
			showLevel = clampf(total * 16.f / std::max(1, nb), 0.f, 1.f);
	}

	void allOff() {
		for (Voice& v : voice) {
			v.on = false;
			v.amp = 0.f;
		}
	}
};

}  // namespace kehle
