// SPDX-License-Identifier: GPL-3.0-only
// FLUT – the tidal drone of GENRA NEON, in the NEON-TERRAIN look: a night sea, the moon rises and sinks with the tide,
// every voice is a long neon cloud that drifts over the water and glows brighter at high tide. VOICES (count, OCT,
// SPREAD, DRIFT, MORPH, SUB with -1 / -2 OCT), TIDE (SYNC / FREE, TIME, DEPTH, PHASE, SKEW), TEXTURE (GLASS, REED,
// CHOIR, BOWED, DUST; BRIGHT, GRIT, BREATH), SPACE (BLOOM, WIDTH, HAZE), SHAPE (SWELL, FADE, LEVEL), DRONE / GATE,
// FREEZE, NEW, UNDO, INIT. Outputs: TIDE (0–10 V), SUB, POLY (the six voices), L, R.
// Engine: FlutDsp.hpp; panel: tools/neon_panel.py (res/Flut.svg + FlutLayout.hpp).
#include "../plugin.hpp"
#include "../neon/ModuleKit.hpp"
#include "FlutDsp.hpp"
#include "FlutPresets.hpp"
#include "FlutLayout.hpp"
#include <memory>

namespace flut {

namespace L = fl;
using namespace neonui;
using namespace neonkit;

struct Flut : Module, follow::Host {
	enum ParamId {
		VOICES_PARAM, OCT_PARAM, SPRD_PARAM, DRIFT_PARAM, MORPH_PARAM, SUB_PARAM, SUBOCT_PARAM, TIDEMODE_PARAM, TIME_PARAM,
		DEPTH_PARAM, PHS_PARAM, SKEW_PARAM, TEXTURE_PARAM, BRIGHT_PARAM, GRIT_PARAM, BREATH_PARAM, BLOOM_PARAM, WIDTH_PARAM,
		HAZE_PARAM, SWELL_PARAM, FADE_PARAM, LEVEL_PARAM, PMODE_PARAM,
		NUM_PARAMS,
		PRESETSEL_PARAM = NUM_PARAMS,
		FREEZE_PARAM, NEW_PARAM, UNDO_PARAM, INIT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT, GATE_INPUT, VEL_INPUT, ACC_INPUT, SLIDE_INPUT, CLOCK_INPUT, RESET_INPUT, TNEW_INPUT, TFREEZE_INPUT,
		SPRD_INPUT, DRIFT_INPUT, MORPH_INPUT, SUB_INPUT, OCT_INPUT, TIME_INPUT, DEPTH_INPUT, INL_INPUT, INR_INPUT,
		PHS_INPUT, SKEW_INPUT, BRIGHT_INPUT, GRIT_INPUT, BREATH_INPUT, BLOOM_INPUT, WIDTH_INPUT, HAZE_INPUT, LEVEL_INPUT,
		INPUTS_LEN
	};
	enum OutputId { TIDE_OUTPUT, SUB_OUTPUT, POLY_OUTPUT, L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };
	static_assert((int) NUM_PARAMS == (int) NUM_PRESET_PARAMS, "presets must cover every knob");
	static_assert(INPUTS_LEN == L::NUM_JACK_IN, "the panel's jacks");
	static_assert(OUTPUTS_LEN == L::NUM_JACK_OUT, "the panel's outputs");

