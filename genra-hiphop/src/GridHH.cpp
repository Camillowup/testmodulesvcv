// SPDX-License-Identifier: GPL-3.0-only
// GENRA GRID HH: the grid editor for GENRA HIP-HOP (44 HP). Drum raster, chop raster (16 pads), piano roll for bass,
// melody and pads, VEL / CHANCE lane; everything GENRA GRID has: selection, copy / paste, nudge, own length and swing
// per track and drum voice, track tools, SNAP, CHORD FOLLOW, ALL / SCALE / USED rows, ONE / GHOST / ALL 5 views,
// UNDO / REDO, REC MIDI, MIDI export and import.
//
// How it talks to GENRA HIP-HOP: the module publishes its pattern into `shared` (under shareM); the grid copies it,
// edits its copy on the UI thread and hands it back with putFromGrid(). The first edit of a track "bakes" it: what
// DENS shows becomes fixed notes (prio 0), the rest goes, the track counts as edited (NEW ALL keeps it, GEN TRACK
// makes it generative again).
#include "HipHopModule.hpp"
#include "HipHopUI.hpp"
#include "GridHHLayout.hpp"
#include <algorithm>
#include <cstring>

using namespace hh;

static const NVGcolor TRACK_COL[NUM_TRACKS] = {nvgRGB(0x8a, 0x8f, 0xb8), nvgRGB(0x9f, 0xbf, 0x5a), nvgRGB(0x5f, 0xb7, 0xb0),
                                               nvgRGB(0xd5, 0x8a, 0xa6), nvgRGB(0xd9, 0x56, 0x3f)};
static const char* const TRACK_NAME[NUM_TRACKS] = {"DRUMS", "BASS", "MELODY", "PADS", "CHOP"};
static const char* const DRUM_NAME[DRUMS] = {"KICK", "SNARE", "CLAP", "RIM", "C.HAT", "O.HAT", "TOM", "PERC"};
static const int GM_DRUM[DRUMS] = {36, 38, 39, 37, 42, 46, 45, 56};

static bool isPiano(int t) { return t == T_BASS || t == T_MEL || t == T_PAD; }
static int rowsOfRaster(int t) { return t == T_DRUMS ? DRUMS : PADS; }

// ================================================================ the items of a track (one model for every track)
struct Ref {
	int s = 0, row = 0; // row: drum voice, pad number or MIDI pitch
	Ref() {}
	Ref(int s_, int row_) : s(s_), row(row_) {}
	bool operator==(const Ref& o) const { return s == o.s && row == o.row; }
};
struct Item {
	int s = 0, row = 0;
	float vel = 0.8f, len = 1.f, chance = 1.f, off = 0.f, prio = 0.f;
	uint8_t cond = 0, ratchet = 1;
	bool acc = false, slide = false, rev = false;
	int pitch = 0; // chop: semitones
	float padLen = 4.f;
};

static int padVoice(const PadEv& e, int pitch) {
	for (int k = 0; k < PAD_VOICES; k++)
		if (e.pitch[k] == pitch)
			return k;
	return -1;
}
static void padSort(PadEv& e) {
	int8_t v[PAD_VOICES];
	int n = 0;
	for (int k = 0; k < PAD_VOICES; k++)
		if (e.pitch[k] >= 0)
			v[n++] = e.pitch[k];
	for (int i = 1; i < n; i++) // (insertion sort: four voices at most)
		for (int j = i; j > 0 && v[j - 1] > v[j]; j--)
			std::swap(v[j - 1], v[j]);
	for (int k = 0; k < PAD_VOICES; k++)
		e.pitch[k] = k < n ? v[k] : -1;
}

static void items(const Pattern& P, int t, std::vector<Ref>& out) {
	out.clear();
	for (int s = 0; s < LEN; s++) {
		switch (t) {
			case T_DRUMS:
				for (int v = 0; v < DRUMS; v++)
					if (P.drum[v][s].vel > 0.f)
						out.push_back({s, v});
				break;
			case T_BASS:
				if (P.bass[s].pitch >= 0)
					out.push_back({s, P.bass[s].pitch});
				break;
			case T_MEL:
				if (P.mel[s].pitch >= 0)
					out.push_back({s, P.mel[s].pitch});
				break;
			case T_PAD:
				for (int k = 0; k < PAD_VOICES; k++)
					if (P.pad[s].pitch[k] >= 0)
						out.push_back({s, P.pad[s].pitch[k]});
				break;
			default:
				if (P.chop[s].pad >= 0)
					out.push_back({s, P.chop[s].pad});
				break;
		}
	}
}

static bool has(const Pattern& P, int t, const Ref& r) {
	if (r.s < 0 || r.s >= LEN)
		return false;
	switch (t) {
		case T_DRUMS: return r.row >= 0 && r.row < DRUMS && P.drum[r.row][r.s].vel > 0.f;
		case T_BASS: return P.bass[r.s].pitch == r.row;
		case T_MEL: return P.mel[r.s].pitch == r.row;
		case T_PAD: return padVoice(P.pad[r.s], r.row) >= 0;
		default: return P.chop[r.s].pad == r.row;
	}
}

static Item getItem(const Pattern& P, int t, const Ref& r) {
	Item it;
	it.s = r.s, it.row = r.row;
	switch (t) {
		case T_DRUMS: {
			const Hit& h = P.drum[r.row][r.s];
			it.vel = h.vel, it.chance = h.chance, it.off = h.off, it.cond = h.cond, it.ratchet = h.ratchet, it.prio = h.prio;
			break;
		}
		case T_BASS:
		case T_MEL: {
			const Note& n = (t == T_BASS ? P.bass : P.mel)[r.s];
			it.vel = n.vel, it.len = n.len, it.chance = n.chance, it.off = n.off, it.cond = n.cond, it.acc = n.acc;
			it.slide = n.slide, it.prio = n.prio;
			break;
		}
		case T_PAD: {
			const PadEv& e = P.pad[r.s];
			it.vel = e.vel, it.len = e.len, it.chance = e.chance, it.off = e.off, it.cond = e.cond, it.prio = e.prio;
			break;
		}
		default: {
			const ChopEv& c = P.chop[r.s];
			it.vel = c.vel, it.len = c.len, it.chance = c.chance, it.off = c.off, it.cond = c.cond, it.ratchet = c.ratchet;
			it.rev = c.rev, it.pitch = c.pitch, it.prio = c.prio;
			break;
		}
	}
	return it;
}

static void erase(Pattern& P, int t, const Ref& r) {
	if (r.s < 0 || r.s >= LEN)
		return;
	switch (t) {
		case T_DRUMS:
			if (r.row >= 0 && r.row < DRUMS)
				P.drum[r.row][r.s] = Hit();
			break;
		case T_BASS:
			if (P.bass[r.s].pitch == r.row)
				P.bass[r.s] = Note();
			break;
		case T_MEL:
			if (P.mel[r.s].pitch == r.row)
				P.mel[r.s] = Note();
			break;
		case T_PAD: {
			PadEv& e = P.pad[r.s];
			int k = padVoice(e, r.row);
			if (k >= 0) {
				e.pitch[k] = -1;
				padSort(e);
				if (e.pitch[0] < 0)
					e = PadEv();
			}
			break;
		}
		default:
			if (P.chop[r.s].pad == r.row)
				P.chop[r.s] = ChopEv();
			break;
	}
}

/** Put an item at (s, row): replaces what is there (one note per step on bass / melody / chop, one hit per voice). */
static Ref putItem(Pattern& P, int t, int s, int row, const Item& it) {
	s = ((s % LEN) + LEN) % LEN;
	switch (t) {
		case T_DRUMS: {
			row = clamp(row, 0, DRUMS - 1);
			Hit& h = P.drum[row][s];
			h.vel = std::max(0.05f, it.vel), h.chance = it.chance, h.off = it.off, h.cond = it.cond;
			h.ratchet = std::max<uint8_t>(1, it.ratchet), h.prio = 0.f;
			break;
		}
		case T_BASS:
		case T_MEL: {
			row = clamp(row, 0, 127);
			Note& n = (t == T_BASS ? P.bass : P.mel)[s];
			n.pitch = (int8_t) row, n.vel = std::max(0.05f, it.vel), n.len = std::max(0.25f, it.len);
			n.chance = it.chance, n.off = it.off, n.cond = it.cond, n.acc = it.acc, n.slide = it.slide, n.prio = 0.f;
			break;
		}
		case T_PAD: {
			row = clamp(row, 0, 127);
			PadEv& e = P.pad[s];
			if (e.pitch[0] < 0) {
				e = PadEv();
				e.len = std::max(0.25f, it.len), e.vel = std::max(0.05f, it.vel);
				e.chance = it.chance, e.off = it.off, e.cond = it.cond;
			}
			if (padVoice(e, row) < 0) {
				if (e.pitch[PAD_VOICES - 1] >= 0)
					e.pitch[PAD_VOICES - 1] = -1; // full: the top voice goes
				for (int k = 0; k < PAD_VOICES; k++)
					if (e.pitch[k] < 0) {
						e.pitch[k] = (int8_t) row;
						break;
					}
				padSort(e);
			}
			e.prio = 0.f;
			break;
		}
		default: {
			row = clamp(row, 0, PADS - 1);
			ChopEv& c = P.chop[s];
			c = ChopEv();
			c.pad = (int8_t) row, c.vel = std::max(0.05f, it.vel), c.len = std::max(0.25f, it.len), c.chance = it.chance;
			c.off = it.off, c.cond = it.cond, c.ratchet = std::max<uint8_t>(1, it.ratchet), c.rev = it.rev;
			c.pitch = (int8_t) it.pitch;
			break;
		}
	}
	return {s, row};
}

/** The first edit of a track: what DENS shows becomes fixed notes, the rest goes. */
static void bake(Pattern& P, int t, float dens) {
	if (P.edited[t])
		return;
	std::vector<Ref> rs;
	items(P, t, rs);
	for (const Ref& r : rs) {
		Item it = getItem(P, t, r);
		if (it.prio > dens)
			erase(P, t, r);
	}
	for (int s = 0; s < LEN; s++) {
		for (int v = 0; v < DRUMS && t == T_DRUMS; v++)
			P.drum[v][s].prio = 0.f;
		if (t == T_BASS)
			P.bass[s].prio = 0.f;
		if (t == T_MEL)
			P.mel[s].prio = 0.f;
		if (t == T_PAD)
			P.pad[s].prio = 0.f;
		if (t == T_CHOP)
			P.chop[s].prio = 0.f;
	}
	P.edited[t] = true;
}

