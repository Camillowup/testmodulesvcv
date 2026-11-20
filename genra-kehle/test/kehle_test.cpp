// SPDX-License-Identifier: GPL-3.0-only
// KEHLE – the engine test (no Rack): every factory preset vocodes a test voice (a buzz through moving vowel formants,
// syllables, S-sounds) on the built-in carrier; the test checks for silence, NaN and clipping and prints the gain that
// brings each preset to the same loudness. Then a stress run: extreme and random knob values, FREEZE, an external
// carrier, no voice at all.
//   g++ -O2 -std=c++11 -I. test/kehle_test.cpp -o kehle_test && ./kehle_test        (from genra-kehle/)
//   ./kehle_test gains   prints "KEHLE index gain" lines (tools/set_gains.py writes them into the presets)
//   ./kehle_test check   as the module plays them (with the presets' gains)
#include "../src/kehle/KehlePresets.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <cstdlib>
#include <vector>

using namespace kehle;
static const float FS = 48000.f;
static const float TARGET_RMS = 2.0f, MAX_PEAK = 5.0f;

struct Meter {
	std::vector<float> blocks;
	double ss = 0.0;
	int n = 0;
	void add(float L, float R) {
		ss += 0.5 * ((double) L * L + (double) R * R);
		if (++n >= (int) (0.05f * FS)) {
			blocks.push_back((float) std::sqrt(ss / n));
			ss = 0.0, n = 0;
		}
	}
	float value() {
		std::vector<float> b;
		for (float x : blocks)
			if (x > 0.0005f)
				b.push_back(x);
		if (b.empty())
			return 0.f;
		std::sort(b.begin(), b.end());
		return b[(size_t) (0.95f * (b.size() - 1))];
	}
};

/** A test voice: a buzz at ~130 Hz through two formants that walk through the vowels, in syllables, with S-sounds. */
struct TestVoice {
	BP f1, f2, f3;
	neon::Rng rng;
	float ph = 0.f;
	float sample(float t) {
		static const float V[5][2] = {{730, 1090}, {530, 1840}, {270, 2290}, {570, 840}, {300, 870}};  // a e i o u
		float pos = t / 0.32f;
		int k = (int) pos % 5;
		float fr = pos - std::floor(pos);
		int k2 = (k + 1) % 5;
		float a = V[k][0] + (V[k2][0] - V[k][0]) * fr * fr, b = V[k][1] + (V[k2][1] - V[k][1]) * fr * fr;
		f1.set(a, 8.f, FS);
		f2.set(b, 10.f, FS);
		f3.set(2600.f, 12.f, FS);
		ph += 130.f * (1.f + 0.01f * std::sin(t * 30.f)) / FS;
		if (ph >= 1.f)
			ph -= 1.f;
		float buzz = 2.f * ph - 1.f;
		float syl = std::sin(neon::PI * std::fmod(t, 0.32f) / 0.32f);
		float v = (f1.process(buzz) * 1.6f + f2.process(buzz) * 1.0f + f3.process(buzz) * 0.4f) * syl;
		float s = (std::fmod(t, 1.28f) > 1.15f) ? rng.bi() * 0.5f : 0.f;  // an S now and then
		return clampf((v * 2.4f + s) * 4.f, -6.f, 6.f);                     // ±5 V like a mic through Rack's Audio
	}
};

struct Result {
	float rms = 0.f, peak = 0.f;
	bool nan = false;
	double cpu = 0.0;
};

static Result playPreset(int pi, float gain) {
	const Preset& pr = PRESETS[pi];
	std::unique_ptr<Engine> e(new Engine);
	e->setSampleRate(FS);
	Params p = paramsFrom(pr.v, gain >= 0.f ? gain : pr.gain);
	const float chord[3] = {-1.f, -1.f + 3.f / 12.f, -1.f + 7.f / 12.f};
	const float chord2[3] = {-1.f + 5.f / 12.f, -1.f + 8.f / 12.f, 0.f};
	TestVoice tv;
	int N = (int) (3.2f * FS);
	Result r;
	Meter m;
	auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < N; i++) {
		float t = i / FS;
		float L, R, env;
		e->process(p, t < 1.6f ? chord : chord2, 3, false, 0.f, 0.f, tv.sample(t), L, R, env);
		if (!std::isfinite(L) || !std::isfinite(R))
			r.nan = true;
		r.peak = std::max(r.peak, std::max(std::fabs(L), std::fabs(R)));
		m.add(L, R);
	}
	r.cpu = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / N;
	r.rms = m.value();
	return r;
}