	/** TIME shows bars (SYNC) or seconds / minutes (FREE). */
	struct TimeKnob : ParamQuantity {
		std::string getDisplayValueString() override {
			int m = module ? (int) std::round(module->params[TIDEMODE_PARAM].getValue()) : 0;
			if (m == TM_SYNC) {
				int b = (int) TIDE_BARS[clamp(barsIndex(getValue()), 0, NUM_BARS - 1)];
				return b == 1 ? "1 bar" : string::f("%d bars", b);
			}
			float s = freeSeconds(getValue());
			return s < 60.f ? string::f("%.1f s", s) : string::f("%.1f min", s / 60.f);
		}
	};
	struct SlowQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			float s = slowTime(getValue());
			return s < 1.f ? string::f("%.0f ms", s * 1000.f) : string::f("%.1f s", s);
		}
	};
	struct MorphQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			float s = morphTime(getValue());
			return s < 1.f ? string::f("%.0f ms", s * 1000.f) : string::f("%.1f s", s);
		}
	};

	std::unique_ptr<Engine> engine{new Engine};
	Params p;
	Channel ch[Notes::MAXCH];
	neon::Tempo tempo;
	int ctl = 0, ppqn = 4;
	dsp::SchmittTrigger clockTrig, resetTrig, newTrig, freezeTrig, newBtn, undoBtn, initBtn;
	std::atomic<uint32_t> newReq{0}, undoReq{0}, initReq{0}, wantSeed{0};
	uint32_t doneN = 0, doneU = 0, doneI = 0, seed = 1;
	float flash = 0.f;
	float poly[MAXV] = {};
	Undo undo;

	genra_presets::Bank bank;
	void initPresets() {
		genra_presets::Factory f;
		f.count = NUM_PRESETS;
		f.style = [](int i) { return PRESETS[i].style; };
		f.name = [](int i) { return PRESETS[i].name; };
		f.info = [](int i) { return TEXTURE_NAMES[(int) PRESETS[i].v[P_TEXTURE]]; };
		f.values = [](int i, float* v) { PRESETS[i].values(v); };
		f.gain = [](int i) { return PRESETS[i].gain; };
		bank.sel = PRESETSEL_PARAM;
		bank.init(this, f, asset::user("GenraNeon/FlutPresets"));
	}

	Flut() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		genra_presel::config(this, PRESETSEL_PARAM);
		srcName = "FLUT";
		audioL = L_OUTPUT;
		audioR = R_OUTPUT;
		fol.bind(VOCT_INPUT, follow::VOCT);
		fol.bind(GATE_INPUT, follow::GATE);
		fol.bind(VEL_INPUT, follow::VEL);
		fol.bind(ACC_INPUT, follow::ACC);
		fol.bind(SLIDE_INPUT, follow::SLIDE);
		fol.bind(CLOCK_INPUT, follow::CLOCK);
		fol.bind(RESET_INPUT, follow::RESET);
		auto names = [](const char* const* a, int n) {
			std::vector<std::string> v;
			for (int i = 0; i < n; i++)
				v.push_back(a[i]);
			return v;
		};
		configParam(VOICES_PARAM, 1.f, (float) MAXV, 6.f, "Voices (the clouds)");
		paramQuantities[VOICES_PARAM]->snapEnabled = true;
		configParam(OCT_PARAM, -2.f, 2.f, 0.f, "Octave");
		paramQuantities[OCT_PARAM]->snapEnabled = true;
		configParam(SPRD_PARAM, 0.f, 1.f, 0.4f, "Spread (the voices over the octaves; NEW throws a new voicing)", "%", 0.f, 100.f);
		configParam(DRIFT_PARAM, 0.f, 1.f, 0.3f, "Drift (the voices wander against each other)", "%", 0.f, 100.f);
		configParam<MorphQuantity>(MORPH_PARAM, 0.f, 1.f, 0.4f, "Morph (how long they glide into a new chord)");
		configParam(SUB_PARAM, 0.f, 1.f, 0.3f, "Sub", "%", 0.f, 100.f);
		configSwitch(SUBOCT_PARAM, 0.f, 1.f, 0.f, "Sub octave", {"One octave below", "Two octaves below"});
		configSwitch(TIDEMODE_PARAM, 0.f, NUM_TIDEMODES - 1, 0.f, "Tide", names(TIDEMODE_NAMES, NUM_TIDEMODES));
		configParam<TimeKnob>(TIME_PARAM, 0.f, 1.f, BR_DEFAULT(), "Tide time");
		configParam(DEPTH_PARAM, 0.f, 1.f, 0.6f, "Depth (how much the tide changes the sound)", "%", 0.f, 100.f);
		configParam(PHS_PARAM, 0.f, 1.f, 0.f, "Phase (where the tide starts)", "%", 0.f, 100.f);
		configParam(SKEW_PARAM, -1.f, 1.f, 0.f, "Skew (fast flood, slow ebb .. slow flood, fast ebb)", "%", 0.f, 100.f);
		configSwitch(TEXTURE_PARAM, 0.f, NUM_TEXTURES - 1, 0.f, "Texture", names(TEXTURE_NAMES, NUM_TEXTURES));
		configParam(BRIGHT_PARAM, 0.f, 1.f, 0.5f, "Bright", "%", 0.f, 100.f);
		configParam(GRIT_PARAM, 0.f, 1.f, 0.f, "Grit (crackle)", "%", 0.f, 100.f);
		configParam(BREATH_PARAM, 0.f, 1.f, 0.1f, "Breath (air)", "%", 0.f, 100.f);
		configParam(BLOOM_PARAM, 0.f, 1.f, 0.3f, "Bloom (partials above, opening at high tide)", "%", 0.f, 100.f);
		configParam(WIDTH_PARAM, 0.f, 1.f, 0.6f, "Width", "%", 0.f, 100.f);
		configParam(HAZE_PARAM, 0.f, 1.f, 0.5f, "Haze (a long reverb)", "%", 0.f, 100.f);
		configParam<SlowQuantity>(SWELL_PARAM, 0.f, 1.f, 0.4f, "Swell (fade in)");
		configParam<SlowQuantity>(FADE_PARAM, 0.f, 1.f, 0.5f, "Fade (fade out, GATE)");
		configParam(LEVEL_PARAM, 0.f, 1.f, 0.8f, "Level", "%", 0.f, 100.f);
		configSwitch(PMODE_PARAM, 0.f, NUM_PLAYMODES - 1, 0.f, "Play mode", {"Drone (plays all the time)", "Gate (while a note is held)"});
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze: holds tide, drift and chord", {"Off", "On"});
		configButton(NEW_PARAM, "New: a new voicing");
		configButton(UNDO_PARAM, "Undo the last NEW / INIT");
		configButton(INIT_PARAM, "Init: back to a plain sound");
		for (int id : {FREEZE_PARAM, NEW_PARAM, UNDO_PARAM, INIT_PARAM})
			paramQuantities[id]->randomizeEnabled = false;
		const char* inNames[INPUTS_LEN] = {
		    "V/Oct (polyphonic: the chord; one note makes its own)", "Gate (polyphonic: which notes are held)", "Velocity",
		    "Accent", "Slide", "Clock (the tide in time; GENRA: 16ths)", "Reset (the tide from its start)", "New trigger",
		    "Freeze trigger (toggles)", "Spread CV", "Drift CV", "Morph CV", "Sub CV", "Octave CV (1 V a step)",
		    "Tide time CV", "Depth CV", "In L (into the haze, carried by the tide)", "In R", "Phase CV", "Skew CV",
		    "Bright CV", "Grit CV", "Breath CV", "Bloom CV", "Width CV", "Haze CV", "Level CV"};
		for (int i = 0; i < INPUTS_LEN; i++)
			configInput(i, inNames[i]);
		configOutput(TIDE_OUTPUT, "Tide (0–10 V: low .. high)");
		configOutput(SUB_OUTPUT, "Sub (alone)");
		configOutput(POLY_OUTPUT, "Poly: the voices, one a channel");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		initPresets();
	}
	static float BR_DEFAULT() { return 5.f / (NUM_BARS - 1); }

	void onAdd(const AddEvent& e) override { engine->setSampleRate(APP->engine->getSampleRate()); }
	void onSampleRateChange(const SampleRateChangeEvent& e) override { engine->setSampleRate(e.sampleRate); }
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		engine->allOff();
		bank.reset();
	}
	void onRemove(const RemoveEvent& e) override { follow::gone(this); }
	void onPortChange(const PortChangeEvent& e) override { fol.portChange(e); }

	void process(const ProcessArgs& args) override {
		fol.process(this, args.frame);
		bool reset = resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		bool tick = clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		tempo.step(args.sampleTime, tick, reset, inputs[CLOCK_INPUT].isConnected(), ppqn);
		if (newTrig.process(inputs[TNEW_INPUT].getVoltage(), 0.1f, 1.f) | newBtn.process(params[NEW_PARAM].getValue()))
			newReq++;
		if (undoBtn.process(params[UNDO_PARAM].getValue()))
			undoReq++;
		if (initBtn.process(params[INIT_PARAM].getValue()))
			initReq++;
		if (freezeTrig.process(inputs[TFREEZE_INPUT].getVoltage(), 0.1f, 1.f))
			params[FREEZE_PARAM].setValue(params[FREEZE_PARAM].getValue() > 0.5f ? 0.f : 1.f);
		uint32_t ws = wantSeed.exchange(0);
		if (ws)
			engine->voicing(ws);
		if (--ctl <= 0) {
			ctl = 16;
			p.voices = clamp((int) std::round(params[VOICES_PARAM].getValue()), 1, MAXV);
			p.oct = clamp(std::round(params[OCT_PARAM].getValue() + inputs[OCT_INPUT].getVoltage()), -3.f, 3.f);
			p.spread = kv(this, SPRD_PARAM, SPRD_INPUT);
			p.drift = kv(this, DRIFT_PARAM, DRIFT_INPUT);
			p.morph = kv(this, MORPH_PARAM, MORPH_INPUT);
			p.sub = kv(this, SUB_PARAM, SUB_INPUT);
			p.subOct = params[SUBOCT_PARAM].getValue() > 0.5f ? 2 : 1;
			p.tideMode = clamp((int) std::round(params[TIDEMODE_PARAM].getValue()), 0, NUM_TIDEMODES - 1);
			p.time = kv(this, TIME_PARAM, TIME_INPUT);
			p.depth = kv(this, DEPTH_PARAM, DEPTH_INPUT);
			p.phase = kv(this, PHS_PARAM, PHS_INPUT);
			p.skew = kv(this, SKEW_PARAM, SKEW_INPUT, -1.f, 1.f);
			p.texture = clamp((int) std::round(params[TEXTURE_PARAM].getValue()), 0, NUM_TEXTURES - 1);
			p.bright = kv(this, BRIGHT_PARAM, BRIGHT_INPUT);
			p.grit = kv(this, GRIT_PARAM, GRIT_INPUT);
			p.breath = kv(this, BREATH_PARAM, BREATH_INPUT);
			p.bloom = kv(this, BLOOM_PARAM, BLOOM_INPUT);
			p.width = kv(this, WIDTH_PARAM, WIDTH_INPUT);
			p.haze = kv(this, HAZE_PARAM, HAZE_INPUT);
			p.swell = params[SWELL_PARAM].getValue();
			p.fade = params[FADE_PARAM].getValue();
			p.level = kv(this, LEVEL_PARAM, LEVEL_INPUT);
			p.playMode = clamp((int) std::round(params[PMODE_PARAM].getValue()), 0, NUM_PLAYMODES - 1);
			p.outGain = bank.gain;
			p.freeze = params[FREEZE_PARAM].getValue() > 0.5f;
		}
		int n = std::min(std::max(inputs[VOCT_INPUT].getChannels(), inputs[GATE_INPUT].getChannels()), (int) Notes::MAXCH);
		bool gatePatched = inputs[GATE_INPUT].isConnected();
		bool any = false;
		for (int c = 0; c < n; c++) {
			ch[c].voct = inputs[VOCT_INPUT].getPolyVoltage(c);
			float g = inputs[GATE_INPUT].getPolyVoltage(c);
			ch[c].gate = gatePatched ? (g >= 1.f ? true : (g <= 0.1f ? false : ch[c].gate)) : true;  // no GATE: V/OCT is the chord
			any = any || ch[c].gate;
		}
		if (n == 0)
			any = !gatePatched;
		float l, r, tide, sub;
		engine->process(p, ch, n, any, tempo.bpm, reset, inputs[INL_INPUT].getVoltage() * 0.1f,
		                (inputs[INR_INPUT].isConnected() ? inputs[INR_INPUT] : inputs[INL_INPUT]).getVoltage() * 0.1f, l, r, tide,
		                sub, poly);
		outputs[L_OUTPUT].setVoltage(outputs[R_OUTPUT].isConnected() || pulled(args.frame) ? l : 0.5f * (l + r));
		outputs[R_OUTPUT].setVoltage(r);
		outputs[TIDE_OUTPUT].setVoltage(tide);
		outputs[SUB_OUTPUT].setVoltage(sub);
		outputs[POLY_OUTPUT].setChannels(p.voices);
		for (int i = 0; i < p.voices; i++)
			outputs[POLY_OUTPUT].setVoltage(poly[i], i);
		if (flash > 0.f)
			flash -= args.sampleTime;
	}

	void uiStep() {
		uint32_t nw = newReq.load(), u = undoReq.load(), in = initReq.load();
		if (nw != doneN) {
			doneN = nw;
			undo.push(this);
			seed = 1u + random::u32() % 99999u;
			wantSeed.store(seed);
			flash = 0.3f;
		}
		if (u != doneU) {
			doneU = u;
			undo.pop(this);
		}
		if (in != doneI) {
			doneI = in;
			undo.push(this);
			for (int i = 0; i < NUM_PARAMS; i++)
				params[i].setValue(paramQuantities[i]->getDefaultValue());
			params[FREEZE_PARAM].setValue(0.f);
			bank.reset();
			seed = 1;
			wantSeed.store(seed);
		}
	}

	int hostPresetCount() override { return bank.count(); }
	void hostPresetInfo(int i, char* name, int nl, char* style, int sl) override {
		follow::putStr(name, nl, bank.nameOf(i).c_str());
		follow::putStr(style, sl, i >= 0 && i < bank.f.count && bank.f.style ? bank.f.style(i) : "");
	}
	int hostPresetCurrent() override { return bank.preset; }
	void hostPresetLoad(int i) override { bank.load(i); }

	json_t* dataToJson() override {
		json_t* j = json_object();
		json_object_set_new(j, "ppqn", json_integer(ppqn));
		json_object_set_new(j, "voicing", json_integer(seed));
		bank.toJson(j);
		fol.toJson(j);
		return j;
	}
	void dataFromJson(json_t* j) override {
		bank.selCode++;
		fol.fromJson(j);
		if (json_t* v = json_object_get(j, "ppqn"))
			ppqn = std::max(1, (int) json_integer_value(v));
		if (json_t* v = json_object_get(j, "voicing")) {
			seed = std::max<uint32_t>(1u, (uint32_t) json_integer_value(v));
			wantSeed.store(seed);
		}
		bank.fromJson(j);
	}
};