// ================================================================ a small MIDI file reader (for Import)
static bool readMidi(const std::vector<uint8_t>& b, Pattern& P, std::string& err) {
	auto u32 = [&](size_t i) { return (uint32_t) b[i] << 24 | b[i + 1] << 16 | b[i + 2] << 8 | b[i + 3]; };
	if (b.size() < 14 || memcmp(b.data(), "MThd", 4)) {
		err = "not a MIDI file";
		return false;
	}
	int ntr = b[10] << 8 | b[11];
	int ppq = b[12] << 8 | b[13];
	if (ppq <= 0 || (ppq & 0x8000)) {
		err = "this MIDI timing is not supported";
		return false;
	}
	size_t pos = 8 + u32(4);
	bool got[NUM_TRACKS] = {};
	Pattern N = P;
	for (int tr = 0; tr < ntr && pos + 8 <= b.size(); tr++) {
		if (memcmp(b.data() + pos, "MTrk", 4)) {
			err = "broken MIDI file";
			return false;
		}
		size_t end = std::min(b.size(), pos + 8 + u32(pos + 4));
		size_t i = pos + 8;
		pos = end;
		std::string name;
		uint32_t tick = 0;
		uint8_t run = 0;
		struct On {
			uint32_t tick;
			int vel;
			bool on;
		};
		std::vector<std::pair<int, std::pair<int, On>>> evs; // (channel, (note, on))
		while (i < end) {
			uint32_t d = 0;
			while (i < end) {
				uint8_t c = b[i++];
				d = (d << 7) | (c & 0x7f);
				if (!(c & 0x80))
					break;
			}
			tick += d;
			if (i >= end)
				break;
			uint8_t st = b[i];
			if (st == 0xff) {
				uint8_t type = b[i + 1];
				i += 2;
				uint32_t l = 0;
				while (i < end) {
					uint8_t c = b[i++];
					l = (l << 7) | (c & 0x7f);
					if (!(c & 0x80))
						break;
				}
				if (type == 0x03 && i + l <= end)
					name.assign((const char*) b.data() + i, l);
				i += l;
				continue;
			}
			if (st == 0xf0 || st == 0xf7) {
				i++;
				uint32_t l = 0;
				while (i < end) {
					uint8_t c = b[i++];
					l = (l << 7) | (c & 0x7f);
					if (!(c & 0x80))
						break;
				}
				i += l;
				continue;
			}
			if (st & 0x80)
				run = st, i++;
			uint8_t hi = run >> 4, ch = run & 15;
			int n = (hi == 0xc || hi == 0xd) ? 1 : 2;
			if (i + n > end)
				break;
			if (hi == 0x9 || hi == 0x8) {
				bool on = hi == 0x9 && b[i + 1] > 0;
				evs.push_back({ch, {b[i], {tick, b[i + 1], on}}});
			}
			i += n;
		}
		if (evs.empty())
			continue;
		// which track: by name, else by channel (10 drums, 1 bass, 2 melody, 3 pads, 4 chop)
		std::string lo = string::lowercase(name);
		int t = -1;
		if (lo.find("drum") != std::string::npos)
			t = T_DRUMS;
		else if (lo.find("bass") != std::string::npos)
			t = T_BASS;
		else if (lo.find("mel") != std::string::npos || lo.find("lead") != std::string::npos)
			t = T_MEL;
		else if (lo.find("pad") != std::string::npos || lo.find("chord") != std::string::npos)
			t = T_PAD;
		else if (lo.find("chop") != std::string::npos)
			t = T_CHOP;
		else {
			int ch = evs[0].first;
			t = ch == 9 ? T_DRUMS : ch == 0 ? T_BASS : ch == 1 ? T_MEL : ch == 2 ? T_PAD : ch == 3 ? T_CHOP : -1;
		}
		if (t < 0)
			continue;
		if (!got[t]) {
			std::vector<Ref> rs;
			items(N, t, rs);
			for (const Ref& r : rs)
				erase(N, t, r);
			got[t] = true;
			N.edited[t] = true;
		}
		for (size_t k = 0; k < evs.size(); k++) {
			const On& o = evs[k].second.second;
			if (!o.on)
				continue;
			int note = evs[k].second.first;
			int s = (int) std::lround(o.tick * 4.0 / ppq);
			if (s >= LEN)
				continue;
			// the note off
			uint32_t offTick = o.tick + ppq / 4;
			for (size_t q = k + 1; q < evs.size(); q++)
				if (evs[q].second.first == note && !evs[q].second.second.on) {
					offTick = evs[q].second.second.tick;
					break;
				}
			Item it;
			it.vel = o.vel / 127.f;
			it.len = std::max(0.25f, (offTick - o.tick) * 4.f / ppq);
			int row = note;
			if (t == T_DRUMS) {
				row = -1;
				for (int v = 0; v < DRUMS; v++)
					if (GM_DRUM[v] == note)
						row = v;
				if (row < 0)
					row = ((note % 12) + 12) % 12 < DRUMS ? ((note % 12) + 12) % 12 : -1;
				if (row < 0)
					continue;
			}
			if (t == T_CHOP) {
				row = note - 36;
				if (row < 0 || row >= PADS)
					continue;
			}
			putItem(N, t, s, row, it);
		}
	}
	bool any = false;
	for (bool g : got)
		any = any || g;
	if (!any) {
		err = "no notes for the tracks found";
		return false;
	}
	P = N;
	return true;
}

// ================================================================ the module
struct GridHH : Module {
	enum ParamId { BAR_PARAM, ZOOM_PARAM, PITCH_PARAM, VEL_PARAM, LEN_PARAM, CHANCE_PARAM, NUM_PARAMS };
	int track = T_BASS;
	bool autoScroll = true;
	int snap = 1;  // 0 off, 1 scale, 2 chord
	int rowsMode = 0; // 0 ALL, 1 SCALE, 2 USED
	int view = 0;     // 0 ONE, 1 GHOST, 2 ALL 5
	int lane = 0;     // 0 VEL, 1 CHANCE
	int laneVoice = 0; // the drum voice / chop pad the lane shows
	int64_t linkedId = -1;
	float pitchBase[NUM_TRACKS] = {0.f, 0.15f, 0.5f, 0.4f, 0.f}; // PITCH per track
	std::atomic<bool> recOn{false};
	midi::InputQueue midiIn;
	struct RecEv {
		uint8_t on, note, vel;
	};
	static const uint32_t REC_N = 256;
	RecEv recBuf[REC_N];
	std::atomic<uint32_t> recHead{0}, recTail{0};

	GridHH() {
		config(NUM_PARAMS, 0, 0, 0);
		configParam(BAR_PARAM, 1.f, 4.f, 1.f, "First visible bar")->snapEnabled = true;
		configSwitch(ZOOM_PARAM, 0.f, 2.f, 2.f, "Visible bars", {"1 bar", "2 bars", "4 bars"});
		configParam(PITCH_PARAM, 0.f, 1.f, 0.15f, "Pitch range (piano roll)");
		configParam(VEL_PARAM, 0.05f, 1.f, 0.8f, "Velocity of new notes", "%", 0.f, 100.f);
		configParam(LEN_PARAM, 1.f, 16.f, 2.f, "Length of new notes", " steps")->snapEnabled = true;
		configParam(CHANCE_PARAM, 0.f, 1.f, 1.f, "Chance of new notes", "%", 0.f, 100.f);
	}

	void process(const ProcessArgs& args) override {
		midi::Message msg;
		while (midiIn.tryPop(&msg, args.frame)) {
			uint8_t st = msg.getStatus();
			if ((st != 0x9 && st != 0x8) || !recOn)
				continue;
			uint32_t h = recHead.load();
			if (h - recTail.load() >= REC_N)
				continue;
			recBuf[h % REC_N] = {(uint8_t) (st == 0x9 && msg.getValue() > 0), (uint8_t) (msg.getNote() & 0x7f),
			                     (uint8_t) (msg.getValue() & 0x7f)};
			recHead = h + 1;
		}
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "track", json_integer(track));
		json_object_set_new(r, "autoScroll", json_boolean(autoScroll));
		json_object_set_new(r, "snap", json_integer(snap));
		json_object_set_new(r, "rows", json_integer(rowsMode));
		json_object_set_new(r, "view", json_integer(view));
		json_object_set_new(r, "lane", json_integer(lane));
		json_object_set_new(r, "linkedId", json_integer(linkedId));
		json_t* pb = json_array();
		for (float v : pitchBase)
			json_array_append_new(pb, json_real(v));
		json_object_set_new(r, "pitchBase", pb);
		json_object_set_new(r, "midiIn", midiIn.toJson());
		return r;
	}
	void dataFromJson(json_t* r) override {
		auto gi = [&](const char* k, int d) {
			json_t* j = json_object_get(r, k);
			return j ? (int) json_integer_value(j) : d;
		};
		track = clamp(gi("track", T_BASS), 0, NUM_TRACKS - 1);
		json_t* j = json_object_get(r, "autoScroll");
		autoScroll = j ? json_is_true(j) : true;
		snap = clamp(gi("snap", 1), 0, 2);
		rowsMode = clamp(gi("rows", 0), 0, 2);
		view = clamp(gi("view", 0), 0, 2);
		lane = clamp(gi("lane", 0), 0, 1);
		j = json_object_get(r, "linkedId");
		linkedId = j ? json_integer_value(j) : -1;
		if (json_t* pb = json_object_get(r, "pitchBase"))
			for (size_t i = 0; i < json_array_size(pb) && i < (size_t) NUM_TRACKS; i++)
				pitchBase[i] = (float) json_number_value(json_array_get(pb, i));
		if (json_t* m = json_object_get(r, "midiIn"))
			midiIn.fromJson(m);
	}
};

// ================================================================ the grid
struct GridView : widget::OpaqueWidget {
	GridHH* m = nullptr;
	GenraHipHop* hh = nullptr;
	Pattern P;       // the grid's copy
	bool have = false;
	uint32_t ver = 0;
	std::vector<Pattern> undo, redo;
	std::vector<Ref> sel;
	std::vector<Item> clip;
	std::vector<Item> barClip;
	int barClipTrack = -1;
	std::string msg;
	float msgTime = 0.f;
	// drag state
	enum Drag { D_NONE, D_PAINT, D_ERASE, D_MOVE, D_LEN, D_RECT, D_LANE, D_LENMARK, D_SWING };
	Drag drag = D_NONE;
	Vec dragStart, dragNow;
	int dragRow = 0, dragS = 0, dragVoice = -1;
	std::vector<Item> moving;
	bool dragDirty = false;
	float swingStart = 0.f;
	// REC: notes held (pitch -> step started)
	int recStart[128];
	bool recBegun = false; // a REC session is one UNDO step
	double lastClick = 0.0;
	Vec lastClickPos;

