// SPDX-License-Identifier: GPL-3.0-only
// SAURUS: PALEOTONE's creature calls (eng_saurus.c, on SLOOP and Felucca) as a VCV Rack voice. The engine runs
// unchanged in a private copy of the SAURUS core (SaurusCore.hpp); this file is the panel, the jacks and the glue:
// knobs and CV into the part's parameters, V/OCT + GATE (polyphonic) into its notes, CALL into one call, its 44.1 kHz
// audio resampled to Rack's rate. The display shows one of eight own pixel creatures, the one the preset is.
// After KURBEL (src/kurbel/Kurbel.cpp).
#include "../plugin.hpp"
#include "../../genra/Follow.hpp"
#include "../../genra/jiant/JiantUI.hpp"
#include "../../genra/ui/Look.hpp"
#include "SaurusCore.hpp"
#include "SaurusLayout.hpp"
#include <atomic>
#include <array>
#include <cstring>

namespace genra_saurus {

#define SAURUS_RGB 0x5c, 0x9c, 0x3a   // keep in step with tools/jpanel.py COLORS

static const int NE = 7;                      // THROAT: SIZE SWOOP LEN GROWL BRTH MOUTH WILD (the engine's E1..E7)
static const int MAXCH = 8;
static const int CALL_NOTE = 48;              // CALL without V/OCT: C3
static const char* CALL_NAMES[JSA_NCALL] = {"HOLD", "BELLOW", "ROAR", "BARK", "HOOT", "TRILL", "CHIRP", "MOAN", "HISS",
                                            "SNORT", "KEYS"};
static const char* VOICE_NAMES[3] = {"POLY", "MONO", "LEGATO"};

// ------------------------------------------------------------------------------------------------ the creatures
// Eight own pixel creatures (16 wide). G body, g shade, W eye white, E eye / open mouth, M mouth (open: E), T teeth,
// A fur. The preset picks one (CREATURE_OF); the KEYS kit shows the one of the key played last.
enum { C_TREX, C_RAPTOR, C_LONGNECK, C_CRESTED, C_PTERO, C_FRILLED, C_TOAD, C_APE, NCREATURES };
static const char* CREATURE_NAMES[NCREATURES] = {"T-REX", "RAPTOR", "LONGNECK", "CRESTED", "PTEROSAUR", "FRILLED",
                                                 "TOAD", "GIANT APE"};
static const std::vector<std::vector<const char*>> SPRITES = {
    {"........GGGGGG..", "......GGGGGGGGG.", "......GGWEGGGGGG", "......GGGGGGGGGG", "......GGGGGMMMMM",
     "......GGGGGT.T.T", "......GGGGGGGG..", "..G..GGGGGGG....", ".GG.GGGGGGGGGG..", "GGGGGGGGGGGG.g..",
     "GGGGGGGGGGGG....", ".GGGGGGGGGGG....", "...GGGGGGGG.....", "....GG...GG.....", "....GG...GG.....",
     "...GGG..GGG....."},
    {"..........GGG...", ".........GGEGG..", ".........GGGGMM.", "........GGGG....", "GG....GGGGG.....",
     ".GGGGGGGGGG.....", "..GGGGGGGG.G....", "......GG..G.....", ".....G..G.......", "....GG..GG......"},
    {"............GG..", "...........GGEG.", "...........GGM..", "..........GG....", ".........GG.....",
     "........GG......", "...GGGGGGG......", "GGGGGGGGGGG.....", "..GGGGGGGGG.....", "...GG...GG......",
     "...GG...GG......"},
    {"GGGG............", "..GGGGG.........", "....GGEGG.......", "....GGGGMM......", "....GGG.........",
     "...GGGG.........", "GGGGGGGGGG......", ".GGGGGGGGGG.....", "..GGGGGGGG......", "...GG...GG......",
     "...GG...GG......"},
    {"......GG........", "......GGEMM.....", "G.....GGG......G", "GG...GGGGG....GG", ".GGGGGGGGGGGGGG.",
     "..GGGG.GG.GGGG..", "....G..GG..G....", ".......GG......."},
    {"..........gg....", ".........gGGg.G.", "........gGGEGGG.", "........gGGGGMM.", ".GGGGGGGgGGGG...",
     "GGGGGGGGGGGG....", ".GGGGGGGGGG.....", "..GG...GG.......", "..GG...GG......."},
    {"...GE....EG.....", "..GGGG..GGGG....", ".GGGGGGGGGGGG...", "GGMMMMMMMMMGGG..", "GGGGGGGGGGGGGG..",
     ".GGgggggggGG....", "GG..........GG.."},
    {"....AAAA........", "...AAEEAA.......", "...AAMMAA.......", "..AAAAAAAA......", ".AAAAAAAAAA.....",
     "AA.AAAAAA.AA....", "AA.AAAAAA.AA....", "...AA..AA.......", "..AAA..AAA......"},
};
// PALEOTONE's presets in order (BRACHIO CALL, TYRANT ROAR, .. COMPY PACK, CALL KIT)
static const int CREATURE_OF[21] = {C_LONGNECK, C_TREX, C_RAPTOR, C_RAPTOR, C_CRESTED, C_PTERO, C_LONGNECK, C_FRILLED,
                                    C_FRILLED, C_LONGNECK, C_APE, C_RAPTOR, C_TOAD, C_TREX, C_LONGNECK, C_TREX, C_TREX,
                                    C_LONGNECK, C_CRESTED, C_RAPTOR, C_TREX};

// ------------------------------------------------------------------------------------------------ the module
struct Saurus : Module, follow::Host {
	enum ParamId {
		TYPE_PARAM, VOICE_PARAM,
		ENUMS(E_PARAM, NE),
		ATK_PARAM, DEC_PARAM, SUS_PARAM, REL_PARAM,
		DIST_PARAM, CHOR_PARAM, DLY_PARAM, REV_PARAM,
		LEVEL_PARAM,
		PREV_PARAM, NEXT_PARAM,
		CALL_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(E_INPUT, NE),
		VOCT_INPUT, GATE_INPUT, VEL_INPUT, CALL_INPUT, TYPE_INPUT,
		INPUTS_LEN
	};
	enum OutputId { L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { CALL_LIGHT, LIGHTS_LEN };
	enum { BLOCK = 32 };

