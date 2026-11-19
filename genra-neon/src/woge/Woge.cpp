// SPDX-License-Identifier: GPL-3.0-only
// WOGE – the scanned-synthesis voice of GENRA NEON, in the NEON-TERRAIN look: the string is the glowing line at the
// front of the display, its last shapes stand behind it as a neon landscape towards a synthwave sun; the beam is the
// scan head. STRING (MASS, TENSION, DAMP, CENTER), EXCITE (HAMMER, PLUCK, NOISE, BOW, AUDIO; POSITION, FORCE), SCAN
// (PATH, WIDTH, DRIFT), FILTER, envelope, POLY 4 / MONO / LEGATO; STRIKE, FREEZE, UNDO, INIT; Follow GENRA, presets,
// MIDI-MAP on everything. Engine: WogeDsp.hpp; panel: tools/neon_panel.py (res/Woge.svg + WogeLayout.hpp).
#include "../plugin.hpp"
#include "../neon/ModuleKit.hpp"
#include "WogeDsp.hpp"
#include "WogePresets.hpp"
#include "WogeLayout.hpp"
#include <memory>

namespace woge {

namespace L = wg;
using namespace neonui;
using namespace neonkit;

struct Woge : Module, follow::Host {
	enum ParamId {
		MASS_PARAM, TENS_PARAM, DAMP_PARAM, CNTR_PARAM, EXCITE_PARAM, POS_PARAM, FORCE_PARAM, PATH_PARAM, WIDTH_PARAM,
		DRIFT_PARAM, CUTOFF_PARAM, RES_PARAM, DRIVE_PARAM, ATK_PARAM, DEC_PARAM, SUS_PARAM, REL_PARAM, VMODE_PARAM,
		GLIDE_PARAM, ACCENT_PARAM,
		NUM_PARAMS,
		PRESETSEL_PARAM = NUM_PARAMS,  // after the knobs: not in a preset
		STRIKE_PARAM, FREEZE_PARAM, UNDO_PARAM, INIT_PARAM,
		PARAMS_LEN
	};
	// in the order of the panel (WogeLayout.hpp JACK_IN)
	enum InputId {
		VOCT_INPUT, GATE_INPUT, VEL_INPUT, ACC_INPUT, SLIDE_INPUT, CLOCK_INPUT, RESET_INPUT, TSTRIKE_INPUT, TFREEZE_INPUT,
		MASS_INPUT, TENS_INPUT, DAMP_INPUT, CNTR_INPUT, POS_INPUT, FORCE_INPUT, WIDTH_INPUT, DRIFT_INPUT,
		EXC_INPUT, FM_INPUT, CUT_INPUT, RES_INPUT, DRIVE_INPUT, ATK_INPUT, DEC_INPUT, REL_INPUT, GLIDE_INPUT,
		INPUTS_LEN
	};
	enum OutputId { ENV_OUTPUT, SHAPE_OUTPUT, L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };
	static_assert((int) NUM_PARAMS == (int) NUM_PRESET_PARAMS, "presets must cover every knob");
	static_assert(INPUTS_LEN == L::NUM_JACK_IN, "the panel's jacks");
	static_assert(OUTPUTS_LEN == L::NUM_JACK_OUT, "the panel's outputs");

	std::unique_ptr<Engine> engine{new Engine};
	Params p;
	Channel ch[Notes::MAXCH];
	neon::Tempo tempo;
	int ctl = 0, ppqn = 4;
	dsp::SchmittTrigger clockTrig, resetTrig, strikeTrig, freezeTrig, strikeBtn, undoBtn, initBtn;
	std::atomic<uint32_t> undoReq{0}, initReq{0};
	uint32_t doneU = 0, doneI = 0;
	float flash = 0.f;
	Undo undo;

