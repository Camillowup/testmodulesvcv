// SPDX-License-Identifier: GPL-3.0-only
// KESSEL: the drum engine of the LXR firmware (Erica Synths, GPL-3.0) as a GENRA drum module, with its expander
// KESSEL FX (the LXR's effect bus plus a bit crusher and a DJ filter).
// The engine runs unchanged in a private copy of the KESSEL core (KesselCore.hpp): six voices (three drum voices,
// snare, cymbal, hi-hat), a block of 64 samples at 44.1 kHz at a time. This file is the panel and the glue: GENRA's
// eight drum triggers in GENRA's order onto the voices (the clap plays the snare: on the LXR the snare is the clap,
// by REPEAT), the knobs of the lane edited, the kit macros, the mix, the effect bus, all resampled to Rack's rate.
// Not affiliated with or endorsed by Erica Synths; the engine is taken from their open-source firmware.
#include "../plugin.hpp"
#include "../../genra/Follow.hpp"
#include "../../genra/OutLimit.hpp"
#include "../../genra/ui/GenraUI.hpp"
#include "../../genra/ui/StateBank.hpp"
#include "KesselCore.hpp"
#include "KesselLayout.hpp"
#include "KesselKits.hpp"
#include <atomic>
#include <array>
#include <cmath>
#include <cctype>
#include <cstring>

namespace genra_kessel {

namespace ui = genra_ui;
#define KESSEL_RGB 0x7b, 0x6c, 0xff  // keep in step with tools/kessel_panel.py: the drum track's colour (DRUM-X)
static const NVGcolor ACCENT = nvgRGB(KESSEL_RGB);

// ------------------------------------------------------------------------------------------------ lanes and knobs
static const int NL = 8;  // the lanes, in GENRA's order of its drum outputs
static const char* LANE_NAMES[NL] = {"KICK", "SNARE", "", "RIM", "HAT CL", "HAT OP", "TOM", "CYM"};
static const int LANE_VOICE[NL] = {JKS_DRUM1, JKS_SNARE, -1, JKS_DRUM3, JKS_HAT, JKS_HAT, JKS_DRUM2, JKS_CYM};
/** GENRA's drum triggers (kick, snare, clap, rim, closed hat, open hat, tom, perc) onto the lanes: the clap plays the
 * snare. */
static const int GENRA_LANE[8] = {0, 1, 1, 3, 4, 5, 6, 7};
static const int NOUT = 7;  // the lane outputs (the clap's place has none)
static const int OUT_LANE[NOUT] = {0, 1, 3, 4, 5, 6, 7};

enum Edit { E_WAVE, E_TUNE, E_FM, E_FMFREQ, E_CLICK, E_PAMT, E_PDEC, E_PAN, E_FTYPE, E_CUT, E_RES, E_DRIVE, E_LEVEL,
            E_ATK, E_DEC, E_SEND, NE };
static const int E_SLOT[NE] = {JKS_S_WAVE,  JKS_S_TUNE, JKS_S_FM,  JKS_S_FMFREQ, JKS_S_CLICK, JKS_S_PAMT,
                               JKS_S_PDEC,  JKS_S_PAN,  JKS_S_FTYPE, JKS_S_CUT,  JKS_S_RES,   JKS_S_DRIVE,
                               JKS_S_LEVEL, JKS_S_ATK,  JKS_S_DEC, -1};
static const char* E_NAMES[NE] = {"Oscillator", "Tune", "FM", "FM frequency", "Click", "Pitch envelope amount",
                                  "Pitch envelope decay", "Pan", "Filter type", "Cutoff", "Resonance", "Drive",
                                  "Level", "Attack", "Decay", "FX send"};
static const int NWAVE = 6, NFILT = 7;
static const char* const WAVE_NAMES[NWAVE] = {"SINE", "TRIANGLE", "SAW", "SQUARE", "NOISE", "PWM"};
static const char* const WAVE_INFO[NWAVE] = {"OSCILLATOR", "OSCILLATOR", "OSCILLATOR", "OSCILLATOR", "OSCILLATOR",
                                             "OSCILLATOR"};
static const char* const FILT_NAMES[NFILT] = {"LOWPASS", "HIGHPASS", "BANDPASS", "BAND UNITY", "NOTCH", "PEAK",
                                              "LOWPASS 2"};
static const char* const FILT_INFO[NFILT] = {"SVF 12 DB", "SVF 12 DB", "SVF", "SVF", "SVF", "SVF", "2-POLE"};

/** The labels of the knobs whose job changes with the voice (FM, FM FREQ, CLICK, P.AMT, P.DEC). */
static const char* voiceLabel(int voice, int e) {
	switch (voice) {
	case JKS_SNARE:
		switch (e) { case E_FM: return "NOISE"; case E_FMFREQ: return "N.FREQ"; case E_CLICK: return "REPEAT"; default: break; }
		break;
	case JKS_CYM:
		switch (e) { case E_CLICK: return "REPEAT"; case E_PAMT: return "FM 2"; case E_PDEC: return "FREQ 2"; default: break; }
		break;
	case JKS_HAT:
		switch (e) { case E_PAMT: return "FM 2"; case E_PDEC: return "FREQ 2"; default: break; }
		break;
	default:
		break;
	}
	switch (e) {
	case E_FM: return "FM";
	case E_FMFREQ: return "FM FREQ";
	case E_CLICK: return "CLICK";
	case E_PAMT: return "P.AMT";
	case E_PDEC: return "P.DEC";
	default: return "";
	}
}

// ------------------------------------------------------------------------------------------------ the effects
enum Fx { FX_DELAY, FX_PING, FX_REVERB, FX_TUBE, FX_FOLD, FX_RAT, FX_RING, FX_CRUSH, FX_DJ, NFX };
static const char* const FX_NAMES[NFX] = {"DELAY", "PING PONG", "REVERB", "TUBE", "FOLD", "RAT", "RING MOD",
                                          "BIT CRUSH", "DJ FILTER"};
static const char* const FX_INFO[NFX] = {"TEMPO", "TEMPO", "LXR", "DRIVE", "DRIVE", "DRIVE", "LXR", "LO-FI", "LP · HP"};
static const char* const FX_LABELS[NFX][4] = {
	{"TIME", "FDBK", "TONE", "RANGE"}, {"TIME", "FDBK", "TONE", "RANGE"}, {"SIZE", "DECAY", "TONE", "EARLY"},
	{"DRIVE", "COLOR", "TONE", "LEVEL"}, {"DRIVE", "OFFSET", "TONE", "LEVEL"}, {"DRIVE", "COLOR", "TONE", "LEVEL"},
	{"FREQ", "-", "-", "WAVE"},          {"BITS", "RATE", "TONE", "-"},       {"FREQ", "RES", "-", "-"}};
static const int NCV = 5;  // the CV's target: MIX, P1..P4
static const int NDIV = 9;
static const char* const DIV_NAMES[NDIV] = {"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4"};
static const float DIV_BEATS[NDIV] = {0.125f, 1.f / 6.f, 0.25f, 1.f / 3.f, 0.375f, 0.5f, 2.f / 3.f, 0.75f, 1.f};

/** The delay's longest time at 44.1 kHz (Delay.h HI_MAX, half for the ping-pong), its shortest on the long range. */
static float delayMax(int fx) { return fx == FX_PING ? 32767.f * 0.48f : 32767.f; }
static const float DELAY_HI_MIN = 0.02f * 44140.f;

/** The division the TIME knob picks while a clock runs: the longest that still fits. */
static int divOf(float p, float bpm, int fx) {
	int d = clamp((int) std::floor(p * NDIV), 0, NDIV - 1);
	while (d > 0 && DIV_BEATS[d] * 60.f / bpm * 44100.f > delayMax(fx))
		d--;
	return d;
}

// what KESSEL tells its expander, every sample (the expander's message buffers)
struct FxMsg {
	float l = 0.f, r = 0.f;  // the effect's return (V)
	float bpm = 0.f;         // the clock's tempo (0: none)
	int64_t frame = 0;
};


// ------------------------------------------------------------------------------------------------ the module
struct Kessel : Module, follow::Host {
	enum ParamId {
		ENUMS(EDIT_PARAM, NE),
		ENUMS(K_PARAM, 7),  // TUNE DECAY TONE DRIVE ACCENT FX VOLUME
		PARAMS_LEN
	};
	enum Macro { M_TUNE, M_DECAY, M_TONE, M_DRIVE, M_ACC, M_FX, M_VOL };
	enum InputId { DRUMS_INPUT, VEL_INPUT, ACC_INPUT, CLOCK_INPUT, RESET_INPUT, KTUNE_INPUT, KDECAY_INPUT, KTONE_INPUT,
	               KDRIVE_INPUT, KFX_INPUT, KVOL_INPUT, INPUTS_LEN };
	enum OutputId { ENUMS(LANE_OUTPUT, NOUT), L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	KesselCore core;
	std::string status;

