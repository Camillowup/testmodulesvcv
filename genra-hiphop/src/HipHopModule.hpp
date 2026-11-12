// SPDX-License-Identifier: GPL-3.0-only
// GENRA HIP-HOP: the module (shared with GENRA GRID HH, which edits its pattern).
#pragma once
#include "plugin.hpp"
#include "Export.hpp"
#include "hh/Core.hpp"
#include "hh/Chop.hpp"
#include <osdialog.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

using namespace hh;

// a sample being loaded / analysed (shared with the worker thread, which may outlive the module)
struct Loader {
	std::atomic<ChopSet*> done{nullptr};
	std::atomic<bool> busy{false};
	std::mutex m;
	std::string err;    // under m
	std::string status; // under m: what happened last (shown when there is no sample)
	bool keepPattern = false;
	void say(const std::string& s, bool isErr) {
		std::lock_guard<std::mutex> lk(m);
		(isErr ? err : status) = s;
		if (isErr)
			status.clear();
		else
			err.clear();
		INFO("GENRA HIP-HOP: %s", s.c_str());
	}
	~Loader() { delete done.exchange(nullptr); }
};

inline void startLoad(std::shared_ptr<Loader> L, std::string path, int mode, bool keepPattern) {
	L->busy = true;
	L->keepPattern = keepPattern;
	L->say("loading " + path, false);
	std::thread([L, path, mode]() {
		ChopSet* cs = new ChopSet();
		std::string err;
		bool ok = false;
		try {
			std::vector<uint8_t> buf = system::readFile(path);
			L->say(string::f("read %d bytes", (int) buf.size()), false);
			ok = readWav(buf, cs->data, err);
			if (ok) {
				cs->data.path = path;
				cs->data.name = system::getStem(path);
				analyse(*cs, mode);
				L->say(string::f("analysed: %d chops, %.1f s", cs->info.count, cs->data.frames() / cs->data.sampleRate), false);
				if (cs->info.count <= 0) {
					ok = false;
					err = "no chops found";
				}
			}
		}
		catch (Exception& e) {
			ok = false;
			err = e.what();
		}
		catch (std::exception& e) {
			ok = false;
			err = e.what();
		}
		catch (...) {
			ok = false;
			err = "unknown error";
		}
		if (ok)
			delete L->done.exchange(cs);
		else {
			delete cs;
			L->say(err.empty() ? std::string("could not read the file") : err, true);
		}
		L->busy = false;
	}).detach();
}

/** Re-cut a loaded sample in another mode (a copy of its audio). */
inline void startRecut(std::shared_ptr<Loader> L, const ChopSet* from, int mode) {
	if (!from)
		return;
	L->busy = true;
	L->keepPattern = false;
	SampleData data = from->data;
	std::thread([L, data, mode]() {
		ChopSet* cs = new ChopSet();
		cs->data = data;
		analyse(*cs, mode);
		delete L->done.exchange(cs);
		L->busy = false;
	}).detach();
}

// ---------------------------------------------------------------- a voice playing a part of a sample
struct SVoice {
	const ChopSet* cs = nullptr;
	double pos = 0.0, inc = 1.0;
	double a = 0.0, b = 0.0; // the chop's frames
	bool rev = false, active = false;
	float gain = 1.f, env = 0.f, envTarget = 1.f, envStep = 0.f;
	int remain = 0; // samples until it lets go (-1: to the end of the chop)
	void start(const ChopSet* c, int pad, float vel, int semis, bool reverse, float engineSr, int lenSamples) {
		cs = c;
		a = (double) c->start[pad];
		b = (double) c->end[pad];
		inc = std::pow(2.0, semis / 12.0) * c->data.sampleRate / engineSr;
		rev = reverse;
		pos = rev ? b - 2.0 : a;
		gain = vel;
		env = 0.f;
		envTarget = 1.f;
		envStep = 1.f / (0.001f * engineSr);
		remain = lenSamples;
		active = true;
	}
	void release(float engineSr) {
		envTarget = 0.f;
		envStep = 1.f / (0.004f * engineSr);
	}
	void next(float sr, float& L, float& R) {
		if (!active)
			return;
		if (remain > 0 && --remain == 0)
			release(sr);
		double p = pos;
		if (p < a || p >= b - 1.0) {
			active = false;
			return;
		}
		size_t i0 = (size_t) p;
		float fr = (float) (p - i0);
		const std::vector<float>& l = cs->data.L;
		const std::vector<float>& r = cs->data.R.empty() ? cs->data.L : cs->data.R;
		float yl = l[i0] + (l[i0 + 1] - l[i0]) * fr;
		float yr = r[i0] + (r[i0 + 1] - r[i0]) * fr;
		if (env < envTarget)
			env = std::min(envTarget, env + envStep);
		else if (env > envTarget)
			env = std::max(envTarget, env - envStep);
		// the last 3 ms of the chop fade, so a cut never clicks
		double left = rev ? (p - a) : (b - p);
		float tail = (float) std::min(1.0, left / (0.003 * cs->data.sampleRate));
		float g = gain * env * tail;
		L += yl * g;
		R += yr * g;
		pos += rev ? -inc : inc;
		if (envTarget == 0.f && env <= 0.f)
			active = false;
	}
};

// ---------------------------------------------------------------- what waits to be played (with swing and timing)
struct Pending {
	int delay = 0;
	int kind = 0; // 0 drum, 1 bass, 2 mel, 3 pad, 4 chop
	int idx = 0;  // drum voice
	int step = 0; // the step in the rendered pattern
	int sub = 0, subs = 1; // a roll's hit
};

struct Gate {
	int left = 0; // samples
	float v = 0.f;
};

struct GenraHipHop : Module {
	enum ParamId {
		BPM_PARAM,
		SWING_PARAM,
		VOL_PARAM,
		BRK_PARAM,
		MUTATE_PARAM,
		PITCH_PARAM,
		DUST_PARAM,
		ENUMS(DENS_PARAM, 4),
		ENUMS(TK2_PARAM, 4), // CHOP (drums, melody, pads), OCT (bass)
		NUM_PARAMS
	};
	enum InputId { CLK_INPUT, RST_INPUT, RUN_INPUT, NEW_INPUT, NUM_INPUTS };
	enum OutputId {
		ENUMS(DRUM_OUTPUT, 8),
		ENUMS(BASS_OUTPUT, 5),
		ENUMS(MEL_OUTPUT, 5),
		ENUMS(PAD_OUTPUT, 4),
		CHOP_L_OUTPUT,
		CHOP_R_OUTPUT,
		BRK_L_OUTPUT,
		BRK_R_OUTPUT,
		CLK_OUTPUT,
		RST_OUTPUT,
		BAR_OUTPUT,
		NUM_OUTPUTS
	};