	genra_presets::Bank bank;
	void initPresets() {
		genra_presets::Factory f;
		f.count = NUM_PRESETS;
		f.style = [](int i) { return PRESETS[i].style; };
		f.name = [](int i) { return PRESETS[i].name; };
		f.info = [](int i) { return EXCITE_NAMES[(int) PRESETS[i].v[P_EXCITE]]; };
		f.values = [](int i, float* v) { PRESETS[i].values(v); };
		f.gain = [](int i) { return PRESETS[i].gain; };
		bank.sel = PRESETSEL_PARAM;
		bank.init(this, f, asset::user("GenraNeon/WogePresets"));
	}

	Woge() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		genra_presel::config(this, PRESETSEL_PARAM);
		srcName = "WOGE";
		audioL = L_OUTPUT;
		audioR = R_OUTPUT;
		fol.bind(VOCT_INPUT, follow::VOCT);
		fol.bind(GATE_INPUT, follow::GATE);
		fol.bind(VEL_INPUT, follow::VEL);
		fol.bind(ACC_INPUT, follow::ACC);
		fol.bind(SLIDE_INPUT, follow::SLIDE);
		fol.bind(CLOCK_INPUT, follow::CLOCK);
		fol.bind(RESET_INPUT, follow::RESET);
		std::vector<std::string> ex, paths, modes;
		for (int i = 0; i < NUM_EXCITES; i++)
			ex.push_back(EXCITE_NAMES[i]);
		for (int i = 0; i < NUM_PATHS; i++)
			paths.push_back(PATH_NAMES[i]);
		for (int i = 0; i < NUM_MODES; i++)
			modes.push_back(MODE_NAMES[i]);
		configParam(MASS_PARAM, 0.f, 1.f, 0.4f, "Mass (heavier: slower, darker motion)", "%", 0.f, 100.f);
		configParam(TENS_PARAM, 0.f, 1.f, 0.5f, "Tension (tighter: faster, brighter motion)", "%", 0.f, 100.f);
		configParam(DAMP_PARAM, 0.f, 1.f, 0.3f, "Damping (how fast the string comes to rest)", "%", 0.f, 100.f);
		configParam(CNTR_PARAM, 0.f, 1.f, 0.2f, "Center (springs that pull every mass back to the middle)", "%", 0.f, 100.f);
		configSwitch(EXCITE_PARAM, 0.f, NUM_EXCITES - 1, 0.f, "Excite", ex);
		configParam(POS_PARAM, 0.f, 1.f, 0.3f, "Position (where the string is excited)", "%", 0.f, 100.f);
		configParam(FORCE_PARAM, 0.f, 1.f, 0.6f, "Force", "%", 0.f, 100.f);
		configSwitch(PATH_PARAM, 0.f, NUM_PATHS - 1, 0.f, "Scan path", paths);
		configParam(WIDTH_PARAM, 0.f, 1.f, 1.f, "Scan width (how much of the string the head reads)", "%", 0.f, 100.f);
		configParam(DRIFT_PARAM, 0.f, 1.f, 0.f, "Drift (the part that is read wanders, in time)", "%", 0.f, 100.f);
		configParam<CutoffQuantity>(CUTOFF_PARAM, 0.f, 1.f, 0.75f, "Cutoff");
		configParam(RES_PARAM, 0.f, 1.f, 0.1f, "Resonance", "%", 0.f, 100.f);
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", "%", 0.f, 100.f);
		configParam<TimeQuantity>(ATK_PARAM, 0.f, 1.f, 0.f, "Attack");
		configParam<TimeQuantity>(DEC_PARAM, 0.f, 1.f, 0.5f, "Decay");
		configParam(SUS_PARAM, 0.f, 1.f, 0.8f, "Sustain", "%", 0.f, 100.f);
		configParam<TimeQuantity>(REL_PARAM, 0.f, 1.f, 0.4f, "Release");
		configSwitch(VMODE_PARAM, 0.f, NUM_MODES - 1, MODE_POLY, "Voice mode", modes);
		configParam(GLIDE_PARAM, 0.f, 1.f, 0.2f, "Glide time");
		paramQuantities[GLIDE_PARAM]->description = "On GENRA's SLIDE; with SLIDE unplugged in Mono/Legato on overlapping notes";
		configParam(ACCENT_PARAM, 0.f, 1.f, 0.5f, "Accent amount", "%", 0.f, 100.f);
		configButton(STRIKE_PARAM, "Strike: hit every sounding string");
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze: the strings stop moving (they keep their shape)", {"Off", "On"});
		configButton(UNDO_PARAM, "Undo the last INIT");
		configButton(INIT_PARAM, "Init: back to a plain sound");
		for (int id : {STRIKE_PARAM, UNDO_PARAM, INIT_PARAM, FREEZE_PARAM})
			paramQuantities[id]->randomizeEnabled = false;
		const char* inNames[INPUTS_LEN] = {
		    "V/Oct (polyphonic)", "Gate (polyphonic)", "Velocity (0–10 V)", "Accent (gate, from GENRA's ACC)",
		    "Slide (from GENRA's SLIDE: glide into the next note)", "Clock (DRIFT in time; GENRA: 16ths)", "Reset",
		    "Strike trigger", "Freeze trigger (toggles)", "Mass CV", "Tension CV", "Damping CV", "Center CV",
		    "Position CV", "Force CV", "Width CV", "Drift CV",
		    "Excite in (audio: pushes the strings with EXCITE on AUDIO)", "FM (audio: phase modulation of the scan)",
		    "Cutoff CV (1 V/oct)", "Resonance CV", "Drive CV", "Attack CV", "Decay CV", "Release CV", "Glide CV"};
		for (int i = 0; i < INPUTS_LEN; i++)
			configInput(i, inNames[i]);
		configOutput(ENV_OUTPUT, "Envelope of the newest note (0–10 V)");
		configOutput(SHAPE_OUTPUT, "Shape: the middle of the newest string (±5 V)");
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
	}
	void onRemove(const RemoveEvent& e) override { follow::gone(this); }
	void onPortChange(const PortChangeEvent& e) override { fol.portChange(e); }

