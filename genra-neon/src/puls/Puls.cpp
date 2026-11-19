// SPDX-License-Identifier: GPL-3.0-only
// PULS – the pulsar voice of GENRA NEON, in the NEON-TERRAIN look: a pulsar turns in the display, every pulse sends a
// ring out and a wave through the neon hills (a masked pulse: a faint dashed ring). PULSE (CLOCK / FREE / NOTE, RATE,
// F1, F2), PULSARET (wave, window, DUTY, SKEW), MASK (16 steps: a pattern or your own, DENS, ROT, PROB, NEW), filter,
// SPACE (SPREAD, JITTER, TAIL), envelope, voice mode; PULSE output: a trigger for every pulse that plays.
// Engine: PulsDsp.hpp; panel: tools/neon_panel.py (res/Puls.svg + PulsLayout.hpp).
#include "../plugin.hpp"
#include "../neon/ModuleKit.hpp"
#include "PulsDsp.hpp"
#include "PulsPresets.hpp"
#include "PulsLayout.hpp"
#include <memory>

namespace puls {

namespace L = pl;
using namespace neonui;
using namespace neonkit;

struct Puls : Module, follow::Host {
	enum ParamId {
		PMODE_PARAM, RATE_PARAM, F1_PARAM, F2_PARAM, SHAPE_PARAM, WINDOW_PARAM, DUTY_PARAM, SKEW_PARAM, PATTERN_PARAM,
		DENS_PARAM, ROT_PARAM, PROB_PARAM, CUTOFF_PARAM, RES_PARAM, DRIVE_PARAM, SPREAD_PARAM, JITTER_PARAM, TAIL_PARAM,
		ATK_PARAM, DEC_PARAM, SUS_PARAM, REL_PARAM, VMODE_PARAM, GLIDE_PARAM, ACCENT_PARAM,
		NUM_PARAMS,
		PRESETSEL_PARAM = NUM_PARAMS,
		NEW_PARAM, UNDO_PARAM, INIT_PARAM,
		STEP_PARAM,
		PARAMS_LEN = STEP_PARAM + STEPS
	};
	enum InputId {
		VOCT_INPUT, GATE_INPUT, VEL_INPUT, ACC_INPUT, SLIDE_INPUT, CLOCK_INPUT, RESET_INPUT, TNEW_INPUT,
		RATE_INPUT, F1_INPUT, F2_INPUT, DUTY_INPUT, SKEW_INPUT, DENS_INPUT, ROT_INPUT, PROB_INPUT,
		FM_INPUT, SPREAD_INPUT, JITTER_INPUT, TAIL_INPUT, CUT_INPUT, RES_INPUT, DRIVE_INPUT, ATK_INPUT, REL_INPUT,
		GLIDE_INPUT,
		INPUTS_LEN
	};
	enum OutputId { ENV_OUTPUT, PULSE_OUTPUT, L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };
	static_assert((int) NUM_PARAMS == (int) NUM_PRESET_PARAMS, "presets must cover every knob");
	static_assert(INPUTS_LEN == L::NUM_JACK_IN, "the panel's jacks");
	static_assert(OUTPUTS_LEN == L::NUM_JACK_OUT, "the panel's outputs");