	GridView() {
		for (int& r : recStart)
			r = -1;
	}

	// ---------------------------------------------------------------- geometry
	static constexpr float HEAD = 17.f, CHORD_Y = 19.f, CHORD_H = 11.f, KEYS = 44.f, RIGHT = 26.f, LANE_H = 30.f;
	float rx() const { return KEYS + 4.f; }
	float rw() const { return box.size.x - rx() - RIGHT; }
	float ry() const { return CHORD_Y + CHORD_H + 3.f; }
	float rh() const { return box.size.y - ry() - LANE_H - 6.f; }
	float laneY() const { return box.size.y - LANE_H - 1.f; }
	int zoomBars() const {
		int z = m ? (int) std::round(m->params[GridHH::ZOOM_PARAM].getValue()) : 2;
		return z == 0 ? 1 : z == 1 ? 2 : 4;
	}
	int firstBar() const {
		int zb = zoomBars();
		if (m && m->autoScroll && hh) {
			float pos = hh->posPub.load();
			if (pos >= 0.f)
				return ((int) (pos / STEPS) / zb) * zb;
		}
		int b = m ? (int) std::round(m->params[GridHH::BAR_PARAM].getValue()) - 1 : 0;
		return clamp(b, 0, BARS - zb);
	}
	int s0() const { return firstBar() * STEPS; }
	int steps() const { return zoomBars() * STEPS; }
	float cw() const { return rw() / steps(); }
	float xOf(float s) const { return rx() + (s - s0()) * cw(); }
	float stepAt(float x) const { return s0() + (x - rx()) / cw(); }
	int trk() const { return m ? m->track : T_BASS; }
	float dens(int t) const {
		if (!hh || t == T_CHOP)
			return 1.f;
		return hh->params[GenraHipHop::DENS_PARAM + t].getValue();
	}
	int scale() const { return hh ? scaleOf(hh->set) : SC_MINOR; }
	int keyRoot() const { return hh ? hh->set.key.root : 5; }

	/** The rows of the piano roll, top first. */
	std::vector<int> pianoRows(int t) const {
		std::vector<int> rows;
		int base = 24 + (int) std::round((m ? m->pitchBase[t] : 0.3f) * 72.f);
		int mode = m ? m->rowsMode : 0;
		if (mode == 2) {
			bool used[128] = {};
			bool any = false;
			std::vector<Ref> rs;
			items(P, t, rs);
			for (const Ref& r : rs)
				used[clamp(r.row, 0, 127)] = true, any = true;
			if (any) {
				for (int n = 127; n >= 0; n--)
					if (used[n])
						rows.push_back(n);
				return rows;
			}
			mode = 1;
		}
		if (mode == 1) {
			for (int n = base + 36; n >= base; n--)
				if (inScale(n, scale(), keyRoot()))
					rows.push_back(n);
			return rows;
		}
		for (int n = base + 24; n >= base; n--)
			rows.push_back(n);
		return rows;
	}
	int rowCount(int t) const { return isPiano(t) ? (int) pianoRows(t).size() : rowsOfRaster(t); }
	float rowH(int t) const { return rh() / std::max(1, rowCount(t)); }
	/** Row index (top 0) of a row value (pitch / voice / pad), -1 if not visible. */
	int rowIndex(int t, int row, const std::vector<int>& pr) const {
		if (!isPiano(t))
			return row;
		for (size_t i = 0; i < pr.size(); i++)
			if (pr[i] == row)
				return (int) i;
		return -1;
	}
	int rowValueAt(int t, float y, const std::vector<int>& pr) const {
		int i = clamp((int) ((y - ry()) / rowH(t)), 0, rowCount(t) - 1);
		return isPiano(t) ? pr[i] : i;
	}
	Rect itemRect(int t, const Item& it, const std::vector<int>& pr) const {
		int ri = rowIndex(t, it.row, pr);
		if (ri < 0)
			return Rect(Vec(-1000, -1000), Vec(0, 0));
		float len = (t == T_DRUMS) ? 1.f : std::max(0.3f, it.len);
		float x = xOf(it.s + it.off);
		return Rect(Vec(x + 0.5f, ry() + ri * rowH(t) + 0.8f), Vec(std::max(3.f, len * cw() - 1.f), rowH(t) - 1.6f));
	}
	int trackLen(int t, int voice = -1) const {
		int L = P.lenTrack[t];
		if (t == T_DRUMS && voice >= 0 && P.lenDrum[voice])
			L = P.lenDrum[voice];
		return (L > 0 && L < LEN) ? L : LEN;
	}

	// ---------------------------------------------------------------- talking to GENRA HIP-HOP
	void step() override {
		hh = nullptr;
		if (m) {
			if (m->linkedId >= 0) {
				hh = dynamic_cast<GenraHipHop*>(APP->engine->getModule(m->linkedId));
			}
			if (!hh) {
				Module* side[2] = {m->leftExpander.module, m->rightExpander.module};
				for (Module* x : side)
					if (x && x->model == modelGenraHipHop) {
						hh = static_cast<GenraHipHop*>(x);
						break;
					}
			}
		}
		if (hh && drag == D_NONE) {
			uint32_t v = hh->shareVer.load();
			if ((!have || v != ver) && !hh->gridIn.load()) {
				std::lock_guard<std::mutex> lk(hh->shareM);
				P = hh->shared;
				ver = v;
				have = true;
			}
		}
		if (!hh)
			have = false;
		consumeRec();
		if (msgTime > 0.f)
			msgTime -= APP->window->getLastFrameDuration();
		OpaqueWidget::step();
	}
	void say(const std::string& s) {
		msg = s;
		msgTime = 2.5f;
	}
	void begin(int t) {
		undo.push_back(P);
		if (undo.size() > 64)
			undo.erase(undo.begin());
		redo.clear();
		bake(P, t, dens(t));
	}
	void commit() {
		if (hh)
			hh->putFromGrid(P);
	}
	void doUndo() {
		if (undo.empty())
			return;
		redo.push_back(P);
		P = undo.back();
		undo.pop_back();
		sel.clear();
		commit();
	}
	void doRedo() {
		if (redo.empty())
			return;
		undo.push_back(P);
		P = redo.back();
		redo.pop_back();
		sel.clear();
		commit();
	}

	/** The pitch a new / moved note snaps to (SNAP: off, scale, chord at that step). */
	int snapPitch(int t, int s, int pitch) const {
		int mode = m ? m->snap : 1;
		if (mode == 1)
			return snapToScale(pitch, scale(), keyRoot());
		if (mode == 2 && hh) {
			const ChopSet* c = hh->uiChops.load();
			Chord ch = chordAt(P, c ? c->info : SampleInfo(), hh->pads, s);
			int tones[5];
			int n = tonesOf(ch, tones);
			int best = pitch, bd = 99;
			for (int i = 0; i < n; i++)
				for (int o = -1; o <= 1; o++) {
					int cand = pitch - ((pitch % 12) + 12) % 12 + ((tones[i] % 12) + 12) % 12 + 12 * o;
					if (std::abs(cand - pitch) < bd)
						bd = std::abs(cand - pitch), best = cand;
				}
			return best;
		}
		return pitch;
	}
	void snapAll(int t) {
		if (!isPiano(t))
			return;
		begin(t);
		std::vector<Ref> rs;
		items(P, t, rs);
		std::vector<Item> its;
		for (const Ref& r : rs)
			its.push_back(getItem(P, t, r));
		for (const Ref& r : rs)
			erase(P, t, r);
		for (Item& it : its)
			putItem(P, t, it.s, snapPitch(t, it.s, it.row), it);
		sel.clear();
		commit();
		say("Snapped");
	}

	// ---------------------------------------------------------------- REC MIDI
	void consumeRec() {
		if (!m)
			return;
		if (!m->recOn)
			recBegun = false;
		while (m->recTail.load() != m->recHead.load()) {
			GridHH::RecEv e = m->recBuf[m->recTail.load() % GridHH::REC_N];
			m->recTail = m->recTail.load() + 1;
			if (!hh || !have)
				continue;
			float pos = hh->posPub.load();
			if (pos < 0.f)
				continue;
			int t = m->track;
			int L = trackLen(t);
			int s = ((int) std::lround(pos)) % LEN % L;
			if (e.on) {
				Item it;
				it.vel = std::max(0.05f, e.vel / 127.f);
				it.len = 1.f;
				int row = e.note;
				if (t == T_DRUMS) {
					row = -1;
					for (int v = 0; v < DRUMS; v++)
						if (GM_DRUM[v] == e.note)
							row = v;
					if (row < 0 && e.note % 12 < DRUMS)
						row = e.note % 12;
					if (row < 0)
						continue;
				}
				else if (t == T_CHOP) {
					row = e.note - 36;
					if (row < 0 || row >= PADS)
						continue;
				}
				else
					row = snapPitch(t, s, row);
				if (!recBegun || !P.edited[t]) {
					begin(t);
					recBegun = true;
				}
				putItem(P, t, s, row, it);
				recStart[e.note] = s;
				commit();
			}
			else if (recStart[e.note] >= 0) {
				int st = recStart[e.note];
				recStart[e.note] = -1;
				if (isPiano(t) || t == T_CHOP) {
					float len = (float) ((((int) std::lround(pos) - st) % L + L) % L);
					Ref r{st, t == T_CHOP ? e.note - 36 : snapPitch(t, st, e.note)};
					if (has(P, t, r)) {
						Item it = getItem(P, t, r);
						it.len = std::max(1.f, len);
						erase(P, t, r);
						putItem(P, t, st, r.row, it);
						commit();
					}
				}
			}
		}
	}

	// ---------------------------------------------------------------- drawing
	void rectFill(NVGcontext* vg, float x, float y, float w, float h, NVGcolor c, float r = 0.f) {
		nvgBeginPath(vg);
		if (r > 0.f)
			nvgRoundedRect(vg, x, y, w, h, r);
		else
			nvgRect(vg, x, y, w, h);
		nvgFillColor(vg, c);
		nvgFill(vg);
	}