/** The display: a night sea; the moon shows the tide, the clouds are the voices. */
struct Display : Widget {
	Flut* module = nullptr;
	float t = 0.f, drift[MAXV] = {};
	void step() override {
		Widget::step();
		t += 1.f / 60.f;
		if (module)
			module->uiStep();
		for (int i = 0; i < MAXV; i++)
			drift[i] += (2.5f + 1.3f * ((i * 7) % 5)) / 60.f * (module && module->p.freeze ? 0.f : 1.f);
	}
	void cloud(NVGcontext* vg, float x, float y, float s, NVGcolor col, float glow) {
		static const float SH[8][4] = {{0, 1, 16, 5}, {20, -2, 22, 7}, {46, -4, 26, 8}, {74, -2, 24, 7},
		                               {100, 0, 20, 6}, {122, 2, 14, 4}, {34, 3, 34, 5}, {84, 3, 32, 5}};
		nvgBeginPath(vg);
		for (auto& e : SH)
			nvgEllipse(vg, x + e[0] * s, y + e[1] * s, (e[2] + 1.6f) * s, e[3] * s + 1.4f);
		nvgFillColor(vg, alpha(col, glow));
		nvgFill(vg);
		nvgBeginPath(vg);
		for (auto& e : SH)
			nvgEllipse(vg, x + e[0] * s, y + e[1] * s, e[2] * s, e[3] * s);
		nvgFillColor(vg, nvgRGB(0x15, 0x0b, 0x26));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgEllipse(vg, x + 56 * s, y - 8 * s, 36 * s, 1.6f);
		nvgFillColor(vg, alpha(col, 0.18f + 0.2f * glow));
		nvgFill(vg);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		float W = box.size.x, H = box.size.y;
		nvgSave(vg);
		nvgScissor(vg, 2, 2, W - 4, H - 4);
		float tide = module ? module->engine->tide : 0.5f + 0.5f * std::sin(t * 0.6f);
		for (int i = 0; i < 20; i++) {
			nvgBeginPath(vg);
			nvgCircle(vg, std::fmod(i * 113.f, W), 3.f + std::fmod(i * 41.f, H * 0.5f), 0.7f);
			nvgFillColor(vg, alpha(WHITE, 0.4f + 0.2f * std::sin(t * 2.f + i)));
			nvgFill(vg);
		}
		// the moon: high at high tide
		float mx = W * 0.78f, my = H * (0.62f - 0.42f * tide), mr = H * 0.11f;
		nvgBeginPath(vg);
		nvgCircle(vg, mx, my, mr * (1.6f + 0.5f * tide));
		nvgFillColor(vg, alpha(CYAN, 0.06f + 0.12f * tide));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, mx, my, mr);
		nvgFillColor(vg, nvgRGB(0xbf, 0xf7, 0xfb));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, mx + mr * 0.5f, my - mr * 0.25f, mr * 0.93f);
		nvgFillColor(vg, nvgRGB(8, 4, 15));
		nvgFill(vg);
		// the clouds: one a voice, brighter as the voice sounds
		int nv = module ? module->p.voices : MAXV;
		for (int i = 0; i < MAXV; i++) {
			if (i >= nv)
				continue;
			float lev = module ? clampf(module->engine->v[i].lev, 0.f, 1.2f) : 0.6f + 0.4f * tide;
			float f = (float) i / (MAXV - 1);
			NVGcolor col = nvgLerpRGBA(CYAN, PINK, f);
			float s = (0.55f + 0.25f * ((i * 3) % 4) / 3.f) * W / 420.f * 1.6f;
			float span = W + 150.f * s * 1.6f;
			float x = std::fmod(drift[i] + i * 97.f, span) - 140.f * s;
			float y = H * (0.14f + 0.075f * ((i * 5) % 6)) - tide * H * 0.04f;
			cloud(vg, x, y, s, col, clampf(0.25f + 0.7f * lev, 0.f, 1.f));
		}
		// the sea: back pink, front cyan, rougher at high tide
		float level = H * (0.86f - 0.1f * tide);
		const int SL = 5;
		for (int k = 0; k < SL; k++) {
			float f = (float) k / (SL - 1);
			float y0 = level + f * H * 0.16f, amp = (1.f + 2.5f * tide) * (0.5f + f);
			nvgBeginPath(vg);
			nvgMoveTo(vg, 0, H);
			for (int i = 0; i <= 60; i++) {
				float x = W * i / 60.f;
				float y = y0 + std::sin(x * 0.045f + t * (0.6f + f * 0.5f) + f * 3.f) * amp + std::sin(x * 0.11f - t * 0.9f) * amp * 0.35f;
				nvgLineTo(vg, x, y);
			}
			nvgLineTo(vg, W, H);
			nvgClosePath(vg);
			nvgFillColor(vg, nvgRGB(8, 4, 15));
			nvgFill(vg);
			nvgBeginPath(vg);
			for (int i = 0; i <= 60; i++) {
				float x = W * i / 60.f;
				float y = y0 + std::sin(x * 0.045f + t * (0.6f + f * 0.5f) + f * 3.f) * amp + std::sin(x * 0.11f - t * 0.9f) * amp * 0.35f;
				if (i == 0)
					nvgMoveTo(vg, x, y);
				else
					nvgLineTo(vg, x, y);
			}
			nvgStrokeColor(vg, alpha(nvgLerpRGBA(PINK, CYAN, f), 0.35f + 0.65f * f));
			nvgStrokeWidth(vg, 0.6f + 1.2f * f);
			nvgStroke(vg);
		}
		// the moon on the water
		for (int i = 0; i < 6; i++) {
			float w = (16.f - i * 2.f + std::sin(t * 2.f + i) * 3.f) * W / 340.f;
			nvgBeginPath(vg);
			nvgRect(vg, mx - w / 2 + std::sin(t * 1.3f + i) * 2.f, level + 3.f + i * 4.f, std::max(3.f, w), 1.2f);
			nvgFillColor(vg, alpha(nvgRGB(0xbf, 0xf7, 0xfb), (0.2f + 0.5f * tide) * (1.f - i / 7.f)));
			nvgFill(vg);
		}
		scanlines(vg, W, H);
		nvgResetScissor(vg);
		std::string l1 = "GLASS · 6 VOICES", l2 = "TIDE 32 BARS · DEPTH 60 %", big = "C3", small = "FREE · 120 BPM";
		if (module) {
			const Params& p = module->p;
			l1 = string::f("%s · %d VOICES", TEXTURE_NAMES[p.texture], p.voices);
			Flut::TimeKnob* tk = dynamic_cast<Flut::TimeKnob*>(module->paramQuantities[Flut::TIME_PARAM]);
			l2 = string::f("TIDE %s · %d %% · DEPTH %d %%", tk ? string::uppercase(tk->getDisplayValueString()).c_str() : "",
			               (int) std::round(tide * 100.f), (int) std::round(p.depth * 100.f));
			big = noteName(module->engine->lastNoteShow);
			small = followLine(module, module->inputs[Flut::CLOCK_INPUT].isConnected(), module->tempo.bpm);
			if (p.freeze)
				small += " · FREEZE";
		}
		corners(vg, W, l1, l2, big, small);
		nvgRestore(vg);
		Widget::drawLayer(args, layer);
	}
};

