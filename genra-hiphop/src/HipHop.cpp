// SPDX-License-Identifier: GPL-3.0-only
// GENRA HIP-HOP: the module. The engine (hh/Core) makes the patterns; this plays them (clock, swing, the drunk
// timing, rolls), plays the sample's chops and the break's hits, takes the pads from the mouse and MIDI, records them
// with REC, keeps the undo steps, loads and analyses samples in a worker thread, exports MIDI and the chops.
#include "plugin.hpp"
#include "HipHopLayout.hpp"
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
	std::string err; // under m
	bool keepPattern = false;
	~Loader() { delete done.exchange(nullptr); }
};

static void startLoad(std::shared_ptr<Loader> L, std::string path, int mode, bool keepPattern) {
	L->busy = true;
	L->keepPattern = keepPattern;
	std::thread([L, path, mode]() {
		ChopSet* cs = new ChopSet();
		std::string err;
		bool ok = false;
		try {
			std::vector<uint8_t> buf = system::readFile(path);
			ok = readWav(buf, cs->data, err);
		}
		catch (Exception& e) {
			err = e.what();
		}
		if (ok) {
			cs->data.path = path;
			cs->data.name = system::getStem(path);
			analyse(*cs, mode);
			delete L->done.exchange(cs);
		}
		else {
			delete cs;
			std::lock_guard<std::mutex> lk(L->m);
			L->err = err;
		}
		L->busy = false;
	}).detach();
}

