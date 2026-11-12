// SPDX-License-Identifier: GPL-3.0-only
// GENRA HIP-HOP: the generator and the live rendering (see Core.hpp).
#include "Core.hpp"
#include <algorithm>
#include <cstdio>

namespace hh {

const char* const QUALITY_NAMES[NUM_QUALITIES] = {"", "m", "7", "maj7", "m7", "m9", "dim", "sus4"};
const int QUALITY_TONES[NUM_QUALITIES][5] = {
	{0, 4, 7, -1, -1}, {0, 3, 7, -1, -1}, {0, 4, 7, 10, -1}, {0, 4, 7, 11, -1},
	{0, 3, 7, 10, -1}, {0, 3, 7, 10, 14}, {0, 3, 6, -1, -1}, {0, 5, 7, -1, -1},
};
const char* const NOTE_NAMES[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
const char* const BASS_MODES[4] = {"FOLLOW", "ROOT+5", "WALK", "808"};
const char* const MEL_MODES[4] = {"STAB", "LEAD", "ARP", "RIFF"};
const char* const PAD_MODES[3] = {"SUSTAIN", "STAB", "SWELL"};
const char* const DRUM_MODES[3] = {"KIT", "BREAK", "BOTH"};

static const int SCALES[NUM_SCALES][7] = {
	{0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 11}, {0, 1, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 8, 11},
};

static int pcOf(int n) { return ((n % 12) + 12) % 12; }

void chordName(const Chord& c, char* buf, int len) {
	if (!c.valid())
		snprintf(buf, len, "-");
	else
		snprintf(buf, len, "%s%s", NOTE_NAMES[pcOf(c.root)], QUALITY_NAMES[c.quality]);
}

void keyName(const Key& k, char* buf, int len) { snprintf(buf, len, "%s%s", NOTE_NAMES[pcOf(k.root)], k.minor ? "m" : ""); }

int scaleStep(int scale, int d) {
	int oct = d >= 0 ? d / 7 : -((-d + 6) / 7);
	int i = d - oct * 7;
	return SCALES[scale][i] + 12 * oct;
}

void Pattern::clear() { *this = Pattern(); }

// ---------------------------------------------------------------- harmony helpers
/** The scale a style plays in a key (a major key with a minor style plays major, and the other way round). */
static int scaleFor(const Style& st, const Key& k) {
	bool styleMinor = st.scale != SC_MAJOR;
	if (k.minor == styleMinor)
		return st.scale;
	return k.minor ? SC_MINOR : SC_MAJOR;
}

/** The chord on scale degree d (1-based) in colour 0/1/2. */
static Chord degreeChord(int scale, int keyRoot, int d, int color) {
	int i = d - 1;
	int r = scaleStep(scale, i);
	int third = scaleStep(scale, i + 2) - r;
	int fifth = scaleStep(scale, i + 4) - r;
	int sev = scaleStep(scale, i + 6) - r;
	Chord c;
	c.root = pcOf(keyRoot + r);
	if (fifth == 6)
		c.quality = Q_DIM;
	else if (third == 4)
		c.quality = color == 0 ? Q_MAJ : (sev == 10 ? Q_DOM7 : Q_MAJ7);
	else
		c.quality = color == 0 ? Q_MIN : (color == 2 && sev == 10 ? Q_MIN9 : Q_MIN7);
	return c;
}

/** Chord tones of a chord as pitch classes (up to 5); returns the count. */
static int chordTones(const Chord& c, int* out) {
	int n = 0;
	if (!c.valid())
		return 0;
	for (int i = 0; i < 5; i++)
		if (QUALITY_TONES[c.quality][i] >= 0)
			out[n++] = c.root + QUALITY_TONES[c.quality][i];
	return n;
}

/** The MIDI note of pitch class pc nearest to `near`. */
static int nearest(int pc, int near) {
	int n = near - pcOf(near) + pcOf(pc);
	if (n - near > 6)
		n -= 12;
	if (near - n > 6)
		n += 12;
	return n;
}

/** The note of the key's scale nearest to n. */
static int snapScale(int n, int scale, int keyRoot) {
	for (int d = 0; d < 7; d++) {
		for (int s = -1; s <= 1; s += 2) {
			int m = n + d * s;
			int rel = pcOf(m - keyRoot);
			for (int i = 0; i < 7; i++)
				if (SCALES[scale][i] == rel)
					return m;
		}
	}
	return n;
}

int scaleOf(const Settings& set) { return scaleFor(STYLES[std::max(0, std::min(NUM_STYLES - 1, set.style))], set.key); }
bool inScale(int note, int scale, int keyRoot) {
	int rel = pcOf(note - keyRoot);
	for (int i = 0; i < 7; i++)
		if (SCALES[scale][i] == rel)
			return true;
	return false;
}
int snapToScale(int note, int scale, int keyRoot) { return snapScale(note, scale, keyRoot); }
int tonesOf(const Chord& c, int* out) { return chordTones(c, out); }

Chord padChord(const SampleInfo& si, const PadSet* ps, int pad) {
	Chord c;
	c.quality = Q_NONE;
	if (pad < 0 || pad >= PADS)
		return c;
	if (ps && ps[pad].chordQ >= NUM_QUALITIES)
		return c; // set by hand: no chord
	if (ps && ps[pad].chordQ >= 0) {
		c.root = ps[pad].chordRoot;
		c.quality = ps[pad].chordQ;
		return c;
	}
	if (pad < si.count && si.chop[pad].conf > 0.15f)
		return si.chop[pad].chord;
	return c;
}

Chord chordAt(const Pattern& p, const SampleInfo& si, const PadSet* pads, int s) {
	s = ((s % LEN) + LEN) % LEN;
	if (si.count > 0) {
		for (int back = 0; back < STEPS; back++) {
			int i = ((s - back) % LEN + LEN) % LEN;
			const ChopEv& e = p.chop[i];
			if (e.pad < 0)
				continue;
			if (back < e.len + 0.5f) {
				Chord c = padChord(si, pads, e.pad);
				if (c.valid()) {
					c.root = pcOf(c.root + e.pitch + (pads ? pads[e.pad].pitch : 0));
					return c;
				}
			}
			break;
		}
	}
	return p.bar[s / STEPS];
}

// ---------------------------------------------------------------- drums
static void genDrums(Pattern& p, const Style& st, Rng& rng) {
	Hit A[DRUMS][STEPS];
	for (int v = 0; v < DRUMS; v++) {
		const char* s = st.drum[v];
		int n = (int) strlen(s);
		for (int i = 0; i < STEPS; i++) {
			char c = i < n ? s[i] : '.';
			Hit h;
			if (c == 'X')
				h.vel = 1.f, h.prio = 0.f;
			else if (c == 'x')
				h.vel = 0.8f, h.prio = 0.1f;
			else if (c == 'o')
				h.vel = 0.45f, h.prio = 0.55f;
			A[v][i] = h;
		}
	}
	// what DENS adds on top: kick pushes, hat ghosts, an open hat, rim ghosts
	static const int KICK_EXTRA[] = {3, 7, 9, 11, 14, 15};
	for (int k : KICK_EXTRA)
		if (A[KICK][k].vel == 0.f && rng.chance(0.4f))
			A[KICK][k].vel = rng.range(0.5f, 0.75f), A[KICK][k].prio = rng.range(0.6f, 0.9f);
	for (int i = 1; i < STEPS; i += 2)
		if (A[CH][i].vel == 0.f && A[CH][i - 1].vel > 0.f && rng.chance(0.6f))
			A[CH][i].vel = rng.range(0.25f, 0.4f), A[CH][i].prio = rng.range(0.7f, 0.95f);
	if (A[OH][14].vel == 0.f && rng.chance(0.5f))
		A[OH][14].vel = 0.6f, A[OH][14].prio = 0.75f;
	if (A[SNARE][4].vel > 0.f || A[SNARE][8].vel > 0.f) {
		int g = rng.chance(0.5f) ? 7 : 15;
		if (A[SNARE][g].vel == 0.f)
			A[SNARE][g].vel = 0.35f, A[SNARE][g].prio = 0.65f;
	}
	// the four bars: A A A A+fill (bar 2 a little different)
	for (int b = 0; b < BARS; b++)
		for (int v = 0; v < DRUMS; v++)
			for (int i = 0; i < STEPS; i++) {
				Hit h = A[v][i];
				if (h.vel > 0.f) {
					h.vel = std::min(1.f, std::max(0.1f, h.vel + rng.range(-0.07f, 0.07f)));
					h.off = rng.range(-st.humanize, st.humanize);
					if (st.dilla > 0.f) {
						if (v == SNARE || v == CLAP)
							h.off += st.dilla * rng.range(0.15f, 0.4f);
						else if (v == KICK)
							h.off += st.dilla * rng.range(-0.3f, 0.2f);
						else
							h.off += st.dilla * rng.range(-0.25f, 0.25f);
					}
					h.off = std::max(-0.45f, std::min(0.45f, h.off));
					if (v == CH && st.hatRoll > 0.f && rng.chance(st.hatRoll * (b == 3 ? 0.6f : 0.3f)))
						h.ratchet = (uint8_t) (2 + rng.below(3)), h.prio = std::min(h.prio, 0.35f);
				}
				p.drum[v][b * STEPS + i] = h;
			}
	// bar 2: one kick moves
	if (rng.chance(0.6f)) {
		int from = -1;
		for (int i = 9; i < STEPS; i++)
			if (p.drum[KICK][STEPS + i].vel > 0.f && p.drum[KICK][STEPS + i].prio > 0.f)
				from = i;
		if (from > 0 && from + 1 < STEPS) {
			std::swap(p.drum[KICK][STEPS + from], p.drum[KICK][STEPS + from + 1]);
		}
	}
	// bar 4: a fill
	int b4 = 3 * STEPS;
	if (rng.chance(0.75f)) {
		int kind = rng.below(3);
		for (int i = 12; i < STEPS; i++) {
			Hit& s = p.drum[SNARE][b4 + i];
			if (kind == 0 && s.vel == 0.f && rng.chance(0.6f))
				s.vel = rng.range(0.4f, 0.8f), s.prio = 0.3f;
			if (kind == 1) {
				Hit& k = p.drum[KICK][b4 + i];
				if (k.vel == 0.f && rng.chance(0.5f))
					k.vel = 0.7f, k.prio = 0.3f;
			}
			if (kind == 2) {
				Hit& t = p.drum[TOM][b4 + i];
				t.vel = rng.range(0.6f, 0.9f), t.prio = 0.35f;
			}
		}
	}
	// a crash-ish open hat at the top of bar 1 (in the next cycle) now and then
	if (rng.chance(0.3f))
		p.drum[OH][0].vel = 0.7f, p.drum[OH][0].prio = 0.5f;
}

// ---------------------------------------------------------------- chords per bar
static void genChords(Pattern& p, const Style& st, const Settings& set, const SampleInfo& si, const PadSet* pads,
                      Rng& rng) {
	int scale = scaleFor(st, set.key);
	int deg[BARS] = {1, 1, 1, 1};
	sscanf(st.prog, "%d %d %d %d", &deg[0], &deg[1], &deg[2], &deg[3]);
	// a little variety: bar 4 sometimes turns elsewhere, bar 2 sometimes swaps with bar 3
	if (rng.chance(0.3f)) {
		static const int TURN[] = {5, 7, 4, 2};
		deg[3] = TURN[rng.below(4)];
	}
	if (rng.chance(0.2f))
		std::swap(deg[1], deg[2]);
	for (int b = 0; b < BARS; b++)
		p.bar[b] = degreeChord(scale, set.key.root, deg[b], st.color);
	// with a sample: the chop that sounds longest in a bar gives its chord
	if (si.count > 0) {
		for (int b = 0; b < BARS; b++) {
			float best = 0.f;
			Chord bc;
			bc.quality = Q_NONE;
			for (int i = 0; i < STEPS; i++) {
				const ChopEv& e = p.chop[b * STEPS + i];
				if (e.pad < 0)
					continue;
				Chord c = padChord(si, pads, e.pad);
				if (c.valid() && e.len > best) {
					best = e.len;
					bc = c;
					bc.root = pcOf(c.root + e.pitch + (pads ? pads[e.pad].pitch : 0));
				}
			}
			if (bc.valid())
				p.bar[b] = bc;
		}
	}
}

// ---------------------------------------------------------------- bass
static void addNote(Note* line, int s, int pitch, float len, float vel, float prio) {
	if (s < 0 || s >= LEN)
		return;
	Note& n = line[s];
	n.pitch = (int8_t) std::max(0, std::min(127, pitch));
	n.len = len;
	n.vel = vel;
	n.prio = prio;
	n.off = 0.f;
	n.acc = false;
	n.slide = false;
}

static int bassNote(int pc) {
	int n = 36 + pcOf(pc);
	if (n > 42)
		n -= 12;
	return n;
}

/** What DENS brings to the bass, and how long its notes ring.
 *  - at least two notes per bar (none of the bars stays almost empty)
 *  - extras with graded priority: a push before the snare (0.3-0.45), octave jumps (0.5-0.65),
 *    walk-ups into the next chord (0.6-0.75), 16th ghosts (0.8-0.95)
 *  - the core notes ring up to the next core note (Premier-style East Coast stays short) */
static void bassLayer(Pattern& p, const Style& st, const Settings& set, const SampleInfo& si, const PadSet* pads,
                      Rng& rng, int mode, int scale) {
	auto empty = [&](int s) { return s >= 0 && s < LEN && p.bass[s].pitch < 0; };
	auto rootAt = [&](int s) {
		Chord c = chordAt(p, si, pads, s);
		if (!c.valid())
			c = p.bar[s / STEPS];
		return bassNote(c.root);
	};
	// at least two core notes per bar
	for (int b = 0; b < BARS; b++) {
		int core = 0;
		for (int i = 0; i < STEPS; i++)
			core += p.bass[b * STEPS + i].pitch >= 0 && p.bass[b * STEPS + i].prio == 0.f;
		static const int FILL[] = {8, 10, 6, 12};
		for (int k = 0; k < 4 && core < 2; k++) {
			int s = b * STEPS + FILL[k];
			if (empty(s)) {
				addNote(p.bass, s, rootAt(s) + (rng.chance(0.3f) ? 7 : 0), 1.f, 0.8f, 0.f);
				core++;
			}
		}
	}
	// the extras, the same in every bar (a groove), bar 4 gets its own walk-up
	int push = rng.chance(0.5f) ? 7 : 11; // the 16th before a snare / the "a" of 3
	float pushPrio = rng.range(0.3f, 0.45f);
	int jump = 1 + rng.below(3);
	float jumpPrio = rng.range(0.5f, 0.65f);
	for (int b = 0; b < BARS; b++) {
		int s0 = b * STEPS;
		if (empty(s0 + push))
			addNote(p.bass, s0 + push, rootAt(s0 + push) + (rng.chance(0.4f) ? 7 : 0), 0.9f, 0.7f, pushPrio);
		if (p.bass[s0].pitch >= 0 && empty(s0 + jump * 2)) {
			addNote(p.bass, s0 + jump * 2, p.bass[s0].pitch + 12, 0.8f, 0.65f, jumpPrio);
			if (mode == BM_808)
				p.bass[s0 + jump * 2].slide = rng.chance(0.5f);
		}
		// walk-up into the next bar's chord (bars 2 and 4, with DENS)
		if (b % 2 == 1) {
			int target = rootAt((s0 + STEPS) % LEN);
			for (int k = 0; k < 3; k++) {
				int s = s0 + 13 + k;
				if (!empty(s))
					continue;
				int n = snapScale(target - (3 - k) * 2, scale, set.key.root);
				addNote(p.bass, s, n, 0.8f, 0.6f, 0.6f + 0.05f * k);
			}
		}
		// 16th ghosts on the empty 8ths
		for (int i = 2; i < STEPS; i += 2) {
			int s = s0 + i;
			if (empty(s) && empty(s - 1) && rng.chance(0.5f)) {
				int prev = s - 1;
				while (prev > s0 && p.bass[prev].pitch < 0)
					prev--;
				int n = p.bass[prev].pitch >= 0 ? p.bass[prev].pitch : rootAt(s);
				addNote(p.bass, s, n + (rng.chance(0.3f) ? 12 : 0), 0.6f, 0.5f, rng.range(0.8f, 0.95f));
			}
		}
	}
	// how long the core notes ring: up to the next core note
	bool shortStyle = st.bass == BM_ROOTS && st.chopLoop < 0.4f; // East Coast: short and punchy
	for (int s = 0; s < LEN; s++) {
		Note& n = p.bass[s];
		if (n.pitch < 0 || n.prio > 0.f)
			continue;
		int nx = s + 1;
		while (nx < s + STEPS && (p.bass[nx % LEN].pitch < 0 || p.bass[nx % LEN].prio > 0.f))
			nx++;
		float gap = (float) (nx - s);
		if (mode == BM_808)
			n.len = std::max(n.len, std::min(gap, 8.f) - 0.2f);
		else if (shortStyle)
			n.len = std::min(gap, 2.f) - 0.15f;
		else
			n.len = std::min(gap, 6.f) - 0.25f;
	}
}

static void genBass(Pattern& p, const Style& st, const Settings& set, const SampleInfo& si, const PadSet* pads,
                    Rng& rng) {
	for (int s = 0; s < LEN; s++)
		p.bass[s] = Note();
	int mode = st.bass;
	bool fifths = false;
	switch (set.bassMode) {
		case 1: mode = BM_ROOTS, fifths = true; break;
		case 2: mode = BM_WALK; break;
		case 3: mode = BM_808; break;
		default: break;
	}
	int scale = scaleFor(st, set.key);
	if (mode == BM_ROOTS || mode == BM_808) {
		// on the kicks (the strong ones; the pushed kicks as DENS extras)
		int last = -100;
		int lastPitch = -1;
		bool alt = false;
		for (int s = 0; s < LEN; s++) {
			const Hit& k = p.drum[KICK][s];
			bool strong = k.vel > 0.f && k.prio <= 0.15f;
			bool weak = k.vel > 0.f && !strong;
			if (!strong && !weak && !(s % STEPS == 0 && mode == BM_808))
				continue;
			int nxt = s + 1;
			while (nxt < LEN && p.drum[KICK][nxt].vel == 0.f)
				nxt++;
			float len = (float) std::min(nxt - s, mode == BM_808 ? 8 : 4);
			Chord c = chordAt(p, si, pads, s);
			if (!c.valid())
				c = p.bar[s / STEPS];
			int root = bassNote(c.root);
			int pitch = root;
			if (fifths && alt)
				pitch = root + 7;
			alt = !alt;
			if (mode == BM_808 && rng.chance(0.15f))
				pitch += 12;
			float prio = strong ? 0.f : std::max(0.3f, k.prio);
			addNote(p.bass, s, pitch, mode == BM_808 ? len - 0.2f : std::min(len, 2.f) - 0.1f, strong ? 0.9f : 0.7f, prio);
			if (mode == BM_808 && lastPitch >= 0 && pitch != lastPitch && s - last <= 8 && rng.chance(0.4f))
				p.bass[s].slide = true;
			if (s % STEPS == 0)
				p.bass[s].acc = true;
			last = s;
			lastPitch = pitch;
		}
		// no strong kick on a bar's one (Drumless): the root there anyway
		for (int b = 0; b < BARS; b++)
			if (p.bass[b * STEPS].pitch < 0 || p.bass[b * STEPS].prio > 0.f) {
				Chord c = chordAt(p, si, pads, b * STEPS);
				if (!c.valid())
					c = p.bar[b];
				addNote(p.bass, b * STEPS, bassNote(c.root), mode == BM_808 ? 7.8f : 3.8f, 0.85f, 0.f);
				p.bass[b * STEPS].acc = true;
			}
		// passing notes before a strong kick (with DENS)
		if (mode == BM_ROOTS)
			for (int s = 2; s < LEN; s++)
				if (p.bass[s].pitch >= 0 && p.bass[s].prio == 0.f && p.bass[s - 1].pitch < 0 && p.bass[s - 2].pitch < 0 &&
				    rng.chance(0.35f)) {
					int pp = p.bass[s].pitch + (rng.chance(0.5f) ? 7 : -5);
					addNote(p.bass, s - 1, snapScale(pp, scale, set.key.root), 0.8f, 0.6f, 0.55f);
				}
	}
	else if (mode == BM_WALK) {
		for (int b = 0; b < BARS; b++) {
			Chord c = p.bar[b];
			Chord nx = p.bar[(b + 1) % BARS];
			int tones[5];
			int nt = chordTones(c, tones);
			int root = bassNote(c.root);
			for (int q = 0; q < 4; q++) {
				int s = b * STEPS + q * 4;
				Chord cs = chordAt(p, si, pads, s);
				if (cs.valid() && cs.root != c.root)
					root = bassNote(cs.root);
				int pitch;
				if (q == 0)
					pitch = root;
				else if (q == 3)
					pitch = bassNote(nx.root) + (rng.chance(0.5f) ? 1 : -1); // approach
				else
					pitch = nearest(nt > 0 ? tones[1 + rng.below(std::max(1, nt - 1))] : c.root, root + 5);
				addNote(p.bass, s, pitch, 3.2f, q == 0 ? 0.85f : 0.7f, 0.f);
				if (rng.chance(0.3f))
					addNote(p.bass, s + 3, snapScale(pitch + (rng.chance(0.5f) ? 2 : -2), scale, set.key.root), 0.8f, 0.5f, 0.6f);
			}
		}
	}
	else { // BM_FUNK
		static const int POS[] = {0, 3, 6, 8, 10, 11, 14};
		bool on[7];
		for (int i = 0; i < 7; i++)
			on[i] = i == 0 || rng.chance(0.55f);
		int intv[7];
		static const int CH[] = {0, 12, 7, 10, 0, 5, 12};
		for (int i = 0; i < 7; i++)
			intv[i] = i == 0 ? 0 : CH[rng.below(7)];
		for (int b = 0; b < BARS; b++)
			for (int i = 0; i < 7; i++) {
				if (!on[i] && !(b == 3 && rng.chance(0.3f)))
					continue;
				int s = b * STEPS + POS[i];
				Chord c = chordAt(p, si, pads, s);
				int pitch = bassNote(c.root) + intv[i];
				addNote(p.bass, s, pitch, i == 0 ? 2.f : 1.f - 0.15f, i == 0 ? 0.9f : 0.75f, on[i] ? 0.f : 0.5f);
				if (i == 0 || POS[i] == 10)
					p.bass[s].acc = true;
				if (intv[i] == 12 && rng.chance(0.4f))
					p.bass[s].slide = true;
			}
	}
	if (mode != BM_WALK)
		bassLayer(p, st, set, si, pads, rng, mode, scale);
}

// ---------------------------------------------------------------- melody
static void genMel(Pattern& p, const Style& st, const Settings& set, const SampleInfo& si, const PadSet* pads, Rng& rng) {
	for (int s = 0; s < LEN; s++)
		p.mel[s] = Note();
	int scale = scaleFor(st, set.key);
	int center = st.melOct;
	bool jazz = st.mel == MM_JAZZ;
	int mode = set.melMode;
	if (mode == 0) { // STAB: short chord-tone hits on a syncopated motif
		static const char* RH[] = {"x..x..x...x.x...", "x...x..x..x.x...", "..x..x...x..x.x.", "x.x...x..x...x.."};
		const char* rh = RH[rng.below(4)];
		int pick = rng.below(3);
		for (int s = 0; s < LEN; s++) {
			if (rh[s % STEPS] != 'x')
				continue;
			Chord c = chordAt(p, si, pads, s);
			int tones[5];
			int nt = chordTones(c, tones);
			if (!nt)
				continue;
			int pc = tones[(pick + (s % STEPS) / 3) % nt];
			addNote(p.mel, s, nearest(pc, center), rng.range(0.5f, 1.2f), 0.8f, (s % STEPS) < 8 ? 0.f : 0.3f);
		}
	}
	else if (mode == 1) { // LEAD: long legato notes, a two-bar line A B A B'
		int line[8];
		int d = rng.below(5);
		for (int i = 0; i < 8; i++) {
			d += rng.below(5) - 2;
			d = std::max(-3, std::min(7, d));
			line[i] = d;
		}
		static const int RH[] = {0, 3, 6, 10, 16, 19, 22, 26};
		for (int b = 0; b < BARS; b += 2)
			for (int i = 0; i < 8; i++) {
				int s = b * STEPS + RH[i];
				if (s >= LEN)
					continue;
				int nextS = i + 1 < 8 ? b * STEPS + RH[i + 1] : (b + 2) * STEPS;
				Chord c = chordAt(p, si, pads, s);
				int pitch = set.key.root + scaleStep(scale, line[i]) + 12 * ((center - set.key.root) / 12);
				if ((i == 0 || i == 4) && c.valid())
					pitch = nearest(c.root + QUALITY_TONES[c.quality][rng.below(3)], pitch);
				if (b == 2 && i >= 6)
					pitch = snapScale(pitch + (rng.chance(0.5f) ? 2 : -3), scale, set.key.root);
				addNote(p.mel, s, pitch, (float) (nextS - s) - 0.1f, 0.8f, i % 2 ? 0.25f : 0.f);
				p.mel[s].slide = i > 0 && rng.chance(0.35f);
			}
	}
	else if (mode == 2) { // ARP: bells through the chord tones, up an octave
		int stepLen = rng.chance(0.5f) ? 2 : 1;
		int dir = rng.chance(0.5f) ? 1 : -1;
		for (int s = 0; s < LEN; s += stepLen) {
			Chord c = chordAt(p, si, pads, s);
			int tones[5];
			int nt = chordTones(c, tones);
			if (!nt)
				continue;
			int k = (s / stepLen) % (nt + 1);
			int idx = dir > 0 ? k : nt - k;
			int pitch = nearest(tones[idx % nt], center + 5) + (idx >= nt ? 12 : 0);
			float prio = (s % 4 == 0) ? 0.f : (stepLen == 1 ? 0.6f : 0.3f);
			addNote(p.mel, s, pitch, stepLen * 0.8f, 0.7f, prio);
		}
	}
	else { // RIFF: a short motif of 4-6 notes, repeated, bar 4 answers
		int n = 4 + rng.below(3);
		int pos[6], deg[6];
		for (int i = 0; i < n; i++) {
			pos[i] = rng.below(STEPS);
			deg[i] = rng.below(6) - 1;
		}
		std::sort(pos, pos + n);
		for (int b = 0; b < BARS; b++)
			for (int i = 0; i < n; i++) {
				int s = b * STEPS + pos[i];
				int dd = deg[i];
				if (b == 3 && i >= n - 2)
					dd += rng.chance(0.5f) ? 2 : -2;
				int pitch = set.key.root + scaleStep(scale, dd) + 12 * ((center - set.key.root) / 12);
				Chord c = chordAt(p, si, pads, s);
				if (jazz && c.valid() && rng.chance(0.5f))
					pitch = nearest(c.root + QUALITY_TONES[c.quality][1 + rng.below(2)], pitch);
				if (jazz && i + 1 < n && pos[i + 1] - pos[i] >= 2 && rng.chance(0.3f))
					addNote(p.mel, s + 1, pitch + 1, 0.8f, 0.5f, 0.6f); // a chromatic step up
				addNote(p.mel, s, pitch, std::max(0.8f, (float) ((i + 1 < n ? pos[i + 1] : STEPS) - pos[i]) * 0.7f), 0.8f,
				        i < 3 ? 0.f : 0.35f);
			}
	}
	// with a sample: where a chop starts, the melody steps back (comes only with more DENS)
	if (si.count > 0)
		for (int s = 0; s < LEN; s++)
			if (p.mel[s].pitch >= 0 && p.chop[s].pad >= 0)
				p.mel[s].prio = std::min(1.f, p.mel[s].prio + 0.4f);
	(void) jazz;
}

// ---------------------------------------------------------------- pads
static void voicing(const Chord& c, int8_t* out) {
	int tones[5];
	int n = chordTones(c, tones);
	for (int i = 0; i < PAD_VOICES; i++)
		out[i] = -1;
	if (!n)
		return;
	int base = 55;
	for (int i = 0; i < PAD_VOICES; i++) {
		int pc = i < n ? tones[i] : tones[0] + 12;
		int note = nearest(pc, base + i * 4);
		out[i] = (int8_t) note;
	}
	std::sort(out, out + PAD_VOICES);
}

static void genPad(Pattern& p, const Style& st, const Settings& set, const SampleInfo& si, const PadSet* pads, Rng& rng) {
	for (int s = 0; s < LEN; s++)
		p.pad[s] = PadEv();
	int mode = set.padMode;
	for (int b = 0; b < BARS; b++) {
		int s0 = b * STEPS;
		if (mode == 1) { // STAB
			static const char* RH[] = {"x.....x...x.....", "x..x......x..x..", "..x...x...x....."};
			const char* rh = RH[rng.below(3)];
			for (int i = 0; i < STEPS; i++)
				if (rh[i] == 'x') {
					PadEv& e = p.pad[s0 + i];
					voicing(chordAt(p, si, pads, s0 + i), e.pitch);
					e.len = 1.5f, e.vel = 0.75f, e.prio = i < 8 ? 0.f : 0.3f;
				}
			continue;
		}
		// SUSTAIN / SWELL: a bar long, or half a bar when the chop's chord changes in the middle
		Chord a = chordAt(p, si, pads, s0);
		Chord m = chordAt(p, si, pads, s0 + 8);
		bool split = m.valid() && a.valid() && (m.root != a.root || m.quality != a.quality);
		PadEv& e = p.pad[s0];
		voicing(a.valid() ? a : p.bar[b], e.pitch);
		e.len = split ? 7.8f : 15.8f;
		e.vel = mode == 2 ? 0.5f : 0.65f;
		e.mod = mode == 2 ? 1.f : 0.f;
		if (split) {
			PadEv& f = p.pad[s0 + 8];
			voicing(m, f.pitch);
			f.len = 7.8f, f.vel = e.vel, f.mod = e.mod;
		}
	}
	(void) st;
}

// ---------------------------------------------------------------- the chops
static float chopSteps(const SampleInfo& si, int pad, float bpm) {
	float stepSec = 60.f / std::max(30.f, bpm) / 4.f;
	return si.chop[pad].seconds / stepSec;
}

static void putChop(Pattern& p, int s, int pad, float len, int pitch, float prio) {
	if (s < 0 || s >= LEN)
		return;
	ChopEv& e = p.chop[s];
	e = ChopEv();
	e.pad = (int8_t) pad;
	e.len = std::max(0.5f, len);
	e.pitch = (int8_t) pitch;
	e.prio = prio;
}

static void genChop(Pattern& p, const Style& st, const Settings& set, const SampleInfo& si, const PadSet* pads, Rng& rng,
                    float bpm) {
	// keep the locked pads' events
	ChopEv kept[LEN];
	bool anyLocked = false;
	for (int s = 0; s < LEN; s++) {
		kept[s] = ChopEv();
		if (p.chop[s].pad >= 0 && pads && pads[p.chop[s].pad].lock)
			kept[s] = p.chop[s], anyLocked = true;
		p.chop[s] = ChopEv();
	}
	if (si.count <= 0)
		return;
	int scale = scaleFor(st, set.key);
	bool loop = rng.uni() < st.chopLoop;
	if (loop) {
		// the sample in order (as it was played), over one or two bars: A1 A2 A1 A2' (or A A A A')
		int start = si.count > 4 ? rng.below(std::min(4, si.count)) : 0;
		int pos = 0, i = start;
		int span = 0;
		ChopEv A[2 * STEPS];
		for (int k = 0; k < 2 * STEPS; k++)
			A[k] = ChopEv();
		while (pos < 2 * STEPS) {
			float d = chopSteps(si, i, bpm);
			int len = std::max(1, std::min(8, (int) std::lround(d)));
			if (st.chopLoop < 0.9f && rng.chance(0.25f))
				len = std::max(1, len / 2); // a cut-short chop now and then
			A[pos].pad = (int8_t) i;
			A[pos].len = (float) len;
			A[pos].pitch = 0;
			pos += len;
			span = pos;
			i = (i + 1) % si.count;
			if (i == start && pos >= STEPS)
				break;
		}
		int alen = span > STEPS ? 2 * STEPS : STEPS;
		for (int s = 0; s < LEN; s++) {
			const ChopEv& e = A[s % alen];
			if (e.pad >= 0)
				putChop(p, s, e.pad, e.len, 0, 0.f);
		}
	}
	else {
		// stabs: 2-5 chops (the short ones first), on a syncopated rhythm, pitched within the key
		int cand[PADS], nc = 0;
		for (int i = 0; i < si.count; i++)
			cand[nc++] = i;
		std::sort(cand, cand + nc, [&](int a, int b) {
			float sa = si.chop[a].seconds + (si.chop[a].kind == 1 ? -10.f : 0.f);
			float sb = si.chop[b].seconds + (si.chop[b].kind == 1 ? -10.f : 0.f);
			return sa < sb;
		});
		int use = std::min(nc, 2 + rng.below(4));
		int set5[5];
		for (int i = 0; i < use; i++)
			set5[i] = cand[std::min(nc - 1, i + rng.below(2))];
		static const char* RH[] = {"x..x..x...x.x...", "x...x..x..x.x...", "x.x...x..x...x..", "..x..x...x..x.x.",
		                           "x..x...x.x..x..."};
		const char* rh = RH[rng.below(5)];
		int k = 0;
		int8_t pitchOf[STEPS];
		for (int i = 0; i < STEPS; i++) {
			pitchOf[i] = 0;
			if (rh[i] == 'x' && rng.chance(st.chopPitch)) {
				static const int DEG[] = {2, 4, -2, 3, 7, -3, 1};
				pitchOf[i] = (int8_t) scaleStep(scale, DEG[rng.below(7)]);
			}
		}
		for (int b = 0; b < BARS; b++)
			for (int i = 0; i < STEPS; i++) {
				if (rh[i] != 'x')
					continue;
				int pad = set5[k++ % use];
				int nxt = i + 1;
				while (nxt < STEPS && rh[nxt] != 'x')
					nxt++;
				float len = std::min((float) (nxt - i), std::max(1.f, chopSteps(si, pad, bpm)));
				putChop(p, b * STEPS + i, pad, len, pitchOf[i], i < 8 ? 0.f : 0.2f);
			}
	}
	// the chipmunk, the drunk timing, cut-short ends
	for (int s = 0; s < LEN; s++) {
		ChopEv& e = p.chop[s];
		if (e.pad < 0)
			continue;
		e.pitch = (int8_t) (e.pitch + st.chopUp);
		e.vel = rng.range(0.8f, 1.f);
		if (st.chopOff > 0.f) {
			e.off = rng.range(-1.f, 1.f) * st.chopOff * 0.35f;
			e.len *= rng.range(0.6f, 1.f);
		}
		if (rng.chance(st.chopRev * 0.25f))
			e.rev = true;
	}
	// bar 4 answers: a stutter, a reverse or a gap on the last beat
	int b4 = 3 * STEPS + 12;
	float r = rng.uni();
	if (r < st.chopStutter) {
		int s = b4;
		while (s < LEN && p.chop[s].pad < 0)
			s++;
		int pad = s < LEN ? p.chop[s].pad : p.chop[3 * STEPS].pad;
		if (pad >= 0)
			for (int i = 0; i < 4; i++) {
				putChop(p, b4 + i, pad, 0.5f, p.chop[3 * STEPS].pitch, 0.f);
				p.chop[b4 + i].ratchet = (uint8_t) (i >= 2 ? 2 : 1);
			}
	}
	else if (r < st.chopStutter + st.chopRev) {
		for (int i = 0; i < 4; i++)
			if (p.chop[b4 + i].pad >= 0)
				p.chop[b4 + i].rev = true;
	}
	else if (r < st.chopStutter + st.chopRev + 0.2f) {
		for (int i = 2; i < 4; i++)
			p.chop[b4 + i] = ChopEv();
	}
	// the locked pads back in place (new events under them go)
	if (anyLocked)
		for (int s = 0; s < LEN; s++)
			if (kept[s].pad >= 0) {
				int end = std::min(LEN, s + std::max(1, (int) std::ceil(kept[s].len)));
				for (int i = s; i < end; i++)
					p.chop[i] = ChopEv();
				p.chop[s] = kept[s];
			}
}

void generate(Pattern& p, int track, const Settings& set, const SampleInfo& si, const PadSet* pads, uint32_t seed,
              bool lockChords, float bpm) {
	const Style& st = STYLES[std::max(0, std::min(NUM_STYLES - 1, set.style))];
	Rng rng;
	rng.seed(seed * 2654435761u + (uint32_t) (track + 7) * 40503u);
	bool all = track < 0;
	bool harmony = track == T_HARMONY;
	// edited tracks (GRID HH) keep their notes, unless their own NEW / GEN asks
	auto may = [&](int t) { return track == t || !p.edited[t]; };
	Chord before[BARS];
	for (int b = 0; b < BARS; b++)
		before[b] = p.bar[b];
	if (track == T_DRUMS || (all && may(T_DRUMS)))
		genDrums(p, st, rng);
	if ((all || track == T_CHOP) && may(T_CHOP))
		genChop(p, st, set, si, pads, rng, bpm);
	if ((all || harmony || track == T_CHOP) && !lockChords)
		genChords(p, st, set, si, pads, rng);
	// the bass leans on the kicks and the chops' chords; the pads on the chords
	if ((all || harmony || track == T_BASS || track == T_CHOP || track == T_DRUMS) && may(T_BASS))
		genBass(p, st, set, si, pads, rng);
	if ((all || harmony || track == T_MEL) && may(T_MEL))
		genMel(p, st, set, si, pads, rng);
	if ((all || harmony || track == T_PAD || track == T_CHOP) && may(T_PAD))
		genPad(p, st, set, si, pads, rng);
	if (track >= 0 && track < NUM_TRACKS) {
		p.edited[track] = false; // generated again: follows the loop, DENS works again
		p.lenTrack[track] = 0;
		if (track == T_DRUMS)
			for (int v = 0; v < DRUMS; v++)
				p.lenDrum[v] = 0;
	}
	// CHORD FOLLOW: edited tone tracks move with the new chords (roots to roots, the rest along the scale)
	if (p.chordFollow) {
		int scale = scaleFor(st, set.key);
		for (int b = 0; b < BARS; b++) {
			int d = pcOf(p.bar[b].root - before[b].root);
			if (d > 6)
				d -= 12;
			if (!d)
				continue;
			for (int s = b * STEPS; s < (b + 1) * STEPS; s++) {
				auto mv = [&](int8_t& n) {
					if (n < 0)
						return;
					bool root = pcOf(n) == pcOf(before[b].root);
					int m = root ? n + d : snapScale(n + d, scale, set.key.root);
					n = (int8_t) std::max(0, std::min(127, m));
				};
				if (p.edited[T_BASS] && track != T_BASS)
					mv(p.bass[s].pitch);
				if (p.edited[T_MEL] && track != T_MEL)
					mv(p.mel[s].pitch);
				if (p.edited[T_PAD] && track != T_PAD)
					for (int k = 0; k < PAD_VOICES; k++)
						mv(p.pad[s].pitch[k]);
			}
		}
	}
}

void mutateChops(Pattern& p, const Settings& set, const SampleInfo& si, const PadSet* pads, uint32_t seed, float amount,
                 float bpm) {
	if (si.count <= 0 || amount <= 0.f)
		return;
	const Style& st = STYLES[std::max(0, std::min(NUM_STYLES - 1, set.style))];
	int scale = scaleFor(st, set.key);
	Rng rng;
	rng.seed(seed * 747796405u + 12345u);
	for (int s = 0; s < LEN; s++) {
		ChopEv& e = p.chop[s];
		if (e.pad < 0 || (pads && pads[e.pad].lock) || !rng.chance(amount * 0.5f))
			continue;
		switch (rng.below(4)) {
			case 0: e.pad = (int8_t) ((e.pad + 1 + rng.below(std::max(1, si.count - 1))) % si.count); break;
			case 1: e.pitch = (int8_t) (st.chopUp + (rng.chance(0.5f) ? scaleStep(scale, rng.below(5) - 1) : 0)); break;
			case 2: e.rev = !e.rev; break;
			default:
				if (e.len >= 2.f)
					e.len *= 0.5f;
				else
					e.ratchet = (uint8_t) (e.ratchet == 1 ? 2 : 1);
				break;
		}
	}
	(void) bpm;
}

// ---------------------------------------------------------------- rendering
static bool cutHas(const Note& n) { return n.pitch >= 0; }
static bool cutHas(const PadEv& e) { return e.pitch[0] >= 0; }
static void cutShorten(Note& n, float len, float vel) {
	if (n.pitch >= 0)
		n.len = std::min(n.len, len), n.vel *= vel;
}
static void cutShorten(PadEv& e, float len, float vel) {
	if (e.pitch[0] >= 0)
		e.len = std::min(e.len, len), e.vel *= vel;
}

template <typename T>
static void cutBeats(T* line, int track, float amount, uint32_t seed) {
	if (amount <= 0.f)
		return;
	T src[LEN];
	for (int s = 0; s < LEN; s++)
		src[s] = line[s];
	for (int q = 0; q < LEN / 4; q++) {
		if (hash01(seed, (uint32_t) track, (uint32_t) q) >= amount * 0.7f)
			continue;
		int op = (int) (hash01(seed + 1, (uint32_t) track, (uint32_t) q) * 4.f);
		int s0 = q * 4;
		switch (op) {
			case 0: // repeat the beat before
				if (q > 0)
					for (int i = 0; i < 4; i++)
						line[s0 + i] = src[s0 - 4 + i];
				break;
			case 1: { // stutter: the beat's first event on every step
				int f = -1;
				for (int i = 0; i < 4 && f < 0; i++)
					if (cutHas(src[s0 + i]))
						f = s0 + i;
				if (f >= 0)
					for (int i = 0; i < 4; i++) {
						line[s0 + i] = src[f];
						cutShorten(line[s0 + i], 0.5f, 1.f - 0.15f * i);
					}
				break;
			}
			case 2: // a gap
				for (int i = 0; i < 4; i++)
					line[s0 + i] = T();
				break;
			default: // the beat backwards
				for (int i = 0; i < 4; i++)
					line[s0 + i] = src[s0 + 3 - i];
				break;
		}
	}
}

static void cutDrums(Hit (*drum)[LEN], float amount, uint32_t seed) {
	if (amount <= 0.f)
		return;
	for (int q = 0; q < LEN / 4; q++) {
		if (hash01(seed, 0u, (uint32_t) q) >= amount * 0.7f)
			continue;
		int op = (int) (hash01(seed + 1, 0u, (uint32_t) q) * 4.f);
		int s0 = q * 4;
		Hit tmp[DRUMS][4];
		for (int v = 0; v < DRUMS; v++)
			for (int i = 0; i < 4; i++)
				tmp[v][i] = drum[v][s0 + i];
		for (int v = 0; v < DRUMS; v++)
			for (int i = 0; i < 4; i++) {
				Hit& h = drum[v][s0 + i];
				switch (op) {
					case 0:
						if (q > 0)
							h = drum[v][s0 - 4 + i];
						break;
					case 1: // the beat's first step stuttered (kick and snare get a roll)
						h = tmp[v][0];
						if (h.vel > 0.f)
							h.vel *= 1.f - 0.18f * i;
						break;
					case 2:
						if (!(v == KICK && i == 0))
							h = Hit();
						break;
					default: h = tmp[v][3 - i]; break;
				}
			}
	}
}

void render(const Pattern& p, const Knobs& k, Render& r) {
	for (int v = 0; v < DRUMS; v++)
		for (int s = 0; s < LEN; s++) {
			Hit h = p.drum[v][s];
			if (h.vel > 0.f && h.prio > k.dens[T_DRUMS] && !p.edited[T_DRUMS])
				h = Hit();
			if (h.ratchet > 1 && k.dens[T_DRUMS] < 0.25f)
				h.ratchet = 1;
			r.drum[v][s] = h;
		}
	for (int s = 0; s < LEN; s++) {
		Note b = p.bass[s];
		if (b.pitch >= 0 && b.prio > k.dens[T_BASS] && !p.edited[T_BASS])
			b = Note();
		if (b.pitch >= 0)
			b.pitch = (int8_t) std::max(0, std::min(127, b.pitch + 12 * k.bassOct));
		r.bass[s] = b;
		Note m = p.mel[s];
		if (m.pitch >= 0 && m.prio > k.dens[T_MEL] && !p.edited[T_MEL])
			m = Note();
		r.mel[s] = m;
		PadEv e = p.pad[s];
		if (e.pitch[0] >= 0 && e.prio > k.dens[T_PAD] && !p.edited[T_PAD])
			e = PadEv();
		r.pad[s] = e;
		ChopEv c = p.chop[s];
		if (c.pad >= 0 && c.prio > k.dens[T_CHOP] && !p.edited[T_CHOP])
			c = ChopEv();
		r.chop[s] = c;
	}
	cutDrums(r.drum, k.cut[T_DRUMS], k.cutSeed);
	cutBeats(r.mel, T_MEL, k.cut[T_MEL], k.cutSeed);
	cutBeats(r.pad, T_PAD, k.cut[T_PAD], k.cutSeed);
}

} // namespace hh