	// ---- state (saved)
	Settings set;
	Pattern pat;
	PadSet pads[PADS];
	int chopMode = CM_TRANSIENT;
	bool lockChords = false;
	int clockDiv = 1; // clock pulses per 16th: 1, 6 (24 PPQN), 12 (48 PPQN)
	bool bakeChops = false; // SAVE writes the chops with their pitch and direction
	std::string samplePath, breakPath;

	// ---- the sample and the break
	std::shared_ptr<Loader> loader = std::make_shared<Loader>();
	std::shared_ptr<Loader> brkLoader = std::make_shared<Loader>();
	ChopSet* chops = nullptr; // the audio thread's (swapped in process())
	ChopSet* brk = nullptr;
	std::atomic<ChopSet*> trash[4];                        // old sets, deleted by the UI
	std::atomic<ChopSet*> incoming{nullptr}, incomingBrk{nullptr}; // handed over by the UI
	std::atomic<bool> taken{false};                        // the audio thread took a new sample (for the log)
	void toTrash(ChopSet* c) {
		if (!c)
			return;
		for (auto& t : trash) {
			ChopSet* none = nullptr;
			if (t.compare_exchange_strong(none, c))
				return;
		}
		// all four full (the UI is not running?): keep it, a small leak is better than a crash
	}
	std::atomic<const ChopSet*> uiChops{nullptr}; // for the panel (deleted only via trash, after the UI let go)

	// ---- playing
	Render ren;
	Knobs knobs;
	bool dirty = true;
	std::atomic<bool> running{false};
	std::atomic<int> step{-1};
	float phase = 0.f, stepPeriod = 5000.f, sinceTick = 0.f;
	float clockGap = 0.f;
	int clockCount = 0;
	dsp::SchmittTrigger clkTrig, rstTrig, newTrig;
	Pending pend[256];
	int nPend = 0;
	Gate drumGate[DRUMS];
	Gate bassGate, melGate, padGate[PAD_VOICES];
	float bassV = 0.f, melV = 0.f, padV[PAD_VOICES] = {}, bassVel = 0.f, melVel = 0.f, padVel = 0.f;
	float bassTarget = 0.f, melTarget = 0.f; // slides glide to these
	bool bassAcc = false, melAcc = false, bassSlide = false, melSlide = false;
	float padMod = 0.f, padModInc = 0.f;
	SVoice cv[2];
	int cvNext = 0;
	SVoice bv[4];
	int bvNext = 0;
	float lpL = 0.f, lpR = 0.f;
	float rootPhase = 0.f;
	std::atomic<bool> rootHeld{false};
	int clkOutLeft = 0, rstOutLeft = 0, barOutLeft = 0;

	// ---- from the panel (UI thread) to the audio thread
	std::atomic<int> reqNew{0};       // bit per track, bit 5: all
	std::atomic<bool> reqUndo{false}, reqStyle{false}, reqRender{false}, reqKill{false};
	bool wasRunning = false;
	std::atomic<int> padQ[32];
	std::atomic<int> padQw{0};
	int padQr = 0;
	std::atomic<bool> rec{false};
	std::atomic<int> lastPad{-1};
	std::atomic<uint32_t> lastPadTick{0};
	uint32_t tick = 0;
	midi::InputQueue midiIn;

	// undo: the patterns before the last NEWs
	std::vector<Pattern> undo;
	int undoN = 0, undoTop = 0;
	std::atomic<int> undoCount{0};