	void drawHeader(NVGcontext* vg) {
		rectFill(vg, 0, 0, box.size.x, HEAD - 2.f, C_LCD, 1.5f);
		auto f = lcdFont();
		int t = trk();
		std::string left = std::string(TRACK_NAME[t]);
		if (hh)
			left += std::string(" / ") + string::uppercase(hh->style().name);
		else
			left += "  -  NO GENRA HIP-HOP: SET IT NEXT TO THE GRID";
		label(vg, f, 5.f, 7.5f, left, 9.f, C_LCDINK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		// chips (right)
		static const char* CH[6] = {"ALL", "SCALE", "USED", "ONE", "GHOST", "ALL 5"};
		for (int i = 0; i < 6; i++) {
			Rect r = chipRect(i);
			bool on = m && (i < 3 ? m->rowsMode == i : m->view == i - 3);
			rectFill(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, on ? C_GRAPH : nvgRGBA(0x3b, 0x3a, 0x38, 0x30), 2.f);
			label(vg, uiBold(), r.pos.x + r.size.x / 2, r.pos.y + r.size.y / 2 + 0.5f, CH[i], 6.5f, on ? C_LCD : C_LCDINK);
		}
		// status (left of the chips)
		std::string st;
		if (P.edited[t])
			st += "EDITED  ";
		st += std::string("SNAP ") + (m ? (m->snap == 0 ? "OFF" : m->snap == 1 ? "SCALE" : "CHORD") : "SCALE");
		if (P.chordFollow)
			st += "  FOLLOW";
		int fb = firstBar(), zb = zoomBars();
		st += zb == 1 ? string::f("  BAR %d/4", fb + 1) : string::f("  BAR %d-%d/4", fb + 1, fb + zb);
		if (hh) {
			char kn[16];
			keyName(hh->set.key, kn, sizeof(kn));
			st += std::string("  ") + kn;
		}
		int L = trackLen(t);
		if (L < LEN)
			st += string::f("  LEN %d", L);
		if (m && m->recOn)
			st = "* REC  " + st;
		label(vg, f, chipRect(0).pos.x - 8.f, 7.5f, st, 8.f, C_LCDINK, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
	}
	Rect chipRect(int i) const {
		float w = 30.f, gap = 3.f, x0 = box.size.x - 4.f - 6 * w - 5 * gap - 6.f;
		float x = x0 + i * (w + gap) + (i >= 3 ? 6.f : 0.f);
		return Rect(Vec(x, 2.f), Vec(w, HEAD - 6.f));
	}

	void drawChords(NVGcontext* vg) {
		if (!hh)
			return;
		const ChopSet* c = hh->uiChops.load();
		SampleInfo none;
		const SampleInfo& si = c ? c->info : none;
		int a = s0(), n = steps();
		Chord prev;
		prev.quality = -2;
		int start = a;
		for (int s = a; s <= a + n; s++) {
			Chord ch = s < a + n ? chordAt(P, si, hh->pads, s) : Chord();
			bool change = s == a + n || ch.root != prev.root || ch.quality != prev.quality;
			if (s > a && change) {
				float x0 = xOf(start), x1 = xOf(s);
				rectFill(vg, x0 + 0.5f, CHORD_Y, x1 - x0 - 1.f, CHORD_H, nvgRGB(0x2e, 0x2d, 0x2b), 2.f);
				char nm[16];
				chordName(prev, nm, sizeof(nm));
				nvgSave(vg);
				nvgIntersectScissor(vg, x0, CHORD_Y, x1 - x0, CHORD_H);
				label(vg, uiBold(), x0 + 4.f, CHORD_Y + CHORD_H / 2 + 0.5f, nm, 8.f, C_CREAM, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
				nvgRestore(vg);
				start = s;
			}
			if (change)
				prev = ch;
		}
	}

	void drawRaster(NVGcontext* vg, int t, float y0, float h, bool active, bool ghostOthers) {
		std::vector<int> pr;
		if (isPiano(t))
			pr = pianoRows(t);
		int rows = isPiano(t) ? (int) pr.size() : rowsOfRaster(t);
		float rhh = h / std::max(1, rows);
		const ChopSet* cset = hh ? hh->uiChops.load() : nullptr;
		SampleInfo none;
		const SampleInfo& si = cset ? cset->info : none;
		// rows
		for (int i = 0; i < rows; i++) {
			float y = y0 + i * rhh;
			NVGcolor bg = (i % 2) ? nvgRGB(0x2c, 0x2b, 0x29) : nvgRGB(0x31, 0x30, 0x2e);
			if (isPiano(t)) {
				int n = pr[i];
				bool black = (((n % 12) + 12) % 12 == 1 || n % 12 == 3 || n % 12 == 6 || n % 12 == 8 || n % 12 == 10);
				bg = black ? nvgRGB(0x29, 0x28, 0x26) : nvgRGB(0x31, 0x30, 0x2e);
				if (inScale(n, scale(), keyRoot()))
					bg = nvgLerpRGBA(bg, nvgRGB(0x50, 0x4e, 0x48), 0.35f);
			}
			rectFill(vg, rx(), y, rw(), rhh, bg);
			// chord tones (piano): tinted per step range
			if (isPiano(t) && hh) {
				int n = pr[i];
				for (int s = s0(); s < s0() + steps(); s += 2) {
					Chord ch = chordAt(P, si, hh->pads, s);
					int tones[5];
					int k = tonesOf(ch, tones);
					for (int q = 0; q < k; q++)
						if (((tones[q] - n) % 12 + 12) % 12 == 0) {
							rectFill(vg, xOf(s), y, 2 * cw(), rhh, nvgRGBA(0x9f, 0xbf, 0x5a, 0x22));
							break;
						}
				}
			}
			// row labels
			if (active) {
				std::string lb;
				if (t == T_DRUMS)
					lb = DRUM_NAME[i];
				else if (t == T_CHOP) {
					lb = string::f("%d", i + 1);
					if (cset && i < cset->info.count) {
						Chord ch = padChord(cset->info, hh->pads, i);
						char nm[16];
						if (ch.valid()) {
							chordName(ch, nm, sizeof(nm));
							lb += std::string(" ") + nm;
						}
					}
				}
				else {
					int n = pr[i];
					lb = string::f("%s%d", NOTE_NAMES[((n % 12) + 12) % 12], n / 12 - 1);
				}
				bool hl = (t == T_DRUMS || t == T_CHOP) && m && m->laneVoice == i;
				rectFill(vg, 0, y + 0.4f, KEYS, rhh - 0.8f, hl ? C_RED : C_CREAM, 1.5f);
				if (rhh >= 6.f)
					label(vg, uiBold(), KEYS - 3.f, y + rhh / 2 + 0.5f, lb, std::min(7.5f, rhh - 1.f), hl ? C_CREAM : C_INK,
					      NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
			}
		}
		// beat lines
		for (int s = 0; s <= steps(); s++) {
			float x = rx() + s * cw();
			nvgBeginPath(vg);
			nvgMoveTo(vg, x, y0);
			nvgLineTo(vg, x, y0 + h);
			int abs = s0() + s;
			nvgStrokeColor(vg, abs % 16 == 0 ? nvgRGB(0x8f, 0x8a, 0x7c) : abs % 4 == 0 ? nvgRGB(0x55, 0x53, 0x4e) : nvgRGB(0x3a, 0x39, 0x36));
			nvgStrokeWidth(vg, abs % 4 == 0 ? 0.9f : 0.4f);
			nvgStroke(vg);
		}
		// past the track's length: darker, repeats paler
		if (t != T_DRUMS) {
			int L = trackLen(t);
			if (L < LEN && xOf(L) < rx() + rw())
				rectFill(vg, std::max(rx(), xOf(L)), y0, rx() + rw() - std::max(rx(), xOf(L)), h, nvgRGBA(0, 0, 0, 0x70));
		}
		else
			for (int v = 0; v < DRUMS; v++) {
				int L = trackLen(t, v);
				if (L < LEN && xOf(L) < rx() + rw())
					rectFill(vg, std::max(rx(), xOf(L)), y0 + v * rhh, rx() + rw() - std::max(rx(), xOf(L)), rhh,
					         nvgRGBA(0, 0, 0, 0x70));
			}
		// ghosts of the other tone tracks
		if (ghostOthers && isPiano(t)) {
			for (int o = T_BASS; o <= T_PAD; o++) {
				if (o == t)
					continue;
				std::vector<Ref> rs;
				items(P, o, rs);
				for (const Ref& r : rs) {
					Item it = getItem(P, o, r);
					int ri = rowIndex(t, it.row, pr);
					if (ri < 0 || it.s < s0() || it.s >= s0() + steps())
						continue;
					nvgBeginPath(vg);
					nvgRoundedRect(vg, xOf(it.s) + 0.5f, y0 + ri * rhh + 0.8f, std::max(3.f, it.len * cw() - 1.f), rhh - 1.6f, 1.5f);
					NVGcolor c = TRACK_COL[o];
					c.a = 0.5f;
					nvgStrokeColor(vg, c);
					nvgStrokeWidth(vg, 0.8f);
					nvgStroke(vg);
				}
			}
		}
		// the items
		std::vector<Ref> rs;
		items(P, t, rs);
		float dn = dens(t);
		for (const Ref& r : rs) {
			Item it = getItem(P, t, r);
			int L = trackLen(t, t == T_DRUMS ? r.row : -1);
			// shown in its cycle: the item and its repeats in the visible range
			for (int rep = 0; rep * L < LEN; rep++) {
				int sAbs = it.s + rep * L;
				if (sAbs >= LEN)
					break;
				if (sAbs < s0() || sAbs >= s0() + steps())
					continue;
				int ri = isPiano(t) ? rowIndex(t, it.row, pr) : it.row;
				if (ri < 0)
					continue;
				float x = xOf(sAbs + it.off) + 0.5f;
				float y = y0 + ri * rhh + 0.8f;
				float w = t == T_DRUMS ? cw() - 1.f : std::max(3.f, it.len * cw() - 1.f);
				float hh2 = rhh - 1.6f;
				bool ghost = !P.edited[t] && it.prio > dn;
				NVGcolor c = TRACK_COL[t];
				if (rep > 0)
					c.a = 0.35f;
				nvgBeginPath(vg);
				nvgRoundedRect(vg, x, y, w, hh2, 1.8f);
				if (ghost) {
					nvgStrokeColor(vg, c);
					nvgStrokeWidth(vg, 0.8f);
					nvgStroke(vg);
				}
				else {
					NVGcolor f = c;
					if (t == T_DRUMS || t == T_CHOP)
						f = nvgLerpRGBA(nvgRGB(0x40, 0x3f, 0x3c), c, 0.35f + 0.65f * it.vel);
					nvgFillColor(vg, f);
					nvgFill(vg);
				}
				if (it.chance < 1.f || it.cond) {
					nvgStrokeColor(vg, C_CREAM);
					nvgStrokeWidth(vg, 0.7f);
					nvgStroke(vg);
					if (w > 14.f && active)
						label(vg, uiBold(), x + w - 1.5f, y + hh2 / 2 + 0.5f,
						      it.cond ? std::string("2nd") : string::f("%d%%", (int) std::round(it.chance * 100.f)), 6.f,
						      nvgRGB(0x1e, 0x1d, 0x1c), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
				}
				bool selected = rep == 0 && std::find(sel.begin(), sel.end(), r) != sel.end() && active;
				if (selected) {
					nvgBeginPath(vg);
					nvgRoundedRect(vg, x - 0.6f, y - 0.6f, w + 1.2f, hh2 + 1.2f, 2.f);
					nvgStrokeColor(vg, nvgRGB(0xff, 0xff, 0xff));
					nvgStrokeWidth(vg, 1.2f);
					nvgStroke(vg);
				}
				if (active && hh2 >= 6.f) {
					std::string tag;
					if (it.acc)
						tag += ">";
					if (it.slide)
						tag += "~";
					if (it.ratchet > 1)
						tag += string::f("x%d", it.ratchet);
					if (t == T_CHOP && it.pitch)
						tag += string::f("%+d", it.pitch);
					if (it.rev)
						tag += "R";
					if (!tag.empty() && w > 8.f)
						label(vg, uiBold(), x + 1.5f, y + hh2 / 2 + 0.5f, tag, 6.f, nvgRGB(0x1e, 0x1d, 0x1c),
						      NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
				}
			}
		}
		// what is being moved (follows the mouse)
		if (active && drag == D_MOVE) {
			int ds = (int) std::round(stepAt(dragNow.x) - stepAt(dragStart.x));
			float dy = dragNow.y - dragStart.y;
			for (const Item& it : moving) {
				int ri = isPiano(t) ? rowIndex(t, it.row, pr) : it.row;
				if (ri < 0)
					continue;
				float x = xOf(it.s + ds + it.off) + 0.5f;
				float w = t == T_DRUMS ? cw() - 1.f : std::max(3.f, it.len * cw() - 1.f);
				float y = y0 + ri * rhh + 0.8f + std::round(dy / rhh) * rhh;
				nvgBeginPath(vg);
				nvgRoundedRect(vg, x, y, w, rhh - 1.6f, 1.8f);
				NVGcolor c = TRACK_COL[t];
				c.a = 0.75f;
				nvgFillColor(vg, c);
				nvgFill(vg);
				nvgStrokeColor(vg, nvgRGB(0xff, 0xff, 0xff));
				nvgStrokeWidth(vg, 1.f);
				nvgStroke(vg);
			}
		}
		// end marks (own length): per drum voice, or one for the track
		if (active) {
			if (t == T_DRUMS)
				for (int v = 0; v < DRUMS; v++)
					drawEndMark(vg, trackLen(t, v), y0 + v * rhh, rhh, P.lenDrum[v] != 0);
			else
				drawEndMark(vg, trackLen(t), y0, h, P.lenTrack[t] != 0);
		}
	}
	void drawEndMark(NVGcontext* vg, int L, float y, float h, bool own) {
		float x = xOf(L);
		if (x < rx() || x > rx() + rw() + 0.5f)
			return;
		if (!own && L == LEN)
			return; // follows the loop: nothing to show at the loop's end
		nvgBeginPath(vg);
		nvgRect(vg, x - 1.f, y, 2.f, h);
		nvgFillColor(vg, nvgRGB(0xf2, 0xc9, 0x4a));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRect(vg, x - 3.f, y + h / 2 - 3.f, 6.f, 6.f);
		nvgFill(vg);
	}
	/** Swing badges: per drum voice, or one for the track (right margin). */
	Rect swingRect(int t, int v) const {
		float x = rx() + rw() + 3.f;
		if (t == T_DRUMS) {
			float rhh = rh() / DRUMS;
			return Rect(Vec(x, ry() + v * rhh + 0.5f), Vec(RIGHT - 4.f, rhh - 1.f));
		}
		return Rect(Vec(x, ry()), Vec(RIGHT - 4.f, 12.f));
	}
	void drawSwing(NVGcontext* vg, int t) {
		int n = t == T_DRUMS ? DRUMS : 1;
		for (int v = 0; v < n; v++) {
			Rect r = swingRect(t, v);
			float sw = t == T_DRUMS ? P.swDrum[v] : P.swTrack[t];
			rectFill(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, sw > 0.f ? nvgRGB(0xf2, 0xc9, 0x4a) : nvgRGB(0x2a, 0x29, 0x28), 2.f);
			label(vg, uiBold(), r.pos.x + r.size.x / 2, r.pos.y + r.size.y / 2 + 0.5f,
			      sw > 0.f ? string::f("%d", (int) std::round(sw * 100.f)) : std::string("SW"), 6.5f,
			      sw > 0.f ? C_INK : nvgRGB(0x8f, 0x8a, 0x7c));
		}
	}

	void drawLane(NVGcontext* vg, int t) {
		float y = laneY(), h = LANE_H;
		rectFill(vg, rx(), y, rw(), h, nvgRGB(0x2a, 0x29, 0x28), 1.5f);
		bool chance = m && m->lane == 1;
		label(vg, uiBold(), KEYS, y + 9.f, "VEL", 7.f, chance ? nvgRGB(0x8f, 0x8a, 0x7c) : C_CREAM, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		label(vg, uiBold(), KEYS, y + 20.f, "CHANCE", 7.f, chance ? C_LCD : nvgRGB(0x8f, 0x8a, 0x7c), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		std::vector<Ref> rs;
		items(P, t, rs);
		for (const Ref& r : rs) {
			if ((t == T_DRUMS || t == T_CHOP) && m && r.row != m->laneVoice && t == T_DRUMS)
				continue;
			if (r.s < s0() || r.s >= s0() + steps())
				continue;
			Item it = getItem(P, t, r);
			float v = chance ? it.chance : it.vel;
			float bh = (h - 4.f) * v;
			rectFill(vg, xOf(r.s) + cw() * 0.3f, y + h - 2.f - bh, std::max(2.f, cw() * 0.4f), bh, TRACK_COL[t], 1.f);
		}
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		int t = trk();
		drawHeader(vg);
		if (!have) {
			label(vg, uiBold(), box.size.x / 2, box.size.y / 2, "SET GENRA HIP-HOP RIGHT NEXT TO THE GRID (OR RIGHT-CLICK > CONNECT)",
			      9.f, nvgRGB(0x8f, 0x8a, 0x7c));
			return;
		}
		if (m && m->view == 2) {
			// ALL 5: a strip per track
			float y = CHORD_Y, h = (box.size.y - CHORD_Y - 2.f) / NUM_TRACKS;
			for (int k = 0; k < NUM_TRACKS; k++) {
				float yy = y + k * h;
				rectFill(vg, 0, yy + 1.f, KEYS, h - 2.f, k == t ? TRACK_COL[k] : C_CREAM, 1.5f);
				label(vg, uiBold(), KEYS / 2, yy + h / 2, TRACK_NAME[k], 7.f, C_INK);
				drawRaster(vg, k, yy + 1.f, h - 2.f, false, false);
				if (k == t) {
					nvgBeginPath(vg);
					nvgRect(vg, rx() - 1.f, yy + 0.5f, rw() + 2.f, h - 1.f);
					nvgStrokeColor(vg, nvgRGB(0xff, 0xff, 0xff));
					nvgStrokeWidth(vg, 1.2f);
					nvgStroke(vg);
				}
			}
		}
		else {
			drawChords(vg);
			drawRaster(vg, t, ry(), rh(), true, m && m->view == 1);
			drawSwing(vg, t);
			drawLane(vg, t);
			if (drag == D_RECT) {
				Rect r = Rect::fromCorners(dragStart, dragNow);
				nvgBeginPath(vg);
				nvgRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
				nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x18));
				nvgFill(vg);
				nvgStrokeColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x90));
				nvgStrokeWidth(vg, 0.8f);
				nvgStroke(vg);
			}
		}
		// the play position
		if (hh) {
			float pos = hh->posPub.load();
			if (pos >= 0.f && pos >= s0() && pos < s0() + steps()) {
				float x = xOf(pos);
				nvgBeginPath(vg);
				nvgRect(vg, x - 0.6f, CHORD_Y, 1.2f, box.size.y - CHORD_Y);
				nvgFillColor(vg, C_RED);
				nvgFill(vg);
			}
		}
		if (msgTime > 0.f && !msg.empty()) {
			rectFill(vg, rx(), laneY() - 14.f, rw(), 12.f, nvgRGBA(0x2a, 0x29, 0x28, 0xe8), 2.f);
			label(vg, uiBold(), rx() + 5.f, laneY() - 8.f, msg, 7.5f, C_CREAM, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		}
	}

	// ---------------------------------------------------------------- finding things under the mouse
	bool inRaster(Vec p) const { return p.x >= rx() && p.x < rx() + rw() && p.y >= ry() && p.y < ry() + rh(); }
	bool inLane(Vec p) const { return p.x >= rx() && p.x < rx() + rw() && p.y >= laneY() && p.y < laneY() + LANE_H; }
	/** The item under the mouse (-1 none), and whether at its right edge. */
	bool hit(Vec p, Ref& out, bool& edge) {
		int t = trk();
		std::vector<int> pr;
		if (isPiano(t))
			pr = pianoRows(t);
		std::vector<Ref> rs;
		items(P, t, rs);
		for (int i = (int) rs.size() - 1; i >= 0; i--) {
			Item it = getItem(P, t, rs[i]);
			Rect r = itemRect(t, it, pr);
			if (t == T_DRUMS)
				r.size.x = cw() - 1.f;
			if (r.contains(p)) {
				out = rs[i];
				edge = t != T_DRUMS && p.x > r.pos.x + r.size.x - 5.f;
				return true;
			}
		}
		return false;
	}
	bool isSel(const Ref& r) const { return std::find(sel.begin(), sel.end(), r) != sel.end(); }

	// ---------------------------------------------------------------- mouse
	void onButton(const ButtonEvent& e) override {
		if (!have || !m) {
			OpaqueWidget::onButton(e);
			return;
		}
		int t = trk();
		Vec p = e.pos;
		// header chips
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && p.y < HEAD) {
			for (int i = 0; i < 6; i++)
				if (chipRect(i).contains(p)) {
					if (i < 3)
						m->rowsMode = i;
					else
						m->view = i - 3;
					e.consume(this);
					return;
				}
		}
		bool dbl = e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && system::getTime() - lastClick < 0.3 &&
		           p.minus(lastClickPos).norm() < 4.f;
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			lastClick = system::getTime();
			lastClickPos = p;
		}
		// ALL 5: click selects the track, double click opens it
		if (m->view == 2) {
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && p.y >= CHORD_Y) {
				int k = clamp((int) ((p.y - CHORD_Y) / ((box.size.y - CHORD_Y - 2.f) / NUM_TRACKS)), 0, NUM_TRACKS - 1);
				selectTrack(k);
				if (dbl)
					m->view = 0;
				e.consume(this);
			}
			return;
		}
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			menu(p);
			e.consume(this);
			return;
		}
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS)
			return;
		int mods = e.mods;
		e.consume(this);
		dragStart = dragNow = p;
		dragDirty = false;
		// row names: the voice / pad for the lane
		if (p.x < KEYS && p.y >= ry() && p.y < ry() + rh() && (t == T_DRUMS || t == T_CHOP)) {
			m->laneVoice = clamp((int) ((p.y - ry()) / rowH(t)), 0, rowsOfRaster(t) - 1);
			return;
		}
		// lane labels
		if (p.x < KEYS && p.y >= laneY()) {
			m->lane = p.y < laneY() + LANE_H / 2 ? 0 : 1;
			return;
		}
		// swing badges
		int nb = t == T_DRUMS ? DRUMS : 1;
		for (int v = 0; v < nb; v++)
			if (swingRect(t, v).contains(p)) {
				float& sw = t == T_DRUMS ? P.swDrum[v] : P.swTrack[t];
				if (dbl) {
					begin(t);
					sw = 0.f;
					commit();
					return;
				}
				begin(t);
				drag = D_SWING;
				dragVoice = v;
				swingStart = sw > 0.f ? sw : (hh ? hh->params[GenraHipHop::SWING_PARAM].getValue() / 100.f : 0.58f);
				return;
			}
		// end marks
		{
			int L = 0, v = -1;
			if (t == T_DRUMS && p.y >= ry() && p.y < ry() + rh()) {
				v = clamp((int) ((p.y - ry()) / rowH(t)), 0, DRUMS - 1);
				L = trackLen(t, v);
			}
			else if (t != T_DRUMS)
				L = trackLen(t);
			if (L && std::fabs(p.x - xOf(L)) < 4.f && p.y >= ry() && p.y < ry() + rh()) {
				begin(t);
				if (dbl) {
					if (v >= 0)
						P.lenDrum[v] = 0;
					else
						P.lenTrack[t] = 0;
					commit();
					return;
				}
				drag = D_LENMARK;
				dragVoice = v;
				return;
			}
		}
		if (inLane(p)) {
			begin(t);
			drag = D_LANE;
			laneAt(p);
			return;
		}
		if (!inRaster(p))
			return;
		Ref r;
		bool edge = false;
		bool on = hit(p, r, edge);
		if (mods & GLFW_MOD_SHIFT) {
			drag = D_RECT;
			if (!(mods & RACK_MOD_CTRL))
				sel.clear();
			return;
		}
		if (mods & RACK_MOD_CTRL) {
			if (on) {
				auto it = std::find(sel.begin(), sel.end(), r);
				if (it != sel.end())
					sel.erase(it);
				else
					sel.push_back(r);
			}
			return;
		}
		if (on && dbl && isPiano(t)) {
			begin(t);
			erase(P, t, r);
			sel.clear();
			commit();
			return;
		}
		if (on && edge) {
			begin(t);
			drag = D_LEN;
			dragS = r.s;
			dragRow = r.row;
			if (!isSel(r))
				sel = {r};
			return;
		}
		if (on && (isSel(r) || isPiano(t))) {
			// move (the selection, or this note)
			if (!isSel(r))
				sel = {r};
			begin(t);
			moving.clear();
			for (const Ref& q : sel)
				if (has(P, t, q))
					moving.push_back(getItem(P, t, q));
			for (const Ref& q : sel)
				erase(P, t, q);
			drag = D_MOVE;
			return;
		}
		std::vector<int> pr;
		if (isPiano(t))
			pr = pianoRows(t);
		int s = clamp((int) std::floor(stepAt(p.x)), 0, LEN - 1);
		int row = rowValueAt(t, p.y, pr);
		sel.clear();
		begin(t);
		if (on) {
			// drums / chop: a click on a hit takes it away, dragging erases more
			erase(P, t, r);
			drag = D_ERASE;
		}
		else {
			Item it;
			it.vel = m->params[GridHH::VEL_PARAM].getValue();
			it.chance = m->params[GridHH::CHANCE_PARAM].getValue();
			it.len = t == T_DRUMS ? 1.f : m->params[GridHH::LEN_PARAM].getValue();
			if (t == T_CHOP)
				it.len = std::min(it.len, 4.f);
			if (isPiano(t))
				row = snapPitch(t, s, row);
			Ref nr = putItem(P, t, s, row, it);
			sel = {nr};
			drag = isPiano(t) ? D_LEN : D_PAINT;
			dragS = nr.s;
			dragRow = nr.row;
		}
		commit();
	}

