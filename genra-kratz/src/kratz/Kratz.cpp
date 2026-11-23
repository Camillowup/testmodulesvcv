// SPDX-License-Identifier: GPL-3.0-only
// KRATZ – a scratch module with one record (in the look of GENRA TECHNO; a test plugin of its own for now).
// The record: a WAV (LOAD: the sample root folder as SCHLAG's, then the Windows window; or drop a file on the module)
// or what comes into IN L/R (REC). Scratch it with the mouse on the platter, with the faders LVL (the fader: sharp,
// made for a MIDI fader) and SCR (where the record is, over RANGE), with SCR CV, a MIDI jog wheel, or let one of the 23
// patterns (PAT, in time: RATE) do it. HOLD: the record stands still while the hand rests on it; SLIP: when the hand
// lets go the record is where it would have been; LOOP (LOOP length: right-click on LOOP or the mouse wheel on it);
// REV; CUT; eight cues (empty: set, set: jump; right-click: clear). MOTION records the hand for one bar and loops it.
// SYNC: GENRA (the tempo of the GENRA it follows or of CLK), SAMPLE (the sample's own tempo, found when it loads; the
// mouse wheel on the tempo in the display corrects it), FREE. VIN|STR: matching GENRA's tempo changes the pitch like a
// record (VIN) or keeps it (STR). TUNE: OFF, KEY (GENRA's key: its KEY knob, or a note on TUNE), BASS (GENRA's bass
// line). Engine: KratzDsp.hpp; panel: tools/kratz_panel.py (res/Kratz.svg + src/KratzLayout.hpp).
#include "../plugin.hpp"
#include "../tui/TechnoUI.hpp"
#include "../../genra/Follow.hpp"
#include "../../genra/link/GenraPorts.hpp"
#include "../KratzLayout.hpp"
#include "KratzDsp.hpp"
#include <osdialog.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace kratz {

namespace L = kratz_layout;
using namespace tui;

static const NVGcolor AC = nvgRGB(0xa7, 0x8b, 0xfa); // KRATZ's accent (violet)
static const NVGcolor MODEBLUE = nvgRGB(0x18, 0x5f, 0xa5);
static const int NUM_CUES = 8;
static const float REC_SECONDS = 30.f;
static const float LOAD_SECONDS = 120.f;
static const float REV_SECONDS = 1.8f; // one turn of the platter at 33 1/3 rpm
static const int MOT_SLOTS = 1024;      // MOTION: one bar in 1024 steps

enum Sync { S_GENRA, S_SAMPLE, S_FREE, NUM_SYNCS };
static const char* const SYNC_NAMES[NUM_SYNCS] = {"GENRA", "SAMPLE", "FREE"};
enum Tune { T_OFF, T_KEY, T_BASS, NUM_TUNES };
static const char* const TUNE_NAMES[NUM_TUNES] = {"OFF", "KEY", "BASS"};
enum Hand { H_NONE, H_MOUSE, H_SCR, H_JOG };

// the buttons MIDI-MAP can press (BTN_PARAM + i; only ever append)
enum Btn {
	B_LOAD, B_VIN, B_STR, B_MOTION, B_HOLD, B_SLIP, B_LOOP, B_REV, B_REC, B_CUT, B_CUE1, NUM_BTNS = B_CUE1 + NUM_CUES
};
static const tui::BtnDef BTNS[NUM_BTNS] = {
    {L::B_LOAD, "LOAD"}, {L::B_VIN, "VIN"}, {L::B_STR, "STR"}, {L::B_MOTION, "MOTION"}, {L::B_HOLD, "HOLD"},
    {L::B_SLIP, "SLIP"}, {L::B_LOOP, "LOOP"}, {L::B_REV, "REV"}, {L::B_REC, "REC"}, {L::B_CUT, "CUT"},
    {L::B_CUE1, "CUE 1"}, {L::B_CUE2, "CUE 2"}, {L::B_CUE3, "CUE 3"}, {L::B_CUE4, "CUE 4"}, {L::B_CUE5, "CUE 5"},
    {L::B_CUE6, "CUE 6"}, {L::B_CUE7, "CUE 7"}, {L::B_CUE8, "CUE 8"}};
// the fields MIDI-MAP can turn through (FLD_PARAM + i; only ever append)
static const int NUM_FLDS = 4;
static const tui::BtnDef FLDS[NUM_FLDS] = {{L::F_PAT, "PAT"}, {L::F_RATE, "RATE"}, {L::F_SYNC, "SYNC"}, {L::F_TUNE, "TUNE"}};

// ---------------------------------------------------------------- the sample root folder (SCHLAG's)
static std::string settingsPath() { return asset::user("Genra/schlag.json"); }
static std::string rootDir() {
	std::string r;
	if (json_t* j = json_load_file(settingsPath().c_str(), 0, nullptr)) {
		if (json_t* s = json_object_get(j, "sampleRoot"))
			if (json_string_value(s))
				r = json_string_value(s);
		json_decref(j);
	}
	return r;
}
static void setRootDir(const std::string& r) {
	system::createDirectories(asset::user("Genra"));
	json_t* j = json_load_file(settingsPath().c_str(), 0, nullptr);
	if (!j)
		j = json_object();
	json_object_set_new(j, "sampleRoot", json_string(r.c_str()));
	json_dump_file(j, settingsPath().c_str(), JSON_INDENT(2));
	json_decref(j);
}
static bool chooseRoot() {
	std::string start = rootDir();
	char* p = osdialog_file(OSDIALOG_OPEN_DIR, start.empty() || !system::isDirectory(start) ? nullptr : start.c_str(), nullptr,
	                        nullptr);
	if (!p)
		return false;
	std::string r = p;
	std::free(p);
	while (r.size() > 3 && (r.back() == '\\' || r.back() == '/'))
		r.pop_back();
	setRootDir(r);
	return true;
}
static bool isWav(const std::string& p) { return string::lowercase(system::getExtension(p)) == ".wav"; }

// ---------------------------------------------------------------- the analysis (in a thread of its own)
struct Analysis {
	std::atomic<bool> done{false};
	int root = -1;
	bool minor = false;
	float conf = 0.f, bpm = 0.f;
};

// ---------------------------------------------------------------- the module
struct Kratz : Module, follow::Host {
	enum ParamId {
		SPEED_PARAM, PITCH_PARAM, DRAG_PARAM, TONE_PARAM, RANGE_PARAM, LVL_PARAM, SCR_PARAM,
		ENUMS(BTN_PARAM, NUM_BTNS),
		ENUMS(FLD_PARAM, NUM_FLDS),
		PARAMS_LEN
	};
	enum InputId {
		L_INPUT, R_INPUT, CLK_INPUT, RST_INPUT, REC_INPUT, SCR_INPUT, LVL_INPUT, TUNE_INPUT, SPD_INPUT, VOCT_INPUT,
		PAT_INPUT, CUE_INPUT, HOLD_INPUT, CUT_INPUT, LOOP_INPUT, MOT_INPUT, SLIP_INPUT, REV_INPUT,
		INPUTS_LEN
	};
	enum OutputId { L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };

	// ---- the settings (the panel writes, the engine reads)
	std::atomic<int> pat{P_OFF}, rate{3}, sync{S_SAMPLE}, tune{T_OFF}, loopLen{2}, ppqn{4};
	std::atomic<bool> stretch{false}, hold{false}, slip{false}, loopOn{false}, rev{false}, sharpFader{true};
	std::atomic<bool> cutHeld{false};
	std::atomic<uint32_t> motionReq{0}, recReq{0}, cueReq[NUM_CUES], cueClear[NUM_CUES];
	uint32_t motionDone = 0, recDone2 = 0, cueDone[NUM_CUES] = {}, cueClearDone[NUM_CUES] = {};
	uint32_t jsonCode = 0;

