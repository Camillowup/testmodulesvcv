// SPDX-License-Identifier: GPL-3.0-only
// GENRA NEON – wavetables: a table is up to 64 frames of one cycle each (2048 samples). For every frame it keeps
// band-limited copies (mip levels: 1024, 512 … 1 harmonics, each only as long as it needs), so a high note reads a copy
// without the harmonics above the Nyquist frequency and does not alias.
// The built-in tables (32, in the folders BASIC, DIGITAL, VOCAL, TECHNO, TEXTURE) are computed here, from formulas;
// MUTATE makes new ones from the same building blocks (a seed). WAV files load too (the common format: frames of 2048
// samples one after the other; a 'clm ' chunk with another frame size is read; 16/24/32-bit PCM and 32-bit float).
// Plain C++ without Rack (test/neon_test.cpp runs it).
#pragma once
#include "NeonDsp.hpp"
#include <complex>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace neon {

static const int FRAME = 2048;        // samples per frame
static const int MAXFRAMES = 64;
static const int LEVELS = 11;         // mip levels: 1024 >> k harmonics (k = 0..10)

// ---------------------------------------------------------------- FFT (radix 2, in place)
inline void fft(std::complex<float>* a, int n, bool inverse) {
	for (int i = 1, j = 0; i < n; i++) {
		int bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
			std::swap(a[i], a[j]);
	}
	for (int len = 2; len <= n; len <<= 1) {
		double ang = 2.0 * 3.14159265358979323846 / len * (inverse ? 1.0 : -1.0);
		std::complex<float> wl((float) std::cos(ang), (float) std::sin(ang));
		for (int i = 0; i < n; i += len) {
			std::complex<float> w(1.f, 0.f);
			for (int j = 0; j < len / 2; j++) {
				std::complex<float> u = a[i + j], v = a[i + j + len / 2] * w;
				a[i + j] = u + v;
				a[i + j + len / 2] = u - v;
				w *= wl;
			}
		}
	}
}

struct Table {
	std::string name;
	int frames = 1;
	float ratio = 1.f;  // the played note sits on this harmonic (chord tables: the chord's root)
	int len[LEVELS] = {};
	std::vector<float> data[LEVELS];  // [frame * len + i]

	static int levelLen(int k) {
		int h = 1024 >> k;
		return std::max(32, std::min(FRAME, 4 * h));
	}
	/** The mip level for a phase increment (cycles per sample). */
	static int level(float inc) {
		if (inc <= 0.f)
			return 0;
		float maxH = 0.5f / inc;  // harmonics below Nyquist
		if (maxH >= 1024.f)
			return 0;
		int k = 0;
		float h = 1024.f;
		while (k < LEVELS - 1 && h > maxH)
			h *= 0.5f, k++;
		return k;
	}
	/** One sample: level k, frame position f (0 .. frames-1, fractional), phase 0..1. */
	float read(int k, float f, float phase) const {
		const int L = len[k];
		const float* d = data[k].data();
		f = clampf(f, 0.f, (float) (frames - 1));
		int f0 = (int) f;
		int f1 = std::min(f0 + 1, frames - 1);
		float ff = f - (float) f0;
		float x = phase * (float) L;
		int i = (int) x;
		float xf = x - (float) i;
		i &= L - 1;
		int i1 = (i + 1) & (L - 1);
		const float* a = d + f0 * L;
		const float* b = d + f1 * L;
		float ya = a[i] + (a[i1] - a[i]) * xf;
		float yb = b[i] + (b[i1] - b[i]) * xf;
		return ya + (yb - ya) * ff;
	}
	/** A frame for the display: n points of level 0 at position f. */
	void shape(float f, int n, float* out) const {
		for (int i = 0; i < n; i++)
			out[i] = read(0, f, (float) i / (float) n);
	}

