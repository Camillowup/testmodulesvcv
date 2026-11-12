// SPDX-License-Identifier: GPL-3.0-only
// GENRA CHOP ABLAUF: the song arranger for GENRA CHOP (26 HP), in GENRA CHOP's look, after GENRA's ABLAUF.
//
// PATTERNS: 64 places (banks A-D, 4 x 4 pads). An empty pad stores what GENRA CHOP plays ("GENRA 1", a new family);
// a part button (INTRO, VERSE, HOOK, BRIDGE, BREAK, OUTRO) makes a variation of the family's original on the next free
// pad ("HOOK 1", "HOOK 2" …) and plays it; every press makes a new one (as in GENRA's ABLAUF): bass, melody and pads
// get new notes, drums and the chop track stay the original's (shaped by the part), the chords stay.
// CLEAR, then a pad: that pad is empty. CLEAR ALL twice: all 64 pads are empty (the song list stays). UNDO brings them back.
// SONG: a list of rows (pattern, bars, fill). MAKE SONG builds a song in the FORM (AUTO: the style's form) from the
// selected pad's family, using variations that are there and making the missing ones. PATT | SONG: the mode. PLAY in
// SONG starts at the selected row, in PATT the selected pad. One sample per song: the pads keep patterns, not samples.
//
// How it plays: GENRA CHOP keeps its clock. On the last 16th of every bar ABLAUF decides what the next bar is and hands
// GENRA CHOP that bar's pattern (the row's pattern with the part's course over the row and the transition on its last
// bar, hh/Song); a new row restarts GENRA CHOP's loop at step 1. All in the audio thread, so a stalled panel (a file
// dialog) never makes a row come late.
#include "GenraChopModule.hpp"
#include "ChopUI.hpp"
#include "ChopAblaufLayout.hpp"
#include "MidiLearn.hpp"
#include "hh/Song.hpp"

static const int SLOTS = 64, ROWS_MAX = 256, UNDO_MAX = 12;
static const NVGcolor FAMILY_COL[8] = {nvgRGB(0xc8, 0x31, 0x2b), nvgRGB(0x6f, 0x8f, 0xa8), nvgRGB(0xd6, 0xa6, 0x40),
                                       nvgRGB(0x7f, 0xa8, 0x5a), nvgRGB(0x9a, 0x6f, 0xa8), nvgRGB(0x5f, 0xa8, 0xa0),
                                       nvgRGB(0xc8, 0x7a, 0x4a), nvgRGB(0x77, 0x73, 0x6a)};
static const NVGcolor PART_COL[NUM_PARTS] = {nvgRGB(0xa9, 0xb9, 0x8c), nvgRGB(0x8f, 0x8a, 0x7c), nvgRGB(0xd6, 0xa6, 0x40),
                                             nvgRGB(0x9a, 0x6f, 0xa8), nvgRGB(0x6f, 0x8f, 0xa8), nvgRGB(0xc8, 0x7a, 0x4a)};
static const int PART_BARS[NUM_PARTS] = {4, 16, 8, 8, 4, 4};
// what a part button rolls anew (fixed): bass, melody, pads
static const int RENEW = (1 << T_BASS) | (1 << T_MEL) | (1 << T_PAD);

struct Slot {
	bool used = false;
	Pattern pat;
	std::string name;
	int part = -1;   // -1: an original (from GENRA CHOP)
	int family = -1; // which original it comes from
};
struct Row {
	int slot = -1;
	int bars = 4;
	bool fill = false;
};

struct ChopAblauf : Module {
	enum ParamId { NUM_PARAMS };
	enum InputId { ROWCV_INPUT, RST_INPUT, RUN_INPUT, NEXT_INPUT, PREV_INPUT, PADCV_INPUT, NUM_INPUTS };
	enum OutputId { END_OUTPUT, BAR_OUTPUT, ROW_OUTPUT, NUM_OUTPUTS };

	// ---- the pads and the song (the panel edits under dataM, the audio thread only try_locks)
	std::mutex dataM;
	Slot slots[SLOTS];
	std::vector<Row> rows;
	int families = 0;
	// ---- the panel's state (saved)
	int bank = 0;
	std::atomic<int> selSlot{-1}, selRow{0};
	std::atomic<bool> songMode{false};
	int form = -1; // -1 AUTO
	// CLEAR (UI thread): armed until a pad is tapped; CLEAR ALL: armed for a moment, the second press clears
	bool clearArmed = false;
	float clearAllArmed = 0.f;
	bool loop = false;
	int64_t linkedId = -1;
	struct Snap {
		std::vector<Slot> slots;
		std::vector<Row> rows;
		int families;
	};
	std::vector<Snap> undo;

	// ---- GENRA CHOP (found by the panel; checked against the registry when one goes)
	std::atomic<GenraChop*> host{nullptr};
	std::atomic<bool> hostChanged{true};
	GenraChop* h = nullptr; // the audio thread's checked copy
	uint32_t hostEpoch = 0;

	// ---- playing (audio thread)
	std::atomic<bool> playing{false};  // SONG: the song runs
	std::atomic<int> curRow{-1};       // the row playing
	std::atomic<int> barInRow{0};
	std::atomic<int> playSlot{-1};     // the pad playing (PATT, or the row's)
	std::atomic<int> queuedSlot{-1};   // PATT: waits for the end of the pattern
	std::atomic<int> reqPlay{0};       // 1 PLAY, 2 STOP
	std::atomic<bool> loadNow{false};  // PATT with GENRA CHOP stopped: the selected pad goes in at once
	int lastStep = -1;
	bool decided = false;              // this bar's end is decided
	int nextRow = -1, nextBar = 0, nextSlot = -1;
	int pendingRow = -1;               // PLAY while running: this row from the next bar
	int stopIn = -1;                   // samples until GENRA CHOP stops (the song's end)
	bool wasRunning = false;
	dsp::SchmittTrigger runT, rstT, nextT, prevT;
	dsp::PulseGenerator endP, barP, rowP;
	int lastPadCv = -1, lastRowCv = -1;
	Pattern work;

	ml::CtrlIn ctrl[ml::CTRLS];