	GenraHipHop() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		configParam(BPM_PARAM, 50.f, 200.f, 92.f, "Tempo", " BPM")->snapEnabled = true;
		configParam(SWING_PARAM, 50.f, 75.f, 58.f, "Swing", "%");
		configParam(VOL_PARAM, 0.f, 1.f, 0.8f, "Chop level", "%", 0.f, 100.f);
		configParam(BRK_PARAM, 0.f, 1.f, 0.8f, "Break level", "%", 0.f, 100.f);
		configParam(MUTATE_PARAM, 0.f, 1.f, 0.f, "Mutate (chops changed every 4 bars)", "%", 0.f, 100.f);
		configParam(PITCH_PARAM, -12.f, 12.f, 0.f, "Pitch of all chops", " semitones")->snapEnabled = true;
		configParam(DUST_PARAM, 0.f, 1.f, 0.f, "Dust (vinyl, 12 bit, dark)", "%", 0.f, 100.f);
		const char* TN[4] = {"Drums", "Bass", "Melody", "Pads"};
		for (int t = 0; t < 4; t++) {
			configParam(DENS_PARAM + t, 0.f, 1.f, 0.5f, string::f("%s density", TN[t]), "%", 0.f, 100.f);
			if (t == 1)
				configParam(TK2_PARAM + t, -2.f, 2.f, 0.f, "Bass octave")->snapEnabled = true;
			else
				configParam(TK2_PARAM + t, 0.f, 1.f, 0.f, string::f("%s MIDI chop", TN[t]), "%", 0.f, 100.f);
		}
		configInput(CLK_INPUT, "Clock (16ths; 24 / 48 PPQN in the menu)");
		configInput(RST_INPUT, "Reset");
		configInput(RUN_INPUT, "Run (gate)");
		configInput(NEW_INPUT, "New all (trigger)");
		const char* DN[8] = {"Kick", "Snare", "Clap", "Rim", "Closed hat", "Open hat", "Tom", "Perc"};
		for (int i = 0; i < 8; i++)
			configOutput(DRUM_OUTPUT + i, string::f("%s trigger (voltage = velocity)", DN[i]));
		const char* NL[5] = {"V/OCT", "Gate", "Velocity", "Accent", "Slide"};
		for (int i = 0; i < 5; i++) {
			configOutput(BASS_OUTPUT + i, string::f("Bass %s", NL[i]));
			configOutput(MEL_OUTPUT + i, string::f("Melody %s", NL[i]));
		}
		configOutput(PAD_OUTPUT + 0, "Pads V/OCT (poly 4)");
		configOutput(PAD_OUTPUT + 1, "Pads gate (poly 4)");
		configOutput(PAD_OUTPUT + 2, "Pads velocity");
		configOutput(PAD_OUTPUT + 3, "Pads mod (swell)");
		configOutput(CHOP_L_OUTPUT, "Chop left");
		configOutput(CHOP_R_OUTPUT, "Chop right");
		configOutput(BRK_L_OUTPUT, "Break left");
		configOutput(BRK_R_OUTPUT, "Break right");
		configOutput(CLK_OUTPUT, "Clock (16ths)");
		configOutput(RST_OUTPUT, "Reset");
		configOutput(BAR_OUTPUT, "Bar (trigger on every bar's one)");
		undo.resize(8);
		for (auto& t : trash)
			t = nullptr;
		for (auto& q : padQ)
			q = -1;
		set.style = 5;
		applyStyleDefaults();
		generate(pat, -1, set, SampleInfo(), pads, random::u32(), false, 92.f);
	}

	~GenraHipHop() {
		delete chops;
		delete brk;
		for (auto& t : trash)
			delete t.exchange(nullptr);
		delete incoming.exchange(nullptr);
		delete incomingBrk.exchange(nullptr);
	}

	const Style& style() const { return STYLES[clamp(set.style, 0, NUM_STYLES - 1)]; }
	const SampleInfo& info() const {
		static const SampleInfo none;
		return chops ? chops->info : none;
	}

	/** The track modes a style starts with. */
	void applyStyleDefaults() {
		const Style& st = style();
		set.bassMode = st.bass == BM_808 ? 3 : (st.bass == BM_WALK ? 2 : 0);
		set.melMode = st.mel == MM_STAB ? 0 : st.mel == MM_LEAD ? 1 : st.mel == MM_BELL ? 2 : 3;
		set.padMode = st.pad;
	}

	void pushUndo() {
		undo[undoTop] = pat;
		undoTop = (undoTop + 1) % (int) undo.size();
		undoN = std::min(undoN + 1, (int) undo.size());
		undoCount = undoN;
	}

	float bpmNow() {
		return (inputs[CLK_INPUT].isConnected() && stepPeriod > 1.f) ? 60.f * APP->engine->getSampleRate() / (stepPeriod * 4.f)
		                                                               : params[BPM_PARAM].getValue();
	}

	void newTrack(int track) {
		pushUndo();
		generate(pat, track, set, info(), pads, random::u32(), lockChords, bpmNow());
		dirty = true;
	}

	// ---------------------------------------------------------------- the audio thread
	int cycle = 0; // loop passes (for "every 2nd time")

	// ---- the pattern shared with GENRA GRID HH (UI thread) - the audio thread only try_locks
	std::mutex shareM;
	Pattern shared;                      // under shareM
	std::atomic<uint32_t> shareVer{0};   // +1 whenever `shared` got the module's pattern
	std::atomic<bool> gridIn{false};     // `shared` holds an edit from the grid for the module
	bool shareDirty = true;
	std::atomic<float> posPub{-1.f};     // play position in steps (-1: stopped)
	/** UI thread (GRID HH): hand an edited pattern to the module. */
	void putFromGrid(const Pattern& p) {
		std::lock_guard<std::mutex> lk(shareM);
		shared = p;
		gridIn = true;
	}

	static int trackOf(int kind) { return kind == 0 ? T_DRUMS : kind == 1 ? T_BASS : kind == 2 ? T_MEL : kind == 3 ? T_PAD : T_CHOP; }
	/** A track's (or drum voice's) own length: GRID HH can make it shorter than the loop. */
	int lenOf(int kind, int idx) const {
		int L = pat.lenTrack[trackOf(kind)];
		if (kind == 0 && pat.lenDrum[idx])
			L = pat.lenDrum[idx];
		return (L > 0 && L < LEN) ? L : LEN;
	}
	float swingOf(int kind, int idx) {
		float sw = pat.swTrack[trackOf(kind)];
		if (kind == 0 && pat.swDrum[idx] > 0.f)
			sw = pat.swDrum[idx];
		return sw > 0.f ? sw : params[SWING_PARAM].getValue() / 100.f;
	}
	/** Chance and "every 2nd time": does the event at step s play this time? */
	bool passes(int kind, int idx, int s) {
		float ch = 1.f;
		int cond = 0;
		switch (kind) {
			case 0: ch = ren.drum[idx][s].chance, cond = ren.drum[idx][s].cond; break;
			case 1: ch = ren.bass[s].chance, cond = ren.bass[s].cond; break;
			case 2: ch = ren.mel[s].chance, cond = ren.mel[s].cond; break;
			case 3: ch = ren.pad[s].chance, cond = ren.pad[s].cond; break;
			default: ch = ren.chop[s].chance, cond = ren.chop[s].cond; break;
		}
		if (cond == 1 && (cycle % 2) == 0)
			return false;
		return ch >= 1.f || random::uniform() < ch;
	}

	void schedule(int kind, int idx, int s, float off, int extraDelay) {
		if (!passes(kind, idx, s))
			return;
		float sw = (s % 2) ? (2.f * swingOf(kind, idx) - 1.f) : 0.f;
		int d = extraDelay + (int) ((sw + off) * stepPeriod);
		if (d < 0)
			d = 0;
		int subs = 1;
		if (kind == 0)
			subs = std::max(1, (int) ren.drum[idx][s].ratchet);
		if (kind == 4)
			subs = std::max(1, (int) ren.chop[s].ratchet);
		for (int k = 0; k < subs && nPend < 256; k++) {
			Pending& p = pend[nPend++];
			p.kind = kind;
			p.idx = idx;
			p.step = s;
			p.sub = k;
			p.subs = subs;
			float span = kind == 4 ? ren.chop[s].len * stepPeriod : stepPeriod;
			p.delay = d + (int) (k * span / subs);
		}
	}

	/** At the tick of loop step s: per track (in its own length) what starts now (late or on time) and what
	 *  starts early on the next step. */
	template <class On, class Off>
	void queueTrack(int s, int kind, int idx, On on, Off offOf) {
		int L = lenOf(kind, idx);
		int sT = s % L;
		int nT = (s + 1 >= LEN) ? 0 : (sT + 1) % L;
		if (on(sT) && offOf(sT) >= 0.f)
			schedule(kind, idx, sT, offOf(sT), 0);
		if (on(nT) && offOf(nT) < 0.f)
			schedule(kind, idx, nT, offOf(nT), (int) stepPeriod);
	}
	void queueStep(int s) {
		for (int v = 0; v < DRUMS; v++)
			queueTrack(s, 0, v, [&](int i) { return ren.drum[v][i].vel > 0.f; }, [&](int i) { return ren.drum[v][i].off; });
		queueTrack(s, 1, 0, [&](int i) { return ren.bass[i].pitch >= 0; }, [&](int i) { return ren.bass[i].off; });
		queueTrack(s, 2, 0, [&](int i) { return ren.mel[i].pitch >= 0; }, [&](int i) { return ren.mel[i].off; });
		queueTrack(s, 3, 0, [&](int i) { return ren.pad[i].pitch[0] >= 0; }, [&](int i) { return ren.pad[i].off; });
		queueTrack(s, 4, 0, [&](int i) { return ren.chop[i].pad >= 0; }, [&](int i) { return ren.chop[i].off; });
	}

	void playChop(int pad, float vel, int semis, bool rev, int lenSamples, float sr) {
		if (!chops || pad < 0 || pad >= chops->info.count)
			return;
		// mono like an MPC pad in mono mode: the new chop cuts the one before
		for (SVoice& v : cv)
			if (v.active)
				v.release(sr);
		SVoice& v = cv[cvNext];
		cvNext = (cvNext + 1) % 2;
		const PadSet& ps = pads[pad];
		v.start(chops, pad, vel, semis + ps.pitch + (int) params[PITCH_PARAM].getValue(), rev != ps.rev, sr, lenSamples);
		lastPad = pad;
		lastPadTick = tick;
	}

	void fire(const Pending& p, float sr) {
		int s = p.step;
		switch (p.kind) {
			case 0: {
				const Hit& h = ren.drum[p.idx][s];
				float vel = h.vel * (p.subs > 1 ? (0.6f + 0.4f * (p.sub == 0)) : 1.f);
				if (set.drumMode != 1 || !brk) {
					drumGate[p.idx].left = (int) (0.005f * sr);
					drumGate[p.idx].v = 10.f * vel;
				}
				if (set.drumMode != 0 && brk && brk->voicePad[p.idx] >= 0) {
					// the break: hats choke each other, the rest round robin
					SVoice& v = bv[(p.idx == CH || p.idx == OH) ? 3 : (bvNext++ % 3)];
					v.start(brk, brk->voicePad[p.idx], vel, 0, false, sr, -1);
				}
				break;
			}
			case 1:
			case 2: {
				const Note& n = (p.kind == 1 ? ren.bass : ren.mel)[s];
				Gate& g = p.kind == 1 ? bassGate : melGate;
				float v = (n.pitch - 60) / 12.f;
				bool slide = n.slide;
				(p.kind == 1 ? bassTarget : melTarget) = v;
				if (!slide)
					(p.kind == 1 ? bassV : melV) = v;
				g.left = std::max(1, (int) (n.len * stepPeriod) - (int) (0.002f * sr));
				g.v = 10.f;
				(p.kind == 1 ? bassVel : melVel) = 10.f * n.vel;
				(p.kind == 1 ? bassAcc : melAcc) = n.acc;
				(p.kind == 1 ? bassSlide : melSlide) = slide;
				break;
			}
			case 3: {
				const PadEv& e = ren.pad[s];
				int len = std::max(1, (int) (e.len * stepPeriod) - (int) (0.002f * sr));
				for (int i = 0; i < PAD_VOICES; i++) {
					if (e.pitch[i] < 0)
						continue;
					padV[i] = (e.pitch[i] - 60) / 12.f;
					padGate[i].left = len;
					padGate[i].v = 10.f;
				}
				padVel = 10.f * e.vel;
				padMod = 0.f;
				padModInc = e.mod > 0.f ? 10.f / len : 0.f;
				break;
			}
			case 4: {
				const ChopEv& e = ren.chop[s];
				int len = (int) (e.len * stepPeriod / p.subs);
				playChop(e.pad, e.vel, e.pitch, e.rev, std::max(1, len), sr);
				break;
			}
		}
	}

	void doReset() {
		step = -1;
		phase = 1.f; // the next tick comes at once
		nPend = 0;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		for (SVoice& v : cv)
			v.active = false;
		for (SVoice& v : bv)
			v.active = false;
	}

	void process(const ProcessArgs& args) override {
		float sr = args.sampleRate;
		tick++;
		// ---- a new sample / break from the worker thread
		ChopSet* c = incoming.exchange(nullptr);
		if (c) {
			for (SVoice& v : cv)
				v.active = false;
			toTrash(chops); // the UI deletes it (collectTrash)
			chops = c;
			uiChops = c;
			if (set.keyAuto)
				set.key = c->info.key;
			if (!loader->keepPattern) {
				for (PadSet& ps : pads)
					ps = PadSet();
				pushUndo();
				generate(pat, T_CHOP, set, c->info, pads, random::u32(), false, bpmNow());
			}
			dirty = true;
			taken = true;
		}
		ChopSet* b = incomingBrk.exchange(nullptr);
		if (b) {
			for (SVoice& v : bv)
				v.active = false;
			toTrash(brk);
			brk = b;
		}
		// ---- requests from the panel
		if (reqStyle.exchange(false)) {
			pushUndo();
			if (set.keyAuto && chops)
				set.key = chops->info.key;
			generate(pat, -1, set, info(), pads, random::u32(), lockChords, bpmNow());
			dirty = true;
		}
		int rq = reqNew.exchange(0);
		if (newTrig.process(inputs[NEW_INPUT].getVoltage(), 0.1f, 1.f))
			rq |= 1 << 5;
		if (rq & (1 << 5))
			newTrack(-1);
		else if (rq & (1 << 6))
			newTrack(T_HARMONY);
		else
			for (int t = 0; t < NUM_TRACKS; t++)
				if (rq & (1 << t))
					newTrack(t);
		if (reqUndo.exchange(false) && undoN > 0) {
			undoTop = (undoTop + (int) undo.size() - 1) % (int) undo.size();
			undoN--;
			undoCount = undoN;
			pat = undo[undoTop];
			dirty = true;
		}
		if (reqRender.exchange(false))
			dirty = true;
		// ---- the knobs (every 32 samples)
		if ((tick & 31) == 0) {
			Knobs k = knobs;
			for (int t = 0; t < 4; t++)
				k.dens[t] = params[DENS_PARAM + t].getValue();
			k.cut[T_DRUMS] = params[TK2_PARAM + 0].getValue();
			k.bassOct = (int) std::round(params[TK2_PARAM + 1].getValue());
			k.cut[T_MEL] = params[TK2_PARAM + 2].getValue();
			k.cut[T_PAD] = params[TK2_PARAM + 3].getValue();
			if (memcmp(&k, &knobs, sizeof(Knobs)))
				knobs = k, dirty = true;
		}
		if ((tick & 127) == 0) {
			if (gridIn.load()) {
				if (shareM.try_lock()) {
					pat = shared;
					gridIn = false;
					shareM.unlock();
					dirty = true;
				}
			}
			else if (shareDirty && shareM.try_lock()) {
				shared = pat;
				shareVer++;
				shareDirty = false;
				shareM.unlock();
			}
		}
		if (dirty) {
			render(pat, knobs, ren);
			dirty = false;
			shareDirty = true;
		}
		// ---- the pads (mouse) and MIDI
		while (true) {
			int w = padQw.load();
			if (padQr == w)
				break;
			int v = padQ[padQr % 32].load();
			padQr++;
			if (v >= 0)
				padHit(v & 0xff, ((v >> 8) & 0xff) / 127.f, sr);
		}
		midi::Message msg;
		while (midiIn.tryPop(&msg, args.frame)) {
			if (msg.getStatus() == 0x9 && msg.getValue() > 0) {
				int n = msg.getNote() - 36;
				if (n >= 0 && n < PADS)
					padHit(n, msg.getValue() / 127.f, sr);
			}
		}
		// ---- the clock
		bool run = inputs[RUN_INPUT].isConnected() ? inputs[RUN_INPUT].getVoltage() >= 1.f : running.load();
		if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			doReset();
			rstOutLeft = (int) (0.001f * sr);
		}
		bool tickNow = false;
		sinceTick += 1.f;
		if (inputs[CLK_INPUT].isConnected()) {
			clockGap += 1.f;
			if (clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
				if (clockGap > 1.f && clockGap < 4.f * sr)
					stepPeriod = clockGap * clockDiv;
				clockGap = 0.f;
				if (clockCount++ % std::max(1, clockDiv) == 0)
					tickNow = run;
			}
		}
		else {
			stepPeriod = sr * 60.f / std::max(20.f, params[BPM_PARAM].getValue()) / 4.f;
			if (run) {
				phase += 1.f / stepPeriod;
				if (phase >= 1.f) {
					phase -= std::floor(phase);
					tickNow = true;
				}
			}
			else
				phase = 1.f;
		}
		if (wasRunning && !run) {
			// stopped: the gates go, back to bar 1 (PLAY starts there)
			bassGate.left = melGate.left = 0;
			for (Gate& g : padGate)
				g.left = 0;
			for (Gate& g : drumGate)
				g.left = 0;
			nPend = 0;
			step = -1;
			phase = 1.f;
		}
		wasRunning = run;
		if (reqKill.exchange(false)) {
			for (SVoice& v : cv)
				v.release(sr);
			for (SVoice& v : bv)
				v.release(sr);
		}
		if (tickNow) {
			int s = (step.load() + 1) % LEN;
			step = s;
			sinceTick = 0.f;
			clkOutLeft = (int) (0.001f * sr);
			if (s % STEPS == 0)
				barOutLeft = (int) (0.001f * sr);
			if (s == 0) {
				cycle++;
				float mut = params[MUTATE_PARAM].getValue();
				if (mut > 0.f && chops) {
					mutateChops(pat, set, chops->info, pads, random::u32(), mut, bpmNow());
					render(pat, knobs, ren);
					shareDirty = true;
				}
			}
			queueStep(s);
		}
		if ((tick & 31) == 0) {
			int sp = step.load();
			posPub = (sp >= 0 && run) ? sp + std::min(0.999f, sinceTick / std::max(1.f, stepPeriod)) : -1.f;
		}
		// ---- what is due
		for (int i = 0; i < nPend;) {
			if (--pend[i].delay <= 0) {
				Pending p = pend[i];
				pend[i] = pend[--nPend];
				fire(p, sr);
			}
			else
				i++;
		}
		// ---- outputs: drums
		for (int v = 0; v < DRUMS; v++) {
			Gate& g = drumGate[v];
			outputs[DRUM_OUTPUT + v].setVoltage(g.left > 0 ? g.v : 0.f);
			if (g.left > 0)
				g.left--;
		}
		// bass and melody (a slide glides in 60 ms)
		float glide = 1.f - std::exp(-1.f / (0.06f * sr));
		if (bassSlide)
			bassV += (bassTarget - bassV) * glide;
		if (melSlide)
			melV += (melTarget - melV) * glide;
		auto line = [&](int base, Gate& g, float v, float vel, bool acc, bool slide) {
			outputs[base + 0].setVoltage(v);
			outputs[base + 1].setVoltage(g.left > 0 ? 10.f : 0.f);
			outputs[base + 2].setVoltage(vel);
			outputs[base + 3].setVoltage(acc && g.left > 0 ? 10.f : 0.f);
			outputs[base + 4].setVoltage(slide && g.left > 0 ? 10.f : 0.f);
			if (g.left > 0)
				g.left--;
		};
		line(BASS_OUTPUT, bassGate, bassV, bassVel, bassAcc, bassSlide);
		line(MEL_OUTPUT, melGate, melV, melVel, melAcc, melSlide);
		outputs[PAD_OUTPUT + 0].setChannels(PAD_VOICES);
		outputs[PAD_OUTPUT + 1].setChannels(PAD_VOICES);
		for (int i = 0; i < PAD_VOICES; i++) {
			outputs[PAD_OUTPUT + 0].setVoltage(padV[i], i);
			outputs[PAD_OUTPUT + 1].setVoltage(padGate[i].left > 0 ? 10.f : 0.f, i);
			if (padGate[i].left > 0)
				padGate[i].left--;
		}
		outputs[PAD_OUTPUT + 2].setVoltage(padVel);
		padMod = std::min(10.f, padMod + padModInc);
		outputs[PAD_OUTPUT + 3].setVoltage(padMod);
		// ---- the chops, through the dust
		float L = 0.f, R = 0.f;
		for (SVoice& v : cv)
			v.next(sr, L, R);
		if (rootHeld) {
			float f = 440.f * std::pow(2.f, (48 + set.key.root - 69) / 12.f);
			rootPhase += f / sr;
			rootPhase -= std::floor(rootPhase);
			float y = 0.25f * std::sin(2.f * (float) M_PI * rootPhase);
			L += y, R += y;
		}
		float dust = params[DUST_PARAM].getValue();
		if (dust > 0.f) {
			float fc = 18000.f * std::pow(3000.f / 18000.f, dust);
			float a = 1.f - std::exp(-2.f * (float) M_PI * fc / sr);
			lpL += (L - lpL) * a, lpR += (R - lpR) * a;
			L = lpL, R = lpR;
			float q = std::pow(2.f, 15.f - 7.f * dust);
			L = std::round(L * q) / q, R = std::round(R * q) / q;
			if (random::uniform() < dust * 0.0004f) {
				float c = (random::uniform() - 0.5f) * 0.25f * dust;
				L += c, R += c;
			}
			float hiss = (random::uniform() - 0.5f) * 0.004f * dust;
			L += hiss, R += hiss;
		}
		float vol = 5.f * params[VOL_PARAM].getValue();
		outputs[CHOP_L_OUTPUT].setVoltage(clamp(L * vol, -12.f, 12.f));
		outputs[CHOP_R_OUTPUT].setVoltage(clamp(R * vol, -12.f, 12.f));
		float bl = 0.f, br = 0.f;
		for (SVoice& v : bv)
			v.next(sr, bl, br);
		float bvol = 5.f * params[BRK_PARAM].getValue();
		outputs[BRK_L_OUTPUT].setVoltage(clamp(bl * bvol, -12.f, 12.f));
		outputs[BRK_R_OUTPUT].setVoltage(clamp(br * bvol, -12.f, 12.f));
		outputs[CLK_OUTPUT].setVoltage(clkOutLeft > 0 ? 10.f : 0.f);
		outputs[RST_OUTPUT].setVoltage(rstOutLeft > 0 ? 10.f : 0.f);
		outputs[BAR_OUTPUT].setVoltage(barOutLeft > 0 ? 10.f : 0.f);
		if (clkOutLeft > 0)
			clkOutLeft--;
		if (rstOutLeft > 0)
			rstOutLeft--;
		if (barOutLeft > 0)
			barOutLeft--;
	}

	/** A pad hit by hand: it plays, and with REC (while running) it goes into the pattern on the nearest step. */
	void padHit(int pad, float vel, float sr) {
		if (!chops || pad >= chops->info.count)
			return;
		playChop(pad, vel, 0, false, -1, sr);
		int s = step.load();
		if (rec && s >= 0) {
			if (sinceTick > stepPeriod * 0.5f)
				s = (s + 1) % LEN;
			ChopEv e;
			e.pad = (int8_t) pad;
			e.vel = vel;
			e.len = std::max(1.f, std::min(8.f, chops->info.chop[pad].seconds / (stepPeriod / sr)));
			pat.chop[s] = e;
			dirty = true;
		}
	}

	/** UI thread: queue a pad hit for the audio thread. */
	void queuePad(int pad, float vel) {
		int w = padQw.load();
		padQ[w % 32] = pad | ((int) (vel * 127.f) << 8);
		padQw = w + 1;
	}

	/** UI thread: after the audio thread let go of a ChopSet (and the panel too), delete it. */
	void collectTrash() {
		// hand finished analyses to the audio thread
		if (!incoming.load())
			if (ChopSet* c = loader->done.exchange(nullptr)) {
				incoming = c;
				loader->say("handed over", false);
			}
		if (!incomingBrk.load())
			if (ChopSet* c = brkLoader->done.exchange(nullptr))
				incomingBrk = c;
		if (taken.exchange(false))
			loader->say("in use", false);
		const ChopSet* ui = uiChops.load();
		for (auto& t : trash) {
			ChopSet* c = t.load();
			if (c && c != ui)
				delete t.exchange(nullptr);
		}
	}

	// ---------------------------------------------------------------- saving
	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "version", json_integer(1));
		json_object_set_new(r, "style", json_integer(set.style));
		json_object_set_new(r, "keyAuto", json_boolean(set.keyAuto));
		json_object_set_new(r, "keyRoot", json_integer(set.key.root));
		json_object_set_new(r, "keyMinor", json_boolean(set.key.minor));
		json_object_set_new(r, "bassMode", json_integer(set.bassMode));
		json_object_set_new(r, "melMode", json_integer(set.melMode));
		json_object_set_new(r, "padMode", json_integer(set.padMode));
		json_object_set_new(r, "drumMode", json_integer(set.drumMode));
		json_object_set_new(r, "chopMode", json_integer(chopMode));
		json_object_set_new(r, "lockChords", json_boolean(lockChords));
		json_object_set_new(r, "clockDiv", json_integer(clockDiv));
		json_object_set_new(r, "bakeChops", json_boolean(bakeChops));
		json_object_set_new(r, "running", json_boolean(running));
		json_object_set_new(r, "sample", json_string(samplePath.c_str()));
		json_object_set_new(r, "break", json_string(breakPath.c_str()));
		json_object_set_new(r, "midiIn", midiIn.toJson());
		json_t* jp = json_array();
		for (const PadSet& ps : pads) {
			json_t* o = json_pack("[biiii]", ps.lock, ps.rev ? 1 : 0, (int) ps.pitch, (int) ps.chordRoot, (int) ps.chordQ);
			json_array_append_new(jp, o);
		}
		json_object_set_new(r, "pads", jp);
		// the pattern
		json_t* jd = json_array();
		for (int v = 0; v < DRUMS; v++)
			for (int s = 0; s < LEN; s++) {
				const Hit& h = pat.drum[v][s];
				if (h.vel > 0.f)
					json_array_append_new(jd, json_pack("[iiffffff]", v, s, h.vel, h.prio, h.off, (double) h.ratchet, h.chance,
					                                    (double) h.cond));
			}
		json_object_set_new(r, "drums", jd);
		auto notes = [&](const Note* ln) {
			json_t* a = json_array();
			for (int s = 0; s < LEN; s++)
				if (ln[s].pitch >= 0)
					json_array_append_new(a, json_pack("[iiffffbbff]", s, (int) ln[s].pitch, ln[s].len, ln[s].vel, ln[s].prio,
					                                   ln[s].off, ln[s].acc, ln[s].slide, ln[s].chance, (double) ln[s].cond));
			return a;
		};
		json_object_set_new(r, "bass", notes(pat.bass));
		json_object_set_new(r, "mel", notes(pat.mel));
		json_t* jpad = json_array();
		for (int s = 0; s < LEN; s++) {
			const PadEv& e = pat.pad[s];
			if (e.pitch[0] >= 0)
				json_array_append_new(jpad, json_pack("[iiiiifffffff]", s, (int) e.pitch[0], (int) e.pitch[1], (int) e.pitch[2],
				                                      (int) e.pitch[3], e.len, e.vel, e.prio, e.off, e.mod, e.chance, (double) e.cond));
		}
		json_object_set_new(r, "padEvents", jpad);
		json_t* jc = json_array();
		for (int s = 0; s < LEN; s++) {
			const ChopEv& e = pat.chop[s];
			if (e.pad >= 0)
				json_array_append_new(jc, json_pack("[iifffiiifff]", s, (int) e.pad, e.len, e.vel, e.off, (int) e.pitch,
				                                    e.rev ? 1 : 0, (int) e.ratchet, e.prio, e.chance, (double) e.cond));
		}
		json_object_set_new(r, "chops", jc);
		json_t* jb = json_array();
		for (int b = 0; b < BARS; b++)
			json_array_append_new(jb, json_pack("[ii]", pat.bar[b].root, pat.bar[b].quality));
		json_object_set_new(r, "bars", jb);
		json_t* jt = json_array();
		for (int t = 0; t < NUM_TRACKS; t++)
			json_array_append_new(jt, json_pack("[ifb]", (int) pat.lenTrack[t], pat.swTrack[t], pat.edited[t]));
		json_object_set_new(r, "trackSettings", jt);
		json_t* jv = json_array();
		for (int v = 0; v < DRUMS; v++)
			json_array_append_new(jv, json_pack("[if]", (int) pat.lenDrum[v], pat.swDrum[v]));
		json_object_set_new(r, "voiceSettings", jv);
		json_object_set_new(r, "chordFollow", json_boolean(pat.chordFollow));
		return r;
	}

	static int geti(json_t* o, const char* k, int d) {
		json_t* j = json_object_get(o, k);
		return j ? (int) json_integer_value(j) : d;
	}
	static bool getb(json_t* o, const char* k, bool d) {
		json_t* j = json_object_get(o, k);
		return j ? json_is_true(j) : d;
	}
	static float num(json_t* a, size_t i) { return (float) json_number_value(json_array_get(a, i)); }

	void dataFromJson(json_t* r) override {
		set.style = clamp(geti(r, "style", set.style), 0, NUM_STYLES - 1);
		set.keyAuto = getb(r, "keyAuto", true);
		set.key.root = clamp(geti(r, "keyRoot", 5), 0, 11);
		set.key.minor = getb(r, "keyMinor", true);
		set.bassMode = clamp(geti(r, "bassMode", 0), 0, 3);
		set.melMode = clamp(geti(r, "melMode", 0), 0, 3);
		set.padMode = clamp(geti(r, "padMode", 0), 0, 2);
		set.drumMode = clamp(geti(r, "drumMode", 0), 0, 2);
		chopMode = clamp(geti(r, "chopMode", 0), 0, NUM_CHOP_MODES - 1);
		lockChords = getb(r, "lockChords", false);
		clockDiv = geti(r, "clockDiv", 1);
		if (clockDiv != 1 && clockDiv != 6 && clockDiv != 12)
			clockDiv = 1;
		bakeChops = getb(r, "bakeChops", false);
		running = getb(r, "running", false);
		if (json_t* m = json_object_get(r, "midiIn"))
			midiIn.fromJson(m);
		if (json_t* jp = json_object_get(r, "pads"))
			for (size_t i = 0; i < json_array_size(jp) && i < (size_t) PADS; i++) {
				json_t* o = json_array_get(jp, i);
				pads[i].lock = json_is_true(json_array_get(o, 0));
				pads[i].rev = num(o, 1) != 0.f;
				pads[i].pitch = (int8_t) num(o, 2);
				pads[i].chordRoot = (int8_t) num(o, 3);
				pads[i].chordQ = (int8_t) num(o, 4);
			}
		if (json_object_get(r, "drums")) {
			pat.clear();
			json_t* a = json_object_get(r, "drums");
			for (size_t i = 0; i < json_array_size(a); i++) {
				json_t* o = json_array_get(a, i);
				int v = (int) num(o, 0), s = (int) num(o, 1);
				if (v < 0 || v >= DRUMS || s < 0 || s >= LEN)
					continue;
				Hit& h = pat.drum[v][s];
				h.vel = num(o, 2), h.prio = num(o, 3), h.off = num(o, 4), h.ratchet = (uint8_t) clamp((int) num(o, 5), 1, 8);
				if (json_array_size(o) >= 8)
					h.chance = num(o, 6), h.cond = (uint8_t) num(o, 7);
			}
			auto notes = [&](const char* key, Note* ln) {
				json_t* a = json_object_get(r, key);
				for (size_t i = 0; i < json_array_size(a); i++) {
					json_t* o = json_array_get(a, i);
					int s = (int) num(o, 0);
					if (s < 0 || s >= LEN)
						continue;
					Note& n = ln[s];
					n.pitch = (int8_t) clamp((int) num(o, 1), 0, 127);
					n.len = num(o, 2), n.vel = num(o, 3), n.prio = num(o, 4), n.off = num(o, 5);
					n.acc = json_is_true(json_array_get(o, 6));
					n.slide = json_is_true(json_array_get(o, 7));
					if (json_array_size(o) >= 10)
						n.chance = num(o, 8), n.cond = (uint8_t) num(o, 9);
				}
			};
			notes("bass", pat.bass);
			notes("mel", pat.mel);
			a = json_object_get(r, "padEvents");
			for (size_t i = 0; i < json_array_size(a); i++) {
				json_t* o = json_array_get(a, i);
				int s = (int) num(o, 0);
				if (s < 0 || s >= LEN)
					continue;
				PadEv& e = pat.pad[s];
				for (int k = 0; k < PAD_VOICES; k++)
					e.pitch[k] = (int8_t) clamp((int) num(o, 1 + k), -1, 127);
				e.len = num(o, 5), e.vel = num(o, 6), e.prio = num(o, 7), e.off = num(o, 8), e.mod = num(o, 9);
				if (json_array_size(o) >= 12)
					e.chance = num(o, 10), e.cond = (uint8_t) num(o, 11);
			}
			a = json_object_get(r, "chops");
			for (size_t i = 0; i < json_array_size(a); i++) {
				json_t* o = json_array_get(a, i);
				int s = (int) num(o, 0);
				if (s < 0 || s >= LEN)
					continue;
				ChopEv& e = pat.chop[s];
				e.pad = (int8_t) clamp((int) num(o, 1), 0, PADS - 1);
				e.len = num(o, 2), e.vel = num(o, 3), e.off = num(o, 4);
				e.pitch = (int8_t) num(o, 5);
				e.rev = num(o, 6) != 0.f;
				e.ratchet = (uint8_t) clamp((int) num(o, 7), 1, 8);
				e.prio = num(o, 8);
				if (json_array_size(o) >= 11)
					e.chance = num(o, 9), e.cond = (uint8_t) num(o, 10);
			}
			a = json_object_get(r, "bars");
			for (size_t i = 0; i < json_array_size(a) && i < (size_t) BARS; i++) {
				json_t* o = json_array_get(a, i);
				pat.bar[i].root = (int) num(o, 0);
				pat.bar[i].quality = (int) num(o, 1);
			}
			a = json_object_get(r, "trackSettings");
			for (size_t i = 0; i < json_array_size(a) && i < (size_t) NUM_TRACKS; i++) {
				json_t* o = json_array_get(a, i);
				pat.lenTrack[i] = (uint8_t) clamp((int) num(o, 0), 0, LEN);
				pat.swTrack[i] = num(o, 1);
				pat.edited[i] = json_is_true(json_array_get(o, 2));
			}
			a = json_object_get(r, "voiceSettings");
			for (size_t i = 0; i < json_array_size(a) && i < (size_t) DRUMS; i++) {
				json_t* o = json_array_get(a, i);
				pat.lenDrum[i] = (uint8_t) clamp((int) num(o, 0), 0, LEN);
				pat.swDrum[i] = num(o, 1);
			}
			pat.chordFollow = getb(r, "chordFollow", true);
		}
		dirty = true;
		json_t* js = json_object_get(r, "sample");
		samplePath = js ? json_string_value(js) : "";
		if (!samplePath.empty() && system::isFile(samplePath))
			startLoad(loader, samplePath, chopMode, true);
		json_t* jb = json_object_get(r, "break");
		breakPath = jb ? json_string_value(jb) : "";
		if (!breakPath.empty() && system::isFile(breakPath))
			startLoad(brkLoader, breakPath, CM_TRANSIENT, true);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (PadSet& ps : pads)
			ps = PadSet();
		lockChords = false;
		set.drumMode = 0;
		applyStyleDefaults();
		reqStyle = true;
	}

	// ---------------------------------------------------------------- UI-thread actions
	void loadSample() {
		std::string dir = samplePath.empty() ? hhx::sampleFolder() : system::getDirectory(samplePath);
		if (!hhx::sampleFolder().empty() && !samplePath.empty() &&
		    samplePath.compare(0, hhx::sampleFolder().size(), hhx::sampleFolder()) != 0)
			dir = hhx::sampleFolder(); // the last sample was outside the root folder: back to the root
		osdialog_filters* f = osdialog_filters_parse("WAV:wav,WAV");
		char* p = osdialog_file(OSDIALOG_OPEN, dir.empty() ? nullptr : dir.c_str(), nullptr, f);
		osdialog_filters_free(f);
		if (!p)
			return;
		samplePath = p;
		std::free(p);
		startLoad(loader, samplePath, chopMode, false);
	}
	void loadBreak() {
		std::string dir = breakPath.empty() ? (samplePath.empty() ? hhx::sampleFolder() : system::getDirectory(samplePath))
		                                    : system::getDirectory(breakPath);
		osdialog_filters* f = osdialog_filters_parse("WAV:wav,WAV");
		char* p = osdialog_file(OSDIALOG_OPEN, dir.empty() ? nullptr : dir.c_str(), nullptr, f);
		osdialog_filters_free(f);
		if (!p)
			return;
		breakPath = p;
		std::free(p);
		startLoad(brkLoader, breakPath, CM_TRANSIENT, true);
		if (set.drumMode == 0)
			set.drumMode = 2;
	}
	void loadPath(const std::string& p) {
		std::string ext = string::lowercase(system::getExtension(p));
		if (ext != ".wav") {
			loader->say("only WAV files", true);
			return;
		}
		samplePath = p;
		startLoad(loader, samplePath, chopMode, false);
	}
	void setChopMode(int m) {
		if (m == chopMode)
			return;
		chopMode = m;
		if (const ChopSet* c = uiChops.load())
			startRecut(loader, c, m);
	}
	std::string saveChops() {
		const ChopSet* c = uiChops.load();
		if (!c)
			return "No sample loaded";
		std::string base = hhx::chopFolder();
		std::string dirName = hhx::freeName(base, c->data.name.empty() ? "chops" : c->data.name, "");
		std::string dir = system::join(base, dirName);
		try {
			system::createDirectories(dir);
			for (int i = 0; i < c->info.count; i++) {
				bool rev = bakeChops && pads[i].rev;
				int semis = bakeChops ? pads[i].pitch + (int) params[PITCH_PARAM].getValue() : 0;
				std::vector<uint8_t> w = writeWav(c->data, c->start[i], c->end[i], rev, semis);
				system::writeFile(system::join(dir, string::f("%s_chop%02d.wav", hhx::cleanName(c->data.name).c_str(), i + 1)), w);
			}
		}
		catch (Exception& e) {
			return e.what();
		}
		return "Saved to " + dir;
	}
	std::string exportMidi() {
		std::vector<uint8_t> f = hhx::midiFile(ren, params[BPM_PARAM].getValue(), params[SWING_PARAM].getValue() / 100.f, pads,
		                                       (int) params[PITCH_PARAM].getValue(), pat);
		std::string dir = hhx::midiFolder();
		std::string name = std::string("HipHop ") + style().shortName;
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
};

