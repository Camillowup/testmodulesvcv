// SPDX-License-Identifier: GPL-3.0-only
// KRATZ engine test (no Rack): a test record (an A minor chord with a beat at 120 BPM) is scratched with every
// pattern, played by the motor with the shifter, looped, slipped, filtered; the key and the tempo must be found.
// Run: g++ -O2 -std=c++11 -I. test/kratz_test.cpp -o kratz_test && ./kratz_test check
#include "../src/kratz/KratzDsp.hpp"
#include <cstdio>
#include <cstdlib>

using namespace kratz;

static int fails = 0;
#define CHECK(c, ...)                         \
	do {                                      \
		if (!(c)) {                           \
			std::printf("FAIL: " __VA_ARGS__); \
			std::printf("\n");                \
			fails++;                          \
		}                                     \
	} while (0)

static Sample testRecord(float bpm, float seconds, int rate) {
	Sample s;
	s.name = "test.wav";
	s.channels = 2;
	s.rate = (float) rate;
	s.frames = (int) (seconds * rate);
	s.data.resize((size_t) s.frames * 2);
	const float notes[3] = {57.f, 60.f, 64.f}; // A3 C4 E4
	float beat = 60.f / bpm;
	for (int i = 0; i < s.frames; i++) {
		float t = (float) i / rate;
		float v = 0.f;
		for (float n : notes) {
			float f = 440.f * std::pow(2.f, (n - 69.f) / 12.f);
			v += 0.12f * std::sin(2.f * PI * f * t) + 0.05f * std::sin(4.f * PI * f * t);
		}
		float bt = std::fmod(t, beat);
		v += 0.6f * std::exp(-bt * 40.f) * std::sin(2.f * PI * 60.f * bt); // a kick on every beat
		s.data[(size_t) i * 2] = v;
		s.data[(size_t) i * 2 + 1] = v * 0.9f;
	}
	return s;
}

