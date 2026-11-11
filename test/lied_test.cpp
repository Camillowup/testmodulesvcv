// Host test of LIED's singing (without Rack): the core speaks the lines, LiedText cuts them into syllables, the Singer
// sings a melody on them. Checks: the syllables of known words, the sung pitch is the note's, a held vowel lasts as
// long as its gate, CHOIR / FORMANT / effects make sound. Optionally writes the song as WAV (argv[3]).
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "../liedcore/liedcore.h"
#include "../src/lied/LiedDsp.hpp"

using namespace lied;

static const lic_api* A = nullptr;

static bool speak(const std::string& voice, int f0, const std::string& text, std::vector<float>& out, std::vector<Ph>& ph) {
	static std::vector<float> buf(22050 * 30);
	static lic_ph p[1024];
	int np = 0;
	int n = A->render(voice.c_str(), text.c_str(), 110, f0, buf.data(), (int) buf.size(), p, 1024, &np);
	if (n <= 0)
		return false;
	out.assign(buf.begin(), buf.begin() + n);
	ph.clear();
	for (int i = 0; i < np; i++)
		ph.push_back({p[i].pos, p[i].vowel != 0, p[i].name});
	return true;
}

static double pitchOf(const std::vector<float>& x, int a, int b, float sr) {
	int n = std::min(4096, b - a);
	int l0 = (int) (sr / 1000), l1 = (int) (sr / 60);
	std::vector<double> r(l1 + 1, 0.0);
	double rmax = 0;
	for (int l = l0; l <= l1; l++) {
		double c = 0, e1 = 0, e2 = 0;
		for (int i = 0; i + l < n; i++) {
			c += x[a + i] * x[a + i + l];
			e1 += x[a + i] * x[a + i];
			e2 += x[a + i + l] * x[a + i + l];
		}
		r[l] = c / std::sqrt(e1 * e2 + 1e-12);
		rmax = std::max(rmax, r[l]);
	}
	// the shortest period that correlates almost as well as the best (not a multiple of it)
	for (int l = l0 + 1; l < l1; l++)
		if (r[l] > 0.9 * rmax && r[l] >= r[l - 1] && r[l] >= r[l + 1])
			return sr / l;
	return 0;
}

static void writeWav(const char* fn, const std::vector<float>& L, const std::vector<float>& R, int sr) {
	FILE* f = fopen(fn, "wb");
	if (!f)
		return;
	int n = (int) L.size();
	int bytes = n * 4;
	auto w32 = [&](int v) { fwrite(&v, 4, 1, f); };
	auto w16 = [&](short v) { fwrite(&v, 2, 1, f); };
	fwrite("RIFF", 1, 4, f);
	w32(36 + bytes);
	fwrite("WAVEfmt ", 1, 8, f);
	w32(16);
	w16(1);
	w16(2);
	w32(sr);
	w32(sr * 4);
	w16(4);
	w16(16);
	fwrite("data", 1, 4, f);
	w32(bytes);
	for (int i = 0; i < n; i++) {
		w16((short) std::max(-32767.f, std::min(32767.f, L[i] * 32767.f)));
		w16((short) std::max(-32767.f, std::min(32767.f, R[i] * 32767.f)));
	}
	fclose(f);
}

int main(int argc, char** argv) {
	const char* path = argc > 1 ? argv[1] : "./liedcore.so";
	const char* data = argc > 2 ? argv[2] : "res/espeak-ng-data";
#ifdef _WIN32
	HMODULE h = LoadLibraryA(path);
	if (!h) {
		printf("LoadLibrary failed\n");
		return 1;
	}
	lic_get_fn get = (lic_get_fn) (void*) GetProcAddress(h, "lic_get");
#else
	void* h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!h) {
		printf("dlopen: %s\n", dlerror());
		return 1;
	}
	lic_get_fn get = (lic_get_fn) dlsym(h, "lic_get");
