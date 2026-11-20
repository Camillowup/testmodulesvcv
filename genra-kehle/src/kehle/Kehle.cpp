// SPDX-License-Identifier: GPL-3.0-only
// KEHLE – the neon vocoder (test plugin of its own for now; joins GENRA Neon Punk once it works). The voice goes into
// MOD, a synth into CAR L/R (or the built-in carrier plays V/OCT + GATE: chords from GENRA via Follow). The display
// shows the bands as a neon landscape and a little figure for each CHARACTER that sings along with the voice.
// Engine: KehleDsp.hpp; panel: tools/kehle_panel.py (res/Kehle.svg + KehleLayout.hpp).
#include "../plugin.hpp"
#include "../neon/ModuleKit.hpp"
#include "KehleDsp.hpp"
#include "KehlePresets.hpp"
#include "KehleLayout.hpp"
#include <memory>

namespace kehle {

namespace L = kl;
using namespace neonui;
using namespace neonkit;

struct Kehle : Module, follow::Host {
	enum ParamId {
		CHAR_PARAM, BANDS_PARAM, FORMANT_PARAM, RESO_PARAM, SIBIL_PARAM, CAR_PARAM, OCT_PARAM, ENS_PARAM, ATK_PARAM,
		REL_PARAM, MIX_PARAM,
		NUM_PARAMS,
		PRESETSEL_PARAM = NUM_PARAMS,  // after the knobs: not in a preset
		FREEZE_PARAM, HOLD_PARAM, UNDO_PARAM, INIT_PARAM, LEVEL_PARAM,  // LEVEL: not in a preset
		PARAMS_LEN
	};
	// in the order of the panel (KehleLayout.hpp JACK_IN)
	enum InputId {
		CARL_INPUT, CARR_INPUT, MOD_INPUT, VOCT_INPUT, GATE_INPUT, VEL_INPUT, TFREEZE_INPUT, FORM_INPUT, RESO_INPUT,
		SIBIL_INPUT, ENS_INPUT, REL_INPUT, MIX_INPUT,
		INPUTS_LEN
	};
	enum OutputId { ENV_OUTPUT, L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };
	static_assert((int) NUM_PARAMS == (int) NUM_PRESET_PARAMS, "presets must cover every knob");
	static_assert(INPUTS_LEN == L::NUM_JACK_IN, "the panel's jacks");
	static_assert(OUTPUTS_LEN == L::NUM_JACK_OUT, "the panel's outputs");

	std::unique_ptr<Engine> engine{new Engine};
	Params p;
	int ctl = 0;
	dsp::SchmittTrigger freezeTrig, undoBtn, initBtn;
	std::atomic<uint32_t> undoReq{0}, initReq{0};
	uint32_t doneU = 0, doneI = 0;
	Undo undo;
	// the built-in carrier's notes: what plays now (HOLD keeps the last chord after the gates close)
	float notes[MAXV] = {};
	int nNotes = 0;
	bool anyGate = false;

	genra_presets::Bank bank;
	void initPresets() {
		genra_presets::Factory f;
		f.count = NUM_PRESETS;
		f.style = [](int i) { return PRESETS[i].style; };
		f.name = [](int i) { return PRESETS[i].name; };
		f.info = [](int i) { return CHAR_NAMES[(int) PRESETS[i].v[P_CHAR]]; };
		f.values = [](int i, float* v) { PRESETS[i].values(v); };
		f.gain = [](int i) { return PRESETS[i].gain; };
		bank.sel = PRESETSEL_PARAM;
		bank.init(this, f, asset::user("GenraKehle/KehlePresets"));
	}

