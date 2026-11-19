// SPDX-License-Identifier: GPL-3.0-only
// GLAS – the ANS synthesizer of GENRA NEON, in the NEON-TERRAIN look: the display is the glass plate you paint on with
// the mouse (pitch up, time across; left button: the tool, right button: erase), the cyan head plays it in time and
// what it plays lights up white. PAINT (PEN, SPRAY, LINE, ERASE, SIZE, SNAP: strokes land on the notes of SCALE; BANK:
// a picture to start from), PLAY (length in bars, direction, SPEED, POS), TONE (SCALE: also GENRA KEY / GENRA CHORD,
// LOW, RANGE, TILT), envelope, SPACE (SPREAD, BLUR, SHIMMER), DRONE / GATE; NEW, CLEAR, REC (paints the IN signal's
// spectrum), UNDO, INIT. Engine: GlasDsp.hpp; panel: tools/neon_panel.py (res/Glas.svg + GlasLayout.hpp).
#include "../plugin.hpp"
#include "../neon/ModuleKit.hpp"
#include "GlasDsp.hpp"
#include "GlasPresets.hpp"
#include "GlasLayout.hpp"
#include <memory>

namespace glas {

namespace L = gl;
using namespace neonui;
using namespace neonkit;

struct Glas : Module, follow::Host {
	enum ParamId {
		TOOL_PARAM, SIZE_PARAM, BANK_PARAM, PMODE_PARAM, LEN_PARAM, DIR_PARAM, SPEED_PARAM, POS_PARAM, SCALE_PARAM,
		LOW_PARAM, RNG_PARAM, TILT_PARAM, ATK_PARAM, DEC_PARAM, SUS_PARAM, REL_PARAM, SPREAD_PARAM, BLUR_PARAM,
		SHIMMER_PARAM,
		NUM_PARAMS,
		PRESETSEL_PARAM = NUM_PARAMS,
		NEW_PARAM, CLEAR_PARAM, REC_PARAM, SNAP_PARAM, UNDO_PARAM, INIT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT, GATE_INPUT, VEL_INPUT, ACC_INPUT, SLIDE_INPUT, CLOCK_INPUT, RESET_INPUT, TNEW_INPUT, TREC_INPUT,
		TCLR_INPUT, SPEED_INPUT, POS_INPUT, LOW_INPUT, RNG_INPUT, TILT_INPUT, SPREAD_INPUT, BLUR_INPUT, IN_INPUT,
		SHIMMER_INPUT, ATK_INPUT, REL_INPUT,
		INPUTS_LEN
	};
	enum OutputId { ENV_OUTPUT, BRIGHT_OUTPUT, L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };
	static_assert((int) NUM_PARAMS == (int) NUM_PRESET_PARAMS, "presets must cover every knob");
	static_assert(INPUTS_LEN == L::NUM_JACK_IN, "the panel's jacks");
	static_assert(OUTPUTS_LEN == L::NUM_JACK_OUT, "the panel's outputs");

	struct LowQuantity : ParamQuantity {
		std::string getDisplayValueString() override { return neonkit::noteName(lowSemis(getValue()) / 12.f); }
	};
	struct RangeQuantity : ParamQuantity {
		std::string getDisplayValueString() override { return string::f("%.1f octaves", rangeOct(getValue())); }
	};
	struct SpeedQuantity : ParamQuantity {
		std::string getDisplayValueString() override { return string::f("x %.2f", speedMul(getValue())); }
	};

	std::unique_ptr<Engine> engine{new Engine};
	std::unique_ptr<Canvas> canvas{new Canvas};
	std::atomic<uint32_t> canvasVer{1};  // bumped when the picture changes (the display uploads it again)
	Params p;
	Channel ch[Notes::MAXCH];
	neon::Tempo tempo;
	int ctl = 0, ppqn = 4;
	dsp::SchmittTrigger clockTrig, resetTrig, newTrig, recTrig, clrTrig, newBtn, clearBtn, undoBtn, initBtn;
	std::atomic<uint32_t> newReq{0}, clearReq{0}, undoReq{0}, initReq{0};
	uint32_t doneN = 0, doneC = 0, doneU = 0, doneI = 0;
	int lastPreset = -2, lastBank = -1;
	float flash = 0.f;
	Undo undo;

