// GENRA CHOP without Rack: the generator for every style, and the analysis of a made-up sample (four chords
// played as stabs) - the chords and the key have to come out right. Build: g++ -std=c++11 -O2 -Isrc test/hh_test.cpp
// src/hh/Core.cpp src/hh/Styles.cpp src/hh/Chop.cpp src/hh/Song.cpp -o hh_test
#include "hh/Chop.hpp"
#include "hh/Song.hpp"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdlib>

using namespace hh;

static int fails = 0;
#define CHECK(c, ...)                                                                                                  \
	do {                                                                                                               \
		if (!(c)) {                                                                                                    \
			printf("FAIL: " __VA_ARGS__);                                                                              \
			printf("\n");                                                                                              \
			fails++;                                                                                                   \
		}                                                                                                              \
	} while (0)

static void chordTone(SampleData& s, size_t at, size_t len, const int* notes, int n) {
	for (size_t i = 0; i < len && at + i < s.L.size(); i++) {
		float t = i / s.sampleRate;
		float env = std::exp(-t * 2.5f) * std::min(1.f, i / 40.f);
		float v = 0.f;
		for (int k = 0; k < n; k++) {
			float f = 440.f * std::pow(2.f, (notes[k] - 69) / 12.f);
			v += std::sin(2.f * (float) M_PI * f * t) + 0.3f * std::sin(4.f * (float) M_PI * f * t);
		}
		s.L[at + i] += 0.15f * env * v;
		s.R[at + i] += 0.15f * env * v;
	}
}

