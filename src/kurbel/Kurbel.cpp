// SPDX-License-Identifier: GPL-3.0-only
// KURBEL: ESTAMPIE's hurdy-gurdy (GHOULBOX's GURDY engine) and its shawm (TRIO) as a VCV Rack voice. The engines run
// unchanged in a private copy of the KURBEL core (KurbelCore.hpp); this file is the panel, the jacks and the glue:
// knobs and CV into the part's parameters, V/OCT + GATE into its (one) melody string, the crank's speed (knob + CV)
// into GHOULBOX's crank, CLOCK into the tempo of COUP's strokes, its 44.1 kHz audio resampled to Rack's rate.
// After GENRA Instruments' ANIMAL (instruments/src/jiant/Animal.cpp) and MATH.
#include "../plugin.hpp"
#include "../../genra/Follow.hpp"
#include "../../genra/jiant/JiantUI.hpp"
#include "KurbelCore.hpp"
#include "KurbelLayout.hpp"
#include <atomic>
#include <array>
#include <cstring>

namespace genra_kurbel {

#define KURBEL_RGB 0xc2, 0x5a, 0x1c   // keep in step with tools/jpanel.py COLORS

static const int NE = 6;                                       // the engine's knobs
static const uint32_t WHEEL_E[NE] = {1, 2, 3, 4, 6, 7};        // GURDY: DLVL 5TH WHL BUZZ BODY WOBL (E0 DRN, E5 COUP)
static const uint32_t REED_E[NE] = {1, 2, 3, 5, 6, 7};         // TRIO: INT2 INT3 DTN CUT RES PW (E0 WAVE, E4 MODE)
static const char* DRONE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char* COUP_NAMES[6] = {"HOLD", "1/4", "1/8", "1/8T", "1/16", "CRANK"};
static const char* TEMP_NAMES[4] = {"EQUAL", "PYTH", "MEAN", "JUST"};
static const char* CLOCK_MODES[4] = {"16ths (4 PPQN)", "8ths (2 PPQN)", "Quarters (1 PPQN)", "24 PPQN"};
static const int CLOCK_PPQN[4] = {4, 2, 1, 24};
static const float CLICKS_PER_REV = 20.f;                     // the FM-1's encoder: the crank's clicks per turn
static const float RPM_PER_VOLT = 24.f;                        // CRANK CV: 0..10 V = 0..240 rpm

// ------------------------------------------------------------------------------------------------ the module
struct Kurbel : Module, follow::Host {
	enum ParamId {
		ENGINE_PARAM, TEMP_PARAM, DRONE_PARAM, COUP_PARAM,
		ENUMS(E_PARAM, NE),
		ATK_PARAM, DEC_PARAM, SUS_PARAM, REL_PARAM,
		DIST_PARAM, CHOR_PARAM, DLY_PARAM, REV_PARAM,
		LEVEL_PARAM,
		CRANK_PARAM,
		RING_PARAM,
		PREV_PARAM, NEXT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(E_INPUT, NE),
		VOCT_INPUT, GATE_INPUT, VEL_INPUT, CLOCK_INPUT, RESET_INPUT, CRANK_INPUT, BUZZ_INPUT,
		INPUTS_LEN
	};
	enum OutputId { L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { RING_LIGHT, LIGHTS_LEN };
	enum { BLOCK = 32 };

	KurbelCore core;
	std::string status;

	// settings
	int preset = 0;
	int wheelPreset = 0;                          // the last WHEEL sound (back to it from REED)
	int clockMode = 0;
	bool crankAlone = true;                       // COUP CRANK without a gate: turning the crank plays (drones + melody)
	bool fromPatch = false;
	std::atomic<int> presetReq{-1};

	// for the UI thread
	std::array<std::array<char, 16>, NE> labels{};
	std::array<std::array<char, 16>, NE + 8> values{};   // the knobs, then ATK .. LEVEL
	std::atomic<int> shownPreset{0};
	std::atomic<bool> sounding{false};
	std::atomic<float> shownRpm{0.f};
	std::atomic<float> shownBpm{120.f};
	std::atomic<uint32_t> shownStrokes{0};
	std::atomic<bool> clocked{false};