	// the kit (engine thread; the UI reads it for the display)
	uint8_t snd[JKS_NVOICE][JKS_NSLOT] = {};
	uint8_t send[JKS_NVOICE] = {};
	float gain[JKS_NVOICE] = {1, 1, 1, 1, 1, 1};
	int kit = 0;
	int lane = 0;
	bool fromPatch = false;

	// presets
	genra_states::Bank bank;
	std::atomic<int> loadSerial{0}, loadedPos{-1};
	std::atomic<int> kitReq{-1};
	std::atomic<bool> edited{false};

	// from the UI thread
	std::atomic<int> laneReq{-1};
	std::atomic<uint32_t> padHits{0};

	// for the UI thread
	std::atomic<int> shownLane{0};
	std::array<std::atomic<float>, JKS_NVOICE> env;
	std::array<std::atomic<float>, 64> scope;  // the edited voice, the last 64 blocks (peaks)
	std::atomic<int> scopePos{0};
	std::atomic<float> bpmShown{0.f};

	// engine thread
	int sent[JKS_NVOICE][JKS_NSLOT];
	int edShown[NE];
	int edLane = -1;
	dsp::SchmittTrigger gtrig[8], clockTrig, resetTrig;
	bool padDown[NL] = {};
	int pending[NL] = {};    // hits waiting for the next block (velocity)
	bool pendingOpen = false;
	int hatOutLane = 4;       // the hat's output: HAT CL or HAT OP (the one struck last)
	double sinceClock = 1e9, clockPeriod = 0.0;
	float bpm = 0.f;
	float envF[JKS_NVOICE] = {};

	// rate conversion: the core's 44.1 kHz frames out, the expander's audio in
	static const int NCH = NOUT + 4;  // lanes, mix L R, return L R
	dsp::SampleRateConverter<NCH> src;
	dsp::DoubleRingBuffer<dsp::Frame<NCH>, 4096> outBuf;
	dsp::SampleRateConverter<2> inSrc;
	dsp::DoubleRingBuffer<dsp::Frame<2>, 1024> inPending;  // the expander's IN at Rack's rate
	dsp::DoubleRingBuffer<dsp::Frame<2>, 4096> inBuf;      // … at 44.1 kHz
	float block[JKS_NVOICE * JKS_BLOCK];
	float busIn[JKS_BLOCK * 2], busOut[JKS_BLOCK * 2];

	// the bit crusher and the DJ filter (KESSEL's own, at 44.1 kHz)
	float crushHold[2] = {}, crushCnt = 0.f, crushLp[2] = {};
	float svfIc1[2] = {}, svfIc2[2] = {};

	Kessel() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		srcName = "KESSEL";
		audioL = L_OUTPUT;
		audioR = R_OUTPUT;
		fol.bind(DRUMS_INPUT, follow::DRUMS);
		fol.bind(CLOCK_INPUT, follow::CLOCK);
		fol.bind(RESET_INPUT, follow::RESET);
		fol.drumsOnly = true;
		for (int e = 0; e < NE; e++) {
			if (e == E_WAVE)
				configSwitch(EDIT_PARAM + e, 0.f, NWAVE - 1, 0.f, "Oscillator", {"Sine", "Triangle", "Saw", "Square", "Noise", "PWM"});
			else if (e == E_FTYPE)
				configSwitch(EDIT_PARAM + e, 0.f, NFILT - 1, 0.f, "Filter type",
				             {"Lowpass", "Highpass", "Bandpass", "Band unity", "Notch", "Peak", "Lowpass 2-pole"});
			else
				configParam<EditQuantity>(EDIT_PARAM + e, 0.f, 127.f, 64.f, E_NAMES[e])->snapEnabled = true;
			paramQuantities[EDIT_PARAM + e]->randomizeEnabled = false;
		}
		configParam<MacroQuantity>(K_PARAM + M_TUNE, 0.f, 127.f, 64.f, "Kit tune", " st")->snapEnabled = true;
		configParam<MacroQuantity>(K_PARAM + M_DECAY, 0.f, 127.f, 64.f, "Kit decay")->snapEnabled = true;
		configParam<MacroQuantity>(K_PARAM + M_TONE, 0.f, 127.f, 64.f, "Kit tone (every voice's cutoff)")->snapEnabled = true;
		configParam<MacroQuantity>(K_PARAM + M_DRIVE, 0.f, 127.f, 64.f, "Kit drive")->snapEnabled = true;
		configParam(K_PARAM + M_ACC, 0.f, 127.f, 64.f, "Accent (how much louder the hits with ACC are)")->snapEnabled = true;
		configParam<MacroQuantity>(K_PARAM + M_FX, 0.f, 127.f, 64.f, "FX sends (all)")->snapEnabled = true;
		configParam(K_PARAM + M_VOL, 0.f, 127.f, 100.f, "Volume")->snapEnabled = true;
		configInput(DRUMS_INPUT, "Drums from GENRA (polyphonic, GENRA's order: kick, snare, clap, rim, closed hat, open hat, "
		                         "tom, perc; height = velocity; the clap plays the snare)");
		configInput(VEL_INPUT, "Velocity (polyphonic, one channel per DRUMS channel; 10 V full)");
		configInput(ACC_INPUT, "Accent (high: the hit at full velocity, the others softer by ACCENT)");
		configInput(CLOCK_INPUT, "Clock (16ths, as GENRA's: the tempo for KESSEL FX's delay)");
		configInput(RESET_INPUT, "Reset");
		configInput(KTUNE_INPUT, "Kit tune CV (1 V/oct)");
		configInput(KDECAY_INPUT, "Kit decay CV (±5 V: ± half the range)");
		configInput(KTONE_INPUT, "Kit tone CV (±5 V: ± half the range)");
		configInput(KDRIVE_INPUT, "Kit drive CV (±5 V: ± half the range)");
		configInput(KFX_INPUT, "FX sends CV (±5 V: ± half the range)");
		configInput(KVOL_INPUT, "Volume CV (10 V: full)");
		for (int o = 0; o < NOUT; o++)
			configOutput(LANE_OUTPUT + o, std::string(LANE_NAMES[OUT_LANE[o]]) + " alone (dry, mono)");
		configOutput(L_OUTPUT, "Mix left (mono when Right is not patched)");
		configOutput(R_OUTPUT, "Mix right");
		for (auto& v : env)
			v = 0.f;
		for (auto& v : scope)
			v = 0.f;
		src.setQuality(6);
		inSrc.setQuality(6);
		forget();
		// the kits: the factory's by style, the user's as saved module states (StateBank)
		genra_states::Factory f;
		f.count = kessel_kits::NUM;
		f.style = [](int i) { return kessel_kits::KITS[i].style; };
		f.name = [](int i) { return kessel_kits::KITS[i].name; };
		f.info = [](int) { return ""; };
		bank.init(this, f, asset::user("Genra/KesselPresets"), &loadSerial, &loadedPos,
		          [this](int i) { kitReq = clamp(i, 0, kessel_kits::NUM - 1); });
		bank.preset = 0;
		bank.presetName = bank.nameOf(0);
	}