	Kehle() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		genra_presel::config(this, PRESETSEL_PARAM);
		srcName = "KEHLE";
		audioL = L_OUTPUT;
		audioR = R_OUTPUT;
		fol.bind(VOCT_INPUT, follow::VOCT);
		fol.bind(GATE_INPUT, follow::GATE);
		fol.bind(VEL_INPUT, follow::VEL);
		std::vector<std::string> chars, cars;
		for (int i = 0; i < NUM_CHARS; i++)
			chars.push_back(CHAR_NAMES[i]);
		for (int i = 0; i < NUM_CARS; i++)
			cars.push_back(CAR_NAMES[i]);
		configSwitch(CHAR_PARAM, 0.f, NUM_CHARS - 1, 0.f, "Character", chars);
		configParam<BandsQuantity>(BANDS_PARAM, 0.f, 1.f, 0.6f, "Bands (more: clearer words, fewer: more robot)");
		configParam<FormantQuantity>(FORMANT_PARAM, 0.f, 1.f, 0.5f, "Formant (moves the carrier's bands: monster .. mouse)");
		configParam(RESO_PARAM, 0.f, 1.f, 0.4f, "Resonance (narrower bands: they ring and sing)", "%", 0.f, 100.f);
		configParam(SIBIL_PARAM, 0.f, 1.f, 0.5f, "Sibilance (the hiss of S, T, F comes through as noise)", "%", 0.f, 100.f);
		configSwitch(CAR_PARAM, 0.f, NUM_CARS - 1, 0.f, "Carrier (built in; a cable in CAR L takes its place)", cars);
		configParam<OctQuantity>(OCT_PARAM, 0.f, 1.f, 0.5f, "Octave of the built-in carrier");
		configParam(ENS_PARAM, 0.f, 1.f, 0.2f, "Ensemble (a wide chorus)", "%", 0.f, 100.f);
		configParam(ATK_PARAM, 0.f, 1.f, 0.15f, "Attack (how fast the bands open)", "%", 0.f, 100.f);
		configParam(REL_PARAM, 0.f, 1.f, 0.3f, "Release (how slowly they close)", "%", 0.f, 100.f);
		configParam(MIX_PARAM, 0.f, 1.f, 0.f, "Dry (the voice itself in the output)", "%", 0.f, 100.f);
		configParam(LEVEL_PARAM, 0.f, 1.f, 0.8f, "Level (not part of a preset)", "%", 0.f, 100.f);
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze: the bands stand still (the sound you are singing stays)", {"Off", "On"});
		configSwitch(HOLD_PARAM, 0.f, 1.f, 0.f, "Hold: the built-in carrier keeps the last chord when the gates close", {"Off", "On"});
		configButton(UNDO_PARAM, "Undo the last INIT");
		configButton(INIT_PARAM, "Init: back to a plain robot");
		for (int id : {UNDO_PARAM, INIT_PARAM, FREEZE_PARAM, HOLD_PARAM})
			paramQuantities[id]->randomizeEnabled = false;
		const char* inNames[INPUTS_LEN] = {
		    "Carrier left (a synth: its sound speaks; unplugged: the built-in carrier)",
		    "Carrier right (unplugged: the left one)", "Modulator (MOD: your voice, from Rack's Audio module)",
		    "V/Oct of the built-in carrier (polyphonic: chords)", "Gate (polyphonic; unplugged: the notes on V/OCT sound)",
		    "Velocity (0–10 V)", "Freeze trigger (toggles)", "Formant CV (±5 V: the whole range)", "Resonance CV",
		    "Sibilance CV", "Ensemble CV", "Release CV", "Dry CV"};
		for (int i = 0; i < INPUTS_LEN; i++)
			configInput(i, inNames[i]);
		configOutput(ENV_OUTPUT, "Envelope of the voice (0–10 V)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		configBypass(CARL_INPUT, L_OUTPUT);
		configBypass(CARR_INPUT, R_OUTPUT);
		initPresets();
	}