	// engine thread
	int sent[JKU_NP];
	int engineNow = -1;                           // the engine the core plays (JKU_WHEEL / JKU_REED)
	float devL[BLOCK], devR[BLOCK];
	jiant::Resampler src;
	dsp::DoubleRingBuffer<dsp::Frame<2>, 2048> outBuf;
	uint32_t framesSinceUi = 0;
	bool gate = false;
	int note = 60;
	dsp::BooleanTrigger prevTrig, nextTrig;
	dsp::SchmittTrigger clockTrig, resetTrig, buzzTrig;
	double sincePulse = 1e9, period = 0.0;
	float bpm = 120.f;

	Kurbel() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		srcName = "KURBEL";
		audioL = L_OUTPUT;
		audioR = R_OUTPUT;
		fol.bind(VOCT_INPUT, follow::VOCT);
		fol.bind(GATE_INPUT, follow::GATE);
		fol.bind(VEL_INPUT, follow::VEL);
		fol.bind(CLOCK_INPUT, follow::CLOCK);
		fol.bind(RESET_INPUT, follow::RESET);
		configSwitch(ENGINE_PARAM, 0.f, 1.f, 0.f, "Engine", {"WHEEL (hurdy-gurdy, bagpipe)", "REED (shawm)"});
		configSwitch(TEMP_PARAM, 0.f, 3.f, 0.f, "Temperament (from DRONE)",
		             {"EQUAL", "PYTH (Pythagorean)", "MEAN (quarter-comma meantone)", "JUST (5-limit)"});
		configSwitch(DRONE_PARAM, 0.f, 11.f, 2.f, "Drone (and the temperament's root)",
		             {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"});
		configSwitch(COUP_PARAM, 0.f, 5.f, 2.f, "Coup (the wrist's strokes on the trompette)",
		             {"HOLD (always)", "1/4", "1/8", "1/8T", "1/16", "CRANK (from the crank's speed)"});
		for (int k = 0; k < NE; k++) {
			configParam<EQuantity>(E_PARAM + k, 0.f, 1.f, 0.5f, string::f("Knob %d", k + 1));
			configInput(E_INPUT + k, string::f("Knob %d CV (±5 V: ± half the range)", k + 1));
		}
		const char* envN[4] = {"Attack", "Decay", "Sustain", "Release"};
		const char* fxN[4] = {"Distortion", "Chorus send", "Delay send", "Reverb send"};
		for (int i = 0; i < 4; i++) {
			configParam<RawQuantity>(ATK_PARAM + i, 0.f, 127.f, 0.f, envN[i])->snapEnabled = true;
			configParam<RawQuantity>(DIST_PARAM + i, 0.f, 127.f, 0.f, fxN[i])->snapEnabled = true;
		}
		configParam<RawQuantity>(LEVEL_PARAM, 0.f, 127.f, 110.f, "Level")->snapEnabled = true;
		configParam(CRANK_PARAM, 0.f, 240.f, 90.f, "Crank (COUP CRANK: how fast the wheel turns)", " rpm");
		configSwitch(RING_PARAM, 0.f, 1.f, 0.f, "Ring (the drones sound on through the rests)", {"Off", "On"});
		configButton(PREV_PARAM, "Previous preset");
		configButton(NEXT_PARAM, "Next preset");
		configInput(VOCT_INPUT, "V/Oct (the melody string)");
		configInput(GATE_INPUT, "Gate");
		configInput(VEL_INPUT, "Velocity (0..10 V)");
		configInput(CLOCK_INPUT, "Clock: the tempo of COUP's strokes (GENRA: 16ths, see the context menu)");
		configInput(RESET_INPUT, "Reset (a held note starts its strokes again, on the beat)");
		configInput(CRANK_INPUT, "Crank CV (added to the knob: 1 V = 24 rpm; a sharp rise is a wrist stroke)");
		configInput(BUZZ_INPUT, "Buzz (trigger: a wrist stroke now, COUP CRANK)");
		configOutput(L_OUTPUT, "Left (mono when Right is not patched)");
		configOutput(R_OUTPUT, "Right");
		for (int j = ATK_PARAM; j <= LEVEL_PARAM; j++)
			paramQuantities[j]->randomizeEnabled = j < DIST_PARAM;
		paramQuantities[ENGINE_PARAM]->randomizeEnabled = false;
		paramQuantities[RING_PARAM]->randomizeEnabled = false;
		for (int& s : sent)
			s = INT32_MIN;
	}

	/** The engine's knobs: 0..1 over the range the firmware gives the parameter, shown as the FM-1 writes it. */
	struct EQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Kurbel* m = dynamic_cast<Kurbel*>(module);
			if (m && m->core.loaded())
				return m->values[paramId - E_PARAM].data();
			return ParamQuantity::getDisplayValueString();
		}
		std::string getLabel() override {
			Kurbel* m = dynamic_cast<Kurbel*>(module);
			if (m && m->labels[paramId - E_PARAM][0])
				return m->labels[paramId - E_PARAM].data();
			return ParamQuantity::getLabel();
		}
	};
	struct RawQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Kurbel* m = dynamic_cast<Kurbel*>(module);
			if (m && m->core.loaded())
				return m->values[NE + paramId - ATK_PARAM].data();
			return ParamQuantity::getDisplayValueString();
		}
	};

	const uint32_t* knobMap(int engine) const { return engine == JKU_REED ? REED_E : WHEEL_E; }

	// ---------------------------------------------------------------------------------- power, presets
	void powerOn() {
		if (!core.load()) {
			status = core.error;
			WARN("KURBEL: %s", status.c_str());
			return;
		}
		status.clear();
		core.api->init(random::u32() | 1u);
		for (int& s : sent)
			s = INT32_MIN;
		outBuf.clear();
		src.reset();
		gate = false;
		int n = std::max(1, (int) core.api->npresets());
		preset = ((preset % n) + n) % n;
		if (!fromPatch)
			applyPreset(preset);
		else {
			core.api->load_preset(preset);
			engineNow = (int) core.api->engine();
		}
		shownPreset = preset;
	}

	/** The factory sound into the knobs (and the engine): its engine, drone, coup, ring. */
	void applyPreset(int i) {
		const jku_api* a = core.api;
		if (!a)
			return;
		int n = (int) a->npresets();
		preset = ((i % n) + n) % n;
		a->load_preset((uint32_t) preset);
		engineNow = (int) a->engine();
		if (engineNow == JKU_WHEEL)
			wheelPreset = preset;
		params[ENGINE_PARAM].setValue((float) engineNow);
		if (engineNow == JKU_WHEEL) {
			params[DRONE_PARAM].setValue((float) a->get(JKU_E0));
			params[COUP_PARAM].setValue((float) a->get(JKU_E5));
		}
		const uint32_t* map = knobMap(engineNow);
		for (int k = 0; k < NE; k++) {
			uint32_t j = JKU_E0 + map[k];
			int lo = a->min(j), hi = a->max(j), v = a->get(j);
			params[E_PARAM + k].setValue(hi > lo ? (float) (v - lo) / (hi - lo) : 0.f);
		}
		for (int i2 = 0; i2 < 4; i2++) {
			params[ATK_PARAM + i2].setValue((float) a->get(JKU_ATK + i2));
			params[DIST_PARAM + i2].setValue((float) a->get(JKU_DIST + i2));
		}
		params[RING_PARAM].setValue((float) a->get(JKU_RING));
		for (int& s : sent)
			s = INT32_MIN;
		shownPreset = preset;
	}

	void onAdd(const AddEvent& e) override {
		params[PREV_PARAM].setValue(0.f);
		params[NEXT_PARAM].setValue(0.f);
		powerOn();
	}
	void onRemove(const RemoveEvent& e) override { follow::gone(this); core.unload(); }
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		fromPatch = false;
		clockMode = 0;
		crankAlone = true;
		presetReq = 0;
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "preset", json_integer(preset));
		json_object_set_new(r, "wheelPreset", json_integer(wheelPreset));
		json_object_set_new(r, "clockMode", json_integer(clockMode));
		json_object_set_new(r, "crankAlone", json_boolean(crankAlone));
		fol.toJson(r);
		return r;
	}
	void dataFromJson(json_t* r) override {
		fol.fromJson(r);
		if (json_t* j = json_object_get(r, "preset"))
			preset = (int) json_integer_value(j);
		if (json_t* j = json_object_get(r, "wheelPreset"))
			wheelPreset = (int) json_integer_value(j);
		if (json_t* j = json_object_get(r, "clockMode"))
			clockMode = clamp((int) json_integer_value(j), 0, 3);
		if (json_t* j = json_object_get(r, "crankAlone"))
			crankAlone = json_boolean_value(j);
		fromPatch = true;
		if (core.api) {
			int n = std::max(1, (int) core.api->npresets());
			preset = ((preset % n) + n) % n;
			core.api->load_preset(preset);
			engineNow = (int) core.api->engine();
			for (int& s : sent)
				s = INT32_MIN;
			shownPreset = preset;
		}
	}

	// ---------------------------------------------------------------------------------- the engine
	void push(const jku_api* a, uint32_t j, int v) {
		if (sent[j] != v) {
			a->set(j, v);
			sent[j] = v;
		}
	}

	/** WHEEL / REED picked on the panel: the engine's sound (REED: the shawm; WHEEL: the last wheel sound). */
	void processEngine(const jku_api* a) {
		int want = params[ENGINE_PARAM].getValue() > 0.5f ? JKU_REED : JKU_WHEEL;
		if (want == engineNow)
			return;
		int n = (int) a->npresets(), pick = wheelPreset;
		if (want == JKU_REED)
			for (int i = 0; i < n; i++)
				if (a->preset_engine((uint32_t) i) == JKU_REED) {
					pick = i;
					break;
				}
		bool wasOn = gate;
		a->all_off();
		applyPreset(pick);
		if (wasOn)
			a->note_on(note, 100);
	}

	void processKnobs(const jku_api* a) {
		const uint32_t* map = knobMap(engineNow);
		for (int k = 0; k < NE; k++) {
			uint32_t j = JKU_E0 + map[k];
			float x = params[E_PARAM + k].getValue();
			if (inputs[E_INPUT + k].isConnected())
				x += inputs[E_INPUT + k].getVoltage() / 10.f;
			x = clamp(x, 0.f, 1.f);
			int lo = a->min(j), hi = a->max(j);
			push(a, j, lo + (int) std::round(x * (hi - lo)));
		}
		int drone = (int) std::round(params[DRONE_PARAM].getValue());
		if (engineNow == JKU_WHEEL) {
			push(a, JKU_E0, drone);
			push(a, JKU_E5, (int) std::round(params[COUP_PARAM].getValue()));
		}
		push(a, JKU_ROOT, drone);                  // the temperament counts from the drone
		push(a, JKU_TEMP, (int) std::round(params[TEMP_PARAM].getValue()));
		for (int i = 0; i < 4; i++) {
			push(a, JKU_ATK + i, (int) std::round(params[ATK_PARAM + i].getValue()));
			push(a, JKU_DIST + i, (int) std::round(params[DIST_PARAM + i].getValue()));
		}
		push(a, JKU_LEVEL, (int) std::round(params[LEVEL_PARAM].getValue()));
		push(a, JKU_RING, params[RING_PARAM].getValue() > 0.5f ? 1 : 0);
		lights[RING_LIGHT].setBrightness(params[RING_PARAM].getValue() > 0.5f ? 1.f : 0.f);
	}

	/** One melody string: the first channel of V/OCT and GATE; a new pitch under a held gate slides (legato). */
	void processNotes(const jku_api* a) {
		bool on = false;
		int nn = clamp((int) std::round(inputs[VOCT_INPUT].getVoltage() * 12.f) + 60, 0, 127);
		if (inputs[GATE_INPUT].isConnected()) {
			float g = inputs[GATE_INPUT].getVoltage();
			on = gate ? g > 0.5f : g >= 1.f;
		} else if (crankAlone && engineNow == JKU_WHEEL && params[COUP_PARAM].getValue() > 4.5f) {
			// no gate: the crank turning plays, as on a hurdy-gurdy: the drones and the melody string, on the DRONE's
			// note (C3 ..) or on V/OCT's
			on = rpmNow() >= (gate ? 3.f : 6.f);
			if (!inputs[VOCT_INPUT].isConnected())
				nn = 48 + clamp((int) std::round(params[DRONE_PARAM].getValue()), 0, 11);
		}
		if (on && !gate) {
			int vel = 100;
			if (inputs[VEL_INPUT].isConnected())
				vel = clamp((int) std::round(inputs[VEL_INPUT].getVoltage() / 10.f * 127.f), 1, 127);
			a->note_on(nn, vel);
			note = nn;
		} else if (!on && gate) {
			a->note_off(note);
		} else if (on && nn != note) {
			a->note_on(nn, 100);
			a->note_off(note);
			note = nn;
		}
		gate = on;
	}

	/** CLOCK into the tempo (COUP's strokes in note values); RESET: a held note starts its strokes again. */
	void processClock(const jku_api* a, float sampleRate) {
		int ppqn = CLOCK_PPQN[clamp(clockMode, 0, 3)];
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			clockTrig.reset();
			if (gate)
				a->note_on(note, 100);              // (the same note again: the strokes from now)
		}
		sincePulse += 1.0;
		if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (sincePulse < sampleRate * 4.0) {
				period = period > 0.0 ? period + (sincePulse - period) * 0.5 : sincePulse;
				bpm = clamp((float) (60.0 * sampleRate / (period * ppqn)), 20.f, 300.f);
			}
			sincePulse = 0.0;
		}
		clocked = period > 0.0 && sincePulse < std::min(period * 2.5, (double) sampleRate * 4.0);
		if (buzzTrig.process(inputs[BUZZ_INPUT].getVoltage(), 0.1f, 1.f))
			a->stroke();
	}

	float rpmNow() {
		float r = params[CRANK_PARAM].getValue();
		if (inputs[CRANK_INPUT].isConnected())
			r += inputs[CRANK_INPUT].getVoltage() * RPM_PER_VOLT;
		return clamp(r, 0.f, 480.f);
	}

	void renderBlock(const jku_api* a, float sampleRate) {
		processKnobs(a);
		int devRate = (int) a->sample_rate();
		a->set_bpm((uint32_t) std::round(bpm));
		float rpm = rpmNow();
		a->crank(rpm * CLICKS_PER_REV / 60.f, (float) BLOCK / devRate);
		a->render(BLOCK, devL, devR);
		dsp::Frame<2> in[BLOCK];
		for (int i = 0; i < BLOCK; i++) {
			in[i].samples[0] = devL[i];
			in[i].samples[1] = devR[i];
		}
		src.process(in, BLOCK, devRate, (int) sampleRate, outBuf);
		framesSinceUi += BLOCK;
		if (framesSinceUi >= (uint32_t) devRate / 30) {
			framesSinceUi = 0;
			const uint32_t* map = knobMap(engineNow);
			for (int k = 0; k < NE; k++) {
				uint32_t j = JKU_E0 + map[k];
				std::snprintf(labels[k].data(), labels[k].size(), "%s", a->label(j));
				a->format(j, a->get(j), values[k].data(), (uint32_t) values[k].size());
			}
			for (uint32_t j = JKU_ATK; j <= JKU_LEVEL; j++)
				a->format(j, a->get(j), values[NE + j - JKU_ATK].data(), (uint32_t) values[NE + j - JKU_ATK].size());
			sounding = a->active_voices() > 0;
			shownRpm = rpm;
			shownBpm = bpm;
			shownStrokes = a->strokes();
		}
	}

	void onPortChange(const PortChangeEvent& e) override { fol.portChange(e); }

	void process(const ProcessArgs& args) override {
		fol.process(this, args.frame);
		const jku_api* a = core.api;
		if (!a) {
			outputs[L_OUTPUT].setVoltage(0.f);
			outputs[R_OUTPUT].setVoltage(0.f);
			return;
		}
		int req = presetReq.exchange(-1);
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
			bool wasOn = gate;
			a->all_off();
			applyPreset(req);
			if (wasOn)
				a->note_on(note, 100);
		}
		processEngine(a);
		processClock(a, args.sampleRate);
		processNotes(a);
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
};

