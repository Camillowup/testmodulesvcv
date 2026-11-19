// SPDX-License-Identifier: GPL-3.0-only
// GENRA NEON – the engine test (no Rack): every factory preset of WELLE, NEBEL, WOGE, PULS, GLAS and FLUT plays a
// short phrase; the test checks
// for silence, NaN and clipping and prints the gain that brings each preset to the same loudness (RMS), so the
// presets are even: not too hot, not too quiet.
//   g++ -O2 -std=c++11 -I.. test/neon_test.cpp -o neon_test && ./neon_test        (from genra-neon/)
//   ./neon_test gains   prints only "module index gain" lines (tools/set_gains.py writes them into the presets)
#include "../src/welle/WellePresets.hpp"
#include "../src/nebel/NebelPresets.hpp"
#include "../src/woge/WogePresets.hpp"
#include "../src/puls/PulsPresets.hpp"
#include "../src/glas/GlasPresets.hpp"
#include "../src/flut/FlutPresets.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>

static const float FS = 48000.f;
static const float TARGET_RMS = 2.0f;  // volts: loud and even (peaks stay under the limiter's knee of 6 V)
static const float MAX_PEAK = 5.0f;  // under the soft limiter (knee 6 V, ceiling 8 V), with room for accents

/** Loudness: RMS of 50 ms blocks, the 95th percentile (how loud it hits; plucks are short) of the blocks that are not silent. */
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

struct Result {
	float rms = 0.f, peak = 0.f;
	bool nan = false;
	double cpu = 0.0;  // µs per sample
};

static Result playWelle(int pi, float gain) {
	using namespace welle;
	const Preset& pr = PRESETS[pi];
	static std::shared_ptr<neon::Table> cache[neon::NUM_BUILTINS];
	if (!cache[pr.tA])
		cache[pr.tA] = neon::makeBuiltin(pr.tA);
	if (!cache[pr.tB])
		cache[pr.tB] = neon::makeBuiltin(pr.tB);
	std::unique_ptr<Engine> e(new Engine);
	e->setSampleRate(FS);
	e->tab[0] = cache[pr.tA].get();
	e->tab[1] = cache[pr.tB].get();
	Params p = pr.params();
	if (gain >= 0.f)
		p.outGain = gain;  // < 0: the preset's own gain
	welle::Channel ch[3];
	bool poly = p.mode == welle::MODE_POLY;
	const float chord[3] = {0.f, 3.f / 12.f, 7.f / 12.f};      // C4 minor
	const float line[4] = {-1.f, -1.f + 3.f / 12.f, -1.f + 7.f / 12.f, -1.f + 10.f / 12.f};
	int N = (int) (2.4f * FS);
	Result r;
	Meter m;
	auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < N; i++) {
		float t = i / FS;
		int n;
		if (poly) {
			n = 3;
			for (int c = 0; c < 3; c++) {
				ch[c].voct = chord[c] + (pr.octA < 0.f || p.sub > 0.3f ? -1.f : 0.f);
				ch[c].gate = std::fmod(t, 0.6f) < 0.45f && t < 1.8f;
				ch[c].vel = 0.8f;
				ch[c].acc = std::fmod(t, 1.2f) < 0.6f;
			}
		} else {
			n = 1;
			int k = (int) (t / 0.3f) % 4;
			ch[0].voct = line[k];
			ch[0].gate = std::fmod(t, 0.3f) < 0.22f && t < 1.8f;
			ch[0].vel = 0.8f;
			ch[0].acc = k == 2;
			ch[0].slide = k == 1;
		}
		float L, R, env, mot;
		e->process(p, ch, n, !poly, 125.f, 0.f, L, R, env, mot);
		if (!std::isfinite(L) || !std::isfinite(R))
			r.nan = true;
		r.peak = std::max(r.peak, std::max(std::fabs(L), std::fabs(R)));
		m.add(L, R);
	}
	r.cpu = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / N;
	r.rms = m.value();
	return r;
}

static Result playNebel(int pi, float gain) {
	using namespace nebel;
	const Preset& pr = PRESETS[pi];
	std::unique_ptr<Engine> e(new Engine);
	e->setSampleRate(FS);
	Params p = pr.params();
	if (gain >= 0.f)
		p.outGain = gain;
	nebel::Channel ch[4];
	const float chordA[4] = {-1.f, -1.f + 3.f / 12.f, -1.f + 7.f / 12.f, -1.f + 10.f / 12.f};
	const float chordB[4] = {-1.f, -0.75f, -0.5f, -0.25f};  // a second voicing: peaks differ with the notes
	int N = (int) (8.f * FS);
	Result r;
	Meter m;
	auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < N; i++) {
		float t = i / FS;
		int n = p.chordSrc == SRC_GENRA ? 4 : 1;
		bool second = t >= 4.f;
		const float* chord = second ? chordB : chordA;
		float tt = second ? t - 4.f : t;
		for (int c = 0; c < n; c++) {
			ch[c].voct = p.chordSrc == SRC_GENRA ? chord[c] : (second ? -0.5f : -1.f);
			ch[c].gate = tt < 2.6f;
			ch[c].vel = 0.8f;
			ch[c].acc = false;
		}
		float L, R, env, orb;
		e->process(p, ch, n, 125.f, 0.f, 0.f, L, R, env, orb);
		if (!std::isfinite(L) || !std::isfinite(R))
			r.nan = true;
		r.peak = std::max(r.peak, std::max(std::fabs(L), std::fabs(R)));
		m.add(L, R);
	}
	r.cpu = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / N;
	r.rms = m.value();
	return r;
}