	// ---- the record (the panel swaps it, the engine plays it)
	Deck deck;
	std::shared_ptr<Sample> uiCur, uiOld;  // UI thread: who owns the samples
	std::atomic<Sample*> next{nullptr};    // the sample the engine plays
	std::atomic<uint32_t> swapSeq{0}, swapAck{0};
	std::string samplePath;                // where it came from ("" : recorded into the patch)
	bool sampleIsRec = false;
	std::shared_ptr<Analysis> analysis;
	std::shared_ptr<Sample> pendingSample; // waits until the engine has taken the last one (or a patch is loading)
	float pendingBpm = 0.f;
	std::vector<double> pendingCues;
	std::atomic<int> sampleRoot{-1};
	std::atomic<bool> sampleMinor{false};
	std::atomic<float> sampleBpm{0.f};
	float userBpm = 0.f;                   // corrected by hand (mouse wheel); kept with the patch
	std::atomic<double> cues[NUM_CUES];
	int lastCue = -1;

	// ---- the hand
	std::atomic<bool> mouseDown{false};
	std::atomic<float> mouseDelta{0.f};    // radians the platter was turned by the mouse since the engine looked
	std::atomic<float> jogDelta{0.f};      // ticks from a MIDI jog wheel
	int handSrc = H_NONE;
	double handPos = 0.0, scrAnchor = 0.0;
	float scrSm = 0.5f, scrLast = -1.f, lvlSm = 1.f, scrIdle = 1.f, jogIdle = 1.f;

	// ---- the patterns and MOTION
	bool patWas = false;
	double patAnchor = 0.0;
	int midiPat = -1;
	int motion = 0;                        // 0 off, 1 recording, 2 playing
	float motBeat0 = 0.f;
	double motAnchor = 0.0;
	float motX[MOT_SLOTS], motG[MOT_SLOTS];
	uint32_t seed = 0x1234567u;

	// ---- the time
	dsp::SchmittTrigger clkTrig, rstTrig, recTrig, cueTrig[NUM_CUES], holdTrig, loopTrig, motTrig, slipTrig, revTrig, patTrig;
	int64_t pulses = 0;
	float samplesSinceClk = 1e9f, clkPeriod = 0.f;
	double freeBeat = 0.0;
	float beat = 0.f, tempo = 120.f;
	bool genraTime = false;
	bool folWas = false, clkWas = false;
	double loopStart = 0.0;

	// ---- the recorder
	std::vector<float> recBuf;
	int recFrames = 0, recMax = 0;
	bool recording = false;
	std::atomic<int> recDone{-1};          // frames of a finished recording, for the UI thread
	std::atomic<bool> pubRec{false};

	// ---- MIDI
	midi::InputQueue midiIn;
	std::atomic<int> learn{0};             // 1 SCR, 2 LVL, 3 jog
	std::atomic<int> ccScr{-1}, ccLvl{-1}, ccJog{-1};

	// ---- for the panel
	std::atomic<float> pubPos{0.f}, pubTempo{120.f}, pubShift{0.f};
	std::atomic<int> pubFrames{0}, pubPat{0};
	std::atomic<bool> pubGenra{false}, pubHand{false};
	std::atomic<int> pubMotion{0};