// ------------------------------------------------------------------------------------------------ the panel (GENRA look)
namespace L = klayout::kurbel;
static const NVGcolor ACCENT = nvgRGB(KURBEL_RGB);

/** The wheel and the strings: the wheel turns with the crank (COUP CRANK) or evenly while a note sounds; the
 * melody string glows while it sounds, the trompette flashes on a stroke. */
struct ScreenWidget : Widget {
	Kurbel* module = nullptr;
	float angle = 0.f, flash = 0.f;
	uint32_t lastStrokes = 0;
	double lastT = 0.0;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		if (module && !module->status.empty()) {
			jiant::drawStatus(vg, module->status, box.size.x);
			return;
		}
		double t = system::getTime();
		float dt = lastT > 0.0 ? (float) std::min(0.1, t - lastT) : 0.f;
		lastT = t;
		bool on = module && module->sounding;
		bool crankMode = module && module->params[Kurbel::COUP_PARAM].getValue() > 4.5f &&
		                 module->params[Kurbel::ENGINE_PARAM].getValue() < 0.5f;
		float rpm = module ? (crankMode ? (float) module->shownRpm : (on ? 60.f : 0.f)) : 30.f;
		angle += rpm / 60.f * 2.f * 3.14159265f * dt;
		if (module) {
			uint32_t s = module->shownStrokes;
			if (s != lastStrokes) {
				lastStrokes = s;
				flash = 1.f;
			}
		}
		flash = std::max(0.f, flash - dt * 4.f);
		float h = box.size.y, cx = 44.f, cy = h / 2, r = 34.f;
		NVGcolor dimc = ACCENT;
		dimc.a = 0.35f;
		// the strings
		const float ys[4] = {-18.f, -6.f, 6.f, 18.f};
		for (int i = 0; i < 4; i++) {
			NVGcolor c = ACCENT;
			c.a = i == 1 ? (on ? 1.f : 0.45f) : (i == 3 ? 0.5f + 0.5f * flash : 0.5f);
			nvgBeginPath(vg);
			float wob = (i == 1 && on) ? 1.2f * std::sin(t * 90.0) : 0.f;
			nvgMoveTo(vg, 6, cy + ys[i]);
			nvgQuadTo(vg, box.size.x / 2, cy + ys[i] + wob, box.size.x - 6, cy + ys[i]);
			nvgStrokeColor(vg, c);
			nvgStrokeWidth(vg, i == 1 ? 1.8f : (i == 3 ? 1.f + 1.5f * flash : 1.f));
			nvgStroke(vg);
		}
		// the wheel
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy, r);
		nvgStrokeColor(vg, ACCENT);
		nvgStrokeWidth(vg, 2.f);
		nvgStroke(vg);
		for (int i = 0; i < 8; i++) {
			float a = angle + i * 3.14159265f / 4;
			nvgBeginPath(vg);
			nvgMoveTo(vg, cx, cy);
			nvgLineTo(vg, cx + std::cos(a) * r, cy + std::sin(a) * r);
			nvgStrokeColor(vg, dimc);
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);
		}
		// the crank's handle
		float ha = angle;
		nvgBeginPath(vg);
		nvgMoveTo(vg, cx, cy);
		nvgLineTo(vg, cx + std::cos(ha) * (r + 8), cy + std::sin(ha) * (r + 8));
		nvgStrokeColor(vg, ACCENT);
		nvgStrokeWidth(vg, 2.f);
		nvgStroke(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, cx + std::cos(ha) * (r + 8), cy + std::sin(ha) * (r + 8), 3.f);
		nvgFillColor(vg, ACCENT);
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy, 3.5f);
		nvgFillColor(vg, ACCENT);
		nvgFill(vg);
	}
};