	void process(const ProcessArgs& args) override {
		fol.process(this, args.frame);
		bool reset = resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		bool tick = clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		tempo.step(args.sampleTime, tick, reset, inputs[CLOCK_INPUT].isConnected(), ppqn);
		if (strikeTrig.process(inputs[TSTRIKE_INPUT].getVoltage(), 0.1f, 1.f) | strikeBtn.process(params[STRIKE_PARAM].getValue())) {
			engine->strike = true;
			flash = 0.15f;
		}
		if (undoBtn.process(params[UNDO_PARAM].getValue()))
			undoReq++;
		if (initBtn.process(params[INIT_PARAM].getValue()))
			initReq++;
		if (freezeTrig.process(inputs[TFREEZE_INPUT].getVoltage(), 0.1f, 1.f))
			params[FREEZE_PARAM].setValue(params[FREEZE_PARAM].getValue() > 0.5f ? 0.f : 1.f);
		if (--ctl <= 0) {
			ctl = 16;
			p.mass = kv(this, MASS_PARAM, MASS_INPUT);
			p.tension = kv(this, TENS_PARAM, TENS_INPUT);
			p.damp = kv(this, DAMP_PARAM, DAMP_INPUT);
			p.center = kv(this, CNTR_PARAM, CNTR_INPUT);
			p.excite = clamp((int) std::round(params[EXCITE_PARAM].getValue()), 0, NUM_EXCITES - 1);
			p.pos = kv(this, POS_PARAM, POS_INPUT);
			p.force = kv(this, FORCE_PARAM, FORCE_INPUT);
			p.path = clamp((int) std::round(params[PATH_PARAM].getValue()), 0, NUM_PATHS - 1);
			p.width = kv(this, WIDTH_PARAM, WIDTH_INPUT);
			p.drift = kv(this, DRIFT_PARAM, DRIFT_INPUT);
			p.cutoff = params[CUTOFF_PARAM].getValue();
			p.cutCv = inputs[CUT_INPUT].getVoltage();
			p.res = kv(this, RES_PARAM, RES_INPUT);
			p.drive = kv(this, DRIVE_PARAM, DRIVE_INPUT);
			p.atk = kv(this, ATK_PARAM, ATK_INPUT);
			p.dec = kv(this, DEC_PARAM, DEC_INPUT);
			p.sus = params[SUS_PARAM].getValue();
			p.rel = kv(this, REL_PARAM, REL_INPUT);
			p.mode = clamp((int) std::round(params[VMODE_PARAM].getValue()), 0, NUM_MODES - 1);
			p.glide = kv(this, GLIDE_PARAM, GLIDE_INPUT);
			p.accent = params[ACCENT_PARAM].getValue();
			p.outGain = bank.gain;
			p.freeze = params[FREEZE_PARAM].getValue() > 0.5f;
		}
		int n = std::min(std::max(inputs[VOCT_INPUT].getChannels(), inputs[GATE_INPUT].getChannels()), (int) Notes::MAXCH);
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
		float l, r, env, sh;
		engine->process(p, ch, n, inputs[SLIDE_INPUT].isConnected(), tempo.bpm, inputs[FM_INPUT].getVoltage(),
		                inputs[EXC_INPUT].getVoltage() * 0.1f, l, r, env, sh);
		outputs[L_OUTPUT].setVoltage(outputs[R_OUTPUT].isConnected() || pulled(args.frame) ? l : 0.5f * (l + r));
		outputs[R_OUTPUT].setVoltage(r);
		outputs[ENV_OUTPUT].setVoltage(10.f * env);
		outputs[SHAPE_OUTPUT].setVoltage(5.f * sh);
		if (flash > 0.f)
			flash -= args.sampleTime;
	}