	void laneAt(Vec p) {
		int t = trk();
		int s = clamp((int) std::floor(stepAt(p.x)), 0, LEN - 1);
		float v = clamp((laneY() + LANE_H - 2.f - p.y) / (LANE_H - 4.f), 0.f, 1.f);
		std::vector<Ref> rs;
		items(P, t, rs);
		for (const Ref& r : rs) {
			if (r.s != s)
				continue;
			if (t == T_DRUMS && r.row != m->laneVoice)
				continue;
			Item it = getItem(P, t, r);
			if (m->lane == 1)
				it.chance = v;
			else
				it.vel = std::max(0.05f, v);
			if (t == T_PAD) {
				P.pad[s].vel = it.vel, P.pad[s].chance = it.chance;
				continue;
			}
			erase(P, t, r);
			putItem(P, t, r.s, r.row, it);
		}
		dragDirty = true;
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (!m || drag == D_NONE)
			return;
		float zoom = getAbsoluteZoom();
		dragNow = dragNow.plus(e.mouseDelta.div(zoom));
		int t = trk();
		Vec p = dragNow;
		std::vector<int> pr;
		if (isPiano(t))
			pr = pianoRows(t);
		switch (drag) {
			case D_PAINT:
			case D_ERASE: {
				if (!inRaster(p))
					break;
				int s = clamp((int) std::floor(stepAt(p.x)), 0, LEN - 1);
				int row = rowValueAt(t, p.y, pr);
				Ref r{s, row};
				if (drag == D_ERASE && has(P, t, r)) {
					erase(P, t, r);
					dragDirty = true;
				}
				if (drag == D_PAINT && !has(P, t, r)) {
					Item it;
					it.vel = m->params[GridHH::VEL_PARAM].getValue();
					it.chance = m->params[GridHH::CHANCE_PARAM].getValue();
					it.len = t == T_CHOP ? std::min(4.f, m->params[GridHH::LEN_PARAM].getValue()) : 1.f;
					putItem(P, t, s, row, it);
					dragDirty = true;
				}
				break;
			}
			case D_LEN: {
				Ref r{dragS, dragRow};
				if (!has(P, t, r))
					break;
				float len = std::max(0.5f, std::round((stepAt(p.x) - dragS) * 2.f) / 2.f);
				if (t == T_PAD)
					P.pad[dragS].len = len;
				else {
					Item it = getItem(P, t, r);
					it.len = len;
					putItem(P, t, dragS, dragRow, it);
				}
				dragDirty = true;
				break;
			}
			case D_LANE: laneAt(p); break;
			case D_LENMARK: {
				int L = clamp((int) std::round(stepAt(p.x)), 1, LEN);
				if (dragVoice >= 0)
					P.lenDrum[dragVoice] = (uint8_t) (L >= LEN ? 0 : L);
				else
					P.lenTrack[t] = (uint8_t) (L >= LEN ? 0 : L);
				dragDirty = true;
				break;
			}
			case D_SWING: {
				float sw = clamp(swingStart + (dragStart.y - p.y) * 0.003f, 0.5f, 0.75f);
				(t == T_DRUMS ? P.swDrum[dragVoice] : P.swTrack[t]) = sw;
				dragDirty = true;
				break;
			}
			default: break;
		}
		if (dragDirty && drag != D_MOVE && drag != D_RECT) {
			commit();
			dragDirty = false;
		}
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (!m)
			return;
		int t = trk();
		if (drag == D_MOVE) {
			std::vector<int> pr;
			if (isPiano(t))
				pr = pianoRows(t);
			int ds = (int) std::round(stepAt(dragNow.x) - stepAt(dragStart.x));
			int drow = 0;
			if (isPiano(t)) {
				int a = rowValueAt(t, dragStart.y, pr), b = rowValueAt(t, dragNow.y, pr);
				drow = b - a;
			}
			else
				drow = (int) std::round((dragNow.y - dragStart.y) / rowH(t));
			sel.clear();
			for (Item& it : moving) {
				int s = clamp(it.s + ds, 0, LEN - 1);
				int row = it.row + drow;
				if (isPiano(t) && drow)
					row = snapPitch(t, s, row);
				if (!isPiano(t))
					row = clamp(row, 0, rowsOfRaster(t) - 1);
				sel.push_back(putItem(P, t, s, row, it));
			}
			moving.clear();
			commit();
		}
		else if (drag == D_RECT) {
			Rect r = Rect::fromCorners(dragStart, dragNow);
			std::vector<int> pr;
			if (isPiano(t))
				pr = pianoRows(t);
			std::vector<Ref> rs;
			items(P, t, rs);
			for (const Ref& q : rs) {
				Rect ir = itemRect(t, getItem(P, t, q), pr);
				if (r.intersects(ir) && !isSel(q))
					sel.push_back(q);
			}
		}
		else if (dragDirty)
			commit();
		drag = D_NONE;
	}