/** A sung vowel (no S): the bands must not flutter with the voice's own vibration and nothing may fizz above 6 kHz
 * (the grit of 2.0.0); a hissed S must still open SIBIL, a vowel must not. */
static int clean() {
	int bad = 0;
	for (int ch = 0; ch < NUM_CHARS; ch++) {
		std::unique_ptr<Engine> e(new Engine);
		e->setSampleRate(FS);
		Params p;
		p.character = ch;
		BP f1, f2;
		f1.set(730, 8, FS);
		f2.set(1090, 10, FS);
		float ph = 0.f, x[3] = {}, y[3] = {}, k = std::exp(-2.f * neon::PI * 6000.f / FS);
		const float notes[3] = {-1.f, -0.75f, -1.f + 7.f / 12.f};
		double hp = 0.0, tot = 0.0, emin[MAXB], emax[MAXB];
		for (int b = 0; b < MAXB; b++)
			emin[b] = 1e9, emax[b] = 0.0;
		float openVowel = 0.f, openS = 0.f;
		neon::Rng r;
		HP1 sh1, sh2;  // an S: noise above ~3 kHz (as in speech)
		sh1.set(3000.f, FS);
		sh2.set(3000.f, FS);
		for (int i = 0; i < (int) (2.5f * FS); i++) {
			ph += 130.f / FS;
			if (ph >= 1.f)
				ph -= 1.f;
			bool s = i > 2 * FS;  // the last half second: a hissed S
			float b = 2.f * ph - 1.f;
			float v = s ? sh2.process(sh1.process(r.bi())) * 4.f : (f1.process(b) * 1.6f + f2.process(b)) * 9.6f;
			float L, R, env;
			e->process(p, notes, 3, false, 0.f, 0.f, v, L, R, env);
			float in = L;
			for (int j = 0; j < 3; j++) {  // 6 kHz high-pass, three poles
				y[j] = k * (y[j] + in - x[j]);
				x[j] = in;
				in = y[j];
			}
			if (i > FS && !s) {
				hp += (double) in * in, tot += (double) L * L;
				for (int q = 0; q < e->nb; q++)
					emin[q] = std::min(emin[q], (double) e->sm2[q]), emax[q] = std::max(emax[q], (double) e->sm2[q]);
				openVowel = std::max(openVowel, e->sibOpen);
			}
			if (i > 2.3f * FS)
				openS = std::max(openS, e->sibOpen);
		}
		double top = 0.0, rip = 0.0;
		for (int q = 0; q < e->nb; q++)
			top = std::max(top, emax[q]);
		for (int q = 0; q < e->nb; q++)
			if (emax[q] > 0.1 * top)
				rip = std::max(rip, (emax[q] - emin[q]) / emax[q]);
		float hf = (float) (10.0 * std::log10(hp / (tot + 1e-12) + 1e-12));
		bool fail = rip > 0.06 || (ch != CH_WHISPER && hf > -40.f) || openVowel > 0.2f || openS < 0.8f;
		std::printf("clean %-8s ripple %.0f%%  above 6 kHz %.0f dB  SIBIL vowel %.2f / S %.2f%s\n", CHAR_NAMES[ch], 100.0 * rip, hf,
		            openVowel, openS, fail ? "  FAIL" : "");
		bad += fail;
	}
	return bad;
}