/** Re-cut a loaded sample in another mode (a copy of its audio). */
static void startRecut(std::shared_ptr<Loader> L, const ChopSet* from, int mode) {
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
	std::atomic<ChopSet*> trash{nullptr}, trash2{nullptr};
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
	std::atomic<bool> reqUndo{false}, reqStyle{false}, reqRender{false};
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
		for (auto& q : padQ)
			q = -1;
		set.style = 5;
		applyStyleDefaults();
		generate(pat, -1, set, SampleInfo(), pads, random::u32(), false, 92.f);
	}

	~GenraHipHop() {
		delete chops;
		delete brk;
		delete trash.exchange(nullptr);
		delete trash2.exchange(nullptr);
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
	void schedule(int kind, int idx, int s, float off, int extraDelay) {
		float sw = (s % 2) ? (2.f * params[SWING_PARAM].getValue() / 100.f - 1.f) : 0.f;
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

	/** At the tick of step s: what starts on s (late or on time) and what starts early on s+1. */
	void queueStep(int s) {
		int nx = (s + 1) % LEN;
		auto both = [&](int kind, int idx, float offS, bool onS, float offN, bool onN) {
			if (onS && offS >= 0.f)
				schedule(kind, idx, s, offS, 0);
			if (onN && offN < 0.f)
				schedule(kind, idx, nx, offN, (int) stepPeriod);
		};
		for (int v = 0; v < DRUMS; v++)
			both(0, v, ren.drum[v][s].off, ren.drum[v][s].vel > 0.f, ren.drum[v][nx].off, ren.drum[v][nx].vel > 0.f);
		both(1, 0, ren.bass[s].off, ren.bass[s].pitch >= 0, ren.bass[nx].off, ren.bass[nx].pitch >= 0);
		both(2, 0, ren.mel[s].off, ren.mel[s].pitch >= 0, ren.mel[nx].off, ren.mel[nx].pitch >= 0);
		both(3, 0, ren.pad[s].off, ren.pad[s].pitch[0] >= 0, ren.pad[nx].off, ren.pad[nx].pitch[0] >= 0);
		both(4, 0, ren.chop[s].off, ren.chop[s].pad >= 0, ren.chop[nx].off, ren.chop[nx].pad >= 0);
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
		ChopSet* c = (!trash.load() && loader->done.load()) ? loader->done.exchange(nullptr) : nullptr;
		if (c) {
			for (SVoice& v : cv)
				v.active = false;
			trash = chops; // the UI deletes it (collectTrash)
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
		}
		ChopSet* b = (!trash2.load() && brkLoader->done.load()) ? brkLoader->done.exchange(nullptr) : nullptr;
		if (b) {
			for (SVoice& v : bv)
				v.active = false;
			trash2 = brk;
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
		if (dirty) {
			render(pat, knobs, ren);
			dirty = false;
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
		if (!run && step.load() >= 0 && !inputs[CLK_INPUT].isConnected()) {
			// stopped: let the gates go
			bassGate.left = melGate.left = 0;
			for (Gate& g : padGate)
				g.left = 0;
			nPend = 0;
			step = -1;
		}
		if (tickNow) {
			int s = (step.load() + 1) % LEN;
			step = s;
			sinceTick = 0.f;
			clkOutLeft = (int) (0.001f * sr);
			if (s % STEPS == 0)
				barOutLeft = (int) (0.001f * sr);
			if (s == 0) {
				float mut = params[MUTATE_PARAM].getValue();
				if (mut > 0.f && chops) {
					mutateChops(pat, set, chops->info, pads, random::u32(), mut, bpmNow());
					render(pat, knobs, ren);
				}
			}
			queueStep(s);
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
		if (ChopSet* t = trash.load())
			if (uiChops.load() != t)
				delete trash.exchange(nullptr);
		delete trash2.exchange(nullptr);
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
					json_array_append_new(jd, json_pack("[iiffff]", v, s, h.vel, h.prio, h.off, (double) h.ratchet));
			}
		json_object_set_new(r, "drums", jd);
		auto notes = [&](const Note* ln) {
			json_t* a = json_array();
			for (int s = 0; s < LEN; s++)
				if (ln[s].pitch >= 0)
					json_array_append_new(a, json_pack("[iiffffbb]", s, (int) ln[s].pitch, ln[s].len, ln[s].vel, ln[s].prio,
					                                   ln[s].off, ln[s].acc, ln[s].slide));
			return a;
		};
		json_object_set_new(r, "bass", notes(pat.bass));
		json_object_set_new(r, "mel", notes(pat.mel));
		json_t* jpad = json_array();
		for (int s = 0; s < LEN; s++) {
			const PadEv& e = pat.pad[s];
			if (e.pitch[0] >= 0)
				json_array_append_new(jpad, json_pack("[iiiiifffff]", s, (int) e.pitch[0], (int) e.pitch[1], (int) e.pitch[2],
				                                      (int) e.pitch[3], e.len, e.vel, e.prio, e.off, e.mod));
		}
		json_object_set_new(r, "padEvents", jpad);
		json_t* jc = json_array();
		for (int s = 0; s < LEN; s++) {
			const ChopEv& e = pat.chop[s];
			if (e.pad >= 0)
				json_array_append_new(jc, json_pack("[iifffiiif]", s, (int) e.pad, e.len, e.vel, e.off, (int) e.pitch,
				                                    e.rev ? 1 : 0, (int) e.ratchet, e.prio));
		}
		json_object_set_new(r, "chops", jc);
		json_t* jb = json_array();
		for (int b = 0; b < BARS; b++)
			json_array_append_new(jb, json_pack("[ii]", pat.bar[b].root, pat.bar[b].quality));
		json_object_set_new(r, "bars", jb);
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
			}
			a = json_object_get(r, "bars");
			for (size_t i = 0; i < json_array_size(a) && i < (size_t) BARS; i++) {
				json_t* o = json_array_get(a, i);
				pat.bar[i].root = (int) num(o, 0);
				pat.bar[i].quality = (int) num(o, 1);
			}
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
		std::string dir = samplePath.empty() ? "" : system::getDirectory(samplePath);
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
		std::string dir = breakPath.empty() ? (samplePath.empty() ? "" : system::getDirectory(samplePath))
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
		                                       (int) params[PITCH_PARAM].getValue());
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

// ================================================================ the panel
static const NVGcolor C_GRAPH = nvgRGB(0x3b, 0x3a, 0x38), C_CREAM = nvgRGB(0xec, 0xe7, 0xda), C_RED = nvgRGB(0xc8, 0x31, 0x2b),
                      C_PAD = nvgRGB(0xb9, 0xb4, 0xa6), C_PADEDGE = nvgRGB(0x8f, 0x8a, 0x7c), C_INK = nvgRGB(0x3b, 0x3a, 0x38),
                      C_INKDIM = nvgRGB(0x77, 0x73, 0x6a), C_LCD = nvgRGB(0xa9, 0xb9, 0x8c), C_LCDINK = nvgRGB(0x24, 0x30, 0x1a);

static std::shared_ptr<window::Font> uiBold() { return APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf")); }
static std::shared_ptr<window::Font> lcdFont() {
	return APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
}
static void label(NVGcontext* vg, std::shared_ptr<window::Font> f, float x, float y, const std::string& s, float size,
                  NVGcolor col, int align = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE) {
	if (!f)
		return;
	nvgFontFaceId(vg, f->handle);
	nvgFontSize(vg, size);
	nvgTextLetterSpacing(vg, 0.4f);
	nvgFillColor(vg, col);
	nvgTextAlign(vg, align);
	nvgText(vg, x, y, s.c_str(), NULL);
}
static Rect rectOf(const float* v) { return Rect(Vec(v[0], v[1]), Vec(v[2], v[3])); }

/** A button in the look (label in the button): press calls `act`; `lit` shows a toggle's state. */
struct HHButton : widget::OpaqueWidget {
	std::string text;
	NVGcolor col = C_GRAPH;
	std::function<void()> act, release;
	std::function<bool()> lit;
	bool down = false;
	void draw(const DrawArgs& args) override {
		bool on = lit && lit();
		NVGcolor c = col;
		if (down)
			c = nvgLerpRGBA(c, nvgRGB(0, 0, 0), 0.25f);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.4f);
		nvgFillColor(args.vg, c);
		nvgFill(args.vg);
		if (on) {
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 1.f, 1.f, box.size.x - 2.f, box.size.y - 2.f, 2.f);
			nvgStrokeColor(args.vg, nvgRGB(0xff, 0xe0, 0x8a));
			nvgStrokeWidth(args.vg, 1.6f);
			nvgStroke(args.vg);
		}
		label(args.vg, uiBold(), box.size.x / 2, box.size.y / 2 + 0.5f, text, 9.5f, C_CREAM);
	}
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			down = true;
			if (act)
				act();
			e.consume(this);
		}
	}
	void onDragEnd(const DragEndEvent& e) override {
		down = false;
		if (release)
			release();
	}
};