	/** RATE shows what it means in the mode: a note value, Hz, or octaves. */
	struct RateQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			int m = module ? (int) std::round(module->params[PMODE_PARAM].getValue()) : 0;
			float k = getValue();
			if (m == M_CLOCK)
				return DIV_NAMES[clamp(divIndex(k), 0, NUM_DIVS - 1)];
			if (m == M_FREE) {
				float hz = freeHz(k);
				return hz < 10.f ? string::f("%.2f Hz", hz) : string::f("%.0f Hz", hz);
			}
			return string::f("%+d oct", noteOct(k));
		}
	};
	struct FormantQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			float hz = formantHz(getValue());
			return hz < 1000.f ? string::f("%.0f Hz", hz) : string::f("%.2f kHz", hz / 1000.f);
		}
	};

	std::unique_ptr<Engine> engine{new Engine};
	Params p;
	Channel ch[Notes::MAXCH];
	neon::Tempo tempo;
	int ctl = 0, ppqn = 4;
	dsp::SchmittTrigger clockTrig, resetTrig, newTrig, newBtn, undoBtn, initBtn;
	std::atomic<uint32_t> newReq{0}, undoReq{0}, initReq{0};
	uint32_t doneN = 0, doneU = 0, doneI = 0;
	uint32_t seed = 1;
	int genKey = -1;
	float flash = 0.f;
	Undo undo;

	genra_presets::Bank bank;
	void initPresets() {
		genra_presets::Factory f;
		f.count = NUM_PRESETS;
		f.style = [](int i) { return PRESETS[i].style; };
		f.name = [](int i) { return PRESETS[i].name; };
		f.info = [](int i) { return PMODE_NAMES[(int) PRESETS[i].v[P_PMODE]]; };
		f.values = [](int i, float* v) { PRESETS[i].values(v); };
		f.gain = [](int i) { return PRESETS[i].gain; };
		bank.sel = PRESETSEL_PARAM;
		bank.init(this, f, asset::user("GenraNeon/PulsPresets"));
	}

	Puls() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		genra_presel::config(this, PRESETSEL_PARAM);
		srcName = "PULS";
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
		configSwitch(PMODE_PARAM, 0.f, NUM_PMODES - 1, 0.f, "Pulse mode", names(PMODE_NAMES, NUM_PMODES));
		configParam<RateQuantity>(RATE_PARAM, 0.f, 1.f, 0.55f, "Rate");
		paramQuantities[RATE_PARAM]->description = "CLOCK: the note value · FREE: 0.25 Hz .. 2 kHz · NOTE: octaves above / below the note";
		configParam<FormantQuantity>(F1_PARAM, 0.f, 1.f, 0.45f, "Formant 1");
		configParam<FormantQuantity>(F2_PARAM, 0.f, 1.f, 0.65f, "Formant 2");
		configSwitch(SHAPE_PARAM, 0.f, NUM_SHAPES - 1, 0.f, "Pulsaret wave", names(SHAPE_NAMES, NUM_SHAPES));
		configSwitch(WINDOW_PARAM, 0.f, NUM_WINDOWS - 1, 0.f, "Pulsaret window", names(WINDOW_NAMES, NUM_WINDOWS));
		configParam(DUTY_PARAM, 0.f, 1.f, 0.5f, "Duty (how much of the period sounds)", "%", 0.f, 100.f);
		configParam(SKEW_PARAM, 0.f, 1.f, 0.5f, "Skew (the window's peak earlier / later)", "%", 0.f, 100.f);
		configSwitch(PATTERN_PARAM, 0.f, NUM_PATTERNS - 1, 0.f, "Mask pattern", names(PATTERN_NAMES, NUM_PATTERNS));
		configParam(DENS_PARAM, 0.f, 1.f, 0.6f, "Density (how many steps play)", "%", 0.f, 100.f);
		configParam(ROT_PARAM, 0.f, 1.f, 0.f, "Rotate the mask", "%", 0.f, 100.f);
		configParam(PROB_PARAM, 0.f, 1.f, 1.f, "Probability (a step that is on plays)", "%", 0.f, 100.f);
		configParam<CutoffQuantity>(CUTOFF_PARAM, 0.f, 1.f, 0.8f, "Cutoff");
		configParam(RES_PARAM, 0.f, 1.f, 0.1f, "Resonance", "%", 0.f, 100.f);
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", "%", 0.f, 100.f);
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.3f, "Spread (pulses left and right)", "%", 0.f, 100.f);
		configParam(JITTER_PARAM, 0.f, 1.f, 0.f, "Jitter (time and formant)", "%", 0.f, 100.f);
		configParam(TAIL_PARAM, 0.f, 1.f, 0.2f, "Tail (the pulses ring out)", "%", 0.f, 100.f);
		configParam<TimeQuantity>(ATK_PARAM, 0.f, 1.f, 0.f, "Attack");
		configParam<TimeQuantity>(DEC_PARAM, 0.f, 1.f, 0.5f, "Decay");
		configParam(SUS_PARAM, 0.f, 1.f, 0.8f, "Sustain", "%", 0.f, 100.f);
		configParam<TimeQuantity>(REL_PARAM, 0.f, 1.f, 0.3f, "Release");
		configSwitch(VMODE_PARAM, 0.f, NUM_MODES - 1, MODE_MONO, "Voice mode (POLY only in NOTE)", names(MODE_NAMES, NUM_MODES));
		configParam(GLIDE_PARAM, 0.f, 1.f, 0.1f, "Glide time");
		configParam(ACCENT_PARAM, 0.f, 1.f, 0.5f, "Accent amount", "%", 0.f, 100.f);
		configButton(NEW_PARAM, "New: a new mask");
		configButton(UNDO_PARAM, "Undo the last NEW / INIT");
		configButton(INIT_PARAM, "Init: back to a plain sound");
		for (int i = 0; i < STEPS; i++) {
			configSwitch(STEP_PARAM + i, 0.f, 1.f, 1.f, string::f("Mask step %d", i + 1), {"Off", "On"});
			paramQuantities[STEP_PARAM + i]->randomizeEnabled = false;
		}
		for (int id : {NEW_PARAM, UNDO_PARAM, INIT_PARAM})
			paramQuantities[id]->randomizeEnabled = false;
		const char* inNames[INPUTS_LEN] = {
		    "V/Oct (polyphonic; CLOCK / FREE: moves the formants)", "Gate (polyphonic; unpatched in CLOCK / FREE: the train runs)",
		    "Velocity (0–10 V)", "Accent (gate, from GENRA's ACC)", "Slide (from GENRA's SLIDE)", "Clock (GENRA: 16ths)", "Reset",
		    "New trigger", "Rate CV (CLOCK: through the note values; FREE / NOTE: 1 V/oct)", "Formant 1 CV", "Formant 2 CV",
		    "Duty CV", "Skew CV", "Density CV", "Rotate CV", "Probability CV", "FM (audio: the pulse rate)", "Spread CV",
		    "Jitter CV", "Tail CV", "Cutoff CV (1 V/oct)", "Resonance CV", "Drive CV", "Attack CV", "Release CV", "Glide CV"};
		for (int i = 0; i < INPUTS_LEN; i++)
			configInput(i, inNames[i]);
		configOutput(ENV_OUTPUT, "Envelope (0–10 V)");
		configOutput(PULSE_OUTPUT, "Pulse: a trigger for every pulse that plays (NOTE: every step that is on)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		initPresets();
	}

	void onAdd(const AddEvent& e) override { engine->setSampleRate(APP->engine->getSampleRate()); }
	void onSampleRateChange(const SampleRateChangeEvent& e) override { engine->setSampleRate(e.sampleRate); }
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		engine->allOff();
		bank.reset();
		genKey = -1;
	}
	void onRemove(const RemoveEvent& e) override { follow::gone(this); }
	void onPortChange(const PortChangeEvent& e) override { fol.portChange(e); }

	/** The pattern writes the steps when it (or DENS, ROT, the seed) changes; EDIT leaves them alone. */
	void generate() {
		int pat = clamp((int) std::round(params[PATTERN_PARAM].getValue()), 0, NUM_PATTERNS - 1);
		float dens = kv(this, DENS_PARAM, DENS_INPUT), rot = kv(this, ROT_PARAM, ROT_INPUT);
		int key = pat * 100000 + (int) std::round(dens * 16.f) * 1000 + (int) std::round(rot * 15.f) * 50 + (int) (seed % 50u);
		if (key == genKey || pat == PT_EDIT) {
			genKey = pat == PT_EDIT ? -1 : genKey;
			return;
		}
		genKey = key;
		bool m[STEPS];
		for (int i = 0; i < STEPS; i++)
			m[i] = params[STEP_PARAM + i].getValue() > 0.5f;
		makeMask(pat, dens, rot, seed, m);
		for (int i = 0; i < STEPS; i++)
			params[STEP_PARAM + i].setValue(m[i] ? 1.f : 0.f);
	}

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
		if (--ctl <= 0) {
			ctl = 16;
			generate();
			for (int i = 0; i < STEPS; i++)
				engine->mask[i] = params[STEP_PARAM + i].getValue() > 0.5f;
			p.pmode = clamp((int) std::round(params[PMODE_PARAM].getValue()), 0, NUM_PMODES - 1);
			p.rate = params[RATE_PARAM].getValue();
			p.rateCv = inputs[RATE_INPUT].getVoltage();
			p.f1 = kv(this, F1_PARAM, F1_INPUT);
			p.f2 = kv(this, F2_PARAM, F2_INPUT);
			p.shape = clamp((int) std::round(params[SHAPE_PARAM].getValue()), 0, NUM_SHAPES - 1);
			p.window = clamp((int) std::round(params[WINDOW_PARAM].getValue()), 0, NUM_WINDOWS - 1);
			p.duty = kv(this, DUTY_PARAM, DUTY_INPUT);
			p.skew = kv(this, SKEW_PARAM, SKEW_INPUT);
			p.pattern = clamp((int) std::round(params[PATTERN_PARAM].getValue()), 0, NUM_PATTERNS - 1);
			p.dens = kv(this, DENS_PARAM, DENS_INPUT);
			p.rot = kv(this, ROT_PARAM, ROT_INPUT);
			p.prob = kv(this, PROB_PARAM, PROB_INPUT);
			p.cutoff = params[CUTOFF_PARAM].getValue();
			p.cutCv = inputs[CUT_INPUT].getVoltage();
			p.res = kv(this, RES_PARAM, RES_INPUT);
			p.drive = kv(this, DRIVE_PARAM, DRIVE_INPUT);
			p.spread = kv(this, SPREAD_PARAM, SPREAD_INPUT);
			p.jitter = kv(this, JITTER_PARAM, JITTER_INPUT);
			p.tail = kv(this, TAIL_PARAM, TAIL_INPUT);
			p.atk = kv(this, ATK_PARAM, ATK_INPUT);
			p.dec = params[DEC_PARAM].getValue();
			p.sus = params[SUS_PARAM].getValue();
			p.rel = kv(this, REL_PARAM, REL_INPUT);
			p.vmode = clamp((int) std::round(params[VMODE_PARAM].getValue()), 0, NUM_MODES - 1);
			p.glide = kv(this, GLIDE_PARAM, GLIDE_INPUT);
			p.accent = params[ACCENT_PARAM].getValue();
			p.outGain = bank.gain;
			p.gated = inputs[GATE_INPUT].isConnected();
		}
		int n = std::min(std::max(inputs[VOCT_INPUT].getChannels(), inputs[GATE_INPUT].getChannels()), (int) Notes::MAXCH);
		n = std::max(n, 1);
		bool vel = inputs[VEL_INPUT].isConnected();
		for (int c = 0; c < n; c++) {
			Channel& x = ch[c];
			x.voct = inputs[VOCT_INPUT].getPolyVoltage(c);
			float g = inputs[GATE_INPUT].getPolyVoltage(c);
			x.gate = g >= 1.f ? true : (g <= 0.1f ? false : x.gate);
			x.vel = vel ? clamp(inputs[VEL_INPUT].getPolyVoltage(c) * 0.1f, 0.f, 1.f) : 0.8f;
			x.acc = inputs[ACC_INPUT].getPolyVoltage(c) >= 1.f;
			x.slide = inputs[SLIDE_INPUT].getPolyVoltage(c) >= 1.f;
		}
		if (!p.gated) {  // no gates: the train runs on its own; V/OCT still moves the formants
			ch[0].gate = false;
			engine->v[0].pitch = engine->v[0].target = inputs[VOCT_INPUT].getVoltage();
		}
		float l, r, env, po;
		engine->process(p, ch, n, inputs[SLIDE_INPUT].isConnected(), tempo.bpm, tempo.beats, reset, inputs[FM_INPUT].getVoltage(),
		                l, r, env, po);
		outputs[L_OUTPUT].setVoltage(outputs[R_OUTPUT].isConnected() || pulled(args.frame) ? l : 0.5f * (l + r));
		outputs[R_OUTPUT].setVoltage(r);
		outputs[ENV_OUTPUT].setVoltage(10.f * env);
		outputs[PULSE_OUTPUT].setVoltage(po);
		if (flash > 0.f)
			flash -= args.sampleTime;
	}

	void uiStep() {
		uint32_t nw = newReq.load(), u = undoReq.load(), in = initReq.load();
		if (nw != doneN) {
			doneN = nw;
			undo.push(this);
			seed = 1u + random::u32() % 99999u;
			int pat = (int) std::round(params[PATTERN_PARAM].getValue());
			if (pat == PT_EUCLID || pat == PT_ALL || pat == PT_EDIT) {
				// a new place to start, and for EDIT new steps at the density
				params[ROT_PARAM].setValue(random::uniform());
				if (pat == PT_EDIT)
					for (int i = 0; i < STEPS; i++)
						params[STEP_PARAM + i].setValue(random::uniform() < params[DENS_PARAM].getValue() ? 1.f : 0.f);
			}
			genKey = -1;
			flash = 0.3f;
		}
		if (u != doneU) {
			doneU = u;
			undo.pop(this);
			genKey = -1;
		}
		if (in != doneI) {
			doneI = in;
			undo.push(this);
			for (int i = 0; i < NUM_PARAMS; i++)
				params[i].setValue(paramQuantities[i]->getDefaultValue());
			bank.reset();
			genKey = -1;
		}
	}

	int hostPresetCount() override { return bank.count(); }
	void hostPresetInfo(int i, char* name, int nl, char* style, int sl) override {
		follow::putStr(name, nl, bank.nameOf(i).c_str());
		follow::putStr(style, sl, i >= 0 && i < bank.f.count && bank.f.style ? bank.f.style(i) : "");
	}
	int hostPresetCurrent() override { return bank.preset; }
	void hostPresetLoad(int i) override {
		bank.load(i);
		genKey = -1;
	}

	json_t* dataToJson() override {
		json_t* j = json_object();
		json_object_set_new(j, "ppqn", json_integer(ppqn));
		json_object_set_new(j, "seed", json_integer(seed));
		bank.toJson(j);
		fol.toJson(j);
		return j;
	}
	void dataFromJson(json_t* j) override {
		bank.selCode++;
		fol.fromJson(j);
		if (json_t* v = json_object_get(j, "ppqn"))
			ppqn = std::max(1, (int) json_integer_value(v));
		if (json_t* v = json_object_get(j, "seed"))
			seed = (uint32_t) json_integer_value(v);
		bank.fromJson(j);
		// the steps come back as they were saved: do not let the pattern write over them
		int pat = clamp((int) std::round(params[PATTERN_PARAM].getValue()), 0, NUM_PATTERNS - 1);
		float dens = params[DENS_PARAM].getValue(), rot = params[ROT_PARAM].getValue();
		genKey = pat * 100000 + (int) std::round(dens * 16.f) * 1000 + (int) std::round(rot * 15.f) * 50 + (int) (seed % 50u);
	}
};