	void onHoverScroll(const HoverScrollEvent& e) override {
		if (!have || !m || m->view == 2)
			return;
		Ref r;
		bool edge;
		if (!inRaster(e.pos) || !hit(e.pos, r, edge))
			return;
		int t = trk();
		float d = e.scrollDelta.y > 0 ? 0.05f : -0.05f;
		begin(t);
		std::vector<Ref> targets = isSel(r) ? sel : std::vector<Ref>{r};
		for (const Ref& q : targets) {
			if (!has(P, t, q))
				continue;
			Item it = getItem(P, t, q);
			it.vel = clamp(it.vel + d, 0.05f, 1.f);
			if (t == T_PAD) {
				P.pad[q.s].vel = it.vel;
				continue;
			}
			erase(P, t, q);
			putItem(P, t, q.s, q.row, it);
		}
		commit();
		e.consume(this);
	}

	// ---------------------------------------------------------------- keys (with the mouse over the grid)
	void onHoverKey(const HoverKeyEvent& e) override {
		if (!have || !m || m->view == 2 || (e.action != GLFW_PRESS && e.action != GLFW_REPEAT))
			return;
		int t = trk();
		bool ctrl = (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL || (e.mods & RACK_MOD_CTRL);
		bool shift = e.mods & GLFW_MOD_SHIFT;
		bool alt = e.mods & GLFW_MOD_ALT;
		auto consume = [&]() { e.consume(this); };
		if (ctrl && e.key == GLFW_KEY_Z) {
			shift ? doRedo() : doUndo();
			return consume();
		}
		if (ctrl && e.key == GLFW_KEY_Y) {
			doRedo();
			return consume();
		}
		if (ctrl && e.key == GLFW_KEY_A) {
			items(P, t, sel);
			return consume();
		}
		if (e.key == GLFW_KEY_ESCAPE) {
			sel.clear();
			return consume();
		}
		if (ctrl && e.key == GLFW_KEY_C) {
			copySel();
			return consume();
		}
		if (ctrl && e.key == GLFW_KEY_V) {
			paste((int) std::floor(stepAt(e.pos.x)));
			return consume();
		}
		if (ctrl && e.key == GLFW_KEY_D) {
			duplicate();
			return consume();
		}
		std::vector<Ref> targets = sel;
		if (targets.empty()) {
			Ref r;
			bool edge;
			if (hit(e.pos, r, edge))
				targets.push_back(r);
		}
		if (targets.empty())
			return;
		if (e.key == GLFW_KEY_DELETE || e.key == GLFW_KEY_BACKSPACE) {
			begin(t);
			for (const Ref& r : targets)
				erase(P, t, r);
			sel.clear();
			commit();
			return consume();
		}
		int ds = 0, drow = 0;
		float nudge = 0.f;
		if (e.key == GLFW_KEY_LEFT || e.key == GLFW_KEY_RIGHT) {
			int dir = e.key == GLFW_KEY_RIGHT ? 1 : -1;
			if (alt)
				nudge = dir / 24.f;
			else
				ds = dir * (shift ? STEPS : 1);
		}
		else if (e.key == GLFW_KEY_UP || e.key == GLFW_KEY_DOWN) {
			int dir = e.key == GLFW_KEY_UP ? 1 : -1;
			drow = isPiano(t) ? dir * (shift ? 12 : 1) : -dir;
		}
		else
			return;
		begin(t);
		std::vector<Item> its;
		for (const Ref& r : targets)
			if (has(P, t, r))
				its.push_back(getItem(P, t, r));
		for (const Ref& r : targets)
			erase(P, t, r);
		sel.clear();
		for (Item& it : its) {
			it.off = clamp(it.off + nudge, -11.f / 24.f, 11.f / 24.f);
			int row = it.row + drow;
			if (!isPiano(t))
				row = clamp(row, 0, rowsOfRaster(t) - 1);
			sel.push_back(putItem(P, t, clamp(it.s + ds, 0, LEN - 1), row, it));
		}
		commit();
		consume();
	}

	void copySel() {
		int t = trk();
		clip.clear();
		int first = LEN;
		for (const Ref& r : sel)
			first = std::min(first, r.s);
		for (const Ref& r : sel)
			if (has(P, t, r)) {
				Item it = getItem(P, t, r);
				it.s -= first;
				clip.push_back(it);
			}
		say(string::f("Copied %d", (int) clip.size()));
	}
	void paste(int at) {
		int t = trk();
		if (clip.empty())
			return;
		begin(t);
		sel.clear();
		for (const Item& it : clip) {
			int s = at + it.s;
			if (s < 0 || s >= LEN)
				continue;
			int row = isPiano(t) ? it.row : clamp(it.row, 0, rowsOfRaster(t) - 1);
			sel.push_back(putItem(P, t, s, row, it));
		}
		commit();
	}
	void duplicate() {
		if (sel.empty())
			return;
		int a = LEN, b = 0;
		for (const Ref& r : sel)
			a = std::min(a, r.s), b = std::max(b, r.s + 1);
		copySel();
		paste(b + ((b - a) % 4 ? 4 - (b - a) % 4 : 0) - 0);
	}

	// ---------------------------------------------------------------- track tools
	void tool(int which) {
		int t = trk();
		begin(t);
		std::vector<Ref> rs = sel;
		if (rs.empty())
			items(P, t, rs);
		std::vector<Item> its;
		for (const Ref& r : rs)
			if (has(P, t, r))
				its.push_back(getItem(P, t, r));
		int L = trackLen(t);
		int a = 0, b = L;
		if (!sel.empty()) {
			a = LEN, b = 0;
			for (const Item& it : its)
				a = std::min(a, it.s), b = std::max(b, it.s + 1);
		}
		int span = std::max(1, b - a);
		for (const Ref& r : rs)
			erase(P, t, r);
		sel.clear();
		for (Item& it : its) {
			int s = it.s, row = it.row;
			switch (which) {
				case 0: s = a + ((s - a - 1) % span + span) % span; break; // rotate left
				case 1: s = a + ((s - a + 1) % span + span) % span; break; // rotate right
				case 2: s = a + (span - 1 - (s - a)); break;                // reverse
				case 3:
					s = a + (s - a) / 2;
					it.len = std::max(0.25f, it.len / 2.f);
					break; // double speed
				case 4:
					s = a + (s - a) * 2;
					it.len *= 2.f;
					break; // half speed
				case 5:
				case 6: {
					if (!isPiano(t))
						break;
					int dir = which == 5 ? 1 : -1;
					int n = row + dir;
					while (!inScale(n, scale(), keyRoot()) && std::abs(n - row) < 3)
						n += dir;
					row = n;
					break;
				}
				case 7: it.vel = clamp(it.vel * (1.f + random::uniform() * 0.3f - 0.15f), 0.05f, 1.f); break;
				case 8: it.vel = 0.8f; break;
			}
			if (s < 0 || s >= LEN)
				continue;
			sel.push_back(putItem(P, t, s, row, it));
		}
		commit();
	}

	// ---------------------------------------------------------------- right-click
	void menu(Vec p) {
		int t = trk();
		Ref r;
		bool edge;
		bool on = inRaster(p) && hit(p, r, edge);
		if (on && !isSel(r))
			sel = {r};
		std::vector<Ref> targets = sel;
		ui::Menu* menu = createMenu();
		GridView* g = this;
		auto apply = [=](std::function<void(Item&)> f) {
			g->begin(t);
			for (const Ref& q : targets) {
				if (!has(g->P, t, q))
					continue;
				Item it = getItem(g->P, t, q);
				f(it);
				if (t == T_PAD) {
					PadEv& e = g->P.pad[q.s];
					e.vel = it.vel, e.chance = it.chance, e.cond = it.cond, e.len = it.len;
					continue;
				}
				erase(g->P, t, q);
				putItem(g->P, t, q.s, q.row, it);
			}
			g->commit();
		};
		if (on) {
			menu->addChild(createMenuLabel(string::f("%d selected", (int) targets.size())));
			if (t == T_DRUMS || t == T_CHOP)
				menu->addChild(createSubmenuItem("Roll", "", [=](ui::Menu* sub) {
					for (int k = 1; k <= 4; k++)
						sub->addChild(createMenuItem(k == 1 ? "Off" : string::f("%dx", k), "", [=]() {
							apply([=](Item& it) { it.ratchet = (uint8_t) k; });
						}));
				}));
			if (t == T_BASS || t == T_MEL) {
				menu->addChild(createMenuItem("Accent on/off", "", [=]() { apply([](Item& it) { it.acc = !it.acc; }); }));
				menu->addChild(createMenuItem("Slide on/off", "", [=]() { apply([](Item& it) { it.slide = !it.slide; }); }));
			}
			if (t == T_CHOP) {
				menu->addChild(createSubmenuItem("Pitch", "", [=](ui::Menu* sub) {
					for (int k = 12; k >= -12; k--)
						sub->addChild(createMenuItem(string::f("%+d", k), "", [=]() { apply([=](Item& it) { it.pitch = k; }); }));
				}));
				menu->addChild(createMenuItem("Reverse on/off", "", [=]() { apply([](Item& it) { it.rev = !it.rev; }); }));
			}
			menu->addChild(createSubmenuItem("Velocity", "", [=](ui::Menu* sub) {
				for (int k = 100; k >= 20; k -= 20)
					sub->addChild(createMenuItem(string::f("%d%%", k), "", [=]() { apply([=](Item& it) { it.vel = k / 100.f; }); }));
			}));
			menu->addChild(createSubmenuItem("Chance", "", [=](ui::Menu* sub) {
				static const int C[] = {100, 90, 75, 50, 25, 10};
				for (int k : C)
					sub->addChild(createMenuItem(string::f("%d%%", k), "", [=]() { apply([=](Item& it) { it.chance = k / 100.f; }); }));
			}));
			menu->addChild(createMenuItem("Only every 2nd time on/off", "", [=]() {
				apply([](Item& it) { it.cond = it.cond ? 0 : 1; });
			}));
			menu->addChild(createMenuItem("Delete", "Del", [=]() {
				g->begin(t);
				for (const Ref& q : targets)
					erase(g->P, t, q);
				g->sel.clear();
				g->commit();
			}));
			menu->addChild(new ui::MenuSeparator);
		}
		menu->addChild(createSubmenuItem("Track tools", sel.empty() ? "whole track" : "selection", [=](ui::Menu* sub) {
			static const char* N[9] = {"Rotate left", "Rotate right", "Reverse", "Double speed", "Half speed", "Transpose up (scale)",
			                           "Transpose down (scale)", "Velocity random ±15%", "Velocity even"};
			for (int k = 0; k < 9; k++) {
				if ((k == 5 || k == 6) && !isPiano(t))
					continue;
				sub->addChild(createMenuItem(N[k], "", [=]() { g->tool(k); }));
			}
		}));
		menu->addChild(createMenuItem("Select all", "Ctrl+A", [=]() { items(g->P, t, g->sel); }));
		menu->addChild(createMenuItem("Copy", "Ctrl+C", [=]() { g->copySel(); }));
		int at = clamp((int) std::floor(stepAt(p.x)), 0, LEN - 1);
		menu->addChild(createMenuItem("Paste here", "Ctrl+V", [=]() { g->paste(at); }));
		menu->addChild(createMenuItem("Duplicate after", "Ctrl+D", [=]() { g->duplicate(); }));
	}

	// ---------------------------------------------------------------- buttons
	void selectTrack(int k) {
		if (!m)
			return;
		m->pitchBase[m->track] = m->params[GridHH::PITCH_PARAM].getValue();
		m->track = k;
		m->params[GridHH::PITCH_PARAM].setValue(m->pitchBase[k]);
		sel.clear();
		if (m->laneVoice >= rowsOfRaster(k))
			m->laneVoice = 0;
	}
	void copyBar() {
		int t = trk();
		barClip.clear();
		int a = firstBar() * STEPS;
		std::vector<Ref> rs;
		items(P, t, rs);
		for (const Ref& r : rs)
			if (r.s >= a && r.s < a + STEPS) {
				Item it = getItem(P, t, r);
				it.s -= a;
				barClip.push_back(it);
			}
		barClipTrack = t;
		say(string::f("Bar %d copied", firstBar() + 1));
	}
	void pasteBar() {
		int t = trk();
		if (barClipTrack != t) {
			say("Copy a bar of this track first");
			return;
		}
		begin(t);
		int a = firstBar() * STEPS;
		std::vector<Ref> rs;
		items(P, t, rs);
		for (const Ref& r : rs)
			if (r.s >= a && r.s < a + STEPS)
				erase(P, t, r);
		for (const Item& it : barClip)
			putItem(P, t, a + it.s, it.row, it);
		commit();
		say(string::f("Pasted into bar %d", firstBar() + 1));
	}
	void clearTrack() {
		int t = trk();
		begin(t);
		std::vector<Ref> rs;
		items(P, t, rs);
		for (const Ref& r : rs)
			erase(P, t, r);
		sel.clear();
		commit();
	}
	void genTrack() {
		if (hh)
			hh->reqNew.fetch_or(1 << trk());
		sel.clear();
		say("Generated again");
	}
	void toggleFollow() {
		begin(trk());
		P.chordFollow = !P.chordFollow;
		commit();
	}

	// ---------------------------------------------------------------- MIDI
	std::string exportMidi(int only) {
		if (!hh)
			return "No GENRA HIP-HOP";
		Render r;
		render(P, hh->knobs, r);
		std::vector<uint8_t> f = hhx::midiFile(r, hh->params[GenraHipHop::BPM_PARAM].getValue(),
		                                       hh->params[GenraHipHop::SWING_PARAM].getValue() / 100.f, hh->pads,
		                                       (int) hh->params[GenraHipHop::PITCH_PARAM].getValue(), P, only);
		std::string dir = hhx::midiFolder();
		std::string name = only < 0 ? "HipHop grid" : std::string("HipHop ") + string::lowercase(TRACK_NAME[only]);
		try {
			system::createDirectories(dir);
			std::string file = hhx::freeName(dir, name, ".mid");
			system::writeFile(system::join(dir, file), f);
			return "Saved " + file;
		}
		catch (Exception& e) {
			return e.what();
		}
	}
	void importMidi() {
		osdialog_filters* fl = osdialog_filters_parse("MIDI:mid,midi,MID");
		std::string dir = hhx::midiFolder();
		char* p = osdialog_file(OSDIALOG_OPEN, dir.c_str(), nullptr, fl);
		osdialog_filters_free(fl);
		if (!p)
			return;
		std::string path = p;
		std::free(p);
		try {
			std::vector<uint8_t> b = system::readFile(path);
			Pattern n = P;
			std::string err;
			if (!readMidi(b, n, err)) {
				say(err);
				return;
			}
			undo.push_back(P);
			redo.clear();
			P = n;
			sel.clear();
			commit();
			say("Imported " + system::getFilename(path));
		}
		catch (Exception& e) {
			say(e.what());
		}
	}
};

// ================================================================ the panel
struct SnapLabel : widget::Widget {
	GridHH* m = nullptr;
	void draw(const DrawArgs& args) override {
		int s = m ? m->snap : 1;
		label(args.vg, uiBold(), box.size.x / 2, box.size.y / 2, s == 0 ? "OFF" : s == 1 ? "SCALE" : "CHORD", 6.5f,
		      s == 0 ? C_INKDIM : C_RED);
	}
};
struct LinkLed : widget::Widget {
	GridView* g = nullptr;
	void draw(const DrawArgs& args) override {
		bool on = g && g->hh;
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, box.size.x / 2, box.size.y / 2, 3.8f);
		nvgFillColor(args.vg, on ? nvgRGB(0x7f, 0xd3, 0x6b) : nvgRGB(0x8f, 0x8a, 0x7c));
		nvgFill(args.vg);
	}
};