	struct BandsQuantity : ParamQuantity {
		std::string getDisplayValueString() override { return string::f("%d", bandCount(getValue())); }
	};
	struct OctQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			int o = octShift(getValue());
			return o > 0 ? string::f("+%d", o) : string::f("%d", o);
		}
	};
	struct FormantQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Kehle* k = dynamic_cast<Kehle*>(module);
			int ch = k ? (int) std::round(k->params[CHAR_PARAM].getValue()) : 0;
			float st = formantOct(getValue(), ch) * 12.f;
			return string::f("%+.1f", st);
		}
		std::string getUnit() override { return " semitones"; }
	};

	void onAdd(const AddEvent& e) override { engine->setSampleRate(APP->engine->getSampleRate()); }
	void onSampleRateChange(const SampleRateChangeEvent& e) override { engine->setSampleRate(e.sampleRate); }
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		engine->allOff();
		bank.reset();
	}
	void onRemove(const RemoveEvent& e) override { follow::gone(this); }
	void onPortChange(const PortChangeEvent& e) override { fol.portChange(e); }

	/** The built-in carrier's notes from V/OCT and GATE (HOLD keeps them; nothing patched: a C minor chord). */
	void updateNotes() {
		Input& vi = inputs[VOCT_INPUT];
		Input& gi = inputs[GATE_INPUT];
		int nv = std::min(vi.getChannels(), MAXV);
		if (!vi.isConnected() && !gi.isConnected()) {
			static const float DEF[3] = {-1.f, -1.f + 3.f / 12.f, -1.f + 7.f / 12.f};
			for (int i = 0; i < 3; i++)
				notes[i] = DEF[i];
			nNotes = 3;
			return;
		}
		if (!gi.isConnected()) {  // V/OCT alone: its notes sound
			for (int c = 0; c < nv; c++)
				notes[c] = vi.getPolyVoltage(c);
			nNotes = nv;
			return;
		}
		int ng = std::min(std::max(gi.getChannels(), nv), MAXV);
		float now[MAXV];
		int k = 0;
		for (int c = 0; c < ng; c++)
			if (gi.getPolyVoltage(c) >= 1.f)
				now[k++] = vi.getPolyVoltage(c);
		bool hold = params[HOLD_PARAM].getValue() > 0.5f;
		if (k > 0) {
			for (int i = 0; i < k; i++)
				notes[i] = now[i];
			nNotes = k;
		} else if (!hold)
			nNotes = 0;
		anyGate = k > 0;
	}

	void process(const ProcessArgs& args) override {
		fol.process(this, args.frame);
		if (undoBtn.process(params[UNDO_PARAM].getValue()))
			undoReq++;
		if (initBtn.process(params[INIT_PARAM].getValue()))
			initReq++;
		if (freezeTrig.process(inputs[TFREEZE_INPUT].getVoltage(), 0.1f, 1.f))
			params[FREEZE_PARAM].setValue(params[FREEZE_PARAM].getValue() > 0.5f ? 0.f : 1.f);
		if (--ctl <= 0) {
			ctl = 16;
			p.character = clamp((int) std::round(params[CHAR_PARAM].getValue()), 0, NUM_CHARS - 1);
			p.carrier = clamp((int) std::round(params[CAR_PARAM].getValue()), 0, NUM_CARS - 1);
			p.bands = params[BANDS_PARAM].getValue();
			p.formant = kv(this, FORMANT_PARAM, FORM_INPUT);
			p.reso = kv(this, RESO_PARAM, RESO_INPUT);
			p.sibil = kv(this, SIBIL_PARAM, SIBIL_INPUT);
			p.oct = params[OCT_PARAM].getValue();
			p.ens = kv(this, ENS_PARAM, ENS_INPUT);
			p.atk = params[ATK_PARAM].getValue();
			p.rel = kv(this, REL_PARAM, REL_INPUT);
			p.dry = kv(this, MIX_PARAM, MIX_INPUT);
			p.level = params[LEVEL_PARAM].getValue();
			p.outGain = bank.gain;
			p.freeze = params[FREEZE_PARAM].getValue() > 0.5f;
			updateNotes();
		}
		bool ext = inputs[CARL_INPUT].isConnected();
		float cl = inputs[CARL_INPUT].getVoltageSum();
		float cr = inputs[CARR_INPUT].isConnected() ? inputs[CARR_INPUT].getVoltageSum() : cl;
		float mod = inputs[MOD_INPUT].getVoltageSum();
		float l, r, env;
		engine->process(p, notes, nNotes, ext, cl, cr, mod, l, r, env);
		outputs[L_OUTPUT].setVoltage(outputs[R_OUTPUT].isConnected() || pulled(args.frame) ? l : 0.5f * (l + r));
		outputs[R_OUTPUT].setVoltage(r);
		outputs[ENV_OUTPUT].setVoltage(10.f * env);
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
		bank.toJson(j);
		fol.toJson(j);
		return j;
	}
	void dataFromJson(json_t* j) override {
		bank.selCode++;
		fol.fromJson(j);
		bank.fromJson(j);
	}
};