	ChopAblauf() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		configInput(ROWCV_INPUT, "Row CV (0-10 V over the song list, from the next bar)");
		configInput(RST_INPUT, "Reset (song to the first row)");
		configInput(RUN_INPUT, "Run (trigger: play / stop)");
		configInput(NEXT_INPUT, "Next (row at the next bar / pad at the end of the pattern)");
		configInput(PREV_INPUT, "Previous (row / pad)");
		configInput(PADCV_INPUT, "Pad CV (0-10 V over the 64 pads, at the end of the pattern)");
		configOutput(END_OUTPUT, "End of the song (trigger)");
		configOutput(BAR_OUTPUT, "Bar (trigger, while ABLAUF plays)");
		configOutput(ROW_OUTPUT, "New row (trigger)");
	}

	// ---------------------------------------------------------------- helpers (with dataM held)
	int partOf(int slot) const { return slot >= 0 && slot < SLOTS && slots[slot].used ? slots[slot].part : -1; }
	/** The pattern for one bar of a row, shaped (bar 0..). */
	void barPattern(Pattern& out, int slot, int bar, int rowBars, bool fill, int nextPart, int style) {
		out = slots[slot].pat;
		int part = slots[slot].part;
		if (part >= 0)
			shapeBar(out, part, bar, rowBars, fill, nextPart, style, (uint32_t) (slot * 131 + bar));
	}

	// ---------------------------------------------------------------- the audio thread
	void process(const ProcessArgs& args) override {
		// ---- GENRA CHOP still there?
		uint32_t ep = ChopRegistry::get().epoch.load();
		if (hostChanged.exchange(false) || ep != hostEpoch) {
			hostEpoch = ep;
			GenraChop* c = host.load();
			GenraChop* before = h;
			h = (c && ChopRegistry::get().alive(c)) ? c : nullptr;
			if (before && before != h && ChopRegistry::get().alive(before))
				before->songPart = -1; // the one we left shows no part any more
		}
		float sr = args.sampleRate;
		if (!h) {
			lastStep = -1;
			playing = false;
			curRow = -1;
			outputs[END_OUTPUT].setVoltage(endP.process(args.sampleTime) ? 10.f : 0.f);
			outputs[BAR_OUTPUT].setVoltage(0.f);
			outputs[ROW_OUTPUT].setVoltage(0.f);
			return;
		}
		bool song = songMode.load();
		// ---- jacks
		if (runT.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f))
			reqPlay = h->running.load() ? 2 : 1;
		if (rstT.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			selRow = 0;
			if (song && playing)
				pendingRow = 0;
		}
		int dir = 0;
		if (nextT.process(inputs[NEXT_INPUT].getVoltage(), 0.1f, 1.f))
			dir = 1;
		if (prevT.process(inputs[PREV_INPUT].getVoltage(), 0.1f, 1.f))
			dir = -1;
		if (dir) {
			if (song) {
				int n = (int) rows.size();
				if (n > 0) {
					int base = playing ? curRow.load() : selRow.load();
					int r = clamp(base + dir, 0, n - 1);
					selRow = r;
					if (playing)
						pendingRow = r;
				}
			}
			else {
				int s = selSlot.load();
				for (int k = 1; k <= SLOTS; k++) {
					int c = ((s + dir * k) % SLOTS + SLOTS) % SLOTS;
					if (slots[c].used) {
						selSlot = c;
						queuedSlot = c;
						break;
					}
				}
			}
		}
		if ((args.frame & 63) == 0) {
			if (inputs[PADCV_INPUT].isConnected()) {
				int c = clamp((int) (inputs[PADCV_INPUT].getVoltage() / 10.f * SLOTS), 0, SLOTS - 1);
				if (c != lastPadCv) {
					lastPadCv = c;
					if (slots[c].used) {
						selSlot = c;
						queuedSlot = c;
						songMode = song = false;
					}
				}
			}
			else
				lastPadCv = -1;
			if (inputs[ROWCV_INPUT].isConnected() && !rows.empty()) {
				int r = clamp((int) (inputs[ROWCV_INPUT].getVoltage() / 10.f * rows.size()), 0, (int) rows.size() - 1);
				if (r != lastRowCv) {
					lastRowCv = r;
					selRow = r;
					if (song && playing)
						pendingRow = r;
				}
			}
			else
				lastRowCv = -1;
		}
		// ---- PLAY / STOP
		int rp = reqPlay.exchange(0);
		if (rp == 2) {
			if (!h->running.load() && !playing)
				selRow = 0; // STOP twice: back to the song's start
			h->running = false;
			playing = false;
			pendingRow = -1;
			queuedSlot = -1;
			stopIn = -1;
		}
		if (rp == 1) {
			if (song) {
				if (!startRow(selRow.load(), !h->running.load()))
					reqPlay = 1; // the pads are being edited: next sample
			}
			else {
				int s = selSlot.load();
				if (s >= 0 && slots[s].used) {
					if (h->running.load())
						queuedSlot = s;
					else if (!loadSlot(s, true))
						reqPlay = 1;
				}
				else
					h->running = true;
			}
		}
		if (loadNow.exchange(false) && !h->running.load()) {
			int s = selSlot.load();
			if (s >= 0 && !loadSlot(s, false))
				loadNow = true;
		}
		if (stopIn > 0 && --stopIn == 0) {
			h->running = false;
			playing = false;
		}
		// STOP at GENRA CHOP stops the song too (the row stays marked)
		bool run = h->running.load();
		if (wasRunning && !run && playing && stopIn < 0) {
			playing = false;
			pendingRow = -1;
		}
		wasRunning = run;
		// ---- the bars, by GENRA CHOP's step
		int st = h->step.load();
		if (!h->running.load())
			st = -1;
		if (st != lastStep) {
			if (st >= 0 && st % STEPS == 0) {
				decided = false;
				if (playing || playSlot.load() >= 0)
					barP.trigger(1e-3f);
				if (nextRow >= 0 && playing) {
					if (nextRow != curRow.load())
						rowP.trigger(1e-3f);
					curRow = nextRow;
					barInRow = nextBar;
					nextRow = -1;
				}
				else if (nextSlot >= 0 && !playing) {
					playSlot = nextSlot;
					barInRow = nextBar;
					nextSlot = -1;
				}
			}
			lastStep = st;
		}
		if (st >= 0 && st % STEPS == STEPS - 1 && !decided)
			decided = decideNext(st, sr);
		outputs[END_OUTPUT].setVoltage(endP.process(args.sampleTime) ? 10.f : 0.f);
		outputs[BAR_OUTPUT].setVoltage(barP.process(args.sampleTime) ? 10.f : 0.f);
		outputs[ROW_OUTPUT].setVoltage(rowP.process(args.sampleTime) ? 10.f : 0.f);
		if ((args.frame & 255) == 0) {
			std::unique_lock<std::mutex> lk(dataM, std::try_to_lock);
			if (lk.owns_lock()) {
				int part = -1;
				if (playing)
					part = partOf(rowSlot(curRow.load()));
				else if (playSlot.load() >= 0)
					part = partOf(playSlot.load());
				h->songPart = part;
			}
		}
	}

	void onRemove(const RemoveEvent& e) override {
		// the engine is locked here: GENRA CHOP stops showing a part
		if (h && ChopRegistry::get().alive(h))
			h->songPart = -1;
	}

	int rowSlot(int r) const { return r >= 0 && r < (int) rows.size() ? rows[r].slot : -1; }

	/** Hand GENRA CHOP a pattern (it takes it within a few samples). */
	bool push(const Pattern& p) {
		if (!h->shareM.try_lock())
			return false;
		h->shared = p;
		h->gridIn = true;
		h->shareM.unlock();
		return true;
	}

	/** Song from row r: at once (GENRA CHOP stopped) or from the next bar. */
	bool startRow(int r, bool now) {
		if (rows.empty())
			return true;
		r = clamp(r, 0, (int) rows.size() - 1);
		if (!now) {
			pendingRow = r;
			playing = true;
			stopIn = -1;
			return true;
		}
		std::unique_lock<std::mutex> lk(dataM, std::try_to_lock);
		if (!lk.owns_lock())
			return false;
		const Row& row = rows[r];
		if (row.slot < 0 || !slots[row.slot].used)
			return true;
		int np = r + 1 < (int) rows.size() ? partOf(rows[r + 1].slot) : (loop ? partOf(rows[0].slot) : -1);
		barPattern(work, row.slot, 0, row.bars, row.fill, np, h->set.style);
		if (!push(work))
			return false;
		h->reqJump = 0;
		curRow = r;
		barInRow = 0;
		nextRow = -1;
		playing = true;
		stopIn = -1;
		h->running = true;
		rowP.trigger(1e-3f);
		return true;
	}

	/** PATT: a pad into GENRA CHOP (start: and run). */
	bool loadSlot(int s, bool start) {
		std::unique_lock<std::mutex> lk(dataM, std::try_to_lock);
		if (!lk.owns_lock())
			return false;
		if (!slots[s].used)
			return true;
		barPattern(work, s, 0, BARS, false, -1, h->set.style);
		if (!push(work))
			return false;
		h->reqJump = 0;
		playSlot = s;
		barInRow = 0;
		if (start)
			h->running = true;
		return true;
	}

	/** On the last 16th of a bar: what the next bar plays. False: try again next sample. */
	bool decideNext(int st, float sr) {
		bool song = songMode.load();
		std::unique_lock<std::mutex> lk(dataM, std::try_to_lock);
		if (!lk.owns_lock())
			return false;
		int style = h->set.style;
		if (song && playing) {
			int r = curRow.load(), b = barInRow.load() + 1;
			bool jump = false;
			if (pendingRow >= 0) {
				r = pendingRow;
				b = 0;
				jump = true;
				pendingRow = -1;
			}
			else if (r < 0 || r >= (int) rows.size() || b >= rows[r].bars) {
				r = r + 1;
				b = 0;
				jump = true;
				if (r >= (int) rows.size()) {
					endP.trigger(1e-3f);
					if (!loop || rows.empty()) {
						// the end: GENRA CHOP stops with this bar
						stopIn = std::max(1, (int) (h->stepPeriod * 0.9f));
						nextRow = -1;
						return true;
					}
					r = 0;
				}
			}
			if (r < 0 || r >= (int) rows.size())
				return true;
			const Row& row = rows[r];
			if (row.slot < 0 || !slots[row.slot].used) {
				nextRow = r;
				nextBar = b;
				return true;
			}
			int np = r + 1 < (int) rows.size() ? partOf(rows[r + 1].slot) : (loop ? partOf(rows[0].slot) : -1);
			barPattern(work, row.slot, b, row.bars, row.fill, np, style);
			if (!push(work))
				return false;
			if (jump)
				h->reqJump = 0;
			nextRow = r;
			nextBar = b;
			selRow = r;
			return true;
		}
		// PATT: a waiting pad starts at the end of the pattern; a part's course runs over the 4-bar loop
		int q = queuedSlot.load();
		if (q >= 0 && st == LEN - 1) {
			queuedSlot = -1;
			if (slots[q].used) {
				barPattern(work, q, 0, BARS, false, -1, style);
				if (!push(work))
					return false;
				h->reqJump = 0;
				nextSlot = q;
				nextBar = 0;
			}
			return true;
		}
		int ps = playSlot.load();
		if (ps >= 0 && slots[ps].used && slots[ps].part >= 0) {
			int b = (st / STEPS + 1) % BARS;
			barPattern(work, ps, b, BARS, false, -1, style);
			if (!push(work))
				return false;
		}
		return true;
	}

	// ---------------------------------------------------------------- the panel's actions (UI thread)
	GenraChop* uiHost() {
		GenraChop* c = host.load();
		return c && ChopRegistry::get().alive(c) ? c : nullptr;
	}
	void snapshot() {
		std::lock_guard<std::mutex> lk(dataM);
		Snap s;
		s.slots.assign(slots, slots + SLOTS);
		s.rows = rows;
		s.families = families;
		undo.push_back(std::move(s));
		if ((int) undo.size() > UNDO_MAX)
			undo.erase(undo.begin());
	}
	void doUndo() {
		if (undo.empty())
			return;
		std::lock_guard<std::mutex> lk(dataM);
		Snap& s = undo.back();
		for (int i = 0; i < SLOTS && i < (int) s.slots.size(); i++)
			slots[i] = s.slots[i];
		rows = s.rows;
		families = s.families;
		undo.pop_back();
		selRow = clamp(selRow.load(), 0, std::max(0, (int) rows.size() - 1));
	}
	int freeSlot(int from) const {
		for (int k = 0; k < SLOTS; k++) {
			int i = (from + k) % SLOTS;
			if (!slots[i].used)
				return i;
		}
		return -1;
	}
	/** What GENRA CHOP plays, as a pattern. */
	bool hostPattern(Pattern& p) {
		GenraChop* c = uiHost();
		if (!c)
			return false;
		std::lock_guard<std::mutex> lk(c->shareM);
		p = c->shared;
		return true;
	}
	/** An empty pad: what GENRA CHOP plays, a new family ("GENRA n"). */
	std::string storeHost(int i) {
		Pattern p;
		if (!hostPattern(p))
			return "No GENRA CHOP connected";
		snapshot();
		std::lock_guard<std::mutex> lk(dataM);
		Slot& s = slots[i];
		s.used = true;
		s.pat = p;
		s.part = -1;
		s.family = families++;
		s.name = string::f("GENRA %d", s.family + 1);
		return "";
	}
	std::string overwrite(int i) {
		Pattern p;
		if (!hostPattern(p))
			return "No GENRA CHOP connected";
		snapshot();
		std::lock_guard<std::mutex> lk(dataM);
		slots[i].pat = p;
		return "";
	}
	int originalOf(int family) const {
		for (int i = 0; i < SLOTS; i++)
			if (slots[i].used && slots[i].family == family && slots[i].part < 0)
				return i;
		return -1;
	}
	int countPart(int family, int part) const {
		int n = 0;
		for (int i = 0; i < SLOTS; i++)
			if (slots[i].used && slots[i].family == family && slots[i].part == part)
				n++;
		return n;
	}
	/** The family to work from: the selected pad's, else the one playing, else -1. */
	int workFamily() const {
		int s = selSlot.load();
		if (s >= 0 && slots[s].used)
			return slots[s].family;
		int p = playSlot.load();
		if (p >= 0 && slots[p].used)
			return slots[p].family;
		return -1;
	}
	/** A part button: a variation of the family's original on the next free pad (stored first if there is none). */
	std::string makeVariation(int part) {
		GenraChop* c = uiHost();
		if (!c)
			return "No GENRA CHOP connected";
		int fam = workFamily();
		int orig = fam >= 0 ? originalOf(fam) : -1;
		if (orig < 0) {
			int f = freeSlot(bank * 16);
			if (f < 0)
				return "All 64 pads are full";
			std::string e = storeHost(f);
			if (!e.empty())
				return e;
			orig = f;
		}
		int dst = freeSlot(bank * 16);
		if (dst < 0)
			return "All 64 pads are full";
		const ChopSet* cs = c->uiChops.load();
		Pattern out;
		makePart(out, slots[orig].pat, part, RENEW, c->set, cs ? cs->info : SampleInfo(), c->pads, random::u32(), c->bpmNow());
		snapshot();
		{
			std::lock_guard<std::mutex> lk(dataM);
			Slot& s = slots[dst];
			s.used = true;
			s.pat = out;
			s.part = part;
			s.family = slots[orig].family;
			s.name = string::f("%s %d", PART_NAMES[part], countPart(s.family, part));
		}
		bank = dst / 16;
		selSlot = dst;
		if (!songMode.load()) {
			if (c->running.load())
				queuedSlot = dst;
			else
				loadNow = true;
		}
		return "";
	}
	/** MAKE SONG: the form from the selected family: variations that are there, the missing ones made. */
	std::string makeSong() {
		GenraChop* c = uiHost();
		int fam = workFamily();
		if (fam < 0) {
			if (!c)
				return "No GENRA CHOP connected";
			int f = freeSlot(bank * 16);
			if (f < 0)
				return "All 64 pads are full";
			std::string e = storeHost(f);
			if (!e.empty())
				return e;
			selSlot = f;
			fam = slots[f].family;
		}
		int orig = originalOf(fam);
		if (orig < 0)
			return "The family's original pad is gone";
		int style = c ? c->set.style : 5;
		const SongForm& F = FORMS[form >= 0 ? form : autoForm(style)];
		snapshot();
		std::vector<Row> nr;
		int used[NUM_PARTS] = {};
		std::string err;
		for (int i = 0; i < F.n; i++) {
			int part = F.part[i];
			// the existing variations of this part, by number; the n-th time the part comes, the n-th one
			std::vector<int> have;
			for (int s = 0; s < SLOTS; s++)
				if (slots[s].used && slots[s].family == fam && slots[s].part == part)
					have.push_back(s);
			std::sort(have.begin(), have.end(), [&](int a, int b) { return slots[a].name < slots[b].name; });
			int pick = -1;
			if (used[part] < (int) have.size())
				pick = have[used[part]];
			else if (!have.empty() && (!c || part == P_HOOK))
				pick = have[0]; // a hook comes back as itself
			else if (c) {
				int dst = freeSlot(orig);
				if (dst < 0) {
					err = "The pads are full: the song uses what is there";
					pick = have.empty() ? orig : have[0];
				}
				else {
					const ChopSet* cs = c->uiChops.load();
					Pattern out;
					makePart(out, slots[orig].pat, part, RENEW, c->set, cs ? cs->info : SampleInfo(), c->pads, random::u32(),
					         c->bpmNow());
					std::lock_guard<std::mutex> lk(dataM);
					Slot& s = slots[dst];
					s.used = true;
					s.pat = out;
					s.part = part;
					s.family = fam;
					s.name = string::f("%s %d", PART_NAMES[part], countPart(fam, part));
					pick = dst;
				}
			}
			else
				pick = orig;
			used[part]++;
			Row r;
			r.slot = pick;
			r.bars = F.bars[i];
			nr.push_back(r);
		}
		// transitions before every change of part
		for (size_t i = 0; i + 1 < nr.size(); i++)
			nr[i].fill = partOf(nr[i].slot) != partOf(nr[i + 1].slot);
		{
			std::lock_guard<std::mutex> lk(dataM);
			rows = nr;
		}
		selRow = 0;
		songMode = true;
		return err.empty() ? string::f("Song: %s, %d rows", F.name, (int) nr.size()) : err;
	}
	void insertRow() {
		int s = selSlot.load();
		if (s < 0 || !slots[s].used || (int) rows.size() >= ROWS_MAX)
			return;
		snapshot();
		std::lock_guard<std::mutex> lk(dataM);
		Row r;
		r.slot = s;
		r.bars = slots[s].part >= 0 ? PART_BARS[slots[s].part] : 8;
		int at = rows.empty() ? 0 : clamp(selRow.load() + 1, 0, (int) rows.size());
		rows.insert(rows.begin() + at, r);
		selRow = at;
	}
	void deleteRow(int r) {
		if (r < 0 || r >= (int) rows.size())
			return;
		snapshot();
		std::lock_guard<std::mutex> lk(dataM);
		rows.erase(rows.begin() + r);
		selRow = clamp(selRow.load(), 0, std::max(0, (int) rows.size() - 1));
	}
	void clearSlot(int i) {
		snapshot();
		std::lock_guard<std::mutex> lk(dataM);
		slots[i] = Slot();
		for (Row& r : rows)
			if (r.slot == i)
				r.slot = -1;
	}
	/** CLEAR ALL: every pad empty; the song list stays as it is (its rows play nothing until their pads are filled). */
	void clearAll() {
		snapshot();
		std::lock_guard<std::mutex> lk(dataM);
		for (Slot& s : slots)
			s = Slot();
		families = 0;
		selSlot = -1;
	}

	// ---------------------------------------------------------------- MIDI export (no dialog)
	std::string exportSong() {
		GenraChop* c = uiHost();
		if (!c)
			return "No GENRA CHOP connected";
		if (rows.empty())
			return "The song is empty";
		std::vector<Pattern> pats;
		std::vector<Render> rens;
		std::vector<hhx::MidiPart> parts;
		Knobs k = c->knobs;
		{
			std::lock_guard<std::mutex> lk(dataM);
			int n = 0;
			for (const Row& r : rows)
				if (r.slot >= 0 && slots[r.slot].used)
					n += r.bars;
			pats.resize(n);
			rens.resize(n);
			int i = 0;
			for (size_t ri = 0; ri < rows.size(); ri++) {
				const Row& r = rows[ri];
				if (r.slot < 0 || !slots[r.slot].used)
					continue;
				int np = ri + 1 < rows.size() ? partOf(rows[ri + 1].slot) : -1;
				for (int b = 0; b < r.bars; b++, i++) {
					barPattern(pats[i], r.slot, b, r.bars, r.fill, np, c->set.style);
					render(pats[i], k, rens[i]);
				}
			}
			i = 0;
			for (const Row& r : rows) {
				if (r.slot < 0 || !slots[r.slot].used)
					continue;
				for (int b = 0; b < r.bars; b++, i++) {
					hhx::MidiPart mp;
					mp.r = &rens[i];
					mp.pat = &pats[i];
					mp.from = (b % BARS) * STEPS;
					mp.steps = STEPS;
					if (b == 0)
						mp.marker = slots[r.slot].name;
					parts.push_back(mp);
				}
			}
		}
		std::vector<uint8_t> f = hhx::midiSong(parts, c->bpmNow(), c->params[GenraChop::SWING_PARAM].getValue() / 100.f, c->pads,
		                                       (int) c->params[GenraChop::PITCH_PARAM].getValue());
		std::string dir = hhx::midiFolder();
		try {
			system::createDirectories(dir);
			std::string file = hhx::freeName(dir, std::string("Chop song ") + c->style().shortName, ".mid");
			system::writeFile(system::join(dir, file), f);
			return "Saved " + file;
		}
		catch (Exception& e) {
			return e.what();
		}
	}
	std::string exportPad(int i) {
		GenraChop* c = uiHost();
		if (!c || !slots[i].used)
			return "No GENRA CHOP connected";
		Render r;
		render(slots[i].pat, c->knobs, r);
		std::vector<uint8_t> f = hhx::midiFile(r, c->bpmNow(), c->params[GenraChop::SWING_PARAM].getValue() / 100.f, c->pads,
		                                       (int) c->params[GenraChop::PITCH_PARAM].getValue(), slots[i].pat);
		std::string dir = hhx::midiFolder();
		try {
			system::createDirectories(dir);
			std::string file = hhx::freeName(dir, string::f("%c%d %s", 'A' + i / 16, i % 16 + 1, slots[i].name.c_str()), ".mid");
			system::writeFile(system::join(dir, file), f);
			return "Saved " + file;
		}
		catch (Exception& e) {
			return e.what();
		}
	}

	// ---------------------------------------------------------------- saving
	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "version", json_integer(1));
		json_object_set_new(r, "bank", json_integer(bank));
		json_object_set_new(r, "selSlot", json_integer(selSlot));
		json_object_set_new(r, "selRow", json_integer(selRow));
		json_object_set_new(r, "songMode", json_boolean(songMode));
		json_object_set_new(r, "form", json_integer(form));
		json_object_set_new(r, "loop", json_boolean(loop));
		json_object_set_new(r, "linkedId", json_integer(linkedId));
		json_object_set_new(r, "families", json_integer(families));
		std::lock_guard<std::mutex> lk(dataM);
		json_t* js = json_array();
		for (int i = 0; i < SLOTS; i++) {
			if (!slots[i].used)
				continue;
			json_t* o = json_object();
			json_object_set_new(o, "index", json_integer(i));
			json_object_set_new(o, "name", json_string(slots[i].name.c_str()));
			json_object_set_new(o, "part", json_integer(slots[i].part));
			json_object_set_new(o, "family", json_integer(slots[i].family));
			patToJson(o, slots[i].pat);
			json_array_append_new(js, o);
		}
		json_object_set_new(r, "pads", js);
		json_t* jr = json_array();
		for (const Row& w : rows)
			json_array_append_new(jr, json_pack("[iib]", w.slot, w.bars, (int) w.fill));
		json_object_set_new(r, "rows", jr);
		return r;
	}
	void dataFromJson(json_t* r) override {
		auto gi = [&](const char* k, int d) {
			json_t* j = json_object_get(r, k);
			return j ? (int) json_integer_value(j) : d;
		};
		bank = clamp(gi("bank", 0), 0, 3);
		selSlot = clamp(gi("selSlot", -1), -1, SLOTS - 1);
		selRow = std::max(0, gi("selRow", 0));
		songMode = jbool(r, "songMode", false);
		form = clamp(gi("form", -1), -1, NUM_FORMS - 1);
		loop = jbool(r, "loop", false);
		linkedId = gi("linkedId", -1);
		families = std::max(0, gi("families", 0));
		std::lock_guard<std::mutex> lk(dataM);
		for (Slot& s : slots)
			s = Slot();
		json_t* js = json_object_get(r, "pads");
		for (size_t i = 0; i < json_array_size(js); i++) {
			json_t* o = json_array_get(js, i);
			int idx = (int) json_integer_value(json_object_get(o, "index"));
			if (idx < 0 || idx >= SLOTS)
				continue;
			Slot& s = slots[idx];
			s.used = true;
			json_t* nm = json_object_get(o, "name");
			s.name = nm ? json_string_value(nm) : "";
			s.part = clamp((int) json_integer_value(json_object_get(o, "part")), -1, NUM_PARTS - 1);
			s.family = (int) json_integer_value(json_object_get(o, "family"));
			families = std::max(families, s.family + 1);
			patFromJson(o, s.pat);
		}
		rows.clear();
		json_t* jr = json_object_get(r, "rows");
		for (size_t i = 0; i < json_array_size(jr) && (int) rows.size() < ROWS_MAX; i++) {
			json_t* o = json_array_get(jr, i);
			Row w;
			w.slot = clamp((int) jnum(o, 0), -1, SLOTS - 1);
			w.bars = clamp((int) jnum(o, 1), 1, 64);
			w.fill = json_is_true(json_array_get(o, 2));
			rows.push_back(w);
		}
		hostChanged = true;
	}
};