	Kratz() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, 0);
		for (int i = 0; i < NUM_CUES; i++) {
			cues[i] = -1.0;
			cueReq[i] = 0;
			cueClear[i] = 0;
		}
		srcName = "KRATZ";
		audioL = L_OUTPUT;
		audioR = R_OUTPUT;
		fol.bind(CLK_INPUT, follow::CLOCK);
		fol.bind(RST_INPUT, follow::RESET);
		configParam(SPEED_PARAM, 0.f, 1.f, 0.5f, "Speed (the motor; middle: normal)");
		configParam(PITCH_PARAM, -12.f, 12.f, 0.f, "Pitch", " semitones");
		configParam(DRAG_PARAM, 0.f, 1.f, 0.35f, "Drag (how long the motor takes to start and stop)");
		configParam(TONE_PARAM, 0.f, 1.f, 0.5f, "Tone (left: low pass, right: high pass)");
		configParam(RANGE_PARAM, 0.f, 1.f, 0.4f, "Range (how much of the record a stroke and the SCR fader cover)");
		configParam(LVL_PARAM, 0.f, 1.f, 1.f, "Level fader (map a MIDI fader to it)", "%", 0.f, 100.f);
		configParam(SCR_PARAM, 0.f, 1.f, 0.5f, "Scratch fader: where the record is (map a MIDI fader to it)", "%", 0.f, 100.f);
		paramQuantities[LVL_PARAM]->randomizeEnabled = false;
		paramQuantities[SCR_PARAM]->randomizeEnabled = false;
		tui::configButtons(this, BTN_PARAM, BTNS, NUM_BTNS);
		tui::configSelects(this, FLD_PARAM, FLDS, NUM_FLDS);
		const char* inNames[INPUTS_LEN] = {
		    "Audio left (REC records it)", "Audio right", "Clock (16ths; right-click: other rates)", "Reset",
		    "REC trigger (start / stop)", "Scratch CV (0–10 V: where the record is, over RANGE)",
		    "Level CV (0–10 V)", "Tune: a note of the key (V/OCT) for TUNE KEY or BASS",
		    "Speed CV (±5 V)", "V/Oct (pitch)", "Pattern gate (high: the pattern plays; unplugged: always)",
		    "Cue trigger (polyphonic: channel n jumps to cue n)", "HOLD trigger (toggles)", "CUT gate (high: silent)",
		    "LOOP trigger (toggles)", "MOTION trigger (record / stop)", "SLIP trigger (toggles)", "REV trigger (toggles)"};
		for (int i = 0; i < INPUTS_LEN; i++)
			configInput(i, inNames[i]);
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		for (int i = 0; i < MOT_SLOTS; i++)
			motX[i] = NAN, motG[i] = 1.f;
		onSampleRateChange();
	}
	~Kratz() {
		next.store(nullptr);
	}

	void onSampleRateChange() override {
		float sr = APP->engine->getSampleRate();
		deck.setSampleRate(sr);
		recMax = (int) (sr * REC_SECONDS);
		recBuf.assign((size_t) recMax * 2, 0.f);
		recording = false;
		recFrames = 0;
	}
	void onRemove(const RemoveEvent& e) override { follow::gone(this); }
	void onPortChange(const PortChangeEvent& e) override {
		fol.portChange(e);
		if (e.type == Port::INPUT && e.portId == CLK_INPUT && e.connecting)
			sync = S_GENRA; // a clock plugged in: SYNC follows it
	}
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		pat = P_OFF;
		rate = 3;
		sync = S_SAMPLE;
		tune = T_OFF;
		loopLen = 2;
		stretch = hold = slip = loopOn = rev = false;
		for (int i = 0; i < NUM_CUES; i++)
			cues[i] = -1.0;
		jsonCode++;
	}

	// ------------------------------------------------------------ the engine
	float stroke(const Sample* s) {
		float b = sampleBpm.load();
		float t = b > 1.f ? b : tempo;
		return rangeBeats(params[RANGE_PARAM].getValue()) * 60.f / t * s->rate;
	}
	void jumpOrSet(int c) {
		if (!deck.smp)
			return;
		if (cues[c].load() < 0.0)
			cues[c] = deck.pos;
		else {
			deck.pos = deck.ghost = cues[c].load();
			handPos = deck.pos;
			scrAnchor = deck.pos - scrSm * stroke(deck.smp);
		}
		lastCue = c;
		if (loopOn)
			loopStart = cues[c].load();
	}

	void handleMidi(const midi::Message& m) {
		int st = m.getStatus();
		if (st == 0xb) {
			int cc = m.getNote(), v = m.getValue();
			int l = learn.load();
			if (l) {
				(l == 1 ? ccScr : l == 2 ? ccLvl : ccJog) = cc;
				learn = 0;
				return;
			}
			if (cc == ccScr)
				params[SCR_PARAM].setValue(v / 127.f);
			if (cc == ccLvl)
				params[LVL_PARAM].setValue(v / 127.f);
			if (cc == ccJog) {
				int d = v < 64 ? v : v - 128; // relative (two's complement); 64 ± n jog wheels show as large values
				if (v > 40 && v < 88)
					d = v - 64;
				jogDelta = jogDelta.load() + (float) d;
			}
		}
		bool on = st == 0x9 && m.getValue() > 0, off = st == 0x8 || (st == 0x9 && m.getValue() == 0);
		if (on || off) {
			int n = m.getNote();
			if (n >= 36 && n < 36 + NUM_CUES && on)
				jumpOrSet(n - 36);
			else if (n == 44)
				cutHeld = on;
			else if (n >= 48 && n < 48 + NUM_PATS - 1) {
				if (on)
					midiPat = n - 48 + 1;
				else if (midiPat == n - 48 + 1)
					midiPat = -1;
			}
		}
	}

	/** The key GENRA plays in (pitch class) or -1. */
	int genraKey(int64_t frame, float sr) {
		Input& ti = inputs[TUNE_INPUT];
		if (ti.isConnected()) {
			int n = (int) std::round(ti.getVoltage() * 12.f);
			return ((n % 12) + 12) % 12;
		}
		Module* g = fol.genra.to.load();
		if (!g)
			return -1;
		if (g->model && g->model->slug == "Genra" && g->model->plugin && g->model->plugin->slug == "Genra" && g->params.size() > 1)
			return ((int) std::round(g->params[1].getValue()) % 12 + 12) % 12; // GENRA's KEY knob (C = 0)
		return bassPc(frame, true); // GENRA CHOP / TECHNO / BOX: the notes of the bass line
	}
	int lastBass = -1;
	float bassHist[12] = {};
	/** The bass note GENRA plays now (pitch class; held while the bass rests). histogram: the most played one. */
	int bassPc(int64_t frame, bool histogram) {
		if (!fol.genra.to.load())
			return histogram ? -1 : lastBass;
		follow::Src sv = follow::genraOut(follow::T_BASS, follow::VOCT), sg = follow::genraOut(follow::T_BASS, follow::GATE);
		const float* v = follow::genraFrame(fol.genra, sv.id, frame);
		const float* gt = follow::genraFrame(fol.genra, sg.id, frame);
		if (v && gt && gt[0] > 1.f) {
			int n = (int) std::round(v[0] * 12.f);
			lastBass = ((n % 12) + 12) % 12;
			bassHist[lastBass] += 1e-4f;
		}
		if (!histogram)
			return lastBass;
		int best = -1;
		float bv = 0.f;
		for (int i = 0; i < 12; i++) {
			bassHist[i] *= 0.999999f;
			if (bassHist[i] > bv)
				bv = bassHist[i], best = i;
		}
		return best;
	}

	void process(const ProcessArgs& args) override {
		fol.process(this, args.frame);
		float sr = args.sampleRate;
		midi::Message msg;
		while (midiIn.tryPop(&msg, args.frame))
			handleMidi(msg);

		// the sample the panel chose
		Sample* s = next.load();
		if (s != deck.smp) {
			deck.setSample(s);
			handSrc = H_NONE;
			motion = 0;
		}
		swapAck = swapSeq.load();

		// ---- SYNC follows when Follow GENRA is switched on
		bool fa = fol.active();
		if (fa && !folWas)
			sync = S_GENRA;
		folWas = fa;

		// ---- the time
		samplesSinceClk += 1.f;
		Input& ci = inputs[CLK_INPUT];
		bool clkIn = ci.isConnected() || fa;
		if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			pulses = 0;
			samplesSinceClk = 0.f;
		}
		if (clkTrig.process(ci.getVoltage(), 0.1f, 1.f)) {
			if (samplesSinceClk < sr * 4.f)
				clkPeriod = samplesSinceClk;
			samplesSinceClk = 0.f;
			pulses++;
		}
		int pq = std::max(1, ppqn.load());
		bool clockAlive = clkIn && clkPeriod > 0.f && samplesSinceClk < std::max(clkPeriod * 3.f, sr * 0.5f);
		int sm = sync.load();
		genraTime = sm == S_GENRA && clockAlive;
		float sb = sampleBpm.load();
		if (genraTime) {
			tempo = fa ? follow::genraBpm(fol.genra, sr) : 60.f * sr / (clkPeriod * pq);
			float frac = clamp(samplesSinceClk / std::max(1.f, clkPeriod), 0.f, 0.999f);
			beat = (float) ((pulses - 1 + frac) / pq);
			if (beat < 0.f)
				beat = 0.f;
		} else {
			tempo = (sm == S_SAMPLE && sb > 1.f) ? sb : 120.f;
			freeBeat += tempo / 60.f / sr;
			if (freeBeat > 1e6)
				freeBeat -= 1e6;
			beat = (float) freeBeat;
		}

		// ---- triggers
		auto toggle = [&](dsp::SchmittTrigger& t, int in, std::atomic<bool>& b) {
			if (t.process(inputs[in].getVoltage(), 0.1f, 1.f))
				b = !b.load();
		};
		toggle(holdTrig, HOLD_INPUT, hold);
		toggle(slipTrig, SLIP_INPUT, slip);
		toggle(revTrig, REV_INPUT, rev);
		bool loopWas = loopOn.load();
		toggle(loopTrig, LOOP_INPUT, loopOn);
		bool recT = recTrig.process(inputs[REC_INPUT].getVoltage(), 0.1f, 1.f);
		uint32_t rr = recReq.load();
		if (rr != recDone2 || recT) {
			recDone2 = rr;
			if (recording) {
				recording = false;
				recDone = recFrames;
			} else if (recDone.load() < 0) {
				recording = true;
				recFrames = 0;
			}
		}
		uint32_t mr = motionReq.load();
		bool motT = motTrig.process(inputs[MOT_INPUT].getVoltage(), 0.1f, 1.f);
		if (mr != motionDone || motT) {
			motionDone = mr;
			if (motion == 0) {
				motion = 1;
				motBeat0 = beat;
				motAnchor = deck.pos;
				for (int i = 0; i < MOT_SLOTS; i++)
					motX[i] = NAN, motG[i] = 1.f;
			} else
				motion = 0;
		}
		for (int c = 0; c < NUM_CUES; c++) {
			uint32_t q = cueReq[c].load();
			if (q != cueDone[c]) {
				cueDone[c] = q;
				jumpOrSet(c);
			}
			uint32_t k = cueClear[c].load();
			if (k != cueClearDone[c]) {
				cueClearDone[c] = k;
				cues[c] = -1.0;
			}
		}
		Input& cuei = inputs[CUE_INPUT];
		if (cuei.isConnected()) {
			int nc = std::min(cuei.getChannels(), NUM_CUES);
			for (int c = 0; c < nc; c++)
				if (cueTrig[c].process(cuei.getVoltage(c), 0.1f, 1.f)) {
					int target = nc == 1 ? std::max(0, lastCue) : c;
					if (cues[target].load() >= 0.0)
						jumpOrSet(target);
				}
		}

		const Sample* smp = deck.smp;
		float l = 0.f, r = 0.f;
		if (smp) {
			float strokeF = stroke(smp);
			// ---- the hand: the mouse, the SCR fader (or its CV), a jog wheel
			float scrSrc = inputs[SCR_INPUT].isConnected() ? clamp(inputs[SCR_INPUT].getVoltage() * 0.1f, 0.f, 1.f)
			                                               : params[SCR_PARAM].getValue();
			scrSm += (scrSrc - scrSm) * (1.f - std::exp(-1.f / (0.004f * sr)));
			bool scrMoved = scrLast >= 0.f && std::fabs(scrSrc - scrLast) > 1e-6f;
			scrLast = scrSrc;
			scrIdle = scrMoved ? 0.f : scrIdle + 1.f / sr;
			float jd = jogDelta.exchange(0.f);
			jogIdle = jd != 0.f ? 0.f : jogIdle + 1.f / sr;
			float md = mouseDelta.exchange(0.f);
			float fpr = smp->rate * REV_SECONDS / (2.f * PI); // frames per radian of the platter
			bool hl = hold.load();
			if (mouseDown.load()) {
				if (handSrc != H_MOUSE)
					handPos = deck.pos;
				handSrc = H_MOUSE;
				handPos += md * fpr;
			} else if (scrIdle < 0.12f || (hl && handSrc == H_SCR)) {
				if (handSrc != H_SCR)
					scrAnchor = deck.pos - scrSm * strokeF;
				handSrc = H_SCR;
				handPos = scrAnchor + scrSm * strokeF;
			} else if (jogIdle < 0.12f || (hl && handSrc == H_JOG)) {
				if (handSrc != H_JOG)
					handPos = deck.pos;
				handSrc = H_JOG;
				handPos += jd * smp->rate * REV_SECONDS / 128.f;
			} else if (!(hl && handSrc != H_NONE))
				handSrc = H_NONE;
			if (handSrc != H_NONE)
				handPos = std::max(0.0, std::min((double) smp->frames - 1.0, handPos));
			bool manual = handSrc != H_NONE;

			// ---- MOTION: one bar of the hand, recorded and looped
			float mGate = 1.f;
			bool motHand = false;
			double motPos = 0.0;
			if (motion) {
				float rel = beat - motBeat0;
				if (rel < 0.f)
					rel = 0.f;
				int slot = clamp((int) (std::fmod(rel, 4.f) / 4.f * MOT_SLOTS), 0, MOT_SLOTS - 1);
				if (motion == 1) {
					motX[slot] = manual ? (float) (handPos - motAnchor) : NAN;
					motG[slot] = faderGain(lvlSm, sharpFader.load());
					if (rel >= 4.f)
						motion = 2;
				} else if (!manual && std::isfinite(motX[slot])) {
					motHand = true;
					motPos = motAnchor + motX[slot];
					mGate = motG[slot];
				}
			}

			// ---- the pattern
			int p = midiPat >= 0 ? midiPat : pat.load();
			bool patGate = !inputs[PAT_INPUT].isConnected() || inputs[PAT_INPUT].getVoltage() >= 1.f || midiPat >= 0;
			bool patOn = p != P_OFF && patGate && !manual && !motHand;
			float pGate = 1.f;
			double pPos = 0.0;
			int shown = 0;
			if (patOn) {
				if (!patWas)
					patAnchor = lastCue >= 0 && cues[lastCue].load() >= 0.0 ? cues[lastCue].load() : deck.pos;
				float ph = beat / RATE_BEATS[clamp(rate.load(), 0, NUM_RATES - 1)];
				int cyc = (int) std::floor(ph);
				shown = resolvePattern(p, cyc, seed);
				float x, g;
				shape(shown, ph - cyc, x, g);
				pPos = patAnchor + x * strokeF;
				pGate = g;
			}
			patWas = patOn;

			// ---- the motor, the pitch, the loop
			float speed = speedOf(params[SPEED_PARAM].getValue() + inputs[SPD_INPUT].getVoltage() * 0.1f);
			float ratio = (genraTime && sb > 1.f) ? tempo / sb : 1.f;
			bool rv = rev.load();
			DeckIn in;
			in.rate = smp->rate / sr * speed * ratio * (rv ? -1.f : 1.f);
			in.drag = dragTime(params[DRAG_PARAM].getValue());
			in.hand = manual || motHand || patOn;
			in.handPos = manual ? handPos : (motHand ? motPos : pPos);
			in.slip = slip.load();
			if (loopOn.load() && !loopWas)
				loopStart = lastCue >= 0 && cues[lastCue].load() >= 0.0 ? cues[lastCue].load() : deck.pos;
			in.loop = loopOn.load();
			in.loopStart = loopStart;
			float lb = sb > 1.f ? sb : tempo;
			in.loopLen = LOOP_BEATS[clamp(loopLen.load(), 0, NUM_LOOPS - 1)] * 60.f / lb * smp->rate;
			// TUNE: the sample's root onto GENRA's key or bass note
			float semis = params[PITCH_PARAM].getValue() + inputs[VOCT_INPUT].getVoltage() * 12.f;
			int tm = tune.load(), sroot = sampleRoot.load();
			if (tm != T_OFF && sroot >= 0) {
				int target = tm == T_KEY ? genraKey(args.frame, sr)
				                         : (inputs[TUNE_INPUT].isConnected() ? genraKey(args.frame, sr) : bassPc(args.frame, false));
				if (target >= 0) {
					int d = ((target - sroot) % 12 + 12) % 12;
					if (d > 6)
						d -= 12;
					semis += d;
				}
			}
			float pitch = std::pow(2.f, semis / 12.f);
			in.shift = stretch.load() ? pitch / std::max(0.25f, ratio) : pitch;
			in.shiftOn = std::fabs(in.shift - 1.f) > 0.001f;
			// the fader, its CV, CUT, the pattern's and MOTION's fader
			lvlSm += (params[LVL_PARAM].getValue() - lvlSm) * (1.f - std::exp(-1.f / (0.003f * sr)));
			float g = faderGain(lvlSm, sharpFader.load());
			if (inputs[LVL_INPUT].isConnected())
				g *= clamp(inputs[LVL_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
			if (patOn)
				g *= pGate;
			if (motHand)
				g *= mGate;
			if (cutHeld.load() || inputs[CUT_INPUT].getVoltage() >= 1.f)
				g = 0.f;
			in.gain = g;
			in.tone = params[TONE_PARAM].getValue();
			deck.process(in, l, r);
			pubPos = (float) deck.pos;
			pubFrames = smp->frames;
			pubShift = semis;
			pubPat = patOn ? shown : 0;
			pubHand = in.hand;
		} else {
			pubFrames = 0;
			pubPat = 0;
		}
		pubTempo = tempo;
		pubGenra = genraTime;
		pubMotion = motion;

		// ---- the recorder
		if (recording) {
			if (recFrames < recMax) {
				float il = inputs[L_INPUT].getVoltageSum() * 0.2f;
				float ir = inputs[R_INPUT].isConnected() ? inputs[R_INPUT].getVoltageSum() * 0.2f : il;
				recBuf[(size_t) recFrames * 2] = il;
				recBuf[(size_t) recFrames * 2 + 1] = ir;
				recFrames++;
			} else {
				recording = false;
				recDone = recFrames;
			}
		}
		pubRec = recording;

		outputs[L_OUTPUT].setVoltage(5.f * l);
		outputs[R_OUTPUT].setVoltage(5.f * r);
	}

	// ------------------------------------------------------------ the panel's side (UI thread)
	/** Play this sample (the UI thread keeps it alive until the engine has let go of the old one). */
	void setSample(std::shared_ptr<Sample> s) {
		if (uiOld && swapAck.load() != swapSeq.load()) {
			// the engine has not taken the last one yet: uiStep() tries again
			pendingSample = s;
			pendingBpm = userBpm;
			return;
		}
		uiOld = uiCur;
		uiCur = s;
		next.store(s.get());
		swapSeq++;
		for (int i = 0; i < NUM_CUES; i++)
			cues[i] = -1.0;
		lastCue = -1;
		sampleRoot = -1;
		sampleBpm = userBpm > 0.f ? userBpm : 0.f;
		std::shared_ptr<Analysis> an = std::make_shared<Analysis>();
		analysis = an;
		if (s)
			std::thread([an, s]() {
				an->root = detectKey(*s, an->minor, an->conf);
				an->bpm = detectBpm(*s);
				an->done = true;
			}).detach();
	}
	bool loadFile(const std::string& path, std::string* err = nullptr) {
		std::vector<uint8_t> buf;
		try {
			buf = system::readFile(path);
		} catch (...) {
			if (err)
				*err = "cannot read the file";
			return false;
		}
		std::string e;
		std::shared_ptr<Sample> s = schlag::readWavData(buf, e, LOAD_SECONDS);
		if (!s) {
			if (err)
				*err = e;
			return false;
		}
		s->name = system::getFilename(path);
		s->makePeaks();
		userBpm = 0.f;
		setSample(s);
		samplePath = path;
		sampleIsRec = false;
		return true;
	}
	void uiStep() {
		if (uiOld && swapAck.load() == swapSeq.load())
			uiOld.reset();
		if (pendingSample && !(uiOld && swapAck.load() != swapSeq.load())) {
			std::shared_ptr<Sample> s = pendingSample;
			pendingSample.reset();
			float ub = pendingBpm;
			setSample(s);
			userBpm = ub;
			if (ub > 0.f)
				sampleBpm = ub;
			for (int i = 0; i < NUM_CUES && i < (int) pendingCues.size(); i++)
				cues[i] = pendingCues[i];
			pendingCues.clear();
		}
		if (analysis && analysis->done.load()) {
			sampleRoot = analysis->root;
			sampleMinor = analysis->minor;
			if (userBpm <= 0.f)
				sampleBpm = analysis->bpm;
			analysis.reset();
		}
		int rd = recDone.load();
		if (rd >= 0) {
			if (rd > 64) {
				auto s = std::make_shared<Sample>();
				s->name = "REC";
				s->channels = 2;
				s->frames = rd;
				s->rate = APP->engine->getSampleRate();
				s->data.assign(recBuf.begin(), recBuf.begin() + (size_t) rd * 2);
				s->makePeaks();
				std::string dir = createPatchStorageDirectory();
				std::string p = system::join(dir, "kratz-rec.wav");
				writeWav(p, s->data.data(), rd, 2, (int) s->rate);
				userBpm = 0.f;
				setSample(s);
				samplePath = p;
				sampleIsRec = true;
			}
			recDone = -1;
		}
	}
	void pickSample() {
		std::string root = rootDir();
		if ((root.empty() || !system::isDirectory(root)) && !chooseRoot())
			return;
		root = rootDir();
		std::string start = !samplePath.empty() && !sampleIsRec ? system::getDirectory(samplePath) : root;
		if (!system::isDirectory(start))
			start = root;
		osdialog_filters* f = osdialog_filters_parse("WAV:wav,WAV");
		char* p = osdialog_file(OSDIALOG_OPEN, start.c_str(), nullptr, f);
		osdialog_filters_free(f);
		if (!p)
			return;
		std::string path = p;
		std::free(p);
		if (isWav(path))
			loadFile(path);
	}

	json_t* dataToJson() override {
		json_t* j = json_object();
		json_object_set_new(j, "pat", json_integer(pat));
		json_object_set_new(j, "rate", json_integer(rate));
		json_object_set_new(j, "sync", json_integer(sync));
		json_object_set_new(j, "tune", json_integer(tune));
		json_object_set_new(j, "loopLen", json_integer(loopLen));
		json_object_set_new(j, "ppqn", json_integer(ppqn));
		json_object_set_new(j, "stretch", json_boolean(stretch));
		json_object_set_new(j, "hold", json_boolean(hold));
		json_object_set_new(j, "slip", json_boolean(slip));
		json_object_set_new(j, "loop", json_boolean(loopOn));
		json_object_set_new(j, "rev", json_boolean(rev));
		json_object_set_new(j, "sharpFader", json_boolean(sharpFader));
		json_object_set_new(j, "ccScr", json_integer(ccScr));
		json_object_set_new(j, "ccLvl", json_integer(ccLvl));
		json_object_set_new(j, "ccJog", json_integer(ccJog));
		json_object_set_new(j, "midi", midiIn.toJson());
		if (!samplePath.empty()) {
			json_object_set_new(j, "sample", json_string(sampleIsRec ? "kratz-rec.wav" : samplePath.c_str()));
			json_object_set_new(j, "sampleIsRec", json_boolean(sampleIsRec));
		}
		json_object_set_new(j, "userBpm", json_real(userBpm));
		json_t* c = json_array();
		for (int i = 0; i < NUM_CUES; i++)
			json_array_append_new(c, json_real(cues[i].load()));
		json_object_set_new(j, "cues", c);
		fol.toJson(j);
		return j;
	}
	void dataFromJson(json_t* j) override {
		auto gi = [&](const char* k, std::atomic<int>& v, int lo, int hi) {
			if (json_t* x = json_object_get(j, k))
				v = clamp((int) json_integer_value(x), lo, hi);
		};
		auto gb = [&](const char* k, std::atomic<bool>& v) {
			if (json_t* x = json_object_get(j, k))
				v = json_boolean_value(x);
		};
		gi("pat", pat, 0, NUM_PATS - 1);
		gi("rate", rate, 0, NUM_RATES - 1);
		gi("sync", sync, 0, NUM_SYNCS - 1);
		gi("tune", tune, 0, NUM_TUNES - 1);
		gi("loopLen", loopLen, 0, NUM_LOOPS - 1);
		gi("ppqn", ppqn, 1, 96);
		gi("ccScr", ccScr, -1, 127);
		gi("ccLvl", ccLvl, -1, 127);
		gi("ccJog", ccJog, -1, 127);
		gb("stretch", stretch);
		gb("hold", hold);
		gb("slip", slip);
		gb("loop", loopOn);
		gb("rev", rev);
		gb("sharpFader", sharpFader);
		if (json_t* x = json_object_get(j, "midi"))
			midiIn.fromJson(x);
		fol.fromJson(j);
		float ub = 0.f;
		if (json_t* x = json_object_get(j, "userBpm"))
			ub = (float) json_number_value(x);
		std::vector<double> cs;
		if (json_t* c = json_object_get(j, "cues"))
			for (size_t i = 0; i < json_array_size(c); i++)
				cs.push_back(json_number_value(json_array_get(c, i)));
		json_t* sp = json_object_get(j, "sample");
		if (sp && json_string_value(sp)) {
			bool isRec = json_is_true(json_object_get(j, "sampleIsRec"));
			std::string path = isRec ? system::join(getPatchStorageDirectory(), json_string_value(sp)) : json_string_value(sp);
			std::vector<uint8_t> buf;
			try {
				buf = system::readFile(path);
			} catch (...) {
			}
			std::string e;
			std::shared_ptr<Sample> s = buf.empty() ? nullptr : schlag::readWavData(buf, e, LOAD_SECONDS);
			if (s) {
				s->name = isRec ? "REC" : system::getFilename(path);
				s->makePeaks();
				pendingSample = s;
				pendingBpm = ub;
				pendingCues = cs;
				samplePath = path;
				sampleIsRec = isRec;
			}
		}
		jsonCode++;
	}
};