	genra_presets::Bank bank;
	void initPresets() {
		genra_presets::Factory f;
		f.count = NUM_PRESETS;
		f.style = [](int i) { return PRESETS[i].style; };
		f.name = [](int i) { return PRESETS[i].name; };
		f.info = [](int i) { return BANK_NAMES[(int) PRESETS[i].v[P_BANK]]; };
		f.values = [](int i, float* v) { PRESETS[i].values(v); };
		f.gain = [](int i) { return PRESETS[i].gain; };
		bank.sel = PRESETSEL_PARAM;
		bank.init(this, f, asset::user("GenraNeon/GlasPresets"));
	}

	Glas() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		engine->canvas = canvas.get();
		genra_presel::config(this, PRESETSEL_PARAM);
		srcName = "GLAS";
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
		configSwitch(TOOL_PARAM, 0.f, NUM_TOOLS - 1, 0.f, "Paint tool (the right mouse button always erases)", names(TOOL_NAMES, NUM_TOOLS));
		configParam(SIZE_PARAM, 0.f, 1.f, 0.3f, "Brush size", "%", 0.f, 100.f);
		configSwitch(BANK_PARAM, 0.f, NUM_BANKS - 1, (float) B_CHOIR, "Picture (choosing one paints it)", names(BANK_NAMES, NUM_BANKS));
		configSwitch(PMODE_PARAM, 0.f, NUM_PLAYMODES - 1, 0.f, "Play mode", {"Drone (plays all the time)", "Gate (while a note is held; V/OCT transposes)"});
		configSwitch(LEN_PARAM, 0.f, NUM_LENS - 1, 2.f, "Plate length", names(LEN_NAMES, NUM_LENS));
		configSwitch(DIR_PARAM, 0.f, NUM_DIRS - 1, 0.f, "Direction", names(DIR_NAMES, NUM_DIRS));
		configParam<SpeedQuantity>(SPEED_PARAM, 0.f, 1.f, 0.5f, "Speed");
		configParam(POS_PARAM, 0.f, 1.f, 0.f, "Position (offset; with POS CV patched the head goes where the CV says)", "%", 0.f, 100.f);
		configSwitch(SCALE_PARAM, 0.f, NUM_SCALES - 1, (float) SC_GENRA_CHORD, "Scale (when it plays; SNAP: when you paint)", names(SCALE_NAMES, NUM_SCALES));
		configParam<LowQuantity>(LOW_PARAM, 0.f, 1.f, 0.4f, "Lowest note");
		configParam<RangeQuantity>(RNG_PARAM, 0.f, 1.f, 0.72f, "Range");
		configParam(TILT_PARAM, -1.f, 1.f, 0.f, "Tilt (dark .. bright)", "%", 0.f, 100.f);
		configParam<TimeQuantity>(ATK_PARAM, 0.f, 1.f, 0.2f, "Attack (DRONE: the fade-in)");
		configParam<TimeQuantity>(DEC_PARAM, 0.f, 1.f, 0.5f, "Decay");
		configParam(SUS_PARAM, 0.f, 1.f, 1.f, "Sustain", "%", 0.f, 100.f);
		configParam<TimeQuantity>(REL_PARAM, 0.f, 1.f, 0.5f, "Release");
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.5f, "Spread (rows across the stereo field)", "%", 0.f, 100.f);
		configParam(BLUR_PARAM, 0.f, 1.f, 0.2f, "Blur (smears the picture in time and pitch)", "%", 0.f, 100.f);
		configParam(SHIMMER_PARAM, 0.f, 1.f, 0.f, "Shimmer (the high rows flicker)", "%", 0.f, 100.f);
		configButton(NEW_PARAM, "New: a new picture of the bank");
		configButton(CLEAR_PARAM, "Clear the glass");
		configSwitch(REC_PARAM, 0.f, 1.f, 0.f, "Rec: paints the spectrum of IN at the head", {"Off", "On"});
		configSwitch(SNAP_PARAM, 0.f, 1.f, 0.f, "Snap: strokes land on the notes of SCALE", {"Off", "On"});
		configButton(UNDO_PARAM, "Undo the last stroke / NEW / CLEAR / INIT");
		configButton(INIT_PARAM, "Init: back to the start");
		for (int id : {NEW_PARAM, CLEAR_PARAM, REC_PARAM, UNDO_PARAM, INIT_PARAM})
			paramQuantities[id]->randomizeEnabled = false;
		const char* inNames[INPUTS_LEN] = {
		    "V/Oct (polyphonic: GENRA KEY / CHORD listen to it; GATE mode: transposes)", "Gate (polyphonic)", "Velocity",
		    "Accent", "Slide", "Clock (the plate runs in time; GENRA: 16ths)", "Reset (the head back to the start)",
		    "New trigger", "Rec trigger (toggles)", "Clear trigger", "Speed CV", "Position CV (0–10 V: the head itself)",
		    "Lowest note CV", "Range CV", "Tilt CV", "Spread CV", "Blur CV", "In (audio REC paints)", "Shimmer CV",
		    "Attack CV", "Release CV"};
		for (int i = 0; i < INPUTS_LEN; i++)
			configInput(i, inNames[i]);
		configOutput(ENV_OUTPUT, "Envelope (0–10 V)");
		configOutput(BRIGHT_OUTPUT, "Brightness: how much of the plate the head plays (0–10 V)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		initPresets();
		canvas->paint(B_CHOIR, 7);
	}