/** Right of the wheel: the preset, engine · drone · temperament · ring, the tempo and strokes, the first knobs. */
struct InfoWidget : Widget {
	Kurbel* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		auto bold = jiant::boldFont(), mono = jiant::monoFont();
		float w = box.size.x;
		const jku_api* a = module ? module->core.api : nullptr;
		int p = module ? (int) module->shownPreset : 0;
		bool reed = module && module->params[Kurbel::ENGINE_PARAM].getValue() > 0.5f;
		int drone = module ? clamp((int) std::round(module->params[Kurbel::DRONE_PARAM].getValue()), 0, 11) : 2;
		int temp = module ? clamp((int) std::round(module->params[Kurbel::TEMP_PARAM].getValue()), 0, 3) : 0;
		int coup = module ? clamp((int) std::round(module->params[Kurbel::COUP_PARAM].getValue()), 0, 5) : 2;
		bool ring = module && module->params[Kurbel::RING_PARAM].getValue() > 0.5f;
		jiant::text(vg, bold, 0, 20, a ? a->preset_name((uint32_t) p) : "HURDY GURDY", 15.f, jiant::WHITE, NVG_ALIGN_LEFT, 0.6f);
		jiant::text(vg, bold, w, 20, string::f("\xc2\xb7 %d/%u", p + 1, a ? a->npresets() : 8u), 6.5f, jiant::DIM,
		            NVG_ALIGN_RIGHT, 0.8f);
		jiant::text(vg, mono, 0, 33, string::f("%s \xc2\xb7 DRONE %s \xc2\xb7 %s%s", reed ? "REED" : "WHEEL",
		                                       DRONE_NAMES[drone], TEMP_NAMES[temp], ring ? " \xc2\xb7 RING" : ""),
		            7.f, jiant::DIM, NVG_ALIGN_LEFT, 0.f);
		if (module) {
			std::string s = reed ? string::f("TEMPO %d BPM", (int) std::round((float) module->shownBpm))
			              : coup == 5 ? string::f("CRANK %d rpm \xc2\xb7 STROKE ON A RUCK", (int) std::round((float) module->shownRpm))
			                          : string::f("TEMPO %d BPM \xc2\xb7 STROKES %s", (int) std::round((float) module->shownBpm),
			                                      COUP_NAMES[coup]);
			jiant::text(vg, mono, 0, 71, s, 7.f, jiant::DIM, NVG_ALIGN_LEFT, 0.f);
		}
		if (module && module->core.loaded()) {
			std::string s;
			for (int k = 0; k < 3; k++)
				s += string::f("%s %s  ", module->labels[k].data(), module->values[k].data());
			jiant::text(vg, mono, 0, 92, s, 6.6f, ACCENT, NVG_ALIGN_LEFT, 0.f);
		}
	}
};