	/** Builds the levels from frames given as spectra (bins 0..FRAME/2, complex). Normalises to a peak of 1. */
	void buildFromSpectra(const std::vector<std::vector<std::complex<float>>>& spec) {
		frames = (int) spec.size();
		std::vector<std::complex<float>> buf(FRAME);
		for (int k = 0; k < LEVELS; k++) {
			len[k] = levelLen(k);
			data[k].assign((size_t) frames * len[k], 0.f);
		}
		float peak = 1e-9f;
		for (int fr = 0; fr < frames; fr++) {
			for (int k = 0; k < LEVELS; k++) {
				int L = len[k];
				int H = std::min(1024 >> k, L / 2 - 1);
				std::fill(buf.begin(), buf.begin() + L, std::complex<float>(0.f, 0.f));
				for (int h = 1; h <= H && h < (int) spec[fr].size(); h++) {
					buf[h] = spec[fr][h];
					buf[L - h] = std::conj(spec[fr][h]);
				}
				fft(buf.data(), L, true);
				float* d = data[k].data() + fr * L;
				for (int i = 0; i < L; i++)
					d[i] = buf[i].real();
				if (k == 0)
					for (int i = 0; i < L; i++)
						peak = std::max(peak, std::fabs(d[i]));
			}
		}
		// loudness: the same RMS for every table (so TABLE and POSITION do not jump in level), peaks kept in check
		double ss = 0.0;
		for (float v : data[0])
			ss += (double) v * v;
		float rms = (float) std::sqrt(ss / std::max<size_t>(1, data[0].size())) + 1e-9f;
		float g = std::min(0.42f / rms, 1.6f / peak);
		for (int k = 0; k < LEVELS; k++)
			for (float& v : data[k])
				v *= g;
	}
	/** Builds from frames in the time domain (FRAME samples each). */
	void buildFromWaves(const std::vector<std::vector<float>>& waves) {
		std::vector<std::vector<std::complex<float>>> spec(waves.size());
		std::vector<std::complex<float>> buf(FRAME);
		for (size_t f = 0; f < waves.size(); f++) {
			for (int i = 0; i < FRAME; i++)
				buf[i] = std::complex<float>(i < (int) waves[f].size() ? waves[f][i] : 0.f, 0.f);
			fft(buf.data(), FRAME, false);
			spec[f].assign(buf.begin(), buf.begin() + FRAME / 2 + 1);
			for (auto& c : spec[f])
				c *= 1.f / (float) FRAME;
			spec[f][0] = 0.f;  // no DC
		}
		buildFromSpectra(spec);
	}
	size_t bytes() const {
		size_t b = 0;
		for (int k = 0; k < LEVELS; k++)
			b += data[k].size() * sizeof(float);
		return b;
	}
};

// ---------------------------------------------------------------- the built-in tables
enum BuiltinFolder { F_BASIC, F_DIGITAL, F_VOCAL, F_TECHNO, F_TEXTURE, NUM_BUILTIN_FOLDERS };
static const char* const FOLDER_NAMES[NUM_BUILTIN_FOLDERS] = {"BASIC", "DIGITAL", "VOCAL", "TECHNO", "TEXTURE"};