#endif
	A = get ? get() : nullptr;
	if (!A || A->api != LIC_API_VERSION || A->init(data) <= 0) {
		printf("core not ready\n");
		return 1;
	}
	int fails = 0;
	const int CSR = 22050;
	const int F0 = 150;
	const float T0 = (float) CSR / F0;
	auto sp = [&](const std::string& v) {
		return [=](const std::string& t, std::vector<float>& o, std::vector<Ph>& p) { return speak(v, F0, t, o, p); };
	};

	// ---------------------------------------------------------------- syllables
	struct Case {
		const char* voice;
		const char* text;
		int syl;
	} cases[] = {{"de+klatt2", "wir sin-gen durch die Nacht", 6},
	             {"de+klatt2", "Hallo Welt, ich bin da", 6},
	             {"de+f2", "Lachen und Weinen", 5},
	             {"en-us+klatt3", "hello beautiful world", 6},
	             {"de", "la la la la la", 5},
	             {"de", "sch\xc3\xb6n gr\xc3\xbc\xc3\x9f-en", 3},
	             {"de", "!!!", 0}};
	Line keep;
	for (const Case& c : cases) {
		Line L;
		bool ok = makeLine(L, c.text, sp(c.voice), CSR, T0);
		printf("%-14s \"%s\": %d syllables, %d words, whole %d samples |", c.voice, c.text, (int) L.syl.size(),
		       (int) L.word.size(), (int) L.whole.s.size());
		for (size_t i = 0; i < L.syl.size(); i++) {
			const Unit& u = L.syl[i];
			printf(" %s[%s](v %d ms)", u.text.c_str(), u.phon.c_str(), (u.v1 - u.v0) * 1000 / CSR);
		}
		printf("\n");
		if ((int) L.syl.size() != c.syl)
			fails++, printf("  ! %d syllables, wanted %d\n", (int) L.syl.size(), c.syl);
		if (ok && L.syl.size() != L.sylAt.size())
			fails++;
		for (const Unit& u : L.syl)
			if (u.marks.size() < 2 || u.v1 <= u.v0)
				fails++, printf("  ! %s: %d marks, vowel %d..%d\n", u.text.c_str(), (int) u.marks.size(), u.v0, u.v1);
		if (c.text[0] == 'w')
			keep = L;
	}

	// ---------------------------------------------------------------- singing
	const float SR = 44100.f;
	Singer s;
	s.setRate(SR, (float) CSR);
	Fx fx;
	fx.setRate(SR);
	Params p;
	p.vowel = 1.f;
	p.vibDepth = 0.f;
	std::vector<float> outL, outR;
	// a melody: one syllable per note, gate 0.45 s, 0.05 s apart; notes D4 F4 A4 G4 E4 D4
	const float notes[6] = {2, 5, 9, 7, 4, 2};
	std::vector<std::pair<int, int>> vowelSpans;  // samples where a held vowel sounds (for the pitch check)
	for (int k = 0; k < 6; k++) {
		s.note(notes[k], false);
		s.start(&keep.syl[k % keep.syl.size()], 1.f, p);
		int on = (int) (0.45f * SR), off = (int) (0.05f * SR);
		int at = (int) outL.size();
		for (int i = 0; i < on + off; i++) {
			if (i == on)
				s.release();
			float l, r;
			s.process(p, l, r);
			outL.push_back(l);
			outR.push_back(r);
		}
		vowelSpans.push_back({at + (int) (0.25f * SR), at + (int) (0.40f * SR)});
	}
	for (int k = 0; k < 6; k++) {
		double f = pitchOf(outL, vowelSpans[k].first, vowelSpans[k].second, SR);
		double want = 261.6256 * std::pow(2.0, notes[k] / 12.0);
		double cents = f > 0 ? 1200.0 * std::log2(f / want) : 9999;
		printf("note %d (%s): sung %.1f Hz, wanted %.1f Hz (%+.0f cents)\n", k, keep.syl[k].text.c_str(), f, want, cents);
		if (std::fabs(cents) > 40)
			fails++;
	}
	// held: a 1.5 s gate keeps sounding to the end, then stops
	s.note(0, false);
	s.start(&keep.syl[5], 1.f, p);  // "Nacht"
	double heldRms = 0, afterRms = 0;
	for (int i = 0; i < (int) (2.0f * SR); i++) {
		if (i == (int) (1.5f * SR))
			s.release();
		float l, r;
		s.process(p, l, r);
		outL.push_back(l);
		outR.push_back(r);
		if (i > (int) (1.3f * SR) && i < (int) (1.45f * SR))
			heldRms += l * l;
		if (i > (int) (1.85f * SR))
			afterRms += l * l;
	}
	heldRms = std::sqrt(heldRms / (0.15 * SR));
	afterRms = std::sqrt(afterRms / (0.15 * SR));
	printf("held vowel at 1.4 s: rms %.3f; after the release: %.4f\n", heldRms, afterRms);
	if (heldRms < 0.03 || afterRms > 0.01)
		fails++;
	// choir, formant, rough, breath, effects: they all sound
	p.choir = 0.8f;
	p.formant = 1.3f;
	p.rough = 0.5f;
	p.breath = 0.5f;
	p.vibDepth = 0.4f;
	double fxRms = 0;
	for (int k = 0; k < 6; k++) {
		s.note(notes[k] + 12, false);
		s.start(&keep.syl[k], 1.f, p);
		for (int i = 0; i < (int) (0.5f * SR); i++) {
			if (i == (int) (0.45f * SR))
				s.release();
			float l, r;
			s.process(p, l, r);
			fx.process(l, r, 0.4f, 0.5f, 0.4f, 0.f, 120.f);
			outL.push_back(l);
			outR.push_back(r);
			fxRms += l * l + r * r;
		}
	}
	fxRms = std::sqrt(fxRms / (6 * 0.5 * SR * 2));
	printf("choir + formant + rough + breath + echo + verb: rms %.3f\n", fxRms);
	if (fxRms < 0.03 || !std::isfinite(fxRms))
		fails++;
	// FREE: the whole line
	p = Params();
	s.note(0, false);
	s.start(&keep.whole, 1.f, p);
	s.release();
	int freeLen = 0;
	while (s.sounding() && freeLen < (int) (10 * SR)) {
		float l, r;
		s.process(p, l, r);
		outL.push_back(l);
		outR.push_back(r);
		freeLen++;
	}
	printf("FREE line: %.2f s\n", freeLen / SR);
	if (freeLen < (int) (0.8f * SR) || freeLen >= (int) (10 * SR))
		fails++;
	float pk = 0;
	for (size_t i = 0; i < outL.size(); i++)
		pk = std::max(pk, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
	printf("peak %.2f over %.1f s\n", pk, outL.size() / SR);
	if (argc > 3)
		writeWav(argv[3], outL, outR, (int) SR);
	printf("%s\n", fails ? "FAILED" : "ok");
	return fails ? 1 : 0;
}