	/** UI thread, every frame (from the display). */
	void uiStep() {
		uint32_t u = undoReq.load(), in = initReq.load();
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
		bank.toJson(j);
		fol.toJson(j);
		return j;
	}
	void dataFromJson(json_t* j) override {
		bank.selCode++;
		fol.fromJson(j);
		if (json_t* v = json_object_get(j, "ppqn"))
			ppqn = std::max(1, (int) json_integer_value(v));
		bank.fromJson(j);
	}
};

/** The display: the newest string at the front, its last shapes behind it towards the sun, the scan head's beam. */
struct Display : Widget {
	Woge* module = nullptr;
	static const int HIST = 18;
	float hist[HIST][N] = {};
	int head = 0, tick = 0;
	float t = 0.f;
	void step() override {
		Widget::step();
		t += 1.f / 60.f;
		if (module)
			module->uiStep();
		if (++tick >= 3) {
			tick = 0;
			head = (head + 1) % HIST;
			for (int i = 0; i < N; i++) {
				if (module) {
					float pk = std::max(module->engine->showPeak, 0.08f);
					hist[head][i] = clampf(module->engine->showShape[i] / pk, -1.2f, 1.2f) * std::min(1.f, module->engine->showPeak * 4.f);
				} else
					hist[head][i] = std::sin(i * 0.4f + t * 2.f) * 0.6f * std::sin((float) M_PI * i / (N - 1)) +
					                0.3f * std::sin(i * 0.9f - t * 3.f);
			}
		}
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		float W = box.size.x, H = box.size.y, vx = W / 2;
		nvgSave(vg);
		nvgScissor(vg, 2, 2, W - 4, H - 4);
		float hz = H * 0.36f;
		// the sun with its stripes, the horizon, a few stars
		nvgBeginPath(vg);
		nvgCircle(vg, vx, hz, H * 0.3f);
		nvgFillPaint(vg, nvgLinearGradient(vg, 0, hz - H * 0.3f, 0, hz, alpha(PINK, 0.32f), alpha(VIOLET, 0.12f)));
		nvgFill(vg);
		for (int i = 0; i < 5; i++) {
			nvgBeginPath(vg);
			nvgRect(vg, vx - H * 0.32f, hz - H * 0.16f + i * H * 0.035f, H * 0.64f, 0.8f + i * 0.5f);
			nvgFillColor(vg, nvgRGB(5, 2, 12));
			nvgFill(vg);
		}
		nvgBeginPath(vg);
		nvgRect(vg, 0, hz, W, H - hz);
		nvgFillColor(vg, nvgRGB(5, 2, 12));
		nvgFill(vg);
		for (int i = 0; i < 14; i++) {
			nvgBeginPath(vg);
			nvgCircle(vg, std::fmod(i * 97.f, W), 4.f + std::fmod(i * 37.f, hz * 0.8f), 0.7f);
			nvgFillColor(vg, alpha(WHITE, 0.5f));
			nvgFill(vg);
		}
		// the landscape: older shapes far away (cyan), the newest at the front (pink)
		Vec front[N];
		for (int k = HIST - 1; k >= 0; k--) {
			const float* s = hist[(head - k + HIST) % HIST];
			float f = 1.f - (float) k / (HIST - 1);  // 0 far .. 1 near
			float y0 = hz + (H - hz) * (0.12f + 0.78f * std::pow(f, 1.4f));
			float half = W * (0.18f + 0.3f * f), amp = mm2px(1.5f + 6.5f * f);
			nvgBeginPath(vg);
			for (int i = 0; i < N; i++) {
				float x = vx - half + 2.f * half * i / (N - 1), y = y0 - s[i] * amp;
				if (i == 0)
					nvgMoveTo(vg, x, y);
				else
					nvgLineTo(vg, x, y);
				if (k == 0)
					front[i] = Vec(x, y);
			}
			NVGcolor c = nvgLerpRGBA(CYAN, PINK, f);
			if (k == 0)
				glowStroke(vg, c, 1.8f, 3.5f);
			else {
				nvgStrokeColor(vg, alpha(c, 0.2f + 0.6f * f));
				nvgStrokeWidth(vg, 0.5f + 0.9f * f);
				nvgStroke(vg);
			}
		}
		for (int i = 0; i < N; i++) {
			nvgBeginPath(vg);
			nvgCircle(vg, front[i].x, front[i].y, 1.5f);
			nvgFillColor(vg, nvgRGB(0xff, 0xe0, 0xf4));
			nvgFill(vg);
		}
		// the scan head: where it reads (DRIFT moves the middle, WIDTH the part)
		float centre = module ? module->engine->showCenter : 0.5f + 0.3f * std::sin(t * 0.4f);
		float width = module ? module->p.width : 1.f;
		float u = 0.5f + 0.5f * std::sin(t * 1.3f);
		float q = width > 0.995f ? u : clampf(centre + (u - 0.5f) * width, 0.f, 1.f);
		float fi = q * (N - 1);
		int i0 = (int) fi;
		float fr = fi - i0;
		Vec hp = front[i0];
		if (i0 < N - 1)
			hp = hp.plus(front[i0 + 1].minus(hp).mult(fr));
		nvgBeginPath(vg);
		nvgRect(vg, hp.x - 1.2f, 2, 2.4f, H - 4);
		nvgFillColor(vg, alpha(CYAN, 0.25f));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, hp.x, hp.y, 4.f);
		glowStroke(vg, CYAN, 1.4f, 3.f);
		if (width < 0.995f) {  // the part that is read, under the string
			float a = clampf(centre - width / 2, 0.f, 1.f), b = clampf(centre + width / 2, 0.f, 1.f);
			float half = W * 0.48f;
			nvgBeginPath(vg);
			nvgMoveTo(vg, vx - half + 2 * half * a, H - 5);
			nvgLineTo(vg, vx - half + 2 * half * b, H - 5);
			glowStroke(vg, CYAN, 1.2f, 3.f);
		}
		scanlines(vg, W, H);
		nvgResetScissor(vg);
		std::string l1 = "HAMMER · LINEAR", l2 = "MASS 40 · TENSION 50 · DAMP 30", note = "C4", small = "FREE · 120 BPM";
		if (module) {
			const Params& p = module->p;
			l1 = std::string(EXCITE_NAMES[clamp(p.excite, 0, NUM_EXCITES - 1)]) + " · " + PATH_NAMES[clamp(p.path, 0, NUM_PATHS - 1)];
			l2 = string::f("MASS %d · TENSION %d · DAMP %d", (int) std::round(p.mass * 100.f), (int) std::round(p.tension * 100.f),
			               (int) std::round(p.damp * 100.f));
			note = noteName(module->engine->lastNote);
			small = followLine(module, module->inputs[Woge::CLOCK_INPUT].isConnected(), module->tempo.bpm);
			if (p.freeze)
				small += " · FREEZE";
		}
		corners(vg, W, l1, l2, note, small);
		nvgRestore(vg);
		Widget::drawLayer(args, layer);
	}
};