	struct EditQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Kessel* m = dynamic_cast<Kessel*>(module);
			int e = paramId - EDIT_PARAM;
			int v = (int) std::round(getValue());
			if (e == E_TUNE) {
				static const char* N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
				return string::f("%s%d", N[v % 12], v / 12 - 1);
			}
			if (e == E_PAN)
				return v == 63 || v == 64 ? "C" : (v < 64 ? string::f("L%d", 64 - v) : string::f("R%d", v - 63));
			(void) m;
			return string::f("%d", v);
		}
		std::string getLabel() override {
			Kessel* m = dynamic_cast<Kessel*>(module);
			int e = paramId - EDIT_PARAM;
			int l = m ? m->shownLane.load() : 0;
			int v = LANE_VOICE[l] < 0 ? JKS_DRUM1 : LANE_VOICE[l];
			std::string n = (e == E_FM || e == E_FMFREQ || e == E_CLICK || e == E_PAMT || e == E_PDEC) ? voiceLabel(v, e)
			                                                                                             : E_NAMES[e];
			return std::string(LANE_NAMES[l]) + " " + n;
		}
	};
	struct MacroQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			int v = (int) std::round(getValue()) - 64;
			if (paramId == K_PARAM + M_TUNE)
				return string::f("%+d", (int) std::lround(v * 24.f / 64.f));
			return string::f("%+d", v);
		}
	};

	void forget() {
		for (auto& r : sent)
			for (int& s : r)
				s = INT32_MIN;
		edLane = -1;
	}

	// ---------------------------------------------------------------------------------- power, kits
	void powerOn() {
		if (!core.load()) {
			status = core.error;
			WARN("KESSEL: %s", status.c_str());
			return;
		}
		status.clear();
		core.api->init(random::u32() | 1u);
		core.api->load_init();
		forget();
		if (!fromPatch)
			applyKit(kit);
	}

	/** Factory kit i: its voices, sends and gains; the macros back to the middle. */
	void applyKit(int i) {
		kit = clamp(i, 0, kessel_kits::NUM - 1);
		const kessel_kits::Kit& k = kessel_kits::KITS[kit];
		for (int v = 0; v < JKS_NVOICE; v++) {
			for (int s = 0; s < JKS_NSLOT && s < kessel_kits::NSLOT; s++)
				snd[v][s] = k.v[v][s];
			send[v] = k.send[v];
			gain[v] = k.gain[v];
		}
		for (int m = 0; m < 7; m++)
			params[K_PARAM + m].setValue(paramQuantities[K_PARAM + m]->getDefaultValue());
		forget();
		edited = false;
		loadedPos = kit;
		loadSerial++;
	}

	void onAdd(const AddEvent& e) override { powerOn(); }
	void onRemove(const RemoveEvent& e) override {
		follow::gone(this);
		core.unload();
	}
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		fromPatch = false;
		lane = 0;
		kitReq = 0;
	}
	void onPortChange(const PortChangeEvent& e) override { fol.portChange(e); }

	// kits the followed GENRA may switch (NEW picks one for the genre)
	int hostPresetCount() override { return kessel_kits::NUM; }
	void hostPresetInfo(int i, char* name, int nl, char* style, int sl) override {
		bool ok = i >= 0 && i < kessel_kits::NUM;
		follow::putStr(name, nl, ok ? kessel_kits::KITS[i].name : "");
		follow::putStr(style, sl, ok ? kessel_kits::KITS[i].style : "");
	}
	int hostPresetCurrent() override { return loadedPos.load(); }
	void hostPresetLoad(int i) override { kitReq = i; }

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "kit", json_integer(kit));
		json_object_set_new(r, "lane", json_integer(lane));
		json_object_set_new(r, "edited", json_boolean(edited.load()));
		json_t* a = json_array();
		for (int v = 0; v < JKS_NVOICE; v++) {
			json_t* row = json_array();
			for (int s = 0; s < JKS_NSLOT; s++)
				json_array_append_new(row, json_integer(snd[v][s]));
			json_array_append_new(a, row);
		}
		json_object_set_new(r, "voices", a);
		json_t* sa = json_array();
		json_t* ga = json_array();
		for (int v = 0; v < JKS_NVOICE; v++) {
			json_array_append_new(sa, json_integer(send[v]));
			json_array_append_new(ga, json_real(gain[v]));
		}
		json_object_set_new(r, "sends", sa);
		json_object_set_new(r, "gains", ga);
		bank.toJson(r);
		fol.toJson(r);
		return r;
	}
	void dataFromJson(json_t* r) override {
		fol.fromJson(r);
		if (json_t* j = json_object_get(r, "kit"))
			kit = clamp((int) json_integer_value(j), 0, kessel_kits::NUM - 1);
		if (json_t* j = json_object_get(r, "lane"))
			lane = clamp((int) json_integer_value(j), 0, NL - 1);
		if (LANE_VOICE[lane] < 0)
			lane = 0;
		edited = json_is_true(json_object_get(r, "edited"));
		json_t* a = json_object_get(r, "voices");
		if (json_is_array(a)) {
			for (int v = 0; v < JKS_NVOICE && v < (int) json_array_size(a); v++) {
				json_t* row = json_array_get(a, v);
				for (int s = 0; s < JKS_NSLOT && s < (int) json_array_size(row); s++)
					snd[v][s] = (uint8_t) clamp((int) json_integer_value(json_array_get(row, s)), 0, 127);
			}
			fromPatch = true;
		}
		json_t* sa = json_object_get(r, "sends");
		json_t* ga = json_object_get(r, "gains");
		for (int v = 0; v < JKS_NVOICE; v++) {
			if (json_is_array(sa) && v < (int) json_array_size(sa))
				send[v] = (uint8_t) clamp((int) json_integer_value(json_array_get(sa, v)), 0, 127);
			if (json_is_array(ga) && v < (int) json_array_size(ga))
				gain[v] = clamp((float) json_number_value(json_array_get(ga, v)), 0.25f, 4.f);
		}
		forget();  // (the knobs show the lane, the core gets every value, in the next block)
		bank.fromJson(r, kit);
	}

	// ---------------------------------------------------------------------------------- the expander
	Module* fxModule() {
		Module* m = rightExpander.module;
		return (m && m->model == modelKesselFx) ? m : nullptr;
	}

	// ---------------------------------------------------------------------------------- the engine
	float cv(int input, float scale) {
		return inputs[input].isConnected() ? inputs[input].getVoltage() * scale : 0.f;
	}

	/** The lane edited: its values into the knobs when it changes, the knobs into the kit when they move. */
	void processEdit() {
		int l = laneReq.exchange(-1);
		if (l >= 0 && l < NL && LANE_VOICE[l] >= 0)
			lane = l;
		int v = LANE_VOICE[lane];
		auto slotOf = [&](int e) { return (e == E_DEC && lane == 5) ? JKS_S_DECOPEN : E_SLOT[e]; };
		if (edLane != lane) {
			edLane = lane;
			for (int e = 0; e < NE; e++) {
				int s = slotOf(e);
				edShown[e] = s < 0 ? send[v] : snd[v][s];
				params[EDIT_PARAM + e].setValue((float) edShown[e]);
			}
		}
		for (int e = 0; e < NE; e++) {
			int x = (int) std::round(params[EDIT_PARAM + e].getValue());
			if (x != edShown[e]) {
				edShown[e] = x;
				int s = slotOf(e);
				if (s < 0)
					send[v] = (uint8_t) clamp(x, 0, 127);
				else
					snd[v][s] = (uint8_t) clamp(x, 0, 127);
				edited = true;
			}
		}
		shownLane = lane;
	}

	/** The kit plus the macros (and their CV) into the core: only what changed. */
	void pushSound() {
		const jks_api* a = core.api;
		int tune = (int) std::lround((params[K_PARAM + M_TUNE].getValue() - 64.f) * 24.f / 64.f + cv(KTUNE_INPUT, 12.f));
		int dec = (int) std::lround(params[K_PARAM + M_DECAY].getValue() - 64.f + cv(KDECAY_INPUT, 12.7f));
		int tone = (int) std::lround(params[K_PARAM + M_TONE].getValue() - 64.f + cv(KTONE_INPUT, 12.7f));
		int drive = (int) std::lround(params[K_PARAM + M_DRIVE].getValue() - 64.f + cv(KDRIVE_INPUT, 12.7f));
		for (int v = 0; v < JKS_NVOICE; v++)
			for (int s = 0; s < JKS_NSLOT; s++) {
				int x = snd[v][s];
				switch (s) {
				case JKS_S_TUNE: x += tune; break;
				case JKS_S_DEC:
				case JKS_S_DECOPEN: x += dec; break;
				case JKS_S_CUT: x += tone; break;
				case JKS_S_DRIVE: x += drive; break;
				default: break;
				}
				if (s == JKS_S_WAVE)
					x = clamp(x, 0, NWAVE - 1);
				else if (s == JKS_S_FTYPE)
					x = clamp(x, 0, NFILT - 1);
				else
					x = clamp(x, 0, 127);
				if (x != sent[v][s]) {
					sent[v][s] = x;
					a->set_slot((uint32_t) v, (uint32_t) s, (uint32_t) x);
				}
			}
	}

	int accent(int vel, int ch) {
		if (!inputs[ACC_INPUT].isConnected())
			return vel;
		bool acc = inputs[ACC_INPUT].getPolyVoltage(ch) >= 1.f;
		if (acc)
			return 127;
		float soft = 1.f - 0.6f * params[K_PARAM + M_ACC].getValue() / 127.f;
		return clamp((int) std::lround(vel * soft), 1, 127);
	}

	void hit(int l, int vel) {
		if (l < 0 || l >= NL || LANE_VOICE[l] < 0)
			return;
		pending[l] = std::max(pending[l], clamp(vel, 1, 127));
	}

	void processHits() {
		uint32_t pads = padHits.exchange(0);
		for (int l = 0; l < NL; l++)
			if ((pads >> l) & 1u)
				hit(l, accent(100, 0));
		if (inputs[DRUMS_INPUT].isConnected()) {
			int n = std::min(8, inputs[DRUMS_INPUT].getChannels());
			bool velIn = inputs[VEL_INPUT].isConnected();
			for (int c = 0; c < n; c++) {
				float g = inputs[DRUMS_INPUT].getPolyVoltage(c);
				if (gtrig[c].process(g, 0.1f, 0.5f)) {
					int vel = velIn ? clamp((int) std::lround(inputs[VEL_INPUT].getPolyVoltage(c) * 12.7f), 1, 127)
					                : clamp((int) std::lround(g * 12.7f), 1, 127);
					hit(GENRA_LANE[c], accent(vel, c));
				}
			}
		}
	}

	void processClock(float sampleRate) {
		sinceClock += 1.0;
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f))
			sinceClock = 1e9;
		bool ext = inputs[CLOCK_INPUT].isConnected();
		float cl = inputs[CLOCK_INPUT].getVoltage();
		Module* fx = fxModule();
		if (!ext && fx && fx->inputs[1].isConnected()) {  // KESSEL FX's own CLOCK (KesselFx::CLOCK_INPUT = 1)
			ext = true;
			cl = fx->inputs[1].getVoltage();
		}
		if (ext && clockTrig.process(cl, 0.1f, 1.f)) {
			if (sinceClock < sampleRate * 2.0) {
				double p = sinceClock;
				clockPeriod = clockPeriod > 0.0 ? clockPeriod + 0.5 * (p - clockPeriod) : p;
			}
			sinceClock = 0.0;
		}
		bool alive = ext && clockPeriod > 0.0 && sinceClock < std::max(clockPeriod * 3.0, (double) sampleRate * 0.5);
		bpm = alive ? clamp((float) (60.0 * sampleRate / (clockPeriod * 4.0)), 30.f, 300.f) : 0.f;
		bpmShown = bpm;
	}

	/** One block of the core: the hits, the voices, the mix, the effect bus; into the output buffer. */
	void renderBlock(float sampleRate) {
		const jks_api* a = core.api;
		pushSound();
		for (int l = 0; l < NL; l++) {
			if (!pending[l])
				continue;
			int v = LANE_VOICE[l];
			a->trigger((uint32_t) v, (uint32_t) pending[l], l == 5 ? 1u : 0u);
			if (v == JKS_HAT)
				hatOutLane = l;
			pending[l] = 0;
		}
		a->render(block);

		// the expander: on, its effect and knobs (read here, at the block)
		Module* fx = fxModule();
		bool fxOn = fx && fx->params[KFX_ON].getValue() > 0.5f;
		bool fxOwnOut = fx && (fx->outputs[0].isConnected() || fx->outputs[1].isConnected());
		float sendScale = clamp(params[K_PARAM + M_FX].getValue() / 64.f + cv(KFX_INPUT, 0.2f), 0.f, 2.f);
		float vol = params[K_PARAM + M_VOL].getValue() / 100.f;
		if (inputs[KVOL_INPUT].isConnected())
			vol *= clamp(inputs[KVOL_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		vol *= vol;

		float pans[JKS_NVOICE][2], sends[JKS_NVOICE];
		for (int v = 0; v < JKS_NVOICE; v++) {
			float p = clamp((float) a->pan((uint32_t) v), 0.f, 127.f) / 127.f;
			pans[v][0] = std::sqrt(1.f - p);
			pans[v][1] = std::sqrt(p);
			sends[v] = fxOn ? clamp(send[v] / 127.f * sendScale, 0.f, 1.f) : 0.f;
		}

		// the effect bus input: the sends, and the expander's IN (resampled to 44.1 kHz)
		dsp::Frame<NCH> frames[JKS_BLOCK];
		for (int i = 0; i < JKS_BLOCK; i++) {
			dsp::Frame<2> ext{};
			if (!inBuf.empty())
				ext = inBuf.shift();
			busIn[2 * i] = ext.samples[0];
			busIn[2 * i + 1] = ext.samples[1];
		}
		for (int v = 0; v < JKS_NVOICE; v++) {
			float pk = 0.f;
			for (int i = 0; i < JKS_BLOCK; i++) {
				float x = block[v * JKS_BLOCK + i] * gain[v];
				block[v * JKS_BLOCK + i] = x;
				pk = std::max(pk, std::fabs(x));
				busIn[2 * i] += x * pans[v][0] * sends[v];
				busIn[2 * i + 1] += x * pans[v][1] * sends[v];
			}
			envF[v] = std::max(pk, envF[v] * 0.86f);
			env[v] = envF[v];
		}
		{
			int ev = LANE_VOICE[lane];
			int p = scopePos.load();
			scope[p] = envF[ev];
			scopePos = (p + 1) % 64;
		}
		if (fxOn)
			processFx(fx);
		else
			std::memset(busOut, 0, sizeof busOut);
		float level = fx ? fx->params[KFX_LEVEL].getValue() : 0.f;

		for (int i = 0; i < JKS_BLOCK; i++) {
			dsp::Frame<NCH>& f = frames[i];
			float l = 0.f, r = 0.f;
			for (int o = 0; o < NOUT; o++)
				f.samples[o] = 0.f;
			for (int v = 0; v < JKS_NVOICE; v++) {
				float x = block[v * JKS_BLOCK + i];
				float dry = 1.f - sends[v];
				l += x * pans[v][0] * dry;
				r += x * pans[v][1] * dry;
			}
			// the lane outputs: each voice alone (the hi-hat on the one struck last)
			f.samples[0] = block[JKS_DRUM1 * JKS_BLOCK + i];
			f.samples[1] = block[JKS_SNARE * JKS_BLOCK + i];
			f.samples[2] = block[JKS_DRUM3 * JKS_BLOCK + i];
			f.samples[hatOutLane == 5 ? 4 : 3] = block[JKS_HAT * JKS_BLOCK + i];
			f.samples[5] = block[JKS_DRUM2 * JKS_BLOCK + i];
			f.samples[6] = block[JKS_CYM * JKS_BLOCK + i];
			float rl = busOut[2 * i] * level, rr = busOut[2 * i + 1] * level;
			if (fxOn && !fxOwnOut) {
				l += rl;
				r += rr;
			}
			f.samples[NOUT] = l * vol;
			f.samples[NOUT + 1] = r * vol;
			f.samples[NOUT + 2] = rl * vol;
			f.samples[NOUT + 3] = rr * vol;
		}
		int out = 0, inLen = JKS_BLOCK;
		int outLen = (int) outBuf.capacity();
		src.setRates(44100, (int) sampleRate);
		src.setChannels(NCH);
		src.process(frames, &inLen, outBuf.endData(), &outLen);
		outBuf.endIncr(outLen);
		(void) out;
	}

	// the expander's parameters (KesselFx's ids, kept here so both read the same)
	enum { KFX_FX, KFX_MIX, KFX_P1, KFX_P2, KFX_P3, KFX_P4, KFX_ON, KFX_CVT, KFX_DEPTH, KFX_LEVEL };

	/** The expander's knobs, with its CV on the target it picks (0..1). */
	void fxKnobs(Module* fx, float* mix, float* p) {
		float cvv = fx->inputs[0].isConnected() ? fx->inputs[0].getVoltage() / 5.f * fx->params[KFX_DEPTH].getValue() : 0.f;
		int t = clamp((int) std::round(fx->params[KFX_CVT].getValue()), 0, NCV - 1);
		*mix = clamp(fx->params[KFX_MIX].getValue() + (t == 0 ? cvv * 0.5f : 0.f), 0.f, 1.f);
		for (int k = 0; k < 4; k++)
			p[k] = clamp(fx->params[KFX_P1 + k].getValue() + (t == k + 1 ? cvv * 0.5f : 0.f), 0.f, 1.f);
	}

	void processFx(Module* fx) {
		const jks_api* a = core.api;
		int type = clamp((int) std::round(fx->params[KFX_FX].getValue()), 0, NFX - 1);
		float mix, p[4];
		fxKnobs(fx, &mix, p);
		float lp[4] = {0.5f, 0.5f, 0.5f, 0.5f};
		switch (type) {
		case FX_DELAY:
		case FX_PING: {
			lp[0] = p[0], lp[3] = p[1] * 0.98f, lp[2] = p[2], lp[1] = p[3];
			if (bpm > 0.f) {  // a clock runs: TIME picks a division
				float n = DIV_BEATS[divOf(p[0], bpm, type)] * 60.f / bpm * 44100.f;
				lp[1] = 1.f;
				lp[0] = clamp((n - DELAY_HI_MIN) / (delayMax(type) - DELAY_HI_MIN), 0.f, 1.f);
			}
			a->fx_set(JKS_FX_DELAY, type == FX_PING ? 1u : 0u, lp, mix);
			a->fx_process(busIn, busOut);
			break;
		}
		case FX_REVERB:
			lp[0] = p[0], lp[1] = p[1] * 0.97f, lp[2] = p[2], lp[3] = p[3];
			a->fx_set(JKS_FX_REVERB, 0, lp, mix);
			a->fx_process(busIn, busOut);
			break;
		case FX_TUBE:
		case FX_FOLD:
		case FX_RAT:
			lp[0] = p[0], lp[1] = p[1], lp[2] = p[2], lp[3] = p[3];
			a->fx_set(JKS_FX_DIST, (uint32_t) (type - FX_TUBE), lp, mix);
			a->fx_process(busIn, busOut);
			break;
		case FX_RING:
			lp[0] = p[0];
			a->fx_set(JKS_FX_RING, (uint32_t) clamp((int) (p[3] * 5.99f), 0, 5), lp, mix);
			a->fx_process(busIn, busOut);
			break;
		case FX_CRUSH: {
			float bits = 16.f - p[0] * 14.f;
			float q = std::pow(2.f, bits - 1.f);
			float hold = 1.f + p[1] * p[1] * 31.f;
			float c = 0.05f + 0.95f * (1.f - p[2]) * (1.f - p[2]);
			for (int i = 0; i < JKS_BLOCK; i++) {
				crushCnt += 1.f;
				if (crushCnt >= hold) {
					crushCnt -= hold;
					for (int ch = 0; ch < 2; ch++)
						crushHold[ch] = std::round(busIn[2 * i + ch] * q) / q;
				}
				for (int ch = 0; ch < 2; ch++) {
					crushLp[ch] += c * (crushHold[ch] - crushLp[ch]);
					float x = busIn[2 * i + ch];
					busOut[2 * i + ch] = x + (crushLp[ch] - x) * mix;
				}
			}
			break;
		}
		case FX_DJ: {
			// a state-variable filter (TPT): below the middle a lowpass going down, above a highpass going up
			float d = p[0] - 0.5f;
			bool hp = d > 0.f;
			float amt = std::fabs(d) * 2.f;
			float fc = hp ? 20.f * std::pow(400.f, amt) : 20000.f * std::pow(0.005f, amt);
			float g = std::tan(M_PI * std::min(fc, 20000.f) / 44100.f);
			float k = 2.f - 1.9f * p[1];
			float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
			for (int i = 0; i < JKS_BLOCK; i++)
				for (int ch = 0; ch < 2; ch++) {
					float x = busIn[2 * i + ch];
					float v3 = x - svfIc2[ch];
					float v1 = a1 * svfIc1[ch] + a2 * v3;
					float v2 = svfIc2[ch] + a2 * svfIc1[ch] + a3 * v3;
					svfIc1[ch] = 2.f * v1 - svfIc1[ch];
					svfIc2[ch] = 2.f * v2 - svfIc2[ch];
					float y = hp ? x - k * v1 - v2 : v2;
					if (amt < 0.02f)
						y = x;
					busOut[2 * i + ch] = x + (y - x) * mix;
				}
			break;
		}
		default:
			std::memcpy(busOut, busIn, sizeof busOut);
			break;
		}
	}

	void process(const ProcessArgs& args) override {
		fol.process(this, args.frame);
		const jks_api* a = core.api;
		if (!a) {
			for (int o = 0; o < OUTPUTS_LEN; o++)
				outputs[o].setVoltage(0.f);
			return;
		}
		int req = kitReq.exchange(-1);
		if (req >= 0)
			applyKit(req);
		processEdit();
		processHits();
		processClock(args.sampleRate);

		// the expander's IN, to 44.1 kHz
		Module* fx = fxModule();
		{
			dsp::Frame<2> in{};
			if (fx) {
				in.samples[0] = fx->inputs[2].getVoltage() / 5.f;
				in.samples[1] = fx->inputs[3].isConnected() ? fx->inputs[3].getVoltage() / 5.f : in.samples[0];
			}
			if (!inPending.full())
				inPending.push(in);
			if (inPending.size() >= 32) {
				int inLen = (int) inPending.size(), outLen = (int) inBuf.capacity();
				inSrc.setRates((int) args.sampleRate, 44100);
				inSrc.process(inPending.startData(), &inLen, inBuf.endData(), &outLen);
				inPending.startIncr(inLen);
				inBuf.endIncr(outLen);
			}
		}

		int guard = 0;
		while (outBuf.empty() && guard++ < 8)
			renderBlock(args.sampleRate);
		dsp::Frame<NCH> f{};
		if (!outBuf.empty())
			f = outBuf.shift();
		const float V = 5.f;
		for (int o = 0; o < NOUT; o++)
			outputs[LANE_OUTPUT + o].setVoltage(genra_out::limit(f.samples[o] * V));
		float l = genra_out::limit(f.samples[NOUT] * V), r = genra_out::limit(f.samples[NOUT + 1] * V);
		if (outputs[R_OUTPUT].isConnected() || pulled(args.frame)) {
			outputs[L_OUTPUT].setVoltage(l);
			outputs[R_OUTPUT].setVoltage(r);
		} else {
			outputs[L_OUTPUT].setVoltage(0.5f * (l + r));
			outputs[R_OUTPUT].setVoltage(0.f);
		}
		if (fx) {
			FxMsg* m = (FxMsg*) fx->leftExpander.producerMessage;
			if (m) {
				m->l = genra_out::limit(f.samples[NOUT + 2] * V);
				m->r = genra_out::limit(f.samples[NOUT + 3] * V);
				m->bpm = bpm;
				m->frame = args.frame;
				fx->leftExpander.requestMessageFlip();
			}
		}
	}
};

// ------------------------------------------------------------------------------------------------ KESSEL FX
struct KesselFx : Module {
	enum ParamId { FX_PARAM, MIX_PARAM, ENUMS(P_PARAM, 4), ON_PARAM, CVT_PARAM, DEPTH_PARAM, LEVEL_PARAM, PARAMS_LEN };
	enum InputId { CV_INPUT, CLOCK_INPUT, L_INPUT, R_INPUT, INPUTS_LEN };
	enum OutputId { L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { ON_LIGHT, LIGHTS_LEN };
	FxMsg msg[2];
	std::atomic<float> bpmShown{0.f};
	std::atomic<bool> docked{false};

	KesselFx() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configSwitch(FX_PARAM, 0.f, NFX - 1, 0.f, "Effect",
		             {"Delay", "Ping pong", "Reverb", "Tube", "Fold", "Rat", "Ring mod", "Bit crush", "DJ filter"});
		configParam(MIX_PARAM, 0.f, 1.f, 0.5f, "Mix", "%", 0.f, 100.f);
		for (int k = 0; k < 4; k++)
			configParam<PQuantity>(P_PARAM + k, 0.f, 1.f, k == 3 ? 0.7f : 0.5f, string::f("Parameter %d", k + 1), "%", 0.f, 100.f);
		configSwitch(ON_PARAM, 0.f, 1.f, 1.f, "Effect on", {"Off", "On"});
		configSwitch(CVT_PARAM, 0.f, NCV - 1, 1.f, "CV target", {"Mix", "Parameter 1", "Parameter 2", "Parameter 3", "Parameter 4"});
		configParam(DEPTH_PARAM, -1.f, 1.f, 0.5f, "CV depth", "%", 0.f, 100.f);
		configParam(LEVEL_PARAM, 0.f, 1.5f, 1.f, "Return level", "%", 0.f, 100.f);
		configInput(CV_INPUT, "CV (±5 V, onto the target the field picks, by DEPTH)");
		configInput(CLOCK_INPUT, "Clock (16ths; KESSEL's own CLOCK first): the delay in divisions of the tempo");
		configInput(L_INPUT, "In left (into the effect, with KESSEL's sends)");
		configInput(R_INPUT, "In right (normalled to left)");
		configOutput(L_OUTPUT, "Effect return left (patched: no longer in KESSEL's mix)");
		configOutput(R_OUTPUT, "Effect return right");
		leftExpander.producerMessage = &msg[0];
		leftExpander.consumerMessage = &msg[1];
	}
	struct PQuantity : ParamQuantity {
		std::string getLabel() override {
			KesselFx* m = dynamic_cast<KesselFx*>(module);
			int t = m ? clamp((int) std::round(m->params[FX_PARAM].getValue()), 0, NFX - 1) : 0;
			return FX_LABELS[t][paramId - P_PARAM];
		}
	};
	void process(const ProcessArgs& args) override {
		Module* k = leftExpander.module;
		bool d = k && k->model != nullptr && dynamic_cast<Kessel*>(k) != nullptr;
		docked = d;
		lights[ON_LIGHT].setBrightness(params[ON_PARAM].getValue() > 0.5f ? 1.f : 0.f);
		if (!d) {
			outputs[L_OUTPUT].setVoltage(0.f);
			outputs[R_OUTPUT].setVoltage(0.f);
			bpmShown = 0.f;
			return;
		}
		const FxMsg* m = (const FxMsg*) leftExpander.consumerMessage;
		outputs[L_OUTPUT].setVoltage(m->l);
		outputs[R_OUTPUT].setVoltage(outputs[R_OUTPUT].isConnected() ? m->r : 0.f);
		if (!outputs[R_OUTPUT].isConnected())
			outputs[L_OUTPUT].setVoltage(0.5f * (m->l + m->r));
		bpmShown = m->bpm;
	}
};

// ------------------------------------------------------------------------------------------------ the panels
namespace L = kessel;
namespace LF = kesselfx;

static Vec at(const float* p) { return mm2px(Vec(p[0], p[1])); }

/** A text the module switches, drawn from its SVG in the panel's font (tools/kessel_panel.py: res/kessel/), centred on
 * x, its baseline at y (px). */
static void drawLabel(NVGcontext* vg, const std::string& kind, float baseMm, const std::string& t, Vec a) {
	std::string sl;
	for (unsigned char c : t)
		sl += std::isalnum(c) ? (char) std::tolower(c) : '_';
	if (t.find("\xc2\xb7") != std::string::npos) {  // the middle dot is two bytes: one '_' in the file name
		size_t i = sl.find("__");
		if (i != std::string::npos)
			sl.erase(i, 1);
	}
	std::shared_ptr<window::Svg> svg = window::Svg::load(asset::plugin(pluginInstance, "res/kessel/" + kind + "_" + sl + ".svg"));
	if (!svg || !svg->handle)
		return;
	Vec sz = svg->getSize();
	nvgSave(vg);
	nvgTranslate(vg, a.x - sz.x / 2, a.y - mm2px(baseMm));
	window::svgDraw(vg, svg->handle);
	nvgRestore(vg);
}

/** A selection field: the name and a short info, a click opens the list, the wheel steps (undoable). */
struct NameField : app::ParamWidget {
	const char* const* names = nullptr;
	const char* const* info = nullptr;
	int count = 1;
	std::string title, undo;
	std::function<std::string(int)> nameOf;  // (instead of names: dynamic)
	int value() {
		engine::ParamQuantity* pq = getParamQuantity();
		return pq ? clamp((int) std::round(pq->getValue()), 0, count - 1) : 0;
	}
	std::string name(int i) { return nameOf ? nameOf(i) : names[i]; }
	void set(int t) {
		engine::ParamQuantity* pq = getParamQuantity();
		if (!pq)
			return;
		float old = pq->getValue();
		if ((int) std::round(old) == t)
			return;
		pq->setValue(t);
		history::ParamChange* h = new history::ParamChange;
		h->name = undo;
		h->moduleId = module->id;
		h->paramId = paramId;
		h->oldValue = old;
		h->newValue = t;
		APP->history->push(h);
	}
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && module) {
			rack::ui::Menu* menu = createMenu();
			menu->addChild(createMenuLabel(title));
			for (int i = 0; i < count; i++)
				menu->addChild(createCheckMenuItem(name(i), info ? info[i] : "", [=]() { return value() == i; }, [=]() { set(i); }));
			e.consume(this);
			return;
		}
		ParamWidget::onButton(e);
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (module && e.scrollDelta.y != 0.f) {
			set(clamp(value() + (e.scrollDelta.y < 0.f ? 1 : -1), 0, count - 1));
			e.consume(this);
			return;
		}
		ParamWidget::onHoverScroll(e);
	}
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			int t = module ? value() : 0;
			auto f = ui::boldFont();
			float y = box.size.y / 2 + 2.6f;
			ui::text(args.vg, f, mm2px(2.f), y, name(t), 7.6f, ui::TXT, NVG_ALIGN_LEFT, 0.9f);
			if (info)
				ui::text(args.vg, f, box.size.x - mm2px(6.f), y - 0.3f, info[t], 5.6f, ui::DIM, NVG_ALIGN_RIGHT, 0.7f);
		}
		ParamWidget::drawLayer(args, layer);
	}
};