struct BuiltinInfo {
	int folder;
	const char* name;
	const char* info;
};
enum Builtin {
	B_BASIC, B_PWM, B_SYNC, B_FOLD, B_SAWSTACK,
	B_DIGITAL, B_FM, B_FMBELL, B_PHASEDIST, B_CHIP, B_BITWAVE,
	B_VOCAL, B_CHOIR, B_GROWL, B_TALK,
	B_ACID, B_STAB, B_DUBCHORD, B_REESE, B_HOOVER, B_RAVEORGAN, B_METALLIC, B_INDUSTRIAL,
	B_GLASS, B_AIR, B_NOISESCAN, B_SPECTRAL, B_ODDEVEN, B_ORGAN, B_STRINGS, B_PLUCK, B_SUB,
	NUM_BUILTINS
};
static const BuiltinInfo BUILTINS[NUM_BUILTINS] = {
	{F_BASIC, "BASIC", "SINE TRI SAW SQUARE"},       {F_BASIC, "PWM", "PULSE WIDTH"},
	{F_BASIC, "SYNC", "HARD SYNC SWEEP"},            {F_BASIC, "FOLD", "SINE FOLDER"},
	{F_BASIC, "SAW STACK", "DENSER, BRIGHTER"},      {F_DIGITAL, "DIGITAL", "80S DIGITAL STEPS"},
	{F_DIGITAL, "FM", "2-OP, INDEX UP"},             {F_DIGITAL, "FM BELL", "BELL FM"},
	{F_DIGITAL, "PHASE DIST", "PHASE DISTORTION"},   {F_DIGITAL, "CHIP", "8-BIT PULSES"},
	{F_DIGITAL, "BITWAVE", "FEWER BITS"},            {F_VOCAL, "VOCAL", "A E I O U"},
	{F_VOCAL, "CHOIR", "SOFT FORMANTS"},             {F_VOCAL, "GROWL", "YOY FORMANTS"},
	{F_VOCAL, "TALK", "TALKING VOWELS"},             {F_TECHNO, "ACID", "RESONANT SAW"},
	{F_TECHNO, "STAB", "MINOR 7 CHORD"},             {F_TECHNO, "DUB CHORD", "DARK MINOR 9"},
	{F_TECHNO, "REESE", "BEATING SAWS"},             {F_TECHNO, "HOOVER", "RAVE HOOVER"},
	{F_TECHNO, "RAVE ORGAN", "90S HOUSE ORGAN"},     {F_TECHNO, "METALLIC", "INHARMONIC"},
	{F_TECHNO, "INDUSTRIAL", "RAW, NOISY"},          {F_TEXTURE, "GLASS", "GLASSY PARTIALS"},
	{F_TEXTURE, "AIR", "BREATHY"},                   {F_TEXTURE, "NOISE SCAN", "TONE TO NOISE"},
	{F_TEXTURE, "SPECTRAL", "HARMONICS ONE BY ONE"}, {F_TEXTURE, "ODD / EVEN", "ODD THEN EVEN"},
	{F_TEXTURE, "ORGAN", "DRAWBARS"},                {F_TEXTURE, "STRINGS", "BRIGHT STRING"},
	{F_TEXTURE, "PLUCK", "BRIGHT TO DULL"},          {F_TEXTURE, "SUB", "ROUND LOWS"},
};
/** The built-in tables of a folder, in order. */
inline std::vector<int> builtinsOf(int folder) {
	std::vector<int> v;
	for (int i = 0; i < NUM_BUILTINS; i++)
		if (BUILTINS[i].folder == folder)
			v.push_back(i);
	return v;
}

namespace gen {
typedef std::vector<std::complex<float>> Spec;
inline float saw(float p) { return 2.f * wrap(p) - 1.f; }
inline float pulse(float p, float pw) { return wrap(p) < pw ? 1.f : -1.f; }
inline float tri(float p) { return 1.f - 4.f * std::fabs(wrap(p + 0.25f) - 0.5f); }
inline float sn(float p) { return std::sin(2.f * PI * p); }
/** A spectrum with harmonic amplitudes a(h) (sine phase). */
template <typename F>
inline Spec harm(F a, int maxH = 1023) {
	Spec s(FRAME / 2 + 1, {0.f, 0.f});
	for (int h = 1; h <= maxH && h < FRAME / 2; h++) {
		float v = a(h);
		s[h] = std::complex<float>(0.f, -0.5f * v);
	}
	return s;
}
/** Formant amplitude of harmonic h (f0 in Hz) for a vowel given by three formants. */
inline float formant(float f, const float* F, const float* B) {
	float a = 0.f;
	for (int i = 0; i < 3; i++) {
		float x = (f - F[i]) / B[i];
		a += std::exp(-0.5f * x * x) * (i == 0 ? 1.f : (i == 1 ? 0.7f : 0.35f));
	}
	return a;
}
static const float VOWELS[5][3] = {{800, 1150, 2900}, {400, 1600, 2700}, {300, 2300, 3000}, {450, 800, 2830}, {325, 700, 2530}};
inline void vowelAt(const int* seq, int n, float t, float* F) {
	float x = clampf(t, 0.f, 1.f) * (n - 1);
	int i = std::min((int) x, n - 2);
	float f = x - i;
	for (int k = 0; k < 3; k++)
		F[k] = VOWELS[seq[i]][k] + (VOWELS[seq[i + 1]][k] - VOWELS[seq[i]][k]) * f;
}
}  // namespace gen