// ---------------------------------------------------------------- the panel
/** A field: the name grey, the value white, the arrow in KRATZ's violet (the frame is printed on the panel). */
struct KField : TField {
	void draw(const DrawArgs& args) override {
		std::string v = want >= 0 && item ? item(want) : (value ? value() : "");
		nvgSave(args.vg);
		nvgIntersectScissor(args.vg, 0, 0, box.size.x - 10.f, box.size.y);
		float x = 4.5f;
		if (!prefix.empty())
			x += text(args.vg, bold(), x, box.size.y / 2 + 0.5f, prefix, 8.f, DIM) + 5.f;
		textFit(args.vg, bold(), x, box.size.y / 2 + 0.5f, v, 8.5f, box.size.x - 12.f - x, WHITE);
		nvgRestore(args.vg);
		float ax = box.size.x - 6.f, ay = box.size.y / 2;
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, ax - 3.f, ay - 1.5f);
		nvgLineTo(args.vg, ax + 3.f, ay - 1.5f);
		nvgLineTo(args.vg, ax, ay + 2.f);
		nvgClosePath(args.vg);
		nvgFillColor(args.vg, AC);
		nvgFill(args.vg);
		ParamWidget::draw(args);
	}
};

/** A fader: drag up and down (a MIDI fader mapped with MIDI-MAP or learned in the menu moves it too). */
struct Fader : app::ParamWidget {
	NVGcolor cap = TXT;
	void draw(const DrawArgs& args) override {
		float v = 0.5f;
		if (ParamQuantity* q = getParamQuantity())
			v = q->getScaledValue();
		float top = mm2px(1.5f), bot = box.size.y - mm2px(1.5f);
		float y = bot - v * (bot - top);
		float w = box.size.x * 0.92f, h = mm2px(3.6f);
		roundRect(args.vg, (box.size.x - w) / 2, y - h / 2, w, h, 2.f, cap);
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, (box.size.x - w) / 2 + 2.f, y);
		nvgLineTo(args.vg, (box.size.x + w) / 2 - 2.f, y);
		nvgStrokeColor(args.vg, nvgRGB(0x17, 0x18, 0x1d));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);
		ParamWidget::draw(args);
	}
	void onButton(const ButtonEvent& e) override {
		ParamWidget::onButton(e);
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			if (ParamQuantity* q = getParamQuantity()) {
				float top = mm2px(1.5f), bot = box.size.y - mm2px(1.5f);
				float y = clamp(e.pos.y, top, bot);
				if (std::fabs(y - (bot - q->getScaledValue() * (bot - top))) > mm2px(2.5f))
					q->setScaledValue((bot - y) / (bot - top)); // a click beside the cap jumps there
			}
			e.consume(this);
		}
	}
	void onDragMove(const DragMoveEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		if (ParamQuantity* q = getParamQuantity()) {
			float range = box.size.y - mm2px(3.f);
			float d = -e.mouseDelta.y / getAbsoluteZoom() / range;
			if ((APP->window->getMods() & RACK_MOD_MASK) == GLFW_MOD_SHIFT)
				d *= 0.15f;
			q->setScaledValue(q->getScaledValue() + d);
		}
	}
};