/** The kit field in the header: number, name (a dot when edited), the style; a click opens the list, the wheel steps. */
struct KitField : OpaqueWidget {
	Kessel* module = nullptr;
	void step() override {
		if (module)
			module->bank.poll();
		OpaqueWidget::step();
	}
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && module) {
			module->bank.scan();
			Menu* menu = createMenu();
			menu->addChild(createMenuLabel("Kit"));
			genra_states::appendList(menu, &module->bank);
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (module && e.scrollDelta.y != 0.f) {
			module->bank.load(module->bank.step(e.scrollDelta.y < 0.f ? 1 : -1));
			e.consume(this);
			return;
		}
		OpaqueWidget::onHoverScroll(e);
	}
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			genra_states::Bank* b = module ? &module->bank : nullptr;
			int idx = b ? b->preset : 0;
			int nf = b ? b->f.count : 1;
			std::string name = b ? b->nameOf(idx) : kessel_kits::KITS[0].name;
			std::string style = b ? b->styleOf(idx) : kessel_kits::KITS[0].style;
			std::string num = idx < 0 ? "--" : idx >= nf ? "U" : string::f("%02d", idx + 1);
			float y = box.size.y / 2 + 2.6f;
			auto font = ui::boldFont();
			ui::text(args.vg, font, mm2px(2.f), y, num, 6.4f, ACCENT, NVG_ALIGN_LEFT, 0.8f);
			float w = ui::text(args.vg, font, mm2px(6.6f), y, name, 7.6f, ui::TXT, NVG_ALIGN_LEFT, 0.9f);
			if (module && module->edited.load()) {
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, mm2px(6.6f) + w + mm2px(1.4f), y - 2.6f, mm2px(0.55f));
				nvgFillColor(args.vg, ACCENT);
				nvgFill(args.vg);
			}
			(void) style;
		}
		OpaqueWidget::drawLayer(args, layer);
	}
};