/** A field with a value: click opens the list, the mouse wheel steps through it. */
struct HHField : widget::OpaqueWidget {
	std::function<std::string()> value;
	std::function<int()> count, current;
	std::function<std::string(int)> item;
	std::function<void(int)> choose;
	void draw(const DrawArgs& args) override {
		std::string v = value ? value() : "";
		nvgSave(args.vg);
		nvgIntersectScissor(args.vg, 0, 0, box.size.x - 9.f, box.size.y);
		label(args.vg, uiBold(), 5.f, box.size.y / 2 + 0.5f, v, 9.5f, C_CREAM, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgRestore(args.vg);
		// the arrow
		float x = box.size.x - 6.f, y = box.size.y / 2;
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, x - 2.6f, y - 1.3f);
		nvgLineTo(args.vg, x + 2.6f, y - 1.3f);
		nvgLineTo(args.vg, x, y + 1.7f);
		nvgClosePath(args.vg);
		nvgFillColor(args.vg, C_RED);
		nvgFill(args.vg);
	}
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && count) {
			ui::Menu* menu = createMenu();
			int n = count(), cur = current ? current() : -1;
			for (int i = 0; i < n; i++)
				menu->addChild(createCheckMenuItem(item(i), "", [=]() { return cur == i; }, [=]() { choose(i); }));
			e.consume(this);
		}
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (!count || !current)
			return;
		int n = count();
		int d = e.scrollDelta.y > 0 ? -1 : (e.scrollDelta.y < 0 ? 1 : 0);
		if (d && n > 0)
			choose(clamp(current() + d, 0, n - 1));
		e.consume(this);
	}
};