	SaurusCore core;
	std::string status;

	// settings
	int preset = 1;                               // TYRANT ROAR
	bool fromPatch = false;
	bool powered = false;                         // the core runs (powerOn): before that it is loaded but not started
	std::atomic<int> presetReq{-1};

	// for the UI thread
	std::array<std::array<char, 16>, JSA_NP> values{};   // every parameter as the firmware writes it
	std::atomic<int> shownPreset{1};
	std::atomic<int> shownType{2};
	std::atomic<int> shownKit{0};                 // KEYS: the kit creature (preset index) of the key played last
	std::atomic<float> shownLevel{0.f};
	std::atomic<float> callAge{1e9f};             // seconds since the last note-on (the call's progress)
	std::atomic<bool> sounding{false};

	// engine thread
	int sent[JSA_NP];
	float devL[BLOCK], devR[BLOCK];
	jiant::Resampler src;
	dsp::DoubleRingBuffer<dsp::Frame<2>, 2048> outBuf;
	uint32_t framesSinceUi = 0;
	float peak = 0.f;
	bool chOn[MAXCH] = {};
	int chNote[MAXCH] = {};
	bool gateIn[MAXCH] = {};
	// the samples from a gate's edge to its note (NOTE_DELAY: V/OCT and VEL may come a sample after the gate, e.g.
	// through a quantizer), the note's velocity, the channels the last sample used
	enum { NOTE_DELAY = 2 };
	int chArm[MAXCH] = {};
	int chVel[MAXCH] = {};
	int chActive = 0;
	bool callOn = false;                          // CALL (button or trigger) holds a note
	int callNote = CALL_NOTE;
	float callLeft = 0.f;                         // the trigger's hold, seconds
	int kitNext = 0;                              // CALL on KEYS: the creatures one after the other
	dsp::BooleanTrigger prevTrig, nextTrig, callBtn;
	dsp::SchmittTrigger callTrig;
	float age = 1e9f;