/** A lane's button: picks the lane edited and strikes it; lit by its hit, the edited one filled. */
struct LaneButton : OpaqueWidget {
	Kessel* module = nullptr;
	int l = 0;
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && module) {
			module->laneReq = l;
			module->padHits |= 1u << l;
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		bool sel = module ? module->shownLane.load() == l : l == 0;
		float lit = module ? std::sqrt(clamp((float) module->env[LANE_VOICE[l]], 0.f, 1.f)) : 0.f;
		NVGcolor base = sel ? ACCENT : nvgRGB(0x2b, 0x2d, 0x35);
		NVGcolor fill = sel ? base : nvgLerpRGBA(base, ACCENT, lit * 0.8f);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, mm2px(1.f));
		nvgFillColor(vg, fill);
		nvgFill(vg);
		nvgStrokeColor(vg, sel ? ui::WHITE : ui::EDGE);
		nvgStrokeWidth(vg, sel ? 1.f : 0.8f);
		nvgStroke(vg);
		drawLabel(vg, sel ? "lanesel" : "lane", kessellabels::BASE_LANE, LANE_NAMES[l], Vec(box.size.x / 2, mm2px(4.f)));
	}
};

/** Texts the module draws on the panel: the VOICE title and the knob labels that follow the lane. */
struct KesselText : TransparentWidget {
	Kessel* module = nullptr;
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		int l = module ? module->shownLane.load() : 0;
		int v = LANE_VOICE[l] < 0 ? JKS_DRUM1 : LANE_VOICE[l];
		drawLabel(vg, "title", kessellabels::BASE_TITLE, std::string("VOICE \xc2\xb7 ") + LANE_NAMES[l], at(L::VOICE_TITLE));
		const float* pos[5] = {L::LBL_FM, L::LBL_FMFREQ, L::LBL_CLICK, L::LBL_PAMT, L::LBL_PDEC};
		const int es[5] = {E_FM, E_FMFREQ, E_CLICK, E_PAMT, E_PDEC};
		for (int i = 0; i < 5; i++)
			drawLabel(vg, "knob", kessellabels::BASE_KNOB, voiceLabel(v, es[i]), at(pos[i]));
	}
};