struct HHPad : widget::OpaqueWidget {
	GenraHipHop* module = nullptr;
	int idx = 0;
	float flash = 0.f;
	uint32_t seenTick = 0;
	void step() override {
		if (module && module->lastPad.load() == idx && module->lastPadTick.load() != seenTick) {
			seenTick = module->lastPadTick.load();
			flash = 1.f;
		}
		flash = std::max(0.f, flash - 0.06f);
		OpaqueWidget::step();
	}
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		const ChopSet* c = module ? module->uiChops.load() : nullptr;
		bool has = c && idx < c->info.count;
		NVGcolor fill = nvgLerpRGBA(C_PAD, nvgRGB(0xf2, 0xc9, 0x6a), flash * 0.85f);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.6f, 0.6f, box.size.x - 1.2f, box.size.y - 1.2f, 3.5f);
		nvgFillColor(vg, fill);
		nvgFill(vg);
		nvgStrokeColor(vg, C_PADEDGE);
		nvgStrokeWidth(vg, 1.2f);
		nvgStroke(vg);
		label(vg, uiBold(), box.size.x / 2, 8.f, std::to_string(idx + 1), 7.5f, C_INKDIM);
		if (!module || !has)
			return;
		const PadSet& ps = module->pads[idx];
		Chord ch = padChord(c->info, module->pads, idx);
		char name[16];
		if (ch.valid()) {
			ch.root = (ch.root + ps.pitch + 120) % 12;
			chordName(ch, name, sizeof(name));
		}
		else
			snprintf(name, sizeof(name), "%s", c->info.chop[idx].kind == 2 ? "HIT" : "-");
		label(vg, uiBold(), box.size.x / 2, box.size.y / 2 + 2.f, name, 10.5f, ps.chordQ >= 0 ? C_RED : C_INK);
		std::string tags;
		if (ps.lock)
			tags += "L ";
		if (ps.rev)
			tags += "R ";
		if (ps.pitch)
			tags += string::f("%+d", ps.pitch);
		if (!tags.empty())
			label(vg, uiBold(), box.size.x / 2, box.size.y - 6.f, tags, 7.f, C_RED);
	}
	void onButton(const ButtonEvent& e) override {
		if (!module)
			return;
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			module->queuePad(idx, 0.9f);
			e.consume(this);
		}
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS) {
			menu();
			e.consume(this);
		}
	}
	void menu() {
		GenraHipHop* m = module;
		int i = idx;
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Pad %d", i + 1)));
		menu->addChild(createBoolPtrMenuItem("Lock (stays through NEW and MUTATE)", "", &m->pads[i].lock));
		menu->addChild(createBoolPtrMenuItem("Reverse", "", &m->pads[i].rev));
		menu->addChild(createSubmenuItem("Pitch", string::f("%+d", m->pads[i].pitch), [=](ui::Menu* sub) {
			for (int p = -12; p <= 12; p++)
				sub->addChild(createCheckMenuItem(string::f("%+d", p), "", [=]() { return m->pads[i].pitch == p; },
				                                  [=]() { m->pads[i].pitch = (int8_t) p; }));
		}));
		const ChopSet* c = m->uiChops.load();
		std::string det = "-";
		if (c && i < c->info.count) {
			char nm[16];
			chordName(c->info.chop[i].chord, nm, sizeof(nm));
			det = nm;
		}
		menu->addChild(createSubmenuItem("Chord", m->pads[i].chordQ >= 0 ? "set by hand" : "detected", [=](ui::Menu* sub) {
			sub->addChild(createCheckMenuItem("As detected (" + det + ")", "", [=]() { return m->pads[i].chordQ < 0; }, [=]() {
				m->pads[i].chordQ = -1;
				m->reqRender = true;
			}));
			sub->addChild(createCheckMenuItem("No chord (a hit)", "", [=]() { return m->pads[i].chordQ == 99; }, [=]() {
				m->pads[i].chordQ = 99;
			}));
			for (int r = 0; r < 12; r++)
				sub->addChild(createSubmenuItem(NOTE_NAMES[r], "", [=](ui::Menu* s2) {
					for (int q = 0; q < NUM_QUALITIES; q++) {
						std::string nm = std::string(NOTE_NAMES[r]) + QUALITY_NAMES[q];
						s2->addChild(createCheckMenuItem(nm, "", [=]() { return m->pads[i].chordRoot == r && m->pads[i].chordQ == q; },
						                                 [=]() {
							                                 m->pads[i].chordRoot = (int8_t) r;
							                                 m->pads[i].chordQ = (int8_t) q;
						                                 }));
					}
				}));
		}));
	}
};