	Saurus() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		srcName = "SAURUS";
		audioL = L_OUTPUT;
		audioR = R_OUTPUT;
		fol.bind(VOCT_INPUT, follow::VOCT);
		fol.bind(GATE_INPUT, follow::GATE);
		fol.bind(VEL_INPUT, follow::VEL);
		configSwitch(TYPE_PARAM, 0.f, (float) (JSA_NCALL - 1), 2.f, "Call type",
		             {"HOLD (as played)", "BELLOW", "ROAR", "BARK", "HOOT", "TRILL", "CHIRP", "MOAN", "HISS", "SNORT",
		              "KEYS (a creature a key from C2)"});
		configSwitch(VOICE_PARAM, 0.f, 2.f, 1.f, "Voice", {"POLY", "MONO", "LEGATO"});
		const char* eN[NE] = {"Size (big animal: low throat)", "Swoop (the call's pitch depth)", "Length of the call",
		                      "Growl", "Breath", "Mouth (how far it opens)", "Wild (each call its own)"};
		for (int k = 0; k < NE; k++) {
			configParam<RawQuantity>(E_PARAM + k, 0.f, 127.f, 64.f, eN[k])->snapEnabled = true;
			configInput(E_INPUT + k, string::f("%s CV (±5 V: ± half the range)", eN[k]));
		}
		const char* envN[4] = {"Attack", "Decay", "Sustain", "Release"};
		const char* fxN[4] = {"Distortion", "Chorus send", "Delay send", "Reverb send"};
		for (int i = 0; i < 4; i++) {
			configParam<RawQuantity>(ATK_PARAM + i, 0.f, 127.f, 0.f, envN[i])->snapEnabled = true;
			configParam<RawQuantity>(DIST_PARAM + i, 0.f, 127.f, 0.f, fxN[i])->snapEnabled = true;
		}
		configParam<RawQuantity>(LEVEL_PARAM, 0.f, 127.f, 110.f, "Level")->snapEnabled = true;
		configButton(PREV_PARAM, "Previous preset");
		configButton(NEXT_PARAM, "Next preset");
		configButton(CALL_PARAM, "Call (V/OCT's note, or C3 without a cable; KEYS: the next creature)");
		configInput(VOCT_INPUT, "V/Oct (polyphonic)");
		configInput(GATE_INPUT, "Gate (polyphonic)");
		configInput(VEL_INPUT, "Velocity (0..10 V)");
		configInput(CALL_INPUT, "Call (trigger: one call)");
		configInput(TYPE_INPUT, "Type CV (added to the switch: 1 V a type)");
		configOutput(L_OUTPUT, "Left (mono when Right is not patched)");
		configOutput(R_OUTPUT, "Right");
		for (int j = ATK_PARAM; j <= LEVEL_PARAM; j++)
			paramQuantities[j]->randomizeEnabled = j < DIST_PARAM;
		paramQuantities[VOICE_PARAM]->randomizeEnabled = false;
		for (int& s : sent)
			s = INT32_MIN;
		core.load();                               // here (the UI thread), not in onAdd: the engine waits there
	}

	/** The firmware's raw value (0..127), shown as the FM-1 writes it. */
	struct RawQuantity : ParamQuantity {
		int coreIndex() const {
			if (paramId >= E_PARAM && paramId < E_PARAM + NE)
				return JSA_SIZE + paramId - E_PARAM;
			if (paramId >= ATK_PARAM && paramId <= REL_PARAM)
				return JSA_ATK + paramId - ATK_PARAM;
			if (paramId >= DIST_PARAM && paramId <= REV_PARAM)
				return JSA_DIST + paramId - DIST_PARAM;
			return JSA_LEVEL;
		}
		std::string getDisplayValueString() override {
			Saurus* m = dynamic_cast<Saurus*>(module);
			if (m && m->core.loaded())
				return m->values[coreIndex()].data();
			return ParamQuantity::getDisplayValueString();
		}
	};

	// ---------------------------------------------------------------------------------- power, presets
	void powerOn() {
		if (!core.loaded() && !core.load()) {
			status = core.error;
			WARN("SAURUS: %s", status.c_str());
			return;
		}
		status.clear();
		core.api->init(random::u32() | 1u);
		for (int& s : sent)
			s = INT32_MIN;
		outBuf.clear();
		src.reset();
		for (int c = 0; c < MAXCH; c++)
			chOn[c] = gateIn[c] = false, chArm[c] = 0;
		chActive = MAXCH;
		callOn = false;
		int n = std::max(1, (int) core.api->npresets());
		preset = ((preset % n) + n) % n;
		if (!fromPatch)
			applyPreset(preset);
		else
			core.api->load_preset((uint32_t) preset);
		shownPreset = preset;
		powered = true;
	}