// ---------------------------------------------------------------- the figures (one per CHARACTER)
/** Draws in the units of the sketch (a 176 × 114 tile, the figure around 88, 48), scaled to the display. */
struct Pen {
	NVGcontext* vg;
	float cx, cy, s;
	float X(float x) const { return cx + (x - 88.f) * s; }
	float Y(float y) const { return cy + (y - 48.f) * s; }
	void stroke(NVGcolor c, float w = 1.8f) { glowStroke(vg, c, w * s * 0.9f, 2.6f); }
	void line(float x1, float y1, float x2, float y2, NVGcolor c, float w = 1.8f) {
		nvgBeginPath(vg);
		nvgMoveTo(vg, X(x1), Y(y1));
		nvgLineTo(vg, X(x2), Y(y2));
		stroke(c, w);
	}
	void circle(float x, float y, float r, NVGcolor c, float w = 1.8f) {
		nvgBeginPath(vg);
		nvgCircle(vg, X(x), Y(y), r * s);
		stroke(c, w);
	}
	void ellipse(float x, float y, float rx, float ry, NVGcolor c, float w = 1.8f) {
		nvgBeginPath(vg);
		nvgEllipse(vg, X(x), Y(y), rx * s, std::max(ry, 0.4f) * s);
		stroke(c, w);
	}
	void rect(float x, float y, float w, float h, float r, NVGcolor c, float sw = 1.8f) {
		nvgBeginPath(vg);
		nvgRoundedRect(vg, X(x), Y(y), w * s, h * s, r * s);
		stroke(c, sw);
	}
	void fillRect(float x, float y, float w, float h, NVGcolor c) {
		nvgBeginPath(vg);
		nvgRect(vg, X(x), Y(y), w * s, h * s);
		nvgFillColor(vg, c);
		nvgFill(vg);
	}
};

static const NVGcolor PALE = nvgRGB(0xc9, 0xd6, 0xff), SOFTV = nvgRGB(0xa0, 0x70, 0xe0);