// ================================================================ the panel
struct AblaufWidgetBase;

struct APad : widget::OpaqueWidget {
	ChopAblauf* m = nullptr;
	AblaufWidgetBase* w = nullptr;
	int idx = 0; // 0..15 in the bank
	int slot() const { return (m ? m->bank : 0) * 16 + idx; }
	void draw(const DrawArgs& args) override;
	void onButton(const ButtonEvent& e) override;
};

struct AList : widget::OpaqueWidget {
	ChopAblauf* m = nullptr;
	AblaufWidgetBase* w = nullptr;
	int top = 0;
	static constexpr float RH = 10.f;
	void draw(const DrawArgs& args) override;
	void onButton(const ButtonEvent& e) override;
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (!m || m->rows.empty())
			return;
		int d = e.scrollDelta.y > 0 ? -1 : (e.scrollDelta.y < 0 ? 1 : 0);
		m->selRow = clamp(m->selRow.load() + d, 0, (int) m->rows.size() - 1);
		e.consume(this);
	}
};

struct AMode : widget::OpaqueWidget {
	ChopAblauf* m = nullptr;
	void draw(const DrawArgs& args) override {
		bool song = m && m->songMode;
		float hw = box.size.x / 2;
		for (int i = 0; i < 2; i++) {
			bool on = (i == 1) == song;
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, i * hw + (i ? 0.5f : 0.f), 0, hw - 0.5f, box.size.y, 2.4f);
			nvgFillColor(args.vg, on ? C_YELLOW : nvgRGB(0x55, 0x53, 0x4f));
			nvgFill(args.vg);
			label(args.vg, uiBold(), i * hw + hw / 2, box.size.y / 2 + 0.5f, i ? "SONG" : "PATT", 8.5f, on ? C_YELLOWINK : C_CREAM);
		}
	}
	void onButton(const ButtonEvent& e) override {
		if (m && e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			m->songMode = e.pos.x > box.size.x / 2;
			e.consume(this);
		}
	}
};