int main(int argc, char** argv) {
	const float SR = 48000.f;
	Sample rec = testRecord(120.f, 8.f, 44100);

	// ---- what the sample is in
	bool minor = false;
	float conf = 0.f;
	int root = detectKey(rec, minor, conf);
	std::printf("key: %s %s (conf %.2f)\n", root >= 0 ? NOTE_NAMES[root] : "-", minor ? "minor" : "major", conf);
	CHECK(root == 9 && minor, "key should be A minor, got %d %s", root, minor ? "minor" : "major");
	float bpm = detectBpm(rec);
	std::printf("tempo: %.1f BPM\n", bpm);
	CHECK(std::fabs(bpm - 120.f) < 2.f, "tempo should be 120, got %.1f", bpm);
	for (float b : {88.f, 95.f, 128.f, 140.f}) {
		Sample t = testRecord(b, 8.f, 48000);
		float got = detectBpm(t);
		std::printf("tempo %.0f: %.1f\n", b, got);
		CHECK(std::fabs(got - b) < 2.f, "tempo should be %.0f, got %.1f", b, got);
	}
	Sample shot = testRecord(120.f, 1.f, 44100);
	CHECK(detectBpm(shot) == 0.f, "a one-shot has no tempo");

	// ---- every pattern scratches the record
	float beatSamples = SR * 60.f / 120.f;
	for (int p = P_BABY; p < NUM_PATS; p++) {
		Deck d;
		d.setSampleRate(SR);
		d.setSample(&rec);
		DeckIn in;
		in.rate = rec.rate / SR;
		in.hand = true;
		double anchor = rec.rate * 2.0;
		float stroke = rec.rate * 0.25f; // a stroke of a 16th... at 120 BPM: 0.125 s; use 0.25 s
		double e = 0.0;
		bool finite = true;
		int n = (int) (beatSamples * 4);
		for (int i = 0; i < n; i++) {
			float cyc = i / (beatSamples * 0.5f); // RATE 1/8
			int pp = resolvePattern(p, (int) cyc, 1234u);
			float x, g;
			shape(pp, cyc, x, g);
			in.handPos = anchor + x * stroke;
			in.gain = g;
			float l, r;
			d.process(in, l, r);
			if (!std::isfinite(l) || !std::isfinite(r))
				finite = false;
			e += l * l + r * r;
		}
		float rms = (float) std::sqrt(e / (2.0 * n));
		std::printf("%-14s rms %.3f\n", PAT_NAMES[p], rms);
		CHECK(finite, "%s: not finite", PAT_NAMES[p]);
		CHECK(rms > 0.01f, "%s: silent", PAT_NAMES[p]);
		CHECK(rms < 1.5f, "%s: too loud", PAT_NAMES[p]);
	}
	// the shapes stay in their range and every fader pattern closes somewhere
	for (int p = P_BABY; p < P_RANDOM; p++) {
		bool closes = false;
		for (int i = 0; i < 1000; i++) {
			float x, g;
			shape(p, i / 1000.f, x, g);
			CHECK(x >= -2.01f && x <= 1.01f, "%s: x %.2f out of range", PAT_NAMES[p], x);
			if (g < 0.5f)
				closes = true;
		}
		if (p >= P_CHIRP && p <= P_BOOMERANG)
			CHECK(closes, "%s: the fader never closes", PAT_NAMES[p]);
	}

	// ---- the motor: plays, stops (silent), REV, LOOP, SLIP, the shifter, TONE
	{
		Deck d;
		d.setSampleRate(SR);
		d.setSample(&rec);
		DeckIn in;
		in.rate = rec.rate / SR;
		in.gain = 1.f;
		in.drag = 0.01f;
		double e = 0.0;
		for (int i = 0; i < (int) SR; i++) {
			float l, r;
			d.process(in, l, r);
			e += l * l;
		}
		CHECK(e / SR > 1e-3, "the motor plays");
		CHECK(std::fabs(d.pos - (rec.rate + 0.0)) < rec.rate * 0.05, "a second of play is a second of record (%.0f)", d.pos);
		// stop: silence after the drag
		in.rate = 0.f;
		for (int i = 0; i < (int) (SR * 0.2f); i++) {
			float l, r;
			d.process(in, l, r);
		}
		e = 0.0;
		for (int i = 0; i < (int) (SR * 0.2f); i++) {
			float l, r;
			d.process(in, l, r);
			e += l * l;
		}
		CHECK(e / (SR * 0.2f) < 1e-5, "a standing record is silent (%.6f)", e / (SR * 0.2f));
		// REV
		double p0 = d.pos;
		in.rate = -rec.rate / SR;
		for (int i = 0; i < (int) (SR * 0.5f); i++) {
			float l, r;
			d.process(in, l, r);
		}
		CHECK(d.pos < p0, "REV plays backwards");
		// LOOP stays inside
		in.rate = rec.rate / SR;
		in.loop = true;
		in.loopStart = rec.rate * 1.0;
		in.loopLen = rec.rate * 0.25;
		bool inside = true;
		for (int i = 0; i < (int) (SR * 2.f); i++) {
			float l, r;
			d.process(in, l, r);
			if (i > SR * 0.1f && (d.pos < in.loopStart - 1 || d.pos > in.loopStart + in.loopLen + 1))
				inside = false;
		}
		CHECK(inside, "LOOP keeps the record inside");
		in.loop = false;
		// SLIP: hold the record for half a second, let go: it is where it would have been
		in.slip = true;
		double start = d.pos;
		in.hand = true;
		in.handPos = start;
		for (int i = 0; i < (int) (SR * 0.5f); i++) {
			float l, r;
			d.process(in, l, r);
		}
		in.hand = false;
		float l, r;
		d.process(in, l, r);
		double want = d.wrap(start + rec.rate * 0.5, in);
		CHECK(std::fabs(d.pos - want) < rec.rate * 0.02, "SLIP: %.0f should be near %.0f", d.pos, want);
		in.slip = false;
		// the shifter and the filter
		in.shiftOn = true;
		in.shift = 1.5f;
		bool fin = true;
		for (float t : {0.f, 0.2f, 0.5f, 0.8f, 1.f}) {
			in.tone = t;
			for (int i = 0; i < (int) (SR * 0.3f); i++) {
				d.process(in, l, r);
				if (!std::isfinite(l) || std::fabs(l) > 2.f)
					fin = false;
			}
		}
		CHECK(fin, "shifter and TONE stay finite");
	}
	// ---- the hand: a hard fader jump does not explode
	{
		Deck d;
		d.setSampleRate(SR);
		d.setSample(&rec);
		DeckIn in;
		in.hand = true;
		in.gain = 1.f;
		bool fin = true;
		for (int i = 0; i < 48000; i++) {
			in.handPos = (i / 4800) % 2 ? rec.rate * 3.0 : rec.rate * 1.0;
			float l, r;
			d.process(in, l, r);
			if (!std::isfinite(l) || std::fabs(l) > 2.f)
				fin = false;
		}
		CHECK(fin, "jumps stay finite");
	}
	// ---- the curves
	CHECK(faderGain(0.f, true) == 0.f && faderGain(0.1f, true) == 1.f, "sharp fader");
	CHECK(std::fabs(speedOf(0.5f) - 1.f) < 1e-6f && speedOf(0.f) < 0.55f && speedOf(1.f) > 1.9f, "speed");
	CHECK(std::fabs(rangeBeats(0.f) - 0.25f) < 1e-4f && std::fabs(rangeBeats(1.f) - 8.f) < 1e-3f, "range");

	if (fails) {
		std::printf("%d checks failed\n", fails);
		return 1;
	}
	std::printf("all checks passed\n");
	(void) argc;
	(void) argv;
	return 0;
}