	/** The factory sound into the knobs (and the engine). */
	void applyPreset(int i) {
		const jsa_api* a = core.api;
		if (!a)
			return;
		int n = (int) a->npresets();
		preset = ((i % n) + n) % n;
		a->load_preset((uint32_t) preset);
		params[TYPE_PARAM].setValue((float) a->get(JSA_CALL));
		for (int k = 0; k < NE; k++)
			params[E_PARAM + k].setValue((float) a->get(JSA_SIZE + k));
		for (int i2 = 0; i2 < 4; i2++) {
			params[ATK_PARAM + i2].setValue((float) a->get(JSA_ATK + i2));
			params[DIST_PARAM + i2].setValue((float) a->get(JSA_DIST + i2));
		}
		for (int& s : sent)
			s = INT32_MIN;
		shownPreset = preset;
	}

	void onAdd(const AddEvent& e) override {
		params[PREV_PARAM].setValue(0.f);
		params[NEXT_PARAM].setValue(0.f);
		params[CALL_PARAM].setValue(0.f);
		powerOn();
	}
	void onRemove(const RemoveEvent& e) override { follow::gone(this); powered = false; core.unload(); }
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		fromPatch = false;
		presetReq = 1;
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "preset", json_integer(preset));
		fol.toJson(r);
		return r;
	}
	void dataFromJson(json_t* r) override {
		fol.fromJson(r);
		if (json_t* j = json_object_get(r, "preset"))
			preset = (int) json_integer_value(j);
		fromPatch = true;
		if (powered && core.api) {
			int n = std::max(1, (int) core.api->npresets());
			preset = ((preset % n) + n) % n;
			core.api->load_preset((uint32_t) preset);
			for (int& s : sent)
				s = INT32_MIN;
			shownPreset = preset;
		}
	}

	// ---------------------------------------------------------------------------------- the engine
	void push(const jsa_api* a, uint32_t j, int v) {
		if (sent[j] != v) {
			a->set(j, v);
			sent[j] = v;
		}
	}

	int typeNow() {
		float t = params[TYPE_PARAM].getValue();
		if (inputs[TYPE_INPUT].isConnected())
			t += inputs[TYPE_INPUT].getVoltage();
		return clamp((int) std::round(t), 0, (int) JSA_NCALL - 1);
	}

	void processKnobs(const jsa_api* a) {
		push(a, JSA_CALL, typeNow());
		for (int k = 0; k < NE; k++) {
			float x = params[E_PARAM + k].getValue();
			if (inputs[E_INPUT + k].isConnected())
				x += inputs[E_INPUT + k].getVoltage() * 12.7f;
			push(a, JSA_SIZE + k, clamp((int) std::round(x), 0, 127));
		}
		for (int i = 0; i < 4; i++) {
			push(a, JSA_ATK + i, (int) std::round(params[ATK_PARAM + i].getValue()));
			push(a, JSA_DIST + i, (int) std::round(params[DIST_PARAM + i].getValue()));
		}
		push(a, JSA_LEVEL, (int) std::round(params[LEVEL_PARAM].getValue()));
		push(a, JSA_VOICE, clamp((int) std::round(params[VOICE_PARAM].getValue()), 0, 2));   // V_POLY V_MONO V_LEGATO
	}

	void noteOn(const jsa_api* a, int nn, int vel) {
		a->note_on((uint32_t) nn, (uint32_t) vel);
		age = 0.f;
		if (typeNow() == (int) JSA_CALL_KEYS)
			shownKit = ((nn - (int) JSA_KIT_BASE) % (int) JSA_NKIT + (int) JSA_NKIT) % (int) JSA_NKIT;
	}

	/** The call's length in seconds, from LEN as the firmware writes it ("1.63s", "383ms"). */
	float lenSeconds() {
		const char* s = values[JSA_LEN].data();
		float v = (float) std::atof(s);
		if (std::strstr(s, "ms"))
			v /= 1000.f;
		return clamp(v, 0.05f, 8.f);
	}

	/** V/OCT + GATE, one note a channel; CALL (button: while held; trigger: the call's length) on V/OCT's first note. */
	void processNotes(const jsa_api* a, float dt) {
		bool gated = inputs[GATE_INPUT].isConnected();
		int chs = gated ? std::min(MAXCH, std::max(inputs[GATE_INPUT].getChannels(), 1)) : 0;
		int last = std::max(chs, chActive);        // (channels that went away still let go of their note)
		for (int c = 0; c < last; c++) {
			bool on = false;
			if (c < chs) {
				float g = inputs[GATE_INPUT].getPolyVoltage(c);
				on = gateIn[c] ? g > 0.5f : g >= 1.f;
			}
			bool rise = on && !gateIn[c] && !chOn[c];
			gateIn[c] = on;
			if (rise) {
				chArm[c] = NOTE_DELAY;              // the gate's edge: the note follows in NOTE_DELAY samples
			} else if (chArm[c] > 0) {
				if (--chArm[c] == 0 || !on) {       // (a gate shorter than the wait still plays)
					chArm[c] = 0;
					int nn = clamp((int) std::round(inputs[VOCT_INPUT].getPolyVoltage(c) * 12.f) + 60, 0, 127);
					int vel = 100;
					if (inputs[VEL_INPUT].isConnected())
						vel = clamp((int) std::round(inputs[VEL_INPUT].getPolyVoltage(c) / 10.f * 127.f), 1, 127);
					noteOn(a, nn, vel);
					chNote[c] = nn;
					chVel[c] = vel;
					chOn[c] = true;
				}
			} else if (!on && chOn[c]) {
				a->note_off((uint32_t) chNote[c]);
				chOn[c] = false;
			} else if (on && chOn[c]) {
				int nn = clamp((int) std::round(inputs[VOCT_INPUT].getPolyVoltage(c) * 12.f) + 60, 0, 127);
				if (nn != chNote[c]) {              // a new pitch under a held gate: the note again, at its velocity
					noteOn(a, nn, chVel[c]);
					a->note_off((uint32_t) chNote[c]);
					chNote[c] = nn;
				}
			}
		}
		chActive = chs;
		// CALL
		bool btn = params[CALL_PARAM].getValue() > 0.5f;
		bool pressed = callBtn.process(btn);
		bool trig = callTrig.process(inputs[CALL_INPUT].getVoltage(), 0.1f, 1.f);
		if (pressed || trig) {
			if (callOn)
				a->note_off((uint32_t) callNote);
			if (typeNow() == (int) JSA_CALL_KEYS && !inputs[VOCT_INPUT].isConnected())
				callNote = (int) JSA_KIT_BASE + (kitNext++ % (int) JSA_NKIT);
			else
				callNote = inputs[VOCT_INPUT].isConnected()
				             ? clamp((int) std::round(inputs[VOCT_INPUT].getVoltage() * 12.f) + 60, 0, 127)
				             : CALL_NOTE;
			noteOn(a, callNote, 110);
			callOn = true;
			callLeft = trig ? lenSeconds() : 0.f;
		}
		if (callOn) {
			callLeft -= dt;
			if (!btn && callLeft <= 0.f) {
				a->note_off((uint32_t) callNote);
				callOn = false;
			}
		}
		lights[CALL_LIGHT].setBrightness(callOn ? 1.f : 0.f);
	}

	void renderBlock(const jsa_api* a, float sampleRate) {
		processKnobs(a);
		int devRate = (int) a->sample_rate();
		float bpm = fol.active() ? follow::genraBpm(fol.genra, sampleRate) : 120.f;
		a->set_bpm((uint32_t) std::round(clamp(bpm, 20.f, 300.f)));
		a->render(BLOCK, devL, devR);
		dsp::Frame<2> in[BLOCK];
		for (int i = 0; i < BLOCK; i++) {
			in[i].samples[0] = devL[i];
			in[i].samples[1] = devR[i];
			peak = std::max(peak, std::max(std::fabs(devL[i]), std::fabs(devR[i])));
		}
		src.process(in, BLOCK, devRate, (int) sampleRate, outBuf);
		framesSinceUi += BLOCK;
		if (framesSinceUi >= (uint32_t) devRate / 30) {
			framesSinceUi = 0;
			for (uint32_t j = 0; j < JSA_NP; j++)
				a->format(j, a->get(j), values[j].data(), (uint32_t) values[j].size());
			sounding = a->active_voices() > 0;
			shownLevel = peak;
			peak = 0.f;
			shownType = typeNow();
		}
	}

	void onPortChange(const PortChangeEvent& e) override { fol.portChange(e); }

	void process(const ProcessArgs& args) override {
		fol.process(this, args.frame);
		const jsa_api* a = core.api;
		if (!a) {
			outputs[L_OUTPUT].setVoltage(0.f);
			outputs[R_OUTPUT].setVoltage(0.f);
			return;
		}
		int req = presetReq.load(std::memory_order_relaxed) >= 0 ? presetReq.exchange(-1) : -1;
		bool want = req >= 0;
		if (prevTrig.process(params[PREV_PARAM].getValue() > 0.5f)) {
			req = preset - 1;
			want = true;
		}
		if (nextTrig.process(params[NEXT_PARAM].getValue() > 0.5f)) {
			req = preset + 1;
			want = true;
		}
		if (want) {
			fromPatch = false;
			applyPreset(req);
		}
		processNotes(a, args.sampleTime);
		age += args.sampleTime;
		callAge.store(age, std::memory_order_relaxed);
		int guard = 0;
		while (outBuf.empty() && guard++ < 8)
			renderBlock(a, args.sampleRate);
		dsp::Frame<2> f = outBuf.empty() ? dsp::Frame<2>{} : outBuf.shift();
		float l = f.samples[0] * 5.f, r = f.samples[1] * 5.f;
		if (outputs[R_OUTPUT].isConnected() || pulled(args.frame)) {
			outputs[L_OUTPUT].setVoltage(l);
			outputs[R_OUTPUT].setVoltage(r);
		} else {
			outputs[L_OUTPUT].setVoltage(0.5f * (l + r));
			outputs[R_OUTPUT].setVoltage(0.f);
		}
	}

	/** The creature on the display now. */
	int creature() {
		int p = clamp((int) shownPreset, 0, 20);
		if (shownType == (int) JSA_CALL_KEYS)
			return CREATURE_OF[clamp((int) shownKit, 0, 19)];
		return CREATURE_OF[p];
	}
};