struct ALed : widget::Widget {
	ChopAblauf* m = nullptr;
	void draw(const DrawArgs& args) override {
		bool on = m && m->uiHost();
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, box.size.x / 2, box.size.y / 2, box.size.x / 2);
		nvgFillColor(args.vg, on ? nvgRGB(0x5f, 0xbf, 0x4a) : nvgRGB(0x55, 0x53, 0x4f));
		nvgFill(args.vg);
	}
};

struct AInfo : widget::Widget {
	ChopAblauf* m = nullptr;
	std::string* msg = nullptr;
	float* msgTime = nullptr;
	void draw(const DrawArgs& args) override {
		std::string t;
		if (msg && msgTime && *msgTime > 0.f)
			t = *msg;
		else if (m) {
			int s = m->selSlot.load();
			if (s >= 0 && m->slots[s].used)
				t = string::f("%c%d  %s", 'A' + s / 16, s % 16 + 1, m->slots[s].name.c_str());
			else if (!m->uiHost())
				t = "SET GENRA CHOP NEXT TO ABLAUF";
			else
				t = "TAP AN EMPTY PAD: STORES WHAT GENRA CHOP PLAYS";
		}
		label(args.vg, uiBold(), 2.f, box.size.y / 2, string::uppercase(t), 7.5f, C_CREAM, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	}
};

struct AblaufWidgetBase : ModuleWidget, ml::LearnHost {
	ml::Learner learn;
	std::string msg;
	float msgTime = 0.f;
	ml::Learner* learner() override { return &learn; }
	void say(const std::string& s) {
		if (s.empty())
			return;
		msg = s;
		msgTime = 3.f;
	}
};

// ---------------------------------------------------------------- a pad
void APad::draw(const DrawArgs& args) {
	NVGcontext* vg = args.vg;
	int s = slot();
	const Slot* sl = m ? &m->slots[s] : nullptr;
	bool used = sl && sl->used;
	nvgBeginPath(vg);
	nvgRoundedRect(vg, 0.6f, 0.6f, box.size.x - 1.2f, box.size.y - 1.2f, 3.5f);
	nvgFillColor(vg, used ? C_PAD : nvgRGB(0x4a, 0x48, 0x45));
	nvgFill(vg);
	nvgStrokeColor(vg, used ? C_PADEDGE : nvgRGB(0x5a, 0x58, 0x54));
	nvgStrokeWidth(vg, 1.f);
	nvgStroke(vg);
	label(vg, uiBold(), box.size.x / 2, 10.f, string::f("%c%d", 'A' + s / 16, s % 16 + 1), 6.5f, used ? C_INKDIM : nvgRGB(0x77, 0x73, 0x6a));
	if (used) {
		// the family on top, the part below the name
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 2.5f, 2.5f, box.size.x - 5.f, 3.5f, 1.5f);
		nvgFillColor(vg, FAMILY_COL[((sl->family % 8) + 8) % 8]);
		nvgFill(vg);
		label(vg, uiBold(), box.size.x / 2, box.size.y / 2 + 2.f, sl->name, 7.f, C_INK);
		if (sl->part >= 0) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, 6.f, box.size.y - 7.f, box.size.x - 12.f, 2.5f, 1.f);
			nvgFillColor(vg, PART_COL[sl->part]);
			nvgFill(vg);
		}
	}
	if (!m)
		return;
	bool playing = m->playing ? m->rowSlot(m->curRow.load()) == s : m->playSlot.load() == s;
	if (playing && m->uiHost() && m->uiHost()->running) {
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.6f, 0.6f, box.size.x - 1.2f, box.size.y - 1.2f, 3.5f);
		nvgStrokeColor(vg, nvgRGB(0xff, 0xff, 0xff));
		nvgStrokeWidth(vg, 2.f);
		nvgStroke(vg);
	}
	else if (m->queuedSlot.load() == s) {
		float a = 0.5f + 0.5f * std::sin(10.f * (float) system::getTime());
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.6f, 0.6f, box.size.x - 1.2f, box.size.y - 1.2f, 3.5f);
		nvgStrokeColor(vg, nvgRGBAf(1.f, 1.f, 1.f, a));
		nvgStrokeWidth(vg, 1.5f);
		nvgStroke(vg);
	}
	if (m->selSlot.load() == s) {
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 2.f, 2.f, box.size.x - 4.f, box.size.y - 4.f, 2.5f);
		nvgStrokeColor(vg, C_RED);
		nvgStrokeWidth(vg, 1.5f);
		nvgStroke(vg);
	}
}