struct FlutWidget : ModuleWidget {
	FlutWidget(Flut* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Flut.svg")));
		follow::knownHost(module);
		using M = Flut;
		Display* d = new Display;
		d->module = module;
		d->box.pos = mm2px(Vec(L::DISPLAY[0], L::DISPLAY[1]));
		d->box.size = mm2px(Vec(L::DISPLAY[2], L::DISPLAY[3]));
		addChild(d);
		presetField(this, module, L::PRESETF, module ? &module->bank : nullptr, M::PRESETSEL_PARAM, PRESETS[0].name);
		field(this, module, L::TIDEF, M::TIDEMODE_PARAM, NUM_TIDEMODES, TIDEMODE_NAMES, TIDEMODE_INFO, "Tide");
		field(this, module, L::TEXF, M::TEXTURE_PARAM, NUM_TEXTURES, TEXTURE_NAMES, TEXTURE_INFO, "Texture");
		knob(this, module, L::VOICES, M::VOICES_PARAM);
		knob(this, module, L::OCT, M::OCT_PARAM);
		knob(this, module, L::SPRD, M::SPRD_PARAM);
		knob(this, module, L::DRIFT, M::DRIFT_PARAM);
		knob(this, module, L::MORPH, M::MORPH_PARAM);
		knob(this, module, L::SUB, M::SUB_PARAM);
		knob(this, module, L::TIME, M::TIME_PARAM);
		knob(this, module, L::DEPTH, M::DEPTH_PARAM);
		knob(this, module, L::PHS, M::PHS_PARAM);
		knob(this, module, L::SKEW, M::SKEW_PARAM);
		knob(this, module, L::BRIGHT, M::BRIGHT_PARAM);
		knob(this, module, L::GRIT, M::GRIT_PARAM);
		knob(this, module, L::BREATH, M::BREATH_PARAM);
		knob(this, module, L::BLOOM, M::BLOOM_PARAM);
		knob(this, module, L::WIDTH, M::WIDTH_PARAM);
		knob(this, module, L::HAZE, M::HAZE_PARAM);
		knob(this, module, L::SWELL, M::SWELL_PARAM);
		knob(this, module, L::FADE, M::FADE_PARAM);
		knob(this, module, L::LEVEL, M::LEVEL_PARAM);
		lamps(this, module, L::SUBOCT, M::SUBOCT_PARAM, 2, CYAN);
		lamps(this, module, L::PMODE, M::PMODE_PARAM, NUM_PLAYMODES, CYAN);
		button(this, module, L::FREEZE, M::FREEZE_PARAM, true, CYAN);
		NButton* nb = button(this, module, L::NEW, M::NEW_PARAM, false, PINK);
		nb->lit = [=]() { return module && module->flash > 0.f; };
		button(this, module, L::UNDO, M::UNDO_PARAM, false, VIOLET);
		button(this, module, L::INIT, M::INIT_PARAM, false, VIOLET);
		jacks(this, module, L::JACK_IN, L::JACK_IN_COL, L::JACK_OUT);
	}
	void appendContextMenu(Menu* menu) override {
		if (follow::Host* h = follow::host(module))
			h->fol.appendMenu(menu, module);
		Flut* m = dynamic_cast<Flut*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		genra_presets::appendMenu(menu, &m->bank);
		menu->addChild(new MenuSeparator);
		clockMenu(menu, &m->ppqn);
		menu->addChild(createMenuLabel("Follow GENRA's pads: FLUT takes the chord and keeps it while nothing is held"));
	}
};

}  // namespace flut

Model* modelFlut = createModel<flut::Flut, flut::FlutWidget>("Flut");