struct Display : Widget {
	Kessel* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		auto bold = ui::boldFont();
		float W = box.size.x;
		if (module && !module->status.empty()) {
			ui::text(vg, bold, mm2px(4.f), mm2px(8.f), module->status, 6.f, ui::DIM, NVG_ALIGN_LEFT, 0.5f);
			return;
		}
		int l = module ? module->shownLane.load() : 0;
		int v = LANE_VOICE[l] < 0 ? JKS_DRUM1 : LANE_VOICE[l];
		static const int ORDER[NL] = {0, 1, 1, 2, 3, 4, 5, 6};
		ui::text(vg, bold, mm2px(4.f), mm2px(4.3f), string::f("VOICE %d / 7", ORDER[l] + 1), 5.6f, ui::DIM, NVG_ALIGN_LEFT, 1.1f);
		ui::text(vg, bold, mm2px(4.f), mm2px(9.9f), LANE_NAMES[l], 15.5f, ui::WHITE, NVG_ALIGN_LEFT, 0.8f);
		int wave = module ? clamp((int) module->snd[v][JKS_S_WAVE], 0, NWAVE - 1) : 0;
		int ft = module ? clamp((int) module->snd[v][JKS_S_FTYPE], 0, NFILT - 1) : 0;
		ui::text(vg, bold, mm2px(4.f), mm2px(12.9f), WAVE_NAMES[wave], 5.6f, ACCENT, NVG_ALIGN_LEFT, 1.2f);