static const float CHORD3[3] = {-1.f, -1.f + 3.f / 12.f, -1.f + 7.f / 12.f};  // C3 minor
static const float LINE4[4] = {-1.f, -1.f + 3.f / 12.f, -1.f + 7.f / 12.f, -1.f + 10.f / 12.f};

/** The phrase the poly / mono presets play: a chord on and off, or a line with an accent and a slide. */
static int phrase(neon::Channel* ch, bool poly, float t, float stop) {
	if (poly) {
		for (int c = 0; c < 3; c++) {
			ch[c].voct = CHORD3[c];
			ch[c].gate = std::fmod(t, 0.6f) < 0.45f && t < stop;
			ch[c].vel = 0.8f;
			ch[c].acc = std::fmod(t, 1.2f) < 0.6f;
			ch[c].slide = false;
		}
		return 3;
	}
	int k = (int) (t / 0.3f) % 4;
	ch[0].voct = LINE4[k];
	ch[0].gate = std::fmod(t, 0.3f) < 0.22f && t < stop;
	ch[0].vel = 0.8f;
	ch[0].acc = k == 2;
	ch[0].slide = k == 1;
	return 1;
}

static Result playWoge(int pi, float gain) {
	using namespace woge;
	const Preset& pr = PRESETS[pi];
	std::unique_ptr<Engine> e(new Engine);
	e->setSampleRate(FS);
	Params p = paramsFrom(pr.v, gain >= 0.f ? gain : pr.gain);
	neon::Channel ch[3];
	bool poly = p.mode == neon::MODE_POLY;
	int N = (int) (2.4f * FS);
	Result r;
	Meter m;
	auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < N; i++) {
		int n = phrase(ch, poly, i / FS, 1.8f);
		float L, R, env, sh;
		e->process(p, ch, n, !poly, 125.f, 0.f, 0.f, L, R, env, sh);
		if (!std::isfinite(L) || !std::isfinite(R))
			r.nan = true;
		r.peak = std::max(r.peak, std::max(std::fabs(L), std::fabs(R)));
		m.add(L, R);
	}
	r.cpu = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / N;
	r.rms = m.value();
	return r;
}

static Result playPuls(int pi, float gain) {
	using namespace puls;
	const Preset& pr = PRESETS[pi];
	std::unique_ptr<Engine> e(new Engine);
	e->setSampleRate(FS);
	Params p = paramsFrom(pr.v, gain >= 0.f ? gain : pr.gain);
	makeMask(p.pattern, p.dens, p.rot, 1, e->mask);
	bool note = p.pmode == M_NOTE;
	p.gated = note;
	neon::Channel ch[3];
	bool poly = p.vmode == neon::MODE_POLY;
	int N = (int) (2.4f * FS);
	Result r;
	Meter m;
	neon::Tempo tp;
	auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < N; i++) {
		int n;
		if (note)
			n = phrase(ch, poly, i / FS, 1.8f);
		else {
			n = 1;
			ch[0].voct = 0.f;
			ch[0].gate = false;
		}
		tp.step(1.f / FS, false, false, false, 4);
		float L, R, env, po;
		e->process(p, ch, n, !poly, 125.f, tp.beats * 125.0 / 120.0, false, 0.f, L, R, env, po);
		if (!std::isfinite(L) || !std::isfinite(R))
			r.nan = true;
		r.peak = std::max(r.peak, std::max(std::fabs(L), std::fabs(R)));
		m.add(L, R);
	}
	r.cpu = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / N;
	r.rms = m.value();
	return r;
}