// ------------------------------------------------------------------------------------------------ the panel (GENRA look)
namespace L = slayout::saurus;
static const NVGcolor ACCENT = nvgRGB(SAURUS_RGB);

static NVGcolor pixelColor(char ch) {
	switch (ch) {
		case 'G': return ACCENT;
		case 'g': return nvgRGB(0x3f, 0x6e, 0x27);
		case 'W': return nvgRGB(0xff, 0xff, 0xff);
		case 'E': return nvgRGB(0x11, 0x11, 0x11);
		case 'M': return nvgRGB(0x8f, 0x2f, 0x2a);
		case 'T': return nvgRGB(0xf2, 0xf0, 0xe6);
		case 'A': return nvgRGB(0x7a, 0x66, 0x50);
		default: return nvgRGBA(0, 0, 0, 0);
	}
}

/** The creature: its mouth opens while the call is loud. */
struct ScreenWidget : Widget {
	Saurus* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		if (module && !module->status.empty()) {
			jiant::drawStatus(vg, module->status, box.size.x);
			return;
		}
		int c = module ? module->creature() : C_TREX;
		bool open = module && module->sounding && module->shownLevel > 0.06f;
		const std::vector<const char*>& sp = SPRITES[c];
		const float P = std::min(box.size.x / 16.f, box.size.y / (float) sp.size());
		float ox = (box.size.x - 16 * P) / 2, oy = (box.size.y - sp.size() * P) / 2;
		for (size_t y = 0; y < sp.size(); y++)
			for (int x = 0; x < 16 && sp[y][x]; x++) {
				char ch = sp[y][x];
				if (ch == '.')
					continue;
				if (open && ch == 'M')
					ch = 'E';
				nvgBeginPath(vg);
				nvgRect(vg, ox + x * P, oy + y * P, P + 0.1f, P + 0.1f);
				nvgFillColor(vg, pixelColor(ch));
				nvgFill(vg);
			}
	}
};