		// the edited voice's level, the last blocks
		float sx = mm2px(44.f), sw = mm2px(36.f), base = mm2px(9.5f), sh = mm2px(6.f);
		nvgBeginPath(vg);
		int p0 = module ? module->scopePos.load() : 0;
		for (int i = 0; i < 64; i++) {
			float x = sx + i * sw / 63.f;
			float a = module ? clamp((float) module->scope[(p0 + i) % 64], 0.f, 1.f)
			                 : std::exp(-i / 10.f) * 0.9f;
			float y = base - std::sqrt(a) * sh;
			if (i == 0)
				nvgMoveTo(vg, x, y);
			else
				nvgLineTo(vg, x, y);
		}
		nvgStrokeColor(vg, ACCENT);
		nvgStrokeWidth(vg, 1.3f);
		nvgLineJoin(vg, NVG_ROUND);
		nvgStroke(vg);
		int dec = module ? (int) module->snd[v][l == 5 ? JKS_S_DECOPEN : JKS_S_DEC] : 0;
		ui::text(vg, bold, sx, mm2px(12.9f), string::f("%s \xc2\xb7 DEC %d", FILT_NAMES[ft], dec), 5.6f, ui::DIM,
		         NVG_ALIGN_LEFT, 1.f);

		float bpm = module ? module->bpmShown.load() : 120.f;
		ui::text(vg, bold, W - mm2px(4.f), mm2px(4.3f), bpm > 0.f ? string::f("%.0f BPM", bpm) : std::string("NO CLOCK"),
		         5.6f, ui::DIM, NVG_ALIGN_RIGHT, 1.1f);
		static const int METER_V[7] = {JKS_DRUM1, JKS_SNARE, JKS_DRUM3, JKS_HAT, JKS_HAT, JKS_DRUM2, JKS_CYM};
		for (int i = 0; i < 7; i++) {
			float bx = W - mm2px(4.f) - (7 - i) * mm2px(3.2f) + mm2px(0.6f);
			float by = mm2px(6.f), bh = mm2px(4.5f), bw = mm2px(2.2f);
			nvgBeginPath(vg);
			nvgRoundedRect(vg, bx, by, bw, bh, 1.f);
			nvgFillColor(vg, nvgRGB(0x1d, 0x1e, 0x24));
			nvgFill(vg);
			float a = module ? std::sqrt(clamp((float) module->env[METER_V[i]], 0.f, 1.f)) : 0.f;
			if (a > 0.02f) {
				nvgBeginPath(vg);
				nvgRoundedRect(vg, bx, by + bh * (1.f - a), bw, bh * a, 1.f);
				nvgFillColor(vg, ACCENT);
				nvgFill(vg);
			}
		}
		ui::text(vg, bold, W - mm2px(4.f), mm2px(12.9f), "7 VOICES", 5.6f, ACCENT, NVG_ALIGN_RIGHT, 1.2f);
		Widget::drawLayer(args, layer);
	}
};

struct KesselWidget : ModuleWidget {
	KesselWidget(Kessel* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Kessel.svg")));
		follow::addStrip(this, module, {at(L::IN_DRUMS), at(L::IN_CLOCK), at(L::IN_RESET)});
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		Display* d = new Display;
		d->module = module;
		d->box.pos = mm2px(Vec(L::DISPLAY[0], L::DISPLAY[1]));
		d->box.size = mm2px(Vec(L::DISPLAY[2], L::DISPLAY[3]));
		addChild(d);
		KitField* kf = createWidget<KitField>(mm2px(Vec(L::PRESETF[0], L::PRESETF[1])));
		kf->box.size = mm2px(Vec(L::PRESETF[2], L::PRESETF[3]));
		kf->module = module;
		addChild(kf);
		for (int l = 0; l < NL; l++) {
			if (LANE_VOICE[l] < 0)
				continue;
			LaneButton* b = createWidget<LaneButton>(mm2px(Vec(L::LANE[l][0], L::LANE[l][1])));
			b->box.size = mm2px(Vec(L::LANE[l][2], L::LANE[l][3]));
			b->module = module;
			b->l = l;
			addChild(b);
		}
		using M = Kessel;
		auto field = [&](const float* r, int id, const char* const* names, const char* const* info, int count,
		                 const char* title, const char* undo) {
			NameField* f = createParam<NameField>(mm2px(Vec(r[0], r[1])), module, id);
			f->box.size = mm2px(Vec(r[2], r[3]));
			f->names = names, f->info = info, f->count = count, f->title = title, f->undo = undo;
			addParam(f);
		};
		field(L::WAVEF, M::EDIT_PARAM + E_WAVE, WAVE_NAMES, WAVE_INFO, NWAVE, "Oscillator", "change oscillator");
		field(L::FTYPEF, M::EDIT_PARAM + E_FTYPE, FILT_NAMES, FILT_INFO, NFILT, "Filter type", "change filter type");
		auto large = [&](const float* p, int id) { addParam(createParamCentered<GLargeKnob>(at(p), module, id)); };
		auto knob = [&](const float* p, int id) { addParam(createParamCentered<GKnob>(at(p), module, id)); };
		auto small = [&](const float* p, int id) { addParam(createParamCentered<GSmallKnob>(at(p), module, id)); };
		large(L::TUNE, M::EDIT_PARAM + E_TUNE);
		knob(L::FM, M::EDIT_PARAM + E_FM);
		knob(L::FMFREQ, M::EDIT_PARAM + E_FMFREQ);
		knob(L::CLICK, M::EDIT_PARAM + E_CLICK);
		small(L::PAMT, M::EDIT_PARAM + E_PAMT);
		small(L::PDEC, M::EDIT_PARAM + E_PDEC);
		small(L::PAN, M::EDIT_PARAM + E_PAN);
		large(L::CUT, M::EDIT_PARAM + E_CUT);
		knob(L::RES, M::EDIT_PARAM + E_RES);
		knob(L::DRIVE, M::EDIT_PARAM + E_DRIVE);
		knob(L::LEVEL, M::EDIT_PARAM + E_LEVEL);
		small(L::ATK, M::EDIT_PARAM + E_ATK);
		small(L::DEC, M::EDIT_PARAM + E_DEC);
		small(L::SEND, M::EDIT_PARAM + E_SEND);
		const float* mk[7] = {L::KTUNE, L::KDECAY, L::KTONE, L::KDRIVE, L::KACC, L::KFX, L::KVOL};
		for (int i = 0; i < 7; i++)
			knob(mk[i], M::K_PARAM + i);
		KesselText* t = new KesselText;
		t->module = module;
		t->box.size = box.size;
		addChild(t);

		const float* ins[M::INPUTS_LEN] = {L::IN_DRUMS, L::IN_VEL, L::IN_ACC, L::IN_CLOCK, L::IN_RESET, L::IN_KTUNE,
		                                   L::IN_KDECAY, L::IN_KTONE, L::IN_KDRIVE, L::IN_KFX, L::IN_KVOL};
		for (int i = 0; i < M::INPUTS_LEN; i++)
			addInput(createInputCentered<PJ301MPort>(at(ins[i]), module, i));
		const float* outs[M::OUTPUTS_LEN] = {L::OUT_LANE0, L::OUT_LANE1, L::OUT_LANE3, L::OUT_LANE4, L::OUT_LANE5,
		                                     L::OUT_LANE6, L::OUT_LANE7, L::OUT_L, L::OUT_R};
		for (int i = 0; i < M::OUTPUTS_LEN; i++)
			addOutput(createOutputCentered<PJ301MPort>(at(outs[i]), module, i));
	}

	void step() override {
		if (module)
			follow::knownHost(module);
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		if (follow::Host* h = follow::host(module))
			h->fol.appendMenu(menu, module);
		Kessel* m = dynamic_cast<Kessel*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		genra_states::appendMenu(menu, &m->bank);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Patch GENRA's drums: its DRUMS (8 triggers), CLOCK, RESET"));
		menu->addChild(createMenuLabel("The clap plays the snare: REPEAT makes the snare a clap"));
		menu->addChild(createMenuLabel("Engine: from the LXR firmware (open source, GPL-3.0) by Erica Synths;"));
		menu->addChild(createMenuLabel("modified for KESSEL, not affiliated with or endorsed by Erica Synths"));
	}
};