	void onAdd(const AddEvent& e) override { engine->setSampleRate(APP->engine->getSampleRate()); }
	void onSampleRateChange(const SampleRateChangeEvent& e) override { engine->setSampleRate(e.sampleRate); }
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		engine->allOff();
		bank.reset();
		canvas->paint(B_CHOIR, 7);
		canvasVer++;
		lastBank = B_CHOIR;
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
		if (clrTrig.process(inputs[TCLR_INPUT].getVoltage(), 0.1f, 1.f) | clearBtn.process(params[CLEAR_PARAM].getValue()))
			clearReq++;
		if (recTrig.process(inputs[TREC_INPUT].getVoltage(), 0.1f, 1.f))
			params[REC_PARAM].setValue(params[REC_PARAM].getValue() > 0.5f ? 0.f : 1.f);
		if (undoBtn.process(params[UNDO_PARAM].getValue()))
			undoReq++;
		if (initBtn.process(params[INIT_PARAM].getValue()))
			initReq++;
		if (--ctl <= 0) {
			ctl = 16;
			p.playMode = clamp((int) std::round(params[PMODE_PARAM].getValue()), 0, NUM_PLAYMODES - 1);
			p.len = clamp((int) std::round(params[LEN_PARAM].getValue()), 0, NUM_LENS - 1);
			p.dir = clamp((int) std::round(params[DIR_PARAM].getValue()), 0, NUM_DIRS - 1);
			p.speed = kv(this, SPEED_PARAM, SPEED_INPUT);
			p.pos = params[POS_PARAM].getValue();
			p.posCv = inputs[POS_INPUT].getVoltage();
			p.posPatched = inputs[POS_INPUT].isConnected();
			p.scale = clamp((int) std::round(params[SCALE_PARAM].getValue()), 0, NUM_SCALES - 1);
			p.low = kv(this, LOW_PARAM, LOW_INPUT);
			p.range = kv(this, RNG_PARAM, RNG_INPUT);
			p.tilt = kv(this, TILT_PARAM, TILT_INPUT, -1.f, 1.f);
			p.atk = kv(this, ATK_PARAM, ATK_INPUT);
			p.dec = params[DEC_PARAM].getValue();
			p.sus = params[SUS_PARAM].getValue();
			p.rel = kv(this, REL_PARAM, REL_INPUT);
			p.spread = kv(this, SPREAD_PARAM, SPREAD_INPUT);
			p.blur = kv(this, BLUR_PARAM, BLUR_INPUT);
			p.shimmer = kv(this, SHIMMER_PARAM, SHIMMER_INPUT);
			p.outGain = bank.gain;
			p.rec = params[REC_PARAM].getValue() > 0.5f && inputs[IN_INPUT].isConnected();
		}
		int n = std::min(std::max(inputs[VOCT_INPUT].getChannels(), inputs[GATE_INPUT].getChannels()), (int) Notes::MAXCH);
		for (int c = 0; c < n; c++) {
			ch[c].voct = inputs[VOCT_INPUT].getPolyVoltage(c);
			float g = inputs[GATE_INPUT].getPolyVoltage(c);
			ch[c].gate = g >= 1.f ? true : (g <= 0.1f ? false : ch[c].gate);
		}
		float l, r, env, br;
		engine->process(p, ch, n, tempo.bpm, reset, inputs[IN_INPUT].getVoltage(), l, r, env, br);
		outputs[L_OUTPUT].setVoltage(outputs[R_OUTPUT].isConnected() || pulled(args.frame) ? l : 0.5f * (l + r));
		outputs[R_OUTPUT].setVoltage(r);
		outputs[ENV_OUTPUT].setVoltage(10.f * env);
		outputs[BRIGHT_OUTPUT].setVoltage(br);
		if (flash > 0.f)
			flash -= args.sampleTime;
	}

	void paintBank(int b, uint32_t seed) {
		if (b == B_BLANK)
			canvas->clear();
		else
			canvas->paint(b, seed);
		canvasVer++;
	}
	/** UI thread, every frame. */
	void uiStep() {
		uint32_t nw = newReq.load(), cl = clearReq.load(), u = undoReq.load(), in = initReq.load();
		if (bank.preset != lastPreset) {  // a preset was loaded: its picture
			if (lastPreset != -2 && bank.preset >= 0) {
				int b = clamp((int) std::round(params[BANK_PARAM].getValue()), 0, NUM_BANKS - 1);
				paintBank(b, bank.preset < NUM_PRESETS ? presetSeed(bank.preset) : random::u32());
			}
			lastPreset = bank.preset;
			lastBank = (int) std::round(params[BANK_PARAM].getValue());
		}
		int b = (int) std::round(params[BANK_PARAM].getValue());
		if (b != lastBank) {  // a new BANK was chosen (field, wheel, MIDI-MAP): paint it
			if (lastBank >= 0) {
				undo.push(this);
				paintBank(clamp(b, 0, NUM_BANKS - 1), 1u + random::u32() % 99999u);
			}
			lastBank = b;
		}
		if (nw != doneN) {
			doneN = nw;
			undo.push(this);
			int bb = clamp(b, 0, NUM_BANKS - 1);
			paintBank(bb == B_BLANK ? 1 + (int) (random::uniform() * (NUM_BANKS - 1)) % (NUM_BANKS - 1) : bb, 1u + random::u32() % 99999u);
			flash = 0.3f;
		}
		if (cl != doneC) {
			doneC = cl;
			undo.push(this);
			canvas->clear();
			canvasVer++;
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
			params[REC_PARAM].setValue(0.f);
			params[SNAP_PARAM].setValue(0.f);
			bank.reset();
			paintBank(B_CHOIR, 7);
			lastBank = B_CHOIR;
		}
	}

	/** The row the brush lands on (SNAP: the nearest note of SCALE; FREE: the nearest semitone). */
	float snapRow(float row) {
		if (params[SNAP_PARAM].getValue() < 0.5f)
			return row;
		float s = Engine::rowSemis(p, row);
		unsigned m = engine->mask(p);
		float q = m ? quantise(s, m) : std::round(s);
		return clampf(Engine::semisRow(p, q), 0.f, ROWS - 1);
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
		json_object_set_new(j, "glass", json_string(string::toBase64(canvas->c, sizeof(canvas->c)).c_str()));
		bank.toJson(j);
		fol.toJson(j);
		return j;
	}
	void dataFromJson(json_t* j) override {
		bank.selCode++;
		fol.fromJson(j);
		if (json_t* v = json_object_get(j, "ppqn"))
			ppqn = std::max(1, (int) json_integer_value(v));
		if (json_t* g = json_object_get(j, "glass")) {
			std::vector<uint8_t> d = string::fromBase64(json_string_value(g) ? json_string_value(g) : "");
			if (d.size() == sizeof(canvas->c))
				std::memcpy(canvas->c, d.data(), d.size());
		}
		bank.fromJson(j);
		canvasVer++;
		// the picture comes with the patch: a preset or BANK seen now must not paint over it
		lastPreset = bank.preset;
		lastBank = (int) std::round(params[BANK_PARAM].getValue());
	}
};