/** Right of the creature: the preset, creature · call · voice, the values, the call's shape (pitch and mouth over LEN). */
struct InfoWidget : Widget {
	Saurus* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		auto bold = jiant::boldFont(), mono = jiant::monoFont();
		float w = box.size.x, h = box.size.y;
		const jsa_api* a = module ? module->core.api : nullptr;
		int p = module ? (int) module->shownPreset : 1;
		int type = module ? (int) module->shownType : 2;
		int voice = module ? clamp((int) std::round(module->params[Saurus::VOICE_PARAM].getValue()), 0, 2) : 1;
		int cr = module ? module->creature() : C_TREX;
		float gw = w * 0.42f, gx = w - gw, gy = 0, gh = h;  // the call's shape, right
		float tw = gx - 6;
		jiant::text(vg, bold, 0, 7.f, string::f("CALL %d / %u", p + 1, a ? a->npresets() : 21u), 5.2f, jiant::DIM,
		            NVG_ALIGN_LEFT, 1.f);
		jiant::text(vg, bold, 0, h * 0.5f, a ? a->preset_name((uint32_t) p) : "TYRANT ROAR", 11.f, jiant::WHITE,
		            NVG_ALIGN_LEFT, 0.5f);
		jiant::text(vg, bold, 0, h * 0.5f + 10.f, string::f("%s \xc2\xb7 %s \xc2\xb7 %s", CREATURE_NAMES[cr], CALL_NAMES[type],
		                                                  VOICE_NAMES[voice]),
		            5.2f, ACCENT, NVG_ALIGN_LEFT, 0.8f);
		if (module && module->core.loaded()) {
			static const char* lab[3] = {"SIZE", "SWOOP", "LEN"};
			std::string s;
			for (int k = 0; k < 3; k++)
				s += string::f("%s%s %s", k ? "  " : "", lab[k], module->values[JSA_SIZE + k].data());
			nvgSave(vg);
			nvgScissor(vg, 0, 0, tw, h);
			jiant::text(vg, bold, 0, h - 1.f, s, 5.f, jiant::DIM, NVG_ALIGN_LEFT, 0.5f);
			nvgRestore(vg);
		}
		jiant::roundRect(vg, gx, gy, gw, gh, 2.2f, jiant::FIELD);
		int32_t pc[5], mc[5];
		if (!a || !a->call_curve((uint32_t) type, 0, pc) || !a->call_curve((uint32_t) type, 1, mc)) {
			jiant::text(vg, mono, gx + 5, gy + gh / 2 - 2, "KIT: A CREATURE A KEY", 5.6f, ACCENT, NVG_ALIGN_LEFT, 0.f);
			jiant::text(vg, mono, gx + 5, gy + gh / 2 + 8, "C2 BRACHIO .. G3 COMPY", 5.2f, jiant::DIM, NVG_ALIGN_LEFT, 0.f);
			return;
		}
		float x0 = gx + 6, xw = gw - 34, mid = gy + gh / 2;
		auto curve = [&](const int32_t* v, float scale, float off, NVGcolor col, float sw) {
			nvgBeginPath(vg);
			for (int i = 0; i < 5; i++) {
				float x = x0 + i * xw / 4, y = clamp(mid + off - v[i] * scale, gy + 3, gy + gh - 3);
				if (i == 0)
					nvgMoveTo(vg, x, y);
				else
					nvgLineTo(vg, x, y);
			}
			nvgStrokeColor(vg, col);
			nvgStrokeWidth(vg, sw);
			nvgStroke(vg);
		};
		NVGcolor mcol = jiant::TXT;
		mcol.a = 0.5f;
		curve(mc, 0.9f, 8.f, mcol, 1.f);
		curve(pc, 0.85f, 0.f, ACCENT, 1.5f);
		jiant::text(vg, mono, gx + gw - 3, gy + 11, "pitch", 5.4f, ACCENT, NVG_ALIGN_RIGHT, 0.f);
		jiant::text(vg, mono, gx + gw - 3, gy + 23, "mouth", 5.4f, jiant::DIM, NVG_ALIGN_RIGHT, 0.f);
		if (module && module->sounding) {
			float q = clamp((float) module->callAge / module->lenSeconds(), 0.f, 1.f);
			float x = x0 + q * xw;
			nvgBeginPath(vg);
			nvgMoveTo(vg, x, gy + 3);
			nvgLineTo(vg, x, gy + gh - 3);
			NVGcolor c = ACCENT;
			c.a = 0.6f;
			nvgStrokeColor(vg, c);
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);
		}
	}
};