/** Computes built-in table b (64 frames). */
inline std::shared_ptr<Table> makeBuiltin(int b) {
	using namespace gen;
	auto T = std::make_shared<Table>();
	T->name = BUILTINS[b].name;
	const int NF = MAXFRAMES;
	bool spectral = false;
	std::vector<std::vector<float>> waves;
	std::vector<Spec> specs;
	Rng rng(1234u + b * 77u);
	// random tables for DIGITAL / INDUSTRIAL / METALLIC: fixed per table
	float rnd[8][64];
	for (int i = 0; i < 8; i++)
		for (int j = 0; j < 64; j++)
			rnd[i][j] = rng.uni();
	switch (b) {
		case B_SAWSTACK: case B_DIGITAL: case B_VOCAL: case B_CHOIR: case B_GROWL: case B_TALK: case B_ACID:
		case B_STAB: case B_DUBCHORD: case B_RAVEORGAN: case B_METALLIC: case B_INDUSTRIAL: case B_GLASS: case B_AIR:
		case B_NOISESCAN: case B_SPECTRAL: case B_ODDEVEN: case B_ORGAN: case B_STRINGS: case B_PLUCK:
			spectral = true;
			break;
		default: break;
	}
	if (b == B_STAB)
		T->ratio = 10.f;
	if (b == B_DUBCHORD)
		T->ratio = 20.f;
	for (int fr = 0; fr < NF; fr++) {
		float t = (float) fr / (NF - 1);
		if (spectral) {
			Spec s;
			switch (b) {
				case B_SAWSTACK: {
					float H = std::pow(2.f, 1.f + 8.f * t);
					s = harm([&](int h) { return (1.f / h) * clampf(H - h + 1.f, 0.f, 1.f) * (1.f + 0.3f * t * ((h % 3) == 0)); });
					break;
				}
				case B_DIGITAL: {
					int step = std::min((int) (t * 8.f), 7), nxt = std::min(step + 1, 7);
					float f = t * 8.f - step;
					f = f < 0.85f ? 0.f : (f - 0.85f) / 0.15f;  // hard digital steps
					s = harm([&](int h) {
						if (h > 48)
							return 0.f;
						float a0 = rnd[step][h] > 0.55f ? rnd[step][(h * 7) % 64] / std::sqrt((float) h) : 0.f;
						float a1 = rnd[nxt][h] > 0.55f ? rnd[nxt][(h * 7) % 64] / std::sqrt((float) h) : 0.f;
						if (h == 1)
							a0 += 0.6f, a1 += 0.6f;
						return a0 + (a1 - a0) * f;
					});
					break;
				}
				case B_VOCAL: case B_CHOIR: case B_GROWL: case B_TALK: {
					static const int sV[5] = {0, 1, 2, 3, 4};
					static const int sC[3] = {3, 0, 1};
					static const int sG[3] = {4, 0, 4};
					static const int sT[5] = {0, 2, 4, 1, 3};
					float F[3];
					float f0 = b == B_CHOIR ? 220.f : (b == B_GROWL ? 55.f : 110.f);
					float bw = b == B_CHOIR ? 1.6f : (b == B_TALK ? 0.7f : 1.f);
					if (b == B_VOCAL) vowelAt(sV, 5, t, F);
					else if (b == B_CHOIR) vowelAt(sC, 3, t, F);
					else if (b == B_GROWL) vowelAt(sG, 3, t, F);
					else vowelAt(sT, 5, t, F);
					const float B[3] = {80.f * bw, 100.f * bw, 140.f * bw};
					int maxH = (int) (4500.f / f0);
					s = harm([&](int h) {
						float a = formant(f0 * h, F, B);
						if (b == B_GROWL)
							a += 0.25f / h;  // a raw saw underneath
						return a / std::pow((float) h, 0.35f);
					}, maxH);
					break;
				}
				case B_ACID: {
					float pk = 2.f + 34.f * t;
					s = harm([&](int h) {
						float x = (h - pk) / 1.6f;
						return 1.f / h + 1.2f * std::exp(-0.5f * x * x) / std::sqrt(pk);
					});
					break;
				}
				case B_STAB: case B_DUBCHORD: {
					// a chord in one cycle: its notes are harmonics of a low fundamental (the table plays ratio x lower)
					static const int st[4] = {10, 12, 15, 18};             // minor 7
					static const int dc[5] = {20, 24, 30, 36, 45};         // minor 9
					const int* n = b == B_STAB ? st : dc;
					int nn = b == B_STAB ? 4 : 5;
					float bright = b == B_STAB ? 2.f + 10.f * t : 1.5f + 4.f * t;
					s = Spec(FRAME / 2 + 1, {0.f, 0.f});
					for (int i = 0; i < nn; i++)
						for (int m = 1; m * n[i] < FRAME / 2 && m < 40; m++) {
							float a = std::exp(-(m - 1) / bright) / m * (b == B_DUBCHORD ? 1.f / m : 1.f);
							s[m * n[i]] += std::complex<float>(0.f, -0.5f * a);
						}
					break;
				}
				case B_RAVEORGAN: case B_ORGAN: {
					static const int bars[9] = {1, 2, 3, 4, 6, 8, 10, 12, 16};
					float lv[9];
					for (int i = 0; i < 9; i++) {
						float x = t * 9.f - i;
						lv[i] = b == B_ORGAN ? clampf(x + 1.f, 0.f, 1.f) * (i == 0 ? 1.f : 0.8f)
						                     : (i < 3 ? 1.f : clampf(x - 2.f + 1.f, 0.f, 1.f) * 0.7f);
					}
					if (b == B_RAVEORGAN)
						lv[1] = 1.f, lv[3] = 0.9f;
					s = Spec(FRAME / 2 + 1, {0.f, 0.f});
					for (int i = 0; i < 9; i++)
						s[bars[i]] += std::complex<float>(0.f, -0.5f * lv[i]);
					if (b == B_RAVEORGAN)  // key click / grit
						for (int h = 17; h < 60; h++)
							s[h] += std::complex<float>(0.f, -0.5f * 0.04f * t * rnd[1][h % 64] / std::sqrt((float) h));
					break;
				}
				case B_METALLIC: {
					static const int part[8] = {1, 7, 11, 13, 17, 23, 29, 37};
					s = Spec(FRAME / 2 + 1, {0.f, 0.f});
					for (int i = 0; i < 8; i++) {
						float a = (i == 0 ? 0.7f : 0.5f + 0.5f * std::sin(6.2832f * (t * (1 + i % 3) + rnd[0][i])));
						s[part[i]] += std::complex<float>(0.f, -0.5f * a / (1.f + 0.15f * i));
						s[part[i] * 2] += std::complex<float>(0.f, -0.5f * a * 0.3f * t);
					}
					break;
				}
				case B_INDUSTRIAL: {
					s = Spec(FRAME / 2 + 1, {0.f, 0.f});
					for (int h = 1; h < 200; h++) {
						float r = rnd[(h + fr) % 8][(h * 13) % 64];
						float a = (h == 1 ? 1.f : (r < 0.3f + 0.6f * t ? r : 0.f)) / std::pow((float) h, 0.7f);
						float ph = 6.2832f * rnd[(h * 3) % 8][(h + fr * 5) % 64];
						s[h] = std::polar(0.5f * a, ph);
					}
					break;
				}
				case B_GLASS: {
					static const int part[6] = {1, 3, 5, 8, 12, 17};
					s = Spec(FRAME / 2 + 1, {0.f, 0.f});
					for (int i = 0; i < 6; i++) {
						float a = std::exp(-i * (1.4f - 1.2f * t));
						s[part[i]] += std::complex<float>(0.f, -0.5f * a);
					}
					break;
				}
				case B_AIR: case B_NOISESCAN: {
					s = Spec(FRAME / 2 + 1, {0.f, 0.f});
					float n = b == B_AIR ? 0.15f + 0.6f * t : t;
					Rng r2(777u + fr * 31u + b);
					int maxH = b == B_AIR ? 220 : 700;
					for (int h = 1; h < maxH; h++) {
						float tone = (b == B_AIR ? (h <= 4 ? 1.f / h : 0.f) : (h == 1 ? 1.f : 0.f)) * (1.f - n);
						float noise = n * r2.uni() / std::sqrt((float) h) * (b == B_AIR ? std::exp(-h / 120.f) : 1.f);
						s[h] = std::polar(0.5f * (tone + noise), h == 1 ? -1.5708f : 6.2832f * r2.uni());
					}
					break;
				}
				case B_SPECTRAL: {
					float H = 1.f + t * 31.f;
					s = harm([&](int h) { return clampf(H - h + 1.f, 0.f, 1.f) / std::sqrt((float) h); });
					break;
				}
				case B_ODDEVEN: {
					s = harm([&](int h) {
						float odd = h % 2 ? 1.f : 0.f;
						float w = t < 0.5f ? (h % 2 ? 1.f : 2.f * t) : (h % 2 ? 1.f - 2.f * (t - 0.5f) : 1.f);
						(void) odd;
						return h == 1 ? 1.f : w / h;
					});
					break;
				}
				case B_STRINGS: {
					s = harm([&](int h) {
						float body = 1.f + 0.6f * std::exp(-0.5f * std::pow((h * 220.f - 2500.f) / 600.f, 2.f));
						return body / std::pow((float) h, 1.15f - 0.35f * t) * std::exp(-h / (12.f + 60.f * t));
					});
					break;
				}
				case B_PLUCK: {
					float damp = 0.02f + 0.5f * t;
					s = harm([&](int h) { return std::exp(-h * damp) / h; });
					break;
				}
				default: s = harm([](int h) { return h == 1 ? 1.f : 0.f; }); break;
			}
			specs.push_back(s);
		} else {
			std::vector<float> w(FRAME);
			for (int i = 0; i < FRAME; i++) {
				float p = (float) i / FRAME, y = 0.f;
				switch (b) {
					case B_BASIC: {
						float x = t * 3.f;
						int seg = std::min((int) x, 2);
						float f = x - seg;
						float a = seg == 0 ? sn(p) : (seg == 1 ? tri(p) : saw(p + 0.5f));
						float c = seg == 0 ? tri(p) : (seg == 1 ? saw(p + 0.5f) : pulse(p, 0.5f));
						y = a + (c - a) * f;
						break;
					}
					case B_PWM: y = pulse(p, 0.5f - 0.47f * t); break;
					case B_SYNC: y = saw(p * (1.f + 7.f * t)) * (1.f - 0.3f * p); break;
					case B_FOLD: y = fold(sn(p) * (1.f + 7.f * t)); break;
					case B_FM: y = sn(p + (6.f * t / 6.2832f) * sn(p)); break;
					case B_FMBELL: y = sn(p + (4.f * t / 6.2832f) * sn(3.f * p) + (1.5f * t / 6.2832f) * sn(7.f * p)); break;
					case B_PHASEDIST: {
						float k = 0.5f - 0.48f * t;
						float q = p < k ? 0.5f * p / k : 0.5f + 0.5f * (p - k) / (1.f - k);
						y = -std::cos(2.f * PI * q);
						break;
					}
					case B_CHIP: {
						float x = t * 3.f;
						int seg = std::min((int) x, 2);
						float f = x - seg;
						auto shape = [&](int s) {
							if (s == 0) return pulse(p, 0.5f);
							if (s == 1) return pulse(p, 0.25f);
							if (s == 2) return pulse(p, 0.125f);
							return std::round(tri(p) * 7.5f) / 7.5f;
						};
						y = shape(seg) + (shape(seg + 1) - shape(seg)) * f;
						break;
					}
					case B_BITWAVE: {
						float levels = std::pow(2.f, 7.f - 6.f * t);
						y = std::round(sn(p) * levels) / levels;
						break;
					}
					case B_REESE: y = 0.5f * (saw(p) + saw(p + t)); break;
					case B_HOOVER: {
						float k = 0.5f - 0.3f * t;
						float q = p < k ? 0.5f * p / k : 0.5f + 0.5f * (p - k) / (1.f - k);
						y = 0.5f * saw(q) + 0.35f * pulse(p, 0.2f + 0.6f * t) + 0.25f * saw(2.f * p + 0.5f * t);
						break;
					}
					case B_SUB: {
						float x = t * 3.f;
						int seg = std::min((int) x, 2);
						float f = x - seg;
						auto shape = [&](int s) {
							if (s == 0) return sn(p);
							if (s == 1) return 0.75f * sn(p) + 0.35f * sn(2.f * p);
							if (s == 2) return tri(p);
							return sat(2.2f * sn(p));
						};
						y = shape(seg) + (shape(seg + 1) - shape(seg)) * f;
						break;
					}
					default: y = sn(p); break;
				}
				w[i] = y;
			}
			waves.push_back(w);
		}
	}
	if (spectral)
		T->buildFromSpectra(specs);
	else
		T->buildFromWaves(waves);
	return T;
}