// ------------------------------------------------------------------------------------------------ KESSEL FX panel
struct FxText : TransparentWidget {
	KesselFx* module = nullptr;
	void draw(const DrawArgs& args) override {
		int t = module ? clamp((int) std::round(module->params[KesselFx::FX_PARAM].getValue()), 0, NFX - 1) : 0;
		const float* pos[4] = {LF::LBL_P1, LF::LBL_P2, LF::LBL_P3, LF::LBL_P4};
		for (int k = 0; k < 4; k++)
			drawLabel(args.vg, "knob", kessellabels::BASE_KNOB, FX_LABELS[t][k], at(pos[k]));
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		// LINK: lit while KESSEL is on the left
		NVGcontext* vg = args.vg;
		bool d = module && module->docked.load();
		Vec c = mm2px(Vec(LF::LINK[0], LF::LINK[1]));
		nvgBeginPath(vg);
		nvgRoundedRect(vg, c.x - mm2px(3.25f), c.y - mm2px(0.54f), mm2px(6.5f), mm2px(1.08f), mm2px(0.54f));
		nvgFillColor(vg, d ? ACCENT : nvgRGB(0x2a, 0x2b, 0x33));
		nvgFill(vg);
	}
};

static std::string fxValues(KesselFx* m, int t, float bpm) {
	auto pc = [&](int k) { return (int) std::lround(m->params[KesselFx::P_PARAM + k].getValue() * 100.f); };
	switch (t) {
	case FX_DELAY:
	case FX_PING:
		if (bpm > 0.f)
			return string::f("%s \xc2\xb7 FDBK %d%% \xc2\xb7 TONE %d%%",
			                 DIV_NAMES[divOf(m->params[KesselFx::P_PARAM].getValue(), bpm, t)], pc(1), pc(2));
		return string::f("TIME %d%% \xc2\xb7 FDBK %d%% \xc2\xb7 TONE %d%%", pc(0), pc(1), pc(2));
	case FX_RING: {
		int w = clamp((int) (m->params[KesselFx::P_PARAM + 3].getValue() * 5.99f), 0, 5);
		return string::f("FREQ %.0f HZ \xc2\xb7 %s", m->params[KesselFx::P_PARAM].getValue() * 880.f, WAVE_NAMES[w]);
	}
	case FX_CRUSH:
		return string::f("%.0f BITS \xc2\xb7 RATE %d%%", 16.f - m->params[KesselFx::P_PARAM].getValue() * 14.f, pc(1));
	case FX_DJ: {
		float d = m->params[KesselFx::P_PARAM].getValue() - 0.5f;
		return std::fabs(d) < 0.01f ? std::string("OPEN") : string::f("%s %d%% \xc2\xb7 RES %d%%", d < 0 ? "LP" : "HP",
		                                                              (int) std::lround(std::fabs(d) * 200.f), pc(1));
	}
	default:
		return string::f("%s %d%% \xc2\xb7 %s %d%%", FX_LABELS[t][0], pc(0), FX_LABELS[t][1], pc(1));
	}
}

struct FxDisplay : Widget {
	KesselFx* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		auto bold = ui::boldFont();
		float W = box.size.x;
		int t = module ? clamp((int) std::round(module->params[KesselFx::FX_PARAM].getValue()), 0, NFX - 1) : 0;
		float bpm = module ? module->bpmShown.load() : 120.f;
		ui::text(vg, bold, mm2px(3.f), mm2px(4.3f), string::f("EFFECT %d / %d", t + 1, NFX), 5.2f, ui::DIM, NVG_ALIGN_LEFT, 1.f);
		ui::text(vg, bold, W - mm2px(3.f), mm2px(4.3f), bpm > 0.f ? string::f("%.0f BPM", bpm) : std::string("FREE"), 5.2f,
		         ui::DIM, NVG_ALIGN_RIGHT, 1.f);
		if (module && !module->docked.load()) {
			ui::text(vg, bold, mm2px(3.f), mm2px(9.6f), "DOCK RIGHT", 9.f, ui::WHITE, NVG_ALIGN_LEFT, 0.6f);
			ui::text(vg, bold, mm2px(3.f), mm2px(12.8f), "OF KESSEL", 5.2f, ACCENT, NVG_ALIGN_LEFT, 1.f);
			Widget::drawLayer(args, layer);
			return;
		}
		ui::text(vg, bold, mm2px(3.f), mm2px(9.6f), FX_NAMES[t], 12.f, ui::WHITE, NVG_ALIGN_LEFT, 0.6f);
		if (module) {
			int cvt = clamp((int) std::round(module->params[KesselFx::CVT_PARAM].getValue()), 0, NCV - 1);
			if (module->inputs[KesselFx::CV_INPUT].isConnected())
				ui::text(vg, bold, W - mm2px(3.f), mm2px(9.4f),
				         std::string("CV \xe2\x80\xba ") + (cvt == 0 ? "MIX" : FX_LABELS[t][cvt - 1]), 5.4f, ACCENT,
				         NVG_ALIGN_RIGHT, 1.f);
			ui::text(vg, bold, mm2px(3.f), mm2px(12.8f), fxValues(module, t, bpm), 5.0f, ui::DIM, NVG_ALIGN_LEFT, 0.6f);
		}
		Widget::drawLayer(args, layer);
	}
};

struct KesselFxWidget : ModuleWidget {
	KesselFxWidget(KesselFx* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/KesselFx.svg")));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		FxDisplay* d = new FxDisplay;
		d->module = module;
		d->box.pos = mm2px(Vec(LF::DISPLAY[0], LF::DISPLAY[1]));
		d->box.size = mm2px(Vec(LF::DISPLAY[2], LF::DISPLAY[3]));
		addChild(d);
		using M = KesselFx;
		NameField* f = createParam<NameField>(mm2px(Vec(LF::FXF[0], LF::FXF[1])), module, M::FX_PARAM);
		f->box.size = mm2px(Vec(LF::FXF[2], LF::FXF[3]));
		f->names = FX_NAMES, f->info = FX_INFO, f->count = NFX, f->title = "Effect", f->undo = "change effect";
		addParam(f);
		NameField* c = createParam<NameField>(mm2px(Vec(LF::CVF[0], LF::CVF[1])), module, M::CVT_PARAM);
		c->box.size = mm2px(Vec(LF::CVF[2], LF::CVF[3]));
		c->count = NCV, c->title = "CV target", c->undo = "change CV target";
		c->nameOf = [module](int i) -> std::string {
			if (i == 0)
				return "MIX";
			int t = module ? clamp((int) std::round(module->params[M::FX_PARAM].getValue()), 0, NFX - 1) : 0;
			return FX_LABELS[t][i - 1];
		};
		addParam(c);
		addParam(createParamCentered<GLargeKnob>(at(LF::MIX), module, M::MIX_PARAM));
		addParam(createParamCentered<GKnob>(at(LF::P1), module, M::P_PARAM + 0));
		addParam(createParamCentered<GKnob>(at(LF::P2), module, M::P_PARAM + 1));
		addParam(createParamCentered<GSmallKnob>(at(LF::P3), module, M::P_PARAM + 2));
		addParam(createParamCentered<GSmallKnob>(at(LF::P4), module, M::P_PARAM + 3));
		addParam(createLightParamCentered<VCVLightBezelLatch<ui::AccentLight<KESSEL_RGB>>>(at(LF::ON), module, M::ON_PARAM,
		                                                                                 M::ON_LIGHT));
		addParam(createParamCentered<GSmallKnob>(at(LF::DEPTH), module, M::DEPTH_PARAM));
		addParam(createParamCentered<GSmallKnob>(at(LF::LEVEL), module, M::LEVEL_PARAM));
		FxText* t = new FxText;
		t->module = module;
		t->box.size = box.size;
		addChild(t);
		addInput(createInputCentered<PJ301MPort>(at(LF::IN_CV), module, M::CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(LF::IN_CLOCK), module, M::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(LF::IN_L), module, M::L_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(LF::IN_R), module, M::R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(LF::OUT_L), module, M::L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(LF::OUT_R), module, M::R_OUTPUT));
	}
	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Dock right of KESSEL: its FX SEND knobs feed this effect"));
		menu->addChild(createMenuLabel("Delay: with a clock (KESSEL's or this CLOCK) TIME picks a division"));
		menu->addChild(createMenuLabel("Effects from the LXR firmware by Erica Synths (GPL-3.0), crusher and DJ filter KESSEL's own"));
	}
};

}  // namespace genra_kessel

Model* modelKessel = createModel<genra_kessel::Kessel, genra_kessel::KesselWidget>("Kessel");
Model* modelKesselFx = createModel<genra_kessel::KesselFx, genra_kessel::KesselFxWidget>("KesselFx");