int main() {
	// ---- every style generates, renders, and stays in range
	for (int st = 0; st < NUM_STYLES; st++) {
		Settings set;
		set.style = st;
		SampleInfo none;
		PadSet pads[PADS];
		Pattern p;
		generate(p, -1, set, none, pads, 1234 + st, false, STYLES[st].bpm);
		Knobs k;
		Render r;
		render(p, k, r);
		int drums = 0, bass = 0, mel = 0, pad = 0;
		for (int s = 0; s < LEN; s++) {
			for (int v = 0; v < DRUMS; v++)
				drums += r.drum[v][s].vel > 0.f;
			bass += r.bass[s].pitch >= 0;
			mel += r.mel[s].pitch >= 0;
			pad += r.pad[s].pitch[0] >= 0;
			if (r.bass[s].pitch >= 0)
				CHECK(r.bass[s].pitch >= 20 && r.bass[s].pitch <= 70, "%s: bass note %d", STYLES[st].name, r.bass[s].pitch);
			if (r.mel[s].pitch >= 0)
				CHECK(r.mel[s].pitch >= 45 && r.mel[s].pitch <= 100, "%s: melody note %d", STYLES[st].name, r.mel[s].pitch);
		}
		if (st != 25) // Drumless
			CHECK(drums > 8, "%s: only %d drum hits", STYLES[st].name, drums);
		CHECK(bass > 0 && mel > 0 && pad > 0, "%s: empty track (bass %d mel %d pad %d)", STYLES[st].name, bass, mel, pad);
		// MIDI chop: all the way up still leaves something
		for (int t = 0; t < NUM_TRACKS; t++)
			k.cut[t] = 1.f;
		render(p, k, r);
		printf("%-22s %-6s bpm %3.0f  drums %3d bass %2d mel %2d pad %2d\n", STYLES[st].name, ERA_NAMES[STYLES[st].era],
		       STYLES[st].bpm, drums, bass, mel, pad);
	}

	// ---- a sample: Fm7 | Bbm7 | Db | C, two stabs each, 0.45 s apart
	ChopSet cs;
	cs.data.sampleRate = 44100.f;
	size_t step = (size_t) (0.45f * 44100.f);
	cs.data.L.assign(step * 8, 0.f);
	cs.data.R.assign(step * 8, 0.f);
	const int CH[4][4] = {{53, 56, 60, 63}, {58, 61, 65, 68}, {61, 65, 68, 73}, {60, 64, 67, 72}};
	for (int i = 0; i < 8; i++)
		chordTone(cs.data, i * step, step, CH[i / 2], 4);
	analyse(cs, CM_TRANSIENT);
	printf("chops %d, key %s%s (%.2f)\n", cs.info.count, NOTE_NAMES[cs.info.key.root], cs.info.key.minor ? "m" : "",
	       cs.info.keyConf);
	for (int i = 0; i < cs.info.count; i++) {
		char name[16];
		chordName(cs.info.chop[i].chord, name, sizeof(name));
		printf("  pad %2d  %5.2fs  %-6s conf %.2f kind %d\n", i + 1, cs.info.chop[i].seconds, name, cs.info.chop[i].conf,
		       cs.info.chop[i].kind);
	}
	CHECK(cs.info.count >= 8, "found %d chops, want at least 8", cs.info.count);
	// the chop at the start of each stab must have the stab's chord root
	const int ROOT[4] = {5, 10, 1, 0};
	for (int i = 0; i < 8; i++) {
		size_t at = i * step;
		int best = -1;
		for (int p = 0; p < cs.info.count; p++)
			if (cs.start[p] <= at + 2048 && cs.start[p] + 2048 >= at)
				best = p;
		CHECK(best >= 0, "no chop at stab %d", i);
		if (best >= 0)
			CHECK(cs.info.chop[best].chord.valid() && cs.info.chop[best].chord.root == ROOT[i / 2],
			      "stab %d: chord root %d, want %d", i, cs.info.chop[best].chord.root, ROOT[i / 2]);
	}
	CHECK(cs.info.key.root == 5 && cs.info.key.minor, "key %d%s, want F minor", cs.info.key.root, cs.info.key.minor ? "m" : "");

	// ---- with the sample: chops sound, the bass follows them
	Settings set;
	set.style = 5;
	set.key = cs.info.key;
	PadSet pads[PADS];
	Pattern p;
	generate(p, -1, set, cs.info, pads, 99, false, 92.f);
	int chops = 0;
	for (int s = 0; s < LEN; s++)
		chops += p.chop[s].pad >= 0;
	CHECK(chops > 4, "only %d chop events", chops);
	for (int st = 0; st < NUM_STYLES; st++) {
		set.style = st;
		generate(p, -1, set, cs.info, pads, 7 + st, false, STYLES[st].bpm);
		mutateChops(p, set, cs.info, pads, 3, 1.f, STYLES[st].bpm);
	}
	// a lock keeps a pad's events through NEW
	set.style = 5;
	generate(p, T_CHOP, set, cs.info, pads, 5, false, 92.f);
	int lockPad = -1, lockStep = -1;
	for (int s = 0; s < LEN && lockPad < 0; s++)
		if (p.chop[s].pad >= 0)
			lockPad = p.chop[s].pad, lockStep = s;
	pads[lockPad].lock = true;
	generate(p, T_CHOP, set, cs.info, pads, 6, false, 92.f);
	CHECK(p.chop[lockStep].pad == lockPad, "locked pad %d gone from step %d", lockPad, lockStep);

	// ---- GENRA CHOP ABLAUF: every part from every style, every form, the bars of a song with transitions
	for (int st = 0; st < NUM_STYLES; st++) {
		set.style = st;
		Pattern src;
		generate(src, -1, set, cs.info, pads, 11 + st, false, STYLES[st].bpm);
		const SongForm& f = FORMS[autoForm(st)];
		CHECK(f.n > 0 && f.n <= 12, "form %d of style %d", autoForm(st), st);
		for (int part = 0; part < NUM_PARTS; part++)
			for (int renew = 0; renew < 32; renew += 7) {
				Pattern v;
				makePart(v, src, part, renew, set, cs.info, pads, 21 + part, STYLES[st].bpm);
				for (int k = 0; k < NUM_FORMS; k++) {
					int bars = FORMS[k].bars[part % FORMS[k].n];
					for (int b = 0; b < bars; b++) {
						Pattern q = v;
						shapeBar(q, part, b, bars, true, (part + 1) % NUM_PARTS, st, 3);
						for (int s = 0; s < LEN; s++) {
							CHECK(q.chop[s].pad < PADS, "chop pad out of range");
							for (int d = 0; d < DRUMS; d++)
								CHECK(q.drum[d][s].vel <= 1.f && q.drum[d][s].ratchet >= 1 && q.drum[d][s].ratchet <= 8,
								      "hit out of range (style %d part %d)", st, part);
							CHECK(q.bass[s].pitch < 128 && q.bass[s].len > 0.f, "bass out of range");
						}
						Render r;
						Knobs kn;
						render(q, kn, r);
					}
				}
			}
		// a BREAK has no kick; an INTRO's first bar of a long row has no bass
		Pattern br;
		makePart(br, src, P_BREAK, 0, set, cs.info, pads, 4, STYLES[st].bpm);
		int kicks = 0;
		for (int s = 0; s < LEN; s++)
			kicks += br.drum[KICK][s].vel > 0.f;
		CHECK(kicks == 0, "break with %d kicks (style %d)", kicks, st);
		Pattern in;
		makePart(in, src, P_INTRO, 0, set, cs.info, pads, 4, STYLES[st].bpm);
		shapeBar(in, P_INTRO, 0, 8, false, P_VERSE, st, 1);
		int bass = 0;
		for (int s = 0; s < STEPS; s++)
			bass += in.bass[s].pitch >= 0;
		CHECK(bass == 0, "intro bar 1 of 8 has bass (style %d)", st);
	}

	// ---- WAV out and back in
	std::vector<uint8_t> w = writeWav(cs.data, cs.start[0], cs.end[0], true, 3);
	SampleData back;
	std::string err;
	CHECK(readWav(w, back, err), "written chop does not read back: %s", err.c_str());
	printf(fails ? "%d FAILED\n" : "all good\n", fails);
	return fails ? 1 : 0;
}