/** MUTATE: a new table from the building blocks, decided by a seed (the patch keeps the seed). */
inline std::shared_ptr<Table> makeMutant(uint32_t seed) {
	using namespace gen;
	Rng r(seed * 2654435761u + 17u);
	auto T = std::make_shared<Table>();
	char nm[24];
	std::snprintf(nm, sizeof(nm), "MUTANT %04u", seed % 10000u);
	T->name = nm;
	// two random spectra (start, end) plus a random path between: harmonics, formants, folds
	int kind = (int) (r.uni() * 4.f);
	float A[2][96];
	for (int e = 0; e < 2; e++)
		for (int h = 1; h < 96; h++) {
			float base = 1.f / std::pow((float) h, 0.5f + r.uni());
			A[e][h] = r.uni() < 0.35f + 0.4f * (h < 8) ? base * (0.3f + r.uni()) : 0.f;
		}
	A[0][1] = A[1][1] = 0.7f;
	float F[2][3];
	for (int e = 0; e < 2; e++)
		for (int k = 0; k < 3; k++)
			F[e][k] = VOWELS[(int) (r.uni() * 4.99f)][k];
	float foldAmt = 1.f + 6.f * r.uni(), skew = 0.1f + 0.4f * r.uni();
	std::vector<Spec> specs;
	std::vector<std::vector<float>> waves;
	for (int fr = 0; fr < MAXFRAMES; fr++) {
		float t = (float) fr / (MAXFRAMES - 1);
		if (kind < 2) {
			float Fi[3];
			for (int k = 0; k < 3; k++)
				Fi[k] = F[0][k] + (F[1][k] - F[0][k]) * t;
			const float B[3] = {90.f, 110.f, 150.f};
			specs.push_back(harm([&](int h) {
				float a = h < 96 ? A[0][h] + (A[1][h] - A[0][h]) * t : 0.f;
				if (kind == 1)
					a = a * 0.4f + formant(110.f * h, Fi, B) / std::pow((float) h, 0.4f);
				return a;
			}, 400));
		} else {
			std::vector<float> w(FRAME);
			for (int i = 0; i < FRAME; i++) {
				float p = (float) i / FRAME;
				float k = 0.5f - skew * t;
				float q = p < k ? 0.5f * p / k : 0.5f + 0.5f * (p - k) / (1.f - k);
				float y = kind == 2 ? fold(sn(q) * (1.f + foldAmt * t)) : 0.6f * saw(q) + 0.4f * fold(tri(p) * (1.f + foldAmt * t));
				w[i] = y;
			}
			waves.push_back(w);
		}
	}
	if (kind < 2)
		T->buildFromSpectra(specs);
	else
		T->buildFromWaves(waves);
	return T;
}