/** Extreme and random knobs, FREEZE, an external carrier, silence: never NaN, never stuck, never above the limiter. */
static int stress() {
	int bad = 0;
	neon::Rng rnd;
	for (int run = 0; run < 40; run++) {
		std::unique_ptr<Engine> e(new Engine);
		e->setSampleRate(run % 3 == 0 ? 96000.f : FS);
		Params p;
		auto pick = [&](int k) { return run < 16 ? ((run >> (k % 4)) & 1 ? 1.f : 0.f) : rnd.uni(); };
		p.character = run % NUM_CHARS;
		p.carrier = (run / 2) % NUM_CARS;
		p.bands = pick(0), p.formant = pick(1), p.reso = pick(2), p.sibil = pick(3), p.oct = pick(0), p.ens = pick(1);
		p.atk = pick(2), p.rel = pick(3), p.dry = pick(0), p.level = 1.f;
		bool ext = run % 4 == 1;
		TestVoice tv;
		float notes[6] = {-2.f, -1.f, 0.f, 0.5f, 1.f, 2.f};
		float peak = 0.f, tail = 0.f;
		bool nan = false;
		int N = (int) (4.f * FS);
		for (int i = 0; i < N; i++) {
			float t = i / FS;
			p.freeze = t > 1.5f && t < 2.2f;
			if (run >= 16 && i % 4800 == 0)
				p.formant = rnd.uni(), p.reso = rnd.uni();  // knobs moving (CV)
			float voice = t < 3.f ? tv.sample(t) : 0.f;  // then silence: the output must die away
			float cl = ext ? 5.f * std::sin(2.f * neon::PI * 110.f * t) : 0.f;
			float L, R, env;
			e->process(p, notes, run % 7, ext, cl, cl, voice, L, R, env);
			if (!std::isfinite(L) || !std::isfinite(R))
				nan = true;
			peak = std::max(peak, std::max(std::fabs(L), std::fabs(R)));
			if (t > 3.9f)
				tail = std::max(tail, std::max(std::fabs(L), std::fabs(R)));
		}
		float tr = 0.005f * std::pow(300.f, p.rel);  // the release time: the tail must have died away after 4 of them
		bool fail = nan || peak > 8.01f || (p.dry < 0.5f && tr < 0.15f && tail > 0.05f);
		if (fail || getenv("KDBG"))
			std::printf("stress %2d %-8s %-9s rel %.2f dry %.2f ens %.2f sib %.2f peak %.2f tail %.3f%s  %s\n", run, CHAR_NAMES[p.character], CAR_NAMES[p.carrier], p.rel, p.dry, p.ens, p.sibil,
			            peak, tail, nan ? " NaN" : "", fail ? "FAIL" : "");
		bad += fail;
	}
	std::printf("stress: %d of 40 failed\n", bad);
	return bad;
}

int main(int argc, char** argv) {
	bool gainsOnly = argc > 1 && !std::strcmp(argv[1], "gains");
	bool check = argc > 1 && !std::strcmp(argv[1], "check");
	int bad = 0;
	float cpu = 0.f;
	for (int i = 0; i < NUM_PRESETS; i++) {
		if (check) {
			Result c = playPreset(i, -1.f);
			bool off = c.rms < 0.6f * TARGET_RMS || c.rms > 1.3f * TARGET_RMS || c.peak > 6.0f;
			std::printf("KEHLE %2d %-16s %.2f V rms  peak %.2f V  cpu %.2f us%s%s\n", i, PRESETS[i].name, c.rms, c.peak, c.cpu,
			            off ? "  UNEVEN" : "", c.nan ? "  NaN" : "");
			bad += c.nan || c.rms < 1e-3f || c.peak > 6.0f;
			cpu = std::max(cpu, (float) c.cpu);
			continue;
		}
		Result a = playPreset(i, 0.1f);
		a.rms *= 10.f, a.peak *= 10.f;
		float g = a.rms > 1e-4f ? TARGET_RMS / a.rms : 1.f;
		if (a.peak * g > MAX_PEAK)
			g = MAX_PEAK / a.peak;
		g = std::min(std::max(g, 0.05f), 20.f);
		bad += a.rms < 1e-3f || a.nan;
		if (gainsOnly)
			std::printf("KEHLE %d %.3f\n", i, g);
		else
			std::printf("KEHLE %2d %-16s rms %.3f  peak %.2f  gain %.3f%s\n", i, PRESETS[i].name, a.rms, a.peak, g,
			            a.rms < 1e-3f ? "  SILENT" : "");
	}
	if (!gainsOnly) {
		bad += clean();
		bad += stress();
		if (check)
			std::printf("worst cpu %.2f us/sample\n", cpu);
		std::printf("%d bad\n", bad);
	}
	return bad ? 1 : 0;
}