/** The glass: the picture (an image, row 0 at the bottom), the head, the keyboard edge; painted with the mouse. */
struct Display : OpaqueWidget {
	Glas* module = nullptr;
	int image = -1;
	NVGcontext* imgVg = nullptr;
	uint32_t shownVer = 0;
	std::vector<uint8_t> rgba;
	Vec pos, start;
	bool painting = false, erasing = false;
	float t = 0.f;
	Canvas demo;
	Display() {
		rgba.assign(COLS * ROWS * 4, 0);
		demo.paint(B_CHOIR, 7);
	}
	~Display() {
		if (image >= 0 && imgVg && APP && APP->window && APP->window->vg == imgVg)
			nvgDeleteImage(imgVg, image);
	}
	void step() override {
		OpaqueWidget::step();
		t += 1.f / 60.f;
		if (module)
			module->uiStep();
	}
	// ---- painting
	Vec cell(Vec p) { return Vec(clampf(p.x / box.size.x, 0.f, 1.f) * (COLS - 1), (1.f - clampf(p.y / box.size.y, 0.f, 1.f)) * (ROWS - 1)); }
	void brush(Vec p, bool dragging) {
		if (!module)
			return;
		Canvas& c = *module->canvas;
		Vec q = cell(p);
		float row = module->snapRow(q.y);
		float sz = module->params[Glas::SIZE_PARAM].getValue();
		bool snap = module->params[Glas::SNAP_PARAM].getValue() > 0.5f;
		float rc = 0.6f + sz * 4.f, rr = snap ? 0.7f : 0.8f + sz * 9.f;
		int tool = erasing ? T_ERASE : clamp((int) std::round(module->params[Glas::TOOL_PARAM].getValue()), 0, NUM_TOOLS - 1);
		switch (tool) {
			case T_PEN: c.blob(q.x, row, rc, rr, 0.32f); break;
			case T_SPRAY:
				for (int k = 0; k < 5; k++) {
					float a = random::uniform() * 6.2832f, d = std::sqrt(random::uniform());
					float r2 = module->snapRow(q.y + std::sin(a) * d * rr * 2.f);
					c.blob(q.x + std::cos(a) * d * rc * 2.f, r2, 0.5f, 0.6f, 0.3f);
				}
				break;
			case T_ERASE: c.blob(q.x, q.y, rc * 1.3f, rr * 1.3f + 1.f, -0.6f); break;
			default: break;  // LINE: at the end of the drag
		}
		module->canvasVer++;
	}
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && (e.button == GLFW_MOUSE_BUTTON_LEFT || e.button == GLFW_MOUSE_BUTTON_RIGHT) && module) {
			module->undo.push(module);
			pos = start = e.pos;
			painting = true;
			erasing = e.button == GLFW_MOUSE_BUTTON_RIGHT;
			brush(pos, false);
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}
	void onDragMove(const DragMoveEvent& e) override {
		if (!painting)
			return;
		Vec d = e.mouseDelta.div(getAbsoluteZoom());
		// fill the way in small steps, so a fast stroke stays a line
		int n = std::max(1, (int) std::ceil(std::max(std::fabs(d.x), std::fabs(d.y)) / 3.f));
		for (int i = 0; i < n; i++) {
			pos = pos.plus(d.div((float) n));
			brush(pos, true);
		}
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (painting && module && !erasing && (int) std::round(module->params[Glas::TOOL_PARAM].getValue()) == T_LINE) {
			Vec a = cell(start), b = cell(pos);
			float sz = module->params[Glas::SIZE_PARAM].getValue();
			bool snap = module->params[Glas::SNAP_PARAM].getValue() > 0.5f;
			float ra = module->snapRow(a.y), rb = module->snapRow(b.y);
			int n = std::max(1, (int) std::ceil(std::max(std::fabs(b.x - a.x), std::fabs(rb - ra)) * 2.f));
			for (int i = 0; i <= n; i++) {
				float tt = (float) i / n;
				float row = ra + (rb - ra) * tt;
				if (snap && std::fabs(rb - ra) < 0.01f)
					row = ra;
				module->canvas->blob(a.x + (b.x - a.x) * tt, row, 0.6f + sz * 2.f, snap ? 0.7f : 0.8f + sz * 5.f, 0.18f);
			}
			module->canvasVer++;
		}
		painting = false;
	}
	// ---- drawing
	void upload(NVGcontext* vg, const Canvas& c) {
		for (int y = 0; y < ROWS; y++) {
			int row = ROWS - 1 - y;
			float hy = (float) row / (ROWS - 1);
			NVGcolor col = nvgLerpRGBA(CYAN, PINK, hy);
			for (int x = 0; x < COLS; x++) {
				float v = c.get(x, row) / 255.f;
				uint8_t* px = &rgba[(y * COLS + x) * 4];
				float w = std::max(0.f, v - 0.7f) * 2.f;  // the brightest strokes go white
				px[0] = (uint8_t) clamp((int) ((col.r * (1.f - w) + w) * 255.f), 0, 255);
				px[1] = (uint8_t) clamp((int) ((col.g * (1.f - w) + w) * 255.f), 0, 255);
				px[2] = (uint8_t) clamp((int) ((col.b * (1.f - w) + w) * 255.f), 0, 255);
				px[3] = (uint8_t) clamp((int) (std::sqrt(v) * 255.f), 0, 255);
			}
		}
		if (image < 0 || imgVg != vg) {
			image = nvgCreateImageRGBA(vg, COLS, ROWS, 0, rgba.data());
			imgVg = vg;
		} else
			nvgUpdateImage(vg, image, rgba.data());
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			OpaqueWidget::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		float W = box.size.x, H = box.size.y;
		const Canvas& c = module ? *module->canvas : demo;
		bool rec = module && module->p.rec;
		uint32_t ver = module ? module->canvasVer.load() : 1;
		if (image < 0 || imgVg != vg || ver != shownVer || rec) {
			upload(vg, c);
			shownVer = ver;
		}
		nvgSave(vg);
		nvgScissor(vg, 2, 2, W - 4, H - 4);
		// faint octave lines
		for (int o = 1; o < 8; o++) {
			float y = H - H * o / 8.f;
			nvgBeginPath(vg);
			nvgMoveTo(vg, 0, y);
			nvgLineTo(vg, W, y);
			nvgStrokeColor(vg, alpha(VIOLET, 0.12f));
			nvgStrokeWidth(vg, 0.5f);
			nvgStroke(vg);
		}
		// the picture, with a soft glow under it
		NVGpaint ip = nvgImagePattern(vg, 0, 0, W, H, 0.f, image, 1.f);
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, W, H);
		nvgFillPaint(vg, ip);
		nvgFill(vg);
		// the head: a beam, and what it plays lights up
		float head = module ? module->engine->head : std::fmod(t / 6.f, 1.f);
		float hx = head * W;
		nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
		nvgScissor(vg, hx - 4.f, 2, 8.f, H - 4);
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, W, H);
		nvgFillPaint(vg, nvgImagePattern(vg, 0, 0, W, H, 0.f, image, 0.9f));
		nvgFill(vg);
		nvgGlobalCompositeOperation(vg, NVG_SOURCE_OVER);
		nvgScissor(vg, 2, 2, W - 4, H - 4);
		nvgBeginPath(vg);
		nvgRect(vg, hx - 1.2f, 0, 2.4f, H);
		nvgFillColor(vg, alpha(CYAN, rec ? 0.75f : 0.4f));
		nvgFill(vg);
		if (rec) {
			nvgBeginPath(vg);
			nvgCircle(vg, hx, 6.f, 3.f);
			nvgFillColor(vg, nvgRGB(0xff, 0x4f, 0x6a));
			nvgFill(vg);
		}
		// the keyboard at the left edge
		for (int k = 0; k < 96; k++) {
			float y = H - (k + 1) * H / 96.f;
			bool blk = (0x54a >> (k % 12)) & 1;
			nvgBeginPath(vg);
			nvgRect(vg, 0, y + 0.2f, blk ? 5.f : 8.f, H / 96.f - 0.4f);
			nvgFillColor(vg, blk ? nvgRGBA(42, 29, 69, 220) : nvgRGBA(61, 42, 102, 200));
			nvgFill(vg);
		}
		// the LINE being drawn
		if (painting && module && !erasing && (int) std::round(module->params[Glas::TOOL_PARAM].getValue()) == T_LINE) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, start.x, start.y);
			nvgLineTo(vg, pos.x, pos.y);
			glowStroke(vg, WHITE, 1.2f, 3.f);
		}
		scanlines(vg, W, H);
		nvgResetScissor(vg);
		std::string l1 = "4 BARS · GENRA CHORD", l2 = "PEN · SIZE 30 · FORWARD", big = "C2", small = "FREE · 120 BPM";
		if (module) {
			const Params& p = module->p;
			l1 = std::string(LEN_NAMES[p.len]) + " · " + SCALE_NAMES[p.scale];
			int tool = clamp((int) std::round(module->params[Glas::TOOL_PARAM].getValue()), 0, NUM_TOOLS - 1);
			l2 = string::f("%s · SIZE %d · %s%s", TOOL_NAMES[tool], (int) std::round(module->params[Glas::SIZE_PARAM].getValue() * 100.f),
			               DIR_NAMES[p.dir], module->params[Glas::SNAP_PARAM].getValue() > 0.5f ? " · SNAP" : "");
			big = noteName(lowSemis(p.low) / 12.f);
			small = followLine(module, module->inputs[Glas::CLOCK_INPUT].isConnected(), module->tempo.bpm);
			if (rec)
				small += " · REC";
		}
		corners(vg, W, l1, l2, big, small);
		nvgRestore(vg);
		OpaqueWidget::drawLayer(args, layer);
	}
};