/** The display: the record (turn it with the mouse), the lamps, the line at the bottom (the mouse wheel on the tempo
 * corrects the sample's tempo). */
struct Display : widget::OpaqueWidget {
	Kratz* module = nullptr;
	float spin = 0.f;
	Vec dragPos;
	float lastAng = 0.f;
	bool dragging = false;
	Vec centre() const { return Vec(mm2px(38.f), mm2px(17.5f)); }
	float radius() const { return mm2px(15.5f); }
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && module) {
			if (e.pos.minus(centre()).norm() < radius() + mm2px(2.f)) {
				dragPos = e.pos;
				Vec d = e.pos.minus(centre());
				lastAng = std::atan2(d.y, d.x);
				dragging = true;
				module->mouseDown = true;
				e.consume(this);
				return;
			}
		}
		OpaqueWidget::onButton(e);
	}
	void onDragMove(const DragMoveEvent& e) override {
		if (!dragging || !module)
			return;
		dragPos = dragPos.plus(e.mouseDelta.div(getAbsoluteZoom()));
		Vec d = dragPos.minus(centre());
		if (d.norm() < mm2px(1.5f))
			return;
		float a = std::atan2(d.y, d.x);
		float da = a - lastAng;
		while (da > PI)
			da -= 2.f * PI;
		while (da < -PI)
			da += 2.f * PI;
		lastAng = a;
		module->mouseDelta = module->mouseDelta.load() + da;
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (dragging && module)
			module->mouseDown = false;
		dragging = false;
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (!module || e.pos.x < box.size.x * 0.55f || e.pos.y < box.size.y - mm2px(6.f))
			return OpaqueWidget::onHoverScroll(e);
		float b = module->sampleBpm.load();
		if (b <= 0.f)
			b = 120.f;
		float st = (APP->window->getMods() & RACK_MOD_MASK) == GLFW_MOD_SHIFT ? 0.1f : 1.f;
		b = clamp(b + (e.scrollDelta.y > 0 ? st : -st), 40.f, 240.f);
		module->userBpm = b;
		module->sampleBpm = b;
		e.consume(this);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		float W = box.size.x, H = box.size.y;
		Vec c = centre();
		float R = radius();
		int frames = module ? module->pubFrames.load() : 0;
		float pos = module ? module->pubPos.load() : 0.f;
		const Sample* s = module ? module->uiCur.get() : nullptr;
		float ang;
		if (module && s && frames > 0)
			ang = pos / (s->rate * REV_SECONDS) * 2.f * PI;
		else {
			spin += 0.02f;
			ang = spin;
		}
		// the record
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, R);
		nvgFillColor(vg, nvgRGB(0x14, 0x15, 0x19));
		nvgFill(vg);
		nvgStrokeColor(vg, AC);
		nvgStrokeWidth(vg, 1.3f);
		nvgStroke(vg);
		// the grooves: the waveform round the record (the whole sample is one turn of the rim)
		if (s) {
			for (int i = 0; i < 64; i++) {
				float a0 = -PI / 2 + 2.f * PI * i / 64.f, a1 = -PI / 2 + 2.f * PI * (i + 1) / 64.f;
				float pk = s->peaks[i];
				nvgBeginPath(vg);
				nvgArc(vg, c.x, c.y, R * (0.66f + 0.25f * pk), a0, a1 - 0.01f, NVG_CW);
				nvgStrokeColor(vg, mixc(nvgRGB(0x34, 0x36, 0x3f), AC, 0.25f + 0.5f * pk));
				nvgStrokeWidth(vg, 1.f);
				nvgStroke(vg);
			}
		}
		for (float rr : {0.8f, 0.62f}) {
			nvgBeginPath(vg);
			nvgCircle(vg, c.x, c.y, R * rr);
			nvgStrokeColor(vg, nvgRGB(0x21, 0x23, 0x29));
			nvgStrokeWidth(vg, 0.8f);
			nvgStroke(vg);
		}
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, R * 0.3f);
		nvgFillColor(vg, nvgRGB(0x2c, 0x2e, 0x36));
		nvgFill(vg);
		nvgStrokeColor(vg, AC);
		nvgStrokeWidth(vg, 0.9f);
		nvgStroke(vg);
		// the mark that turns with the record
		nvgBeginPath(vg);
		nvgMoveTo(vg, c.x, c.y);
		nvgLineTo(vg, c.x + std::sin(ang) * (R - 3.f), c.y - std::cos(ang) * (R - 3.f));
		nvgStrokeColor(vg, AC);
		nvgStrokeWidth(vg, 1.8f);
		nvgLineCap(vg, NVG_ROUND);
		nvgStroke(vg);
		// where the record is in the sample, and the cues (on the rim)
		if (module && s && frames > 0) {
			auto rim = [&](double f, NVGcolor col, float rad) {
				float a = -PI / 2 + 2.f * PI * (float) (f / frames);
				nvgBeginPath(vg);
				nvgCircle(vg, c.x + std::cos(a) * (R + 3.2f), c.y + std::sin(a) * (R + 3.2f), rad);
				nvgFillColor(vg, col);
				nvgFill(vg);
			};
			for (int i = 0; i < NUM_CUES; i++)
				if (module->cues[i].load() >= 0.0)
					rim(module->cues[i].load(), nvgRGB(0xd9, 0xdb, 0xe2), 1.6f);
			rim(pos, AC, 2.2f);
		}
		// the tone arm
		Vec pivot(c.x + R + mm2px(9.f), c.y - R + mm2px(1.f));
		nvgBeginPath(vg);
		nvgMoveTo(vg, pivot.x, pivot.y);
		nvgLineTo(vg, c.x + R * 0.55f, c.y + R * 0.7f);
		nvgStrokeColor(vg, nvgRGB(0x55, 0x5a, 0x66));
		nvgStrokeWidth(vg, 2.6f);
		nvgStroke(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, pivot.x, pivot.y, 4.f);
		nvgFillColor(vg, nvgRGB(0x5a, 0x5c, 0x66));
		nvgFill(vg);
		// the lamps
		float lx = mm2px(72.f), ly = mm2px(5.f);
		auto lamp = [&](int k, const std::string& lb, bool on, NVGcolor col) {
			float y = ly + k * mm2px(4.8f);
			nvgBeginPath(vg);
			nvgCircle(vg, lx, y - 0.5f, 2.7f);
			nvgFillColor(vg, on ? col : nvgRGB(0x34, 0x36, 0x3f));
			nvgFill(vg);
			text(vg, bold(), lx + mm2px(2.5f), y, lb, 8.f, on ? TXT : nvgRGB(0x55, 0x5a, 0x66));
		};
		NVGcolor blue = nvgRGB(0x3d, 0x8f, 0xe0);
		int mot = module ? module->pubMotion.load() : 0;
		lamp(0, "HOLD", module && module->hold.load(), blue);
		lamp(1, "SLIP", module && module->slip.load(), blue);
		lamp(2, std::string("LOOP ") + LOOP_NAMES[module ? clamp(module->loopLen.load(), 0, NUM_LOOPS - 1) : 2],
		     module && module->loopOn.load(), AC);
		lamp(3, "REV", module && module->rev.load(), blue);
		lamp(4, mot == 1 ? "MOTION REC" : (mot == 2 ? "MOTION PLAY" : "MOTION"), mot > 0, mot == 1 ? RED : AC);
		// the pattern that plays now
		int pp = module ? module->pubPat.load() : 0;
		if (pp > 0)
			text(vg, bold(), W - mm2px(2.5f), mm2px(4.5f), PAT_NAMES[pp], 8.f, AC, NVG_ALIGN_RIGHT);
		if (module && module->pubRec.load())
			text(vg, bold(), W - mm2px(2.5f), mm2px(9.f), "● REC", 8.f, RED, NVG_ALIGN_RIGHT);
		// the line at the bottom: the sample, its tempo › the tempo it plays in, the key
		float by = H - mm2px(2.2f);
		std::string name = s ? string::uppercase(s->name) : "LOAD OR DROP A WAV · REC RECORDS IN L/R";
		textFit(vg, bold(), mm2px(2.5f), by, name, 8.f, W * 0.5f, s ? DIM : nvgRGB(0x55, 0x5a, 0x66));
		if (module && s) {
			float sb = module->sampleBpm.load();
			std::string t = sb > 0.f ? string::f("SMP %.1f", sb) : std::string("ONE SHOT");
			if (t.size() > 2 && t.substr(t.size() - 2) == ".0")
				t = t.substr(0, t.size() - 2);
			if (module->pubGenra.load())
				t += string::f(" › GENRA %.0f", module->pubTempo.load());
			else if (module->sync.load() == S_FREE)
				t += " · FREE";
			int rt = module->sampleRoot.load();
			if (rt >= 0)
				t += std::string(" · ") + NOTE_NAMES[rt] + (module->sampleMinor.load() ? " MIN" : " MAJ");
			else
				t += " · …";
			float sh = module->pubShift.load();
			if (std::fabs(sh) > 0.05f)
				t += string::f(" %+.0f", sh);
			textFit(vg, bold(), W - mm2px(2.5f), by, t, 8.f, W * 0.46f, AC, NVG_ALIGN_RIGHT);
		}
		Widget::drawLayer(args, layer);
	}
};