struct HHLcd : widget::Widget {
	GenraHipHop* module = nullptr;
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 1.5f);
		nvgFillColor(vg, C_LCD);
		nvgFill(vg);
		auto f = lcdFont();
		std::string l1 = "90s > EAST COAST BOOM BAP", l2 = "92 BPM  F#m  SWING 58%", l3 = "NO SAMPLE - PRESS LOAD";
		if (module) {
			const Style& st = module->style();
			l1 = string::uppercase(string::f("%s > %s", ERA_NAMES[st.era], st.name));
			char kn[16];
			keyName(module->set.key, kn, sizeof(kn));
			int s = module->step.load();
			l2 = string::f("%3.0f BPM  %s%s  SWING %2.0f%%  %s", module->bpmNow(), kn, module->set.keyAuto ? "" : "*",
			               module->params[GenraHipHop::SWING_PARAM].getValue(),
			               s >= 0 ? string::f("BAR %d.%d", s / STEPS + 1, (s % STEPS) / 4 + 1).c_str() : "STOP");
			const ChopSet* c = module->uiChops.load();
			if (module->loader->busy)
				l3 = "ANALYSING SAMPLE...";
			else if (c)
				l3 = string::f("SMPL %s  %d CHOPS  KEY:%s", string::uppercase(c->data.name).substr(0, 18).c_str(),
				               c->info.count, module->set.keyAuto ? "AUTO" : "MAN");
			else {
				std::lock_guard<std::mutex> lk(module->loader->m);
				l3 = module->loader->err.empty() ? "NO SAMPLE - PRESS LOAD" : string::uppercase("ERR: " + module->loader->err);
			}
			if (module->rec)
				l3 = "REC  " + l3;
		}
		label(vg, f, 6.f, 9.5f, l1, 13.f, C_LCDINK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		label(vg, f, 6.f, 22.f, l2, 11.5f, C_LCDINK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		label(vg, f, 6.f, 33.5f, l3, 10.f, nvgRGB(0x3a, 0x4a, 0x2a), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	}
};

struct HHWave : widget::Widget {
	GenraHipHop* module = nullptr;
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		const ChopSet* c = module ? module->uiChops.load() : nullptr;
		float w = box.size.x, h = box.size.y;
		if (!c || c->peaks.empty()) {
			label(vg, uiBold(), w / 2, h / 2, module && module->loader->busy ? "ANALYSING..." : "LOAD A WAV (LOAD BUTTON)", 9.f,
			      nvgRGB(0x8f, 0x8a, 0x7c));
			return;
		}
		size_t total = c->data.frames();
		// the chop playing right now
		int lp = module->lastPad.load();
		if (lp >= 0 && lp < c->info.count && total) {
			float x0 = w * c->start[lp] / total, x1 = w * c->end[lp] / total;
			nvgBeginPath(vg);
			nvgRect(vg, x0, 0, x1 - x0, h);
			nvgFillColor(vg, nvgRGBA(0xa9, 0xb9, 0x8c, 0x30));
			nvgFill(vg);
		}
		nvgBeginPath(vg);
		size_t n = c->peaks.size();
		for (size_t i = 0; i < n; i++) {
			float x = w * i / (n - 1), a = c->peaks[i] * h * 0.48f;
			nvgRect(vg, x, h / 2 - a, std::max(0.6f, w / n), 2 * a);
		}
		nvgFillColor(vg, C_LCD);
		nvgFill(vg);
		for (int p = 0; p < c->info.count; p++) {
			float x = total ? w * c->start[p] / total : 0.f;
			nvgBeginPath(vg);
			nvgRect(vg, x, 0, 0.8f, h);
			nvgFillColor(vg, C_RED);
			nvgFill(vg);
			label(vg, uiBold(), x + 2.f, 5.f, std::to_string(p + 1), 6.5f, nvgRGB(0xd8, 0xd1, 0xbf), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		}
	}
};

struct HHBars : widget::Widget {
	GenraHipHop* module = nullptr;
	void draw(const DrawArgs& args) override {
		int s = module ? module->step.load() : 16;
		for (int i = 0; i < BARS; i++) {
			float w = box.size.x / BARS;
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, i * w + 1.5f, 0, w - 3.f, box.size.y, 1.8f);
			nvgFillColor(args.vg, s >= 0 && s / STEPS == i ? C_RED : C_GRAPH);
			nvgFill(args.vg);
		}
	}
};

struct GenraHipHopWidget : ModuleWidget {
	std::string note; // last export message
	float noteTime = 0.f;

	template <typename T>
	T* at(const float* r) {
		T* w = new T();
		w->box = rectOf(r);
		addChild(w);
		return w;
	}
	HHButton* button(const float* r, const char* text, NVGcolor col, std::function<void()> act) {
		HHButton* b = at<HHButton>(r);
		b->text = text;
		b->col = col;
		b->act = act;
		return b;
	}