struct GlasWidget : ModuleWidget {
	GlasWidget(Glas* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Glas.svg")));
		follow::knownHost(module);
		using M = Glas;
		Display* d = new Display;
		d->module = module;
		d->box.pos = mm2px(Vec(L::DISPLAY[0], L::DISPLAY[1]));
		d->box.size = mm2px(Vec(L::DISPLAY[2], L::DISPLAY[3]));
		addChild(d);
		presetField(this, module, L::PRESETF, module ? &module->bank : nullptr, M::PRESETSEL_PARAM, PRESETS[0].name);
		field(this, module, L::BANKF, M::BANK_PARAM, NUM_BANKS, BANK_NAMES, BANK_INFO, "Picture");
		field(this, module, L::LENF, M::LEN_PARAM, NUM_LENS, LEN_NAMES, nullptr, "Plate length");
		field(this, module, L::DIRF, M::DIR_PARAM, NUM_DIRS, DIR_NAMES, nullptr, "Direction");
		field(this, module, L::SCALEF, M::SCALE_PARAM, NUM_SCALES, SCALE_NAMES, SCALE_INFO, "Scale");
		knob(this, module, L::SIZE, M::SIZE_PARAM);
		knob(this, module, L::SPEED, M::SPEED_PARAM);
		knob(this, module, L::POS, M::POS_PARAM);
		knob(this, module, L::LOW, M::LOW_PARAM);
		knob(this, module, L::RNG, M::RNG_PARAM);
		knob(this, module, L::TILT, M::TILT_PARAM);
		knob(this, module, L::ATK, M::ATK_PARAM);
		knob(this, module, L::DEC, M::DEC_PARAM);
		knob(this, module, L::SUS, M::SUS_PARAM);
		knob(this, module, L::REL, M::REL_PARAM);
		knob(this, module, L::SPREAD, M::SPREAD_PARAM);
		knob(this, module, L::BLUR, M::BLUR_PARAM);
		knob(this, module, L::SHIMMER, M::SHIMMER_PARAM);
		lamps(this, module, L::TOOL, M::TOOL_PARAM, NUM_TOOLS, CYAN);
		lamps(this, module, L::PMODE, M::PMODE_PARAM, NUM_PLAYMODES, CYAN);
		button(this, module, L::SNAP, M::SNAP_PARAM, true, CYAN);
		NButton* nb = button(this, module, L::NEW, M::NEW_PARAM, false, PINK);
		nb->lit = [=]() { return module && module->flash > 0.f; };
		button(this, module, L::CLEAR, M::CLEAR_PARAM, false, PINK);
		button(this, module, L::REC, M::REC_PARAM, true, nvgRGB(0xff, 0x4f, 0x6a));
		button(this, module, L::UNDO, M::UNDO_PARAM, false, VIOLET);
		button(this, module, L::INIT, M::INIT_PARAM, false, VIOLET);
		jacks(this, module, L::JACK_IN, L::JACK_IN_COL, L::JACK_OUT);
	}
	void appendContextMenu(Menu* menu) override {
		if (follow::Host* h = follow::host(module))
			h->fol.appendMenu(menu, module);
		Glas* m = dynamic_cast<Glas*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		genra_presets::appendMenu(menu, &m->bank);
		menu->addChild(new MenuSeparator);
		clockMenu(menu, &m->ppqn);
		menu->addChild(createMenuLabel("Paint on the glass: left button the tool, right button erases"));
		menu->addChild(createMenuLabel("Follow GENRA's pads: SCALE GENRA CHORD plays the picture in GENRA's chords"));
	}
};

}  // namespace glas

Model* modelGlas = createModel<glas::Glas, glas::GlasWidget>("Glas");