struct KratzWidget : ModuleWidget {
	Kratz* mod = nullptr;
	bool mapped = false;

	KratzWidget(Kratz* module) {
		setModule(module);
		mod = module;
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Kratz.svg")));
		follow::knownHost(module);
		using M = Kratz;
		addParam(createParamCentered<GKnob>(mm(L::K_SPEED), module, M::SPEED_PARAM));
		addParam(createParamCentered<GKnob>(mm(L::K_PITCH), module, M::PITCH_PARAM));
		addParam(createParamCentered<GKnob>(mm(L::K_DRAG), module, M::DRAG_PARAM));
		addParam(createParamCentered<GKnob>(mm(L::K_TONE), module, M::TONE_PARAM));
		addParam(createParamCentered<GKnob>(mm(L::K_RANGE), module, M::RANGE_PARAM));
		Fader* lvl = createParam<Fader>(Vec(0, 0), module, M::LVL_PARAM);
		lvl->box = mmRect(L::A_LVL);
		lvl->cap = TXT;
		addParam(lvl);
		Fader* scr = createParam<Fader>(Vec(0, 0), module, M::SCR_PARAM);
		scr->box = mmRect(L::A_SCR);
		scr->cap = AC;
		addParam(scr);

		Display* d = new Display;
		d->module = module;
		d->box = mmRect(L::DISP);
		addChild(d);

		// the fields
		auto field = [&](const float* r, const char* prefix, std::function<std::string()> value, int n,
		                 std::function<int()> cur, std::function<std::string(int)> item, std::function<void(int)> choose) {
			KField* f = place<KField>(this, r);
			f->prefix = prefix;
			f->value = value;
			f->count = [=]() { return n; };
			f->current = cur;
			f->item = item;
			f->choose = choose;
			return f;
		};
		KField* fp = field(
		    L::F_PAT, "PAT", [=]() { return std::string(PAT_NAMES[mod ? mod->pat.load() : 0]); }, NUM_PATS,
		    [=]() { return mod ? mod->pat.load() : 0; }, [](int i) { return std::string(PAT_NAMES[i]); },
		    [=](int i) {
			    if (mod)
				    mod->pat = i;
		    });
		fp->groupOf = [](int i) { return std::string(patGroup(i)); };
		field(
		    L::F_RATE, "RATE", [=]() { return std::string(RATE_NAMES[mod ? mod->rate.load() : 3]); }, NUM_RATES,
		    [=]() { return mod ? mod->rate.load() : 3; }, [](int i) { return std::string(RATE_NAMES[i]); },
		    [=](int i) {
			    if (mod)
				    mod->rate = i;
		    });
		field(
		    L::F_SYNC, "SYNC", [=]() { return std::string(SYNC_NAMES[mod ? mod->sync.load() : 1]); }, NUM_SYNCS,
		    [=]() { return mod ? mod->sync.load() : 1; }, [](int i) { return std::string(SYNC_NAMES[i]); },
		    [=](int i) {
			    if (mod)
				    mod->sync = i;
		    });
		field(
		    L::F_TUNE, "TUNE", [=]() { return std::string(TUNE_NAMES[mod ? mod->tune.load() : 0]); }, NUM_TUNES,
		    [=]() { return mod ? mod->tune.load() : 0; }, [](int i) { return std::string(TUNE_NAMES[i]); },
		    [=](int i) {
			    if (mod)
				    mod->tune = i;
		    });

		// the keys (blue: a mode that is on, red: REC, violet: KRATZ's own)
		auto key = [&](const float* r, const char* label) {
			TButton* b = place<TButton>(this, r);
			b->label = label;
			return b;
		};
		auto modeKey = [&](const float* r, const char* label, std::atomic<bool> Kratz::*flag) {
			TButton* b = key(r, label);
			b->lit = [=]() { return mod && (mod->*flag).load(); };
			b->colOf = [=]() { return mod && (mod->*flag).load() ? MODEBLUE : BTN; };
			b->act = [=]() {
				if (mod)
					(mod->*flag) = !(mod->*flag).load();
			};
			return b;
		};
		TButton* load = key(L::B_LOAD, "LOAD");
		load->col = DISP;
		load->act = [=]() {
			if (mod)
				mod->pickSample();
		};
		TButton* vin = key(L::B_VIN, "VIN");
		vin->colOf = [=]() { return mod && !mod->stretch.load() ? MODEBLUE : DISP; };
		vin->act = [=]() {
			if (mod)
				mod->stretch = false;
		};
		TButton* str = key(L::B_STR, "STR");
		str->colOf = [=]() { return mod && mod->stretch.load() ? MODEBLUE : DISP; };
		str->act = [=]() {
			if (mod)
				mod->stretch = true;
		};
		TButton* motion = key(L::B_MOTION, "MOTION");
		motion->labelOf = [=]() {
			int m = mod ? mod->pubMotion.load() : 0;
			return std::string(m == 1 ? "MOTION REC" : (m == 2 ? "MOTION ▶" : "MOTION"));
		};
		motion->colOf = [=]() {
			int m = mod ? mod->pubMotion.load() : 0;
			return m == 1 ? RED : (m == 2 ? dark(AC, 0.8f) : BTN);
		};
		motion->act = [=]() {
			if (mod)
				mod->motionReq++;
		};
		modeKey(L::B_HOLD, "HOLD", &Kratz::hold);
		modeKey(L::B_SLIP, "SLIP", &Kratz::slip);
		modeKey(L::B_REV, "REV", &Kratz::rev);
		TButton* loop = key(L::B_LOOP, "LOOP");
		loop->labelOf = [=]() {
			return std::string("LOOP ") + LOOP_NAMES[mod ? clamp(mod->loopLen.load(), 0, NUM_LOOPS - 1) : 2];
		};
		loop->textCol = AC;
		loop->lit = [=]() { return mod && mod->loopOn.load(); };
		loop->colOf = [=]() { return mod && mod->loopOn.load() ? dark(AC, 0.55f) : BTN; };
		loop->act = [=]() {
			if (mod)
				mod->loopOn = !mod->loopOn.load();
		};
		loop->rightMenu = [=](ui::Menu* m) {
			m->addChild(createMenuLabel("Loop length"));
			for (int i = 0; i < NUM_LOOPS; i++)
				m->addChild(createCheckMenuItem(
				    LOOP_NAMES[i], "", [=]() { return mod && mod->loopLen.load() == i; },
				    [=]() {
					    if (mod)
						    mod->loopLen = i;
				    }));
		};
		TButton* rec = key(L::B_REC, "REC");
		rec->textCol = RED;
		rec->lit = [=]() { return mod && mod->pubRec.load(); };
		rec->colOf = [=]() {
			if (mod && mod->pubRec.load())
				return std::fmod(system::getTime(), 0.5) < 0.25 ? RED : dark(RED, 0.5f);
			return BTN;
		};
		rec->act = [=]() {
			if (mod)
				mod->recReq++;
		};
		TButton* cut = key(L::B_CUT, "CUT");
		cut->col = AC;
		cut->textCol = nvgRGB(0x17, 0x18, 0x1d);
		cut->act = [=]() {
			if (mod)
				mod->cutHeld = true;
		};
		cut->release = [=]() {
			if (mod)
				mod->cutHeld = false;
		};
		static const float* const CUE_R[NUM_CUES] = {L::B_CUE1, L::B_CUE2, L::B_CUE3, L::B_CUE4,
		                                             L::B_CUE5, L::B_CUE6, L::B_CUE7, L::B_CUE8};
		for (int i = 0; i < NUM_CUES; i++) {
			TButton* c = place<TButton>(this, CUE_R[i]);
			c->label = std::to_string(i + 1);
			c->col = PLATE;
			c->lit = [=]() { return mod && mod->cues[i].load() >= 0.0; };
			c->colOf = [=]() { return mod && mod->cues[i].load() >= 0.0 ? dark(AC, 0.45f) : PLATE; };
			c->act = [=]() {
				if (mod)
					mod->cueReq[i]++;
			};
			c->rightMenu = [=](ui::Menu* m) {
				m->addChild(createMenuLabel(string::f("Cue %d (empty: a click sets it, set: a click jumps)", i + 1)));
				m->addChild(createMenuItem("Clear this cue", "", [=]() {
					if (mod)
						mod->cueClear[i]++;
				}));
			};
		}

		// the jacks
		static const float* const IN[M::INPUTS_LEN] = {
		    L::IN_L, L::IN_R, L::IN_CLK, L::IN_RST, L::IN_REC, L::IN_SCR, L::IN_LVL, L::IN_TUNE, L::IN_SPD,
		    L::IN_VOCT, L::IN_PAT, L::IN_CUE, L::IN_HOLD, L::IN_CUT, L::IN_LOOP, L::IN_MOT, L::IN_SLIP, L::IN_REV};
		for (int i = 0; i < M::INPUTS_LEN; i++)
			addInput(createInputCentered<PJ301MPort>(mm(IN[i]), module, i));
		addOutput(createOutputCentered<PJ301MPort>(mm(L::OUT_L), module, M::L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm(L::OUT_R), module, M::R_OUTPUT));
		follow::addStrip(this, module, {mm(L::IN_CLK), mm(L::IN_RST)});
	}