	GenraHipHopWidget(GenraHipHop* m) {
		setModule(m);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/HipHop.svg")));
		using M = GenraHipHop;
		// header
		HHButton* play = button(hhl::PLAY, "PLAY", C_RED, [=]() {
			if (m)
				m->running = !m->running;
		});
		play->lit = [=]() { return m && m->running; };
		addParam(createParamCentered<HHKnobSmall>(Vec(hhl::BPM[0], hhl::BPM[1]), m, M::BPM_PARAM));
		addParam(createParamCentered<HHKnobSmall>(Vec(hhl::SWING[0], hhl::SWING[1]), m, M::SWING_PARAM));
		addParam(createParamCentered<HHKnobSmall>(Vec(hhl::VOL[0], hhl::VOL[1]), m, M::VOL_PARAM));
		addParam(createParamCentered<HHKnobSmall>(Vec(hhl::BRK[0], hhl::BRK[1]), m, M::BRK_PARAM));
		// LCD, fields
		HHLcd* lcd = at<HHLcd>(hhl::LCD);
		lcd->module = m;
		HHField* era = at<HHField>(hhl::ERA);
		era->value = [=]() { return std::string("ERA  ") + ERA_NAMES[m ? m->style().era : 1]; };
		era->count = []() { return (int) NUM_ERAS; };
		era->current = [=]() { return m ? m->style().era : 1; };
		era->item = [](int i) { return std::string(ERA_NAMES[i]); };
		era->choose = [=](int i) {
			if (!m || i == m->style().era)
				return;
			int list[NUM_STYLES];
			stylesInEra(i, list, NUM_STYLES);
			chooseStyle(m, list[0]);
		};
		HHField* sty = at<HHField>(hhl::STYLE);
		sty->value = [=]() { return std::string("STYLE  ") + (m ? m->style().name : "East Coast Boom Bap"); };
		sty->count = [=]() { return m ? stylesInEra(m->style().era, nullptr, 0) : 0; };
		sty->current = [=]() { return m ? styleIndexInEra(m->set.style) : 0; };
		sty->item = [=](int i) {
			int list[NUM_STYLES];
			stylesInEra(m->style().era, list, NUM_STYLES);
			return std::string(STYLES[list[i]].name);
		};
		sty->choose = [=](int i) {
			int list[NUM_STYLES];
			stylesInEra(m->style().era, list, NUM_STYLES);
			chooseStyle(m, list[i]);
		};
		HHField* key = at<HHField>(hhl::KEY);
		key->value = [=]() {
			if (!m)
				return std::string("KEY  Auto");
			char kn[16];
			keyName(m->set.key, kn, sizeof(kn));
			return std::string("KEY  ") + (m->set.keyAuto ? "Auto " : "") + kn;
		};
		key->count = []() { return 25; };
		key->current = [=]() { return !m || m->set.keyAuto ? 0 : 1 + m->set.key.root * 2 + (m->set.key.minor ? 1 : 0); };
		key->item = [](int i) {
			if (i == 0)
				return std::string("Auto (from the sample)");
			Key k;
			k.root = (i - 1) / 2;
			k.minor = (i - 1) % 2;
			char kn[16];
			keyName(k, kn, sizeof(kn));
			return std::string(kn) + (k.minor ? " minor" : " major");
		};
		key->choose = [=](int i) {
			if (!m)
				return;
			if (i == 0) {
				m->set.keyAuto = true;
				if (const ChopSet* c = m->uiChops.load())
					m->set.key = c->info.key;
			}
			else {
				m->set.keyAuto = false;
				m->set.key.root = (i - 1) / 2;
				m->set.key.minor = (i - 1) % 2;
			}
			m->reqNew.fetch_or(1 << T_BASS) | (1 << T_MEL) | (1 << T_PAD);
		};
		// the pads
		for (int i = 0; i < PADS; i++) {
			const float* r = i == 0 ? hhl::PAD0 : i == 1 ? hhl::PAD1 : i == 2 ? hhl::PAD2 : i == 3 ? hhl::PAD3 : i == 4 ? hhl::PAD4
			                 : i == 5 ? hhl::PAD5 : i == 6 ? hhl::PAD6 : i == 7 ? hhl::PAD7 : i == 8 ? hhl::PAD8 : i == 9 ? hhl::PAD9
			                 : i == 10 ? hhl::PAD10 : i == 11 ? hhl::PAD11 : i == 12 ? hhl::PAD12 : i == 13 ? hhl::PAD13
			                 : i == 14 ? hhl::PAD14 : hhl::PAD15;
			HHPad* p = at<HHPad>(r);
			p->module = m;
			p->idx = i;
		}
		// CHOP
		HHField* mode = at<HHField>(hhl::MODE);
		mode->value = [=]() { return std::string(CHOP_MODES[m ? m->chopMode : 0]); };
		mode->count = []() { return (int) NUM_CHOP_MODES; };
		mode->current = [=]() { return m ? m->chopMode : 0; };
		mode->item = [](int i) { return std::string(CHOP_MODES[i]); };
		mode->choose = [=](int i) {
			if (m)
				m->setChopMode(i);
		};
		addParam(createParamCentered<HHKnob>(Vec(hhl::MUTATE[0], hhl::MUTATE[1]), m, M::MUTATE_PARAM));
		addParam(createParamCentered<HHKnob>(Vec(hhl::PITCH[0], hhl::PITCH[1]), m, M::PITCH_PARAM));
		addParam(createParamCentered<HHKnob>(Vec(hhl::DUST[0], hhl::DUST[1]), m, M::DUST_PARAM));
		button(hhl::LOAD, "LOAD", C_GRAPH, [=]() {
			if (m)
				m->loadSample();
		});
		button(hhl::NEW, "NEW", C_RED, [=]() {
			if (m)
				m->reqNew.fetch_or(1 << T_CHOP);
		});
		HHButton* rec = button(hhl::REC, "REC", C_RED, [=]() {
			if (m)
				m->rec = !m->rec;
		});
		rec->lit = [=]() { return m && m->rec; };
		HHButton* lock = button(hhl::LOCK, "LOCK", C_GRAPH, [=]() {
			if (m)
				m->lockChords = !m->lockChords;
		});
		lock->lit = [=]() { return m && m->lockChords; };
		HHButton* root = button(hhl::ROOT, "ROOT", C_GRAPH, [=]() {
			if (m)
				m->rootHeld = true;
		});
		root->release = [=]() {
			if (m)
				m->rootHeld = false;
		};
		button(hhl::UNDO, "UNDO", C_GRAPH, [=]() {
			if (m)
				m->reqUndo = true;
		});
		button(hhl::SAVE, "SAVE", C_GRAPH, [=]() {
			if (m)
				say(m->saveChops());
		});
		button(hhl::NEWALL, "NEW ALL", C_RED, [=]() {
			if (m)
				m->reqNew.fetch_or(1 << 5);
		});
		// the waveform and the tracks
		HHWave* wave = at<HHWave>(hhl::WAVE);
		wave->module = m;
		const float* TM[4] = {hhl::TMODE0, hhl::TMODE1, hhl::TMODE2, hhl::TMODE3};
		const float* DN[4] = {hhl::DENS0, hhl::DENS1, hhl::DENS2, hhl::DENS3};
		const float* K2[4] = {hhl::TK2_0, hhl::TK2_1, hhl::TK2_2, hhl::TK2_3};
		const float* NW[4] = {hhl::TNEW0, hhl::TNEW1, hhl::TNEW2, hhl::TNEW3};
		for (int t = 0; t < 4; t++) {
			HHField* f = at<HHField>(TM[t]);
			f->count = [t]() { return t == 0 ? 3 : t == 1 ? 4 : t == 2 ? 4 : 3; };
			f->item = [t](int i) {
				return std::string(t == 0 ? DRUM_MODES[i] : t == 1 ? BASS_MODES[i] : t == 2 ? MEL_MODES[i] : PAD_MODES[i]);
			};
			f->current = [=]() {
				if (!m)
					return 0;
				return t == 0 ? m->set.drumMode : t == 1 ? m->set.bassMode : t == 2 ? m->set.melMode : m->set.padMode;
			};
			f->value = [=]() { return f->item(f->current()); };
			f->choose = [=](int i) {
				if (!m)
					return;
				(t == 0 ? m->set.drumMode : t == 1 ? m->set.bassMode : t == 2 ? m->set.melMode : m->set.padMode) = i;
				if (t > 0)
					m->reqNew.fetch_or(1 << t);
			};
			addParam(createParamCentered<HHKnob>(Vec(DN[t][0], DN[t][1]), m, M::DENS_PARAM + t));
			addParam(createParamCentered<HHKnob>(Vec(K2[t][0], K2[t][1]), m, M::TK2_PARAM + t));
			button(NW[t], "NEW", C_RED, [=]() {
				if (m)
					m->reqNew.fetch_or(1 << t);
			});
		}
		HHBars* bars = at<HHBars>(hhl::BARS);
		bars->module = m;
		// jacks
		auto in = [&](const float* p, int id) { addInput(createInputCentered<PJ301MPort>(Vec(p[0], p[1]), m, id)); };
		auto out = [&](const float* p, int id) { addOutput(createOutputCentered<PJ301MPort>(Vec(p[0], p[1]), m, id)); };
		in(hhl::CLK_IN, M::CLK_INPUT);
		in(hhl::RST_IN, M::RST_INPUT);
		in(hhl::RUN_IN, M::RUN_INPUT);
		in(hhl::NEW_IN, M::NEW_INPUT);
		const float* DR[8] = {hhl::DRUM0, hhl::DRUM1, hhl::DRUM2, hhl::DRUM3, hhl::DRUM4, hhl::DRUM5, hhl::DRUM6, hhl::DRUM7};
		for (int i = 0; i < 8; i++)
			out(DR[i], M::DRUM_OUTPUT + i);
		const float* BA[5] = {hhl::BASS0, hhl::BASS1, hhl::BASS2, hhl::BASS3, hhl::BASS4};
		const float* ME[5] = {hhl::MEL0, hhl::MEL1, hhl::MEL2, hhl::MEL3, hhl::MEL4};
		for (int i = 0; i < 5; i++) {
			out(BA[i], M::BASS_OUTPUT + i);
			out(ME[i], M::MEL_OUTPUT + i);
		}
		const float* PO[4] = {hhl::PADO0, hhl::PADO1, hhl::PADO2, hhl::PADO3};
		for (int i = 0; i < 4; i++)
			out(PO[i], M::PAD_OUTPUT + i);
		out(hhl::CHOP_L, M::CHOP_L_OUTPUT);
		out(hhl::CHOP_R, M::CHOP_R_OUTPUT);
		out(hhl::BRK_L, M::BRK_L_OUTPUT);
		out(hhl::BRK_R, M::BRK_R_OUTPUT);
		out(hhl::CLK_OUT, M::CLK_OUTPUT);
		out(hhl::RST_OUT, M::RST_OUTPUT);
		out(hhl::BAR_OUT, M::BAR_OUTPUT);
	}