/** m: how wide the mouth is open (0..1), t: seconds, form: the FORMANT knob (SHIFT: monster .. mouse). */
static void drawFigure(Pen& P, int ch, float m, float t, float form, bool frozen) {
	NVGcontext* vg = P.vg;
	switch (ch) {
		case CH_CLASSIC: {  // the robot: the antenna blinks, the grille opens
			P.line(88, 14, 88, 24, CYAN);
			float blink = frozen ? 1.f : 0.5f + 0.5f * std::sin(t * 6.f);
			P.circle(88, 11, 3, alpha(PINK, 0.4f + 0.6f * blink));
			P.rect(64, 24, 48, 40, 6, CYAN);
			P.circle(77, 40, 5, CYAN);
			P.circle(99, 40, 5, CYAN);
			float h = 3.f + 9.f * m;
			P.rect(74, 51, 28, h, 1, PINK);
			for (float x : {81.f, 88.f, 95.f})
				P.line(x, 51, x, 51 + h, PINK, 1.2f);
			P.line(59, 40, 64, 40, CYAN);
			P.line(112, 40, 117, 40, CYAN);
			P.rect(72, 66, 32, 18, 3, SOFTV);
			break;
		}
		case CH_WHISPER: {  // the ghost floats
			float dy = frozen ? 0.f : 3.f * std::sin(t * 1.7f);
			nvgBeginPath(vg);
			nvgMoveTo(vg, P.X(66), P.Y(82 + dy));
			nvgLineTo(vg, P.X(66), P.Y(40 + dy));
			nvgQuadTo(vg, P.X(66), P.Y(16 + dy), P.X(88), P.Y(16 + dy));
			nvgQuadTo(vg, P.X(110), P.Y(16 + dy), P.X(110), P.Y(40 + dy));
			nvgLineTo(vg, P.X(110), P.Y(82 + dy));
			float w = frozen ? 0.f : 2.f * std::sin(t * 4.f);
			nvgLineTo(vg, P.X(103), P.Y(76 + dy + w));
			nvgLineTo(vg, P.X(96), P.Y(82 + dy));
			nvgLineTo(vg, P.X(88), P.Y(76 + dy - w));
			nvgLineTo(vg, P.X(80), P.Y(82 + dy));
			nvgLineTo(vg, P.X(73), P.Y(76 + dy + w));
			nvgClosePath(vg);
			P.stroke(PALE);
			P.circle(80, 40 + dy, 3, CYAN);
			P.circle(96, 40 + dy, 3, CYAN);
			P.ellipse(88, 56 + dy, 4, 1.5f + 6.f * m, PINK);
			for (int k = 0; k < 2; k++) {  // the breath
				float o = k * 8.f, a = 0.3f + 0.7f * m;
				nvgBeginPath(vg);
				nvgMoveTo(vg, P.X(118 + o), P.Y(32 + dy - o / 2));
				nvgQuadTo(vg, P.X(124 + o), P.Y(36 + dy - o / 2), P.X(118 + o), P.Y(40 + dy - o / 2));
				nvgQuadTo(vg, P.X(112 + o), P.Y(44 + dy - o / 2), P.X(118 + o), P.Y(48 + dy - o / 2));
				P.stroke(alpha(k ? VIOLET : SOFTV, a));
			}
			break;
		}
		case CH_CHOIR: {  // three heads, they take turns
			const float X[3] = {56, 88, 120}, Yc[3] = {50, 38, 50}, R[3] = {15, 17, 15};
			const NVGcolor C[3] = {PINK, CYAN, SOFTV};
			for (int k = 0; k < 3; k++) {
				float mk = frozen ? m : m * (0.55f + 0.45f * std::sin(t * 3.f + k * 2.1f));
				P.circle(X[k], Yc[k], R[k], C[k]);
				P.circle(X[k] - 5, Yc[k] - 4, 2, C[k]);
				P.circle(X[k] + 5, Yc[k] - 4, 2, C[k]);
				P.ellipse(X[k], Yc[k] + 6, 4, 1.f + 6.f * mk, C[k]);
			}
			nvgBeginPath(vg);
			nvgMoveTo(vg, P.X(40), P.Y(84));
			nvgQuadTo(vg, P.X(88), P.Y(66), P.X(136), P.Y(84));
			P.stroke(VIOLET);
			break;
		}
		case CH_METAL: {  // the tin can: the jaw drops
			P.ellipse(88, 20, 24, 5, PALE);
			P.line(64, 20, 64, 66, PALE);
			P.line(112, 20, 112, 66, PALE);
			nvgBeginPath(vg);
			nvgMoveTo(vg, P.X(64), P.Y(66));
			nvgQuadTo(vg, P.X(88), P.Y(74), P.X(112), P.Y(66));
			P.stroke(PALE);
			P.line(72, 38, 82, 38, CYAN, 3.f);
			P.line(94, 38, 104, 38, CYAN, 3.f);
			float j = 52.f + 4.f + 9.f * m;
			nvgBeginPath(vg);
			nvgMoveTo(vg, P.X(74), P.Y(52));
			nvgLineTo(vg, P.X(102), P.Y(52));
			nvgLineTo(vg, P.X(98), P.Y(j));
			nvgLineTo(vg, P.X(78), P.Y(j));
			nvgClosePath(vg);
			P.stroke(PINK);
			for (float x : {68.f, 108.f})
				for (float y : {28.f, 58.f})
					P.circle(x, y, 1.5f, SOFTV, 1.2f);
			float sp = m * (0.6f + 0.4f * std::sin(t * 20.f));
			P.line(120, 30, 120 + 8 * sp, 26, PINK);
			P.line(122, 42, 122 + 9 * sp, 42, PINK);
			P.line(120, 54, 120 + 8 * sp, 58, PINK);
			break;
		}
		case CH_GLASS: {  // the crystal sparkles
			nvgBeginPath(vg);
			nvgMoveTo(vg, P.X(88), P.Y(12));
			nvgLineTo(vg, P.X(114), P.Y(36));
			nvgLineTo(vg, P.X(88), P.Y(84));
			nvgLineTo(vg, P.X(62), P.Y(36));
			nvgClosePath(vg);
			P.stroke(CYAN);
			P.line(62, 36, 114, 36, alpha(CYAN, 0.6f), 1.f);
			P.line(88, 12, 76, 36, alpha(CYAN, 0.6f), 1.f);
			P.line(88, 12, 100, 36, alpha(CYAN, 0.6f), 1.f);
			P.line(76, 36, 88, 84, alpha(CYAN, 0.6f), 1.f);
			P.line(100, 36, 88, 84, alpha(CYAN, 0.6f), 1.f);
			P.circle(80, 44, 2.5f, PINK);
			P.circle(96, 44, 2.5f, PINK);
			if (m > 0.08f)
				P.ellipse(88, 57, 3, 1.f + 5.f * m, PINK);
			else {
				nvgBeginPath(vg);
				nvgMoveTo(vg, P.X(83), P.Y(56));
				nvgQuadTo(vg, P.X(88), P.Y(61), P.X(93), P.Y(56));
				P.stroke(PINK);
			}
			const float SX[4] = {126, 46, 129, 50}, SY[4] = {22, 58, 64, 20};
			for (int k = 0; k < 4; k++) {
				float a = frozen ? 0.8f : 0.5f + 0.5f * std::sin(t * 5.f + k * 1.9f);
				float r = 2.f + 2.f * a * (0.5f + m);
				P.line(SX[k] - r, SY[k], SX[k] + r, SY[k], alpha(WHITE, a), 1.2f);
				P.line(SX[k], SY[k] - r, SX[k], SY[k] + r, alpha(WHITE, a), 1.2f);
			}
			break;
		}
		case CH_SHIFT: {  // monster <> mouse: FORMANT decides who is lit
			float mo = clamp(1.f - form * 1.6f + 0.3f, 0.25f, 1.f), mu = clamp(form * 1.6f - 0.5f, 0.25f, 1.f);
			NVGcolor a = alpha(PINK, mo), b = alpha(CYAN, mo);
			P.line(28, 34, 34, 18, a);
			P.line(34, 18, 42, 30, a);
			P.line(66, 30, 74, 18, a);
			P.line(74, 18, 80, 34, a);
			P.circle(54, 50, 24, a);
			P.circle(45, 44, 3, b);
			P.circle(63, 44, 3, b);
			float z = 3.f + 7.f * m * mo;
			nvgBeginPath(vg);
			nvgMoveTo(vg, P.X(40), P.Y(60));
			nvgLineTo(vg, P.X(46), P.Y(60 + z));
			nvgLineTo(vg, P.X(52), P.Y(60));
			nvgLineTo(vg, P.X(58), P.Y(60 + z));
			nvgLineTo(vg, P.X(64), P.Y(60));
			nvgLineTo(vg, P.X(68), P.Y(60 + z * 0.6f));
			P.stroke(a);
			P.line(88, 50, 100, 50, VIOLET);
			P.line(96, 46, 100, 50, VIOLET);
			P.line(96, 54, 100, 50, VIOLET);
			NVGcolor c = alpha(CYAN, mu), d = alpha(PINK, mu);
			P.circle(120, 34, 8, c);
			P.circle(148, 34, 8, c);
			P.circle(134, 52, 14, c);
			P.circle(129, 49, 1.5f, c);
			P.circle(139, 49, 1.5f, c);
			P.circle(134, 57, 1.5f + 2.5f * m * mu, d);
			break;
		}
		case CH_TALKBOX: {  // the funk tube wobbles
			P.circle(72, 52, 22, CYAN);
			nvgBeginPath(vg);
			nvgArc(vg, P.X(72), P.Y(52), 22 * P.s, -2.6f, -0.55f, NVG_CW);
			P.stroke(SOFTV, 5.f);
			P.rect(62, 44, 8, 4, 1, PINK);
			P.rect(76, 44, 8, 4, 1, PINK);
			P.line(70, 46, 76, 46, PINK);
			float r = 2.f + 3.f * m;
			P.circle(78, 62, r, PINK);
			float w = frozen ? 0.f : std::sin(t * 6.f) * (2.f + 5.f * m);
			nvgBeginPath(vg);
			nvgMoveTo(vg, P.X(78 + r), P.Y(62));
			nvgBezierTo(vg, P.X(104), P.Y(68 + w), P.X(112 + w), P.Y(40), P.X(112), P.Y(34));
			nvgQuadTo(vg, P.X(112), P.Y(22), P.X(126), P.Y(22));
			P.stroke(PALE);
			P.rect(126, 14, 22, 16, 3, CYAN);
			P.circle(137, 22, 3, alpha(PINK, 0.5f + 0.5f * m));
			break;
		}
	}
}