/** A choice of a switch parameter as a field with its value and ▾; a click opens the list. */
struct SelectField : app::ParamWidget {
	std::vector<std::string> names;
	std::function<bool()> enabled;
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			engine::ParamQuantity* pq = getParamQuantity();
			if (pq && (!enabled || enabled())) {
				ui::Menu* menu = createMenu();
				menu->addChild(createMenuLabel(pq->getLabel()));
				for (size_t i = 0; i < names.size(); i++) {
					menu->addChild(createCheckMenuItem(names[i], "",
					                                   [=]() { return (int) std::round(pq->getValue()) == (int) i; },
					                                   [=]() { pq->setValue((float) i); }));
				}
			}
			e.consume(this);
			return;
		}
		ParamWidget::onButton(e);
	}
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			engine::ParamQuantity* pq = getParamQuantity();
			int v = pq ? (int) std::round(pq->getValue()) : 0;
			bool en = !enabled || enabled();
			jiant::roundRect(args.vg, 0, 0, box.size.x, box.size.y, 2.2f, jiant::FIELD);
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0.4f, 0.4f, box.size.x - 0.8f, box.size.y - 0.8f, 2.2f);
			nvgStrokeColor(args.vg, jiant::EDGE);
			nvgStrokeWidth(args.vg, 0.8f);
			nvgStroke(args.vg);
			std::string s = (v >= 0 && v < (int) names.size() ? names[v] : "?") + " \xe2\x96\xbe";
			jiant::text(args.vg, jiant::boldFont(), box.size.x / 2, box.size.y / 2 + 2.3f, s, 6.4f,
			            en ? jiant::TXT : jiant::DIM, NVG_ALIGN_CENTER, 0.4f);
		}
		ParamWidget::drawLayer(args, layer);
	}
};