struct SaurusWidget : ModuleWidget {
	SaurusWidget(Saurus* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Saurus.svg")));
		follow::addStrip(this, module, {jiant::at(L::VOCT), jiant::at(L::GATE), jiant::at(L::VEL)});
		jiant::addScrews(this);

		ScreenWidget* scr = new ScreenWidget;
		scr->module = module;
		scr->box = jiant::area(L::SCREEN);
		addChild(scr);
		InfoWidget* info = new InfoWidget;
		info->module = module;
		info->box = jiant::area(L::INFO);
		addChild(info);

		genra_look::PickField* pf = createWidget<genra_look::PickField>(Vec(L::PRESETF[0], L::PRESETF[1]));
		pf->box.size = Vec(L::PRESETF[2], L::PRESETF[3]);
		pf->accent = ACCENT;
		pf->count = [=]() { return module && module->core.api ? (int) module->core.api->npresets() : 1; };
		pf->name = [=](int i) -> std::string {
			return module && module->core.api ? module->core.api->preset_name((uint32_t) i) : "TYRANT ROAR";
		};
		pf->current = [=]() { return module ? (int) module->shownPreset : 1; };
		pf->choose = [=](int i) {
			if (module)
				module->presetReq = i;
		};
		addChild(pf);

		addParam(createLightParamCentered<VCVLightBezel<jiant::AccentLight<SAURUS_RGB>>>(
		    jiant::at(L::CALL), module, Saurus::CALL_PARAM, Saurus::CALL_LIGHT));
		genra_look::ParamField* type = createParam<genra_look::ParamField>(Vec(L::TYPEF[0], L::TYPEF[1]), module, Saurus::TYPE_PARAM);
		type->box.size = Vec(L::TYPEF[2], L::TYPEF[3]);
		type->count = JSA_NCALL;
		type->nameOf = [](int i) -> std::string { return CALL_NAMES[clamp(i, 0, (int) JSA_NCALL - 1)]; };
		type->infoOf = [](int) -> std::string { return "TYPE"; };
		type->title = "Call type";
		type->undo = "change call type";
		addParam(type);
		jiant::LampRow* vs = createParam<jiant::LampRow>(Vec(0, 0), module, Saurus::VOICE_PARAM);
		vs->place(L::VOICE, 3);
		vs->accent = ACCENT;
		addParam(vs);
		addParam(createParamCentered<GKnob>(jiant::at(L::LEVEL), module, Saurus::LEVEL_PARAM));

		for (int k = 0; k < NE; k++) {
			Vec c = jiant::at(L::E[k]);
			if (k == 3)
				addParam(createParamCentered<GLargeKnob>(c, module, Saurus::E_PARAM + k));
			else if (k < 3)
				addParam(createParamCentered<GKnob>(c, module, Saurus::E_PARAM + k));
			else
				addParam(createParamCentered<GSmallKnob>(c, module, Saurus::E_PARAM + k));
			addInput(createInputCentered<PJ301MPort>(jiant::at(L::E_IN[k]), module, Saurus::E_INPUT + k));
		}
		const float* env[4] = {L::ATK, L::DEC, L::SUS, L::REL};
		const float* fx[4] = {L::DIST, L::CHOR, L::DLY, L::REV};
		for (int i = 0; i < 4; i++) {
			addParam(createParamCentered<GSmallKnob>(jiant::at(env[i]), module, Saurus::ATK_PARAM + i));
			addParam(createParamCentered<GSmallKnob>(jiant::at(fx[i]), module, Saurus::DIST_PARAM + i));
		}
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::VOCT), module, Saurus::VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::GATE), module, Saurus::GATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::VEL), module, Saurus::VEL_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::CALL_IN), module, Saurus::CALL_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::TYPE_IN), module, Saurus::TYPE_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(jiant::at(L::OUT_L), module, Saurus::L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(jiant::at(L::OUT_R), module, Saurus::R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		if (follow::Host* h = follow::host(module))
			h->fol.appendMenu(menu, module);
		Saurus* m = getModule<Saurus>();
		if (!m)
			return;
		if (m->core.loaded()) {
			menu->addChild(new MenuSeparator);
			std::vector<std::string> names;
			for (uint32_t i = 0; i < m->core.api->npresets(); i++)
				names.push_back(m->core.api->preset_name(i));
			menu->addChild(createIndexSubmenuItem("Preset", names, [=]() { return (size_t) m->shownPreset; },
			                                      [=](size_t i) { m->presetReq = (int) i; }));
		}
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("The delay follows GENRA's tempo while it follows a GENRA (else 120 BPM)"));
		menu->addChild(createMenuLabel("Creature calls: PALEOTONE's SAURUS on SLOOP and Felucca (GPL-3.0)"));
	}
};

}  // namespace genra_saurus

Model* modelSaurus = createModel<genra_saurus::Saurus, genra_saurus::SaurusWidget>("Saurus");