/** A mask step: on / off (a param, so MIDI-MAP learns it); the step that plays now has a cyan frame. A click switches
 * the pattern to EDIT, so your steps stay. */
struct StepButton : NButton {
	Puls* pm = nullptr;
	int idx = 0;
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && pm)
			pm->params[Puls::PATTERN_PARAM].setValue((float) PT_EDIT);
		NButton::onButton(e);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		NButton::drawLayer(args, layer);
		if (layer == 1 && pm && pm->engine->step == idx) {
			NVGcontext* vg = args.vg;
			nvgBeginPath(vg);
			nvgRect(vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f);
			glowStroke(vg, CYAN, 1.2f, 3.f);
		}
	}
};

/** The display: a pulsar with two turning beams; every pulse a ring and a wave through the hills. */
struct Display : Widget {
	Puls* module = nullptr;
	struct Ring {
		float born;
		bool on;
	};
	std::vector<Ring> rings;
	std::vector<float> bumps;
	uint32_t seen = 0;
	float t = 0.f, kick = 0.f;
	void step() override {
		Widget::step();
		t += 1.f / 60.f;
		if (module)
			module->uiStep();
		uint32_t c = module ? module->engine->pulseCount : (uint32_t) (t * 5.f);
		bool on = module ? module->engine->lastOn : ((int) (t * 5.f) % 3 != 1);
		if (c != seen) {
			seen = c;
			if (rings.size() < 24)
				rings.push_back({t, on});
			if (on) {
				bumps.push_back(t);
				kick = 1.f;
			}
		}
		kick *= 0.85f;
		rings.erase(std::remove_if(rings.begin(), rings.end(), [&](const Ring& r) { return t - r.born > 1.3f; }), rings.end());
		bumps.erase(std::remove_if(bumps.begin(), bumps.end(), [&](float b) { return t - b > 1.6f; }), bumps.end());
		if (bumps.size() > 16)
			bumps.erase(bumps.begin(), bumps.begin() + (bumps.size() - 16));
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		float W = box.size.x, H = box.size.y, cx = W / 2, cy = H * 0.42f;
		nvgSave(vg);
		nvgScissor(vg, 2, 2, W - 4, H - 4);
		for (int i = 0; i < 18; i++) {
			nvgBeginPath(vg);
			nvgCircle(vg, std::fmod(i * 131.f, W), 3.f + std::fmod(i * 53.f, H * 0.6f), 0.7f);
			nvgFillColor(vg, alpha(WHITE, 0.45f));
			nvgFill(vg);
		}
		// beams
		float a = t * 1.6f;
		for (int k = 0; k < 2; k++) {
			float b = a + k * (float) M_PI, L = W;
			nvgBeginPath(vg);
			nvgMoveTo(vg, cx, cy);
			nvgLineTo(vg, cx + std::cos(b - 0.09f) * L, cy + std::sin(b - 0.09f) * L);
			nvgLineTo(vg, cx + std::cos(b + 0.09f) * L, cy + std::sin(b + 0.09f) * L);
			nvgClosePath(vg);
			nvgFillColor(vg, alpha(CYAN, 0.1f));
			nvgFill(vg);
		}
		// rings
		for (const Ring& r : rings) {
			float age = t - r.born;
			nvgBeginPath(vg);
			nvgCircle(vg, cx, cy, 6.f + age * W * 0.4f);
			float o = (1.f - age / 1.3f) * (r.on ? 1.f : 0.45f);
			if (r.on)
				glowStroke(vg, alpha(CYAN, o), 1.6f, 3.f);
			else {
				nvgStrokeColor(vg, alpha(VIOLET, o));
				nvgStrokeWidth(vg, 0.7f);
				nvgStroke(vg);
			}
		}
		// the core
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy, 9.f + 7.f * kick);
		nvgFillColor(vg, alpha(CYAN, 0.18f + 0.25f * kick));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy, 4.f + 3.f * kick);
		nvgFillColor(vg, CYAN);
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy, 1.8f);
		nvgFillColor(vg, nvgRGB(0xe8, 0xfe, 0xff));
		nvgFill(vg);
		// the hills: back pink, front cyan; every pulse a wave from the middle
		const int RL = 6, RN = 64;
		for (int k = 0; k < RL; k++) {
			float f = (float) k / (RL - 1);
			float y0 = H * 0.62f + f * H * 0.32f, sx = 0.55f + 0.5f * f;
			nvgBeginPath(vg);
			for (int i = 0; i <= RN; i++) {
				float x0 = (float) i / RN * W, x = cx + (x0 - cx) * sx;
				float y = y0 - std::sin(x0 * 0.05f + t * 0.7f + f * 2.f) * 1.2f * (0.3f + f);
				for (float b : bumps) {
					float age = t - b - (1.f - f) * 0.15f;
					if (age < 0.f)
						continue;
					float d = std::fabs(x0 - cx) - age * W * 0.5f;
					y -= (3.f + 6.f * f) * std::exp(-d * d / 160.f) * std::max(0.f, 1.f - age / 1.4f);
				}
				if (i == 0)
					nvgMoveTo(vg, x, y);
				else
					nvgLineTo(vg, x, y);
			}
			nvgStrokeColor(vg, alpha(nvgLerpRGBA(PINK, CYAN, f), 0.3f + 0.7f * f));
			nvgStrokeWidth(vg, 0.6f + 1.2f * f);
			nvgStroke(vg);
		}
		scanlines(vg, W, H);
		nvgResetScissor(vg);
		std::string l1 = "CLOCK · 1/16", l2 = "F1 400 HZ · F2 900 HZ · SINE", big = "8.0 HZ", small = "FREE · 120 BPM";
		if (module) {
			const Params& p = module->p;
			Puls::RateQuantity* rq = dynamic_cast<Puls::RateQuantity*>(module->paramQuantities[Puls::RATE_PARAM]);
			l1 = std::string(PMODE_NAMES[p.pmode]) + " · " + (rq ? rq->getDisplayValueString() : "");
			l2 = string::f("F1 %.0f HZ · F2 %.0f HZ · %s", formantHz(p.f1), formantHz(p.f2), SHAPE_NAMES[p.shape]);
			float hz = module->engine->showRate;
			big = p.pmode == M_NOTE ? noteName(module->engine->lastNote) : (hz < 10.f ? string::f("%.1f HZ", hz) : string::f("%.0f HZ", hz));
			small = followLine(module, module->inputs[Puls::CLOCK_INPUT].isConnected(), module->tempo.bpm);
		}
		corners(vg, W, l1, l2, big, small);
		nvgRestore(vg);
		Widget::drawLayer(args, layer);
	}
};