static Result playGlas(int pi, float gain) {
	using namespace glas;
	const Preset& pr = PRESETS[pi];
	std::unique_ptr<Engine> e(new Engine);
	std::unique_ptr<Canvas> cv(new Canvas);
	cv->paint((int) pr.v[P_BANK], presetSeed(pi));
	e->setSampleRate(FS);
	e->canvas = cv.get();
	Params p = paramsFrom(pr.v, gain >= 0.f ? gain : pr.gain);
	neon::Channel ch[3];
	int N = (int) (4.f * FS);
	Result r;
	Meter m;
	auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < N; i++) {
		int n = phrase(ch, true, i / FS, 3.6f);
		if (p.playMode == PM_DRONE)
			for (int c = 0; c < 3; c++)
				ch[c].gate = true;
		float L, R, env, br;
		e->process(p, ch, n, 125.f, false, 0.f, L, R, env, br);
		if (!std::isfinite(L) || !std::isfinite(R))
			r.nan = true;
		r.peak = std::max(r.peak, std::max(std::fabs(L), std::fabs(R)));
		m.add(L, R);
	}
	r.cpu = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / N;
	r.rms = m.value();
	return r;
}

static Result playFlut(int pi, float gain) {
	using namespace flut;
	const Preset& pr = PRESETS[pi];
	std::unique_ptr<Engine> e(new Engine);
	e->setSampleRate(FS);
	Params p = paramsFrom(pr.v, gain >= 0.f ? gain : pr.gain);
	e->tidePh = 0.5;  // measured at high tide (the loudest), with the envelope open
	e->env = 1.f;
	neon::Channel ch[3];
	int N = (int) (4.f * FS);
	Result r;
	Meter m;
	float poly[6];
	auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < N; i++) {
		int n = phrase(ch, true, i / FS, 99.f);
		for (int c = 0; c < 3; c++)
			ch[c].gate = true;
		float L, R, tide, sub;
		e->process(p, ch, n, true, 125.f, false, 0.f, 0.f, L, R, tide, sub, poly);
		if (!std::isfinite(L) || !std::isfinite(R))
			r.nan = true;
		r.peak = std::max(r.peak, std::max(std::fabs(L), std::fabs(R)));
		m.add(L, R);
	}
	r.cpu = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / N;
	r.rms = m.value();
	return r;
}

template <typename F>
static int run(const char* mod, int count, F play, bool gainsOnly, float& worstCpu, bool check = false) {
	int bad = 0;
	for (int i = 0; i < count; i++) {
		if (check) {  // as the module plays it (with the preset's gain)
			Result c = play(i, -1.f);
			bool off = c.rms < 0.6f * TARGET_RMS || c.rms > 1.3f * TARGET_RMS || c.peak > 6.0f;
			std::printf("%-6s %2d  %.2f V rms  peak %.2f V%s%s\n", mod, i, c.rms, c.peak, off ? "  UNEVEN" : "", c.nan ? "  NaN" : "");
			bad += c.nan || c.rms < 1e-3f;
			continue;
		}
		// measured quietly (0.1: the limiter does not touch it), so level and peak scale exactly with the gain
		Result a = play(i, 0.1f);
		a.rms *= 10.f, a.peak *= 10.f;
		float g = a.rms > 1e-4f ? TARGET_RMS / a.rms : 1.f;
		// the peak scales with the gain (before the limiter): keep it under MAX_PEAK
		if (a.peak * g > MAX_PEAK)
			g = MAX_PEAK / a.peak;  // strictly: no preset reaches the limiter in normal playing
		g = std::min(std::max(g, 0.05f), 20.f);
		Result b = play(i, g);
		worstCpu = std::max(worstCpu, (float) b.cpu);
		bool silent = a.rms < 1e-3f;
		if (silent || a.nan || b.nan)
			bad++;
		if (gainsOnly)
			std::printf("%s %d %.3f\n", mod, i, g);
		else
			std::printf("%-6s %2d  rms %.3f -> %.2f V  peak %.2f V  crest %.1f  gain %.3f  cpu %.2f us%s%s\n", mod, i, a.rms, b.rms,
			            b.peak, a.peak / std::max(a.rms, 1e-4f), g, b.cpu, silent ? "  SILENT" : "", (a.nan || b.nan) ? "  NaN" : "");
	}
	return bad;
}

int main(int argc, char** argv) {
	bool gainsOnly = argc > 1 && !std::strcmp(argv[1], "gains");
	bool check = argc > 1 && !std::strcmp(argv[1], "check");
	float cpu = 0.f;
	int bad = run("WELLE", welle::NUM_PRESETS, playWelle, gainsOnly, cpu, check);
	bad += run("NEBEL", nebel::NUM_PRESETS, playNebel, gainsOnly, cpu, check);
	bad += run("WOGE", woge::NUM_PRESETS, playWoge, gainsOnly, cpu, check);
	bad += run("PULS", puls::NUM_PRESETS, playPuls, gainsOnly, cpu, check);
	bad += run("GLAS", glas::NUM_PRESETS, playGlas, gainsOnly, cpu, check);
	bad += run("FLUT", flut::NUM_PRESETS, playFlut, gainsOnly, cpu, check);
	if (!gainsOnly)
		std::printf("worst cpu %.2f us/sample, %d bad\n", cpu, bad);
	return bad ? 1 : 0;
}