	static void chooseStyle(GenraHipHop* m, int s) {
		if (!m)
			return;
		m->set.style = s;
		m->applyStyleDefaults();
		m->params[GenraHipHop::BPM_PARAM].setValue(STYLES[s].bpm);
		m->params[GenraHipHop::SWING_PARAM].setValue(STYLES[s].swing * 100.f);
		m->reqStyle = true;
	}

	void say(const std::string& s) {
		note = s;
		noteTime = 3.f;
	}

	void step() override {
		if (GenraHipHop* m = dynamic_cast<GenraHipHop*>(module))
			m->collectTrash();
		if (noteTime > 0.f)
			noteTime -= APP->window->getLastFrameDuration();
		ModuleWidget::step();
	}

	void draw(const DrawArgs& args) override {
		ModuleWidget::draw(args);
		if (noteTime > 0.f && !note.empty()) {
			// a short message over the waveform (export done / failed)
			float x = hhl::WAVE[0], y = hhl::WAVE[1] + hhl::WAVE[3] - 12.f, w = hhl::WAVE[2];
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x, y, w, 12.f);
			nvgFillColor(args.vg, nvgRGBA(0x2a, 0x29, 0x28, 0xe0));
			nvgFill(args.vg);
			label(args.vg, uiBold(), x + 4.f, y + 6.f, note, 8.f, C_CREAM, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		}
	}

