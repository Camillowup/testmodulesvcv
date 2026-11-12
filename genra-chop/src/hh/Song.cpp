// SPDX-License-Identifier: GPL-3.0-only
// GENRA CHOP ABLAUF: song parts, hip-hop song forms and transitions (see Song.hpp).
#include "Song.hpp"
#include <algorithm>

namespace hh {

const char* const PART_NAMES[NUM_PARTS] = {"INTRO", "VERSE", "HOOK", "BRIDGE", "BREAK", "OUTRO"};

// The forms in bars. Boom Bap: 16-bar verses, 8-bar hooks. Trap / Drill: an 8-bar intro and the hook first.
// Old School: short hooks and a break for the DJ. Beat tape (Lo-Fi, instrumental): short loops that come back.
const SongForm FORMS[NUM_FORMS] = {
	{"BOOM BAP", 8, {P_INTRO, P_VERSE, P_HOOK, P_VERSE, P_HOOK, P_BRIDGE, P_HOOK, P_OUTRO}, {4, 16, 8, 16, 8, 8, 8, 4}},
	{"OLD SCHOOL", 9, {P_INTRO, P_VERSE, P_HOOK, P_VERSE, P_HOOK, P_BREAK, P_VERSE, P_HOOK, P_OUTRO},
	 {8, 16, 4, 16, 4, 8, 16, 4, 8}},
	{"WEST COAST", 8, {P_INTRO, P_VERSE, P_HOOK, P_VERSE, P_HOOK, P_BRIDGE, P_HOOK, P_OUTRO}, {8, 16, 8, 16, 8, 8, 8, 8}},
	{"SOUTH · CRUNK", 9, {P_INTRO, P_HOOK, P_VERSE, P_HOOK, P_VERSE, P_HOOK, P_BREAK, P_HOOK, P_OUTRO},
	 {8, 8, 16, 8, 16, 8, 4, 8, 4}},
	{"TRAP · DRILL", 8, {P_INTRO, P_HOOK, P_VERSE, P_HOOK, P_VERSE, P_BRIDGE, P_HOOK, P_OUTRO}, {8, 8, 16, 8, 16, 8, 8, 4}},
	{"SHORT (2020s)", 5, {P_INTRO, P_HOOK, P_VERSE, P_HOOK, P_OUTRO}, {4, 8, 16, 8, 4}},
	{"BEAT TAPE", 7, {P_INTRO, P_VERSE, P_HOOK, P_VERSE, P_BREAK, P_HOOK, P_OUTRO}, {4, 8, 8, 8, 4, 8, 4}},
};

int autoForm(int style) {
	// by the 29 styles' order (Styles.cpp)
	static const int F[NUM_STYLES] = {
		1, 1, 0, 1, 2,       // 80s: Electro, Old School, Golden Age, Miami Bass, New Jack
		0, 0, 0, 2, 2, 3, 3, // 90s: East Coast, Jazz Rap, Dark NY, G-Funk, West Coast, Memphis, Dirty South
		0, 6, 2, 3, 6, 3,    // 2000s: Soul Chop, Dilla, Neptunes, Crunk, Abstract, Snap
		4, 4, 4, 4, 6, 2, 0, // 2010s: Trap, Chicago Drill, UK Drill, Cloud, Lo-Fi, Ratchet, Grimy Revival
		6, 5, 5, 5,          // 2020s: Drumless, Rage, Plugg, Drift Phonk
	};
	return F[std::max(0, std::min(NUM_STYLES - 1, style))];
}

bool trapStyle(int style) {
	const Style& st = STYLES[std::max(0, std::min(NUM_STYLES - 1, style))];
	return st.bass == BM_808 || st.hatRoll > 0.f;
}

// ---------------------------------------------------------------- helpers
static void clearDrum(Pattern& p, int v, int a, int b) {
	for (int s = a; s < b; s++)
		p.drum[v][s] = Hit();
}
static void setHit(Pattern& p, int v, int s, float vel, uint8_t ratchet = 1) {
	Hit& h = p.drum[v][s];
	h = Hit();
	h.vel = vel;
	h.prio = 0.f;
	h.ratchet = ratchet;
}
static void clearNotes(Note* ln, int a, int b) {
	for (int s = a; s < b; s++)
		ln[s] = Note();
	// a note from before that would sound into the range ends where it starts
	for (int s = std::max(0, a - 16); s < a; s++)
		if (ln[s].pitch >= 0 && s + ln[s].len > a)
			ln[s].len = std::max(0.5f, (float) (a - s));
}
static void clearPads(Pattern& p, int a, int b) {
	for (int s = a; s < b; s++)
		p.pad[s] = PadEv();
}
static void clearChops(Pattern& p, int a, int b) {
	for (int s = a; s < b; s++)
		p.chop[s] = ChopEv();
}
static void velDrum(Pattern& p, int v, float k) {
	for (int s = 0; s < LEN; s++)
		p.drum[v][s].vel = std::min(1.f, p.drum[v][s].vel * k);
}
/** Long roots: one note per bar (the bar's first note, else the chord's root), the rest goes. */
static void longRoots(Pattern& p, float vel) {
	for (int b = 0; b < BARS; b++) {
		int pitch = -1;
		for (int s = b * STEPS; s < (b + 1) * STEPS && pitch < 0; s++)
			pitch = p.bass[s].pitch;
		if (pitch < 0 && p.bar[b].valid())
			pitch = 36 + ((p.bar[b].root % 12) + 12) % 12;
		for (int s = b * STEPS; s < (b + 1) * STEPS; s++)
			p.bass[s] = Note();
		if (pitch >= 0) {
			Note& n = p.bass[b * STEPS];
			n.pitch = (int8_t) pitch;
			n.len = 15.5f;
			n.vel = vel;
		}
	}
}

// ---------------------------------------------------------------- a part from a pattern
void makePart(Pattern& out, const Pattern& src, int part, int renewMask, const Settings& set, const SampleInfo& si,
              const PadSet* pads, uint32_t seed, float bpm) {
	out = src;
	Rng rng;
	rng.seed(seed * 2246822519u + (uint32_t) (part + 1) * 3266489917u);
	for (int t = 0; t < NUM_TRACKS; t++)
		if (renewMask & (1 << t))
			generate(out, t, set, si, pads, rng.next(), true, bpm);
	bool trap = trapStyle(set.style);
	switch (part) {
		case P_INTRO: {
			// thinner drums (no toms, no percussion, hats on 8ths, no open hat), the kick only on the one and three;
			// pads up front, the melody thin. Bass, snare and the rest come in over the row (shapeBar).
			clearDrum(out, TOM, 0, LEN);
			clearDrum(out, PERC, 0, LEN);
			clearDrum(out, OH, 0, LEN);
			for (int s = 0; s < LEN; s++) {
				if (s % 2)
					out.drum[CH][s] = Hit();
				out.drum[CH][s].ratchet = 1;
				if (s % 8)
					out.drum[KICK][s] = Hit();
			}
			velDrum(out, CH, 0.75f);
			for (int s = 0; s < LEN; s++) {
				if (out.mel[s].pitch >= 0 && out.mel[s].prio > 0.45f)
					out.mel[s] = Note();
				if (out.pad[s].pitch[0] >= 0)
					out.pad[s].vel = std::min(1.f, out.pad[s].vel * 1.15f);
			}
			out.edited[T_DRUMS] = out.edited[T_MEL] = true;
			break;
		}
		case P_VERSE: {
			// room for the voice: the chops step back, the melody leaves space (stays generated: DENS still works)
			int count = 0;
			for (int s = 0; s < LEN; s++)
				count += out.chop[s].pad >= 0;
			for (int s = 0; s < LEN; s++) {
				ChopEv& c = out.chop[s];
				if (c.pad < 0)
					continue;
				if (count > 6 && c.prio > 0.55f) {
					c = ChopEv();
					count--;
					continue;
				}
				c.vel *= 0.85f;
			}
			for (int s = 0; s < LEN; s++)
				if (out.mel[s].pitch >= 0)
					out.mel[s].prio = std::min(1.f, out.mel[s].prio + 0.25f);
			break;
		}
		case P_HOOK: {
			// the chops fuller: every beat without a chop takes the one before again; claps on the snares, the
			// melody fuller, the pads up
			for (int b = 0; b < BARS; b++) {
				ChopEv last;
				for (int s = 0; s < b * STEPS; s++)
					if (out.chop[s].pad >= 0)
						last = out.chop[s];
				float ringsTo = -1.f;
				for (int s = b * STEPS; s < (b + 1) * STEPS; s++) {
					ChopEv& c = out.chop[s];
					if (c.pad >= 0) {
						last = c;
						ringsTo = s + c.len;
						continue;
					}
					if (s % 4 == 0 && s >= ringsTo && last.pad >= 0) {
						c = last;
						c.vel = std::min(1.f, last.vel * 0.85f);
						c.len = std::min(4.f, std::max(1.f, last.len));
						c.prio = 0.15f;
						c.ratchet = 1;
						ringsTo = s + c.len;
					}
				}
			}
			for (int s = 0; s < LEN; s++) {
				if (out.drum[SNARE][s].vel > 0.5f && out.drum[CLAP][s].vel <= 0.f)
					setHit(out, CLAP, s, 0.7f);
				if (out.mel[s].pitch >= 0)
					out.mel[s].prio = std::max(0.f, out.mel[s].prio - 0.2f);
				if (out.pad[s].pitch[0] >= 0)
					out.pad[s].vel = std::min(1.f, out.pad[s].vel * 1.1f);
			}
			if (!trap)
				for (int b = 1; b < BARS; b += 2)
					if (out.drum[OH][b * STEPS + 14].vel <= 0.f)
						setHit(out, OH, b * STEPS + 14, 0.55f);
			break;
		}
		case P_BRIDGE: {
			// half-time: kick on the one (and late in the bar), snare on the three; reversed chops in between,
			// long bass roots, the pads up
			clearDrum(out, KICK, 0, LEN);
			clearDrum(out, SNARE, 0, LEN);
			clearDrum(out, CLAP, 0, LEN);
			clearDrum(out, OH, 0, LEN);
			for (int b = 0; b < BARS; b++) {
				setHit(out, KICK, b * STEPS, 0.95f);
				setHit(out, KICK, b * STEPS + 10, 0.7f);
				setHit(out, SNARE, b * STEPS + 8, 0.9f);
			}
			for (int s = 0; s < LEN; s++)
				if (s % 2)
					out.drum[CH][s] = Hit();
			velDrum(out, CH, 0.6f);
			int k = 0;
			for (int s = 0; s < LEN; s++)
				if (out.chop[s].pad >= 0 && (k++ % 2))
					out.chop[s].rev = !out.chop[s].rev;
			longRoots(out, 0.75f);
			for (int s = 0; s < LEN; s++)
				if (out.pad[s].pitch[0] >= 0)
					out.pad[s].vel = std::min(1.f, out.pad[s].vel * 1.2f);
			out.edited[T_DRUMS] = out.edited[T_BASS] = true;
			break;
		}
		case P_BREAK: {
			// the drums out but for soft hats on 8ths (they leave the first half of the row too, shapeBar), the
			// sample carries; the bass changes each press: long roots, thinned, or out
			for (int v = 0; v < DRUMS; v++)
				if (v != CH)
					clearDrum(out, v, 0, LEN);
			for (int s = 0; s < LEN; s++) {
				if (s % 2)
					out.drum[CH][s] = Hit();
				out.drum[CH][s].ratchet = 1;
			}
			velDrum(out, CH, 0.5f);
			int kind = rng.below(3);
			if (kind == 0)
				longRoots(out, 0.7f);
			else if (kind == 1) {
				for (int s = 0; s < LEN; s++) {
					Note& n = out.bass[s];
					if (n.pitch < 0)
						continue;
					if (s % 8)
						n = Note();
					else
						n.len *= 0.6f, n.vel *= 0.7f;
				}
			}
			else
				clearNotes(out.bass, 0, LEN);
			for (int s = 0; s < LEN; s++) {
				if (out.mel[s].pitch >= 0 && out.mel[s].prio > 0.3f)
					out.mel[s] = Note();
				if (out.pad[s].pitch[0] >= 0)
					out.pad[s].vel = std::min(1.f, out.pad[s].vel * 1.25f);
			}
			out.edited[T_DRUMS] = out.edited[T_BASS] = out.edited[T_MEL] = true;
			break;
		}
		case P_OUTRO: {
			// the melody thin; the thinning bar by bar comes over the row (shapeBar)
			for (int s = 0; s < LEN; s++)
				if (out.mel[s].pitch >= 0 && out.mel[s].prio > 0.4f)
					out.mel[s] = Note();
			out.edited[T_DRUMS] = out.edited[T_BASS] = out.edited[T_MEL] = true;
			break;
		}
	}
}

// ---------------------------------------------------------------- one bar of a song row
void shapeBar(Pattern& p, int part, int barInRow, int rowBars, bool fill, int nextPart, int style, uint32_t seed) {
	if (rowBars < 1)
		rowBars = 1;
	int lb = barInRow % BARS, a = lb * STEPS, b = a + STEPS;
	float pos = (barInRow + 0.5f) / rowBars; // where in the row (0..1)
	bool last = barInRow == rowBars - 1;
	// ---- the part's course over the row
	if (part == P_INTRO) {
		if (pos < 0.5f) {
			// first half: no bass, no melody, no snare; first quarter no kick either
			clearNotes(p.bass, a, b);
			clearNotes(p.mel, a, b);
			clearDrum(p, SNARE, a, b);
			clearDrum(p, CLAP, a, b);
			clearDrum(p, RIM, a, b);
			if (pos < 0.25f)
				clearDrum(p, KICK, a, b);
		}
	}
	else if (part == P_BREAK) {
		if (pos < 0.5f)
			clearDrum(p, CH, a, b);
		if (last) {
			// the pickup back in
			setHit(p, SNARE, a + 13, 0.5f);
			setHit(p, SNARE, a + 14, 0.65f);
			setHit(p, SNARE, a + 15, 0.8f);
		}
	}
	else if (part == P_OUTRO) {
		if (pos > 0.25f)
			clearNotes(p.mel, a, b);
		if (pos > 0.5f) {
			clearDrum(p, CH, a, b);
			clearDrum(p, OH, a, b);
			clearDrum(p, PERC, a, b);
			clearDrum(p, TOM, a, b);
		}
		if (pos > 0.75f) {
			clearNotes(p.bass, a, b);
			clearDrum(p, SNARE, a, b);
			clearDrum(p, CLAP, a, b);
			clearDrum(p, RIM, a, b);
		}
		if (last && rowBars > 1) {
			// only the kick on the one, the chop and the pads ring out
			clearDrum(p, KICK, a + 1, b);
		}
		return; // no transition after an outro
	}
	// ---- transitions
	bool phrase = (part == P_VERSE || part == P_HOOK) && rowBars >= 16 && barInRow % 8 == 7 && !last;
	if (!(fill && last) && !phrase)
		return;
	bool light = phrase;
	Rng rng;
	rng.seed(seed * 747796405u + (uint32_t) barInRow * 2891336453u + (uint32_t) part * 97u);
	if (trapStyle(style)) {
		// hat rolls into the next part
		static const uint8_t R[4] = {2, 2, 3, 4};
		for (int i = light ? 2 : 0; i < 4; i++) {
			int s = a + 12 + i;
			setHit(p, CH, s, 0.5f + 0.13f * i, R[i]);
		}
		if (!light && (nextPart == P_HOOK || nextPart < 0)) {
			// the 808 (and the kick) stop on the last beat: the hook hits harder
			clearNotes(p.bass, a + 12, b);
			clearDrum(p, KICK, a + 12, b);
			if (nextPart == P_HOOK) {
				// a riser: the bar's chop again, backwards, over the last two beats
				ChopEv src;
				for (int s = a; s < b && src.pad < 0; s++)
					src = p.chop[s];
				for (int s = 0; s < LEN && src.pad < 0; s++)
					src = p.chop[s];
				if (src.pad >= 0) {
					clearChops(p, a + 8, b);
					ChopEv& r = p.chop[a + 8];
					r = src;
					r.rev = !src.rev;
					r.len = 8.f;
					r.vel = 0.75f;
					r.ratchet = 1;
					r.prio = 0.f;
				}
			}
		}
		return;
	}
	// Boom Bap and the rest: a snare (or tom) fill, or a stop
	int kind = light ? 0 : rng.below(nextPart == P_BREAK ? 2 : 3); // 0 snare fill, 1 stop, 2 tom fill
	if (nextPart == P_BREAK)
		kind = 1;
	if (kind == 1 && !light) {
		// stop-time: on the last beat only the sample
		for (int v = 0; v < DRUMS; v++)
			clearDrum(p, v, a + 12, b);
		clearNotes(p.bass, a + 12, b);
		if (nextPart < 0)
			clearPads(p, a + 12, b);
		return;
	}
	int voice = kind == 2 ? TOM : SNARE;
	int from = light ? 14 : (kind == 2 ? 12 : 10);
	clearDrum(p, CH, a + from, b);
	clearDrum(p, OH, a + from, b);
	for (int s = from; s < 16; s++)
		if (kind == 2 || s >= 12 || s % 2 == 0)
			setHit(p, voice, a + s, 0.5f + 0.45f * (s - from) / std::max(1, 15 - from));
}

} // namespace hh