/** The display: the bands as a neon landscape (older ones far away), the figure in front, singing along. */
struct Display : Widget {
	Kehle* module = nullptr;
	static const int HIST = 14;
	float hist[HIST][MAXB] = {};
	int head = 0, tick = 0, n = 16;
	float t = 0.f, mouth = 0.f;
	void step() override {
		Widget::step();
		t += 1.f / 60.f;
		if (module)
			module->uiStep();
		float lvl;
		if (module) {
			n = std::max(2, module->engine->showN);
			lvl = module->engine->showLevel;
		} else {
			n = 16;
			lvl = 0.5f + 0.5f * std::sin(t * 7.f) * std::sin(t * 1.3f);
		}
		mouth += (lvl - mouth) * 0.5f;
		if (++tick >= 3) {
			tick = 0;
			head = (head + 1) % HIST;
			for (int i = 0; i < n; i++) {
				float v;
				if (module) {
					float tot = 0.f;
					for (int k = 0; k < n; k++)
						tot += module->engine->showBand[k];
					v = tot > 1e-5f ? module->engine->showBand[i] / tot * n * 0.5f : 0.f;
					v *= std::min(1.f, module->engine->showLevel * 3.f);
				} else
					v = (0.5f + 0.5f * std::sin(i * 0.7f + t * 3.f)) * (0.4f + 0.6f * std::sin((float) M_PI * i / (n - 1)));
				hist[head][i] = clamp(v, 0.f, 1.4f);
			}
		}
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
		// the landscape: older band shapes far away (cyan), the newest at the front (pink)
		float hz = H * 0.42f;
		for (int k = HIST - 1; k >= 0; k--) {
			const float* s = hist[(head - k + HIST) % HIST];
			float f = 1.f - (float) k / (HIST - 1);
			float y0 = hz + (H - hz) * (0.1f + 0.82f * std::pow(f, 1.3f));
			float half = W * (0.22f + 0.27f * f), amp = mm2px(2.f + 9.f * f);
			nvgBeginPath(vg);
			for (int i = 0; i < n; i++) {
				float x = W / 2 - half + 2.f * half * i / (n - 1), y = y0 - s[i] * amp;
				if (i == 0)
					nvgMoveTo(vg, x, y);
				else
					nvgLineTo(vg, x, y);
			}
			NVGcolor c = nvgLerpRGBA(CYAN, PINK, f);
			if (k == 0)
				glowStroke(vg, c, 1.6f, 3.f);
			else {
				nvgStrokeColor(vg, alpha(c, 0.15f + 0.55f * f));
				nvgStrokeWidth(vg, 0.5f + 0.8f * f);
				nvgStroke(vg);
			}
		}
		// the figure, on a dark halo so the landscape does not run through it
		int ch = 0;
		float form = 0.5f;
		bool frozen = false;
		if (module) {
			ch = module->p.character;
			form = module->p.formant;
			frozen = module->p.freeze;
		} else
			ch = ((int) (t / 3.f)) % NUM_CHARS;
		Pen P{vg, W / 2, H * 0.6f, H / 135.f};
		nvgBeginPath(vg);
		nvgEllipse(vg, P.cx, P.cy, 72.f * P.s, 46.f * P.s);
		nvgFillPaint(vg, nvgRadialGradient(vg, P.cx, P.cy, 30.f * P.s, 72.f * P.s, alpha(DEEP, 0.9f), alpha(DEEP, 0.f)));
		nvgFill(vg);
		drawFigure(P, ch, clamp(mouth, 0.f, 1.f), t, form, frozen);
		scanlines(vg, W, H);
		nvgResetScissor(vg);
		std::string l1 = "CLASSIC · SAW CHORD", l2 = "16 BANDS · FORMANT 0", big = "C3", small = "FREE";
		if (module) {
			const Params& p = module->p;
			bool ext = module->engine->showExt;
			l1 = std::string(CHAR_NAMES[clamp(p.character, 0, NUM_CHARS - 1)]) + " · " +
			     (ext ? "EXT CARRIER" : CAR_NAMES[clamp(p.carrier, 0, NUM_CARS - 1)]);
			l2 = string::f("%d BANDS · FORMANT %+.0f", bandCount(p.bands), formantOct(p.formant, p.character) * 12.f);
			big = ext ? "EXT" : (module->nNotes > 0 ? noteName(module->engine->showNote) : "–");
			if (module->fol.active())
				small = followLine(module, false, 120.f);
			else
				small = module->inputs[Kehle::MOD_INPUT].isConnected() ? "VOICE IN" : "PATCH YOUR VOICE TO MOD";
			if (p.freeze)
				small += " · FREEZE";
			if (module->params[Kehle::HOLD_PARAM].getValue() > 0.5f)
				small += " · HOLD";
		}
		corners(vg, W, l1, l2, big, small);
		nvgRestore(vg);
		Widget::drawLayer(args, layer);
	}
};