	void step() override {
		if (mod) {
			mod->uiStep();
			if (!mapped) {
				tui::mapButtons(this, mod, Kratz::BTN_PARAM, BTNS, NUM_BTNS);
				tui::mapFields(this, mod, Kratz::FLD_PARAM, FLDS, NUM_FLDS, &mod->jsonCode);
				mapped = true;
			}
		}
		ModuleWidget::step();
	}
	void onPathDrop(const PathDropEvent& e) override {
		if (mod)
			for (const std::string& p : e.paths)
				if (isWav(p)) {
					mod->loadFile(p);
					e.consume(this);
					return;
				}
		ModuleWidget::onPathDrop(e);
	}
	void appendContextMenu(Menu* menu) override {
		if (follow::Host* h = follow::host(module))
			h->fol.appendMenu(menu, module);
		Kratz* m = mod;
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("The record"));
		menu->addChild(createMenuItem("Load a WAV …", "", [=]() { m->pickSample(); }));
		std::string root = rootDir();
		menu->addChild(createMenuItem(root.empty() ? "Choose the sample root folder …" : "Choose another sample root folder …",
		                              "", []() { chooseRoot(); }));
		if (!root.empty())
			menu->addChild(createMenuLabel("Root: " + root + " (shared with SCHLAG)"));
		menu->addChild(createMenuItem("Find the tempo and key again", "", [=]() {
			m->userBpm = 0.f;
			if (m->uiCur) {
				std::shared_ptr<Analysis> an = std::make_shared<Analysis>();
				m->analysis = an;
				std::shared_ptr<Sample> s = m->uiCur;
				std::thread([an, s]() {
					an->root = detectKey(*s, an->minor, an->conf);
					an->bpm = detectBpm(*s);
					an->done = true;
				}).detach();
			}
		}));
		menu->addChild(createMenuItem("Clear all cues", "", [=]() {
			for (int i = 0; i < NUM_CUES; i++)
				m->cueClear[i]++;
		}));
		menu->addChild(createIndexSubmenuItem(
		    "LVL fader", {"Sharp (a scratch fader: open after a few %)", "Smooth (a volume)"},
		    [=]() { return (size_t) (m->sharpFader.load() ? 0 : 1); }, [=](size_t i) { m->sharpFader = i == 0; }));
		menu->addChild(createIndexSubmenuItem(
		    "CLOCK input", {"Quarters (1 PPQN)", "8ths (2 PPQN)", "16ths (4 PPQN, GENRA)", "24 PPQN"},
		    [=]() {
			    static const int P[4] = {1, 2, 4, 24};
			    for (int i = 0; i < 4; i++)
				    if (P[i] == m->ppqn.load())
					    return (size_t) i;
			    return (size_t) 2;
		    },
		    [=](size_t i) {
			    static const int P[4] = {1, 2, 4, 24};
			    m->ppqn = P[i];
		    }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createSubmenuItem("MIDI input", "", [=](Menu* sub) { appendMidiMenu(sub, &m->midiIn); }));
		auto learnItem = [=](const char* name, int which, std::atomic<int>* cc) {
			std::string right = m->learn.load() == which ? "move it …" : (cc->load() >= 0 ? string::f("CC %d", cc->load()) : "");
			menu->addChild(createMenuItem(name, right, [=]() { m->learn = which; }));
		};
		learnItem("Learn the SCR fader (move a MIDI fader)", 1, &m->ccScr);
		learnItem("Learn the LVL fader (move a MIDI fader)", 2, &m->ccLvl);
		learnItem("Learn a jog wheel (turn it: relative CC)", 3, &m->ccJog);
		menu->addChild(createMenuItem("Forget the learned controls", "", [=]() {
			m->ccScr = -1;
			m->ccLvl = -1;
			m->ccJog = -1;
		}));
		menu->addChild(createMenuLabel("Notes: C1–G1 cues 1–8 · G#1 CUT · from C2 the patterns (held: it plays)"));
		menu->addChild(createMenuLabel("Every knob, fader, key and field can be mapped with MIDI-MAP too"));
	}
};

} // namespace kratz

Model* modelKratz = createModel<kratz::Kratz, kratz::KratzWidget>("Kratz");