/** A text field in a menu: Enter renames the pad. */
struct RenameField : ui::TextField {
	ChopAblauf* m = nullptr;
	int slot = 0;
	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
			std::string t = string::uppercase(text).substr(0, 10);
			m->snapshot();
			{
				std::lock_guard<std::mutex> lk(m->dataM);
				m->slots[slot].name = t;
			}
			ui::MenuOverlay* o = getAncestorOfType<ui::MenuOverlay>();
			if (o)
				o->requestDelete();
			e.consume(this);
		}
		if (!e.isConsumed())
			ui::TextField::onSelectKey(e);
	}
};

void APad::onButton(const ButtonEvent& e) {
	if (!m || e.action != GLFW_PRESS)
		return;
	int s = slot();
	if (e.button == GLFW_MOUSE_BUTTON_LEFT && m->clearArmed) {
		// CLEAR was pressed: this pad is emptied
		m->clearArmed = false;
		if (m->slots[s].used) {
			m->clearSlot(s);
			w->say(string::f("Pad %c%d cleared (UNDO brings it back)", 'A' + s / 16, s % 16 + 1));
		}
		e.consume(this);
		return;
	}
	if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
		if (!m->slots[s].used)
			w->say(m->storeHost(s));
		m->selSlot = s;
		if (!m->songMode.load() && m->slots[s].used) {
			GenraChop* c = m->uiHost();
			if (c && c->running)
				m->queuedSlot = s;
			else
				m->loadNow = true;
		}
		e.consume(this);
	}
	if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
		ChopAblauf* mm = m;
		AblaufWidgetBase* ww = w;
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Pad %c%d", 'A' + s / 16, s % 16 + 1)));
		if (m->slots[s].used) {
			menu->addChild(createMenuLabel("Name (Enter):"));
			RenameField* tf = new RenameField;
			tf->m = mm;
			tf->slot = s;
			tf->text = m->slots[s].name;
			tf->box.size.x = 140.f;
			menu->addChild(tf);
			menu->addChild(createSubmenuItem("Part", m->slots[s].part >= 0 ? PART_NAMES[m->slots[s].part] : "original", [=](ui::Menu* sub) {
				sub->addChild(createCheckMenuItem("Original (no part)", "", [=]() { return mm->slots[s].part < 0; }, [=]() {
					mm->snapshot();
					std::lock_guard<std::mutex> lk(mm->dataM);
					mm->slots[s].part = -1;
				}));
				for (int p = 0; p < NUM_PARTS; p++)
					sub->addChild(createCheckMenuItem(PART_NAMES[p], "", [=]() { return mm->slots[s].part == p; }, [=]() {
						mm->snapshot();
						std::lock_guard<std::mutex> lk(mm->dataM);
						mm->slots[s].part = p;
					}));
			}));
			menu->addChild(createMenuItem("Insert into the song", "", [=]() {
				mm->selSlot = s;
				mm->insertRow();
			}));
			menu->addChild(createMenuItem("Overwrite with GENRA CHOP", "", [=]() { ww->say(mm->overwrite(s)); }));
			menu->addChild(createMenuItem("Export as MIDI", "", [=]() { ww->say(mm->exportPad(s)); }));
			menu->addChild(createMenuItem("Clear pad", "", [=]() { mm->clearSlot(s); }));
		}
		else
			menu->addChild(createMenuItem("Store what GENRA CHOP plays", "", [=]() {
				ww->say(mm->storeHost(s));
				mm->selSlot = s;
			}));
		e.consume(this);
	}
}