struct WogeWidget : ModuleWidget {
	WogeWidget(Woge* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Woge.svg")));
		follow::knownHost(module);
		using M = Woge;
		Display* d = new Display;
		d->module = module;
		d->box.pos = mm2px(Vec(L::DISPLAY[0], L::DISPLAY[1]));
		d->box.size = mm2px(Vec(L::DISPLAY[2], L::DISPLAY[3]));
		addChild(d);
		presetField(this, module, L::PRESETF, module ? &module->bank : nullptr, M::PRESETSEL_PARAM, PRESETS[0].name);
		field(this, module, L::EXCF, M::EXCITE_PARAM, NUM_EXCITES, EXCITE_NAMES, EXCITE_INFO, "Excite");
		field(this, module, L::PATHF, M::PATH_PARAM, NUM_PATHS, PATH_NAMES, PATH_INFO, "Scan path");
		knob(this, module, L::MASS, M::MASS_PARAM);
		knob(this, module, L::TENS, M::TENS_PARAM);
		knob(this, module, L::DAMP, M::DAMP_PARAM);
		knob(this, module, L::CNTR, M::CNTR_PARAM);
		knob(this, module, L::POS, M::POS_PARAM);
		knob(this, module, L::FORCE, M::FORCE_PARAM);
		knob(this, module, L::WIDTH, M::WIDTH_PARAM);
		knob(this, module, L::DRIFT, M::DRIFT_PARAM);
		knob(this, module, L::CUTOFF, M::CUTOFF_PARAM);
		knob(this, module, L::RES, M::RES_PARAM);
		knob(this, module, L::DRIVE, M::DRIVE_PARAM);
		knob(this, module, L::ATK, M::ATK_PARAM);
		knob(this, module, L::DEC, M::DEC_PARAM);
		knob(this, module, L::SUS, M::SUS_PARAM);
		knob(this, module, L::REL, M::REL_PARAM);
		knob(this, module, L::GLIDE, M::GLIDE_PARAM);
		knob(this, module, L::ACCENT, M::ACCENT_PARAM);
		lamps(this, module, L::VMODE, M::VMODE_PARAM, NUM_MODES, CYAN);
		NButton* sb = button(this, module, L::STRIKE, M::STRIKE_PARAM, false, PINK);
		sb->lit = [=]() { return module && module->flash > 0.f; };
		button(this, module, L::FREEZE, M::FREEZE_PARAM, true, CYAN);
		button(this, module, L::UNDO, M::UNDO_PARAM, false, VIOLET);
		button(this, module, L::INIT, M::INIT_PARAM, false, VIOLET);
		jacks(this, module, L::JACK_IN, L::JACK_IN_COL, L::JACK_OUT);
	}
	void appendContextMenu(Menu* menu) override {
		if (follow::Host* h = follow::host(module))
			h->fol.appendMenu(menu, module);
		Woge* m = dynamic_cast<Woge*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		genra_presets::appendMenu(menu, &m->bank);
		menu->addChild(new MenuSeparator);
		clockMenu(menu, &m->ppqn);
		menu->addChild(createMenuLabel("Patch GENRA's melody, bass or pads: V/OCT, GATE, VEL, ACC, SLIDE, CLOCK"));
	}
};

}  // namespace woge

Model* modelWoge = createModel<woge::Woge, woge::WogeWidget>("Woge");