// ---------------------------------------------------------------- WAV files
/** Loads a wavetable WAV (frames of `frameHint` samples, or what a 'clm ' chunk says). nullptr on failure. */
inline std::shared_ptr<Table> loadWav(const std::string& path, std::string* err = nullptr) {
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f) {
		if (err) *err = "cannot open";
		return nullptr;
	}
	std::vector<unsigned char> d;
	{
		unsigned char buf[65536];
		size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
			d.insert(d.end(), buf, buf + n);
			if (d.size() > 64u * 1024u * 1024u)
				break;
		}
		std::fclose(f);
	}
	auto u32 = [&](size_t i) { return (uint32_t) d[i] | ((uint32_t) d[i + 1] << 8) | ((uint32_t) d[i + 2] << 16) | ((uint32_t) d[i + 3] << 24); };
	auto u16 = [&](size_t i) { return (uint32_t) d[i] | ((uint32_t) d[i + 1] << 8); };
	if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) {
		if (err) *err = "not a WAV file";
		return nullptr;
	}
	int fmt = 0, ch = 1, bits = 16, frameSize = 0;
	size_t dataPos = 0, dataLen = 0;
	for (size_t p = 12; p + 8 <= d.size();) {
		uint32_t len = u32(p + 4);
		if (!std::memcmp(d.data() + p, "fmt ", 4) && p + 24 <= d.size()) {
			fmt = (int) u16(p + 8);
			ch = std::max(1, (int) u16(p + 10));
			bits = (int) u16(p + 22);
			if (fmt == 0xFFFE && p + 32 <= d.size())
				fmt = (int) u16(p + 32);  // WAVE_FORMAT_EXTENSIBLE: the sub format
		} else if (!std::memcmp(d.data() + p, "clm ", 4)) {
			std::string s(d.begin() + p + 8, d.begin() + std::min(d.size(), (size_t) (p + 8 + len)));
			size_t k = s.find("<!>");
			if (k != std::string::npos)
				frameSize = std::atoi(s.c_str() + k + 3);
		} else if (!std::memcmp(d.data() + p, "data", 4)) {
			dataPos = p + 8;
			dataLen = std::min((size_t) len, d.size() - dataPos);
		}
		p += 8 + len + (len & 1);
	}
	if (!dataPos || (fmt != 1 && fmt != 3)) {
		if (err) *err = "unsupported WAV format";
		return nullptr;
	}
	int bps = bits / 8;
	if (bps < 1 || (fmt == 3 && bits != 32)) {
		if (err) *err = "unsupported sample format";
		return nullptr;
	}
	size_t n = dataLen / (bps * ch);
	std::vector<float> x(n);
	for (size_t i = 0; i < n; i++) {
		size_t o = dataPos + i * bps * ch;
		float v = 0.f;
		if (fmt == 3) {
			uint32_t u = u32(o);
			std::memcpy(&v, &u, 4);
		} else if (bits == 16)
			v = (int16_t) u16(o) / 32768.f;
		else if (bits == 24) {
			int32_t s = (int32_t) ((d[o] << 8) | (d[o + 1] << 16) | (d[o + 2] << 24)) >> 8;
			v = s / 8388608.f;
		} else if (bits == 32)
			v = (int32_t) u32(o) / 2147483648.f;
		else if (bits == 8)
			v = (d[o] - 128) / 128.f;
		x[i] = v;
	}
	if (n < 16) {
		if (err) *err = "too short";
		return nullptr;
	}
	if (frameSize <= 0)
		frameSize = n % 2048 == 0 ? 2048 : (n % 1024 == 0 && n >= 1024 ? (n >= 2048 * 2 ? 2048 : 1024) : (n <= 8192 ? (int) n : 2048));
	int total = std::max(1, (int) (n / frameSize));
	int frames = std::min(total, MAXFRAMES);
	std::vector<std::vector<float>> waves(frames, std::vector<float>(FRAME));
	for (int fr = 0; fr < frames; fr++) {
		int src = frames == 1 ? 0 : (int) std::lround((double) fr * (total - 1) / (frames - 1));
		const float* s = x.data() + (size_t) src * frameSize;
		for (int i = 0; i < FRAME; i++) {
			float pos = (float) i * frameSize / FRAME;
			int k = (int) pos;
			float fk = pos - k;
			float a = s[k % frameSize], b = s[(k + 1) % frameSize];
			waves[fr][i] = a + (b - a) * fk;
		}
	}
	auto T = std::make_shared<Table>();
	std::string base = path;
	size_t sl = base.find_last_of("/\\");
	if (sl != std::string::npos)
		base = base.substr(sl + 1);
	size_t dot = base.find_last_of('.');
	if (dot != std::string::npos)
		base = base.substr(0, dot);
	for (char& c : base)
		c = (char) std::toupper((unsigned char) c);
	T->name = base;
	T->buildFromWaves(waves);
	return T;
}

}  // namespace neon