// ---------------------------------------------------------------- the song list
void AList::draw(const DrawArgs& args) {
	NVGcontext* vg = args.vg;
	nvgBeginPath(vg);
	nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 1.5f);
	nvgFillColor(vg, C_LCD);
	nvgFill(vg);
	if (!m) {
		label(vg, lcdFont(), 5.f, 8.f, "1  INTRO 1        4", 9.f, C_LCDINK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		return;
	}
	int n = (int) m->rows.size();
	int vis = std::max(1, (int) ((box.size.y - 9.f) / RH));
	int sel = m->selRow.load(), cur = m->playing ? m->curRow.load() : -1;
	int focus = cur >= 0 ? cur : sel;
	if (focus < top)
		top = focus;
	if (focus >= top + vis)
		top = focus - vis + 1;
	top = clamp(top, 0, std::max(0, n - vis));
	if (n == 0)
		label(vg, lcdFont(), 5.f, 8.f, "EMPTY: INS OR MAKE SONG", 8.5f, C_LCDINK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	int totalBars = 0, barsBefore = 0;
	for (int i = 0; i < n; i++) {
		if (i < cur)
			barsBefore += m->rows[i].bars;
		totalBars += m->rows[i].bars;
	}
	for (int i = top; i < n && i < top + vis; i++) {
		const Row& r = m->rows[i];
		float y = 1.f + (i - top) * RH;
		if (i == sel || i == cur) {
			nvgBeginPath(vg);
			nvgRect(vg, 1.5f, y, box.size.x - 3.f, RH - 0.5f);
			nvgFillColor(vg, i == cur ? nvgRGB(0x7f, 0x90, 0x60) : nvgRGB(0x93, 0xa4, 0x76));
			nvgFill(vg);
		}
		std::string nm = "-";
		if (r.slot >= 0 && m->slots[r.slot].used) {
			nm = m->slots[r.slot].name;
			nvgBeginPath(vg);
			nvgRect(vg, 16.f, y + 2.f, 3.f, RH - 4.5f);
			nvgFillColor(vg, FAMILY_COL[((m->slots[r.slot].family % 8) + 8) % 8]);
			nvgFill(vg);
		}
		label(vg, lcdFont(), 4.f, y + RH / 2, string::f("%d", i + 1), 8.5f, C_LCDINK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		label(vg, lcdFont(), 23.f, y + RH / 2, nm, 8.5f, C_LCDINK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		std::string right = string::f("%s%2d BARS", r.fill ? "F  " : "", r.bars);
		if (i == cur)
			right = string::f("%d/%d  ", m->barInRow.load() + 1, r.bars) + right;
		label(vg, lcdFont(), box.size.x - 4.f, y + RH / 2, right, 8.f, C_LCDINK, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
	}
	// bottom line: bars and time
	GenraChop* c = m->uiHost();
	float bpm = c ? c->bpmNow() : 90.f;
	auto mmss = [&](int bars) {
		int sec = (int) (bars * 4 * 60.f / std::max(20.f, bpm));
		return string::f("%d:%02d", sec / 60, sec % 60);
	};
	std::string bl = cur >= 0 ? string::f("BAR %d / %d  %s / %s", barsBefore + m->barInRow.load() + 1, totalBars,
	                                      mmss(barsBefore + m->barInRow.load()).c_str(), mmss(totalBars).c_str())
	                          : string::f("%d BARS  %s%s", totalBars, mmss(totalBars).c_str(), m->loop ? "  LOOP" : "");
	label(vg, lcdFont(), 4.f, box.size.y - 4.5f, bl, 7.5f, nvgRGB(0x3a, 0x4a, 0x2a), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
}

void AList::onButton(const ButtonEvent& e) {
	if (!m || e.action != GLFW_PRESS)
		return;
	int i = top + (int) ((e.pos.y - 1.f) / RH);
	if (i < 0 || i >= (int) m->rows.size())
		return;
	m->selRow = i;
	if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
		ChopAblauf* mm = m;
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Row %d", i + 1)));
		menu->addChild(createSubmenuItem("Bars", string::f("%d", m->rows[i].bars), [=](ui::Menu* sub) {
			for (int b : {1, 2, 4, 6, 8, 12, 16, 24, 32})
				sub->addChild(createCheckMenuItem(string::f("%d", b), "", [=]() { return i < (int) mm->rows.size() && mm->rows[i].bars == b; },
				                                  [=]() {
					                                  mm->snapshot();
					                                  std::lock_guard<std::mutex> lk(mm->dataM);
					                                  if (i < (int) mm->rows.size())
						                                  mm->rows[i].bars = b;
				                                  }));
		}));
		menu->addChild(createCheckMenuItem("Fill / transition at the end", "", [=]() { return i < (int) mm->rows.size() && mm->rows[i].fill; },
		                                   [=]() {
			                                   mm->snapshot();
			                                   std::lock_guard<std::mutex> lk(mm->dataM);
			                                   if (i < (int) mm->rows.size())
				                                   mm->rows[i].fill = !mm->rows[i].fill;
		                                   }));
		int s = m->selSlot.load();
		if (s >= 0 && m->slots[s].used)
			menu->addChild(createMenuItem("Pattern: the selected pad (" + m->slots[s].name + ")", "", [=]() {
				mm->snapshot();
				std::lock_guard<std::mutex> lk(mm->dataM);
				if (i < (int) mm->rows.size())
					mm->rows[i].slot = s;
			}));
		menu->addChild(createMenuItem("Move up", "", [=]() {
			if (i <= 0)
				return;
			mm->snapshot();
			std::lock_guard<std::mutex> lk(mm->dataM);
			std::swap(mm->rows[i], mm->rows[i - 1]);
			mm->selRow = i - 1;
		}));
		menu->addChild(createMenuItem("Move down", "", [=]() {
			if (i + 1 >= (int) mm->rows.size())
				return;
			mm->snapshot();
			std::lock_guard<std::mutex> lk(mm->dataM);
			std::swap(mm->rows[i], mm->rows[i + 1]);
			mm->selRow = i + 1;
		}));
		menu->addChild(createMenuItem("Duplicate", "", [=]() {
			mm->snapshot();
			std::lock_guard<std::mutex> lk(mm->dataM);
			if (i < (int) mm->rows.size() && (int) mm->rows.size() < ROWS_MAX)
				mm->rows.insert(mm->rows.begin() + i + 1, mm->rows[i]);
		}));
		menu->addChild(createMenuItem("Delete", "", [=]() { mm->deleteRow(i); }));
	}
	e.consume(this);
}

// ---------------------------------------------------------------- the module widget
struct ChopAblaufWidget : AblaufWidgetBase {
	template <typename T>
	T* at(const float* r) {
		T* w = new T();
		w->box = rectOf(r);
		addChild(w);
		return w;
	}
	HHButton* button(const float* r, const std::string& text, NVGcolor col, std::function<void()> act, const std::string& id) {
		HHButton* b = at<HHButton>(r);
		b->text = text;
		b->col = col;
		b->act = act;
		b->fontSize = 8.5f;
		ml::Target t;
		t.id = id;
		t.label = text;
		t.btn = b;
		learn.targets.push_back(t);
		b->rightMenu = [=](ui::Menu* m) {
			m->addChild(createMenuLabel(text));
			learn.appendTargetMenu(m, id);
		};
		b->learning = [=]() { return learn.learning == id; };
		return b;
	}

	ChopAblaufWidget(ChopAblauf* m) {
		setModule(m);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/ChopAblauf.svg")));
		learn.kind = "ablauf";
		learn.module = m;
		learn.ins = m ? m->ctrl : nullptr;
		if (m)
			learn.restorePorts();
		HHButton* stop = button(cal::STOP, "STOP", C_GRAPH, [=]() {
			if (m)
				m->reqPlay = 2;
		}, "stop");
		stop->symbol = 2;
		HHButton* play = button(cal::PLAY, "PLAY", C_RED, [=]() {
			if (m)
				m->reqPlay = 1;
		}, "play");
		play->symbol = 1;
		play->lit = [=]() {
			GenraChop* c = m ? m->uiHost() : nullptr;
			return c && c->running;
		};
		ALed* led = new ALed;
		led->box.size = Vec(6.f, 6.f);
		led->box.pos = Vec(cal::LINK[0], cal::LINK[1] - 3.f);
		led->m = m;
		addChild(led);
		// banks
		const float* BK[4] = {cal::BANK0, cal::BANK1, cal::BANK2, cal::BANK3};
		for (int i = 0; i < 4; i++) {
			HHButton* b = button(BK[i], std::string(1, (char) ('A' + i)), nvgRGB(0x55, 0x53, 0x4f), [=]() {
				if (m)
					m->bank = i;
			}, string::f("bank%d", i));
			b->lit = [=]() { return m && m->bank == i; };
		}
		const float* PD[16] = {cal::PAD0, cal::PAD1, cal::PAD2,  cal::PAD3,  cal::PAD4,  cal::PAD5,  cal::PAD6,  cal::PAD7,
		                       cal::PAD8, cal::PAD9, cal::PAD10, cal::PAD11, cal::PAD12, cal::PAD13, cal::PAD14, cal::PAD15};
		for (int i = 0; i < 16; i++) {
			APad* p = at<APad>(PD[i]);
			p->m = m;
			p->w = this;
			p->idx = i;
		}
		AInfo* info = at<AInfo>(cal::PADINFO);
		info->m = m;
		info->msg = &msg;
		info->msgTime = &msgTime;
		// CLEAR | CLEAR ALL under the pads
		HHButton* clr = button(cal::CLEAR, "CLEAR", nvgRGB(0x55, 0x53, 0x4f), [=]() {
			if (!m)
				return;
			m->clearArmed = !m->clearArmed;
			m->clearAllArmed = 0.f;
			if (m->clearArmed)
				say("CLEAR: tap the pad to empty");
		}, "clear");
		clr->colOf = [=]() { return m && m->clearArmed ? C_RED : nvgRGB(0x55, 0x53, 0x4f); };
		HHButton* clrAll = button(cal::CLEARALL, "CLEAR ALL", nvgRGB(0x55, 0x53, 0x4f), [=]() {
			if (!m)
				return;
			m->clearArmed = false;
			if (m->clearAllArmed > 0.f) {
				m->clearAllArmed = 0.f;
				m->clearAll();
				say("All 64 pads cleared (UNDO brings them back)");
			}
			else {
				m->clearAllArmed = 2.5f;
				say("CLEAR ALL: press again to empty all 64 pads");
			}
		}, "clearall");
		clrAll->colOf = [=]() {
			if (!m || m->clearAllArmed <= 0.f)
				return nvgRGB(0x55, 0x53, 0x4f);
			return (std::fmod(system::getTime(), 0.4) < 0.2) ? C_RED : nvgRGB(0x55, 0x53, 0x4f); // blinks: press again
		};
		// song
		AMode* mode = at<AMode>(cal::MODE);
		mode->m = m;
		AList* list = at<AList>(cal::LIST);
		list->m = m;
		list->w = this;
		button(cal::INS, "INS", nvgRGB(0x55, 0x53, 0x4f), [=]() {
			if (m)
				m->insertRow();
		}, "ins");
		button(cal::DEL, "DEL", nvgRGB(0x55, 0x53, 0x4f), [=]() {
			if (m)
				m->deleteRow(m->selRow.load());
		}, "del");
		button(cal::UNDO, "UNDO", nvgRGB(0x55, 0x53, 0x4f), [=]() {
			if (m)
				m->doUndo();
		}, "undo");
		button(cal::MIDI, "MIDI", nvgRGB(0x55, 0x53, 0x4f), [=]() {
			if (m)
				say(m->exportSong());
		}, "midi");
		HHField* form = at<HHField>(cal::FORM);
		form->count = []() { return NUM_FORMS + 1; };
		form->current = [=]() { return m ? m->form + 1 : 0; };
		form->item = [=](int i) {
			if (i == 0) {
				GenraChop* c = m ? m->uiHost() : nullptr;
				return std::string("AUTO") + (c ? std::string(" (") + FORMS[autoForm(c->set.style)].name + ")" : "");
			}
			return std::string(FORMS[i - 1].name);
		};
		form->value = [=]() {
			if (!m)
				return std::string("FORM AUTO");
			GenraChop* c = m->uiHost();
			if (m->form < 0)
				return std::string("AUTO ") + (c ? FORMS[autoForm(c->set.style)].name : "");
			return std::string(FORMS[m->form].name);
		};
		form->choose = [=](int i) {
			if (m)
				m->form = i - 1;
		};
		learnField(form, "form", "FORM");
		button(cal::MAKE, "MAKE SONG", C_RED, [=]() {
			if (m)
				say(m->makeSong());
		}, "make");
		// parts
		const float* PT[6] = {cal::PART0, cal::PART1, cal::PART2, cal::PART3, cal::PART4, cal::PART5};
		for (int p = 0; p < NUM_PARTS; p++)
			button(PT[p], PART_NAMES[p], nvgRGB(0x55, 0x53, 0x4f), [=]() {
				if (m)
					say(m->makeVariation(p));
			}, string::f("part%d", p));
		// jacks
		using M = ChopAblauf;
		addInput(createInputCentered<PJ301MPort>(Vec(cal::ROWCV_IN[0], cal::ROWCV_IN[1]), m, M::ROWCV_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(cal::RST_IN[0], cal::RST_IN[1]), m, M::RST_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(cal::RUN_IN[0], cal::RUN_IN[1]), m, M::RUN_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(cal::NEXT_IN[0], cal::NEXT_IN[1]), m, M::NEXT_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(cal::PREV_IN[0], cal::PREV_IN[1]), m, M::PREV_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(cal::PADCV_IN[0], cal::PADCV_IN[1]), m, M::PADCV_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(cal::END_OUT[0], cal::END_OUT[1]), m, M::END_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(cal::BAR_OUT[0], cal::BAR_OUT[1]), m, M::BAR_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(cal::ROW_OUT[0], cal::ROW_OUT[1]), m, M::ROW_OUTPUT));
	}

	void learnField(HHField* f, const std::string& id, const std::string& lab) {
		ml::Target t;
		t.id = id;
		t.label = lab;
		t.field = f;
		learn.targets.push_back(t);
		f->rightMenu = [=](ui::Menu* mn) {
			mn->addChild(createMenuLabel(lab));
			learn.appendTargetMenu(mn, id);
		};
		f->learning = [=]() { return learn.learning == id; };
	}

	/** GENRA CHOP: chosen in the menu, else right next to ABLAUF (or next to the GRID beside it), else the only one. */
	void findHost(ChopAblauf* m) {
		GenraChop* found = nullptr;
		if (m->linkedId >= 0) {
			Module* x = APP->engine->getModule(m->linkedId);
			if (x && x->model == modelGenraChop)
				found = static_cast<GenraChop*>(x);
		}
		if (!found) {
			Module* side[2] = {m->leftExpander.module, m->rightExpander.module};
			for (int k = 0; k < 2 && !found; k++) {
				Module* x = side[k];
				if (!x)
					continue;
				if (x->model == modelGenraChop)
					found = static_cast<GenraChop*>(x);
				else if (x->model == modelChopGrid) {
					// ABLAUF | GRID | GENRA CHOP
					Module* y = k == 0 ? x->leftExpander.module : x->rightExpander.module;
					if (y && y->model == modelGenraChop)
						found = static_cast<GenraChop*>(y);
				}
			}
		}
		if (!found && m->linkedId < 0) {
			ChopRegistry& reg = ChopRegistry::get();
			std::lock_guard<std::mutex> lk(reg.m);
			if (reg.all.size() == 1)
				found = reg.all[0];
		}
		if (found != m->host.load()) {
			m->host = found;
			m->hostChanged = true;
		}
	}

	void step() override {
		ChopAblauf* m = dynamic_cast<ChopAblauf*>(module);
		if (m) {
			findHost(m);
			learn.step();
			if (m->clearAllArmed > 0.f)
				m->clearAllArmed = std::max(0.f, m->clearAllArmed - (float) APP->window->getLastFrameDuration());
		}
		if (msgTime > 0.f)
			msgTime -= APP->window->getLastFrameDuration();
		if (!learn.learning.empty()) {
			msg = "MIDI LEARN " + learn.labelOf(learn.learning) + ": MOVE A CONTROL";
			msgTime = 0.2f;
		}
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		ChopAblauf* m = dynamic_cast<ChopAblauf*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Connection"));
		menu->addChild(createCheckMenuItem("Automatic (GENRA CHOP next to ABLAUF, or the only one)", "", [=]() { return m->linkedId < 0; },
		                                   [=]() { m->linkedId = -1; }));
		menu->addChild(createSubmenuItem("Connect to…", "", [=](Menu* sub) {
			int n = 0;
			for (int64_t id : APP->engine->getModuleIds()) {
				Module* x = APP->engine->getModule(id);
				if (!x || x->model != modelGenraChop)
					continue;
				n++;
				GenraChop* c = static_cast<GenraChop*>(x);
				sub->addChild(createCheckMenuItem(string::f("GENRA CHOP #%d (%s)", n, c->style().name), "",
				                                  [=]() { return m->linkedId == id; }, [=]() { m->linkedId = id; }));
			}
			if (!n)
				sub->addChild(createMenuLabel("No GENRA CHOP in the patch"));
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Song"));
		menu->addChild(createBoolPtrMenuItem("Loop the song", "", &m->loop));
		menu->addChild(createMenuItem("Export song as MIDI", "", [=]() { say(m->exportSong()); }));
		menu->addChild(createMenuItem("MIDI export folder…", "", []() { hhx::pickMidiFolder(); }));
		menu->addChild(createMenuItem("Clear the song list", "", [=]() {
			m->snapshot();
			std::lock_guard<std::mutex> lk(m->dataM);
			m->rows.clear();
			m->selRow = 0;
		}));
		menu->addChild(createSubmenuItem("Clear pads", "", [=](Menu* sub) {
			for (int b = 0; b < 4; b++)
				sub->addChild(createMenuItem(string::f("Bank %c", 'A' + b), "", [=]() {
					m->snapshot();
					std::lock_guard<std::mutex> lk(m->dataM);
					for (int i = b * 16; i < b * 16 + 16; i++) {
						m->slots[i] = Slot();
						for (Row& r : m->rows)
							if (r.slot == i)
								r.slot = -1;
					}
				}));
			sub->addChild(createMenuItem("All 64 pads", "", [=]() {
				m->snapshot();
				std::lock_guard<std::mutex> lk(m->dataM);
				for (Slot& s : m->slots)
					s = Slot();
				for (Row& r : m->rows)
					r.slot = -1;
				m->families = 0;
			}));
		}));
		learn.appendModuleMenu(menu, [=](const std::string& s) { say(s); });
	}
};

Model* modelChopAblauf = createModel<ChopAblauf, ChopAblaufWidget>("GenraChopAblauf");