struct GridHHWidget : ModuleWidget {
	GridView* grid = nullptr;

	template <typename T>
	T* at(const float* r) {
		T* w = new T();
		w->box = rectOf(r);
		addChild(w);
		return w;
	}
	HHButton* button(const float* r, const char* text, NVGcolor col, float fs, std::function<void()> act) {
		HHButton* b = at<HHButton>(r);
		b->text = text;
		b->col = col;
		b->fontSize = fs;
		b->act = act;
		return b;
	}

	GridHHWidget(GridHH* m) {
		setModule(m);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/GridHH.svg")));
		grid = at<GridView>(ghl::GRID);
		grid->m = m;
		GridView* g = grid;
		LinkLed* led = new LinkLed;
		led->box = Rect(Vec(ghl::LINK[0] - 5.f, ghl::LINK[1] - 5.f), Vec(10.f, 10.f));
		led->g = g;
		addChild(led);
		const float* TR[5] = {ghl::TRK0, ghl::TRK1, ghl::TRK2, ghl::TRK3, ghl::TRK4};
		const char* TN[5] = {"DRUMS", "BASS", "MEL", "PADS", "CHOP"};
		for (int k = 0; k < NUM_TRACKS; k++) {
			HHButton* b = button(TR[k], TN[k], C_GRAPH, 7.f, [=]() { g->selectTrack(k); });
			b->lit = [=]() { return m && m->track == k; };
		}
		addParam(createParamCentered<HHKnob>(Vec(ghl::BAR[0], ghl::BAR[1]), m, GridHH::BAR_PARAM));
		addParam(createParamCentered<HHKnob>(Vec(ghl::ZOOM[0], ghl::ZOOM[1]), m, GridHH::ZOOM_PARAM));
		addParam(createParamCentered<HHKnob>(Vec(ghl::PITCH[0], ghl::PITCH[1]), m, GridHH::PITCH_PARAM));
		HHButton* au = button(ghl::AUTO, "AUTO", C_GRAPH, 7.f, [=]() {
			if (m)
				m->autoScroll = !m->autoScroll;
		});
		au->lit = [=]() { return m && m->autoScroll; };
		addParam(createParamCentered<HHKnob>(Vec(ghl::VEL[0], ghl::VEL[1]), m, GridHH::VEL_PARAM));
		addParam(createParamCentered<HHKnob>(Vec(ghl::LEN[0], ghl::LEN[1]), m, GridHH::LEN_PARAM));
		addParam(createParamCentered<HHKnob>(Vec(ghl::CHANCE[0], ghl::CHANCE[1]), m, GridHH::CHANCE_PARAM));
		button(ghl::SNAP, "SNAP", C_GRAPH, 7.f, [=]() {
			if (!m)
				return;
			if (APP->window->getMods() & GLFW_MOD_SHIFT)
				g->snapAll(m->track);
			else
				m->snap = (m->snap + 1) % 3;
		});
		SnapLabel* sl = at<SnapLabel>(ghl::SNAPMODE);
		sl->m = m;
		HHButton* fo = button(ghl::FOLLOW, "FOLLOW", C_GRAPH, 6.2f, [=]() { g->toggleFollow(); });
		fo->lit = [=]() { return g->have && g->P.chordFollow; };
		button(ghl::UNDO, "UNDO", C_GRAPH, 6.2f, [=]() { g->doUndo(); });
		button(ghl::REDO, "REDO", C_GRAPH, 6.2f, [=]() { g->doRedo(); });
		button(ghl::COPY, "COPY", C_GRAPH, 6.2f, [=]() { g->copyBar(); });
		button(ghl::PASTE, "PASTE", C_GRAPH, 6.2f, [=]() { g->pasteBar(); });
		button(ghl::CLEAR, "CLEAR", C_GRAPH, 6.2f, [=]() { g->clearTrack(); });
		button(ghl::GEN, "GEN", C_RED, 6.2f, [=]() { g->genTrack(); });
		HHButton* rec = button(ghl::REC, "REC", C_RED, 7.f, [=]() {
			if (m)
				m->recOn = !m->recOn;
		});
		rec->lit = [=]() { return m && m->recOn; };
	}