struct KehleWidget : ModuleWidget {
	KehleWidget(Kehle* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Kehle.svg")));
		follow::knownHost(module);
		using M = Kehle;
		Display* d = new Display;
		d->module = module;
		d->box.pos = mm2px(Vec(L::DISPLAY[0], L::DISPLAY[1]));
		d->box.size = mm2px(Vec(L::DISPLAY[2], L::DISPLAY[3]));
		addChild(d);
		presetField(this, module, L::PRESETF, module ? &module->bank : nullptr, M::PRESETSEL_PARAM, PRESETS[0].name);
		field(this, module, L::CHARF, M::CHAR_PARAM, NUM_CHARS, CHAR_NAMES, CHAR_INFO, "Character");
		field(this, module, L::CARF, M::CAR_PARAM, NUM_CARS, CAR_NAMES, CAR_INFO, "Carrier");
		knob(this, module, L::BANDS, M::BANDS_PARAM);
		knob(this, module, L::FORMANT, M::FORMANT_PARAM);
		knob(this, module, L::RESO, M::RESO_PARAM);
		knob(this, module, L::SIBIL, M::SIBIL_PARAM);
		knob(this, module, L::OCT, M::OCT_PARAM);
		knob(this, module, L::ENS, M::ENS_PARAM);
		knob(this, module, L::ATK, M::ATK_PARAM);
		knob(this, module, L::REL, M::REL_PARAM);
		knob(this, module, L::MIX, M::MIX_PARAM);
		knob(this, module, L::LEVEL, M::LEVEL_PARAM);
		button(this, module, L::FREEZE, M::FREEZE_PARAM, true, CYAN);
		button(this, module, L::HOLD, M::HOLD_PARAM, true, PINK);
		button(this, module, L::UNDO, M::UNDO_PARAM, false, VIOLET);
		button(this, module, L::INIT, M::INIT_PARAM, false, VIOLET);
		for (int i = 0; i < L::NUM_JACK_IN; i++)
			addPort<NPort>(this, L::JACK_IN[i], module, i, false, hex(L::JACK_IN_COL[i]));
		for (int i = 0; i < L::NUM_JACK_OUT; i++)
			addPort<NPort>(this, L::JACK_OUT[i], module, i, true, PINK);
		follow::addStrip(this, module, {mm(L::JACK_IN[M::VOCT_INPUT]), mm(L::JACK_IN[M::GATE_INPUT]), mm(L::JACK_IN[M::VEL_INPUT])});
	}
	void appendContextMenu(Menu* menu) override {
		if (follow::Host* h = follow::host(module))
			h->fol.appendMenu(menu, module);
		Kehle* m = dynamic_cast<Kehle*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		genra_presets::appendMenu(menu, &m->bank);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Voice into MOD (Rack's Audio module: your mic input), a synth into CAR L/R"));
		menu->addChild(createMenuLabel("Nothing in CAR: the built-in carrier plays V/OCT + GATE (Follow GENRA: its pads)"));
	}
};

}  // namespace kehle

Model* modelKehle = createModel<kehle::Kehle, kehle::KehleWidget>("Kehle");