	void appendContextMenu(Menu* menu) override {
		GenraHipHop* m = dynamic_cast<GenraHipHop*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Sample"));
		menu->addChild(createMenuItem("Load sample…", "", [=]() { m->loadSample(); }));
		menu->addChild(createMenuItem("Load break (drums)…", "", [=]() { m->loadBreak(); }));
		menu->addChild(createMenuItem("Clear break", "", [=]() {
			m->breakPath.clear();
			m->set.drumMode = 0;
		}));
		menu->addChild(createBoolPtrMenuItem("SAVE writes the chops with their pitch and direction", "", &m->bakeChops));
		menu->addChild(createMenuItem("Chop folder…", "", []() { hhx::pickChopFolder(); }));
		menu->addChild(createMenuItem("Open chop folder", "", []() {
			system::createDirectories(hhx::chopFolder());
			system::openDirectory(hhx::chopFolder());
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("MIDI"));
		menu->addChild(createMenuItem("Export MIDI (4 bars, 5 tracks)", "", [=]() { say(m->exportMidi()); }));
		menu->addChild(createMenuItem("MIDI export folder…", "", []() { hhx::pickMidiFolder(); }));
		menu->addChild(createMenuItem("Open MIDI export folder", "", []() {
			system::createDirectories(hhx::midiFolder());
			system::openDirectory(hhx::midiFolder());
		}));
		menu->addChild(createSubmenuItem("MIDI input (plays the pads, C1 = pad 1)", "", [=](Menu* sub) {
			app::appendMidiMenu(sub, &m->midiIn);
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Clock input", {"16ths", "24 PPQN", "48 PPQN"},
		                                      [=]() { return m->clockDiv == 6 ? 1 : m->clockDiv == 12 ? 2 : 0; },
		                                      [=](int i) { m->clockDiv = i == 1 ? 6 : i == 2 ? 12 : 1; }));
		menu->addChild(createMenuItem("Undo steps", string::f("%d", m->undoCount.load()), []() {}));
	}
};

Model* modelGenraHipHop = createModel<GenraHipHop, GenraHipHopWidget>("GenraHipHop");