	void appendContextMenu(Menu* menu) override {
		GridHH* m = dynamic_cast<GridHH*>(module);
		if (!m)
			return;
		GridView* g = grid;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Connection"));
		menu->addChild(createCheckMenuItem("Automatic (GENRA HIP-HOP right next to the grid)", "", [=]() { return m->linkedId < 0; },
		                                   [=]() { m->linkedId = -1; }));
		menu->addChild(createSubmenuItem("Connect to…", "", [=](Menu* sub) {
			std::vector<int64_t> ids = APP->engine->getModuleIds();
			int n = 0;
			for (int64_t id : ids) {
				Module* x = APP->engine->getModule(id);
				if (!x || x->model != modelGenraHipHop)
					continue;
				n++;
				GenraHipHop* h = static_cast<GenraHipHop*>(x);
				std::string nm = string::f("GENRA HIP-HOP #%d (%s)", n, h->style().name);
				sub->addChild(createCheckMenuItem(nm, "", [=]() { return m->linkedId == id; }, [=]() { m->linkedId = id; }));
			}
			if (!n)
				sub->addChild(createMenuLabel("No GENRA HIP-HOP in the patch"));
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("MIDI"));
		menu->addChild(createMenuItem("Export as MIDI file (all tracks)", "", [=]() { g->say(g->exportMidi(-1)); }));
		menu->addChild(createMenuItem(string::f("Export %s only", TRACK_NAME[m->track]), "", [=]() { g->say(g->exportMidi(m->track)); }));
		menu->addChild(createMenuItem("Import MIDI file…", "", [=]() { g->importMidi(); }));
		menu->addChild(createMenuItem("MIDI export folder…", "", []() { hhx::pickMidiFolder(); }));
		menu->addChild(createSubmenuItem("MIDI input (for REC)", "", [=](Menu* sub) { app::appendMidiMenu(sub, &m->midiIn); }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Keys (mouse over the grid)"));
		menu->addChild(createMenuLabel("Shift+drag frame, Ctrl+click pick, Ctrl+A/C/V/D, Del"));
		menu->addChild(createMenuLabel("Arrows move (Shift: octave / bar), Alt+←/→ nudge 1/24"));
		menu->addChild(createMenuLabel("Ctrl+Z undo, Ctrl+Y redo, Shift+SNAP snaps the track"));
	}
};

Model* modelGridHH = createModel<GridHH, GridHHWidget>("GenraGridHH");