/** The crank's speed as a number, next to its knob. */
struct RpmText : TransparentWidget {
	Kurbel* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		float rpm = module ? (float) module->shownRpm : 90.f;
		jiant::text(args.vg, jiant::monoFont(), box.size.x / 2, 8, string::f("%d", (int) std::round(rpm)), 8.f, ACCENT,
		            NVG_ALIGN_CENTER, 0.f);
		jiant::text(args.vg, jiant::monoFont(), box.size.x / 2, 15, "rpm", 5.5f, jiant::DIM, NVG_ALIGN_CENTER, 0.f);
	}
};

struct KurbelWidget : ModuleWidget {
	KurbelWidget(Kurbel* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Kurbel.svg")));
		follow::addStrip(this, module, {jiant::at(L::VOCT), jiant::at(L::GATE), jiant::at(L::VEL), jiant::at(L::CLOCK),
		                                jiant::at(L::RESET)});
		jiant::addScrews(this);

		ScreenWidget* scr = new ScreenWidget;
		scr->module = module;
		scr->box = jiant::area(L::SCREEN);
		addChild(scr);
		InfoWidget* info = new InfoWidget;
		info->module = module;
		info->box = jiant::area(L::INFO);
		addChild(info);
		jiant::ChipRow* eng = createParam<jiant::ChipRow>(Vec(L::INFO[0], L::INFO[1] + 41), module, Kurbel::ENGINE_PARAM);
		eng->box.size = Vec(83, 14);
		eng->names = {"WHEEL", "REED"};
		eng->accent = ACCENT;
		addParam(eng);
		jiant::ChipRow* tmp = createParam<jiant::ChipRow>(Vec(L::INFO[0] + 88, L::INFO[1] + 41), module, Kurbel::TEMP_PARAM);
		tmp->box.size = Vec(L::INFO[2] - 88, 14);
		tmp->names = {"EQUAL", "PYTH", "MEAN", "JUST"};
		tmp->accent = ACCENT;
		addParam(tmp);

		addParam(createParamCentered<VCVBezel>(jiant::at(L::PREV), module, Kurbel::PREV_PARAM));
		addParam(createParamCentered<VCVBezel>(jiant::at(L::NEXT), module, Kurbel::NEXT_PARAM));
		addParam(createLightParamCentered<VCVLightLatch<jiant::AccentLight<KURBEL_RGB>>>(
		    jiant::at(L::RING), module, Kurbel::RING_PARAM, Kurbel::RING_LIGHT));
		SelectField* drone = createParam<SelectField>(Vec(L::DRONE_SEL[0], L::DRONE_SEL[1]), module, Kurbel::DRONE_PARAM);
		drone->box.size = Vec(L::DRONE_SEL[2], L::DRONE_SEL[3]);
		drone->names.assign(DRONE_NAMES, DRONE_NAMES + 12);
		addParam(drone);
		SelectField* coup = createParam<SelectField>(Vec(L::COUP_SEL[0], L::COUP_SEL[1]), module, Kurbel::COUP_PARAM);
		coup->box.size = Vec(L::COUP_SEL[2], L::COUP_SEL[3]);
		coup->names.assign(COUP_NAMES, COUP_NAMES + 6);
		coup->enabled = [=]() { return !module || module->params[Kurbel::ENGINE_PARAM].getValue() < 0.5f; };
		addParam(coup);
		addParam(createParamCentered<GSmallKnob>(jiant::at(L::CRANK), module, Kurbel::CRANK_PARAM));
		RpmText* rpm = new RpmText;
		rpm->module = module;
		rpm->box = jiant::area(L::CRANK_TEXT);
		addChild(rpm);
		addParam(createParamCentered<GSmallKnob>(jiant::at(L::LEVEL), module, Kurbel::LEVEL_PARAM));

		static const char* defLab[NE] = {"DLVL", "5TH", "WHL", "BUZZ", "BODY", "WOBL"};
		for (int k = 0; k < NE; k++) {
			addParam(createParamCentered<GSmallKnob>(jiant::at(L::E[k]), module, Kurbel::E_PARAM + k));
			addInput(createInputCentered<PJ301MPort>(jiant::at(L::E_IN[k]), module, Kurbel::E_INPUT + k));
			jiant::KnobText* t = new jiant::KnobText;
			t->box = jiant::area(L::E_TEXT[k]);
			t->accent = ACCENT;
			t->label = [=]() -> std::string {
				if (module && module->labels[k][0])
					return module->labels[k].data();
				return defLab[k];
			};
			t->value = [=]() -> std::string { return module && module->core.loaded() ? module->values[k].data() : ""; };
			addChild(t);
		}
		const float* env[4] = {L::ATK, L::DEC, L::SUS, L::REL};
		const float* fx[4] = {L::DIST, L::CHOR, L::DLY, L::REV};
		for (int i = 0; i < 4; i++) {
			addParam(createParamCentered<GTrimpot>(jiant::at(env[i]), module, Kurbel::ATK_PARAM + i));
			addParam(createParamCentered<GTrimpot>(jiant::at(fx[i]), module, Kurbel::DIST_PARAM + i));
		}
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::VOCT), module, Kurbel::VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::GATE), module, Kurbel::GATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::VEL), module, Kurbel::VEL_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::CLOCK), module, Kurbel::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::RESET), module, Kurbel::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::CRANK_IN), module, Kurbel::CRANK_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::BUZZ_IN), module, Kurbel::BUZZ_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(jiant::at(L::OUT_L), module, Kurbel::L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(jiant::at(L::OUT_R), module, Kurbel::R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		if (follow::Host* h = follow::host(module))
			h->fol.appendMenu(menu, module);
		Kurbel* m = getModule<Kurbel>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Clock input", {CLOCK_MODES[0], CLOCK_MODES[1], CLOCK_MODES[2], CLOCK_MODES[3]},
		                                      [=]() { return (size_t) m->clockMode; },
		                                      [=](size_t i) { m->clockMode = (int) i; }));
		menu->addChild(createBoolPtrMenuItem("Crank plays without a gate (COUP CRANK)", "", &m->crankAlone));
		if (!m->core.loaded())
			return;
		std::vector<std::string> names;
		for (uint32_t i = 0; i < m->core.api->npresets(); i++)
			names.push_back(m->core.api->preset_name(i));
		menu->addChild(createIndexSubmenuItem("Preset", names, [=]() { return (size_t) m->shownPreset; },
		                                      [=](size_t i) { m->presetReq = (int) i; }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Hurdy-gurdy: GHOULBOX's GURDY in ESTAMPIE (GPL-3.0); shawm: Felucca's TRIO (MIT)"));
	}
};

}  // namespace genra_kurbel

Model* modelKurbel = createModel<genra_kurbel::Kurbel, genra_kurbel::KurbelWidget>("Kurbel");