struct PulsWidget : ModuleWidget {
	PulsWidget(Puls* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Puls.svg")));
		follow::knownHost(module);
		using M = Puls;
		Display* d = new Display;
		d->module = module;
		d->box.pos = mm2px(Vec(L::DISPLAY[0], L::DISPLAY[1]));
		d->box.size = mm2px(Vec(L::DISPLAY[2], L::DISPLAY[3]));
		addChild(d);
		presetField(this, module, L::PRESETF, module ? &module->bank : nullptr, M::PRESETSEL_PARAM, PRESETS[0].name);
		field(this, module, L::PMODEF, M::PMODE_PARAM, NUM_PMODES, PMODE_NAMES, PMODE_INFO, "Pulse mode");
		field(this, module, L::SHAPEF, M::SHAPE_PARAM, NUM_SHAPES, SHAPE_NAMES, SHAPE_INFO, "Pulsaret wave");
		field(this, module, L::WINF, M::WINDOW_PARAM, NUM_WINDOWS, WINDOW_NAMES, WINDOW_INFO, "Pulsaret window");
		field(this, module, L::PATF, M::PATTERN_PARAM, NUM_PATTERNS, PATTERN_NAMES, PATTERN_INFO, "Mask pattern");
		knob(this, module, L::RATE, M::RATE_PARAM);
		knob(this, module, L::F1, M::F1_PARAM);
		knob(this, module, L::F2, M::F2_PARAM);
		knob(this, module, L::DUTY, M::DUTY_PARAM);
		knob(this, module, L::SKEW, M::SKEW_PARAM);
		knob(this, module, L::DENS, M::DENS_PARAM);
		knob(this, module, L::ROT, M::ROT_PARAM);
		knob(this, module, L::PROB, M::PROB_PARAM);
		knob(this, module, L::CUTOFF, M::CUTOFF_PARAM);
		knob(this, module, L::RES, M::RES_PARAM);
		knob(this, module, L::DRIVE, M::DRIVE_PARAM);
		knob(this, module, L::SPREAD, M::SPREAD_PARAM);
		knob(this, module, L::JITTER, M::JITTER_PARAM);
		knob(this, module, L::TAIL, M::TAIL_PARAM);
		knob(this, module, L::ATK, M::ATK_PARAM);
		knob(this, module, L::DEC, M::DEC_PARAM);
		knob(this, module, L::SUS, M::SUS_PARAM);
		knob(this, module, L::REL, M::REL_PARAM);
		knob(this, module, L::GLIDE, M::GLIDE_PARAM);
		knob(this, module, L::ACCENT, M::ACCENT_PARAM);
		lamps(this, module, L::VMODE, M::VMODE_PARAM, NUM_MODES, CYAN);
		NButton* nb = button(this, module, L::NEW, M::NEW_PARAM, false, PINK);
		nb->lit = [=]() { return module && module->flash > 0.f; };
		button(this, module, L::UNDO, M::UNDO_PARAM, false, VIOLET);
		button(this, module, L::INIT, M::INIT_PARAM, false, VIOLET);
		for (int i = 0; i < STEPS; i++) {
			StepButton* s = createParam<StepButton>(Vec(0, 0), module, M::STEP_PARAM + i);
			float r[4] = {L::STEPS[0] + i * L::STEPS[2] + 0.3f, L::STEPS[1], L::STEPS[2] - 0.6f, L::STEPS[3]};
			s->setup(r, true);
			s->col = PINK;
			s->pm = module;
			s->idx = i;
			addParam(s);
		}
		jacks(this, module, L::JACK_IN, L::JACK_IN_COL, L::JACK_OUT);
	}
	void appendContextMenu(Menu* menu) override {
		if (follow::Host* h = follow::host(module))
			h->fol.appendMenu(menu, module);
		Puls* m = dynamic_cast<Puls*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		genra_presets::appendMenu(menu, &m->bank);
		menu->addChild(new MenuSeparator);
		clockMenu(menu, &m->ppqn);
		menu->addChild(createMenuLabel("CLOCK / FREE: a pulse train (the gate is optional) · NOTE: an instrument"));
	}
};

}  // namespace puls

Model* modelPuls = createModel<puls::Puls, puls::PulsWidget>("Puls");
